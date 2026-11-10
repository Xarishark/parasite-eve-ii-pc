# HD and widescreen backgrounds: implementation in progress

**Widescreen work resumed, 2026-11-10.** Aspect selection and `--widescreen 0|1`
are available independently of experimental artwork packs. The GPU presenter at
2x+ can re-present the original MDEC image centred in 16:9 with black side
regions. No additional artwork is required or generated. Pack controls and
`PE2_HD_BACKGROUNDS` still require `-DPE2_HD_BACKGROUNDS=ON`. Native 1x, software
rendering and unsupported transition frames retain the original 4:3 path.
The historical validation below predates this change unless explicitly stated.

The target is optional authored HD backgrounds with horizontally extended artwork,
using the existing PsyZ sprite-atlas presenter. The original 320x240 camera,
models, native image pixels, timing and foreground masks remain authoritative.
Without extended artwork, the temporary 16:9 path fills the sides with black
and preserves the original centred background. Square pixels remains an
independent existing option.

## Current verification status

The feature is incomplete and no reviewed production artwork pack is available.
The sections below retain historical experiments; their local test counts and
scope describe those builds rather than superseding this summary.

| Area | Current evidence | Work still required |
| --- | --- | --- |
| Options and loader | Persistent aspect/pack controls, native fallback, foreground presentation and immutable local installation are implemented. | Production pack and distribution/download/update workflow. |
| Inventory | 1759 unique decoded native backgrounds; 3346 logical rows across 168 rooms, 3314 eligible for forced-start probes. | Finish excluded-row/source diagnostics and runtime checks of special screens; MIST R21 is an empty debug room without static views. |
| Broad runtime survey | Complete software survey before the two-room glow endpoint fix: 2708 exact matches, 606 without settled captures; no pixel differences or crashes. All 5416 ordered GPU audits validate. The endpoint fix additionally passes all 38 two-room variants on macOS software and Metal. A subsequent GTE shadow-output fix passes all 40 previously differing locations on macOS software and Metal. | Repeat the broad survey after both endpoint and shadow-output corrections. A supplemental menu route verifies gallery view 14; resolve the other 605 no-target locations and complete natural traversal, transient/scripted/special screens and supported GPU backends. This is forced static coverage, not an all-game pass. |
| Artwork | A private installed fixture contains four gallery views; none is approved final artwork. | Finish geometry, sharpness, side joins and foreground/matte reviews; author/review the remaining backgrounds. |
| Platforms | Scoped Metal and Linux ARM64 Vulkan/lavapipe evidence; 53 C tests on both hosts, 69 Python HD tests on macOS, and 16 Windows PE tests plus software gameplay under Wine pass. Linux x86-64 under amd64 emulation passes 53 host tests and 38 two-room software/Vulkan controls. | Native Windows/D3D12, native Linux x86-64/physical GPU behavior and complete supported-backend traversal remain unverified. |

All source captures, generated artwork and native-reference pack payloads remain
local. Passing renderer controls do not approve artwork or grant distribution
permission. The detailed reports below state each endpoint, binary and backend.

## Current implementation

The Graphics page cycles Classic 4:3 / Square pixels / Widescreen (16:9).
`--widescreen 0|1` overrides the saved `widescreen` preference; invalid, missing
or duplicate values fail before launch. The preference persists in settings.ini.
It is separate from the renderer's effective widescreen state. The coverage gate
only enables widescreen for a centred plane at least 428 pixels wide (artwork
or the temporary native-image plane with black sides);
unsupported backends and narrower spans fall back to original rendering.
Select a pack folder with Graphics > Background pack, then use Graphics >
HD backgrounds to switch it off/on. Both choices persist in settings.ini.
The folder chooser does not install, copy or download artwork. Individual
missing/invalid assets retain the original image. Picture detail must be at
least 2x for HD presentation. No production pack is bundled yet.

### Separating material and private reference planes

The runtime atlases contain two planes: material and native reference pixels.
`tools/hd/material_bundle.py` separates these without changing either plane:

```sh
venv/bin/python tools/hd/material_bundle.py split PRIVATE_RUNTIME_PACK \
  --materials MATERIAL_FOLDER --private-cache PRIVATE_CACHE
venv/bin/python tools/hd/material_bundle.py verify MATERIAL_FOLDER
venv/bin/python tools/hd/material_bundle.py hydrate MATERIAL_FOLDER \
  --private-cache PRIVATE_CACHE --output NEW_PRIVATE_RUNTIME_PACK
venv/bin/python tools/hd/install_pack.py install NEW_PRIVATE_RUNTIME_PACK \
  --destination PRIVATE_INSTALLED_FOLDER
```

Use separate, non-overlapping directories. Material folders contain layout
metadata, upper-plane PNGs and `pe2-materials.json`; they are not directly
loadable game packs. The private cache contains content-addressed reference
planes. Hydration checks their dimensions and pixel hashes, reconstructs full
atlases, and runs the existing runtime-pack structural verifier before publishing
an output directory. Existing outputs/cache entries are never overwritten.
Unlisted material-folder files/directories and symlink payloads are rejected;
failed operations do not publish partial material/runtime folders.

This separates reference planes, not ownership or provenance of every material
pixel. Source-guided candidate materials may still contain native-derived paint,
and metadata/artwork require distribution review. Neither the cache nor hydrated
pack is for publication. All current gallery candidates remain private and
unapproved. The cache is independent of material colors, so matching references
can come from local native exports without the author's runtime pack or cache:

```sh
venv/bin/python tools/hd/material_bundle.py acquire MATERIAL_FOLDER \
  --private-cache PRIVATE_CACHE --native-root LOCAL_BACKGROUND_EXPORTS \
  --foreground-sources LOCAL_FOREGROUND_AUDIT
```

Background exports are `<key>.rgb555` files from the local inventory. Foreground
inputs are source-audit directories containing `report.json` and native sprite
exports; repeat `--foreground-sources` for additional contexts. Acquisition
checks native identities, source context/parameters, source hashes and the
expected complete reference-plane pixel hash before caching. Already verified
entries are reusable without their original exports. This command consumes
existing local exports; automatic extraction from owned discs/live gameplay
remains unimplemented.

### Transferring a material archive

Use a material folder, rather than a private runtime pack or reference cache,
as the input to the archive tool:

```sh
venv/bin/python tools/hd/material_archive.py create MATERIAL_FOLDER \
  --output MATERIALS.zip
venv/bin/python tools/hd/material_archive.py unpack MATERIALS.zip \
  --sha256 TRUSTED_ARCHIVE_SHA256 --output NEW_MATERIAL_FOLDER
```

Creation prints a deterministic archive SHA-256. Obtain that checksum through a
trusted channel when receiving someone else's archive; a hash does not establish
artwork ownership. Unpacking requires the exact checksum, validates the full
material manifest/layout and publishes a new folder only after verification.
Then use `material_bundle.py acquire` with local native exports and `hydrate`
as above. The extracted folder is not directly loadable by the game.

The flat ZIP contains only material payloads and `pe2-materials.json`, with fixed
timestamps/permissions and stored PNG bytes. Duplicate/unlisted/path-bearing
entries, directories, symlinks/non-regular entries, encrypted entries and
unsupported compression are rejected. Limits are 20001 entries, 128 MiB per
entry and 64 GiB total uncompressed data, accommodating the full inventory
without unbounded extraction. Existing archives/material outputs are immutable;
failed or interrupted operations do not publish partial outputs. Input changes
prevent publication. This provides local archive transfer, not an online
catalog/downloader or automatic update policy. Current candidate materials and
layout metadata remain unapproved for public distribution.

### Local versioned installation

The offline installer copies an owner-disc pack into an immutable version folder:

```sh
venv/bin/python tools/hd/install_pack.py manifest path/to/pack --id my-pack --version v1
venv/bin/python tools/hd/install_pack.py verify path/to/pack
venv/bin/python tools/hd/install_pack.py install path/to/pack --destination path/to/installed
```

Select the printed `installed/my-pack--v1` directory in Background pack, or pass
it to `--hd-backgrounds`. A new version gets a separate directory; reverting
means selecting the older directory. Existing versions are never overwritten.
The `pe2-pack.json` manifest records payload sizes and SHA-256 hashes. Installation
copies only listed renderer payloads, verifies the staged copy, and publishes
the directory after successful checks. Private notes and source audit JSON files
are excluded. Invalid filenames, symlink payloads, changed files, missing atlases
and invalid base metadata/PNG files are rejected. Runtime foreground validation
remains authoritative; the installer does not replace it. Manifest hashes detect
corruption and changes, not publisher authenticity, artistic quality or permission
to distribute. These packs contain native reference planes and stay local.
There is no automatic download, signed release manifest or game-managed update UI.

At this checkpoint all 36 Python HD tests pass, including five installer tests
for immutable versions, rejected changed/path-escape payloads, symlinks, invalid
base metadata and interrupted-copy cleanup. The local eight-file gallery pack
was installed as `gallery-study--v10-8-v11-3`. The nine controls in
`foreground-view11-installed-route/report.json` pass using that installed folder
on the controller route from view 10 to 11 on macOS Metal, including both physical
buffers and malformed-foreground fallback. This does not approve the candidate
artwork or establish installation/runtime behavior on other operating systems.

The five installer tests also pass on Ubuntu 24.04 ARM64, Python 3.12.3 and
Pillow 10.2.0. Installation there produces the same eight payloads and manifest
byte-for-byte as macOS (`linux-installed-pack-file-parity.json`). The first
controller-route Vulkan/lavapipe collection exceeded its 40-second per-case
limit during the combined-pack classic run; it is retained as incomplete in
`linux-foreground-view11-installed-route`. The capture tool now accepts
`--timeout 1..3600` seconds (default 40), records the limit, and keeps all capture,
native-state and material gates unchanged. The separate 180-second collection
has now completed all nine controls successfully; the initial timeout is kept
as incomplete evidence rather than overwritten.

`linux-foreground-view11-installed-route-180s/report.json` verifies the installed
two-view pack on the controller route from view 10 to 11 on Linux ARM64,
Ubuntu 24.04, Mesa Vulkan/lavapipe 25.2.8. All 13 foreground sources activate;
the wide completed frames are 1707x960 on buffers 0 and 272. Native VRAM remains
exact between base and combined packs within this backend, as do the original
command inputs and observed simulation state. Both malformed-foreground controls
fall back exactly. No physical Linux GPU, Windows or complete-game claim follows.

`linux-installed-gallery-cross-backend.json` compares all nine endpoints with
the corresponding macOS Metal installed-pack run: normalized commands and
observed simulation state agree in each case. Full native VRAM differs at one
word for narrow controls and three words for wide controls; every difference is
one RGB555 channel level, with identical STP bits. Each base/combined pair has
the same cross-backend differences. This is evidence for a small pre-existing
renderer rounding difference at this endpoint, not cross-backend pixel identity
or a full artistic/occlusion approval. The test container was stopped afterward.

Installer foreground checks now also cover PE2FG1/PE2FG2 descriptor size,
background/atlas identity, filename/location binding, required base descriptor,
source ordering, page/CLUT ranges, inset constraints, contiguous cell allocation
and PNG dimensions/integrity. New tests recompute manifests around deliberately
corrupt descriptors, so SHA-256 agreement alone cannot make those inputs pass.
Both legacy and inset formats are exercised. All 38 Python HD tests pass on
macOS; all seven installer tests pass on Linux ARM64. The existing eight-file
gallery candidate passes the stricter verifier. This remains structural
validation; runtime native-texel, alpha/material and occlusion checks still apply.

The equivalent command-line options are `--hd-backgrounds /absolute/pack/path`
and `--no-hd-backgrounds`. They are mutually exclusive and reject duplicates,
empty/missing paths and newlines. Relative paths are saved as absolute paths,
so restarting from a different directory does not change the selected folder.
`PE2_HD_BACKGROUNDS=/absolute/pack/path` remains supported. Command-line options
override that environment variable, which overrides saved preferences.
Pack disable/change drops old wide coverage immediately; the next background
draw releases the previous atlas and reloads the current source, even when the
camera/key has not changed. Selecting the same folder again reloads edited
artwork. This permits switching packs without restarting.
It identifies the unmodified MDEC output by a 64-bit FNV-1a hash over all original
RGB555 words in row order, serialized little endian. It supports contiguous and
20-column image layouts. Without an available valid pack, frame submission keeps
original 0..320 coverage. Cropped transitions and active movies do not claim
full static coverage. No generated room artwork is approved yet.

This distinction matters for PE2. Unlike PE1's sprite-built backgrounds, its base
image is uploaded directly with LoadImage. Enabling PsyZ's squeeze without
re-presenting that background misaligns it with the game geometry. The initial
GPU comparison reproduced this; it must not be reintroduced by the loader.
Actual framebuffer rows are 0 and 272, not 0 and 256.

Transient completed-buffer checkpoints use `PE2_CAPTURE_FRAME_COUNT=N` with
`PE2_CAPTURE_OUTPUT=/ignored/local/directory`. Add
`PE2_CAPTURE_TARGET=stage,area,view,room:key` to require the exact room/view and
native background key as well as the frame count. Invalid target syntax disables
the checkpoint. Frame counters can restart after returning to title; a frame
number alone does not identify a gameplay moment. A nonmatching target leaves
no checkpoint report. This optional guard does not change the separate settled
survey's field-ready and 30-frame requirements. Checkpoints retain their own
`pe2-frame-checkpoint-v1` format.

`PE2_DUMP_FRAME=<pad-tick>:<file.ppm>` captures the actual GPU presenter,
including widescreen framing, without using desktop screenshots. It complements
`PE2_DUMP_VRAM`, which captures native VRAM.
`tools/hd/check_fallback.py` reproduces a no-pack off/on comparison with both
renderers from an existing replay, and optionally compares a pre-change baseline. Use `PSYZ_HEADLESS=gpu` for hidden
Metal/Vulkan/D3D output, or `PSYZ_HEADLESS=1` for the software renderer.

`tools/hd/survey.py --runner /path/to/launcher --binary /shared/game` can run
static-view comparisons in a container or on another compatible host. The
launcher receives the absolute game-binary path followed by the normal game
arguments; it must forward the PE2/PSYZ/SDL environment and make input/output
paths available in that runtime. The host still validates captured files.
The report hashes the actual binary, launcher, selected catalog and replay.
If any of those files changes during the survey, no complete report is saved.
Omit `--runner` for direct execution. These probes establish individual settled
static-view comparisons, not natural traversal or artwork approval.

## Pack and presenter

`PE2_DUMP_BACKGROUNDS=/ignored/local/directory` records exact 153600-byte
`<key>.rgb555` native backdrops and `observations.jsonl`, associating their keys
with observed stage/area/logical view/room state/warp/variant. These observations
are not a complete reachability survey and can include load/title frames.
All exports remain local game data.

Create a pack for one registered canvas:

```sh
venv/bin/python3 tools/hd/pack.py --native /local/<key>.rgb555 \
  --art /local/registered.png --left -64 --right 384 \
  --output build-hd-study/pack
./build-port/parasite-eve-ii-pc --disc /path/to/disc1.cue \
  --hd-backgrounds "$PWD/build-hd-study/pack" --widescreen 1
```

Use Picture detail 2x or above for HD atlas presentation; the current shader's
HD path is display-only above native resolution. At 1x the loader deliberately
keeps the original 4:3 image and does not claim wider coverage. The tool rejects wrong-sized
or transparent base artwork. It never crops, resizes or shifts it. For -64..384,
input must be exactly 1792x960, with the original centre at x=256..1535. The
PNG uses PE1/PsyZ's existing two-plane 68-pixel cell atlas format: 64 material
pixels at 4x plus 2-pixel replicated gutters, and original RGB reference cells
below. Base materials are presentation-only. A direct LoadImage background is
opaque even where its RGB555 word is zero; treating it as a normal texture
caused black pinholes in classic HD. The classic presenter now uses the existing
material-only plane with a 321-pixel-wide scissor at the original frame height,
which prevents squeezing on both buffers without marking unrelated CLUT rows
as drawn. The temporary renderer switch is drained/restored before room
OTs. Original native pixels remain unchanged. The decoded catalog's 1759
mode-0 MDEC sources all have STP clear; this is not a claim about arbitrary
modified workspaces. Foreground textures separately retain native transparency,
STP, blending and palette-reference fallback.

Each asset has a 48-byte `<key>.pe2bg` descriptor. Its Python layout is
`<8sQIIiiIIII`: magic `PE2BG1\0\0`, key, native width/height (320/240), left/right
bounds, static-base flag (1), scale (4), cell count, reserved zero. Bounds must
be 16-pixel aligned within -96..0 and 320..416; count must cover every 16x16
cell in the span and all 240 rows. This is a format/version marker, not proof of
artistic review. The separate local installation manifest described above covers
whole-pack file integrity; an online distribution manifest remains unimplemented.

The loader rejects extra/truncated metadata, wrong identities, invalid flags,
scale/count/bounds, mismatched/oversized PNG headers and transparent material
cells, including gutters. It checks PNG dimensions and a 64 MiB encoded-size
limit before decoding. It reuses one active source atlas on view changes, rather
than retaining every room in GPU memory. Sprite material selection is serialized
as an application GP0 command in one FIFO submission per background; a separate
DrawPrim per cell exhausted Metal submission resources and must not return.
The presenter compensates for the actual draw offset (normally 160,120), and
restores the original draw mode after submission. PsyZ is unchanged.

GPU integration gate (requires Pillow, NumPy and a GPU):

```sh
cmake --build build-port --target background_presenter_probe
venv/bin/python3 tools/hd/check_presenter.py
venv/bin/python3 tests/hd_pack_test.py
venv/bin/python3 tools/hd/check_activation.py --disc /path/to/disc1.cue \
  --disc2 /path/to/disc2.cue
```

This gate uses entirely synthetic native pixels with mixed STP/transparent
values and a centred game-style draw offset. It checks native VRAM equality,
a visible HD material change, full 16:9 corner coverage, and exact original
fallback for eight bad-pack cases. It does not prove all-game mask correctness.
It also disables, re-enables, replaces with a missing folder, and restores a
pack in one GPU process, proving exact original fallback and restored wide
pixels without any native VRAM change. The activation check exercises real
CLI parsing, saved preference reload, environment precedence and rejection of
invalid/conflicting options using an isolated temporary profile.
`host_menu_probe` renders the real settings painter into a PPM without opening
a desktop window, for visual inspection of the scrolled Graphics page.

## Local inventory and artwork inputs

After extraction, with Pillow and NumPy installed:

```sh
venv/bin/python3 tools/hd/inventory.py --export
venv/bin/python3 tests/hd_inventory_test.py
```

The default output is `build-hd-study/inventory/`, an ignored directory containing
disc-derived data. Never commit its inventory, source images or packs. The tool
accepts both decoded PNGs and the current raw BS store. It records compressed
and decoded hashes, dimensions, mapped view-file indices, special-renderer
warnings and an explicit unverified runtime status. It deduplicates exported
sources by compressed hash. Source file indices do not establish reachable
logical-view mappings.

Current local extraction: 1899 references, 1759 distinct raw/decoded backgrounds,
all 320x240, no missing sources. Stage 0 accounts for 41 references and includes
non-field screens requiring separate classification. Code inspection flags
Akropolis Square and Shelter R47 scrolling backdrops, the B1 pod tunnel image
scroll, Underpass/Garden foreground sprites, Pyramid puzzle overlay, Water Hole
water and West Elevator Hall glow. These warnings are not a runtime sweep.
The title's 8-bit texture use is its chrome effect, not proof of wider base art.

### Foreground source audit

View foregrounds are separate paletted/native sprites, linked by authored
depth; an HD base image alone does not replace them. `foreground.py` resolves
their 4/8/16-bit texture pages and CLUTs from a settled runtime VRAM capture,
retaining exact RGB555 words, zero-texel coverage and STP bits. It exports
individual RGBA source images and a 320x240 potential-coverage mask:

```sh
venv/bin/python3 tools/hd/foreground.py \
  --scene build-hd-study/scenes/mist_shooting_gallery.json \
  --capture /local/settled-capture --output build-hd-study/foreground-gallery
venv/bin/python3 tools/hd/foreground.py \
  --survey build-hd-study/survey-all/report.json \
  --output build-hd-study/foreground-survey
```

The survey mode checks capture metadata and VRAM SHA-256 against the original
runtime report. It decoded 56,867 source references across 2697 of the 2698
passing static captures. The remaining laboratory view (stage 4, area 31,
view 5) has a source-table diagnostic: batches reach 24 sprites, but its known
source array contains 23. That view remains unresolved. Diagnostics scoped to
another mapped view do not invalidate an otherwise complete source table.
Gallery view 10 has nine sprite sources and 5394 potential covered screen
pixels, including the doorway regions where the generated candidate shows seams.

These masks are authoring evidence, not accepted HD foregrounds or live
occlusion proof. They include authored hidden/skipped batches and do not
capture runtime packet changes, texture windows, depth interleaving or
draw-area ordering. All exported images/masks/VRAM remain ignored local data.
Psyz's existing HD atlas path accepts 16x16 sprites; PE2's foreground strips
also include widths of eight pixels and arbitrary heights. Replacing these
requires preserving their exact clipping, palettes, blending and OT order,
without changing PsyZ or the original native framebuffer.

`background_foreground.c` now provides an experimental packet builder for this
case: it divides an original SPRT into 16x16 atlas cells, intersects the actual
current clip with the original rectangle, and restores that clip afterwards.
The normal atlas path retains native transparency/STP and palette-reference
fallback; it also works in HD 4:3, without using the widescreen-only extension
flag. Unsupported dimensions, sprite-origin wrapping and flip modes return
no replacement, leaving the caller's original primitive available.

```sh
cmake --build build-port --target foreground_presenter_probe
venv/bin/python3 tools/hd/check_foreground_presenter.py
venv/bin/python3 tools/hd/check_foreground_presenter.py --backend software \
  --output build-hd-study/foreground-presenter-software
```

Both backends pass 25 synthetic cases with exact full native VRAM equality.
The GPU cases visibly change HD material while preserving original cutouts,
a later foreground primitive, all four blend modes, raw/modulated colours,
UV wrapping, forced/tested mask bits and restored draw-area state. Native 1x,
missing-atlas and changed-palette cases retain exact original rendered pixels.
The packet builder's caller
must provide the actual clip/offset at that primitive's place in the OT;
`GetDrawEnv` only returns the last SDK environment and cannot establish later
depth-sorted DR_AREA changes.

The read-only `gpu_state.c` walker now observes those changes in actual OT
order. It follows PsyZ's native/packed LP64 command ABI, including MargePrim's
marker, tracks E1..E6 state and texture pages inherited from textured polygons,
and skips complete primitive payloads rather than reading vertex/colour data
as commands. Unknown/truncated commands, oversized packets and cyclic lists
invalidate the audit. It never submits or modifies commands. Frame submission
still sends the original OT exactly once.

```sh
venv/bin/python3 tools/hd/survey.py --catalog /local/selected-catalog.json \
  --gpu-state --backend software --disc /path/to/disc1.cue \
  --disc2 /path/to/disc2.cue --output build-hd-study/state-survey
```

`--gpu-state` opts into `PE2_DUMP_GPU_STATE=1`. Settled survey captures include
`gpu-state.json`, with the physical buffer origin and each variable SPRT's
current clip, offset, draw mode, texture window and mask mode. The survey
requires complete matching evidence and records its SHA-256. Foreground exports
can join this to an exact static geometry/texture tuple and produce an
`observed-source-coverage.png` mask. These are issued-source coverage estimates,
not final visibility: final VRAM is not a per-draw palette snapshot, and model
occlusion/mask-test results still require renderer validation.

A selected 51-case survey of previously captured room/file combinations with
authored draw areas completed with exact off/on frame/VRAM parity and complete
state traversal. All 1772 authored foreground source references in those cases
matched issued sprites. Thirty-five cases used non-full clips at sprite draw
time, including offscreen render regions. Gallery view 10 matched all nine
foreground sources and 5394 covered source pixels. The gallery's audit-on/off
control comparisons have exact frame and VRAM equality on software and hidden
GPU backends. Evidence: `build-hd-study/state-clips/`,
`foreground-state-clips/`, `state-view10*/` and `foreground-state-view10/`.
This is a selected forced-camera sample, not a new complete-game survey.
The live integration now registers only packets emitted by the room-sprite
paths, with source-array IDs and an epoch following each actual OT clear. A
matching pack can replace those packets at their original depth, using the
walker's exact state. The whole OT is validated before any links change;
unrelated packets keep their original addresses/interpolation metadata. PsyZ
snapshots submission synchronously, then all original links are restored.
Unknown commands, invalid packet shapes, registry overflow and incompatible
source descriptors retain the original primitives. An identical unregistered
HUD texture tuple is not eligible for replacement.

### Foreground pack and current limits

Build a local candidate from exported native sources and the registered canvas:

```sh
venv/bin/python3 tools/hd/foreground_pack.py \
  --sources build-hd-study/foreground-state-view10 \
  --art /local/registered-candidate.png --left -64 \
  --output /local/background-pack
```

The 48-byte header is `<8sQQ6I`: `PE2FG2\0\0`, background key, foreground
atlas key, stage, area, logical view, room state, source count, cell count.
Each 16-byte `<IHHHHBBH` record contains source ID, texture page, CLUT,
width, height, U, V and first cell. In version 2, source-ID bit 12 marks
inset tail cells; bits 0..11 hold the ID and higher bits are rejected. Inset
sources must be at least 16 pixels in each dimension. Version 1 remains readable
with its original padded-cell layout. IDs are sorted/unique; cells are contiguous;
limits are 512 sources and 960 cells. The atlas key must differ from the base.
Metadata is named `<background>-<stage>-<area>-<view>-<room>.pe2fg`, and its
opaque material/reference atlas is `<atlas-key>.fg.png`. Native texels own
coverage/STP; reference colour mismatches fall back per pixel. Position is
taken from the live packet, so moving sources can retain their material.
The builder records provenance and marks its outputs as candidates; a crop of
the base image is not sufficient proof of individual-layer blend correctness.
Use `--materials /local/sprites` instead of `--art` to supply individual
`sprite-NNNN.png` materials at exactly 4x the source dimensions. This is the
appropriate path for independently enhanced/blended textures. Input may retain
native transparent holes, but cannot remove native-covered pixels; the shader
continues to own coverage. Nonzero live texture-window states currently keep
the original sprite until explicit window-specific material identities exist.

```sh
venv/bin/python3 tools/hd/check_foreground_presenter.py --ot \
  --output build-hd-study/foreground-ot
venv/bin/python3 tools/hd/check_foreground_live.py \
  --disc /path/to/disc1.cue --disc2 /path/to/disc2.cue \
  --base-pack /local/base-only --combined-pack /local/base-and-foreground \
  --location 1,20,10,1,1,1 --key 83f3736e454385c9
```

The registered OT probe passes 56 GPU and 56 software cases, including exact
restoration of all original packet bytes and exclusion of an unregistered
sprite with the same texture tuple. The real gallery check demonstrates nine
classic foreground replacements, visible material changes, exact full VRAM
equality against the original/base-only controls, and exact fallback for bad
metadata or a missing atlas. The source image remains an unapproved candidate.

The port now handles PE2's non-contiguous framebuffer rows during wide draws.
Psyz's classifier normally accepts only two display heights; the second buffer
ends at row 511 rather than 479. Around an HD base draw or its room OT, the port
uses the public GP1 command to set a temporary 256-line draw range, drains the
commands, then restores the actual 240-line display range before presentation.
The cached game DISPENV and requested aspect remain unchanged. This only applies
to validated 320x240 RGB15 timing with active wide artwork; movies, original
backgrounds and classic 4:3 do not use this adapter. PsyZ remains unmodified.

Completed-buffer captures in `build-hd-study/foreground-staged-gallery` show
nine gallery foreground replacements in classic and widescreen modes, on both
buffers. Narrow sources now use native staging instead of an added scissor.
Each replaced frame matches its base-only control in full native VRAM; classic
base and foreground also match the original native VRAM. The candidate artwork
still has geometry drift and is not approved. The synthetic base presenter
requires byte-identical output on both buffers in 4:3 and 16:9, verifies 240-line
output restoration, and preserves all native VRAM including STP/zero pixels.
These are specific integration checks, not full-game or artwork validation.

Partial-width sprites with fractional squeezed boundaries, nonzero texture
windows, and unsupported partial-cell layouts still
retain their original primitive. Base/model registration, player occlusion,
reflections, moving layers and other GPU backends require further validation.
The source image remains an unapproved candidate.

A wider synthetic fixture check is available:

```sh
venv/bin/python tools/hd/check_foreground_rooms.py \
  --disc /path/to/disc1.cue --disc2 /path/to/disc2.cue \
  --survey build-hd-study/state-clips/report.json
```

It derives bounded foreground atlases from the survey's verified native texture
snapshots, supplies synthetic solid-colour base/foreground materials, and runs
the same full-VRAM, visible-change, malformed-pack and both-buffer checks for
each source-bearing camera. `--location` selects repeatable six-component
locations; `--limit` bounds a run. All fixtures remain local disc-derived data.
Errors are recorded individually and make the final check fail.

The five-stage selection in `build-hd-study/foreground-five-stages-inset`
passes all five cameras: helicopter landing pad `1,16,7,1,1,1` (29 sources),
motel room `2,12,5,1,1,1` (83), night balcony `3,29,2,1,1,1` (185), sleeping
quarters `4,14,6,1,1,1` (112), and airlock `5,5,5,1,2,1` (36).
This is integration evidence for selected forced cameras, not traversal or
artwork approval. New diagnostic captures include `draw_frame`, `game_tick`
and `vsync_count`; the live checker requires equal clock evidence for each
base/foreground comparison.

The airlock exposed a native-feedback bug: inserting DR_AREA commands around a
partial sprite flushes preceding GPU work, changing later framebuffer reads.
A single 32x40 source reproduces it. Version 2 places the final opaque 16x16
cell inside the source extent, overlapping the preceding row/column, and samples
the corresponding material/reference coordinates. Repeated opaque pixels retain
the same colour and STP. Complete or inset cells emit no added scissor commands;
this preserves the original OT's batching. A synthetic framebuffer-read test
has 121 native differences when scissor commands are deliberately reinstated,
and zero with the inset path. Mask modes, raw/modulated colour and rejection of
semitransparent overlap are covered by the 56-case GPU/software probes.

The inset-only airlock checkpoint uses 36 classic and 19 wide replacements;
its seventeen narrow wide sources originally remained native. That limitation
motivated the native staging path below. Inset overlap is never used for
semitransparent sprites, sources narrower/shorter than 16, or incompatible live
flags. Old failure and repaired-inset evidence remain local in
`build-hd-study/foreground-five-stages` and `foreground-airlock-inset`.

The expanded 46-camera run is recorded in
`build-hd-study/foreground-state-rooms-inset`. Its initial strict visible-material
check passes 43 cameras and reports three failures. One has an entirely empty
native foreground (`1,16,27,1,1,1`): rewriting its packet should not change any
pixels. The live checker now accepts `--empty-sources /local/audit` only after
verifying the capture identity, complete descriptor source records, exact raw
source hashes/sizes, and that every RGB555 word is zero. Covered black `0x8000`
is rejected. An empty case must preserve both the rendered material and native
VRAM exactly; it never counts as demonstrated visible HD activation. The room
runner selects this mode only for all-zero audited sources.

The repeated empty-camera run passes all nine checks in
`build-hd-study/foreground-empty-camera-check`. The aggregate `assessment.json`
records 44 verified cameras (43 with visible material, one verified empty) and
two failures on that executable (now addressed by staging below):

- Main corridor `4,15,7,1,1,1`: classic foreground changes 6246 native VRAM
  words. Its sources include narrow/short legacy cells needing extra scissor
  commands. That run did not establish classic native preservation; it did not record
  complete model inputs. Independent synthetic clip controls also exposed
  native differences with the old extra-scissor path.
- Incinerator `4,40,4,1,1,1`: classic material passes, and wide native VRAM is
  unchanged, but none of its fourteen 8-pixel-wide sources receives an active
  HD replacement in 16:9. This is unsupported visible coverage, not observed
  native corruption. The old path failed the active-material gate.

The corridor failure is reduced to source 0 alone (16x8), in
`build-hd-study/corridor-source0-check`. A feasibility audit also rejects simply
expanding every narrow sprite into neighbouring native texels: all fourteen
incinerator sources have covered neighbours, and some expansions exceed their
texture page. Extra padding cannot be assumed transparent.

`foreground_stage.c` tracks original polygon/sprite texture pages, indexed
CLUTs, draw areas, fills, copies, readbacks and both display buffers. The runtime
uses it to select an unreferenced 64x256 direct-colour page. A completed
1024x512 native snapshot supplies exact current texels and a backup of that
page. During the original ordered walk, preceding primitive writes invalidate
the temporary known map; affected sources cannot be staged. The decoder handles
4/8/16-bit textures, UV wrap, original STP and literal zero padding. One page
holds at most 64 cells; incompatible or over-budget sources retain original
primitives, including in classic mode. No added DR_AREA is used by a live
partial-cell replacement.

Staging is restricted to the main field's sole resident OT at a drained
background boundary, with validated 320x240 RGB15 timing and internal resolution
above 1x. Task OTs do not introduce transfer/flush boundaries. The native page is
uploaded before the whole OT, and each staged cell uses a 16-bit native source
with its original HD atlas/reference selection. Every staged replacement restores
the original draw mode before following primitives. Original non-foreground
packet addresses and all links are preserved. The complete OT is drained, then
the exact page backup is uploaded/restored before readback or presentation.
No simulation, input or VSync step is added. PsyZ remains unmodified.

A CPU upload mirror, a partial-page backup readback and extra DrawSync calls
were insufficient for the corridor model: a native-only upload control still
changed its output. Uploading identical backup pixels passed; resolving the
complete native snapshot before staging also passed. The exact backend/cache
cause is not established. The runtime therefore keeps the complete snapshot
boundary, rather than assuming that an unused page alone makes transfers safe.
This synchronous GPU readback has a performance cost. A local hidden Metal
4x wall-time sample (`foreground-staged-benchmark/timings.json`), subtracting
30-frame from 300-frame runs, estimates base/combined costs of 2.64/6.17 ms
per frame in the gallery and 2.29/8.12 ms on the balcony. These single samples
include timing noise; sustained frame timing and all-platform performance
remain release gates.

`build-hd-study/foreground-staged-snapshot-live` verifies all 24 corridor and
14 incinerator replacements in classic and wide modes on both physical buffers,
with exact full native VRAM against the base-only controls, original native
VRAM in classic mode, and bad/missing-pack fallbacks. The 56-case GPU/software
probe also covers aligned 8-pixel sources, every blend mode, masks, framebuffer
reads, following unregistered sprites, and staging-disabled/1x fallbacks.
The updated 46-camera run in `build-hd-study/foreground-staged-all` is separate
from the old failed run: it completed with 45 passing cameras and one
unverified bad-metadata fallback in the corridor. That control performed no
HD replacements, but its original GP0 stream had 673 commands versus 674 and
different meaningful model coordinates/colours despite equal global clocks.
It therefore does not establish equivalent model inputs or isolate a renderer
defect. An isolated same-binary corridor repeat in
`build-hd-study/foreground-staged-corridor-repeat` passes all nine controls.
The original failed sweep is retained: a passing repeat does not prove a
stable 46/46 sweep. Deterministic model-input control remains open.
The live checker now requires matching original command streams before comparing
native output, including original/base and malformed-pack controls. It records
`input-comparisons.jsonl` and rejects unequal inputs as unverified. Normalization
ignores raw-texture RGB modulation, unused subsequent polygon colour high
bytes, unused third/fourth textured polygon UV high halves, and CLUT fields
only when a known 16-bit texture page proves palette sampling unused. Polygon
TPage updates are tracked for following sprites. Unknown, indexed and reserved
texture depths retain exact CLUT comparisons; other words stay exact. Equality supports input comparability; it does not prove equal initial
VRAM, PGXP hints or all game state. Regression fixtures reject meaningful XY,
CLUT, texture-page, UV, modulated-colour and command-count differences.
A fresh guarded corridor run (`foreground-input-guard-corridor/assessment.json`)
found matching command inputs and native output for original/classic base,
classic foreground, and wide buffer 0. Wide buffer 272 differed in both inputs
(674 versus 675 commands, first difference 430) and native output. The checker
correctly stopped as unverified before malformed-pack controls. This is fresh
evidence that a single passing repeat does not settle model-state determinism.
Current v3 captures additionally record `simulation_state`: animation frame,
game-owned vblank count, loop count/ticks and gameplay LCG/BIOS RNG states,
passed by value from the field presentation call. This adds observation only;
no clock or RNG is advanced/reset. The live checker requires complete uint32
values and equality in compared runs, alongside the existing clocks and GP0
stream. Older v3 captures lacking these fields remain historical evidence and
cannot pass this strengthened live check. In a fresh
`foreground-simulation-guard-corridor` run, classic base/combined had identical
additional state (including LCG 1373713762), but 673/674 GP0 commands with the
first difference at 429. These observations rule out differing values of those
specific counters/state as an explanation; they do not prove identical actor,
heap or animation pose state. The run remains unverified.
The ASan/UBSan build exposed a negative signed left shift in collision pushback
(`object_lists.c`, floor correction). Conversion now shifts unsigned 32-bit
values and adds floor corrections with explicit 32-bit wrap, matching MIPS
arithmetic. A regression checks negative floor and negative/positive wall-axis
corrections. With `UBSAN_OPTIONS=halt_on_error=1`, the subsequent corridor run
reports no sanitizer error through its captured cases, but still stops at
wide buffer 272 because original streams differ (674/675 commands). The
collision fix is verified separately; it is not claimed to resolve input
nondeterminism. No all-game sanitizer claim is made.

`tools/hd/check_repeatability.py` reproduces these comparisons without requiring
an HD pack. It uses new output directories, isolated profiles/cards, fixed pad
replay, virtual raster timing and completed-buffer captures. Optional base and
combined packs are interleaved with explicit `--no-hd-backgrounds` controls.
The report separates exact repetition within each variant (including RGB) from
native/input equality across variants (HD RGB intentionally differs). It records
all attempts and exits successfully when collection completes, even if
`repeatable_within_variants` is false; callers must inspect the report rather
than treating collection exit status as a passing renderer test.

```sh
venv/bin/python tools/hd/check_repeatability.py \
  --disc /local/disc1.cue --disc2 /local/disc2.cue \
  --location 4,15,7,1,1,1 --key 777972122756a6f0 --runs 6 \
  --output build-hd-study/new-native-repeat-controls
```

Six original-only development captures matched exactly. A separate nine-run
interleaved diagnostic found a 674/675-command difference in both one original
(no configured pack) and one combined capture, with unchanged observed clocks
and shared LCG state. A subsequent nine-run collection using the checked-in
tool (`native-corridor-repeatability-tool`) matched all native/input comparisons
and all repetitions within variants. This is intermittent evidence, not a
resolution or proof that a pack causes the input difference. Original failed
artifacts are retained alongside passing collections.
The longer explicit `--no-hd-backgrounds` collection
(`native-corridor-repeatability-no-hd/report.json`) completed 16 launches.
Runs 2 and 5 have 674 original commands versus 675 in the first capture,
first difference 430, and different native VRAM. Both have zero foreground
replacements and equal recorded simulation state and clocks. This directly
reproduces the same input-divergence symptom without any HD material/staging.
It establishes a native-path repeatability issue, not its root cause, and does
not independently prove every HD path correct.
The repeatability tool now records the samples actually consumed by the pad
callback in each `input.pad`, with hashes and sample counts in the report. A
12-run no-HD collection (`native-corridor-input-records`) has identical 994
samples in every run; runs 3 and 8 still diverge in original GPU inputs.
Current captures also include `player_root`: nine row-major signed rotation
coefficients followed by three translation components, or null when unavailable.
The borrowed matrix is read only for opted-in ready-field diagnostics. Another
12-run collection (`native-corridor-player-roots`) reproduces divergence in run
11 while all roots remain the identity rotation at translation [0,128,-19000].
Recorded input, observed clocks/LCG and the root transform therefore do not
explain the different model commands. Bone/animation and later transform state
still require investigation; no motion or interpolation code was changed.

Subsequent pose diagnostics found unchanged player local/composed matrices and
view matrix in 16 original-only runs, including two divergent GPU streams.
The ordered model audit then localized both divergent runs (1 and 7 of 16) to
model ordinal 5: a 21-part enemy at root [0,0,-10000]. Its local and composed
pose hashes differed while its root and all other models remained unchanged.
Matrix hashes include defined signed coefficients/translation serialized as
little-endian 32-bit values; padding, pointers and composition stamps are excluded.
`models_complete` rejects truncated (>64 models) or unsupported coordinate
records. Hashes are diagnostic evidence, not a full animation-state snapshot.

The game was still resolving `rand()`/`srand()` to host libc, independently of
its shared gameplay LCG. That gives platform-specific sequences/ranges and
shares state with unrelated host consumers. `port/random.h` is now forced only
into resident game and overlay/variant compilation, mapping those calls to
PsyZ's existing `Psyz_Rand`/`Psyz_Srand` BIOS generator. Host libraries and port
utilities keep their normal libc symbols; PsyZ is unmodified. The new
`port_random_test` checks the BIOS recurrence for multiple seeds and verifies
that unrelated host draws/reseeds do not consume game RNG state and game
draws/reseeds do not consume host RNG state. Native game
AI/animation choices can intentionally differ from the old host-libc build;
old native baselines remain historical rather than expected hashes for this
compatibility correction. No save-file format changed.

Twenty original-only runs on the mapped build
(`native-corridor-bios-random`) matched exactly. The subsequent current-build
nine-control foreground check (`foreground-bios-random-corridor`) passes
classic/wide, both physical buffers and malformed/missing-pack fallbacks with
matching original inputs and exact native VRAM. Captures now record
`random_bios` through `Psyz_RandState()` without consuming a draw; the live
checker requires equality of both RNG states. These local results support the
isolation correction; they do not yet prove all-room repeatability, identify
an unrelated libc consumer, or establish all-platform/traversal correctness.

The 46-camera BIOS-RNG sweep completed with 43 passing cameras and three
unverified airlock views. All three had identical native VRAM and differing
unused CLUT fields on direct-colour sprites under known E1 page 0x700. This
was an over-strict input comparator, not an observed material/native mismatch.
After the depth-aware comparison correction, all three isolated views pass all
nine controls (`foreground-bios-random-airlock`). The original failed report
is retained. `foreground-bios-random-all/assessment.json` joins same-binary
results: 45 cameras demonstrate visible foreground material and one has
verified empty sources, covering all 46 selected cameras. This is combined
per-camera evidence, not a newly green single sweep. Direct-colour CLUT
regressions preserve rejection of indexed, reserved and unknown-page changes.

### Linux ARM64 checkpoint

The current code builds with GCC 13.3.0 in a local Ubuntu 24.04 ARM64 container.
The offline build reused the already fetched libwebp source via
`FETCHCONTENT_SOURCE_DIR_PSYZ_WEBP`; no PsyZ source change was needed.
All 49 C tests pass with headless software rendering. The first invocation
without a video device failed two GPU tests at SDL initialization; CTest now
sets headless/dummy-audio explicitly for `gpu_tpage_test`, `caption_packet_test`
and `gpu_state_test`, consistent with CI's existing environment.

Both 56-case foreground probes pass on Linux: Vulkan through Mesa lavapipe
(`linux-bios-foreground-gpu`) and the software backend
(`linux-bios-foreground-software`). The real corridor's full nine-control check
also passes on Linux Vulkan (`linux-bios-foreground-corridor-mounted`), including
active 24-source replacements in classic/wide on both physical buffers and
exact within-platform native VRAM. This validates a software Vulkan driver in
a local VM, not physical Linux GPU hardware or interactive traversal.

The live/presenter/room checkers accept `--runner /local/launcher` to execute a
native binary in another environment. The launcher receives the actual binary
path and arguments, must forward the supplied test environment and expose
identical paths, and must return the native process status. Successful live
and probe reports hash the actual native binary plus launcher; neither may
change during a check. The local container wrapper forwards only PE2/PSYZ/SDL
settings, selects offscreen Vulkan/lavapipe and keeps disc mounts read-only.
The temporary validation container was stopped and removed after capture.

Cross-platform corridor comparison finds matching normalized commands,
observed clocks/both RNGs and model matrix hashes in all nine cases. Native
VRAM is not byte-identical between Metal and Vulkan: 8–9 displayed words differ
by at most one RGB555 level per channel, with no STP or off-display differences
(`linux-bios-cross-platform-corridor.json`). Within each backend, the HD/original
comparisons remain exact. No general cross-backend pixel-equivalence claim is
made; Windows/other architectures and hardware backends remain unverified here.
Neither a partial run nor these synthetic fixtures establish all-game,
traversal, artwork or all-platform completion.

With `PE2_DUMP_GPU_STATE=1`, complete ordered audits include
`staging_free_pages`: a hexadecimal 32-bit geometry-availability mask, with bit
`16*(y/256)+x/64` representing a 64x256 page. A complete audit is required; the
mask alone does not prove a CPU backup is known. `gpu-commands.bin` records the
original pre-replacement GP0 stream as little-endian word-count/word-array
records, capped at 262144 words including counts. Unused UV padding and raw
texture colour fields can vary without changing rendering; raw byte inequality
alone does not prove different effective model inputs. All captures contain
disc-derived data and stay local/ignored.

New settled captures use `pe2-static-view-survey-v3`. PE2 normally displays the
opposite buffer while drawing; immediately before this diagnostic exits, it
presents the completed draw buffer explicitly. Its reported `buffer_y`, native
VRAM, GPU-state audit and image now refer to the same frame. v2 captures remain
useful native-source inputs and literal off/on comparisons, but cannot prove
current-buffer visuals. Visual coverage must be repeated with v3. The optional
`PE2_SURVEY_SETTLE_FRAMES=30..36000` permits longer controller routes and
odd/even buffer parity checks (default 30). This
diagnostic presentation change is never used in ordinary gameplay.

The native catalog now decodes all 1759 distinct BS inputs with the game's own
VLC implementation and PsyZ MDEC, rather than treating an approximate Python
decode as a runtime identity. All 1899 source references resolve, with zero
decoder errors. A runtime gallery capture matches the offline native bytes.
The publisher logo is a separate INIT.BS source outside the room inventory.

```sh
cmake --build build-port --target hd_native_decode
venv/bin/python3 tools/hd/native_catalog.py
venv/bin/python3 tools/peassets/room_scene.py --all --out build-hd-study/scenes
venv/bin/python3 tools/hd/view_catalog.py
venv/bin/python3 tools/hd/survey.py --disc /path/to/disc1.cue \
  --disc2 /path/to/disc2.cue --output build-hd-study/survey-all
```

The resulting local catalog covers 168 rooms and 3346 logical
view rows; 3314 are eligible static-camera candidates. Nineteen source-table
diagnostics and the room without view rows remain explicit unresolved entries.
Logical view IDs and mapped background file indices are distinct.

`survey.py` uses an isolated profile and replay for each launch. The opt-in
`PE2_SURVEY_KEEP_VIEW=1` diagnostic retains the requested camera after arrival
warp initialization; ordinary startup otherwise selects the warp's authored
camera. This is a forced static-camera test, not proof of physical traversal.
It accepts only 30 consecutive visible field frames with the expected native
key and exact six-component location, rejects black/loading captures, and
compares the complete native VRAM and rendered frame with widescreen off/on.
Use `--replay /local/route.pad` to supply deliberate controller inputs instead
of generic startup confirmation pulses. The tool preserves a local trace copy
and hashes both the copy and the supplied source; changing either prevents a
complete report. Each selected location still uses its authored forced start,
so a supplied trace does not imply an end-to-end story route.

The executable hash must remain unchanged throughout a survey. The initial
20-camera run passed. The full 3314-candidate run completed on executable
SHA-256 `2367b99d2b42d3c64b68c9589310c4301f7e993633f70c4c4efcdbee05e80d5f`:
2698 exact off/on matches, 616 unverified. Of the latter, 563 ended the replay
without the required capture, 46 exited with signal 11, two with signal 10,
and five differed in rendered/native pixels on the night motel balcony.
Passing runs also emitted 130 unsupported-renderer-command warnings; pixel
parity does not establish that these commands render correctly.
The full local report is `build-hd-study/survey-all/report.json`.

The Mist Center R18 crash was reproduced under LLDB: the header wait timed out
while `_gMdecSceneImageHeader` was null, then dereferenced its buffer kind.
The timeout now releases bookkeeping without reading or committing capacity
from an unselected/stale header. A production-code test covers both cold and
stale caches, and a valid header arriving at the timeout boundary. A targeted
runtime retry no longer crashes, but still does not produce the requested
static camera; room scripts change cameras and stream their own backgrounds.
This is not successful runtime coverage. Three identical widescreen-off
balcony retries also produced distinct VRAM/frame hashes, so the five original
differences require deterministic-state investigation before attribution to
the aspect preference. They remain unverified.

The first imagegen candidate is local under
`build-hd-study/art/mist-shooting-gallery-view10/`: source metadata, source PNG,
generated-v1.png and provenance.json with the exact prompt. Requested canvas:
1792x960, original view at 4x in the central 1280 pixels, 256 new pixels on each
side. Actual output: 1659x948. **Unapproved:** its registration/geometry has not
been verified and it must not be used as accepted artwork or committed.
The built-in imagegen tool was used; no API-key fallback was used.

A second generated candidate also returned 1659x948. Its resized 1792x960
canvas was exercised in the real GPU presenter at gallery view 10, with exact
native VRAM preservation. Visible foreground seams and geometry differences
prevent approval. `normalize_art.py` is an experimental source-guided texture
normalizer: protected source edges and boundary bands remain exact, while
interior detail changes are bounded. Its audits prove those numerical
constraints only. The resulting candidates and rejected optical-flow
registration experiments do not establish production artwork quality.

The v6 generated candidate (1672x941, central 75% assumed to represent the
original view) has a global median nearest-edge distance of about 0.96 source
pixels, but local gradient matching in all nine foreground source rectangles
prefers offsets of 3–5 source pixels. A whole-image affine fit also requires
scale/shear changes. It is rejected as registered artwork and retained only as
a texture reference. The subsequent v7 original-frame 4:3 restoration
(1448x1086) still prefers local offsets of 2–5 source pixels and is likewise
unapproved. Corresponding local JSON assessments are beside the candidates in
`build-hd-study/art/mist-shooting-gallery-view10`. These are read-only analyses;
no warping, production pack update or artwork commit was made. A global
nearest-edge statistic alone must not approve geometry: invented texture edges
can be close while the actual foreground contours remain displaced.

The next v8 edit uses the full-resolution v4 source-geometry canvas as the edit
target, restoring surface detail and the two obvious vertical composite joins.
Its output is 1713x918 with the original view occupying the middle 5/7 width.
All nine foreground rectangles prefer zero integer gradient displacement
(correlations approximately 0.82–0.91), unlike v6/v7. This is a registration
indicator, not proof of exact subpixel contours. The local candidate, assessment
and provenance are beside earlier rejected variants.

Both pack builders now accept explicit `--fit-canvas`: material-only size
normalization with Lanczos, retaining the full canvas/framing. Aspect changes
beyond integer-size rounding are rejected, opacity is checked before fitting,
and the default still rejects all wrong-size art. Base and foreground builders
use the same preparation function; native reference planes are never fitted.
The source artwork file remains unchanged. Tests cover default rejection,
explicit matching-canvas fitting, wrong aspect/transparency rejection, unchanged
input bytes and exact native background/foreground reference values.

Local `view10-v8-base-pack` and `view10-v8-combined-pack` fit the candidate to
1792x960 at bounds -64..384. `foreground-v8-gallery/report.json` passes all nine
real-game controls: classic and wide, both buffers, nine active foreground
sources, exact full native VRAM and invalid/missing-pack fallback. Completed
classic/wide frames were inspected: earlier vertical joins are not visible.
The player is outside these forced-camera frames, so player occlusion,
traversal and final artwork approval remain open. No art or pack was committed.

#### Controller-driven v8 gallery checks

`check_foreground_live.py` now accepts `--replay` and `--settle`; it copies and
hashes the route, checks the requested matching-frame count was honored, and
requires both physical buffers in the wide pair. Larger default traces grow
with the settle interval. The native diagnostic accepts up to 36000 matching
frames; regular gameplay and the default 30-frame check remain unchanged.
The read-only LLDB checkpoint also records composed camera rotation/translation,
projection distance and the player's cached view-space origin, without calling
any target function.

Real movement from warp 6 reaches a visible player pose at [4719,131,6302].
A slightly longer second leg reaches [6022,131,5701], beside the left foreground
post. `foreground-v8-gallery-walk` and `foreground-v8-gallery-occlusion` each
pass all nine controls at 1300/1301 matching frames, including exact native VRAM,
matching original GP0 input and RNG/clocks, nine active foreground sources,
both physical buffers and malformed/missing-pack fallback. Original and HD
completed-buffer images show the character in the frame; no coordinate or
actor-state injection was used.

For the latter pose, exact 4x colour differencing against the earlier empty
original frame identifies 4461 visible model/shadow contribution pixels.
Foreground-HD versus base-HD changes zero RGB channels on those pixels.
The 578 original-to-base differences include the translucent foot shadow over
changed floor artwork. An earlier native-cell-upsampled estimate included 11
subpixels of original occluder inside actor cells; it is retained as an
insufficient mask, superseded by the exact-resolution colour assessment.
This is evidence for preserved visible contributions at this pose, not a
complete per-primitive matte, equal-colour coverage, hidden geometry, all poses
or final artwork approval.

The same native controller path proceeds into views 11 and 12. The independent
`gallery-v8-transitions-settled/report.json` completes three controls per view:
original 4:3, native wide preference, and wide preference with only the v8
view-10 pack available. The settled view-10 preview is 1707x960 with the pack;
the completed view-11/view-12 frame returns to 1280x960, with zero foreground
replacements and exact native VRAM plus rendered RGB against the no-pack
control. Normalized original commands and observed simulation states also match.
This proves two concrete in-room transitions and missing-art fallback on one
macOS Metal route, not every room transition. The initial tick-1000 preview
had a transient source key and was rejected as activation evidence; its failed
collection is retained separately. No art or pack was committed or published.



#### Read-only artwork registration diagnostics

`check_art_registration.py` compares native and fitted material gradient maps
in each authored foreground rectangle, including a four-pixel context margin:

```sh
venv/bin/python tools/hd/check_art_registration.py \
  --native path/to/background.rgb555 --art path/to/candidate.png \
  --sources path/to/foreground-audit --output path/to/registration.json
```

The source audit must identify the same native background. Without `--sources`,
the tool compares the view interior. Canvas fitting follows the packer's bounds,
aspect and opacity checks. Search samples include generated side margins so a
source near x=320 retains its visible pixels rather than losing them to an
inward-clamped search window. Reports identify clipped searches, flat regions,
ambiguous peaks and boundary peaks. They always contain `approved: false`:
integer shifts after downsampling cannot prove subpixel contours, occlusion or
artwork quality. Three synthetic tests check known shifts, side-margin searches,
input immutability and invalid or uninformative evidence. All 31 Python HD tests
pass at this checkpoint.

The existing view-10 v8 candidate reports zero displacement in all nine source
regions with this tool. On the controller-reached view 11, key
`1503fa132d262605`, four local generated candidates have been inspected. V3 is
the best current geometry candidate: 12 of 13 regions report zero displacement;
the right central-wall edge reports one native pixel left. Floor joins still
need review. V4 worsens central-wall and right-wall alignment, including
ambiguous and search-boundary peaks, and is rejected. These remain local artwork
experiments; no view-11 replacement is approved. Its source
audit uses an explicit route capture at warp 6; the minimal derived catalog
record remains `runtime_verified: false`, rather than weakening location checks
or claiming a complete catalog traversal.

`check_foreground_live.py --start-location` separates the initial location from
the `--location` capture target, requires an explicit controller replay, and
records both locations in the final report. The target's full location and key
are still checked against the completed runtime capture. With the existing
6000-sample view-11 route, start `1,20,10,1,6,1` and target
`1,20,11,1,6,1`, local `foreground-view11-v3-route/report.json` passes all nine
controls on macOS Metal. All 13 foreground sources activate in classic and wide
modes; wide captures cover physical buffers 0 and 272 at 1707x960. Native VRAM,
normalized original commands and observed simulation states match the respective
controls; malformed metadata and missing atlas fall back exactly. The player
reaches world position [8052,128,5688] through the controller route. The completed
image was inspected and still shows floor joins requiring artwork correction.
This validates this candidate's renderer integration at one reached endpoint,
not final geometry, complete actor mattes, intermediate transition frames or
other platforms. Only the view-11 candidate pack was supplied for this check;
it does not establish a continuous HD-to-HD transition from view 10.

A subsequent local two-view pack combines view-10 v8 and view-11 v3. The six
controller runs in `gallery-two-view-transitions-reviewed/report.json` complete
both routes, each with no-pack classic, no-pack wide and combined-pack wide
controls. The settled view-10 preview and view-11 endpoint are both 1707x960;
13 foreground sources activate at view 11. Continuing to unpacked view 12 gives
1707x960 at the preview and 1280x960 at the endpoint, with no replacements and
exact native VRAM and rendered RGB against the no-pack control. Original command
streams and observed simulation state match at both endpoints. For active wide
view 11, full native VRAM equals the earlier single-view-11 wide control exactly.
An initial failed comparison with the narrow control is retained separately:
its 34962 changed native words reproduce the existing wide presentation, rather
than showing a new two-pack transition divergence. The assessment records the
control and pack/replay/script hashes. These are settled observations on one
macOS Metal route, not a capture of every intermediate transition frame.

View-11 artwork v5, made with built-in imagegen to repair only outer floor joins,
improves those joins visually but drifts in central sources 3–6 and right source
8. Its image, prompt, provenance and registration report remain local under
`build-hd-study/art/mist-shooting-gallery-view11/`; it is rejected as a replacement
for v3. No generated image or native-derived pack is committed or published.

#### Controller-reached view-12 artwork study

View 12 has native key `9800711c12f1bc39`. Its existing controller route from
view 10 reaches world position [11611,132,5781]. A source export using that
completed native capture finds 14 authored sources and 17199 potentially covered
screen pixels. Eight narrow sources require at most 53 staging cells, below the
64-cell staging-page budget. The explicit warp-6 catalog record preserves the
full captured location and remains `runtime_verified: false`; this is no
complete catalog traversal claim.

Two built-in imagegen candidates and their prompts/provenance remain local in
`build-hd-study/art/mist-shooting-gallery-view12/`. The native geometry guide
calibrates all 14 diagnostic regions at zero integer shift. V1 has 11 zero-shift
regions; sources 8/11 have ambiguous left shifts and source 12 shifts [4,8]. A
source-guided correction and second generation improve v2 to 12 zero-shift
regions, but source 8 shifts [0,-1] and source 12 [4,7] with an ambiguous peak.
Source 11's zero peak is also ambiguous. Neither candidate has final artwork
approval. Downsampled gradients do not establish subpixel contours or occlusion.

The local v2 base/foreground packs were tested as technical candidates only.
`foreground-view12-v2-route/report.json` passes all nine controls on macOS Metal
after the controller replay reaches view 12. All 14 foreground sources activate
in classic and wide modes; the wide frames are 1707x960 on buffers 0 and 272.
Base/combined native VRAM, normalized original commands and observed simulation
state match; classic base matches original native VRAM, and malformed metadata
or missing atlas falls back exactly. The completed image was visually inspected.
This verifies renderer integration at this endpoint, not final geometry, full
actor mattes, intermediate frames or another platform. No art or native-derived
pack is committed or published.

#### Retrying an old unverified survey crash

The old full survey recorded exit 139 at Dryfield garage target
`2,24,7,1,1,1`. A current no-pack software retry times out at 40 seconds;
a separate retry with `PSYZ_AUDIO_CLOCK=video` instead completes its replay
without reaching the settled target. Neither reproduces the old SIGSEGV.
Read-only LLDB inspection observes location `2,24,14,1,1,1`, demonstrating that
the requested view is not the current camera during that replay. Stack samples
are in scene pacing and kernel polling. A transient deadline of 518720 with
accumulated lines 12823 initially suggests an unreachable masked-counter wait,
but eventual process completion contradicts a permanent-loop conclusion;
stream servicing can change timing data while polling. No game/PsyZ change is
justified from this evidence. `garage-crash-retry-assessment.json` retains logs,
report hashes and this limitation. The target remains unverified; this neither
clears the old crash nor proves that natural gameplay reaches view 7 correctly.

#### Captured-backdrop rooms need separate review

A current no-pack Metal retry of historical MIST r18 target `1,18,1,1,1,1`
finishes its 8001-sample replay without a settled target capture, rather than
reproducing the old exit 139. The failed collection is retained in
`mist-r18-retry`; the target and the other old room failures remain unverified.

Source inspection identifies an inventory omission: MIST r18 and Shelter B1
sterilization room share captured-backdrop crossfade code. In MIST r18,
`_mistR18CaptureBackdropState` copies the current 320x240 physical draw buffer
into two texture regions; the shared live-frame renderer samples that framebuffer
again during blending. A static image match cannot validate these transitions.
Both rooms are now explicitly classified as `captured-frame backdrop crossfade`
by the inventory tool. A synthetic inventory test preserves that review label
and the unverified state; all 39 Python HD tests pass.

The local `native-catalog/index-captured-review.json` updates only review metadata
for 32 source references, retains the prior catalog hash, and leaves decoded
bytes and verification states unchanged. Its separate joined catalog is
`view-catalog-captured-review.json`. Historical catalogs and survey results are
retained. No runtime rendering fix or captured-screen HD support is claimed.

#### Script-reached MIST briefing endpoint

Read-only EOF checkpointing of the MIST r18 replay identifies view 4 with the
briefing controller and UI open, rather than requested initial view 1. A new
collection starts at `1,18,1,1,1,1` and waits for actual target
`1,18,4,1,1,1`, key `d3659a3b298379b5`. Both no-pack classic and wide-preference
runs produce valid completed v3 captures after 30 matching field frames, with
complete ordered GPU audits. Full native VRAM, 320x240 rendered RGB, normalized
original commands and observed simulation states match exactly on macOS Metal
at 1x. Evidence and binary/replay identities are retained in
`mist-r18-reached-view4/parity-assessment.json`.

This is one script-reached endpoint, not the original forced-view-1 test or a
full briefing traversal. The settled endpoint has no GP0 VRAM-copy commands and
its observed sprites use indexed pages, so it does not exercise the earlier
captured-backdrop transition. Code inspection alone does not establish whether
that transition correctly preserves or drops HD side coverage. Intermediate
crossfade frames and installed-art behavior remain required work; no fallback
or renderer change was made on speculation. The old survey's crash record is
retained and remains separate from this successful endpoint observation.

#### Located MIST captured-backdrop event

Read-only LLDB breakpoints on the same input replay locate
`_mistR18CaptureBackdropState` at `frameCount=1290`, `animFrame=786`, in view 2.
The original OT submitted at count 1290 contains no copy yet. The next observed
submission is count 1292: its native packets contain GP0(80h) copies from
(192,272), size 128x240, to (640,256), and from (0,272), size 192x240, to (832,0).
Together they capture the entire lower 320x240 framebuffer. The queued copies
match the source-level captured-backdrop layout. An exact count-1291 breakpoint
was never reached; its EOF/unavailable-argument attempt is retained as invalid
evidence rather than mistaken for a successful export.

`mist-crossfade-event-assessment.json` retains callback/OT observations and
binary, replay, script and packet-export hashes. The debugger only reads globals
and packet memory, without target function calls or game-state injection.
This identifies a reproducible concrete transition event for further work; it
does not prove rendered crossfade pixels, both-buffer parity, or correct HD
coverage. No renderer behavior or settled-capture validation gate was changed.

#### Completed transient-frame checkpoints

`PE2_CAPTURE_FRAME_COUNT=N PE2_CAPTURE_OUTPUT=/absolute/output` opts into a
single completed-frame checkpoint at the game's `frameCount`. Use it without
`PE2_SURVEY_TARGET`; survey mode takes precedence. A known native background and
simulation record are required, but the frame may be a loading/script transition
without the ready-field state required by settled surveys. The path shares the
completed-buffer capture implementation and optional `PE2_DUMP_GPU_STATE=1`.
It emits `pe2-frame-checkpoint-v1` with zero matched frames. Settled-view validators
reject that format even if its matched count is edited to 30; their existing
ready-field and minimum-30-frame gates are unchanged. A requested count that is
not presented with known background information produces no checkpoint.

`mist-crossfade-completed-frame/assessment.json` verifies no-pack classic/wide
preference parity at count 1292, view 2, buffer 272 on macOS Metal at 4x. Both
completed images are 1280x960 and match exactly, as do full native VRAM, original
commands and observed simulation state. The completed GP0 audit contains both
expected framebuffer copies, and the captured image was visually inspected.
This is transient native fallback evidence, not HD-art crossfade support or a
settled-view approval. All 49 C tests and 39 Python HD tests pass; PsyZ is unchanged.

#### Captured-frame crossfade fallback

An eight-background synthetic material pack for MIST r18 reproduced an actual
presentation defect: at transient checkpoints 1304 and 1310 the copied native
centre overlaid the current HD pattern, while its unblended sides stayed wide.
The native and material runs have equal original commands and observed simulation
states, isolating this from different gameplay inputs. The pre-fix collection
remains in `mist-pattern-crossfade`.

Captured-backdrop producers now request a native frame for their logical draw
buffer. Presentation consumes the request once for physical y=0 or 272, disables
HD base/foreground material for that frame, and reports native 4:3 coverage.
MIST r18 and Shelter B1 sterilization-room capture callbacks request this before
their copy packets; the shared live-frame crossfade drawer requests it for each
blend frame. Ordinary frames resume their normal pack selection afterward.
This preserves captured images without claiming an extended version of them.
Psyz and original game packet contents remain unchanged.

`mist-pattern-crossfade-guarded/assessment.json` verifies macOS Metal at 4x:
checkpoints 1292 (capture), 1304 and 1310 (blend) produce 1280x960 RGB exactly
equal to no-pack controls; checkpoint 1340 resumes 1707x960 HD presentation.
The blend checks cover both physical buffers. Each current native framebuffer
also matches exactly. At 1292 and 1304, 2951 words in the *other*, older framebuffer
still differ from earlier wide rendering; by 1310 full VRAM matches. This is not
a full-history native VRAM parity claim. Unit coverage verifies per-buffer
requests, duplicate requests, invalid buffers and one-shot consumption; all 49
C tests pass. Sterilization-room runtime traversal, other backends and actual
extended captured-frame artwork remain unverified.

All four no-pack checkpoints also match pre-fix rendered RGB, full VRAM,
normalized commands and observed simulation state exactly. The installed two-view
gallery route's nine controls pass again on the new binary
(`foreground-view11-after-crossfade-guard`), including active foreground material
in both buffers and malformed-pack fallback. This limits regression evidence to
these concrete routes rather than all-room or all-platform correctness.

#### Crossfade validation still open on Linux and the sterilization route

The updated game and per-buffer coverage test build on Linux ARM64; all 49 C
tests pass there. A Vulkan/lavapipe 4x replay targeting transient frame 1292
times out after 180 seconds in the first **no-pack control**, before producing a
checkpoint. `linux-mist-pattern-crossfade-guarded` retains the failed collection;
it provides no crossfade rendering approval. The remaining container process was
terminated and the test container stopped. The cause is not established: next
diagnosis must distinguish scene pacing from a skipped exact counter, rather
than crediting the macOS result to Linux or attributing the timeout to HD.

Source inspection of the sterilization-room action handler shows that trap
entry requires both a dormant trap and `GAME_FLAG_B2_NORTH_WALKWAY_SCENE_SEEN`.
An ordinary fresh forced start does not supply that story prerequisite. A real
prior route or an appropriate unmodified save is needed to exercise this second
carrier; setting the flag through the debugger would not validate that route.
Its crossfade remains runtime-unverified despite sharing the tested guard code.

#### Linux scene-pacing initialization fix

Read-only GDB inspection reproduces the Linux timeout at frameCount 948, view 2,
inside `_gameMainPaceToSceneTiming`. The accumulated timing is zero and the first
deadline is 2377060496: the newly allocated timing reservation contains host heap
pointer remnants, while the image decoder has not supplied its timing payload.
Audio startup enables pacing before that payload arrives, so waiting on this
uninitialized word prevents progress. Logs remain in `linux-mist-runtime-debug`.

`cdCmdReservePlaybackBuffers` now initializes the first timing word to the
existing zero/end sentinel when a valid reservation is established. This covers
allocated and borrowed actor storage, including offsets beyond a shared VLC
prefix. The decoder still replaces that word with the real payload normally;
the fix changes neither host allocator policy nor PsyZ. The new
`scene_timing_buffer_test` calls the actual reservation function with poisoned
memory for all four storage kinds, checks the untouched following word/VLC
prefix, and verifies replacement with a synthetic timing payload. All 50 C tests
pass on macOS and Linux ARM64.

The same eight-run Vulkan/lavapipe collection now completes, rather than timing
out (`linux-mist-pattern-crossfade-timing-init`). Capture/blend checkpoints 1292,
1304 and 1310 produce native 1280x960 RGB exactly equal to no-pack controls;
1340 resumes 1707x960 HD material. Original commands and observed simulation
state agree within each pair. The matching macOS collection also passes, and
all four macOS no-pack checkpoints retain exact pre-fix native VRAM, command
inputs and observed simulation state. Earlier timeout evidence is retained.
This resolves the reproduced initialization stall and verifies one crossfade
route on two backends; it does not establish all scene timing, physical Linux
GPUs, Windows, or the sterilization-room story route.

#### Retest of all historical crash cases

The current binary retests all 48 old exit-139/138 locations with the software
backend, video audio-clock, virtual raster clock and complete GPU-state audits.
`historical-crash-retry-timing-init/report.json` records five exact off/on parity
passes: MIST r18 views 3, 4, 5, 6 and 10. The other 43 remain unverified: 35 exit
139, five exit 138 and three finish without a settled target. Neither the old
full survey nor this failed subset is rewritten as complete coverage.

Read-only LLDB reproduction of requested Dryfield r08 `2,8,1,2,1,1` faults in
`viewGetMappedIndex`, but the current location is already `3,9,6,2,3,1`.
Both maps for the initial r08 room state correctly point to the identity map.
The later transition and its room-state/table semantics need investigation;
an arbitrary NULL fallback in the initial room would conceal that distinction.
Logs and report identities are retained in the subset assessment. No fix for
this later crash is claimed.

View-12 artwork v3 is retained locally with its built-in imagegen prompt and
provenance. It has the wrong canvas aspect beyond permitted rounding and is
rejected by `prepare_canvas`, before registration or packing. No stretch/crop
exception was introduced and no artwork is approved or published.

#### Dryfield scene return selects the destination room state

The Aya-double scene's actor-121300 reload changes saved stage/area/warp to
Dryfield Night's dilapidated house but previously retained the source room state.
R08 has two lighting states; the destination has only one view-map/count row.
Returning from state 2 therefore indexes past that table and crashes in
`viewGetMappedIndex`. The scene reload now explicitly selects destination room
state 1, alongside its existing stage, area and warp assignments. No generic
NULL-map fallback or camera substitution is introduced.

Owned-disc input replays with read-only EOF checks pass from all six R08 views
in source state 2, plus a state-1 control. Each reaches stage 3, area 9, room
state 1, warp 3 without the reproduced crash. Evidence is retained in
`dryfield-r08-return-regression` and the separate view-1 checkpoint log.
All 50 C tests pass on macOS after this change. The six-case forced-view survey
now ends normally without settled *source* targets, rather than crashing
(`dryfield-r08-return-fixed-survey`). These six targets remain unverified;
successful later-room replay does not make their original static parity pass.
Other platforms and natural-story traversal remain separate validation work.

#### Streamed water packet layout and lifetime

The Shelter B2 pod-bottom replay reproduces GPU packet corruption in view 12:
its supposed length is `0x2D000009`. The custom actor-403600 water-grid quad had
only a pointer-width link, omitting PsyZ's separate length field. Writing its
command byte therefore corrupted the length. Its declaration now uses `O_TAG`,
with actual host size/command-offset assertions, and shares a private packet
header with a regression test that writes/link-checks the real type and verifies
all four corner offsets against `POLY_FT4`. PsyZ is unchanged.

Actor-361100 streamed water also placed host-sized packets into legacy streaming
tail halves sized for 40-byte PS1 packets. Host builds now allocate two aligned
packet arrays in the water task's own work block, with the existing ripple state
at offset zero. Refraction has 320 strip slots plus a leading slot, avoiding a
pointer before the array; ripple output uses the same per-buffer storage. The
task owns its allocation and normal teardown frees it. The native stream-tail
eligibility gate remains unchanged and the effect is retained.

All 51 C tests pass on macOS. Fourteen of the 15 old pod-bottom crash probes
now reach replay EOF without a crash, while remaining unverified because they
do not capture their settled target. View 11 still faults in a CD-sector copy
to an invalid destination (`pod-bottom-view11-after-packet-debug.log`), a separate
unresolved path. An ASan/UBSan build reaches EOF for the view-1 replay with no
AddressSanitizer error when UBSan recovery is enabled; it records 85 UBSan
diagnostics, including world-coordinate overflow, CD bit shifts and animation
scratch alignment. The first halt-on-UB attempt and full logs are retained;
this is explicitly not a clean sanitizer pass.

The complete 48-case historical subset was rerun on the combined current build
(`historical-crash-retry-water-fix`). Five cases still pass exact parity; all
other outcomes remain unverified and separately classified in its assessment.
No all-room completion, final water artwork, natural-story traversal, or
cross-platform approval follows from these macOS software probes.

#### Reserve missing scene storage before starting CD I/O

The remaining pod-bottom view-11 crash writes a CD sector at address 0x7c4.
Read-only LLDB inspection finds a NULL decode buffer despite a selected 32768-byte
reservation. A breakpoint trace shows the last heap/image reservation at frame
877 and scene selection afterward at 881, with no subsequent reservation before
playback. This path cannot rely on another model/image load to prepare its buffers.

Scene-audio startup now checks storage before asynchronous I/O. A completely
absent scene reservation is prepared using the same allocation/actor-buffer
selection code as normal heap restoration. Existing reservations are retained;
partial or failed allocations report a scene error instead of starting unsafe
transfers. The lazy path does not allocate/reset movie workspaces. Failed VLC
allocation no longer calls the table builder with NULL. The existing timing
buffer test now checks late timing/decode reservation, distinct allocations,
idempotency, preserved timing data, no movie lookup, partial storage and allocation
failure. All 51 C tests pass on macOS.

All 15 pod-bottom probes now reach normal replay EOF without the prior crashes,
but remain unverified because no settled target is captured. The gallery's nine
foreground checks pass on the final binary
(`foreground-view11-scene-reservation-checked`). In the complete 48-case old-crash
subset, five still pass parity, 37 reach EOF without a target and six still crash
(`historical-crash-retry-scene-reservation`). The preceding build had 20 crashes.
Earlier reports and debugger traces remain local and unchanged; these results
do not prove natural gameplay traversal or other-platform correctness.

#### Wait for the growth-room companion model before distress reactions

The six remaining historical crash probes all entered Shelter B6 Growth Room.
A headless LLDB reproduction stopped in the distress controller while reading
coordinates through an absent companion task (address `0x58`). Fresh forced
starts can retain positive companion HP without having a companion actor.
The controller now pauses its timer and reactions until that slot owns a live
TMD with root coordinates. Available-companion sound, animation and damage
requests retain their existing order and timing; no actor or story flags are
fabricated. The model lookup uses the task already fetched for that update.

The complete 48-case retry (`historical-crash-retry-companion-ready`) now has
no crashes: five exact parity passes and 43 normal replay EOF results without
a settled target capture, including all six growth-room cases. These 43 remain
unverified, rather than becoming parity passes. All 51 C and 39 Python HD tests
pass on macOS. Loaded-companion natural traversal and other platforms still
need runtime validation; this probe does not establish them.

#### Linux ARM64 validation and fixed-width Bezier output

A fresh Linux ARM64 build compiled the timing, room-return, water-packet,
scene-reservation and companion fixes. All 51 then-current C tests passed.
The nine gallery view-11 controls passed through Vulkan/lavapipe, covering the
controller route from view 10, active foreground material on both buffers,
original command/simulation equality, native VRAM equality within each comparison
and malformed-pack fallback (`linux-current-fixes-foreground-view11`). This was
before the Bezier correction below and is not a physical-GPU performance test.

The 48 historical forced starts exposed an additional Linux failure: 47 reached
normal 8001-frame replay completion, while Shelter R48 view 10 aborted with
stack protection. GDB identified `_actor503500LargeChainLayoutLinks`.
The shared Bezier evaluator accepted a host `long[3]` output while its chain
callers supplied the 32-bit XYZ prefix of `VECTOR`; on LP64 it wrote 24 bytes
into that 12-byte prefix. The output contract and the debug-room caller now use
`s32[3]`. Arithmetic and sampling order are unchanged. The regression test runs
the actual shared evaluator, verifies three independent axes, VECTOR padding
and surrounding canaries, checks a nonconstant midpoint and zero-count inputs.
All 52 C tests pass on both macOS and Linux ARM64. The formerly aborting Linux
R48 replay now completes all 8001 frames (`linux-r48-bezier-fixed.log`).
The final rebuilt Linux binary also completes all 48 historical forced-start
replays without crashes (`linux-historical-crash-bezier-fixed/report.json`).
This checks replay termination with native backgrounds; it does not establish
settled image parity, natural story traversal or full platform correctness.
The macOS R48 replay also completes after the correction.

#### View-12 further geometry review

Two additional generated candidates were checked against all 14 native
foreground source regions. View-12 v4 retained the required canvas aspect but
moved multiple architectural edges and added ceiling seams; its source-12
corner remained ambiguous. A v5 local-corner repair from the better v2 input
also changed other regions: sources 7, 8, 11, 12 and 13 no longer had zero
best shifts, with ambiguous evidence. Both are rejected and were not packed.
The local art assessment retains candidates, prompts, diagnostic reports and
SHA256 values. V3 remains rejected for incompatible canvas aspect. No final
view-12 artwork is approved, and these experiments do not extend verified pack
coverage. The v2 runtime fixture remains technical evidence only.

#### Further host coordinate-width corrections

A syntax-only incompatible-pointer audit found 13 diagnostic sites involving
`int *` model components and `long *` accessors. Review identified six actual
pointer declarations: three fractional root-push helpers (actors 00400, 01100,
356100), the fireball Y component (105100), and boss walk X/Z components
(503500). These accessors now use `s32 *`, matching MATRIX/VECTOR storage and
preserving adjacent components on LP64. The correction arithmetic is unchanged.
A targeted syntax pass over all five source files succeeds with none of those
13 diagnostics remaining. The wider audit's other warnings remain unaudited.

The corrected five objects were compiled and linked to a separate local binary
(`host-coordinate-width-build/build.json`), preserving the executable used by
the concurrent full survey. Its nine Metal gallery controls pass
(`foreground-view11-coordinate-width-fixed`). Its complete historical 48-case
survey has five parity passes and 43 normal replay EOF results without settled
targets, with no crashes (`historical-crash-coordinate-width-fixed`). Existing
52 C and 39 Python HD tests pass. This is not Linux/Windows runtime validation
of these latest coordinate changes, nor full natural boss/creature traversal.

#### Completed v3 whole-catalog survey and balcony shaft bounds

The completed macOS software survey selected all 3314 eligible static starts
from the reviewed catalog and required complete ordered GPU-state evidence.
It recorded 2703 exact off/on VRAM and rendered-image matches, 606 normal
replay completions without a settled target, and five image differences; no
process crashed (`full-survey-current-v3/report.json`). The binary and original
reports are retained locally. This baseline predates the latest coordinate
pointer changes. The 32 ineligible logical entries remain outside its scope.

All five differences belong to the nighttime motel balcony. Repeating them
also changed pixels between runs with the same widescreen setting, despite
equal captured simulation/player state. The room passed scalar point addresses
to a two-point light-shaft drawer, reading beyond separate C objects. The room
adapter now copies each named start and endpoint into an explicit two-vector
array. Existing endpoint values, lamp-break position and flare table remain
in use; no light geometry is guessed from neighboring storage. Five failing
cases now pass, and all 117 eligible balcony room/view variants pass exact
software off/on parity on the corrected binary
(`balcony-explicit-pairs-all-views`). All 52 C tests pass. The earlier whole
survey and this room retry have distinct binary hashes and scopes.

Separately, the Linux ARM64 coordinate-pointer build (before the balcony
adapter) passes all 52 C tests, all 48 historical forced-start replay completions
and the nine Vulkan/lavapipe foreground controls
(`linux-historical-crash-coordinate-width`,
`linux-coordinate-width-foreground-view11`). This does not establish physical
GPU performance, Windows runtime behavior, or natural story traversal.

#### Launcher-backed survey attribution and Linux balcony verification

The static-view survey now accepts `--runner` with the same binary-first
argument convention as the foreground checker. Reports retain separate SHA256
identities for the actual binary and launcher, plus the selected catalog and
replay. Any change to these four files prevents a complete report. The
end-to-end fixture checks argument forwarding with paths containing spaces,
fixed-input identities, and rejection of each input mutation. The executable
fixture uses a POSIX shebang and is skipped on non-POSIX hosts. All 41 Python
HD tests pass on macOS; the existing settled-capture validation remains intact.

The rebuilt Linux ARM64 balcony fix passes all 52 C tests. Using the new
launcher path, all 117 eligible balcony room/view variants have exact off/on
VRAM and rendered-image equality in both software and Vulkan/lavapipe runs,
with complete GPU-state captures (`linux-balcony-explicit-pairs-all-views`,
`linux-balcony-explicit-pairs-vulkan-all-views`). Both reports identify the same
Linux binary. The five original failures also pass a direct macOS software
run through the updated survey (`macos-survey-input-hashes`). These results
cover this room's forced static starts; natural traversal, physical GPU
performance and other rooms/backends retain their separate validation scope.

#### Incinerator reachability diagnosis

The 280 incinerator catalog entries are source-authored: seven room-state maps
with 40 views each. Only 21 entries have an arrival warp matching their logical
view; the other 259 reuse another arrival. The whole survey verified 13 entries
and left 267 without settled targets. These remain unverified.

Read-only EOF checkpoints from the forced view-1 start show active scripted
playback: at 1000 input samples the live room is view 11, and at 1600 it is
view 13, both with eventState 1, actor mode 2 and HP 110. At the full replay EOF,
the player is unavailable with negative HP and the session area is zero.
Neutral input after sample 700 also fails to produce settled captures for
starts 1, 8 and 9. This diagnoses these sampled starts only; it does not prove
the same cause for every unverified incinerator entry.

Three attempted frame-count checkpoints captured location [0,0,0,0,0,0] with
no player/model after returning to title, when frame counters were reused.
They are explicitly rejected despite equal off/on pixels. Their local
assessment records that rejection; they add no room coverage. Future transient
checks must verify location and source identity in addition to frame number.
No story flags, player position or camera state were injected. Local evidence:
`incinerator-start-timeline`, `incinerator-neutral-arrivals`,
`incinerator-reachability-assessment.json` and the rejected
`incinerator-intro-checkpoints` experiment.

#### Context-gated transient checkpoints

`PE2_CAPTURE_TARGET` now optionally constrains a transient checkpoint by the
same stage/area/view/room and native key grammar as a settled survey. A malformed
constraint disables the checkpoint. The shared parser/matcher is tested against
wrong locations, wrong keys, title context, invalid syntax and range boundaries.
The capture diagnostic now prints the actual captured context/key in both modes.

Owned-disc controls pass on macOS and Linux ARM64: the matching gallery target
captures view 11; the wrong source key and invalid target capture nothing. The
incinerator frame-900 probe no longer exits on the unrelated title image when
constrained to its room. A macOS unguarded control still reproduces that title
capture, confirming why a frame number alone is insufficient. Local evidence:
`capture-target-runtime` and `linux-capture-target-runtime`. These are guard
controls and add no incinerator coverage. Five existing settled balcony probes
still pass (`capture-target-settled-regression`). All 53 C tests pass on both
hosts and all 41 Python HD tests pass on macOS. No player/story/camera state is
changed by this diagnostic filter.

#### Gallery view 13: local material candidate and reached endpoint

The view-13 native key is `f0ce18a723c50c14`. Its 12 authored foreground sources
were exported from a passing static capture. Generated v1 is rejected for an
incompatible canvas aspect; raw v2 retains the correct aspect but moves some
source edges. A source-guided v2 normalization retains native structural edges
and bounded generated microtexture; all 12 diagnostic regions then have zero,
non-ambiguous best shifts. Its centre-to-outpaint sharpness and surface joins
remain visible, so this is not approved final artwork.

Local base and combined packs pass the nine Metal controls initially at a
forced view. That initial player's position was off-screen and is not an
occlusion-pose validation. A controller route now reaches view 13 from the
existing view-10 start, through 11 and 12: continue forward to tick 3000, hold
Right for 96 ticks, then Up+Circle until 4200 and release. The endpoint remains
at [12675,133,123] with warp 6. Read-only EOF snapshots confirm the location;
no player/camera/story values were injected. The route uses the earlier gallery
startup controls before tick 2900.

The same candidate passes all nine controls after that route
(`foreground-view13-source-guided-v2-route`), with a visible player, all 12
foreground sources active on physical buffers 0 and 272, exact native VRAM
within the compared modes and malformed-pack fallback. Re-exported route
sources have identical native hashes to all 12 static sources. The two wider
captures are 1707x960. This proves one endpoint's integration, not final matte
quality across poses or other-platform behavior. Art, native sources and packs
remain local under `build-hd-study`; no artwork is committed or distributed.
The view-13 art assessment retains prompts, candidates, audits and hashes.

#### View-13 sharpness follow-up

Three further generated edits attempt to remove the normalized candidate's
centre/side sharpness difference. V3 improves surface detail but most source
regions move about one native pixel and the right-wall joint changes more;
it is rejected. A local right-wall repair (v4) and another high-fidelity
restoration (v5) produce incompatible canvas aspects and are rejected before
packing. A diagnostic one-pixel bounds offset is also rejected by the format's
16-pixel alignment rule; no format invariant was relaxed or translated pack
created. The source-guided v2 technical fixture and its runtime evidence remain
unchanged. The local assessment retains all three outputs, prompts, dimensions,
rejection reasons and hashes. No final view-13 artwork is approved.

#### View-12 additional geometry attempts

Three further built-in image edits try to preserve the source-guided canvas's
far-left ceiling corner while restoring coherent surface detail. V6 and v8
produce 1659x948 canvases; the existing aspect check rejects both. V7 produces
1714x918, within integer-rounding tolerance, but its geometry diagnostic still
finds nonzero/ambiguous shifts for sources 7, 8, 11, 12 and 13 (source 12's best
shift is five native pixels left). These candidates are rejected and never
packed; no crop, stretch or registration tolerance change is used to conceal
the mismatch. Outputs, exact prompts, hashes, dimensions and failed regions are
retained privately in `art/mist-shooting-gallery-view12/`
`followup-v6-v8-assessment.json` and `provenance-v6.json` through
`provenance-v8.json`. View 12 remains unfinished, and the older installed v2
material remains an unapproved technical fixture.

#### View-13 full-detail v6 candidate

A new built-in image edit of the source-guided v2 canvas improves uniform
surface detail across the centre and side extensions. The saved image is
1714x918, within the packer's existing integer-rounding aspect tolerance; an
explicit size-only fit produces the canonical 1792x960 canvas. No geometry
translation, crop, format relaxation or source-guided repaint is applied.
All 12 foreground-region diagnostics have zero best displacement, with 11
non-ambiguous peaks. Source 1's narrow upper-right wall region remains ambiguous
(score gap 0.00682); the diagnostic is not contour or artwork approval.

The private `view13-generated-v6-base-pack` and
`view13-generated-v6-combined-pack` use this same canvas for background and
foreground materials. Nine Metal controls replay the controller route from
view 10 to view 13 (`foreground-view13-generated-v6-route/report.json`). Both
physical buffers activate all 12 foreground replacements. Base/combined pairs
preserve full native VRAM, normalized original GPU commands and captured
simulation state; malformed metadata and missing foreground atlases fall back
exactly to the base pack. Classic and wide captures were visually inspected:
former vertical sharpness bands are no longer apparent at this sampled pose.
A further nine controls capture at 180/181 matching frames on the same route
(`foreground-view13-generated-v6-route-later/report.json`), with the player
farther down the corridor. The later wide capture and its native original were
also visually inspected. Across all six base/combined presentation pairs, the
complete captured model lists, player roots and pose hashes match exactly
(`art/mist-shooting-gallery-view13/assessment-v6.json`).
The candidate is still unapproved: the ambiguous region, exact contours,
additional player occlusion and broader transitions require review. It does not
replace the installed four-view fixture. Its prompt, input/output hashes and
review notes remain in the ignored `art/mist-shooting-gallery-view13/`
`provenance-v6.json`; no artwork or native data is committed.

#### Installed four-view gallery transition fixture

A private `gallery-four-view-study--1` installation combines view 10 v8,
view 11 v3, view 12 v2 and view 13 source-guided v2. All 16 renderer payloads
pass the installer manifest/structure/hash checks; only renderer files are
assembled. The artwork remains unapproved, especially the documented geometry
and sharpness issues in views 11–13. This is not a production artwork release.

The installed combined pack passes all nine Metal controls at each reached
endpoint 11, 12 and 13, using the four-view base pack for the paired comparisons
(27 controls total; `gallery-four-view-study-route-{11,12,13}`). These checks
cover active foreground material on both buffers, native VRAM within compared
modes, normalized original-command/simulation checks and malformed-pack fallback.

A return route holds Down+Circle from tick 4200 to 6500, then releases. Read-only
EOF inspection places the player at [-6995,128,4611] in uncovered view 8.
A context-gated completed-buffer checkpoint at frameCount 8000 compares four
configurations: original, widescreen with no pack, installed pack in classic,
and installed pack in widescreen. All four have identical full native VRAM,
rendered RGB and captured simulation counters; the output is 1280x960, with no
foreground replacements. The player is visibly present. Model/player hashes
are incomplete at this checkpoint, so it does not prove full actor-state parity.

The installed-pack log confirms loading backgrounds 10, 11, 12, 13 and returning
through 11 and 10 before the uncovered view. Evidence is local under
`gallery-four-view-return-fallback`, `gallery-four-view-return-search` and
`gallery-four-view-study-assessment.json`. This verifies sampled transitions
and one missing-art return on Metal, not all-game traversal, other GPU backends
or final artistic/occlusion approval. No artwork or native payload is committed.

#### Four-view installed fixture on Linux Vulkan

The same installed 16-file `gallery-four-view-study--1` fixture passes all nine
Vulkan/lavapipe controls at reached view 13 after the route through views 10–13
(`linux-gallery-four-view-study-route-13`). All 12 foreground sources activate
on physical buffers 0 and 272; base/combined native VRAM and normalized
original GPU inputs agree within the checked modes. Both malformed-foreground
controls return to the expected native foreground path.

The return to uncovered view 8 also passes on that same Linux ARM64 binary
(`linux-gallery-four-view-return-fallback`). Installed-pack widescreen and
original rendering have identical full VRAM, rendered RGB, all 765 normalized
original GP0 commands and captured simulation counters at the context-gated
frame-8000 checkpoint. The output stays 1280x960 with zero foreground
replacements. The log records all four covered backgrounds before the native
return. Model hashes are incomplete, so this is supporting render/input evidence
rather than complete actor-state equality. The unchanged prior Metal captures
also have identical 765-command audits (`input-comparisons.json` beside the
Metal return assessment).

Both Linux reports identify the same binary and the private installed manifest;
`linux-gallery-four-view-study-assessment.json` binds their hashes. This adds
one Linux reached endpoint and one return checkpoint, not Windows/physical GPU
coverage, all possible transition states or final artistic approval.

#### Windows cross-build and portable graphics CI

A local MinGW-w64 GCC 16.2 cross-build produces the current Windows x86-64
SDL_GPU game executable and all 16 members of the new `portable_graphics_tests`
CMake target. The local assessment records PE files, sizes, hashes and system
DLL imports (`windows-cross/assessment.json`). At that checkpoint no Windows
runtime was configured; the later Wine evidence below executes these binaries
without establishing native Windows gameplay.

The build exposed two fixture portability problems. `background_coverage_test`
now defines `__psyz` when including the public runtime types, preserving the
pointer-sized `u_long` contract on LLP64. The isolated scene-buffer allocator
test disables COFF unwind tables so dead-section linking can discard unrelated
CD command handlers. The game retains its unwind tables; the cross-built game
object has `.pdata` sections while the isolated test object has none.

CTest's portable graphics label selects the same 16 tests as the aggregate
build target. The normal CI workflow now has a Windows CLANG64 build/test/help
job, and Windows release packaging uses that target/label instead of a separate
five-test list. Both workflows pass actionlint and shellcheck locally; the jobs
have not been run on GitHub for these unpublished commits.

All registered host-test targets now undefine `NDEBUG` in release configurations,
so checks and fixture setup inside `assert()` actually execute. With assertions
enabled, all 53 C tests pass on macOS and Linux ARM64. The Windows executables
compile with assertions as well (the cache fixture imports `_assert`). The 16
portable executables subsequently pass under Wine, as detailed below; native
Windows execution remains required. No change to PsyZ was required.

The bundled runtime also declares an experimental build-time `sdl3_gl` backend.
Its atlas-loading and widescreen entry points are unsupported stubs, and the
macOS/iOS configurations reject that backend explicitly. The default SDL_GPU
binary cannot switch to OpenGL through `SDL_GPU_DRIVER=opengl`. This corrects
the earlier overly broad statement that the repository contains no GL backend;
no experimental GL build/runtime validation is claimed here.

#### Material/reference separation and reconstruction

`material_bundle.py` splits validated runtime atlases into material-plane PNGs
and a separate content-addressed private reference cache. It preserves existing
layout descriptors, uses disjoint directories, rejects unlisted files/symlinks,
and stages outputs without overwriting earlier artifacts. Reference cache
entries are published atomically and identified by dimensions plus RGBA pixel
hashes. Hydration verifies those references and invokes the existing structural
pack verifier before publishing a runtime folder. Input changes during either
operation prevent output publication.

Ten new tests cover material-only plane contents, exact base/foreground runtime
reconstruction and installation, reference-cache independence from material
colors, missing/changed references, path escapes/symlinks, output immutability,
input changes and interrupted writes. All 57 Python HD tests pass on macOS;
the ten new tests also pass on Linux ARM64 with Pillow 10.2.0.

The private four-view gallery fixture separates into eight reference planes and
16 material/descriptor payloads. On macOS, hydration reproduces all 16 original
files and the runtime manifest byte-for-byte and installs as a new immutable
folder (`gallery-four-view-material-roundtrip.json`). Linux reconstruction of
that Mac-origin input preserves every decoded atlas pixel and descriptor byte;
PNG encoding/hashes differ across Pillow versions, and a fresh local runtime
manifest records the new encoded files (`material-bundle-linux-roundtrip.json`).

Reference acquisition now reconstructs the cache from local RGB555 background
and foreground source exports. Six additional tests cover legacy/inset/partial
foreground cells, exact reconstruction without the author's runtime/cache,
changed sources, missing or ambiguous contexts, mismatched sprite parameters,
invalid descriptors, expected-plane hash mismatches and cache reuse. Descriptor
layout validation is shared with the runtime installer and runs before cache
construction. The real four-view fixture acquires all eight reference planes
from native exports, then hydrates all 16 payloads plus its manifest byte-for-byte
(`gallery-four-view-acquired-roundtrip.json`). This acquisition does not read the
author's runtime pack or reference cache. All six acquisition and ten material
bundle tests also pass on Linux ARM64. Acquisition and hydration of the real
four-view fixture there preserve all atlas pixels and descriptor bytes
(`reference-acquisition-linux-roundtrip.json`); encoded PNGs may differ by
Pillow version.

A real Metal route to view 13 compares the original and hydrated installations:
rendered RGB, full native VRAM, normalized original GPU inputs and captured
simulation counters are identical, with all 12 foreground sources active
(`material-bundle-runtime/assessment.json`). This does not approve the candidate
artwork, layout metadata or a public bundle. Automatic reference acquisition
from owner discs/live gameplay and the full production distribution workflow
remain required. Caches and hydrated packs stay private.

#### Source classification independent of static background availability

The logical catalog now adds known room renderers directly, even when the
native inventory has no matching image or predates that room classification.
The regenerated catalog retains all 3346 rows, 3314 start candidates and 19
source diagnostics; no eligibility or runtime-verification result changes.
It adds missing special labels to eight Akropolis plaza rows, ten MIST R18
rows, 72 sterilization-room rows and pod-access-tunnel view 11
(`special-source-classification-assessment.json`). All 58 Python HD tests pass
on macOS, including absent-background classification without granting coverage.

Akropolis plaza has no static view files for its eight logical views. Its room
sequence in `src/stages/akropolis/rooms/plaza/plaza.c` prepares movie playback,
seeks forward/reverse traversal streams and applies animated camera samples.
These rows require streamed-scene validation rather than invented static
background replacements. This source classification does not prove correct
movie presentation or natural traversal.

MIST R21 has no static view rows because the Akropolis view-map, count, camera
and sprite directories each contain only 20 area entries
(`include/generated/assets/rooms/akropolis/tables.inc`), while its area is 21.
Its room task only installs messages, idles and implements an L3+Cross debug
reload to the plaza; the port's room loader explicitly skips scene-resource
setup. Thus it is not a missing static artwork target. Its four raw exporter
diagnostics remain visible as source facts; no passing runtime capture is
inferred. Other camera/source-boundary diagnostics remain unresolved.

#### Current-binary complete static survey

`full-survey-current-v4/report.json` completes all 3314 eligible source rows
against a preserved copy of the current executable, SHA-256
`d4ea6bfb17d03395b83f903e213b5f4d16b749953ab3da808f940f7d1d1a7d7a`.
The catalog includes the special-room classification described above, with its
hash and replay hash recorded. Eight parallel headless software workers use
virtual raster timing, video audio timing, isolated settings/cards and no art.
Every successful run requires the expected native source and exact six-part
location, at least 30 matching field-ready frames, and a complete attributed
ordered GPU audit.

Results: 2708 exact off/on comparisons of full native VRAM and presented RGB;
606 replays end without a settled target capture. There are no renderer parity
differences, process crashes or timeouts in the completed report. The survey
returns nonzero because missing captures remain unverified. All 5416 successful
run audits were separately hash-checked and validated against their capture
identity/buffer. Successful pairs by stage are 477, 463, 749, 681 and 338.

Compared with the earlier complete v3 survey, the five balcony pixel-difference
cases now pass in this single complete current-binary run. Every previous
passing location still passes, and the exact same 606 no-target locations remain
unverified (`full-survey-current-v4/assessment.json`). Thus the old broad report
and later room-specific evidence no longer need to be combined to establish
current static off/on parity. The excluded 32 source rows, all no-target cases,
natural gameplay transitions, production artwork and physical GPU/platform
coverage remain outstanding; this report does not prove them.

#### Gallery fixed-training view reached through its menus

The generic full-survey replay cannot capture gallery view 14
(`[1,20,14,1,3,1]`, native key `843ae3abe481d815`). A read-only EOF checkpoint
instead finds view 15, a live player and pending display mode 129. The room's
jukebox requires a selected track followed by menu/cancel to close; generic
Cross pulses choose tracks but do not close it. A controller trace adds Circle
at pad ticks 2600–2607, then resumes confirmations through the remaining menus.
A second EOF checkpoint reaches view 14 with HP 100 and no pending display mode.
No position, camera, story or task values are injected.

The same trace passes two settled off/on probes: software
(`gallery-view14-controller-survey/report.json`) and headless Metal
(`gallery-view14-controller-metal/report.json`). Each captures the expected
source/location after 30 field frames, complete ordered GPU audits, and identical
full native VRAM plus RGB within that backend. These no-art probes use native
1x 320x240 presentation; no higher-scale HD, cross-backend pixel equivalence or
final artwork approval is claimed. The software training frame was visually
inspected. `gallery-view14-diagnosis/assessment.json` binds both reports to the
same executable and controller trace.

`survey.py --replay` now supports such reproducible menu routes. Two integration
tests verify exact trace copying/attribution and reject a source trace changed
during collection; all 60 Python HD tests pass. The original full report retains
its 606 generic no-target entries. This supplemental route accounts for one of
them, leaving 605 without settled evidence rather than rewriting that report.

#### Gallery fixed-training view-14 artwork study

The verified menu route supplies a first artwork study for native background
`843ae3abe481d815`. Its source audit exports all 29 authored foreground sources
from the settled training capture. A 1792x960 native geometry guide marks
256-pixel side margins for outpainting. Built-in image edits produce two
1713x918 candidates, retaining prompts and hashes under the ignored
`art/mist-shooting-gallery-view14/` directory.

Raw v1 and v2 are not approved: lower foreground-rail regions show nonzero or
ambiguous gradient matches, despite sharper coherent material. A source-guided
v1 technical canvas preserves native high-contrast geometry and bounded
surface microtexture. All 29 best shifts are zero, with 25 non-ambiguous peaks;
sources 2, 11, 12 and 13 remain ambiguous. Its centre/side sharpness transitions
remain visible, and its centre includes native-derived pixels. It stays private
and is not a substitute for finished authored artwork. Base/foreground packs
are separate study folders and do not replace the installed four-view fixture.

Nine headless Metal 4x controls replay the verified training-menu trace
(`foreground-view14-source-guided-v1-route/report.json`). Classic and wide
base/combined pairs preserve native VRAM, normalized original GPU commands,
captured simulation counters and complete model/player-pose hashes. All 29
foreground replacements activate; wide captures cover physical buffers 0 and
272, and malformed/missing foreground payloads fall back exactly. The 1707x960
wide training frame was visually inspected: the player is visible, but material
sharpness bands remain obvious. This is renderer integration evidence for a new
view, not final art or moving-target occlusion approval. The local assessment
binds the runtime report, candidate hashes and unresolved diagnostics.

#### Linux Vulkan training-view integration

The same private view-14 fixture and native training-menu controller trace pass
all nine controls on Linux ARM64 with Vulkan/lavapipe at 4x
(`linux-foreground-view14-source-guided-v1-route/report.json`). The current Linux
executable is rebuilt before collection and retained at SHA-256
`89a2a7d4c452e9ebb776926cfd55637ca74a2f59a4c49f9f6ab343a89c123ad3`.
Both physical buffers 0/272 activate all 29 foreground replacements in 1707x960
wide presentation. Within Linux, base/combined pairs preserve full native VRAM,
normalized original GPU inputs, simulation counters and complete model/player
pose hashes. Invalid metadata and missing atlases fall back exactly.

Classic and wide Linux captures were visually inspected. Across all nine
corresponding Metal/Linux cases, captured model/pose hashes, simulation counters
and normalized GPU command sequences agree. Native VRAM differs across the
backends in six or seven RGB555 words; cross-backend framebuffer or native-VRAM
identity is not claimed. The bound assessment records these differences rather
than hiding them (`linux-foreground-view14-source-guided-v1-route/assessment.json`).
Lavapipe is CPU rendering, so this does not establish physical-GPU performance,
Windows behavior, full training playback or final artwork quality.

A further local rail-repair image edit (raw view-14 v3) produces a 1622x970
canvas. It is rejected by the unchanged aspect check and never packed; the
exact built-in prompt and result remain private in `provenance-v3.json`.
Centre/side sharpness and moving-target occlusion review remain open.

#### Later training target and native attack inputs

Read-only gallery checkpoint diagnostics now report live course/menu tasks,
selected course, phase, target count/lock mask, spawn index, timer and per-kind
kills. Work is read only through a matching live controller task in the default
list, rather than trusting the retained global controller pointer after task
retirement. Existing room diagnostics remain unchanged.

The earlier menu trace reaches course 0 phase 7 with one target and waits for
lock-on; it does not exercise the subsequent tutorial. A native input extension
holds Square and pulses R1/Cross from pad tick 3600. A nearby EOF checkpoint
then finds phase 10, one recorded target kill and another live target. It also
shows the ammo-instruction interruption; this trace does not complete the course.
The checkpoint is 12 vsyncs before the corresponding rendered capture, explicitly
recorded as a nearby state observation rather than same-frame proof.

Nine Metal 4x controls with `--settle 300` replay that trace
(`foreground-view14-source-guided-v1-active-target/report.json`). The captured
paper target is visible and the scene contains 11 complete models, compared
with the earlier training-entry pose. All base/combined pairs preserve full
native VRAM, normalized original GPU inputs, simulation counters, complete
models and player pose. All 29 foreground sources activate on both physical
buffers. Invalid metadata/missing-atlas fallback passes. Sampled opaque target
interiors are pixel-identical between base/combined pairs in classic, wide-300
and wide-301 captures; this is not complete silhouette/occlusion coverage.
Original and wide-combined captures were visually inspected, with known artwork
sharpness issues still visible. The local bound assessment records target crop
rectangles, report/replay hashes and the separate debugger observation.

An exploratory 1700-settled-frame capture reaches replay EOF without the required
consecutive field-ready interval and remains unverified. Its threshold was not
relaxed or relabelled. The successful 300/301-frame captures independently satisfy
the existing settled gate. Python compilation and real debugger checkpoints
validate the diagnostic additions; no game executable or PsyZ code changes.

#### Native course-0 countdown, completion and return

The earlier attack replay kept pressing Cross, which releases a target lock;
holding Square continuously then cannot acquire the next target. A revised
controller trace preserves the menu prefix, then periodically releases/represses
Square and pulses R1 without Cross. Native attack handling also performs the
necessary automatic reloads. This is authored controller input, not a scene,
position, camera or flag override.

Read-only checkpoints on prefixes of the same 12001-sample trace show course 0
in timed-wave phase 15 at sample 8001 (13 paper-target kills, three live targets,
1295 clock frames remaining), then controller finishing state 2 at sample 10801
(23 kills, one remaining target, clock zero). At sample 12001 training is inactive,
the live course task is gone and the player is in view 8 with HP 100. This proves
the sampled tutorial/wave/countdown/finish route; no high-score prize or completion
of other courses is claimed.

Three headless Metal 4x runs use that exact trace: no pack in classic, the view-14
study pack in classic, and the same pack with widescreen requested. Each reaches
30 settled frames in uncovered view 8 at vsync 11120. All three captures have
identical full native VRAM, presented RGB, simulation counters, complete models
and player pose; normalized original GPU command comparisons also pass. Output
is 1280x960 with zero foreground replacements, despite the pack having loaded
view 14 and its 29-source atlas earlier in the run. The returned classic frame
was visually inspected. Thus this course completion restores native framing
without stale extended artwork.

`gallery-course1-return-fallback/assessment.json` binds all three runs, input
hashes and the separate course-progress checkpoints. Its local `check.py`
reproduces the collection. This is one authored-warp course route and one
post-course return checkpoint; it does not establish per-frame parity throughout
the course, final artwork quality, other courses, other platforms or an entire
story playthrough.

#### Windows PE execution under Wine

A local amd64 Linux container with Ubuntu Wine 9.0 executes all 16 cross-built
Windows x86-64 `portable-graphics` tests successfully, with assertions active.
The executable list comes directly from CTest's label selection; each PE and log
hash is recorded in `windows-wine-runtime/assessment.json`. A current rebuild
reports no work required. The game PE hash remains
`7169346e8ebca24d627eea02c7ab3b88832722075520782814f02dd149d5caf0`.
`--help` loads the executable and prints usage with the existing exit status 2;
this is the CLI's current usage-error behavior, matching release CI's text check.

With the owner's two disc images mounted read-only, the Windows game reaches
training view 14 through the native menu trace, then a second trace reaches
uncovered view 8 after course-0 countdown/completion. Both routes obtain complete
source/location-gated 30-frame captures with widescreen off and on under the
headless software renderer at 1x. Each pair has exactly identical RGB, full
native VRAM, original GPU commands, simulation state, models and player pose.
The training capture is at vsync 3134; the course return is at vsync 11120.
The returned Windows frame was visually inspected.

The Windows training capture also matches the native macOS software capture
exactly in RGB and full VRAM, with all 558 normalized GPU commands and captured
model/simulation state equal. At course return, the 787 normalized commands and
model/pose/simulation state match the earlier Metal route; framebuffer equality
across software and Metal is not asserted. Input hashes, captured reports,
comparison records, Wine version and local runtime-image identity are retained
in the ignored assessment. Test-only runtime caching occurred before mounting
any game data, and source captures are not included in the cached image.

This executes the actual Windows PE code and exercises Windows ABI, file IO,
disc decoding, native menus and a sampled training route. Wine in an emulated
Linux container is not native Windows. D3D12, physical GPUs, the Windows HD
presenter, other courses and whole-game play remain unverified. No public CI
job, release or upload is implied by these local results.

#### Windows GPU compatibility probes remain unsuccessful

Additional Windows PE probes use Xvfb, Wine 9 and Mesa 25.2.8 lavapipe in an
isolated amd64 Linux container. The cached Windows SDL build enables both
D3D12 and Vulkan, so an absent build backend is not the cause of rejection.
However, the unchanged PsyZ Windows runtime requests DXIL shader format only;
SDL's Vulkan device preparation requires SPIR-V. Forcing `SDL_GPU_DRIVER=vulkan`
therefore cannot validate this Windows executable's GPU presentation.

The default Windows backend, forced as `direct3d12`, is also rejected by the
built-in Wine stack. Verbose SDL logging identifies the reason: DXIL is not
supported and the application supplies no DXBC. These runs continue without a
valid device and were explicitly terminated after diagnosis; they provide no
renderer parity evidence. A separate 15-second bounded verbose probe confirms
the capability rejection. The successful `gpu_state_test.exe` only exercises
command-state parsing and never initializes SDL_GPU, so its result is not
promoted to backend evidence.

A separate Wine prefix then loads the published
[vkd3d-proton 3.1](https://github.com/HansKristian-Work/vkd3d-proton/releases/tag/v3.1)
D3D12 DLLs with DXVK 2.3.1 DXGI. The vkd3d release archive matches the release
API's SHA-256. DXVK 3.x is not used because its
[driver requirements](https://github.com/doitsujin/dxvk/wiki/Driver-support)
include Wine 10.1+, while this environment has Wine 9. The alternative stack
enumerates llvmpipe, then the process exits 133 with Rosetta's invalid-GDT-selector
error before producing any capture. This is an unsuccessful compatibility-stack
probe; it does not establish a defect in the game's D3D12 renderer.

`windows-wine-gpu-runtime/assessment.json` records failed outcomes, log/source
hashes and dependency DLL/archive hashes. No settled GPU capture exists. PsyZ,
shader requirements and game renderer code remain unchanged. The earlier 16
Windows PE regression tests and successful software gameplay remain valid;
native Windows/D3D12 and Windows HD presentation still require verification.

#### Verified portable material archives

`material_archive.py` creates deterministic flat material-only ZIPs and unpacks
with a required trusted SHA-256 into a staged, fully verified new folder. It
reuses the material/descriptor verifier and never exports the separate reference
cache. Native references can subsequently be rebuilt from owner exports using
the acquisition command. Material provenance and public distribution approval
remain separate requirements.

Nine new tests cover deterministic byte-for-byte transfer/hydration, exact entry
inventory, checksum rejection, immutable outputs, path traversal and cache-file
rejection, duplicates/symlinks/directories, unsupported compression, changed
payloads and rehashed bad descriptors, resource limits, interrupted creation,
CRC failure, source changes during creation/extraction, and symlink inputs and
outputs. All 69 Python HD tests pass on macOS; all nine archive tests also pass
on Linux ARM64.

The actual four-view fixture creates archive SHA-256
`897de8a85df51038fac24406172a236a7967e88f9945d20ace251c58949be48a`.
A fresh extraction, reconstruction of eight reference planes from raw native
exports and hydration preserve all 16 original runtime payloads and their
manifest byte-for-byte on macOS (`material-archive-roundtrip.json`). Linux
creates the exact same archive bytes from the same material inputs, then
unpacks/acquires/hydrates all atlas pixels and descriptor bytes identically
(`material-archive-linux-roundtrip.json`). Hydrated PNG encodings may differ
between Pillow versions. No game binary changes or additional runtime approval
are implied; the archive and resulting runtime packs remain private.

#### First gallery return-view-8 artwork study

Native background `0915825986800bae` now has a first private outpaint/detail
study. Its foreground source audit exports 25 authored sprites from the
verified post-course return capture. The route retains authored warp 3; a
separate scoped catalog records that observed location while preserving the
source/view mapping. The general inventory's default probe warp is unchanged,
and no identity gate is relaxed.

A built-in image edit of the native geometry guide produces raw v1 at 1713x918,
with 14/25 zero, non-ambiguous local registration results. The near-right booth
divider has nonzero one/two-native-pixel shifts. A second edit targets only that
divider and produces v2 at 1714x918, improving to 16/25 zero, non-ambiguous
regions but leaving nonzero/ambiguous edges and changing other local peaks.
Neither image is packed or approved. A third built-in edit again targets only
the numbered divider, yielding 1713x918 and 17/25 zero, non-ambiguous regions.
Eight regions remain unresolved, with source 22 reaching the nine-native-pixel
search boundary. This candidate is rejected and not packed; the metric is not
contour approval. `generated-v3-candidate.png`, `prompt-v3.txt`, registration and
`provenance-v3.json` are retained privately with the earlier attempts. Bench
geometry, signs/glyphs, floor joints and full silhouettes still require review.

Candidates, exact built-in prompts, source/audit hashes and failed regions stay
in the ignored `art/mist-shooting-gallery-view8/` study directory. The existing
installed gallery fixture still has no view-8 replacement, preserving all prior
native-fallback return evidence. No game/renderer code changes, artwork commit
or public distribution is implied.

## Earlier verification checkpoint

This historical snapshot predates the current verification summary above.

- 49 C host tests pass (including BIOS RNG isolation), including foreground metadata/context validation, ordered GP0 state/LP64 merge traversal, foreground packet bounds/state rejection, missing scene-header timeouts, native layout hashing, metadata/PNG-header checks and coverage gating for complete, missing, partial,
  out-of-bounds and unsupported cases. The hidden GPU probe also proves that
  native 1x rendering falls back exactly with a valid wide pack supplied.
- Two synthetic pack tests and two synthetic inventory tests pass (duplicate pixel hashes, missing inputs,
  stage-0 classification, special renderers and raw-store path resolution).
- Twenty-three Python HD tests pass in total. Additional checks exercise the native
  BS decoder with generated grayscale inputs, logical/mapped view joins,
  unresolved entries, source-guided artwork constraints, and rejection of stale,
  wrong-location, black or incomplete runtime capture evidence.
  Synthetic foreground fixtures check palette lookup, transparent colour zero,
  STP black, UV wrapping, screen clipping, diagnostic scope, and rejection of
  VRAM changed after the runtime survey. Ordered-state checks cover physical
  buffer origins, texture windows, clipped observed coverage and incomplete or
  mismatched GPU-state evidence. Foreground packing tests independently check
  native references, registered RGB crops and rejection of changed inputs.
- CLI rejection, preference persistence and subsequent settings reload pass
  against isolated local profiles and the owner's two disc images.
- A 6001-sample New Game replay reaches the gallery course menu. At pad tick
  6000 the hidden Metal renderer produces byte-identical rendered PPMs and whole
  native VRAM dumps with the preference off/on and no art. The off dump also
  matches the pre-gating GPU run. This is one scene, not all-room parity.
- Local evidence: `/tmp/pe2-hd-safe-parity/{0,1}/`,
  `/tmp/pe2-hd-coverage-tests.log`, `/tmp/pe2-hd-inventory.log`; the reproducible
  four-run check and report live in `build-hd-study/fallback-check/`.
- The software baseline for this exact replay, before option edits, has SHA-1
  `c5f7de402de5dc1c6057ac55b15cafda1381b59f` at tick 6000. This is a different
  checkpoint from the earlier handoff's default-scene hash.

### Bounded glow endpoints in the night garage and trailer coach

A software cross-platform snapshot compared 2834 completed Windows/Wine runs
with the same macOS source locations and input trace. All recorded clocks, RNGs,
player poses and complete model states matched. 80 runs differed in VRAM: 52
only in the mask bit, and 28 also in visible RGB. These differences are separate
from widescreen off/on parity, which passed within each completed platform pair.
The snapshot is not a complete Windows survey or native Windows GPU approval.
Private evidence: `windows-full-survey-v1/macos-comparison-snapshot.json` and
`macos-difference-characterization.json` under `build-hd-study/`.

Two affected drawers indexed beyond separately declared static objects. The
night garage read its fourth capsule beyond a six-endpoint array, and read two
more pairs through a scalar endpoint. The trailer coach read four capsule pairs
through indices 1/3/5/7 of its scalar marker. These pointers relied on host linker
adjacency. The garage now stores five explicit endpoint pairs, and the trailer
coach borrows the first four pairs of its existing view-3 array. Coordinates,
radii and view selection stay unchanged; PsyZ is unmodified.

The patched macOS binary is SHA-256
`c193ab0099981e1787ccd49647f5190b5cd8e76eda8265c373f6866bde7c1f36`.
All 38 eligible source/location variants across the two rooms pass software and
Metal off/on controls, with exact within-backend RGB, full VRAM, normalized GP0
commands, complete model/player poses and simulation state. Against the prior
macOS software baseline, the 58 unaffected-view runs preserve exact RGB, VRAM
and normalized commands; the 18 affected-view runs change their glow packets
and visible pixels. All 76 retain the same model and simulation state.
Private evidence is `glow-endpoints-fix-v1/` (scoped catalog, immutable binaries,
reports, `macos-assessment.json`, `macos-before-after.json`, provenance).
This does not replace the earlier full survey with current-build whole-game
coverage, approve any artwork, or establish cross-backend pixel identity.

The patched Windows binary is SHA-256
`a7ab071df794df903ac00fab980e75b00aca73c6cb13527b0b9230e4d33f0c06`. Its first scoped Wine/Rosetta
run passes 37/38 locations; the remaining view-8 widescreen process exits 133
with `rosetta error: invalid gdt selector index 5`. A separately retained
one-case retry passes. Combining those attributed successes, all 38 pairs / 76
runs exactly match patched macOS software RGB, full VRAM, normalized GP0,
complete model/player state and simulation clocks/RNGs. The original failure
remains in the first report; this does not establish its root cause.
See `windows-assessment.json` and both `windows/` and `windows-retry/` reports.

A Linux x86-64 Release build under amd64 emulation also compiles the patched
game and passes all 53 registered host tests with assertions enabled. Its
binary is SHA-256 `70dabdc9f19edac7861c834900c31036e117c35e1300ce581ddcee4d28a87d5d`;
private logs and image/source provenance are in `linux-x86-release-v1/`.
The initial baseline build was intentionally cancelled before completion to
apply the diagnosed fix; it supplies no passing result. The host tests alone do not verify gameplay, a physical GPU or native Windows behavior.


The same patched Linux x86-64 binary now passes all 38 two-room source/location
pairs in software and separately in headless Vulkan/lavapipe under amd64
emulation. Each backend preserves exact off/on RGB and full VRAM, normalized
GP0, complete model/player state and clocks/RNGs. All 76 software runs exactly
match patched macOS software pixels/VRAM/state/commands. Vulkan also matches
all macOS software state and normalized commands, but only 44/76 RGB and 28/76
full-VRAM captures are pixel-identical; no cross-renderer pixel-equivalence
claim is made. The assessed comparison keeps the differing records intact.
Private evidence: `glow-endpoints-fix-v1/linux-x86-assessment.json`, scoped
software/Vulkan reports and `linux-x86-runtime-provenance.json` (container image,
read-only source mount, immutable binary/runner hashes). The initial malformed
source mount prevented disc access; its failed reports are retained and excluded
from these successes. Vulkan pilot and corrected full runs use separate outputs.
This is emulated two-room rendering coverage, not a physical Linux PC or GPU,
a full traversal, pack-artwork approval, or a finished broad current-build survey.

### Fixed-width GTE outputs in shadow workspaces

The next comparison isolated a signed-output ABI error. PsyZ's `RotTransPers4`
writes `int` screen words, depth-cue output and FLAG. Four shadow/stain scratch
structures and two local limb-shadow drawers supplied host `long` storage.
On macOS/Linux LP64, a four-byte FLAG write leaves the upper four bytes stale;
bit 31 no longer reliably makes the host value negative. Failed projections can
therefore be queued as shadow packets. Windows LLP64 already uses four-byte
`long`, explaining the platform difference without a different simulation.

Those outputs now use signed `s32` in `ActorLimbShadowScratch`, the Zebra Stalker
floor-shadow block, the Mad Chaser block, the actor-00400 ground-stain block,
and the actor-400500/403100 local shadow drawers. PsyZ is unchanged. The regression
in `gte_compat_test` calls the real SDK projection into the actual shared shadow
workspace, prefilled with nonzero bytes, for a quad in front of and behind the
camera. It checks complete screen words, signed rejection and the untouched
adjacent depth field. The old LP64 layout fails six checks; the correction passes.
The updated test also passes as a Windows PE executable under Wine. All 53 macOS
host tests pass with the corrected shared layout and this regression.
The patched Linux x86-64 Release build also passes all 53 host tests under
amd64 emulation. Its binary SHA-256 is
`b9b65623d45d8a0c16d4170e45b9379b2cfec1139b426a8eaf2424f3062c78fc`.
It subsequently passes all 40 source locations in software and separately in
headless Vulkan/lavapipe. Off/on RGB, full VRAM, complete model/player state,
clocks/RNGs and normalized GP0 match within each backend. All 80 software runs
exactly match patched macOS software. Vulkan matches all state/commands but only
44/80 RGB and 16/80 full-VRAM captures; cross-renderer pixel identity is not
claimed. See `shadow-gte-abi-v1/linux-build-assessment.json`,
`linux-runtime-provenance.json`, the immutable binary and `assessment-v2.json`.
These runs do not establish physical-PC/GPU or whole-game behavior.

The final macOS binary is SHA-256
`82d42eba8ded6e1fe97cdd47b36a5210ae57f1c69d5a366d0177b93af4e849f1`.
All 40 locations from the original cross-platform difference snapshot pass
software and Metal off/on controls: exact within-backend RGB/full VRAM,
normalized GP0, complete model/player state and clocks/RNGs. All 80 corrected
software captures also match the existing corresponding Windows RGB, full VRAM
and normalized commands, including the previously mask-bit-only differences.
For the two previously corrected glow rooms, that comparison uses the attributed
glow-fixed Windows fixture; other locations use the preserved original Windows
survey. It is not a replacement for a new Windows run. Metal matches all software
state/commands, but only 78/80 RGB and 38/80 full-VRAM captures match software;
no cross-renderer pixel identity is claimed.

The new Windows binary is SHA-256
`48eff70cc1589f0c9ac147259febf1b4772d74a160c5d3faf79ee1b142300380`.
Its initial Wine/Rosetta run passes 37/40 pairs; three processes exit 133 with
`invalid gdt selector index 5`. A separate three-case serial retry passes all
three. The union of these attributed successful pairs covers all 40 locations;
all 80 software runs exactly match patched macOS RGB/full VRAM, normalized GP0,
complete model/player state and clocks/RNGs. Initial errors remain in the
original report, and their emulation root cause is not established. These are
headless Windows PE software controls, not native Windows/D3D12 validation.

Private evidence is `build-hd-study/shadow-gte-abi-v1/`: failing/passing test logs,
scoped catalog, immutable staged binaries, `macos-v2-to-prior-windows.json` and
`assessment-v2.json`. Earlier partial v1 captures are retained separately: their
remaining underpass discrepancy led to the actor-400500 local-output correction.
The completed broad `full-survey-current-v5` uses its immutable pre-shadow-fix
binary: 2708 exact off/on pairs and the same 606 no-target locations as v4.
It cannot establish current-code whole-game coverage. Other GTE output
call sites using host `long` still require audit; these scoped shadow/stain
corrections do not establish complete supported-platform behavior.

A read-only syntax audit replays all 593 repository `src/` translation-unit
commands from the macOS compilation database with incompatible-pointer warnings
enabled (the normal game build suppresses them). All compile syntactically;
192 emitted `long *` diagnostics normalize to 138 source locations in 16 files,
including repeated shared water drawers. They cover remaining projection and
transformation outputs in water/effects, collision rebuilds and held-model or
helicopter drawers. This is an outstanding source-review/fix list, not a runtime
pass or an audit of macro stores through `void *`. Private evidence:
`gte-output-audit-v1/report.json`, `unique-diagnostics.json`, per-TU logs and the
recorded compilation database/source/header hashes. External code and generated
variant wrappers are excluded; PsyZ remains unmodified.

### Remaining SDK output widths and completed baseline sweeps

The reviewed GTE output corrections now cover 17 further files: signed screen
words, projection/depth-cue values and transform flags use `s32`, matching the
SDK's `int *` outputs. Ordinary host indices are unchanged. This covers water
strips/reservoir/sewers, effect and held-model projections, collision rebuilds
and the departure helicopter. No coordinates, effect parameters, game flags,
PsyZ code or artwork were changed.

The original 593-source audit missed the factory lift because it is compiled
only through generated day/night wrappers. A second audit includes all 713
repository source and generated overlay-variant commands. Before the lift fix,
it reports two residual `long *` diagnostics; after it, all 713 syntax checks
pass with zero such diagnostics. This is a pointer-type audit, not a guarantee
about every macro store through `void *` or runtime execution. Shared source and
header hashes, compiler commands and diagnostic logs remain private under
`gte-output-audit-v1` through `v4`.

All three game builds succeed. macOS and Linux x86-64 pass all 53 host tests;
all 16 currently built portable Windows PE tests pass serially under Wine9.
The latest macOS software binary is SHA-256
`8336bccf7f7d09bd9a8c7559d9ce1d8d5d72f35630b6075f8db722c24a4a7f45`.
All 471 eligible static source variants in 18 carrier rooms pass off/on RGB,
full VRAM, normalized GP0, complete model/player state and clocks/RNGs. This
scoped probe does not activate every scripted effect or moving collision path,
verify those routes, or establish native Windows/physical-GPU behavior.
Private binaries, scoped catalog, hashes, reports and `macos-assessment.json`
are in `gte-output-width-fix-v1/`.

The same current binary also completes all 471 variants on headless Metal.
All off/on RGB/full-VRAM pairs, state and normalized GP0 streams match. All 942
Metal captures match macOS software state and commands, while 926/942 RGB and
916/942 full-VRAM captures are pixel-identical across the renderers. Differences
remain recorded; no cross-renderer pixel-identity claim is made. All 942 Metal
GPU-context hashes and source/location/buffer bindings are revalidated, as are
the native hashes and off/on image pairs. See `macos-metal-assessment.json` and
the complete Metal report in the same private study directory. This scoped
source-gated pass does not activate every corrected scripted effect.


The completed original Windows software sweep uses preserved binary
`7169346e8ebca24d627eea02c7ab3b88832722075520782814f02dd149d5caf0`,
before the endpoint and GTE fixes. All 3314 eligible locations were attempted:
2520 exact off/on pairs, 596 replay EOFs without settled captures and 198
process exits 133 whose retained logs contain Rosetta `invalid gdt selector`.
All accepted Windows locations are a subset of the original macOS passes;
188 macOS-passing locations fail this Windows attempt. This does not clear the
existing 605 locations without generic or supplemental runtime coverage.
Revalidation checks all 5040 accepted captures' metadata, full VRAM hashes,
pixel pairs and ordered GPU-context hashes/buffer bindings. All 2520 pairs also
match recorded state and GP0 streams; two pairs have no GP0 commands in either
mode, explicitly counted rather than treated as nonempty command evidence.
The main report retains all rejected attempts.

A single-case follow-up starts bridge view 7 with the same original Windows
binary after all other own game processes have ended. It still exits 133 with
the same Rosetta message, so concurrency alone is not an established cause.
The error's emulation/game root cause and native Windows behavior remain open.
See `windows-full-survey-v1/completed-assessment.json`,
`completed-integrity.json`, `quiet-retry/` and the before-run process inventory.
The completed macOS v5 sweep similarly retains its earlier immutable binary;
neither completed broad report supplies latest-code whole-game approval.

## Remaining required work

1. Finish and review the authored HD/outpainted artwork across the background
   inventory. The four-view gallery installation is a technical fixture, with
   documented geometry/sharpness issues; it is not the finished production pack.
   Preserve camera registration, masks, effects, dynamic layers and lettering.
2. Finish logical-view/source classification, including the 32 excluded rows,
   remaining source diagnostics. MIST R21 is classified as an empty debug room
   with no static views; plaza uses streamed scenes. Finish distinguishing
   other unused/special assets from views needing authored routes or playback.
3. Complete remaining runtime coverage across both discs: natural movement,
   view and room transitions, menus/dialogue, title, captured-room screens,
   special panorama/scroll renderers and RGB24 movies. Of the full survey's 606
   generic no-target locations, gallery view 14 now has a supplemental verified
   menu route; the other 605 remain unverified. Context-gated transient checkpoints supplement rather than
   satisfy the settled survey's field-ready/30-frame gate.
4. Finish the consumable production-pack/distribution workflow and outstanding
   download/update decision. Local activation, manifest verification and
   immutable version installation already work. Material/reference separation,
   verified archive transfer and hydration now work, but automatic live/disc
   owner-reference acquisition and reviewed
   material distribution remain open. Caches and runtime packs include native
   reference pixels and must stay private. Keep native game pixels out of public
   distribution and keep PsyZ unmodified.
5. Verify supported platforms and GPU backends with appropriate builds and
   runtime evidence. Metal, Linux ARM64/lavapipe and Windows-PE-under-Wine
   subsets do not establish native Windows/D3D12, Linux x86-64, physical GPU
   or all-game behavior.
6. Prove complete off/baseline parity and wide coverage without holes, retaining
   native state and masks. Extend missing-art returns, pack toggles, malformed
   input and foreground occlusion checks across required scenes and player poses.
   Do not promote candidate artwork from a passing integration test alone.

Work remains on local main. No push is authorized by the handoff. Do not commit
hd_handoff.md, modify PsyZ, or publish local source captures.


## Native-background widescreen (2026-11-10)

Normal builds expose Graphics → Aspect ratio → Widescreen (16:9), persist the
choice and accept `--widescreen 0|1`. `--no-hd-backgrounds` is also available
without enabling experimental pack support. Selecting wide preserves an existing
2x–8x detail setting and raises 1x to 2x; returning to classic permits 1x again.
Software rendering and unsupported/native-feedback frames use classic 4:3.

The port re-presents the original MDEC image in the display plane, with black
side cells rather than invented scenery. Models may occupy the wider view.
The native-reference plane preserves opaque RGB555 zero. A temporary atlas file
is removed after upload, including after a partial save failure. Only one live
background atlas is retained. The local PsyZ checkout adds an opt-in
`Psyz_VideoSetWidescreenDisplayOnly` switch: native rasterization retains original
positions, clipping and blend-copy bounds while the display target is widened.
Existing clients keep its default behavior. Startup configuration does not
initialize the GPU/window, and the scale getter exposes pending settings before
platform initialization, so selecting wide cannot silently reduce saved 4x.

Completed retail UI frames have a separate diagnostic:
`PE2_CAPTURE_UI_FRAME=20 PE2_CAPTURE_OUTPUT=/private/output`. It counts completed
buffers while `uiOpen` is true and writes `pe2-ui-frame-checkpoint-v1`, with a
separate `ui_frame` count and no field background identity. It cannot satisfy a
settled static-view gate. Pad-poll screenshots alone may still show the earlier
world image during a menu handoff. The read-only replay checkpoint tool also
supports `--backend gpu` and reports display ownership and menu-hold state.

Reproducible scoped checks with local USA disc images:

```sh
venv/bin/python tools/hd/check_native_widescreen.py \
  --disc rom/image/SLUS-01042.cue --disc2 rom/image/SLUS-01055.cue
venv/bin/python tools/hd/check_native_ui.py \
  --disc rom/image/SLUS-01042.cue --disc2 rom/image/SLUS-01055.cue
```

macOS/Metal evidence is private under `build-hd-study/widescreen-20261110/`:

- `final-static`: gallery view 8 and fire-escape view 6 produce 1707x960 versus
  1280x960, with equal whole native VRAM, simulation state and model poses.
- `ui-verified`: completed inventory and map buffers stay 1280x960, with exact
  classic/wide pixel, whole-VRAM and simulation equality. Both were visually
  reviewed. These supersede the earlier menu-timed attempts.
- `final-transitions`: controller travel from gallery view 10 to 11/12
  preserves GPU inputs, VRAM and simulation; early/final wide frames stay 16:9.
- `final-feedback`: MIST R18 checkpoints 1292/1304/1310 retain classic
  presentation during native feedback; 1340 resumes wide. All four retain
  identical native VRAM and GPU inputs.
- `final-presenter` and `scales`: synthetic native/HD controls, both buffers,
  reloads, malformed packs, safe low-level 1x fallback and scales 2–8 pass.
- `final-software`: gallery view 8 retains exact classic/wide pixels and VRAM
  under the unsupported software renderer.

The 53 C host tests and normal-build settings painter checks pass. These are
scoped controls for the native widescreen implementation, not whole-game or
physical Windows/Linux GPU certification. Production extended artwork and the
broader historical HD distribution/artwork work remain separate unfinished work.
