// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/lesson_jobs.h"
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/engine/engine_info.h"
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/menu.h"
#include "rhythmer/shell/project_kits.h"
#include "rhythmer/shell/project_store.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace rhythmer {
namespace {

// What a lesson's project holds before its first step: for Sequences, two sequences of track 1 to switch between.
void lesson_patterns(unsigned lesson, PatternBank& bank) {
    if (lesson != 1) return;
    auto note = [&](unsigned pattern, unsigned sixteenth, unsigned pad, uint8_t velocity) {
        Event e;
        e.tick = sixteenth * sixteenth_ticks;
        e.key = uint8_t(sampler_first_note + pad);
        e.velocity = velocity;
        bank.at[0][pattern].add(e);
    };
    for (unsigned s : {0u, 8u}) note(0, s, 0, 110);              // sequence 1: pad 1 on beats 1 and 3, pad 2 on 2 and 4
    for (unsigned s : {4u, 12u}) note(0, s, 1, 100);
    for (unsigned s : {0u, 6u, 8u, 14u}) note(1, s, 0, 110);     // sequence 2: busier, pad 3 on every eighth
    for (unsigned s : {4u, 12u}) note(1, s, 1, 100);
    for (unsigned s = 0; s < 16; s += 2) note(1, s, 2, 80);
}

} // namespace

void lesson_start(Context& c, unsigned lesson) {
    auto& ui = c.ui;
    ui.menu = menu_closed;
    if (!c.store) { c.notice("Lessons need the card"); return; }
    if (ui.recording || ui.rec_armed) { c.notice("Lessons: not in a take"); return; }
    if (c.import_unsaved) { c.notice("Save the project first: Project, B1"); return; } // an MPC project just opened: no file of its own
    if (!std::filesystem::exists(factory_kit_path)) { c.notice("Lessons need the MPC's factory kits"); return; }
    if (c.host.playing()) c.stop();
    if (c.dirty && !c.autosave()) { c.notice("Lessons need the card: the project is not saved"); return; }
    const std::string back = c.project_path; // empty: a new project nothing was done to
    auto fresh = std::make_unique<Project>();
    auto bank = std::make_unique<PatternBank>();
    starting_project(*fresh, *bank);
    load_sound(fresh->tracks[0], EngineKind::Sampler, 0); // an empty kit until the factory kit lands on it
    std::snprintf(fresh->name, sizeof(fresh->name), "Lesson");
    lesson_patterns(lesson % lesson_count, *bank);
    if (lesson % lesson_count == 1) { fresh->tracks[0].in_scene[0] = 1; fresh->tracks[0].in_scene[1] = 2; } // SEQ 1 keeps P1, SEQ 2 P2
    c.switch_to(*fresh, *bank, {});
    c.lesson_return = back;
    c.ui.lesson = uint8_t(lesson % lesson_count + 1);
    c.ui.lesson_step = 0; c.ui.lesson_pot = -1; c.ui.lesson_with = -1;
    c.ui.lesson_at = c.now;
    c.load_kit_file(factory_kit_path, factory_kit_name); // onto track 1; the first step waits for it
}

void lesson_leave(Context& c, bool done) {
    auto& ui = c.ui;
    if (!ui.lesson) return;
    const unsigned lesson = (ui.lesson - 1u) % lesson_count;
    ui.lesson = 0; ui.lesson_step = 0; ui.lesson_with = -1;
    ui.menu = menu_closed;
    if (done) {
        ui.settings.lessons_done |= uint8_t(1u << lesson);
        if (c.store) c.store->save_settings(ui.settings.text());
    }
    if (c.host.playing()) c.stop();
    // A kit still on its way keeps its name in kit_from: it lands as a kit on the project open then, never as a project.
    const std::string back = c.lesson_return;
    c.lesson_return.clear();
    c.dirty = false; // the lesson's project is never saved
    if (!back.empty() && c.store) c.store->load(back); // the project open before, from its file
    else c.new_project();
    c.notice(done ? std::string(lesson_at(lesson).name) + ": done" : "Lesson left");
}

void lesson_tick(Context& c, uint32_t now) {
    auto& ui = c.ui;
    if (!ui.lesson) return;
    if (lesson_finished(ui)) { // DONE shows a moment, then the project opens again
        if (now - ui.lesson_at >= lesson_done_ms) lesson_leave(c, true);
        return;
    }
    lesson_page(ui, now); // a step that opens a page: done once it is open
    const LessonStep* s = lesson_step(ui);
    if (s && s->kind == LessonKind::Count) { // the take has begun: playing, the count-in over (or off)
        if (now - ui.lesson_at >= lesson_count_ms && c.host.playing() && !c.host.count_in()) lesson_next(ui, now);
        return;
    }
    if (!s || s->kind != LessonKind::Wait) return;
    const bool loaded = !ui.loading && c.kit_from.empty() && c.samples_to_load >= sample_slots && kit_track(c.project) >= 0;
    if (loaded) {
        stage_go(ui, c.track().engine, StageKind::Trim);
        lesson_next(ui, now);
    }
    else if (now - ui.lesson_at >= lesson_wait_ms) {
        c.notice("Lessons need the MPC's factory kits");
        c.kit_from.clear(); // it never came
        lesson_leave(c, false);
    }
}

} // namespace rhythmer
