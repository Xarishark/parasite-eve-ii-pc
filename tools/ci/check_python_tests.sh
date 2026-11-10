#!/bin/sh
# Runs the hd tool regression suite with unittest (they need no disc data, only
# Pillow/numpy from requirements.txt). hd_native_decode_test builds the decoder
# it drives. Run from the repository root.
set -e
if [ -f venv/bin/python3 ]; then PY=venv/bin/python3; else PY=python3; fi
cmake --build "${1:-build-port}" --target hd_native_decode --parallel
PYTHONPATH=tools/hd "$PY" -m unittest discover -s tests -p 'hd_*_test.py' -v