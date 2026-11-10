#!/bin/sh
# Runs the hd tool regression suite with unittest (they need no disc data, only
# Pillow/numpy from requirements.txt). hd_native_decode_test builds the decoder
# it drives. Run from the repository root.
set -e
if [ -f venv/bin/python3 ]; then PY=venv/bin/python3; else PY=python3; fi
test_build=${1:-build-port}
cmake --build "$test_build" --target hd_native_decode --parallel
export PE2_HD_TEST_DECODER="$(cd "$test_build" && pwd)/hd_native_decode"
PYTHONPATH=tools/hd "$PY" -m unittest discover -s tests -p 'hd_*_test.py' -v
