// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "surface/controls.h"

// The undo list's jobs (shell/history_list.h has its rules).
namespace rhythmer {

class Context;

void history_open(Context& c);                       // Shift + Minus held: the list, on NOW; with no steps, a notice
bool history_button(Context& c, SurfaceButton b);    // a press while it is open; false: the button's own job (it closes first)
void history_wheel(Context& c, int delta);           // the wheel, Minus and Plus move the cursor
void history_go(Context& c);                         // Push or B3: to the state after the step under the cursor

} // namespace rhythmer
