// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/mpc_wav_slices.h"
#include "rhythmer/engine/dsp.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace rhythmer {
namespace {

struct Chunk {
    char id[4] {};
    uint32_t size = 0;
    std::streamoff body = 0; // where its bytes start in the file
};

// The file's chunks after "RIFF....WAVE", in order; empty when it is not a WAV.
std::vector<Chunk> chunks_of(std::ifstream& file) {
    std::vector<Chunk> out;
    char head[12];
    file.seekg(0);
    if (!file.read(head, sizeof(head)) || std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4)) return out;
    char at[8];
    while (file.read(at, sizeof(at))) {
        Chunk c;
        std::memcpy(c.id, at, 4);
        c.size = juce::ByteOrder::littleEndianInt(at + 4);
        c.body = file.tellg();
        out.push_back(c);
        file.seekg(std::streamoff(c.size) + (c.size & 1), std::ios::cur);
    }
    file.clear();
    return out;
}

bool is(const Chunk& c, const char* id) { return !std::memcmp(c.id, id, 4); }

std::string body_of(std::ifstream& file, const Chunk& c) {
    std::string out(c.size, '\0');
    file.seekg(c.body);
    file.read(out.data(), std::streamsize(c.size));
    return file ? out : std::string();
}

struct Format {
    unsigned rate = 0, align = 0;
    uint64_t frames = 0;
};
Format format_of(std::ifstream& file, const std::vector<Chunk>& chunks) {
    Format f;
    for (const auto& c : chunks) {
        if (is(c, "fmt ") && c.size >= 16) {
            auto b = body_of(file, c);
            f.rate = juce::ByteOrder::littleEndianInt(b.data() + 4);
            f.align = juce::ByteOrder::littleEndianShort(b.data() + 12);
        }
        if (is(c, "data") && f.align) f.frames = c.size / f.align;
    }
    return f;
}

double number(const juce::var& o, const char* key) { return o.hasProperty(key) ? double(o[key]) : 0.0; }

juce::var slice_json(int64_t start_frame, int64_t end_frame) {
    const auto start = juce::int64(start_frame), end = juce::int64(end_frame);
    auto* o = new juce::DynamicObject();
    o->setProperty("Start", start);
    o->setProperty("End", end);
    o->setProperty("LoopStart", start);
    o->setProperty("LoopMode", 0);
    o->setProperty("PulsePosition", 0);
    o->setProperty("LoopCrossfadeLength", 0);
    o->setProperty("LoopCrossfadeType", 0);
    o->setProperty("TailLength", 0.0);
    o->setProperty("TailLoopPosition", 0.5);
    return juce::var(o);
}

} // namespace

SampleChop read_mpc_slices(const std::string& path) {
    SampleChop chop;
    std::ifstream file(path, std::ios::binary);
    if (!file) return chop;
    const auto chunks = chunks_of(file);
    const Format f = format_of(file, chunks);
    if (!f.rate || !f.frames) return chop;
    std::string text;
    for (const auto& c : chunks) if (is(c, "atem")) text = body_of(file, c);
    if (text.empty()) return chop;
    const auto value = juce::JSON::parse(juce::String::fromUTF8(text.c_str()))["value0"]; // up to its first 0
    const int count = int(number(value, "Num slices"));
    if (count <= 0) return chop;
    const double scale = double(dsp::model_rate) / f.rate;
    const uint32_t model_frames = uint32_t(std::llround(double(f.frames) * scale));
    // A frame of the file in the model's; an end at (or past) the file's last frame is the sample's end (0).
    auto model = [&](double at) { return uint32_t(std::clamp<int64_t>(std::llround(at * scale), 0, model_frames)); };
    auto end_of = [&](double at) { return at + 1 >= double(f.frames) ? 0u : model(at); };
    struct Region { uint32_t start, stop; };
    std::vector<Region> found;
    for (int n = 0; n < count && found.size() < max_slices; ++n) {
        const auto slice = value[juce::Identifier("Slice " + juce::String(n))];
        if (!slice.isObject()) continue;
        const double start = number(slice, "Start"), end = number(slice, "End");
        if (end <= start || start >= double(f.frames)) continue;
        found.push_back({model(start), end_of(end)});
    }
    std::sort(found.begin(), found.end(), [](const Region& a, const Region& b) { return a.start < b.start; });
    auto& sl = chop.slices;
    for (const auto& r : found) {
        const uint32_t stop = r.stop ? r.stop : model_frames;
        if (stop <= r.start) continue;
        if (sl.count && r.start < sl.end(sl.count - 1, model_frames)) continue; // overlapping the one before
        sl.start[sl.count] = r.start; sl.stop[sl.count] = r.stop; ++sl.count;
    }
    if (!sl.count) return chop;
    const auto& whole = value["defaultSlice"];
    chop.from = whole.isObject() ? model(number(whole, "Start")) : 0;
    chop.to = whole.isObject() && whole.hasProperty("End") ? end_of(number(whole, "End")) : 0;
    chop.mode = ChopMode::Manual; // the MPC's chops say no type: Manual
    // Rhythmer's own chunk ("mlch", which other software passes over): the chop's type and amount, and the cuts made or
    // taken out by hand, so a chop opened again after a restart is told apart as it was left.
    for (const auto& c : chunks) {
        if (!is(c, "mlch")) continue;
        const auto ours = juce::JSON::parse(juce::String::fromUTF8(body_of(file, c).c_str()));
        if (!ours.isObject()) break;
        chop.mode = ChopMode(std::clamp(int(number(ours, "Type")), 0, int(ChopMode::Count) - 1));
        chop.amount = int16_t(number(ours, "Amount"));
        if (const auto* hand = ours["Hand"].getArray()) for (const auto& at : *hand) chop.hand.pin(model(double(at)));
        if (const auto* out = ours["Out"].getArray()) for (const auto& at : *out) chop.hand.take_out(model(double(at)));
        if (const auto* back = ours["Reverse"].getArray()) // the slices reversed on their own, by where they start
            for (const auto& at : *back) {
                const uint32_t from = model(double(at));
                for (unsigned n = 0; n < sl.count; ++n)
                    if ((sl.start[n] > from ? sl.start[n] - from : from - sl.start[n]) <= 1) sl.reverse[n] = true;
            }
        break;
    }
    return chop;
}

bool write_mpc_slices(const std::string& path, const SampleChop& chop, bool in_place) {
    static std::mutex writing; // one file at a time (Context::write_chops' threads, an export)
    std::lock_guard<std::mutex> one(writing);
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const auto chunks = chunks_of(in);
    const Format f = format_of(in, chunks);
    if (chunks.empty() || !f.rate || !f.frames) return false;
    const double scale = double(f.rate) / dsp::model_rate;
    const uint32_t model_frames = uint32_t(std::llround(double(f.frames) / scale));
    auto file_frame = [&](uint32_t at) { return std::clamp<int64_t>(std::llround(at * scale), 0, int64_t(f.frames)); };

    // The "atem" JSON: the file's own (its key, tempo and default slice kept), its slices replaced.
    juce::var doc;
    bool had_atem = false;
    for (const auto& c : chunks)
        if (is(c, "atem")) { doc = juce::JSON::parse(juce::String::fromUTF8(body_of(in, c).c_str())); had_atem = true; }
    if (!doc.isObject()) {
        doc = juce::var(new juce::DynamicObject());
        doc.getDynamicObject()->setProperty("version", 1);
    }
    if (!doc["value0"].isObject()) {
        auto* value = new juce::DynamicObject();
        value->setProperty("defaultSlice", slice_json(0, int64_t(f.frames) - 1));
        value->setProperty("numBars", 2);
        doc.getDynamicObject()->setProperty("value0", juce::var(value));
    }
    auto* value = doc["value0"].getDynamicObject();
    for (int n = 0; value->hasProperty(juce::Identifier("Slice " + juce::String(n))); ++n) value->removeProperty(juce::Identifier("Slice " + juce::String(n)));
    const auto& sl = chop.slices;
    value->setProperty("Num slices", int(sl.count));
    std::string slic;
    auto put = [&](uint32_t v, int bytes) { for (int b = 0; b < bytes; ++b) slic.push_back(char((v >> (8 * b)) & 0xff)); };
    put(1, 2); put(sl.count, 2); put(0, 2);
    for (unsigned n = 0; n < sl.count; ++n) {
        const int64_t start = file_frame(sl.start[n]), end = file_frame(sl.end(n, model_frames));
        value->setProperty(juce::Identifier("Slice " + juce::String(n)), slice_json(start, end));
        put(uint32_t(start), 4); put(uint32_t(end), 4); put(uint32_t(start), 4); put(0, 2);
    }
    const std::string atem = juce::JSON::toString(doc).toStdString();
    // Rhythmer's own chunk: the type, its amount, the cuts made by hand and the ones taken out, and the starts of the
    // slices reversed on their own (at the file's rate).
    std::string mlch;
    if (sl.count) {
        auto* ours = new juce::DynamicObject();
        ours->setProperty("Type", int(chop.mode));
        ours->setProperty("Amount", int(chop.amount));
        juce::Array<juce::var> hand, out;
        for (unsigned n = 0; n < chop.hand.pin_count; ++n) hand.add(juce::int64(file_frame(chop.hand.pins[n])));
        for (unsigned n = 0; n < chop.hand.drop_count; ++n) out.add(juce::int64(file_frame(chop.hand.drops[n])));
        ours->setProperty("Hand", hand);
        ours->setProperty("Out", out);
        juce::Array<juce::var> back;
        for (unsigned n = 0; n < sl.count; ++n) if (sl.reverse[n]) back.add(juce::int64(file_frame(sl.start[n])));
        if (!back.isEmpty()) ours->setProperty("Reverse", back);
        mlch = juce::JSON::toString(juce::var(ours), true).toStdString();
    }

    if (in_place) {
        // The two chunks as they go in the file, each with its pad byte.
        std::string blob;
        auto chunk = [&](const char* id, const std::string& body) {
            blob.append(id, 4);
            for (int b = 0; b < 4; ++b) blob.push_back(char((uint32_t(body.size()) >> (8 * b)) & 0xff));
            blob += body;
            if (body.size() & 1) blob.push_back('\0');
        };
        if (sl.count || had_atem) chunk("atem", atem); // no slices: the file's key and tempo still kept
        if (sl.count) { chunk("slic", slic); chunk("mlch", mlch); }
        size_t data = chunks.size();
        for (size_t n = 0; n < chunks.size() && data == chunks.size(); ++n) if (is(chunks[n], "data")) data = n;
        if (data == chunks.size()) return false;
        in.close();
        auto ours = [](const Chunk& c) { return is(c, "atem") || is(c, "slic") || is(c, "mlch") || is(c, "JUNK"); };
        // The room right before the audio: the atem, slic and JUNK chunks there.
        const std::streamoff audio = chunks[data].body - 8;
        std::streamoff room_at = audio;
        for (size_t n = data; n > 0 && ours(chunks[n - 1]); --n) room_at = chunks[n - 1].body - 8;
        const auto room = uint64_t(audio - room_at);
        const bool before = room == blob.size() || room >= blob.size() + 8;
        // Else the end of the file: after the audio and whatever else follows it, over the atem, slic and JUNK
        // chunks a write before left there.
        std::error_code error;
        const auto size = uint64_t(std::filesystem::file_size(path, error));
        if (error) return false;
        uint64_t tail = size;
        for (size_t n = chunks.size(); n > data + 1 && ours(chunks[n - 1]); --n) tail = uint64_t(chunks[n - 1].body - 8);
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) return false;
        auto word = [&](uint32_t v) { for (int b = 0; b < 4; ++b) file.put(char((v >> (8 * b)) & 0xff)); };
        for (const auto& c : chunks) // the old ones, wherever they are
            if ((is(c, "atem") || is(c, "slic") || is(c, "mlch")) && uint64_t(c.body - 8) < tail) { file.seekp(c.body - 8); file.write("JUNK", 4); }
        if (before) {
            file.seekp(room_at);
            file.write(blob.data(), std::streamsize(blob.size()));
            if (room > blob.size()) { // what is left over, a JUNK chunk
                const uint64_t left = room - blob.size() - 8;
                file.write("JUNK", 4); word(uint32_t(left));
                const std::string zeros(size_t(left), '\0');
                file.write(zeros.data(), std::streamsize(zeros.size()));
            }
            if (tail < size) { // what a write after the audio left: gone
                file.close();
                std::filesystem::resize_file(path, tail, error);
                file.open(path, std::ios::binary | std::ios::in | std::ios::out);
                if (!file || error) return false;
            }
        } else {
            file.close();
            if (tail < size) std::filesystem::resize_file(path, tail, error);
            file.open(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!file || error) return false;
            file.seekp(0, std::ios::end);
            if (uint64_t(file.tellp()) & 1) file.put('\0'); // a last chunk's pad byte some files leave off
            file.write(blob.data(), std::streamsize(blob.size()));
        }
        file.seekp(0, std::ios::end);
        const auto now = uint64_t(file.tellp());
        if (now > 0xffffffffull) return false;
        file.seekp(4);
        word(uint32_t(now - 8));
        file.flush();
        return bool(file);
    }

    // Written beside: every chunk as it was but the old atem and slic, the new ones before the samples.
    const std::string temp = path + ".slices";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        auto word = [&](uint32_t v) { for (int b = 0; b < 4; ++b) out.put(char((v >> (8 * b)) & 0xff)); };
        auto chunk = [&](const char* id, const std::string& body) {
            out.write(id, 4); word(uint32_t(body.size())); out.write(body.data(), std::streamsize(body.size()));
            if (body.size() & 1) out.put('\0');
        };
        out.write("RIFF\0\0\0\0WAVE", 12);
        std::vector<char> block(1 << 16);
        bool placed = false;
        for (const auto& c : chunks) {
            if (is(c, "atem") || is(c, "slic") || is(c, "mlch")) continue;
            if (is(c, "data") && !placed) {
                if (sl.count || had_atem) chunk("atem", atem); // no slices: the file's key and tempo still kept
                if (sl.count) { chunk("slic", slic); chunk("mlch", mlch); }
                placed = true;
            }
            out.write(c.id, 4); word(c.size);
            in.clear(); in.seekg(c.body);
            for (uint64_t left = c.size + (c.size & 1); left && in;) {
                const auto take = std::streamsize(std::min<uint64_t>(left, block.size()));
                in.read(block.data(), take);
                const auto got = in.gcount();
                out.write(block.data(), got);
                left -= uint64_t(got);
                if (got < take) { if (left == 1) out.put('\0'); break; } // a last pad byte some files leave off
            }
        }
        const auto size = uint64_t(out.tellp());
        out.seekp(4);
        word(uint32_t(size - 8));
        if (!out || size > 0xffffffffull) { out.close(); std::error_code e; std::filesystem::remove(temp, e); return false; }
    }
    in.close();
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) { std::filesystem::remove(temp, error); return false; }
    return true;
}

} // namespace rhythmer
