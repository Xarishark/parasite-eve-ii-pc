"""Native MDEC integration with generated test images, no disc assets."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/peassets'))
from bs_codec import encode_bs_v2

DECODER=Path(os.environ.get('PE2_HD_TEST_DECODER', 'build-port/hd_native_decode')).resolve()

class DecodeTest(unittest.TestCase):
    def test_exact_gray_values_and_column_to_row_layout(self):
        image=np.full((240,320,3),64,np.uint8);image[:,160:]=192
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'synthetic.bs';source.write_bytes(encode_bs_v2(image))
            target=root/'native.rgb555'
            run=subprocess.run([str(DECODER),str(source),str(target)],capture_output=True,text=True,
                               env={**os.environ,'PSYZ_HEADLESS':'1','SDL_AUDIODRIVER':'dummy'},timeout=10)
            self.assertEqual(run.returncode,0,run.stderr)
            decoded=np.frombuffer(target.read_bytes(),dtype='<u2').reshape(240,320)
            self.assertTrue(np.all(decoded[:,:160]==0x2108))
            self.assertTrue(np.all(decoded[:,160:]==0x6318))
    def test_invalid_header_fails_without_an_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'bad.bs';source.write_bytes(bytes(12));output=root/'output'
            run=subprocess.run([str(DECODER),str(source),str(output)],capture_output=True,
                               env={**os.environ,'PSYZ_HEADLESS':'1'},timeout=10)
            self.assertEqual(run.returncode,2);self.assertFalse(output.exists())

if __name__=='__main__':unittest.main()
