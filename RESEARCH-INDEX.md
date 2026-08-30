# Research index

A read-only reference branch: the investigations, experiments, protocol specs and design
notes from the `tobymurray/una-sdk` fork, gathered in one place instead of scattered across
branches that get retired. It carries documentation and evidence only, so
`git diff upstream/main...research` is purely additive and nothing here is meant to merge.

Not upstream policy and not official SDK documentation. A contributor's working record.

**The two rules this branch is maintained by.** First, a claim lives here only if this branch
can keep it true. Anything owned elsewhere, such as whether a pull request is open, what a
branch currently holds, or what upstream now documents, gets named and pointed at, never
restated. If you find a restatement, it has probably already rotted; correct it or delete it.

Second, nothing here should be derivable from the SDK by reading it. An inventory of what
upstream contains is the form rot takes: it reads as useful, it is re-derivable at any moment
by anyone, and it goes wrong silently. What earns its place is what a checkout cannot tell you:
a measurement off real hardware, a wire format off real traffic, an answer from a maintainer,
a defect in a vendored binary, prior art from outside this project.

Rebase onto `upstream/main` regularly; the diff is documentation, so it should not conflict.
`git replay --onto upstream/main $(git merge-base research upstream/main)..research` does it
without a working tree, which is useful when a checkout of this branch is inconvenient.

---

## 1. BLE protocol and hardware recovery

`Docs/Investigations/2026-07-29-hardware-config-recovery/`

Characterising the silicon inside a physically owned watch, dumping its firmware, and
recovering its BLE sync protocol, to make a phone-free companion possible.

**Read `BLE-COMPANION-protocol-spec.md` § 0 first.** UNA has since published
`Docs/BLE-File-Transfer-Service.md` and `Docs/BLE-Services-Overview.md` on `main`, and those
are authoritative for FTS and the standard services. This spec is reduced to corroboration
plus what they do not cover: the **CCS** custom command service, **CANS** notifications, and
the on-watch file layout. Two discrepancies against the published document are recorded in
§ 0.3 and neither has been addressed there: `modificationTime` decodes as microseconds where
the spec says nanoseconds, and the documented Nordic UART service is absent from this watch's
GATT table.

| File | What it is |
|---|---|
| `README.md` | The verification ledger. Every claim tagged CONFIRMED / LIKELY / UNVERIFIED / REFUTED with the method that got it there, plus per-sweep round data. |
| `REPRODUCTION-GUIDE.md` | The technique, command by command: how to reproduce the hardware-config read from nothing. Narrative counterpart to the ledger, and a genuinely different document, not an older draft of it. |
| `BLE-COMPANION-protocol-spec.md` | The protocol recovery. § 3.2 is the finding with the widest blast radius for any consumer: **the watch reports a fabricated heart rate while off-wrist.** § 3c has the CANS wire format, decompiled from the vendor app. |
| `CANS-LIVE-PROBE-2026-08-21.md` | Firmware 1.4.0, phone-free. FTS protocol 5 confirmed and its windowing measured. The CCS event channel decoded: `0x01 0x00 <appId u64LE>` announces a saved activity, checked one-for-one against the files it produced. **And the answer to silent notifications: `phone.notifications` in `/settings.json` gates the whole of CANS, and a watch with it false discards every event without a reply of any kind.** Capability statements in the spec above describe 1.3.0; this describes 1.4.0. |
| `prototype/` | Working, validated Linux/BlueZ clients, no phone involved. `una_ble_client.py` pulls `.fit` files off the watch with matching CRC-16, which is the proof the spec is right. `una_gatt_dump.py` reads BlueZ's resolved cache, so it dumps the GATT table without putting anything on the air. `una_fts_walk.py`, `una_read_probe.py`, `una_hr_probe.py`, `una_cans_*`, `una_ccs_event_probe.py`, `una_v5_probe.py` each recovered a named section of the spec. `transcripts/` holds the frames the numbers came from. |
| `SEAM-HUNT-disassembly-prompt.md`, `NEXT-SESSION-disassembly-prompt.md` | Two self-contained handoffs, both still live work: find injectable seams in the closed kernel for per-peripheral Rust replacement, and disassemble the rest of the kernel dump. The BLE companion handoff that sat beside them is deleted, its five deliverables all met, four of them twice over now that upstream publishes the same protocol. |
| `../2026-08-07-ble-write-path/` | The **write** half, measured on 1.3.0: the `WRITE`/`WRITE_PACING`/`WRITE_DATA` framing exercised for real, a chunk sweep, the HCI connection interval, and the failure modes that matter to a companion, including out-of-order and partial writes. It is also where the read-path `real_chunklen` bug was found and filed as #272. |
| `../2026-08-18-fts-read-chunklen-fix/` | #272 confirmed fixed on 1.4.0 against real hardware, with the boundary sweep and a 177 kB CRC-checked read behind it. Roughly doubles usable read throughput, 2.1 kB/s to 4.2 kB/s. This is the evidence the CANS probe's claim rests on. |
| `GADGETBRIDGE-scoping.md` | What supporting this watch in Gadgetbridge would actually take. |
| `reassemble_dump.py`, `service-cpp-instrumentation-sweep7.cpp` | The chunked flash-dump reassembler, and the instrumented `Service.cpp` that was the read primitive. |

Established, with the ledger holding the confidence tag on each: the MCU is an **STM32U5A5**,
agreed by three independent methods; apps run with **no isolation at all**, MPU disabled, CPU
privileged and TrustZone off; the full 4 MB flash is dumped and CRC-verified two ways, the real
image ending at `0x0820A140`; a dual vector table places a bootloader at `0x08000000` and the
kernel at `0x08060000`; the IMU is a **BMI270** by exact CHIP_ID match.

## 2. Backlight control from an app

`Docs/Investigations/2026-08-26-backlight-control/`

**Answered and demonstrated on hardware.** The kernel drives the front-light as a plain on/off
enable, so `RequestBacklightSet::brightness` is inert; the hardware dims perfectly well, and an
ordinary `.uapp` can dim it. The gap is firmware, not silicon.

The evidence is a register diff across six brightness levels showing `GPIOF ODR` bit 3
byte-identical at every one, which also excludes software PWM: six lit sweeps all read the pin
low, and at brightness 1 that is a one-in-a-hundred coincidence. `PF3` has no timer output, so
the working route is DMA-driven, and it costs no thread. The raw sweeps are in `pwm-run/` and
`dma-run/`.

An earlier revision of this file concluded the opposite, that the hardware could not dim. The
correction is kept in place because the error is instructive: it read a firmware configuration
as a physical constraint.

Four SDK defects fall out of it, each independent of the hardware answer: the inert documented
`brightness` field; `Mock::Backlight::on(0)` blanking after ~50 ms where the device holds the
light indefinitely; four `Docs/` pages asserting brightness control works, one of them also
naming the wrong PMIC; and `IID_COUNT` evaluating to `0x000B0001` rather than a count. The
fixes are on their own branches on the fork.

## 3. TouchGFX `drawPartialBitmap`

Two investigations of the same framework defect, found from different directions and kept
apart for that reason.

`Docs/Investigations/2026-05-16-touchgfx-drawpartialbitmap-negative-x/` covers how to test whether
*your* platform is affected, plus `DynamicBitmapDraw.hpp`, the workaround itself, active only
under `SIMULATOR && __linux__`. It belongs at `Libs/Header/SDK/GUI/` if you adopt it.

`Docs/Tutorials/RawTilesMap/Investigations/2026-05-16-cell-render-bug/` is **resolved.** The
full experimental record across experiments A, B, C and C2.
`LCD8bpp_ABGR2222::drawPartialBitmap` in the prebuilt Linux `libtouchgfx.a` silently drops
external dynamic bitmaps at a negative target X, and stretches source across widget width at a
positive one. The fix is `LCD::blitCopy` with a source-relative `blitRect`, verified in
`experiment-C2-blitCopy-corrected/`. The dead ends are left in deliberately: the "shared pixel
pointer count" theory was a red herring, and that is worth knowing before deriving it again.
The tile fixture these runs load is on `feat/rawtilesmap-tutorial`, at
`Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles`.

`Docs/touchgfx-drawpartialbitmap-y-clipping-bug-report.md` characterises the defect: the exact
trigger conditions, the non-trigger that tells you when you are looking at something else, and
a minimal reproduction. It was posted to the ST Community and the file links that thread, which
is where a reply would arrive. `bug/drawpartialbitmap-y-clipping-repro` holds the running
reproduction.

## 4. Map rendering on the watch

`Docs/Investigations/2026-08-05-rawtiles-device-proof/` draws one tile on the physical watch.
A 64 KiB read off watch storage costs **7 to 9 ms cold**, which makes read-on-pan affordable;
`blitCopy` renders overhanging ABGR2222 buffers correctly on hardware where
`drawPartialBitmap` does not; and **no absolute `N:/` volume path resolves on the device**, so
app file access is sandbox-relative. The watch photographs and the probe code stay on
`spike/rawtiles-device-proof`.

`Docs/Investigations/2026-08-05-rawtiles-map-evaluation/` is the evaluation the device proof was
built to feed, `EVALUATION.md` with its evidence bundle: a conformance matrix against the
reference reader, a fuzz run, the arithmetic behind the size numbers, and audits of the spec, the
SDK and the ecosystem.

`Docs/Investigations/2026-08-07-athensrun-map-verification/` is what happened when an offline map
met real hardware. `Container::openFromFile()` did a mandatory synchronous whole-file CRC-32
before returning, which is spec-conformant eager verify and also a ten-second GUI freeze on a
45 MB pack, then a watchdog crash. The fix, the on-device validation, a legibility problem
resolved empirically, and a tile-sourcing compliance finding are all here, with the crash dump.
The 1.2 MB verify log stays on `archive/poc/athensrun`.

`Docs/Investigations/2026-08-12-map-e2e-run/` is the whole workflow run end to end, and the part
worth keeping is photographic: the reflective panel shot at z12 through z16, stock style against
watch style, and style v3 against v4, so the colorimetry in slippypack can be checked against
what the glass actually does. `2026-08-06-rawtiles-spec-adequacy/` sits behind it with
`ADEQUACY.md` and three encodings of the same pack, uncompressed against RGB565 against RLE, and
`2026-08-06-athens-pack/` records how the basemap was built.

`Docs/Research/2026-08-13-watch-cartography-prior-art.md` covers what the cartographic, human-factors
and standards literature already settles about map design for small, colour-limited, round,
reflective displays, so that measuring effort goes where it is actually needed. Every source is
marked by read depth, from *full text* down to *not retrieved*, and any figure taken from a
document that could not be fetched is flagged as unverified against its primary source.

## 5. Can the watch produce HRV?

`Docs/Investigations/2026-06-15-heart-beat-vs-ppg/`

**Read this before designing anything HRV-shaped**, and read it in preference to the SDK,
because upstream `Docs/SensorsLayer.md` still lists `HEART_BEAT 0x40` as "Beat peak event" and
that is the one thing it does not do. This is UNA's own authoritative answer, given in PR #167:

- `HEART_BEAT` (0x40) emits **no events**. HR detection is frequency-domain, not per-beat, so
  RR intervals cannot be read off beat timestamps.
- The PPG waveform is **20 Hz, single channel**, which UNA called "the low end for HRV
  extraction". A higher-rate mode and on-chip HRV are both being explored, so expect these
  numbers to move.
- **Optical HRV will only ever work at rest.** Mid-exercise "can't be done optically, it has
  to be an electrical measurement." Physics, not a roadmap gap.

Independently corroborated since, on the 1.4 line and without asking anyone: SleepLab's ledger
rows S4 and S5 (in `tobymurray/watch-apps`) found that `HEART_BEAT` does not resolve a driver at
all, so `connect()` returns false. It is not that the type emits nothing; there is nothing there
to subscribe to. `SPO2` behaves the same way, which retires it as a foundation for anything.

That last point is why a chest strap is structurally necessary rather than merely convenient.
The strap route is PR #220, `feat/rr-interval-contract`, whose description on GitHub is the
live rationale. One hazard worth carrying: in the Bluetooth Heart Rate Service `0x2A37`
notification the RR array is the last field and its offset moves, because flags bit 3 inserts a
two-byte energy-expended field ahead of it. Misread values still look like plausible intervals.

`Docs/Investigations/2026-08-04-rr-interval-contract-review/` is the adversarial review of that
PR's contract, and the reason to read it is the method: three experiments against a mutated
header and a saturated queue, two of which changed the review's conclusion and one of which
refuted something the reviewer had already written down as fact. `REVIEW.md` beside it is the
full review.

`BeatProbe.hpp` and its usage guide and integration patch are kept here, still runnable: the
answer has a "not today" shape, so re-run the probe once the firmware moves.

## 6. Design notes

`Docs/units-and-display.md` is why `SDK::Units` exists: measurements stay SI internally, and
metric/imperial is applied exactly once, at the moment a value becomes text. Includes the
measured flash cost and why the drawing functions are `SDK_GUI_NO_INLINE`. The code is the
`feat/sdk-units-core` → `feat/sdk-units-touchgfx` → `refactor/running-adopt-units` →
`perf/unit-label-repaint-skip` stack.

`Docs/companion-data-channel-analysis.md` asked for a way to get data onto the watch from off
it, and `SDK::AppConfig` (`Docs/app-config-fields.md`) has since answered most of it. What is
left is the three residues, which upstream's own § 1.1 confirms are constraints rather than
gaps: nothing reaches a *running* app, nothing may be secret, and `SettingsSerializer` is still
copied per app. Plus the reason the phone has to be the transport at all, and how Garmin
Connect IQ and Apple Wallet each landed on the same shape from the same constraints. The SDK
inventory that made up most of the original is deleted: it was derivable by reading the tree,
which is exactly why it rotted without anyone noticing, and it had come to recommend reusing
the `Sender` idiom that upstream has since retired.

## 7. Verification harness, and why it is not here

`proto-tests/` used to be copied onto this branch, and has been removed. It compiles against the
`Libs/Header` of whatever tree it sits in, so on a branch that is upstream plus documentation it
reports a wall of failures that says nothing about the design it exists to check. It is carried,
identically, by both branches that hold the logger redesign, and passes on them:

```sh
git checkout rfc/logger-if-constexpr-lifetime && zsh proto-tests/run.sh
```

Needs only clang++ with C++17: no TouchGFX tree, no SDL2, no network. Each check is a property
the old `#if`-based design could not hold at the same time as the others, the sharpest being that
the logger's own implementation has to build at `LOG_LEVEL=0`.

## 8. Linux simulator

`Docs/Simulator-Linux.md` is gone. Upstream `Docs/Simulator.md` § "Linux (GCC)" is the
instruction set now, and everything that page carried is either in it or made obsolete by it.
Two things are worth knowing that it does not spell out. Arch's package names for the
Debian list it gives are `base-devel ruby sdl2 sdl2_image libjpeg-turbo`, plus the `nokogiri`
gem; `roo` and `rubyXL` are not needed, being reachable only through the `.xlsx` text parser,
and every app here keeps its strings in `texts.xml`. And guarding the message `static_assert`s
to 32-bit builds, which that page does describe, means they do not run on an x86-64 host: a
pointer-carrying struct really is larger under the simulator, so a `sizeof` compared against
the firmware's will not match.

`Docs/assets/screenshots/` shows the simulator running on Linux across HelloWorld, Sensors,
Buttons, ScrollMenu, Files and GpsTrack, plus a FIT track rendered on a map. These existed only
on branches that have been retired. Upstream documents the headless run
(`SDL_VIDEODRIVER=dummy`) but not how to capture a window, which is how these and the
experiment bundles' screenshots were taken:

```bash
DISPLAY=:0.0 ./build/bin/simulator.out &
sleep 1
WID=$(DISPLAY=:0.0 xdotool search --name "<AppName>" | head -1)
# if the app folder name does not match the window title:
# WID=$(DISPLAY=:0.0 xdotool search --pid $(pgrep -f simulator.out) | head -1)
DISPLAY=:0.0 import -window "$WID" /tmp/sim.png
```

Needs `xdotool` and ImageMagick. `xdotool getwindowgeometry "$WID"` reports the window size,
which comes from the app's own `SimConstants.hpp`.

---

## What is deliberately not here

**Live code.** Documents were copied here, not moved, so the branch carrying an implementation
still carries its own copy. This branch is for reading, not for building or merging from.

**Anything a pull request or another branch owns.** Branches attached to a PR were not touched.
The one exception was `feat/beat-event-probe`, a declined PR that existed to ask a question;
both the probe and the answer are in § 5 above and GitHub keeps the discussion regardless.

**Feature documentation that ships with unmerged work**, such as tutorial `ARCHITECTURE.md`
files and the GpsLab README, stays with the branch it documents.

## Research that lives in the other repositories

This branch is not the whole record and should stop pretending to be. Three sibling
repositories carry hardware findings about this watch that no checkout of the SDK could give
you, and several of them already cite this branch's ledger convention while nothing here
pointed back. All three are readable on GitHub under the same owner. Named rather than copied:
they are maintained where they are, and a second copy would drift the way everything else in
this file's history did.

### `tobymurray/watch-apps`

Instrument apps, and the measurements they produced. The app documentation stays with the app;
what is worth knowing from here is which question each one answers.

| Where | What it establishes |
|---|---|
| `SensorLab/Profiles/1.4.0-2026-08-28/REPORT.md` | A measured sensor profile of firmware 1.4.0, claim by claim with the method and sample count behind each figure. Six sensor types are declared in `SensorTypes.hpp` and absent from upstream `Docs/SensorsLayer.md` entirely; five ship no parser at all, so the frame descriptions there are the only ones that exist anywhere; and twenty-eight of twenty-nine shipped parsers test the delivered field count for *exact* equality, so one appended field would silently invalidate every sample they read. `HeartRateEx` is the sole exception and uses `>=` deliberately. It also finds a real defect: `GpsLocation::isDataValid()` reads a field before checking the field count, which is an out-of-bounds read on a short frame once the bounds assert is compiled out at `-Os`. |
| `SensorLab/Docs/LEDGER.md`, `FINDINGS.md` | The ledger behind that profile, and why the simulator cannot answer any of it: it resolves no sensor drivers for a service. |
| `SleepLab/Docs/FEASIBILITY-LEDGER.md` | Rows S4 and S5, measured on hardware 2026-08-18. `HEART_BEAT` (0x40) and `SPO2` (0xF1) do not resolve a driver at all, so `connect()` returns false and the question is not whether they emit but that there is nothing to subscribe to. That is § 5's answer reached independently, on the 1.4 line, without asking anyone. |
| `RustGuiPoc/Docs/FINDINGS.md` | The display platform, measured over five hardware runs: 240x240 8bpp ABGR2222 at four levels per channel, one app-owned 57,600-byte framebuffer, whole frames only because `RequestDisplayUpdate`'s rectangle fields are reserved, 600 KiB of GUI RAM that code also executes from, and software rendering only since the port's `STM32DMA` is a stub. |
| `MagProbe/README.md` | The one hardware-inventory row still unconfirmed. The magnetometer is `BMM350` on a firmware string alone, a `CHIP_ID` read did not match, and this is the app built to settle whether the watch can be a compass at all. Unrun as of writing. |
| `MapLab/Docs/Investigations/2026-08-19-*`, `MapLab/Docs/GATES.md` | Vector map rendering measured on the glass: 24.0 ms rural, 70.2 ms suburban, 160.5 ms city centre against a 100 ms budget, two independent passes agreeing to 0.3%. And the finding that kills a tempting line of work: decode and transform are 3.4% of a render, the rest is rasterising, so a better wire format cannot touch the budget that fails. |
| `BacklightProbe/Output/`, `BacklightPwm/DMA-NOTES.md` | The raw register sweeps behind § 2, and the DMA route notes. |

### `slippypack`

[github.com/tobymurray/slippypack](https://github.com/tobymurray/slippypack). Authored on a
Gitea at `nas:3000/toby/slippypack`, which is where pushes go; GitHub mirrors it and the two
were verified branch-for-branch identical, so the links here resolve for anyone.

`Docs/Investigations/2026-08-07-watch-cartography/README.md` on `main`. **The prior-art review
in § 4 above declares the hardware facts and this project's empirical findings out of scope and
does not say where they are. They are here.** The panel is a Sharp LS012B7DD06A and its
datasheet settles what had been guesswork: the display is reflective memory LCD, so ABGR2222 is
the panel's **native** format rather than a software compromise and quantising to 64 colours is
exact; the four levels per channel are spatial area modulation, so output is linear in
reflectance and **not** sRGB gamma; contrast is about 25:1, so there is no deep black; and panel
white reflects 8.4% against paper's 90%, with an NTSC ratio of 18%, so saturated colours land as
pastels. Eight experiments, two of the author's own hypotheses falsified, both kept.
`MAP_COMPLIANCE_APPENDIX.md` beside it carries the tile-source licence evaluation, which is what
decides whether a source may be packed offline at all.

Two more investigations sit alongside it. `2026-08-14-x4-browser-render` measures browser render
throughput, the experiment the delivery workflow had cut and then needed once the hosting
question was answered "static page, no hosting". `2026-08-16-pack-size`, on
`investigation/pack-size-and-vector`, measures what a region actually costs: 18,169 tiles over
z11 to z16 across roughly 50 km of rural Ontario builds to 65 MB. It contradicts three of the
format's own spec decisions on measurement, one of them by the renderer that already shipped,
and it is explicit that the spec is what moves when that happens.

### `rawtiles`

[github.com/tobymurray/rawtiles](https://github.com/tobymurray/rawtiles), same Gitea-to-GitHub
arrangement. The pack format's specification, currently **v0.6**, wire `(1,0)`, with a reference
reader and a conformance suite. Both investigations above read it at that version, and the
pack-size measurements are the standing argument for changing parts of it.

## Apps live there too, and one migration is unfinished

Apps were subtree-split out of this SDK into `tobymurray/watch-apps` and build against a released SDK
found through `$UNA_SDK`. Do not re-add one here.

**One migration is still incomplete.** `feat/gps-quality-logging` stays on this fork because it
is *ahead* of watch-apps' GpsLab rather than behind it:

- Two developer fields watch-apps does not have: `DF_HR_TRUST`, the kernel-arbitrated HR
  confidence, and `DF_BATTERY_MAH`, a coulomb-counted cumulative discharge integrated from
  `BATTERY_METRICS` average current rather than diffed from state-of-charge.
- `Tests/Host/apps/GpsLab/ActivityWriter_test.cpp`, still not carried over. A subtree split
  misses these, because app host tests live in the SDK tree under `Tests/Host/apps/` and not
  inside the app directory. Squash's were moved by hand; GpsLab's were not.
- SDK-side changes that belong to the SDK either way: `Libs/Header/SDK/Fit/FitProfile.hpp`,
  `Docs/FitFiles-Structure.md`, `Tests/Host/CMakeLists.txt`.

An app moving out does not take its SDK support with it: `feat/fit-profile-racket-squash`
(squash = sport 64 / sub_sport 94) has to stay here for the migrated Squash app to build.

## Recovering a retired branch

Every branch deleted or rewritten in the consolidation was tagged on `origin` first.

```sh
git ls-remote --tags origin 'refs/tags/archive/*'          # what was kept
git fetch origin 'refs/tags/archive/*:refs/tags/archive/*'
git branch experiments archive/experiments                  # bring one back
```

The tags are a safety net, not a permanent record. Drop them once you are satisfied.
