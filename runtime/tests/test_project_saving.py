"""Project save/recovery safety checks against the built runtime and real file I/O."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProjectSavingTests(unittest.TestCase):
    def test_atomic_file_and_media(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / 'recovery-file'
            includes = ROOT / ('src/rhythmer' if (ROOT / 'src/rhythmer').is_dir() else 'extensions')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(includes), str(ROOT / 'tests/recovery_file.cpp'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), str(Path(temp) / 'files')], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('RECOVERY_FILE_OK', result.stdout)

    def test_runtime_controls_and_store(self):
        binary = ROOT / 'build/rhythmer'
        if not binary.is_file():
            self.skipTest('Build the runtime first')
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.run([str(binary), '--rhythmer-project-save-check', temp],
                                    capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('PROJECT_SAVING_OK', result.stdout)


if __name__ == '__main__':
    unittest.main()
