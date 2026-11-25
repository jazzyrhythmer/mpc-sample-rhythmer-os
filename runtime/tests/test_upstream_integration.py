import os,subprocess,tempfile,unittest,sys
from pathlib import Path
from sources import source
class UpstreamIntegrationTests(unittest.TestCase):
    def test_custom_and_upstream_state_and_control_ids(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'integration'
            subprocess.run(['c++','-std=c++17','-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                            '-Wall','-Wextra','-Werror','-Isrc','-Icore/src','-Ibase/src','tests/upstream_integration.cpp',
                            *[source(x) for x in ('src/rhythmer/model/project.cpp','src/rhythmer/model/project_file.cpp','src/rhythmer/model/pattern.cpp',
                            'src/rhythmer/engine/engine_info.cpp','src/rhythmer/model/print.cpp')],'-o',str(binary)],cwd=root,check=True)
            subprocess.run([str(binary)],cwd=root,check=True,timeout=60,
                           env=dict(os.environ,ASAN_OPTIONS=('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
