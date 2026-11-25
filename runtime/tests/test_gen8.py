import sys
from sources import source
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Gen8Tests(unittest.TestCase):
    def test_break_genres_seed_mutation_and_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'gen8-test'
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g1',
                            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror',
                            '-Isrc','-Icore/src','-Ibase/src',source('src/rhythmer/model/project.cpp'),'tests/gen8.cpp',
                            source('src/rhythmer/model/project_file.cpp'),source('src/rhythmer/model/pattern.cpp'),
                            source('src/rhythmer/engine/engine_info.cpp'),
                            source('src/rhythmer/model/print.cpp'),source('src/rhythmer/model/gen8.cpp'),source('src/rhythmer/model/break_gen.cpp'),'-o',str(binary)],
                           cwd=ROOT,check=True)
            env=dict(os.environ,ASAN_OPTIONS=('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
            subprocess.run([str(binary)],cwd=ROOT,env=env,check=True,timeout=120)
