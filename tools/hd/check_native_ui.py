#!/usr/bin/env python3
"""Check completed inventory/map frames with native-background widescreen.

Uses normal controller inputs, isolated profiles and hidden GPU windows. This
checks one forced-start location, not menu behavior in every gameplay state.
Captures contain game data and must stay in an ignored local directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build-port/parasite-eve-ii-pc'))
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--disc2', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build-hd-study/native-ui-check'))
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    proof = root / 'proof.json'
    proof.unlink(missing_ok=True)
    binary = args.binary.resolve()
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    cases = []
    for name, button in [('inventory', 8), ('map', 1)]:
        replay = root / (name + '.pad')
        with replay.open('w') as output:
            output.write('PE2PAD 1\n')
            for index in range(12001):
                mask = 8 if 150 <= index < 158 else button if 6000 <= index < 6008 else 0
                if 200 <= index < 4000 and index % 60 < 8:
                    mask |= 0x4000
                frame = bytes([0, 0x41, (~mask) & 255, ((~mask) >> 8) & 255]) + bytes(30)
                output.write(f'{index} {frame.hex()}\n')
        runs = []
        for wide in [0, 1]:
            out = root / name / str(wide)
            out.mkdir(parents=True, exist_ok=True)
            for item in ['report.json', 'frame.ppm', 'vram.bin']:
                (out / item).unlink(missing_ok=True)
            with tempfile.TemporaryDirectory(prefix='profile-', dir=root) as profile:
                Path(profile, 'settings.ini').write_text('scale=4\ngeometry=1\n')
                env = {key: value for key, value in os.environ.items() if not key.startswith('PE2_')}
                env.update(PSYZ_HEADLESS='gpu', PSYZ_RASTER_CLOCK='virtual', PSYZ_AUDIO_CLOCK='video',
                           SDL_AUDIODRIVER='dummy', PE2_SKIPINTRO='1', PE2_CONFIG_DIR=profile,
                           PE2_MEMORY_CARD_DIR=profile + '/cards', PE2_START_LOCATION='1,15,6,1,2,1',
                           PE2_SURVEY_KEEP_VIEW='1', PE2_PAD_REPLAY=str(replay),
                           PE2_CAPTURE_UI_FRAME='20', PE2_CAPTURE_OUTPUT=str(out))
                command = [str(binary), '--disc', str(args.disc.resolve()), '--disc2', str(args.disc2.resolve()),
                           '--widescreen', str(wide), '--no-hd-backgrounds']
                (out / 'command.json').write_text(json.dumps(command) + '\n')
                with (out / 'run.log').open('w') as log:
                    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
            report = json.loads((out / 'report.json').read_text())
            assert report['format'] == 'pe2-ui-frame-checkpoint-v1' and report['ui_frame'] == 20
            assert report['location'] == [1, 15, 6, 1, 2, 1]
            assert (report['width'], report['height']) == (1280, 960)
            assert report['buffer_y'] in [0, 272]
            assert (out / 'vram.bin').stat().st_size == 1024 * 512 * 2
            with Image.open(out / 'frame.ppm') as image:
                assert image.size == (1280, 960) and image.getbbox()
                image.save(out / 'frame.png')
            runs.append(report)
            print(name, wide, report['width'], report['height'], flush=True)
        classic, wide = root / name / '0', root / name / '1'
        assert (classic / 'frame.ppm').read_bytes() == (wide / 'frame.ppm').read_bytes(), name + ' pixels'
        assert (classic / 'vram.bin').read_bytes() == (wide / 'vram.bin').read_bytes(), name + ' native VRAM'
        assert runs[0]['simulation_state'] == runs[1]['simulation_state'], name + ' simulation'
        cases.append(dict(ui=name, pixels_identical=True, native_vram_identical=True, reports=runs))
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_hash, 'Binary changed during test'
    proof.write_text(json.dumps(dict(passed=True, binary_sha256=binary_hash, cases=cases, scope=__doc__), indent=2) + '\n')


if __name__ == '__main__':
    main()
