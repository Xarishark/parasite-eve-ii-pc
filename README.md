# Parasite Eve II — native PC port

[![native-c](https://github.com/khasinski/parasite-eve-ii-pc/actions/workflows/pc.yml/badge.svg)](https://github.com/khasinski/parasite-eve-ii-pc/actions/workflows/pc.yml)

A native PC port of *Parasite Eve II* (PlayStation, USA release), built from the matching decompilation of the
original game code and [our PsyZ fork](https://github.com/khasinski/parasite-eve-psyz), a drop-in replacement for the
PlayStation runtime library (Psy-Q) on top of SDL3. It follows the architecture of
[parasite-eve-pc](https://github.com/khasinski/parasite-eve-pc), our port of the first game.

![Parasite Eve II running in the native port](docs/images/shooting-gallery.png)

## Status

**Pre-alpha.** The [0.3.3-alpha release](https://github.com/khasinski/parasite-eve-ii-pc/releases/tag/0.3.3) has downloads for macOS (Apple silicon, signed and notarized), Linux (x86-64 and ARM64) and Windows (experimental), plus a save editor. The game boots from your disc images and plays the
opening movie, the title screen and New Game. Recorded New Game input reaches the Acropolis helicopter landing pad,
including No. 9 combat and transformer damage. Native gameplay and save/reload have since been exercised through Dryfield and into the Shelter Laboratory on macOS; later story progression remains unverified.

Known limits:

- All five stages are linked and load (Akropolis, Dryfield, Dryfield by night, the mine, the Shelter / Neo Ark; the last two need disc 2), with native progression exercised into the Shelter Laboratory on macOS. Attract demos are disabled.
- Audio, movies and saves are incomplete.
- Played on macOS (arm64) only. The Linux ARM64 build was started with both discs on Ubuntu 24.04 and renders; Linux x86-64 and Windows are built in CI but not run on a real PC yet.
- The host menu has Graphics (detail, aspect, dithering, geometry correction, fullscreen, antialiasing, model filtering, smooth lighting), Controls, Audio and Discs pages; widescreen selection is available; HD background packs remain experimental (see [HD backgrounds](docs/HD_BACKGROUNDS.md)). Frame interpolation and mods are not there yet.

## What you need

Your own **North American CUE/BIN images of both discs**. Game data is not included and never will be: the port reads
your discs at run time.

Nothing else: the build uses committed layout sources (types, sizes, decoders; no game data) and the game reads the
discs when it starts. The tools that extract data from a disc are only for contributors (see below).

## Build

Requires Git, CMake 3.21 or newer and a C11 compiler.

```sh
git clone https://github.com/khasinski/parasite-eve-ii-pc
cd parasite-eve-ii-pc
git submodule update --init third_party/psyz
git -C third_party/psyz submodule update --init external/SDL
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
./build/parasite-eve-ii-pc "path/to/Parasite Eve II (Disc 1).cue"
```

Put the second disc next to the first (or pick it when the game asks for it). You can also set `PE2_DISC` instead of
passing the path. Host tests: `cmake --build build --target host_tests && ctest --test-dir build`. Nothing from your
disc is ever committed: `tools/ci/check_no_game_data.sh` enforces that.

**If it does not start:** a disc or start-up error is shown in a window and written to the log. Started from a
terminal, the log is the terminal itself. Started from Finder or a desktop launcher, it is `parasite-eve-ii.log` in the
preferences folder, rewritten on each start: `~/Library/Application Support/parasite-eve/parasite-eve-ii/` on macOS,
`~/.local/share/parasite-eve/parasite-eve-ii/` on Linux. Attach that file to a bug report. `PE2_LOG=<file>` writes the
log to a file of your choice.

Graphics → Aspect ratio offers Classic 4:3, Square pixels and Widescreen (16:9).
`--widescreen 0|1` overrides the saved preference in normal builds as well.
Use `--widescreen 1 --no-hd-backgrounds` to select native backgrounds explicitly.
On the GPU renderer, scenes without an extended background use the original
centred image with black side regions; models can occupy the wider view. No
scenery is generated. Selecting widescreen sets Picture detail to at least 2x.
Software rendering and native feedback/unsupported transition frames retain
4:3. Experimental artwork
packs still require `-DPE2_HD_BACKGROUNDS=ON`, which adds the HD background rows
and `--hd-backgrounds /path/to/pack`. See
[HD backgrounds](docs/HD_BACKGROUNDS.md) for the format and current limits.
`tools/ci/check_host_casts.sh [build-dir]` re-runs the game sources with the pointer/int cast warnings the build otherwise hides (`-w`); it must report none.

**Contributors:** the layout sources are committed and the extraction tooling (splat split, `ninja_config.py`, disc
extraction) is no longer in the tree. If a generator change needs them regenerated, check out the tag
`pe2-layout-tooling` in a separate worktree, run its `tools/port/refresh_layout.sh` there with your images in `rom/image/`,
and copy the result back (see [docs/PORT_PLAN.md](docs/PORT_PLAN.md)).

### Development switches

| Variable | Effect |
| --- | --- |
| `PE2_SKIPINTRO=1` | skip the opening movie |
| `PE2_LOG=<file>` | write standard output and error to this file (rewritten on each start) |
| `PE2_HD_BACKGROUNDS=<directory>` | experimental local PE2BG1 atlas pack (builds with `-DPE2_HD_BACKGROUNDS=ON` only); use Picture detail 2x+ and `--widescreen 0|1`; missing/invalid art retains 4:3 |
| `PE2_DUMP_BACKGROUNDS=<directory>` | export native RGB555 backdrops and observed logical locations for local artwork work; never commit these exports |
| `PE2_DUMP_FRAME=<tick>:<file.ppm>` | capture the actual presenter, including HD/widescreen framing; `PSYZ_HEADLESS=gpu` uses a hidden GPU window |
| `PE2_DUMP_VRAM=<tick>:<file>` | write the raw VRAM once at that pad tick; `tools/port/export_vram.py` turns it into a PNG (no screen capture needed) |
| `PE2_DEBUG_PORT=<port>` | start PsyZ's HTTP debug server on 127.0.0.1: `curl localhost:<port>/screenshot -o shot.png` captures what the window shows (the real renderer, unlike the VRAM dump) |
| `PE2_MENU_PAGE=<0-3>` | open the host menu on this page (with `--host-menu`) |
| `PE2_DUMP_MENU=<file.ppm>` | write the painted host menu (`--host-menu`) as a PPM, for review without a screen capture |
| `PE2_WARP=<tick>:stage,area,view,roomState,warp,variant[;<tick>:...]` | change stage during play at that pad tick (the session-start path a stage change takes); up to 8, separated by `;` |
| `PE2_START_LOCATION=stage,area,view,roomState,warp,variant` | new game starts there (stages 1-5; stages 4 and 5 need `--disc2`) |
| `PE2_FAST=1` | no frame limiter during movies |
| `PE2_AUTOPRESS=<tick>` | scripted Start/Cross from that tick |
| `PE2_PAD_REPLAY=<file>` / `PE2_PAD_RECORD=<file>` | replay or record input (see `tools/port/make_gallery_input.py`) |
| `PE2_DUMP_ASSETS=<file>` | write one hash per array loaded from the disc (to compare two builds or two discs) |

## How it is built

- The original game code is compiled natively with a host C11 compiler and linked against PsyZ. It is neither an
  emulator nor a static recompiler.
- The port is one-way: byte-matching the PS1 binaries is not a goal. Compiler workarounds from the decompilation
  (register pins, scheduling barriers, declaration order for the linker) are removed as the code is repaired.
- Game data is loaded from the disc images at run time. Disc records (32-bit offsets and addresses) are decoded into
  host structures; pointers are never stored in 32-bit fields ([docs/SIZE_AUDIT.md](docs/SIZE_AUDIT.md)).
- Overlays are linked statically with explicit state and reset.

More in [docs/PORT_PLAN.md](docs/PORT_PLAN.md) (phases and notes), [docs/RUNTIME_ASSETS.md](docs/RUNTIME_ASSETS.md)
(runtime asset loading) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Acknowledgements

This port would not exist without the work of others:

- **[GabeRealB/parasite-eve-2-decomp](https://github.com/GabeRealB/parasite-eve-2-decomp)** — the matching
  decompilation of *Parasite Eve II* that all game code in this repository comes from, together with its documentation
  of the game's formats and systems and its asset tooling (`tools/peassets`). This repository started as a copy of
  that project and keeps its history; the decompilation is released under CC0.
- **[PsyZ](https://github.com/Xeeynamo/psyz)** by Xeeynamo, and our
  [fork](https://github.com/khasinski/parasite-eve-psyz), which provide the PSY-Q compatible runtime. PsyZ builds on
  the [sotn-decomp](https://github.com/Xeeynamo/sotn-decomp) PSY-Q work and
  [Sozud's PSY-Q decomp](https://github.com/sozud/psy-q-decomp).
- **[parasite-eve-pc](https://github.com/khasinski/parasite-eve-pc)** — the port of the first game, whose architecture
  and conventions this project follows.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for licenses.

*Parasite Eve* is a trademark of Square Enix. This is an unofficial fan project and is not affiliated with or endorsed
by Square Enix. The screenshots show the game running from the author's own discs.

## License

Project code is released under [CC0 1.0](LICENSE), as the decompilation it is derived from. Game assets and the game's
copyrighted content are not included and remain the property of their owners. PsyZ and its dependencies carry their own
licenses (see `third_party/psyz` and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
