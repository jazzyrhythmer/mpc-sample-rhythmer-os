// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
//
// The stem separator Rhythmer runs beside itself (src/rhythmer/shell/stems.h,
// tools/stems/build.py): Spleeter's 4-stem separation as Deezer's own program
// does it, on SpleeterRT's network and STFT code.
//
//   rhythmer-stems <model> <threads> <file.wav> [best]
//
// <model> is Deezer's 4-stem model as tools/stems/convert_model.py writes it.
// <file.wav> is stereo (or mono) at 44.1 kHz. It writes <file.wav>_Drums.wav,
// _Bass.wav, _Music.wav and _Vocals.wav beside it (44.1 kHz, 32-bit float,
// stereo, the input's length to the frame) and prints "progress N" (0 to 100)
// as it goes.
//
// What it does, and SpleeterRT's own offline program does not: that one holds
// a drums and a vocals network, multiplies the sound by each one's mask and
// calls what is left the music. Here each of the four stems has its network
// (vocals, drums, bass, other), each estimates its stem from the whole mix,
// and every bin of the mix is then shared between the four by the squares of
// their estimates (the ratio mask), so the stems add up to the mix and a
// stem's network never has the last word alone. The networks see 512 frames
// (about 12 s) a pass, as they were trained. Above the highest bin they see,
// each frame is shared as its top quarter below was.
//
// Without "best": 1024 bins (up to 11 kHz, Deezer's 4stems settings), one
// pass over the sound. With "best", about three times as long:
//   - the networks see 1536 bins, up to 16 kHz, as Deezer's 4stems-16kHz
//     settings have them (the same weights: the network is convolutional
//     along the bins), so the stems' highs between 11 and 16 kHz are their
//     own, not their share of the band below;
//   - two passes, the second half a pass (256 frames) after the first, each
//     frame's estimate the two passes' crossfaded (each counted the less the
//     nearer its pass's edge), so no seam is left where a pass ends.
// Tried on TAKE 13 and left out, scored against Demucs v4's stems of it: four
// passes (no better than two), each network also run with left and right
// swapped (no better), and one step of the multichannel Wiener filter as
// Deezer's --mwf does it (0.7 to 1 dB worse on every stem).
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include "spleeter.h"
#include "stftFix.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "dr_wav.h"
#pragma GCC diagnostic pop

void openblas_set_num_threads(int threads);

enum { STEMS = 4, FRAMES = 512, RATE = 44100, MOST_PASSES = 2 };
static const char MAGIC[4] = {'M', 'L', 'S', 'T'};
// The files in the order Rhythmer lands them, and the network of the model each is (vocals, drums, bass, other).
static const char* const stem_file[STEMS] = {"_Drums.wav", "_Bass.wav", "_Music.wav", "_Vocals.wav"};
static const int stem_net[STEMS] = {1, 2, 3, 0};

// The settings: Deezer's 4stems, or (best) its 4stems-16kHz in two passes.
static size_t bins = 1024;   // the bins the networks see: 1024 up to 11 kHz, 1536 up to 16 kHz
static size_t passes = 1;    // 1, or 2 half a pass apart
static size_t step = FRAMES; // frames from one pass's start to the next one's: FRAMES / passes

static void fail(const char* what) {
    fprintf(stderr, "rhythmer-stems: %s\n", what);
    exit(1);
}

static void progress(int percent) { printf("progress %d\n", percent); }

// An IEEE 754 half float; its denormals read as zero, as SpleeterRT reads its own.
static float half(uint16_t in) {
    uint32_t bits = (uint32_t)(in & 0x7fff) << 13;
    bits += 0x38000000u;
    if (!(in & 0x7c00)) bits = 0;
    bits |= (uint32_t)(in & 0x8000) << 16;
    float out;
    memcpy(&out, &bits, sizeof out);
    return out;
}

// The model's four networks as floats, each a spleeterCoeff.
static float* load_model(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) fail("cannot open the model");
    unsigned char head[16];
    uint32_t words[3];
    if (fread(head, 1, sizeof head, file) != sizeof head || memcmp(head, MAGIC, 4)) fail("not a model file");
    memcpy(words, head + 4, sizeof words);
    const size_t weights = getCoeffSize() / sizeof(float);
    if (words[0] != 1 || words[1] != STEMS || words[2] != weights) fail("the model is not the 4-stem one");
    float* out = (float*)malloc(STEMS * weights * sizeof(float));
    uint16_t* piece = (uint16_t*)malloc(65536 * sizeof(uint16_t));
    if (!out || !piece) fail("out of memory");
    for (size_t at = 0; at < STEMS * weights;) {
        size_t want = STEMS * weights - at;
        if (want > 65536) want = 65536;
        if (fread(piece, sizeof(uint16_t), want, file) != want) fail("the model is cut short");
        for (size_t n = 0; n < want; n++) out[at + n] = half(piece[n]);
        at += want;
    }
    free(piece);
    fclose(file);
    return out;
}

// One job: the mix's spectrum, the networks, and the masks as they are worked out.
//
// A run is the four networks on FRAMES frames of the mix; runs start every `step` frames, the first (passes - 1)
// steps before the sound. A segment is `step` frames of the sound: segment s lies in the runs s to s + passes - 1,
// and run r holds the segments r - passes + 1 to r. A segment is shared out as soon as all its runs' four networks
// are in; a run's masks go once all its segments are.
typedef struct {
    size_t frames;                 // of spectrum
    float *re[2], *im[2];          // the mix: frames x FFTSIZE each, left and right
    const float* model;
    size_t runs, segments, next, done; // a task is one network on one run: STEMS a run
    float* (*estimates)[STEMS];    // run, network: the network's own mask, side x FRAMES x bins
    unsigned char* in;             // run: its networks in so far
    unsigned char* open;           // run: its segments not yet shared out
    unsigned char* taken;          // segment: a thread is sharing it out, or has
    uint16_t* share[STEMS];        // network: its share of each bin, frames x 2 sides x bins, 0..65535
    pthread_mutex_t lock;
} Job;

static long run_start(size_t run) { return (long)(run * step) - (long)((passes - 1) * step); }

// How much a run's estimate counts at `at` frames into it: all of it with one pass; with two, the less the nearer
// either edge, so the two runs that hold a frame add up to one.
static float weight(size_t at) {
    if (passes == 1) return 1.0f;
    return 1.0f - fabsf(2.0f * (float)at + 1.0f - (float)FRAMES) / (float)FRAMES;
}

static int segment_ready(const Job* job, size_t segment) {
    for (size_t r = segment; r < segment + passes; r++)
        if (job->in[r] != STEMS) return 0;
    return 1;
}

// A segment's masks into each network's share of every bin: the squares of what each estimates, over their sum.
static void share_segment(Job* job, size_t segment) {
    const size_t first = segment * step;
    size_t last = first + step;
    if (last > job->frames) last = job->frames;
    for (size_t t = first; t < last; t++) {
        // The runs that hold this frame, where in each it is, and how much each counts.
        const float* from[MOST_PASSES][STEMS];
        float part[MOST_PASSES];
        float total = 0;
        for (size_t c = 0; c < passes; c++) {
            const size_t run = segment + c, at = (size_t)((long)t - run_start(run));
            part[c] = weight(at);
            total += part[c];
            for (int n = 0; n < STEMS; n++) from[c][n] = job->estimates[run][n] + at * bins;
        }
        for (size_t c = 0; c < passes; c++) part[c] /= total;
        for (int side = 0; side < 2; side++) {
            const float* re = job->re[side] + t * FFTSIZE;
            const float* im = job->im[side] + t * FFTSIZE;
            const size_t in = (size_t)side * FRAMES * bins, out = (t * 2 + (size_t)side) * bins;
            for (size_t bin = 0; bin < bins; bin++) {
                const float level = hypotf(re[bin], im[bin]) * (float)FFTSIZE, power = level * level;
                float each[STEMS], sum = 1e-10f;
                for (int n = 0; n < STEMS; n++) {
                    float mask = 0;
                    for (size_t c = 0; c < passes; c++) mask += part[c] * from[c][n][in + bin];
                    each[n] = power * mask * mask + 1e-10f / STEMS;
                    sum += power * mask * mask;
                }
                for (int n = 0; n < STEMS; n++) job->share[n][out + bin] = (uint16_t)lrintf(65535.0f * each[n] / sum);
            }
        }
    }
}

static void* work(void* given) {
    Job* job = (Job*)given;
    float* level = (float*)malloc(2 * FRAMES * bins * sizeof(float));
    if (!level) fail("out of memory");
    for (;;) {
        pthread_mutex_lock(&job->lock);
        const size_t task = job->next++;
        pthread_mutex_unlock(&job->lock);
        if (task >= job->runs * STEMS) break;
        const size_t run = task / STEMS;
        const long first = run_start(run);
        const int net = (int)(task % STEMS);
        // The run's levels, as the networks were trained on them: side, frame, bin. Outside the sound: silence.
        for (int side = 0; side < 2; side++)
            for (size_t t = 0; t < FRAMES; t++) {
                float* row = level + (size_t)side * FRAMES * bins + t * bins;
                const long frame = first + (long)t;
                if (frame < 0 || (size_t)frame >= job->frames) { memset(row, 0, bins * sizeof(float)); continue; }
                const float* re = job->re[side] + (size_t)frame * FFTSIZE;
                const float* im = job->im[side] + (size_t)frame * FFTSIZE;
                for (size_t bin = 0; bin < bins; bin++) row[bin] = hypotf(re[bin], im[bin]) * (float)FFTSIZE;
            }
        spleeter nn = (spleeter)allocateSpleeterStr();
        float* mask = NULL;
        float* kept = (float*)malloc(2 * FRAMES * bins * sizeof(float));
        if (!nn || !kept) fail("out of memory");
        initSpleeter(nn, bins, FRAMES, 1, (void*)(job->model + (size_t)net * (getCoeffSize() / sizeof(float))));
        getMaskPtr(nn, &mask);
        processSpleeter(nn, level, mask);
        memcpy(kept, mask, 2 * FRAMES * bins * sizeof(float));
        freeSpleeter(nn);
        free(nn);
        // The segments this run completes: each is shared out by the thread that completes it, alone.
        size_t ready[MOST_PASSES], claimed = 0;
        pthread_mutex_lock(&job->lock);
        job->estimates[run][net] = kept;
        if (++job->in[run] == STEMS) {
            const size_t low = run + 1 >= passes ? run + 1 - passes : 0;
            for (size_t s = low; s <= run && s < job->segments; s++)
                if (!job->taken[s] && segment_ready(job, s)) {
                    job->taken[s] = 1;
                    ready[claimed++] = s;
                }
        }
        progress(8 + (int)(80 * ++job->done / (job->runs * STEMS)));
        pthread_mutex_unlock(&job->lock);
        for (size_t c = 0; c < claimed; c++) share_segment(job, ready[c]);
        if (claimed) {
            pthread_mutex_lock(&job->lock);
            for (size_t c = 0; c < claimed; c++)
                for (size_t r = ready[c]; r < ready[c] + passes; r++)
                    if (--job->open[r] == 0)
                        for (int n = 0; n < STEMS; n++) {
                            free(job->estimates[r][n]);
                            job->estimates[r][n] = NULL;
                        }
            pthread_mutex_unlock(&job->lock);
        }
    }
    free(level);
    return NULL;
}

// As a background job on the MPC: it ends with Rhythmer, holds none of its open files, runs below everything Rhythmer
// runs, and is the first thing the kernel ends when memory runs out.
static void settle(void) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) exit(4);
    for (int fd = 3; fd < 1024; fd++) close(fd);
    setvbuf(stdout, NULL, _IOLBF, 0); // a line as it is printed: a pipe would hold them back to the end
    struct sched_param normal = {0};
    sched_setscheduler(0, SCHED_OTHER, &normal);
    setpriority(PRIO_PROCESS, 0, 19);
    FILE* oom = fopen("/proc/self/oom_score_adj", "w");
    if (oom) {
        fputs("1000\n", oom);
        fclose(oom);
    }
}

int main(int argc, char* argv[]) {
    settle();
    if (argc != 4 && !(argc == 5 && !strcmp(argv[4], "best"))) fail("usage: rhythmer-stems <model> <threads> <file.wav> [best]");
    if (argc == 5) {
        bins = 1536;
        passes = 2;
        step = FRAMES / 2;
    }
    int threads = atoi(argv[2]);
    if (threads < 1) threads = 1;
    if (threads > 8) threads = 8;
    openblas_set_num_threads(1); // the threads here are the job's own

    unsigned int channels = 0, rate = 0;
    drwav_uint64 length = 0;
    float* sound = drwav_open_file_and_read_pcm_frames_f32(argv[3], &channels, &rate, &length, NULL);
    if (!sound || !length || channels < 1) fail("cannot read the file");
    if (rate != RATE) fail("the file is not at 44.1 kHz");
    progress(1);
    float* model = load_model(argv[1]);
    progress(5);

    // The sound a window in from the start, and a window and more of silence after it, as SpleeterRT's STFT wants it.
    const size_t padded = FFTSIZE * (size_t)ceil((double)length / FFTSIZE) + 2 * FFTSIZE;
    float* side[2] = {(float*)calloc(padded, sizeof(float)), (float*)calloc(padded, sizeof(float))};
    if (!side[0] || !side[1]) fail("out of memory");
    for (size_t n = 0; n < length; n++) {
        side[0][FFTSIZE + n] = sound[n * channels];
        side[1][FFTSIZE + n] = sound[n * channels + (channels > 1 ? 1 : 0)];
    }
    drwav_free(sound, NULL);

    OfflineSTFT* st = (OfflineSTFT*)malloc(sizeof(OfflineSTFT));
    if (!st) fail("out of memory");
    InitSTFT(st, (size_t)threads);
    Job job;
    memset(&job, 0, sizeof job);
    job.frames = stft(st, side[0], side[1], padded, &job.re[0], &job.im[0], &job.re[1], &job.im[1]);
    free(side[0]);
    free(side[1]);
    if (!job.frames) fail("nothing to separate");
    progress(8);

    job.model = model;
    job.segments = (job.frames + step - 1) / step;
    job.runs = job.segments + passes - 1;
    job.estimates = calloc(job.runs, sizeof *job.estimates);
    job.in = (unsigned char*)calloc(job.runs, 1);
    job.open = (unsigned char*)calloc(job.runs, 1);
    job.taken = (unsigned char*)calloc(job.segments, 1);
    if (!job.estimates || !job.in || !job.open || !job.taken) fail("out of memory");
    for (size_t s = 0; s < job.segments; s++)
        for (size_t r = s; r < s + passes; r++) job.open[r]++;
    for (int n = 0; n < STEMS; n++) {
        job.share[n] = (uint16_t*)malloc(job.frames * 2 * bins * sizeof(uint16_t));
        if (!job.share[n]) fail("out of memory");
    }
    pthread_mutex_init(&job.lock, NULL);
    pthread_t workers[8];
    for (int n = 1; n < threads; n++)
        if (pthread_create(&workers[n], NULL, work, &job)) fail("cannot start a thread");
    work(&job);
    for (int n = 1; n < threads; n++) pthread_join(workers[n], NULL);
    free(model);
    free(job.estimates);
    free(job.in);
    free(job.open);
    free(job.taken);

    // Each stem: the mix's spectrum with the stem's share of every bin, back to sound, to its file.
    float *re[2], *im[2];
    for (int s = 0; s < 2; s++) {
        re[s] = (float*)malloc(job.frames * FFTSIZE * sizeof(float));
        im[s] = (float*)malloc(job.frames * FFTSIZE * sizeof(float));
        if (!re[s] || !im[s]) fail("out of memory");
    }
    float* joined = (float*)malloc((size_t)length * 2 * sizeof(float));
    char* name = (char*)malloc(strlen(argv[3]) + 16);
    if (!joined || !name) fail("out of memory");
    const size_t top = bins / 4;
    for (int stem = 0; stem < STEMS; stem++) {
        const uint16_t* share = job.share[stem_net[stem]];
        for (size_t t = 0; t < job.frames; t++)
            for (int s = 0; s < 2; s++) {
                const uint16_t* row = share + (t * 2 + (size_t)s) * bins;
                const float *from_re = job.re[s] + t * FFTSIZE, *from_im = job.im[s] + t * FFTSIZE;
                float *to_re = re[s] + t * FFTSIZE, *to_im = im[s] + t * FFTSIZE;
                float high = 0;
                for (size_t bin = 0; bin < bins; bin++) {
                    const float gain = (float)row[bin] * (1.0f / 65535.0f);
                    to_re[bin] = from_re[bin] * gain;
                    to_im[bin] = from_im[bin] * gain;
                    if (bin >= bins - top) high += gain;
                }
                high /= (float)top; // above the bins the networks see: the frame's share of their top quarter
                for (size_t bin = bins; bin < HALFWNDLEN; bin++) {
                    to_re[bin] = from_re[bin] * high;
                    to_im[bin] = from_im[bin] * high;
                }
                memset(to_re + HALFWNDLEN, 0, (FFTSIZE - HALFWNDLEN) * sizeof(float));
                memset(to_im + HALFWNDLEN, 0, (FFTSIZE - HALFWNDLEN) * sizeof(float));
            }
        float *left = NULL, *right = NULL;
        const size_t made = istft(st, re[0], im[0], re[1], im[1], job.frames, &left, &right);
        if (!left || !right || made < FFTSIZE + (size_t)length) fail("the sound came back short");
        for (size_t n = 0; n < length; n++) {
            joined[2 * n] = left[FFTSIZE + n];
            joined[2 * n + 1] = right[FFTSIZE + n];
        }
        free(left);
        free(right);
        sprintf(name, "%s%s", argv[3], stem_file[stem]);
        drwav wav;
        drwav_data_format format;
        format.container = drwav_container_riff;
        format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
        format.channels = 2;
        format.sampleRate = RATE;
        format.bitsPerSample = 32;
        if (!drwav_init_file_write(&wav, name, &format, NULL)) fail("cannot write a stem");
        const drwav_uint64 written = drwav_write_pcm_frames(&wav, length, joined);
        drwav_uninit(&wav);
        if (written != length) fail("a stem was cut short");
        progress(87 + 3 * (stem + 1));
    }
    progress(100);
    return 0;
}
