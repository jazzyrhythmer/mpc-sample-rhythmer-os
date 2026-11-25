# SPDX-License-Identifier: AGPL-3.0-only
"""Rhythmer's tests that need its shell (the core's own are core/tests), built for the host."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from sources import source  # noqa: E402
SOURCES = ["src/rhythmer/engine/engine_info.cpp", "src/rhythmer/engine/engine_host.cpp", "src/rhythmer/engine/channel_strip.cpp", "src/rhythmer/engine/looper.cpp",
           "src/rhythmer/shell/bindings.cpp", "src/rhythmer/shell/spectrum_view.cpp", "src/rhythmer/shell/loop_sheet.cpp", "src/rhythmer/engine/effects.cpp", "src/rhythmer/engine/effects_extra.cpp", "src/rhythmer/engine/master_comp.cpp", "src/rhythmer/engine/effect_info.cpp", "src/rhythmer/engine/macros.cpp",
           "src/rhythmer/engine/sampler.cpp", "src/rhythmer/engine/sample_data.cpp", "src/rhythmer/engine/onsets.cpp", "src/rhythmer/engine/auto_chop.cpp", "src/rhythmer/engine/root_find.cpp", "src/rhythmer/engine/sfz.cpp", "src/rhythmer/engine/keygroup.cpp", "src/rhythmer/engine/exs.cpp", "src/rhythmer/engine/sf2.cpp", "src/rhythmer/engine/dspreset.cpp", "src/rhythmer/engine/stream_pager.cpp", "src/rhythmer/engine/render_pool.cpp", "src/rhythmer/shell/sample_cache.cpp",
           "src/rhythmer/model/project.cpp", "src/rhythmer/model/project_file.cpp", "src/rhythmer/model/pattern.cpp", "src/rhythmer/model/note_edits.cpp", "src/rhythmer/model/smf.cpp", "src/rhythmer/model/generators.cpp", "src/rhythmer/engine/punch.cpp", "src/rhythmer/engine/print_render.cpp", "src/rhythmer/engine/print_player.cpp", "src/rhythmer/engine/audio_player.cpp", "src/rhythmer/model/print.cpp", "src/rhythmer/engine/punch_mpc.cpp", "src/rhythmer/engine/pad_fx_info.cpp", "src/rhythmer/model/sound_file.cpp",
           "src/rhythmer/shell/page_stack.cpp", "src/rhythmer/shell/button_jobs.cpp", "src/rhythmer/shell/roll.cpp",
           "src/rhythmer/shell/shift_pad_jobs.cpp", "src/rhythmer/shell/fader_job.cpp", "src/rhythmer/shell/midi_route.cpp", "src/rhythmer/shell/mtc_in.cpp",
           "src/rhythmer/shell/undo_scope.cpp", "src/rhythmer/shell/project_kits.cpp", "src/rhythmer/shell/card_access.cpp",
           "src/rhythmer/shell/pad_lights.cpp", "src/rhythmer/plugin/nam.cpp", "src/rhythmer/shell/xpm_write.cpp", "src/rhythmer/engine/loop_find.cpp",
           "src/rhythmer/engine/auto_probe.cpp", "src/rhythmer/engine/release_fit.cpp", "src/rhythmer/engine/external_notes.cpp"]


SOURCES += ["src/rhythmer/engine/break_manipulator.cpp", "src/rhythmer/model/chord_gen.cpp"]


class RhythmerTests(unittest.TestCase):
    def test_layouts_bindings_engine_and_sequencer(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "rhythmer"
            compiled = [source(x) for x in SOURCES]
            if os.environ.get("RHYTHMER_TEST_OBJECTS"):
                compiled = [str(ROOT / "build/obj" / Path(x).relative_to("src").with_suffix(".o")) for x in SOURCES]
                missing = [x for x in compiled if not Path(x).is_file()]
                if missing: raise RuntimeError("Build runtime objects first: " + ", ".join(missing))
            # Object mode still tests the production -O2 engine/shell objects. The
            # assertion driver may be unoptimized to stay within the build VM's RAM.
            driver_opt = os.environ.get("RHYTHMER_TEST_DRIVER_OPT", "-O2")
            if driver_opt not in ("-O0", "-O1", "-O2"):
                raise ValueError("Unsupported test-driver optimization")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", driver_opt, "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "src"), "-I" + str(ROOT / "core/src"), "-I" + str(ROOT / "base/src"), str(ROOT / "tests/rhythmer.cpp"),
                            *compiled, "-pthread", "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("rhythmer ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
