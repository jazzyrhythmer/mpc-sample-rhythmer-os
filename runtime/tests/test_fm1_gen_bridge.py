import subprocess,unittest
from pathlib import Path
class Fm1OriginalGenTests(unittest.TestCase):
 def test_original_seed_streams_and_four_tracks(self):
  root=Path(__file__).resolve().parents[1]
  binary=root/'build/fm1-gen-check'
  subprocess.run(['cc','-O1','-Ifm1-gen','tests/fm1_gen_bridge_test.c','build/obj/fm1-gen.o','-lm','-o',str(binary)],cwd=root,check=True)
  subprocess.run([str(binary)],cwd=root,check=True,timeout=120)
