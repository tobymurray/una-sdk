# H6 — rebasing the device-proof probe onto the real Container API

Follow-up session, 2026-08-05 (later). Not part of `feat/rawtiles-container`'s diff — this
ran on a throwaway branch, `spike/rawtiles-device-proof-container-api`, off
`origin/spike/rawtiles-device-proof`, never pushed anywhere. Its only job was to answer:
*does the real Container API, substituted for the spike's hand-rolled parsing, still
produce the same tile on the same hardware?* — before this PR merges, not after.

## Why this exists

`spike/rawtiles-device-proof`'s `RawTilesProbe` never called `SDK::RawTiles::Container` —
there was no Container yet. It hand-parsed the header/index directly against
`SDK::Interface::IFileSystem`, and its numbers (7-9 ms cold tile reads, ~113 ms first-touch,
`blitCopy` overhang correctness) are exactly what shaped this Container's API. Proving the
finished Container reproduces those numbers, on the same watch, closes that loop instead of
leaving it as an assumption.

## Method

1. Copied `Libs/Header|Source/RawTiles/Container.{hpp,cpp}` from `feat/rawtiles-container`
   onto the throwaway branch unchanged.
2. Rewrote `RawTilesProbe::run()` (only the `.cpp`; the `.hpp`'s `Result` struct, and every
   other file — `TileBlitProbe`, `Model`, `MainPresenter`, `MainView` — untouched) to call
   `openFromFile()` / `tileCountAtZoom()` + `tileAtIndex()` (to find "first tile at
   zoom_max" the same way the original's zoom-directory-offset read did, through the public
   API instead of hand-parsing the directory) / `readTile()`, instead of hand-rolled
   `fread`-and-interpret logic. Kept the probe's own tile-bytes CRC-32 helper (unrelated to
   Container's internal footer CRC) so the on-screen OK/BAD readout is unchanged.
3. Added `Container.cpp` to the Linux simulator's `simulator/gcc/Makefile`
   (`ADDITIONAL_SOURCES_UNA`) and to `HelloWorld-CMake/CMakeLists.txt`'s `GUI_SOURCES`
   (ARM build) — same one-line addition either way (no `cmake/una-sdk.cmake` on this old
   branch to hang a source-list entry off of, so it's a direct path here rather than via
   `UNA_SDK_SOURCES_RAWTILES`).
4. Built + ran natively in the Linux simulator (`Docs/Simulator.md`'s Linux recipe — no
   docker needed for this step, SDL2/jpeg/ruby were already on the host machine).
5. Built the ARM `.uapp` in `xanderhendriks/stm32cubeide:16.0` (same recipe as the main
   bundle's `arm-build.log`), keeping the branch's existing `KERNEL_INTERFACE_VERSION = 2`
   pin (this watch's firmware; unrelated to this Container, spike-only, not to be carried
   anywhere near a merge).
6. **Before touching the watch**: confirmed BLE sync was off (the spike's README documents
   USB-MSC-vs-BLE-sync as the actual root cause of prior silent corruption on this exact
   device — asked rather than assumed). Mounted the watch's `exfat` partition via
   `udisksctl` (no sudo needed), inspected existing contents first rather than overwriting
   blind, found `stanley.rawtiles` already present and byte-identical (SHA-256 match) to
   the reference copy — left it untouched. Removed the old
   `HelloWorld_1.3.0-72-f0b47b6-dirty.uapp` (the original spike's own build, still on the
   watch) and copied in the new `.uapp`, verifying the copy was byte-for-byte identical via
   SHA-256 before unmounting. `sync` before every unmount; `udisksctl unmount`, not a bare
   `rm`/yank.

## Results

**Simulator** (`sim-run.log`; headless, `SDL_VIDEODRIVER=dummy`):
```
exist stanley.rawtiles -> 1
open stanley.rawtiles ok, 787008 bytes, 9ms
header ok: z12..14, 12 tiles, 9ms
first z14 tile: x2587 y5604 at 393748
tile 64K: cold 0ms, warm 0ms, crc 0x26E5979B (stanley OK)
```
(The log's tail — `pure virtual method called` / `terminate called` / `dumped core` — is
this run being killed by an 8-second `timeout` mid-shutdown, not a probe or Container bug;
the probe's own output above completed cleanly before that.)

**Physical watch** (`01-container-api-stanley-run1.png`, run 1, on-screen readout):
```
run 1  stanley.rawtiles
scan 113ms open 27ms
z12-14 n=12  787008 B
tile z14 x2587 y5604
64K cold 6ms warm 7ms
crc 26E5979B OK
```
(Transcribed from the photo; the round bezel clips a character or two off the right edge
of each line, per the known bezel-clipping finding — the digits present are unambiguous.)

**Render** (`02-container-api-stanley-render.png`, L2 pressed to hide the stats overlay):
the actual `blitCopy` output — Stanley Park / Lions Gate Bridge, with the "Ambleside Park",
"Squamish Nation", "Lions Gate Bridge", and "BC 99" labels all legible. This is the visual
half of the confirmation: the stats readout above proves the *bytes* `Container::readTile()`
returned are correct (CRC match); this photo proves the *pixels* — that `TileBlitProbe`'s
unmodified `blitCopy` path renders that data correctly on this hardware, same as the
original spike established for the hand-rolled reader's bytes.

## Verdict

**CONFIRMED.** Every structural fact matches the original hand-rolled probe's documented
values exactly: file size (787,008 B), zoom range and tile count (z12-14, n=12), the tile
identity (z14, x2587, y5604), and — the one that actually matters — the tile-bytes CRC
(`0x26E5979B`, flagged `OK` on-device). That CRC is computed from bytes `Container::readTile()`
returned on the real watch; matching it means the new streaming Container is feeding
`blitCopy` byte-identical pixel data to what the spike's hand-rolled reader did, through a
completely different code path — confirmed visually too, not just by CRC: the render
photo shows a correct, legible Stanley Park / Lions Gate Bridge tile. Timings are in the
same envelope as the original
(cold/warm reads single-digit-to-low-tens ms; `scan` reproduced the addendum's ~113 ms
first-touch cost almost exactly) — no regression, and no reason to believe the new API's
extra validation work costs anything that matters at this pack size.

**Known, expected non-equivalence**: `open`/`scan` timing isn't apples-to-apples against
the original probe's separate "open the handle" stage. `Container::openFromFile()` folds
what used to be two stages (open a handle; separately parse+trust the 292-byte header) into
one eager pass that also validates the entire tile index, every extension section, and the
footer CRC — none of which the original probe checked at all. A slower, more thorough
number here is the expected cost of a real validating reader replacing a probe that trusted
its input, not a performance regression to chase.
