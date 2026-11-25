import os,subprocess,unittest,sys
from sources import source
from pathlib import Path
class BreakAcidTests(unittest.TestCase):
 def test_original_performance_and_acid(self):
  root=Path(__file__).resolve().parents[1]
  flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
  subprocess.run(['cc',*flags,'-fno-sanitize=shift','-c','tests/break_reference.c','-o','build/break-reference.o'],cwd=root,check=True)
  subprocess.run(['cc',*flags,'-c','tests/acid_reference.c','-o','build/acid-reference.o'],cwd=root,check=True)
  subprocess.run(['c++','-std=c++17',*flags,'-Isrc','-Icore/src','-Ibase/src','tests/break_acid.cpp',source('src/rhythmer/engine/break_manipulator.cpp'),source('src/rhythmer/engine/stream_pager.cpp'),source('src/rhythmer/engine/sample_data.cpp'),source('src/rhythmer/model/acid_gen.cpp'),source('src/rhythmer/model/pattern.cpp'),'build/break-reference.o','build/acid-reference.o','-lm','-o','build/break-acid-test'],cwd=root,check=True)
  subprocess.run(['build/break-acid-test'],cwd=root,env=dict(os.environ,ASAN_OPTIONS=('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'),check=True,timeout=120)
