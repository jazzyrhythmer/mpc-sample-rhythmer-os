// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
//
// Screen Example: a drive effect that draws its own screen in Rhythmer on the MPC Sample (../rhythmer_screen.h,
// docs/PLUGIN_SCREENS.md). MIT licensed: copy any of it.
//
// One source, one library with both entry points: the same file loads as a CLAP plug-in (named .clap) and as a VST2
// one (named .so). Three parameters, Drive, Tone and Mix: the sound is driven into a soft clip, then a low-pass, then
// mixed with the dry sound. In any host but Rhythmer it is an ordinary effect with no editor.
//
// Its screen, the page under B1-B3 (320 x 218) or the strip under a chain's slots (320 x 144):
//   - its name, the clip's curve, the levels in and out, and its three knobs;
//   - each knob drawn once, with a small triangle where the pot sits while the pot has not reached the value;
//   - K1 Drive, K2 Tone, K3 Mix;
//   - Minus / Plus pick a knob, the wheel turns it a hundredth a detent, Push puts it back to its default.
// The audio thread asks for a draw when the levels move, at most every 30 ms. A draw redraws only when something it
// shows changed.
#include <rhythmer_screen.h>
#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// --- The effect ---

constexpr int param_count = 3;
const char* const param_names[param_count] {"Drive", "Tone", "Mix"};
const char* const knob_labels[param_count] {"DRIVE", "TONE", "MIX"};
constexpr float param_defaults[param_count] {0.3f, 0.7f, 1.0f};

struct Effect {
    std::atomic<float> values[param_count];
    std::atomic<float> level_in {0}, level_out {0}; // the last block's peaks, for the screen
    double rate = 48000;
    float low[2] {0, 0};                            // the low-pass's state, per channel
    uint32_t since_redraw = 0;                      // frames since the audio thread last asked for a draw
    bool loud = false;                              // it had sound at the last ask
    // The screen's own state (its thread only): what it drew last, and the knob in hand.
    int focus = 0;
    float drawn_values[param_count] {-1, -1, -1};
    int drawn_in = -1, drawn_out = -1, drawn_focus = -1;
    int16_t drawn_pots[3] {-2, -2, -2};
    uint8_t drawn_live[3] {2, 2, 2};
    uint32_t drawn_colour = 0;
    uint32_t drawn_area = 99;

    Effect() { for (int n = 0; n < param_count; ++n) values[n] = param_defaults[n]; }
    float value(int n) const { return values[n].load(std::memory_order_relaxed); }
    void set(int n, float v) { values[n].store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed); }

    // Drive 1x to 20x into tanh; Tone a one-pole low-pass, 300 Hz to 18 kHz; Mix the wet share.
    void process(const float* const* in, float* const* out, uint32_t frames) {
        const float gain = 1.0f + 19.0f * value(0) * value(0);
        const float norm = 1.0f / std::tanh(gain);
        const float cutoff = 300.0f * std::pow(60.0f, value(1));
        const float a = 1.0f - std::exp(-2.0f * 3.14159265f * cutoff / float(rate));
        const float wet = value(2);
        float peak_in = 0, peak_out = 0;
        for (int c = 0; c < 2; ++c)
            for (uint32_t f = 0; f < frames; ++f) {
                float x = in[c][f];
                float y = std::tanh(x * gain) * norm;
                low[c] += a * (y - low[c]);
                float o = wet * low[c] + (1.0f - wet) * x;
                out[c][f] = o;
                peak_in = std::max(peak_in, std::fabs(x));
                peak_out = std::max(peak_out, std::fabs(o));
            }
        level_in.store(peak_in, std::memory_order_relaxed);
        level_out.store(peak_out, std::memory_order_relaxed);
    }
    // After a block: whether to ask for a draw (the levels moved, at most every 30 ms; once more as they go quiet).
    bool wants_draw(uint32_t frames) {
        since_redraw += frames;
        const bool now_loud = level_in.load(std::memory_order_relaxed) > 0.001f || level_out.load(std::memory_order_relaxed) > 0.001f;
        if ((!now_loud && !loud) || since_redraw < uint32_t(rate * 0.03)) return false;
        since_redraw = 0;
        loud = now_loud;
        return true;
    }
};

void value_text(int n, float v, char* out, size_t size) {
    if (n == 0) std::snprintf(out, size, "%.1fx", 1.0f + 19.0f * v * v);
    else if (n == 1) std::snprintf(out, size, "%d Hz", int(300.0f * std::pow(60.0f, v)));
    else std::snprintf(out, size, "%d %%", int(std::lround(v * 100)));
}

// --- Drawing: pixels are 0x00RRGGBB ---

constexpr uint32_t bg = 0x10131a, frame_line = 0x2a2f3a, text = 0xe8ecf2, dim = 0x7a8290, well = 0x1b2029;

struct Canvas {
    uint32_t* px;
    int w, h, stride;
    void fill(int x, int y, int fw, int fh, uint32_t c) {
        for (int j = std::max(0, y); j < std::min(h, y + fh); ++j)
            for (int i = std::max(0, x); i < std::min(w, x + fw); ++i) px[j * stride + i] = c;
    }
    void dot(int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < w && y < h) px[y * stride + x] = c; }
    void disc(float cx, float cy, float r, uint32_t c) {
        for (int y = int(cy - r); y <= int(cy + r); ++y)
            for (int x = int(cx - r); x <= int(cx + r); ++x)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) dot(x, y, c);
    }
    // An arc of thickness `t` from angle a0 to a1 (radians, 0 up, clockwise).
    void arc(float cx, float cy, float r, float t, float a0, float a1, uint32_t c) {
        for (int y = int(cy - r - t); y <= int(cy + r + t); ++y)
            for (int x = int(cx - r - t); x <= int(cx + r + t); ++x) {
                float dx = x - cx, dy = y - cy, d = std::sqrt(dx * dx + dy * dy);
                if (std::fabs(d - r) > t / 2) continue;
                float a = std::atan2(dx, -dy);
                if (a >= a0 && a <= a1) dot(x, y, c);
            }
    }
    void line(float x0, float y0, float x1, float y1, uint32_t c) {
        int steps = int(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))) + 1;
        for (int s = 0; s <= steps; ++s) {
            float t = float(s) / float(steps);
            int x = int(std::lround(x0 + (x1 - x0) * t)), y = int(std::lround(y0 + (y1 - y0) * t));
            dot(x, y, c); dot(x, y + 1, c);
        }
    }
    // A 5 x 7 capital letter, digit or sign at twice its size: 14 pixels tall, 12 a character.
    void print(int x, int y, const char* s, uint32_t c) {
        static const char* const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%.-";
        static const uint8_t glyphs[][7] {
            {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
            {31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,2,18,12},
            {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,17,25,21,19,17,17},{14,17,17,17,17,17,14},
            {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
            {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,17,10,4,4,4},
            {31,1,2,4,8,16,31},{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},
            {2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
            {14,17,17,15,1,2,12},{24,25,2,4,8,19,3},{0,0,0,0,0,12,12},{0,0,0,31,0,0,0},
        };
        for (; *s; ++s, x += 12) {
            const char* at = std::strchr(chars, *s);
            if (!at || !*s) continue;
            const uint8_t* g = glyphs[at - chars];
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g[row] >> (4 - col) & 1) fill(x + col * 2, y + row * 2, 2, 2, c);
        }
    }
};
int text_width(const char* s) { return int(std::strlen(s)) * 12 - 2; }

// A knob: its track, its value's arc in the track's colour, a pointer; the knob in hand underlined; where the pot sits
// marked while the pot has not reached the value (the page's only pickup mark: Rhythmer draws none over it).
void knob(Canvas& g, float cx, float cy, float r, float v, uint32_t colour, bool focused, int16_t pot, bool live, const char* label) {
    const float a0 = -2.356f, a1 = 2.356f, at = a0 + (a1 - a0) * v;
    g.disc(cx, cy, r - 4, well);
    g.arc(cx, cy, r, 3, a0, a1, frame_line);
    g.arc(cx, cy, r, 3, a0, at, colour);
    g.line(cx + std::sin(at) * (r * 0.25f), cy - std::cos(at) * (r * 0.25f), cx + std::sin(at) * (r - 6), cy - std::cos(at) * (r - 6), text);
    if (pot >= 0 && !live) { // a hollow triangle outside the track, pointing at the pot
        const float p = a0 + (a1 - a0) * (pot / 127.0f), px = cx + std::sin(p) * (r + 6), py = cy - std::cos(p) * (r + 6);
        g.disc(px, py, 4, text);
        g.disc(px, py, 2, bg);
    }
    g.print(int(cx) - text_width(label) / 2, int(cy + r + 8), label, focused ? text : dim);
    if (focused) g.fill(int(cx) - text_width(label) / 2, int(cy + r + 24), text_width(label), 2, colour);
}

// The levels in and out, as bars.
void meters(Canvas& g, int x, int y, int w, float in, float out, uint32_t colour) {
    g.print(x, y, "IN", dim);
    g.print(x, y + 24, "OUT", dim);
    const int bx = x + 42, bw = w - 42;
    g.fill(bx, y, bw, 14, well);
    g.fill(bx, y + 24, bw, 14, well);
    g.fill(bx, y, int(bw * std::min(1.0f, in)), 14, dim);
    g.fill(bx, y + 24, int(bw * std::min(1.0f, out)), 14, colour);
}

// The clip's curve, -1..1 in and out.
void curve(Canvas& g, int x, int y, int w, int h, float drive, uint32_t colour) {
    g.fill(x, y, w, h, well);
    g.fill(x, y + h / 2, w, 1, frame_line);
    g.fill(x + w / 2, y, 1, h, frame_line);
    const float gain = 1.0f + 19.0f * drive * drive, norm = 1.0f / std::tanh(gain);
    float px = 0, py = 0;
    for (int i = 0; i <= w; ++i) {
        float in = -1.0f + 2.0f * i / w, out = std::tanh(in * gain) * norm;
        float sx = float(x + i), sy = float(y + h / 2) - out * (h / 2 - 4);
        if (i) g.line(px, py, sx, sy, colour);
        px = sx; py = sy;
    }
}

bool draw(Effect& e, const rhythmer_screen_frame_t* frame) {
    const int level_in = int(std::min(1.0f, e.level_in.load(std::memory_order_relaxed)) * 100);
    const int level_out = int(std::min(1.0f, e.level_out.load(std::memory_order_relaxed)) * 100);
    bool changed = frame->redraw_all || frame->colour != e.drawn_colour || frame->area != e.drawn_area || e.focus != e.drawn_focus
                || level_in != e.drawn_in || level_out != e.drawn_out;
    for (int n = 0; n < param_count; ++n) changed = changed || e.value(n) != e.drawn_values[n];
    for (int k = 0; k < 3; ++k) changed = changed || frame->pots[k] != e.drawn_pots[k] || frame->pots_live[k] != e.drawn_live[k];
    if (!changed) return false;

    Canvas g {frame->pixels, int(frame->width), int(frame->height), int(frame->stride)};
    const uint32_t colour = frame->colour & 0xffffffu;
    g.fill(0, 0, g.w, g.h, bg);
    g.print(10, 8, "SCREEN EXAMPLE", text);
    char shown[16];
    value_text(e.focus, e.value(e.focus), shown, sizeof(shown));
    for (char* c = shown; *c; ++c) *c = char(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    g.print(g.w - 10 - text_width(shown), 8, shown, colour);
    const bool strip = frame->area == RHYTHMER_SCREEN_STRIP;
    if (strip) {
        meters(g, 10, 32, 300, level_in / 100.0f, level_out / 100.0f, colour);
    } else {
        curve(g, 10, 32, 140, 100, e.value(0), colour);
        meters(g, 166, 50, 144, level_in / 100.0f, level_out / 100.0f, colour);
    }
    const float ky = strip ? 96 : 160, r = strip ? 14 : 18;
    for (int k = 0; k < 3; ++k)
        knob(g, 58.0f + 102.0f * k, ky, r, e.value(k), colour, k == e.focus, frame->pots[k], frame->pots_live[k] != 0, knob_labels[k]);

    for (int n = 0; n < param_count; ++n) e.drawn_values[n] = e.value(n);
    for (int k = 0; k < 3; ++k) { e.drawn_pots[k] = frame->pots[k]; e.drawn_live[k] = frame->pots_live[k]; }
    e.drawn_in = level_in; e.drawn_out = level_out; e.drawn_focus = e.focus;
    e.drawn_colour = frame->colour; e.drawn_area = frame->area;
    return true;
}

// An input on its page; true if it used it. `changed`: a value moved (the host is told).
bool input(Effect& e, const rhythmer_screen_input_t* in, bool& changed) {
    changed = false;
    if (in->kind == RHYTHMER_SCREEN_STEP) { e.focus = std::clamp(e.focus + in->value, 0, param_count - 1); return true; }
    if (in->kind == RHYTHMER_SCREEN_WHEEL) { e.set(e.focus, e.value(e.focus) + 0.01f * float(in->value)); changed = true; return true; }
    if (in->kind == RHYTHMER_SCREEN_PUSH) { e.set(e.focus, param_defaults[e.focus]); changed = true; return true; }
    return false;
}

// --- CLAP ---

struct ClapEffect {
    clap_plugin plugin {};
    const clap_host* host = nullptr;
    const rhythmer_screen_host_t* screen_host = nullptr;
    const clap_host_params* host_params = nullptr;
    Effect effect;
};
ClapEffect& clap_of(const clap_plugin* p) { return *static_cast<ClapEffect*>(p->plugin_data); }
ClapEffect& clap_of(const void* p) { return clap_of(static_cast<const clap_plugin*>(p)); }

const char* const clap_features[] {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION, nullptr};
const clap_plugin_descriptor clap_descriptor {CLAP_VERSION_INIT, "rhythmer.screen-example", "Screen Example", "Rhythmer", "", "", "",
                                              "1.0.0", "A drive effect that draws its own screen in Rhythmer", clap_features};

void clap_events(ClapEffect& s, const clap_input_events* in) {
    for (uint32_t i = 0, n = in ? in->size(in) : 0; i < n; ++i) {
        const auto* h = in->get(in, i);
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* ev = reinterpret_cast<const clap_event_param_value*>(h);
        if (ev->param_id < uint32_t(param_count)) s.effect.set(int(ev->param_id), float(ev->value));
    }
}

uint32_t clap_param_count(const clap_plugin*) { return param_count; }
bool clap_param_info(const clap_plugin*, uint32_t index, clap_param_info* info) {
    if (index >= uint32_t(param_count)) return false;
    *info = {};
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name, sizeof(info->name), "%s", param_names[index]);
    info->min_value = 0;
    info->max_value = 1;
    info->default_value = param_defaults[index];
    return true;
}
bool clap_param_value(const clap_plugin* p, clap_id id, double* out) {
    if (id >= uint32_t(param_count)) return false;
    *out = clap_of(p).effect.value(int(id));
    return true;
}
bool clap_value_text(const clap_plugin*, clap_id id, double v, char* out, uint32_t size) {
    if (id >= uint32_t(param_count)) return false;
    value_text(int(id), float(v), out, size);
    return true;
}
bool clap_text_value(const clap_plugin*, clap_id, const char* text, double* out) { *out = std::atof(text); return true; }
void clap_flush(const clap_plugin* p, const clap_input_events* in, const clap_output_events*) { clap_events(clap_of(p), in); }
const clap_plugin_params clap_params {clap_param_count, clap_param_info, clap_param_value, clap_value_text, clap_text_value, clap_flush};

uint32_t clap_port_count(const clap_plugin*, bool) { return 1; }
bool clap_port_info(const clap_plugin*, uint32_t index, bool input, clap_audio_port_info* info) {
    if (index != 0) return false;
    *info = {};
    info->id = input ? 0 : 1;
    std::snprintf(info->name, sizeof(info->name), "%s", input ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports clap_ports {clap_port_count, clap_port_info};

bool clap_save(const clap_plugin* p, const clap_ostream* out) {
    float v[param_count];
    for (int n = 0; n < param_count; ++n) v[n] = clap_of(p).effect.value(n);
    return out->write(out, v, sizeof(v)) == int64_t(sizeof(v));
}
bool clap_load(const clap_plugin* p, const clap_istream* in) {
    float v[param_count];
    if (in->read(in, v, sizeof(v)) != int64_t(sizeof(v))) return false;
    for (int n = 0; n < param_count; ++n) clap_of(p).effect.set(n, v[n]);
    return true;
}
const clap_plugin_state clap_state {clap_save, clap_load};

bool clap_screen_draw(const void* p, const rhythmer_screen_frame_t* frame) { return draw(clap_of(p).effect, frame); }
bool clap_screen_input(const void* p, const rhythmer_screen_input_t* in) {
    auto& s = clap_of(p);
    bool changed = false, used = input(s.effect, in, changed);
    if (changed) {
        if (s.host_params) s.host_params->rescan(s.host, CLAP_PARAM_RESCAN_VALUES); // the host reads its values again
        if (s.screen_host) s.screen_host->redraw(s.host, RHYTHMER_SCREEN_RUN);     // and runs a block, though silent
    }
    return used;
}
void clap_screen_controls(const void*, uint32_t ids[3]) { ids[0] = 0; ids[1] = 1; ids[2] = 2; }
const rhythmer_screen_t clap_screen {clap_screen_draw, clap_screen_input, clap_screen_controls};

const void* clap_extension(const clap_plugin*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &clap_params;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &clap_ports;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &clap_state;
    if (!std::strcmp(id, RHYTHMER_SCREEN_CLAP)) return &clap_screen;
    return nullptr;
}

clap_process_status clap_process(const clap_plugin* p, const clap_process* process) {
    auto& s = clap_of(p);
    clap_events(s, process->in_events);
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
    s.effect.process(process->audio_inputs[0].data32, process->audio_outputs[0].data32, process->frames_count);
    if (s.effect.wants_draw(process->frames_count) && s.screen_host) s.screen_host->redraw(s.host, 0);
    return CLAP_PROCESS_CONTINUE;
}

const clap_plugin* clap_create(const clap_plugin_factory*, const clap_host* host, const char* id) {
    if (std::strcmp(id, clap_descriptor.id)) return nullptr;
    auto* s = new ClapEffect;
    s->host = host;
    auto& p = s->plugin;
    p.desc = &clap_descriptor;
    p.plugin_data = s;
    p.init = [](const clap_plugin* p) {
        auto& s = clap_of(p);
        s.screen_host = static_cast<const rhythmer_screen_host_t*>(s.host->get_extension(s.host, RHYTHMER_SCREEN_CLAP_HOST)); // null elsewhere
        s.host_params = static_cast<const clap_host_params*>(s.host->get_extension(s.host, CLAP_EXT_PARAMS));
        return true;
    };
    p.destroy = [](const clap_plugin* p) { delete &clap_of(p); };
    p.activate = [](const clap_plugin* p, double rate, uint32_t, uint32_t) { clap_of(p).effect.rate = rate; return true; };
    p.deactivate = [](const clap_plugin*) {};
    p.start_processing = [](const clap_plugin*) { return true; };
    p.stop_processing = [](const clap_plugin*) {};
    p.reset = [](const clap_plugin* p) { auto& e = clap_of(p).effect; e.low[0] = e.low[1] = 0; };
    p.process = clap_process;
    p.get_extension = clap_extension;
    p.on_main_thread = [](const clap_plugin*) {};
    return &p;
}
uint32_t clap_factory_count(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor* clap_factory_descriptor(const clap_plugin_factory*, uint32_t i) { return i == 0 ? &clap_descriptor : nullptr; }
const clap_plugin_factory clap_factory {clap_factory_count, clap_factory_descriptor, clap_create};

// --- VST2 ---
// The VST 2.4 binary interface as far as this plug-in needs it, from its public, long-documented layout (as VeSTige
// and FST reimplement it). Steinberg's withdrawn SDK is not used.

struct Vst2Effect;
using Vst2Host = intptr_t (*)(Vst2Effect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
struct Vst2Effect {
    int32_t magic;
    intptr_t (*dispatcher)(Vst2Effect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    void* process_accumulating;
    void (*set_parameter)(Vst2Effect*, int32_t index, float value);
    float (*get_parameter)(Vst2Effect*, int32_t index);
    int32_t programs, params, inputs, outputs, flags;
    intptr_t reserved1, reserved2;
    int32_t initial_delay, real_qualities, off_qualities;
    float io_ratio;
    void* object;
    void* user;
    int32_t unique_id, version;
    void (*process_replacing)(Vst2Effect*, float** inputs, float** outputs, int32_t frames);
    void (*process_double_replacing)(Vst2Effect*, double** inputs, double** outputs, int32_t frames);
    char future[56];
};
constexpr int32_t vst2_magic = ('V' << 24) | ('s' << 16) | ('t' << 8) | 'P';
constexpr int32_t vst2_can_replace = 1 << 4;
enum : int32_t { eff_open = 0, eff_close = 1, eff_get_param_label = 6, eff_get_param_display = 7, eff_get_param_name = 8,
                 eff_set_sample_rate = 10, eff_get_effect_name = 45, eff_get_vendor_string = 47, eff_get_product_string = 48,
                 eff_vendor_specific = 50, eff_can_do = 51 };
enum : int32_t { host_automate = 0, host_vendor_specific = 35, host_can_do = 37 };

struct Vst2Plugin {
    Vst2Effect effect {};
    Vst2Host host = nullptr;
    bool rhythmer = false; // the host takes a screen (Rhythmer): ask it for draws
    Effect fx;
};
Vst2Plugin& vst2_of(Vst2Effect* e) { return *static_cast<Vst2Plugin*>(e->object); }
Vst2Plugin& vst2_of(const void* e) { return vst2_of(static_cast<Vst2Effect*>(const_cast<void*>(e))); }

// Text to the host, cut to fit: VST2 gives names and values 8 bytes, the rest 64.
void vst2_copy(void* ptr, const char* s, size_t size = 64) {
    if (!ptr || !size) return;
    auto* out = static_cast<char*>(ptr);
    size_t n = 0;
    for (; s[n] && n + 1 < size; ++n) out[n] = s[n];
    out[n] = 0;
}

bool vst2_screen_draw(const void* e, const rhythmer_screen_frame_t* frame) { return draw(vst2_of(e).fx, frame); }
bool vst2_screen_input(const void* e, const rhythmer_screen_input_t* in) {
    auto& s = vst2_of(e);
    bool changed = false, used = input(s.fx, in, changed);
    if (changed) {
        auto* effect = &s.effect;
        s.host(effect, host_automate, s.fx.focus, 0, nullptr, s.fx.value(s.fx.focus)); // the host hears the new value
        s.host(effect, host_vendor_specific, RHYTHMER_SCREEN_VST2, RHYTHMER_SCREEN_RUN, nullptr, 0); // and runs a block
    }
    return used;
}
const rhythmer_screen_t vst2_screen {vst2_screen_draw, vst2_screen_input, clap_screen_controls}; // a VST2 ID is the index

intptr_t vst2_dispatch(Vst2Effect* e, int32_t opcode, int32_t index, intptr_t, void* ptr, float opt) {
    auto& s = vst2_of(e);
    switch (opcode) {
    case eff_open:
        s.rhythmer = s.host(e, host_can_do, 0, 0, (void*) RHYTHMER_SCREEN_VST2_CANDO, 0) == 1;
        return 0;
    case eff_close: delete &s; return 0;
    case eff_set_sample_rate: s.fx.rate = opt; return 0;
    case eff_get_param_name: if (index >= 0 && index < param_count) vst2_copy(ptr, param_names[index], 8); return 0;
    case eff_get_param_label: if (ptr) static_cast<char*>(ptr)[0] = 0; return 0;
    case eff_get_param_display:
        if (index >= 0 && index < param_count) { char t[16]; value_text(index, s.fx.value(index), t, sizeof(t)); vst2_copy(ptr, t, 8); }
        return 0;
    case eff_get_effect_name: case eff_get_product_string: vst2_copy(ptr, "Screen Example"); return 1;
    case eff_get_vendor_string: vst2_copy(ptr, "Rhythmer"); return 1;
    case eff_can_do: // a screen of its own, for Rhythmer (asked before the vendor call)
        return ptr && !std::strcmp(static_cast<const char*>(ptr), RHYTHMER_SCREEN_VST2_CANDO) ? 1 : 0;
    case eff_vendor_specific: return index == RHYTHMER_SCREEN_VST2 ? reinterpret_cast<intptr_t>(&vst2_screen) : 0;
    default: return 0;
    }
}
void vst2_set(Vst2Effect* e, int32_t index, float v) { if (index >= 0 && index < param_count) vst2_of(e).fx.set(index, v); }
float vst2_get(Vst2Effect* e, int32_t index) { return index >= 0 && index < param_count ? vst2_of(e).fx.value(index) : 0.0f; }
void vst2_process(Vst2Effect* e, float** in, float** out, int32_t frames) {
    auto& s = vst2_of(e);
    s.fx.process(in, out, uint32_t(frames));
    if (s.fx.wants_draw(uint32_t(frames)) && s.rhythmer) s.host(e, host_vendor_specific, RHYTHMER_SCREEN_VST2, 0, nullptr, 0);
}

} // namespace

extern "C" {
// CLAP: declared by clap/entry.h, exported by it.
const clap_plugin_entry clap_entry {
    CLAP_VERSION_INIT,
    [](const char*) { return true; },
    [] {},
    [](const char* id) -> const void* { return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &clap_factory : nullptr; },
};

// VST2.
__attribute__((visibility("default"))) Vst2Effect* VSTPluginMain(Vst2Host host) {
    auto* s = new Vst2Plugin;
    s->host = host;
    auto& e = s->effect;
    e.magic = vst2_magic;
    e.dispatcher = vst2_dispatch;
    e.set_parameter = vst2_set;
    e.get_parameter = vst2_get;
    e.params = param_count;
    e.inputs = 2;
    e.outputs = 2;
    e.flags = vst2_can_replace;
    e.object = s;
    e.unique_id = ('M' << 24) | ('l' << 16) | ('S' << 8) | 'x';
    e.version = 1;
    e.process_replacing = vst2_process;
    return &e;
}
}
