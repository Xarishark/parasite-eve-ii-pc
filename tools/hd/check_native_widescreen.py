#!/usr/bin/env python3
"""Compare original/wide native-background rendering at fixed source locations.

Uses private profiles, a deterministic controller trace and hidden GPU windows.
Captures contain game data and belong in an ignored local build directory.
This is a scoped static-camera check, not a traversal or all-platform proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from survey import startup_replay, validate_capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build-port/parasite-eve-ii-pc'))
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--disc2', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, default=Path('build-hd-study/view-catalog.json'))
    parser.add_argument('--location', action='append', help='Six comma-separated location components; repeatable')
    parser.add_argument('--scale', type=int, default=4, choices=range(2, 9))
    parser.add_argument('--backend', choices=['gpu', 'software'], default='gpu')
    parser.add_argument('--output', type=Path, default=Path('build-hd-study/native-widescreen-check'))
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    proof = root / 'proof.json'
    proof.unlink(missing_ok=True)
    catalog_bytes = args.catalog.read_bytes()
    catalog = json.loads(catalog_bytes)['records']
    locations = args.location or ['1,20,8,1,1,1', '1,15,6,1,2,1']
    targets = []
    for location in locations:
        key = [int(x) for x in location.split(',')]
        target = next((x for x in catalog if x['location'] == key and x.get('eligible_for_start_test')), None)
        if target is None:
            parser.error(f'No eligible catalog entry for {location}')
        targets.append(target)
    binary = args.binary.resolve()
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    replay = root / 'startup.pad'
    startup_replay(replay)
    results = []
    for target in targets:
        name = '-'.join(map(str, target['location']))
        pair = []
        for wide in [0, 1]:
            out = root / name / str(wide)
            out.mkdir(parents=True, exist_ok=True)
            for item in ['report.json', 'frame.ppm', 'vram.bin']:
                (out / item).unlink(missing_ok=True)
            with tempfile.TemporaryDirectory(prefix='profile-', dir=root) as profile:
                Path(profile, 'settings.ini').write_text(f'scale={args.scale}\ngeometry=1\naa=0\n')
                env = {k: v for k, v in os.environ.items() if not k.startswith('PE2_')}
                env.update(PSYZ_HEADLESS='gpu' if args.backend == 'gpu' else '1', PSYZ_RASTER_CLOCK='virtual',
                           SDL_AUDIODRIVER='dummy', PE2_SKIPINTRO='1', PE2_CONFIG_DIR=profile,
                           PE2_MEMORY_CARD_DIR=str(Path(profile) / 'cards'), PE2_PAD_REPLAY=str(replay),
                           PE2_START_LOCATION=','.join(map(str, target['location'])), PE2_SURVEY_KEEP_VIEW='1',
                           PE2_SURVEY_TARGET=','.join(map(str, target['location'][:4])) + ':' + target['keys'][0],
                           PE2_SURVEY_OUTPUT=str(out))
                command = [str(binary), '--disc', str(args.disc.resolve()), '--disc2', str(args.disc2.resolve()),
                           '--widescreen', str(wide)]
                (out / 'command.json').write_text(json.dumps(command) + '\n')
                with (out / 'run.log').open('w') as log:
                    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
            report, native = validate_capture(out, target)
            pair.append((report, native))
            print(name, wide, report['width'], report['height'], flush=True)
        checks = {key: pair[0][0][key] == pair[1][0][key]
                  for key in ['simulation_state', 'models', 'player_pose', 'player_root']}
        checks['native_vram'] = pair[0][1] == pair[1][1]
        checks['height'] = pair[0][0]['height'] == pair[1][0]['height']
        if args.backend == 'gpu':
            checks['wide_width'] = pair[1][0]['width'] > pair[0][0]['width'] * 1.3
        else:
            checks['software_fallback'] = (root / name / '0/frame.ppm').read_bytes() == (root / name / '1/frame.ppm').read_bytes()
        assert all(checks.values()), (name, checks)
        results.append(dict(location=target['location'], key=target['keys'][0], checks=checks,
                            reports=[x[0] for x in pair]))
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_hash, 'Binary changed during test'
    assert args.catalog.read_bytes() == catalog_bytes, 'Catalog changed during test'
    proof.write_text(json.dumps(dict(passed=True, binary_sha256=binary_hash, backend=args.backend,
                                    scale=args.scale, cases=results, scope=__doc__), indent=2) + '\n')


if __name__ == '__main__':
    main()
