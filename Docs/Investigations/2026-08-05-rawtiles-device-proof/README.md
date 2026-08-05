# 2026-08-05 — rawtiles device proof (one tile on the physical watch)

Branch: `spike/rawtiles-device-proof` (off `upstream/main` @ `59210889`). Never meant to
merge — the deliverable is this bundle's numbers, which shape the streaming Container API
(`RAWTILES_MAP_EVALUATION.md` §5, step 1).

The probe rides HelloWorld (same play as `bug/drawpartialbitmap-y-clipping-repro`): the
GUI process locates `stanley.rawtiles` through `SDK::Interface::IFileSystem`, reads the
292-byte header + one index entry, then seek+reads the first zoom_max tile (64 KiB,
z14 x2587 y5604) into a static buffer and blits it with the pre-clipped `blitCopy`
discipline. Every stage is timed with `ISystem::getTimeMs` and drawn on screen, so a
photo of the watch is the evidence. All stages also go to LOG_INFO.

## Hypotheses

| # | Hypothesis | Retired by | Verdict |
|---|---|---|---|
| H1 | The GUI process can open and read files at all on hardware (a `SDK::Kernel` with a live `fs` reaches the GUI via `KernelProviderGUI`) | probe reaching the `open` stage | **CONFIRMED** |
| H2 | A 64 KiB single `read()` off watch storage costs single-digit-to-low-tens of ms — cheap enough for read-on-pan | the `64K cold/warm` numbers on screen | **CONFIRMED** — cold 7–9 ms, warm 6–9 ms across 7 runs |
| H3 | `LCD::blitCopy` renders an ABGR2222 buffer correctly on the hardware draw path, including partial-overhang draws (its sibling `drawPartialBitmap` has two defects, the Y-overhang one reproduced on STM32U595) | tile visible and correct at all three L1 positions, incl. the (60,220) repro geometry | **CONFIRMED** — all three positions render cleanly, no red-artifact at the (60,220) Y-overhang geometry that broke `drawPartialBitmap` |
| H4 | A 64 KiB tile buffer in GUI `.bss` fits the app RAM budget | GUI ELF links; app boots | **CONFIRMED** (link: see `arm-build.log`) |
| H5 | Which volumes exist and are USB-reachable — does the mass-storage drive map to `2:/`, and is `1:/` populatable at all today | the `2:/maps … 2:/Apps` Y/n line on screen | **ANSWERED, differently than planned** — none of the four absolute `N:/...` volume paths ever resolved on hardware; see Results |

## Simulator validation (done 2026-08-05 — code paths green, mock FS)

Built in amd64 `gcc:13` docker, run headless under Xvfb (`sim/sim.log`,
`sim/spike-*.png`):
- Probe end-to-end: pack found at `2:/maps/` (mock FS maps volume prefixes as plain
  directories), `open 1ms`, `header ok: z12..14, 12 tiles`, first z14 tile
  `x2587 y5604 at 393748`, `64K cold 2ms warm 0ms`, **`crc 0x26E5979B (stanley OK)`**
  — byte-exact against the host-computed value. R1 re-run produced a second identical
  pass (`run 2` in the log).
- Render: `sim/spike-04-no-overlay.png` shows the Lions Gate Bridge tile drawn as
  exactly the 136×136 bottom-right quarter in the top-left of the screen — position 2,
  the negative-X/-Y overhang that `drawPartialBitmap` silently drops — rendered
  correctly by the pre-clipped blitCopy path. Stats overlay renders all six lines
  (`sim/spike-03-pos2.png`).
- Quirk, expected: the emulated sim ticks well below 60 Hz, so the tick-60 probe fired
  ~10 s after boot there (`spike-01/02` still show the placeholder). On hardware ticks
  are real-time and the probe fires ~1 s after the screen appears.
- ARM build: `arm-build.log` — `HelloWorld_0.0.0-dev.uapp` (231,740 B) in
  `una-armgcc:latest`; GUI ELF 217 KB text / 137 KB bss including the 64 KiB tile
  buffer — well inside the 600 K GUI RAM_LENGTH. H4's link-level half holds.

## Watch run-book

1. Build (or take the prebuilt) `HelloWorld_*.uapp` from this branch:
   `docker run --rm --platform linux/amd64 -v <tree>:/src -e UNA_SDK=/src una-armgcc:latest`
   → `cd Docs/Tutorials/HelloWorld/Software/Apps/HelloWorld-CMake && mkdir build && cd build && cmake .. && make`.
2. Connect the watch as USB mass storage (`Docs/deploy.md`).
3. Copy `HelloWorld_*.uapp` into `<Drive>:\Apps\HelloWorld\` (replace the stock one if
   present). Copy by drag in Finder/Explorer, not scripted copy —
   `Update-Watch-Apps.ps1:25` warns scripted copies have produced silent corruption.
4. Copy `stanley.rawtiles` (787,008 bytes, from
   `feat/rawtilesmap-tutorial:Docs/Tutorials/RawTilesMap/Resources/`) to BOTH:
   - `<Drive>:\maps\stanley.rawtiles` (create the folder), and
   - `<Drive>:\Apps\HelloWorld\stanley.rawtiles`
   The probe reports which candidate paths exist — that mapping is H5's answer. If the
   USB drive turns out to be `2:/`, the two copies light up candidates 0 and 3.
5. Eject cleanly, launch HelloWorld on the watch. ~1 s after the screen appears the
   probe runs and the map tile + stats overlay replace the tutorial content.
6. Photograph: (a) the boot screen with stats, (b) after one L1 press (tile at the
   (60,220) Y-overhang repro position), (c) after a second L1 press (negative-X/Y
   position), (d) after R1 (re-run; warm timings, `run 2`).
7. Buttons: L1 cycle tile position · L2 hide/show stats · R1 re-run probe · R2 exit.

## Reading the screen

```
run 1  2:/maps/stanley.rawtiles     <- winning path (H5)
scan 4ms open 3ms                   <- exist() x4, file()+open() (H1)
z12-14 n=12  787008 B               <- header parsed
tile z14 x2587 y5604                <- first zoom_max index entry
64K cold 12ms warm 8ms              <- THE numbers (H2)
crc 26E5979B OK                     <- tile bytes intact end-to-end
```
`crc … OK` compares against the host-computed CRC-32 of that tile
(`0x26E5979B`); `??` with a rendered-looking map means a different pack, garbage plus
`??` means a read-integrity problem. `FAIL @ <stage>` lines show the per-candidate
exist flags — all-`n` means no candidate path is visible to the app (H5 answered in
the negative for those volumes).

## Failure modes worth recording (failures included, per convention)

- App boots but stats stay "probe in ~1s..." forever → the probe hung inside a
  filesystem call; note which log line was last.
- `FAIL @ open` with an exist hit → volume readable by `exist()` but not `file()`.
- Tile renders wrong (stretched/shifted/missing) at position 1 or 2 but correct at 0
  → hardware blit path misbehaves on overhang; photograph and compare against the
  drawPartialBitmap Y-overhang report (`docs/touchgfx-drawpartialbitmap-y-clipping-bug-report`).
- Reboot/watchdog during probe → the synchronous read stalled the GUI thread longer
  than the platform tolerates; that alone reshapes the Container API toward chunked
  reads.

## Results

Four real problems surfaced between "sim green" and a clean device run, none of them the
Container API questions this spike was actually built to answer. Recorded here because the
run-book above still describes the *original* plan, not what actually worked.

**1. Kernel interface version mismatch (blocked launch entirely).** `main` bumped
`KERNEL_INTERFACE_VERSION` 2→3 on 2026-07-31 (`68676e7c`, for home-widget IPC this app
doesn't use) — three days after this watch's firmware was last confirmed at v2. Built against
`main` after that commit, both `HelloWorld.SRV` and `HelloWorld.GUI` hit `una_init_kernel`'s
version gate and exited immediately (`Kernel not supported. Minimum 3, got 2`), surfaced on
the watch as an `App PID` fault screen with nothing else logged. Fixed locally (this branch
only — never meant to merge) by pinning `KERNEL_INTERFACE_VERSION` back to `2` in
`Libs/Header/SDK/Interfaces/IKernel.hpp`.

**2. Stats overlay clipped by the round bezel.** The overlay was top-anchored (`y=4`,
`width=228` in a 240×240 square framebuffer), which put its upper lines where the physical
round panel's visible chord is narrowest — confirmed on-device: `run 1 FAIL @ exist` read as
`-AIL @ exist`, losing ~7 leading characters (`device/01-fail-clipped-layout.png`). Fixed by
recentering the block vertically on the panel's center (`y=36..204` for the backdrop) and
insetting it horizontally (`x=20`, `width=200`) — confirmed clean afterward
(`device/02-success-run1-recentered.png`).

**3. H5's real answer: none of the four absolute volume-prefixed candidates ever worked.**
`2:/maps/...`, `1:/maps/...`, `0:/maps/...`, `2:/Apps/HelloWorld/...` all reported
`exist() == 0` on every run — the `2:` mapping was only ever confirmed in the sim's mock FS,
never on hardware. `watch-apps/Barcode`'s `InputConfig.hpp` had already solved this: a bare
relative path (`stanley.rawtiles`, no volume prefix) resolves against the app's own sandbox
folder the same way for the USB volume and the BLE file-transfer service both. Added as a
5th candidate, confirmed as the only one that resolves, then the probe was simplified to just
that one candidate — the four dead absolute paths were deleted from `RawTilesProbe.cpp`
rather than left as leverage.

**4. USB-MSC writes and the watch's own BLE sync collided on the same exFAT partition.**
Mid-session, `stanley.rawtiles` was found corrupted at two different byte offsets in its two
on-device copies (787,008 B and mtime both unchanged — same size, same timestamp, different
content), then a redeployed `.uapp` became unreadable (`Input/output error`, persistent on
retry, isolated to that one file — new filenames and other paths wrote fine), then the mount
itself died (`Transport endpoint is not connected`). Root cause: the watch was actively
syncing over Bluetooth with a phone while the same partition was mounted via USB-MSC on the
host — two independent writers on one filesystem with no coordination. Turning off BLE sync
resolved it immediately; a clean unmount/remount recovered the dead mount, and a fresh copy
verified byte-identical across a genuine eject→remount→read cycle (not just page cache).
**Takeaway for the real Container API and for anyone re-running this bundle: don't have the
watch actively BLE-syncing while pushing files over USB-MSC.**

**Final clean run** (`device/02-success-run1-recentered.png`, UART trace in this bundle):

```
run 1  stanley.rawtiles
scan 4ms open 1ms
z12-14 n=12  787008 B
tile z14 x2587 y5604
64K cold 7ms warm 6ms
crc 26E5979B OK
```

Repeated across 7 R1 reruns, cold/warm consistently 6–9 ms, CRC matching every time. All
three L1 positions photographed and correct:
- `device/03-pos0-near-full.png` — position 0 `(-8,-8)`, near-full tile.
- `device/04-pos1-y-overhang-repro.png` — position 1 `(60,220)`, the exact geometry that
  produced a Y-clipping defect on `drawPartialBitmap`; blitCopy renders it clean, no
  red-artifact.
- `device/05-pos2-neg-xy-overhang.png` — position 2 `(-120,-120)`, matches the sim's
  136×136 bottom-right-quadrant-in-top-left-corner result exactly.

H1–H4 confirmed as planned. H5 answered in a way that changes the Container API
recommendation: design around sandbox-relative paths, not absolute volume letters — the
volume-letter scheme this spike started with never worked on real hardware at all.
