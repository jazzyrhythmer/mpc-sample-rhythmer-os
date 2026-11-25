#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Read usage logs (Rhythmer/Usage/usage-*.txt on a tester's card, written with
Settings' USAGE LOG on; shell/usage_log.h has the lines) and list where people
got stuck, most often first:

- presses, knobs and wheel turns that did nothing, by control and page;
- pages left within 2 s of opening them;
- undo within 5 s of an edit, by the edit and page;
- MENU tiles and cards picked;
- each lesson step's time (median and longest).

    python3 tools/usage_report.py usage-*.txt [folder ...]
"""
from collections import Counter, defaultdict
from pathlib import Path
import statistics
import sys

QUICK_MS = 2000
SOON_MS = 5000
TOP = 25


def lines(paths):
    for arg in paths:
        path = Path(arg)
        files = sorted(path.glob("usage-*.txt")) if path.is_dir() else [path]
        for file in files:
            for line in file.read_text(errors="replace").splitlines():
                words = line.split()
                if len(words) >= 2:
                    yield words[1], words[2:]


def report(paths):
    nojob, quick, undo, menu = Counter(), Counter(), Counter(), Counter()
    steps = defaultdict(list)
    starts = 0
    for kind, rest in lines(paths):
        if kind == "start":
            starts += 1
        elif kind == "nojob" and len(rest) >= 2:
            nojob[" on ".join(rest[:2])] += 1
        elif kind == "page" and len(rest) >= 3 and rest[2].isdigit():
            if int(rest[2]) < QUICK_MS:
                quick[f"{rest[1]} (to {rest[0]})"] += 1
        elif kind in ("undo", "redo") and len(rest) >= 3 and rest[0].isdigit():
            if kind == "undo" and int(rest[0]) < SOON_MS:
                undo[f"{rest[1]} on {rest[2]}"] += 1
        elif kind == "menu" and len(rest) >= 2:
            menu[" ".join(rest[:2])] += 1
        elif kind == "lesson" and len(rest) >= 3 and rest[2].isdigit():
            steps[f"lesson {rest[0]} step {rest[1]}"].append(int(rest[2]))

    print(f"{starts} log(s)\n")

    def table(title, counts):
        print(title)
        if not counts:
            print("  none\n")
            return
        for what, n in counts.most_common(TOP):
            print(f"  {n:5}  {what}")
        print()

    table("Did nothing (control on page)", nojob)
    table(f"Left within {QUICK_MS / 1000:g} s (page left, and where to)", quick)
    table(f"Undone within {SOON_MS / 1000:g} s (edit on page)", undo)
    table("MENU picks", menu)
    print("Lesson steps (median / longest, s)")
    if not steps:
        print("  none")
    for what in sorted(steps):
        times = steps[what]
        print(f"  {what}: {statistics.median(times) / 1000:.1f} / {max(times) / 1000:.1f}  ({len(times)} done)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    report(sys.argv[1:])
