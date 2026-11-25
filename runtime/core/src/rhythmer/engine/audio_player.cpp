// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/audio_player.h"
#include "rhythmer/engine/dsp.h"
#include <algorithm>
#include <cmath>

namespace rhythmer {

unsigned AudioPack::hold_take(std::vector<float> left, std::vector<float> right, float rate) {
    auto part = std::make_unique<ZonePart>();
    owned.push_back(std::move(left));
    const float* l = owned.back().data();
    const uint32_t frames = uint32_t(owned.back().size());
    owned.push_back(std::move(right));
    part->left = l;
    part->right = owned.back().data();
    part->rate = rate;
    part->total = frames;
    part->frames.store(frames);
    takes.push_back(std::move(part));
    return unsigned(takes.size() - 1);
}

void RegionPlayer::close(Voice& v) {
    if (v.stream >= 0 && pager) pager->close(v.stream);
    v = Voice {};
}

void RegionPlayer::cut_all() { for (auto& v : voices) close(v); }

void RegionPlayer::fade_all(float rate) {
    for (auto& v : voices) {
        if (!v.clip) continue;
        v.leaving = true;
        v.fade = std::min(v.fade, -1.0f / (fade_seconds * rate));
    }
}

bool RegionPlayer::leaving() const {
    for (const auto& v : voices) if (v.clip && v.leaving) return true;
    return false;
}

bool RegionPlayer::sounding() const {
    for (const auto& v : voices) if (v.clip) return true;
    return false;
}

double RegionPlayer::step_of(const RegionClip& c, float rate) const {
    const double own = double(c.take ? c.take->rate : rate) / std::max(1.0f, rate);
    return c.warp ? own * double(tempo_now) / std::max<double>(c.tempo, 1) : own;
}

void RegionPlayer::steer(Voice& v, float rate) {
    const RegionClip& c = *v.clip;
    v.step = step_of(c, rate);
    v.stretch = c.warp && c.mode == WarpMode::Stretch && !c.reverse;
    v.read = v.stretch ? double(c.take ? c.take->rate : rate) / std::max(1.0f, rate) * c.pitch : v.step;
}

void RegionPlayer::begin(const RegionClip* clip, double pos, bool fade_in, float rate) {
    auto& m = voices[0];
    if (m.clip) close(m);
    if (!clip || !clip->take || pos >= clip->frames) return;
    m.clip = clip;
    m.pos = std::max(0.0, pos);
    steer(m, rate);
    m.window = std::clamp(uint32_t(window_seconds * rate), 64u, max_window);
    m.hop = m.window / 2;
    if (weights_for != m.window) { // once a rate
        for (uint32_t n = 0; n < m.window; ++n) { const float s = std::sin(dsp::pi * float(n) / float(m.window)); weights[n] = s * s; }
        weights_for = m.window;
    }
    m.since = m.hop; // the first window opens on the first frame
    m.on = {};
    m.gain = fade_in ? 0.0f : 1.0f;
    m.fade = fade_in ? 1.0f / (fade_seconds * rate) : 0.0f;
    if (clip->resident) return;
    const uint32_t into = uint32_t(m.pos);
    const uint32_t frame = clip->reverse ? clip->offset + clip->frames - 1 - into : clip->offset + into;
    // A region started at its top reads what the pack pinned at once; elsewhere it waits for the pager.
    const uint32_t safe = !clip->reverse && into < clip->held ? clip->held - into : 0;
    const float ahead = float(std::max(1.0, m.step)) * std::max(1.0f, rate / float(stream_chunk)) * 0.75f; // about a second
    m.stream = pager ? pager->open(*clip->take, frame, clip->reverse, ahead, safe) : -1;
    m.seeking = safe == 0;
    if (m.stream < 0) ++underruns; // no stream free: silent until the next start
}

void RegionPlayer::retire_main(float rate) {
    auto& m = voices[0];
    if (!m.clip) return;
    Voice* to = nullptr;
    for (unsigned n = 1; n < voice_count && !to; ++n) if (!voices[n].clip) to = &voices[n];
    if (!to) { to = &voices[1]; close(*to); } // none free: the oldest makes room
    *to = m;
    to->fade = std::min(to->fade, -1.0f / (fade_seconds * rate));
    m = Voice {};
}

void RegionPlayer::release(float rate) { retire_main(rate); }

void RegionPlayer::follow(const RegionClip* clip, double into, bool top, bool jumped, float rate) {
    auto& m = voices[0];
    if (!clip) { if (m.clip) release(rate); return; }
    const double pos = into * step_of(*clip, rate);
    if (top) { // the pattern starts (again): its take from the top, what still sounds faded out
        if (m.clip) retire_main(rate);
        begin(clip, 0, false, rate);
    } else if (m.clip != clip) { // another pattern's take, or Play inside the pattern: from where the pattern is
        if (m.clip) retire_main(rate);
        begin(clip, pos, pos > slack, rate);
    } else {
        steer(m, rate); // the tempo may have moved: a warped take follows it
        // Somewhere else in it now (a locate); warped, it also keeps to where the pattern is as the tempo moves.
        if ((jumped || clip->warp) && std::fabs(m.pos - pos) > slack * m.step) {
            retire_main(rate);
            begin(clip, pos, true, rate);
        }
    }
}

// Stretch: where near `near` a window should start so that it reads like the window playing (at `from`) goes on, over
// match_seconds: the start whose frames line up best with those (correlation of the two sides summed, on whole frames,
// every fourth). Coarse first (every sixth start, every eighth frame), then each start near the best: a search costs a
// tenth of a millisecond on the MPC's cores, in the one block a window opens in. Within the region and what the pager
// holds; else `near`.
double RegionPlayer::align(const Voice& v, const float* l, const float* r, uint32_t limit, double from, double near) const {
    const RegionClip& c = *v.clip;
    const double span = search_seconds * double(c.take->rate);
    const unsigned length = std::max(8u, unsigned(match_seconds * c.take->rate));
    const double reach = double(length) * v.read + 2;
    const double lo = std::max(0.0, near - span), hi = std::min(double(c.frames) - reach - 1, near + span);
    if (hi <= lo || from < 0 || from + reach >= double(c.frames)) return near;
    if (!c.resident && (uint64_t(c.offset) + uint64_t(std::max(hi, from) + reach) + 2 >= limit)) return near;
    const float* lp = l + c.offset;
    const float* rp = r + c.offset;
    constexpr unsigned most = 128;
    const unsigned taps = std::min(most, length / 4);
    std::array<float, most> want {};
    std::array<uint32_t, most> step {}; // each tap's distance from a start, in whole frames
    for (unsigned k = 0; k < taps; ++k) {
        step[k] = uint32_t(double(4 * k) * v.read + 0.5);
        const uint32_t i = uint32_t(from) + step[k];
        want[k] = lp[i] + rp[i];
    }
    auto score = [&](uint32_t s, unsigned every) {
        float dot = 0, energy = 1e-9f;
        for (unsigned k = 0; k < taps; k += every) { const float x = lp[s + step[k]] + rp[s + step[k]]; dot += x * want[k]; energy += x * x; }
        return dot / std::sqrt(energy);
    };
    const uint32_t first = uint32_t(lo), last = uint32_t(hi);
    uint32_t best = uint32_t(near);
    float top = -1e30f;
    for (uint32_t s = first; s <= last; s += 6) if (float value = score(s, 2); value > top) { top = value; best = s; }
    const uint32_t from_fine = best > first + 5 ? best - 5 : first, to_fine = std::min(last, best + 5);
    top = -1e30f;
    for (uint32_t s = from_fine; s <= to_fine; ++s) if (float value = score(s, 1); value > top) { top = value; best = s; }
    return double(best) + (near - std::floor(near)); // the place's fraction kept: the window reads as finely as it plays
}

void RegionPlayer::render(float* left, float* right, unsigned frames) {
    for (unsigned n = 0; n < voice_count; ++n) {
        auto& v = voices[n];
        if (!v.clip) continue;
        const RegionClip& c = *v.clip;
        const ZonePart& part = *c.take;
        if (!c.resident && (v.stream < 0 || !pager || pager->dead(v.stream))) { // no stream, or the card went
            close(v);
            continue;
        }
        const uint32_t limit = c.resident ? part.total : pager->limit(v.stream);
        const uint32_t first = c.offset, last = c.offset + c.frames - 1; // the region's frames in the take
        const float* l = part.left;
        const float* r = part.right;
        bool late = false;
        unsigned f = 0;
        for (; f < frames; ++f) {
            if (v.pos >= c.frames) { close(v); break; }
            if (v.stretch) { // windows of the take at its own pitch, each placed by align(), their sin² weights summing to one
                if (v.since >= v.hop) {
                    int playing = -1; // the window halfway through (the other has run out): the new one goes on from it
                    for (int k = 0; k < 2; ++k)
                        if (v.on[size_t(k)] && v.age[size_t(k)] < v.window && (playing < 0 || v.age[size_t(k)] > v.age[size_t(playing)])) playing = k;
                    const double start = playing >= 0 ? align(v, l, r, limit, v.tap[size_t(playing)], v.pos) : v.pos;
                    const size_t slot = playing == 0 ? 1 : 0;
                    v.tap[slot] = start; v.age[slot] = 0; v.on[slot] = true;
                    v.since = 0;
                }
                float sum_l = 0, sum_r = 0;
                bool all = true;
                for (size_t k = 0; k < 2; ++k) {
                    if (!v.on[k]) continue;
                    const double x = double(first) + v.tap[k];
                    const uint32_t i0 = uint32_t(x), i1 = std::min(i0 + 1, last);
                    if (v.tap[k] < 0 || v.tap[k] >= c.frames || v.age[k] >= v.window) { v.on[k] = false; continue; }
                    if (c.resident || i1 < limit) {
                        const float t = float(x - double(i0)), w = v.window == weights_for ? weights[v.age[k]] : 0.5f;
                        sum_l += (l[i0] + (l[i1] - l[i0]) * t) * w;
                        sum_r += (r[i0] + (r[i1] - r[i0]) * t) * w;
                    } else {
                        all = false;
                    }
                    v.tap[k] += v.read;
                    ++v.age[k];
                }
                if (all) v.seeking = false; else late = true;
                float g = quiet ? 0.0f : c.gain * v.gain;
                if (c.fade_in && v.pos < c.fade_in) g *= float(v.pos / c.fade_in);
                const double left_in = double(c.frames) - v.pos;
                if (c.fade_out && left_in < c.fade_out) g *= float(left_in / c.fade_out);
                left[f] += sum_l * g;
                right[f] += sum_r * g;
                ++v.since;
                v.pos += v.step;
                if (v.fade != 0) {
                    v.gain = std::clamp(v.gain + v.fade, 0.0f, 1.0f);
                    if (v.fade > 0 && v.gain >= 1) v.fade = 0;
                    if (v.fade < 0 && v.gain <= 0) { close(v); break; }
                }
                continue;
            }
            const double x = c.reverse ? double(last) - v.pos : double(first) + v.pos;
            const uint32_t i0 = uint32_t(x);
            const uint32_t i1 = std::min(i0 + 1, last);
            const bool ready = c.resident || (c.reverse ? i0 >= limit : i1 < limit);
            if (ready) {
                v.seeking = false;
                const float t = float(x - double(i0));
                float g = quiet ? 0.0f : c.gain * v.gain;
                if (c.fade_in && v.pos < c.fade_in) g *= float(v.pos / c.fade_in);
                const double left_in = double(c.frames) - v.pos;
                if (c.fade_out && left_in < c.fade_out) g *= float(left_in / c.fade_out);
                left[f] += (l[i0] + (l[i1] - l[i0]) * t) * g;
                right[f] += (r[i0] + (r[i1] - r[i0]) * t) * g;
            } else {
                late = true;
            }
            v.pos += v.step;
            if (v.fade != 0) {
                v.gain = std::clamp(v.gain + v.fade, 0.0f, 1.0f);
                if (v.fade > 0 && v.gain >= 1) v.fade = 0;
                if (v.fade < 0 && v.gain <= 0) { close(v); break; }
            }
        }
        if (late) (v.seeking ? waits : underruns) += 1;
        if (v.clip && v.stream >= 0 && pager) {
            const double x = c.reverse ? double(last) - v.pos : double(first) + v.pos;
            pager->place(v.stream, uint32_t(std::clamp(x, double(first), double(last))));
        }
    }
}

} // namespace rhythmer
