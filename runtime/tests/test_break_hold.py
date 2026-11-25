import sys
from sources import source
import os, subprocess, unittest
from pathlib import Path
class BreakHoldTests(unittest.TestCase):
 def test_live_holds_and_recorded_gates(self):
  root=Path(__file__).resolve().parents[1]
  subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Isrc','-Icore/src','tests/break_hold.cpp',source('src/rhythmer/engine/break_manipulator.cpp'),'-o','build/break-hold-test'],cwd=root,check=True)
  subprocess.run(['build/break-hold-test'],cwd=root,env=dict(os.environ,ASAN_OPTIONS=('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'),check=True,timeout=120)
