# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 jazzy rhythmer
"""Display-only external tempo smoothing; no audio/MIDI processing changes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class TempoDisplayTests(unittest.TestCase):
    def test_jitter_fractional_changes_reset_and_update_limit(self):
        with tempfile.TemporaryDirectory() as temp:
            binary=Path(temp)/"tempo-display"
            subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-Wall","-Wextra","-Werror","-I"+str(ROOT/"src"),str(ROOT/"tests/tempo_display.cpp"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
