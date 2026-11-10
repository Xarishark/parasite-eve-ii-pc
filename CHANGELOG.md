# Changelog

## 0.3.3

- Fix the crash when leaving the inventory after equipping an M4A1. Restore the real resident rifle animation bank and model; apply the same fix to the MP5 family.
- Fix saving standalone raw and MCS files in the save editor. BP and EXP edits now update the player record restored by the game, as well as the save preview. Add headless editor persistence regressions.
- Keep native backgrounds and menus at their intended proportions in widescreen mode, improve resolution preference handling, and extend portable renderer and host regression coverage.

## 0.3.1

- Fix saving on every platform: about one save in ten failed with "Failed to access MEMORY CARD." because the game's random save name can contain ':', which the port rejected (Windows does not allow it in file names). Such names are now stored with '!' on disk. Memory-card errors are written to the log, and on Windows a save that an antivirus scan holds up is retried.

## 0.3

- Report start-up and disc errors instead of exiting silently: a window explains what is wrong with the chosen image (missing BIN, wrong game or region, ISO instead of CUE/BIN, damaged image) and the log is written to `parasite-eve-ii.log` in the preferences folder when the game is not started from a terminal (`PE2_LOG=<file>` chooses another file). CUE sheets with a byte-order mark, lower-case keywords, unquoted names or Windows paths are accepted, and a raw BIN chosen by a Windows path keeps that path.
- Fix scene reloads after the Aya double, growth-room companion reactions, missing scene buffers before asynchronous playback, scene timing before audio pacing, streamed water packets and motel balcony light shafts.
- Fix host integer widths for projected shadows, water and lighting effects, held objects, Bezier model coordinates and collision rebuilds. Fix room-light endpoint arrays that relied on separate objects being adjacent in memory.
- Fix texture fields and PGXP packet ownership in screen-coordinate writes, Controls labels that did not match their key bindings, empty numeric preferences and incomplete memory-card listings (contributed by GUMMIBRI).
- Remove the UBSan and static-analyzer findings on the default scene (signed shifts, out-of-table reads the PS1 layout tolerated, null area resources).
- HD backgrounds and widescreen are still in development and are off in release builds; the game draws the original backgrounds in 4:3. They can be built with `-DPE2_HD_BACKGROUNDS=ON`.
- Expand host regression coverage to 53 tests and the portable Windows subset to 16, which now also builds and runs on Windows in CI. Native Windows/D3D12 and physical-PC GPU behavior remain unverified.

## 0.2

- Fix room callback bindings and scripted movement that could block story progression in Dryfield and the Shelter.
- Fix a Windows Clang optimization that could read null records from the COFF asset registry.
- Preserve loaded model fixups and runtime state when mounting or swapping discs.
- Fix streaming movie ring-buffer wraparound and backpressure in PsyZ.
- Correct host GTE arithmetic, 15-bit SDK random draws, model lighting and screen effects; enlarge the native primitive arena for long caption pages.
- Expand host regression coverage to 41 tests; release builds run the full suite on macOS and both Linux architectures, plus portable regressions on Windows.
- Native gameplay and memory-card save/reload have been exercised through Dryfield and into the Shelter Laboratory on macOS. Later story progression and full gameplay on Linux/Windows remain unverified.

## 0.1

- First public preview of the native PC port of *Parasite Eve II* (USA). It reads your own North American CUE/BIN images of both discs when it starts; no game data is included.
- Boots from the disc image: opening movie, title screen and New Game, then the Akropolis up to the helicopter landing pad (including the No. 9 fight) and the Mist shooting gallery. Every stage (Dryfield, Dryfield by night, the mine, the Shelter / Neo Ark) is linked and every one of the 166 real areas loads and renders without a crash (`PE2_START_LOCATION`, `PE2_WARP`; checked by warping into each); MIST_R21 (area 21) is the game's empty debug room: it has no per-area tables, so only its own room task runs. The ending credits relocate and run on the host and shared model sources (Glutton, Eve, Flint, Kyle) are bound; no area hits a skip path any more. Only the Akropolis has been played through.
- Added a host menu (**Esc**): **Graphics** (picture detail, aspect ratio, dithering, geometry correction, fullscreen, antialiasing, model filtering, smooth lighting), **Controls** (keyboard remapping), **Audio** (volume) and **Discs** (choose the images, swap to disc 2 when the game asks). Settings are saved.
- Game data is loaded from the discs at start (models, animations, room collision, fonts and the tables converted from the original program) instead of being compiled in.
- The disc images are validated on selection: wrong region, wrong disc or a missing BIN is reported with what to fix.
- Builds with CMake and a C compiler; no extraction tools are needed. Host tests (37, plus the save editor's) run on macOS and Linux in CI.
- Added a save editor (`Parasite Eve II Save Editor.app`, GUI and CLI; `save-editor/` in the source): edits a save's character, items, weapons, Parasite Energy and progression and keeps its checksums valid. It follows the layout in the decompilation but has not been tried on a retail save yet: work on a copy.
- Known limits: audio, movies and saves are incomplete; tested on macOS (Apple silicon) only; the story after the Akropolis landing pad is unverified.
