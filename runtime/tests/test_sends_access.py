"""Native button-routing regression for the BASIC mixer SENDS doorway."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class SendsAccessTests(unittest.TestCase):
    def test_native_buttons_and_mode_change(self):
        binary = ROOT / 'build/rhythmer'
        if not binary.is_file():
            self.skipTest('Build the runtime first')
        with tempfile.TemporaryDirectory() as out:
            p = subprocess.run([str(binary), '--rhythmer-sends-check', out], capture_output=True, text=True, timeout=120)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            self.assertIn('SENDS_ACCESS_OK', p.stdout)
if __name__ == '__main__':
    unittest.main()
