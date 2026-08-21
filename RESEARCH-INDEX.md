# Research index

This branch is a **read-only reference collection**. It is the consolidation of every
investigation, experiment, protocol spec, design note and handoff prompt that used to be
scattered across a dozen short-lived branches on the `tobymurray/una-sdk` fork.

It is built directly on top of `upstream/main` (`0f17b5c7`) and adds **documentation and
research artifacts only** — no SDK source is modified, so `git diff upstream/main..research`
is purely additive. Nothing here is upstream policy or official SDK documentation; it is a
contributor's working record.

**If you are an agent picking up work on this repo, read this file first**, then open only the
investigation that matches your task. Several of these documents are large (the hardware
recovery ledger is ~39 KB) and are written to be self-contained.

---

## 1. Hardware & firmware reverse engineering

`Docs/Investigations/2026-07-29-hardware-config-recovery/`

The largest body of work here. Characterizing the real silicon inside a physically-owned UNA
Watch, dumping its firmware, and recovering its BLE sync protocol — in order to build
independent firmware and a phone-free companion.

| File | What it is |
|---|---|
| `README.md` | **Start here.** The living investigation doc: a verification ledger where every claim is tagged CONFIRMED / LIKELY / UNVERIFIED / REFUTED with its corroborating method, plus per-sweep round data. |
| `REPRODUCTION-GUIDE.md` | A self-contained walkthrough of the *technique* — every command and source edit needed to reproduce the hardware-config read from scratch. Narrative counterpart to the ledger. |
| `BLE-COMPANION-protocol-spec.md` | Running output of the BLE/GATT recovery. **Start at its §0**: UNA published an official BLE specification on 2026-08-08, which is now authoritative for FTS and the standard services, and this document is reduced to corroboration plus what the official docs do not cover — the CCS custom command service, CANS, and the on-watch file layout. §3.2 carries the most consequential finding for any consumer: the watch reports a *fabricated* heart rate while off-wrist. §3c has the CANS notification wire format, decompiled from the vendor app. |
| `CANS-LIVE-PROBE-2026-08-21.md` | Live probe of CANS and the FTS v5 extensions on firmware 1.4.0. FTS 5 confirmed and windowing measured (2129 to 4255 B/s, reproducing the reference bench independently); CANS drew no reply to thirteen events and the reason is still unknown, with the ruled-out list recorded. Also decodes the CCS event channel: `0x01 0x00 <appId u64LE>` announces a saved activity, verified one-for-one against the files it produced. |
| `prototype/una_ble_client.py` | **Working, validated code.** A phone-free Linux BlueZ client that lists directories and pulls `.fit` activity files off the watch with matching CRC-16. Proof the protocol spec above is correct. |
| `prototype/una_gatt_dump.py` | **Working, validated code.** Dumps the full GATT table with property flags, reading BlueZ's resolved cache so it puts nothing on the air. Refuted the NUS-transport hypothesis and settled which direction each CANS/CCS channel runs; see the spec's §1.2. |
| `prototype/una_fts_walk.py` | **Working, validated code.** Read-only recursive FTS filesystem walk. Identified the `0x51` entry's trailing 12 bytes (mtime + size) and established the real filesystem layout; see §2.2.1, §2.2.2. |
| `prototype/una_read_probe.py` | **Working, validated code.** Reads one file at a chosen chunk size and dumps the raw `0x11` header both ways. Established that 128 is not a chunk limit (200 works; the bound is `mtu - 19`) and that the header fields are `uint32`. |
| `prototype/una_hr_probe.py` | **Working, validated code.** Black-box probe for CCS's two daily-health commands (`0x10` aggregate, `0x14` hourly per-minute HR matrix), including deliberately impossible requests to characterise error behaviour. Recovered the full `0x14` wire format from a real watch; see the spec's §3.1. |
| `prototype/README.md` | Setup and usage for that prototype. |
| `BLE-COMPANION-disassembly-prompt.md` | Self-contained handoff prompt: recover the GATT profile to build a Gadgetbridge plugin / standalone companion. |
| `SEAM-HUNT-disassembly-prompt.md` | Self-contained handoff prompt: find injectable seams in the closed vendor kernel for incremental per-peripheral Rust replacement. |
| `NEXT-SESSION-disassembly-prompt.md` | Self-contained handoff prompt: disassemble the verified 4 MB kernel dump in Ghidra. |
| `reassemble_dump.py` | Reassembles and CRC-verifies the chunked on-device flash dump. |
| `service-cpp-instrumentation-sweep7.cpp` | The instrumented `Service.cpp` used for the register/I²C sweeps — the actual read primitive. |

**Established highlights** (see the ledger for the confidence tag on each): MCU is an
STM32U5A5 confirmed three independent ways; apps run with **no isolation at all** (MPU
disabled, CPU privileged, TrustZone off); full 4 MB flash dumped and CRC-verified two ways
with the real image ending at `0x0820A140`; dual vector table confirming a bootloader at
`0x08000000` and kernel at `0x08060000`; IMU confirmed as BMI270 by exact CHIP_ID match.

> Note on the two top-level documents: `README.md` and `REPRODUCTION-GUIDE.md` both lived at
> `README.md` on their respective branches and are genuinely different documents, not
> revisions of one another. Both are kept.

---

## 2. TouchGFX `drawPartialBitmap` defect

Two independent investigations of the same underlying framework bug, kept separate because
they were found from different directions.

### `Docs/Investigations/2026-05-16-touchgfx-drawpartialbitmap-negative-x/`

| File | What it is |
|---|---|
| `README.md` | How to test whether *your* platform (Windows simulator, embedded ARM) is affected. The Linux simulator already works around it. |
| `DynamicBitmapDraw.hpp` | **The workaround itself**, kept here as an investigation artifact. On its original branch this lived at `Libs/Header/SDK/GUI/DynamicBitmapDraw.hpp`; move it back there if you adopt it. Active only under `SIMULATOR && __linux__`. |

### `Docs/Tutorials/RawTilesMap/Investigations/2026-05-16-cell-render-bug/`

**Status: RESOLVED.** A full experimental record — 33 files of READMEs, run logs, patch diffs
and screenshots across experiments A, B, C and C2. `LCD8bpp_ABGR2222::drawPartialBitmap` in
the prebuilt Linux `libtouchgfx.a` silently drops external dynamic bitmaps with a negative
target X, and stretches source across widget width for positive X. Fix: `LCD::blitCopy` with
a source-relative `blitRect`, verified in `experiment-C2-blitCopy-corrected/`.

The dead-ends are deliberately left in the narrative — the "shared pixel pointer count"
theory was a red herring, and that is worth knowing before re-deriving it.

### `Docs/touchgfx-drawpartialbitmap-y-clipping-bug-report.md`

Draft ST Community post reporting the defect upstream to ST. Never posted. The reproduction
case lives on the still-open `bug/drawpartialbitmap-y-clipping-repro` branch (PR #134), which
was left untouched.

---

## 3. Design notes

| File | What it is |
|---|---|
| `Docs/units-and-display.md` | Design rationale for `SDK::Units`: measurements stay SI internally, metric/imperial is applied exactly once at the moment a value becomes text. Includes measured flash cost and why the drawing functions are `SDK_GUI_NO_INLINE`. The live code for this is on the `feat/sdk-units-core` → `feat/sdk-units-touchgfx` → `refactor/running-adopt-units` → `perf/unit-label-repaint-skip` stack. |
| `Docs/companion-data-channel-analysis.md` | Analysis / RFC: third-party watch apps have no supported way to receive companion data. Written against upstream `2764a3e7`, with each claim checked against the cited file. |
| `RR_INTERVAL_PR.md` | The long-form rationale for the experimental `RR_INTERVAL` beat-to-beat pathway — the watch cannot produce HRV-grade timing but a BLE strap already sends it in the packet the kernel parses and discards. Written as a PR description for **open PR #220** (`feat/rr-interval-contract`, untouched). |

---

## 4. Verification harnesses

| Path | What it is |
|---|---|
| `proto-tests/` | Self-contained harness (`zsh proto-tests/run.sh`, needs only clang++ with C++17 — no TouchGFX or SDL2 tree, no network) verifying the logger redesign's claims. Each check corresponds to a property the old `#if`-based design could not hold simultaneously: compiling at `LOG_LEVEL=0` and `4`, via transitive include, and the logger's own implementation building at `LOG_LEVEL=0`. Live code on `refactor/logger-if-constexpr-gating` and `rfc/logger-if-constexpr-lifetime`. |

---

## 4a. Sensor capability: can the watch produce HRV?

`Docs/Investigations/2026-06-15-heart-beat-vs-ppg/`

**Read this before designing anything HRV-shaped.** It records UNA's own authoritative answer
(PR #167) to what the wrist sensor can and cannot do:

- `HEART_BEAT` (0x40) emits **no events** — HR detection is a frequency-domain algorithm, not
  per-beat detection. So RR intervals cannot be read off beat timestamps.
- The PPG waveform is **20 Hz, single channel**, which UNA called "the low end for HRV
  extraction." A higher-rate (higher-power) mode and on-chip HRV are both being explored, so
  these numbers are expected to change.
- **Optical HRV will only ever work at rest.** Mid-exercise HRV "can't be done optically — it
  has to be an electrical measurement." This is physics, not a roadmap gap.

That last point is why the chest-strap path (§3, `RR_INTERVAL_PR.md`) is structurally necessary
rather than just convenient. The folder also keeps `BeatProbe.hpp` plus its usage guide and
integration patch — a runnable 90-second diagnostic worth re-running once the firmware moves.

---

## 5. Linux simulator

| Path | What it is |
|---|---|
| `Docs/Simulator-Linux.md` | **Largely superseded** — Linux build instructions were folded into upstream `Docs/Simulator.md` by PR #213. Kept because this version is longer and retains the patch-by-patch "applying patches to upstream" detail that the upstream page condenses. Tested on Ubuntu 24.04 / GCC 13 and Arch / GCC 15. |
| `Docs/assets/screenshots/simulator-*.png`, `gpstrack-fit-map.png` | Evidence screenshots of the simulator running on Linux (HelloWorld, Sensors, Buttons, ScrollMenu, Files, GpsTrack, and a FIT track rendered on a map). These existed only on branches that have now been retired. |

---

## 6. Provenance and recovery

Every artifact above was taken from the newest or superset version available. Where two
branches held byte-identical copies, either was used; where they diverged, the newest was
taken and any genuinely different document was kept alongside rather than overwritten.

| Artifact | Taken from |
|---|---|
| `Docs/Investigations/2026-07-29-hardware-config-recovery/README.md` and BLE files | `docs/una-ble-companion-re-prompt` (superset: the seam-hunt copy plus 57 lines) |
| ...`/REPRODUCTION-GUIDE.md` | `docs/hardware-config-recovery` (was `README.md` there) |
| ...`/SEAM-HUNT-disassembly-prompt.md` | `docs/una-seam-hunt-re-prompt` |
| `Docs/Investigations/2026-05-16-touchgfx-.../README.md`, `DynamicBitmapDraw.hpp` | `docs/touchgfx-drawpartialbitmap-negative-x` |
| `Docs/touchgfx-drawpartialbitmap-y-clipping-bug-report.md` | `docs/touchgfx-drawpartialbitmap-y-clipping-bug-report` |
| `Docs/Tutorials/RawTilesMap/Investigations/**` | `experiments` (identical on `feat/rawtilesmap-tutorial`) |
| `Docs/companion-data-channel-analysis.md` | `docs/companion-data-channel-analysis` |
| `Docs/units-and-display.md` | `perf/unit-label-repaint-skip` (stack tip; superset of the `refactor/running-adopt-units` copy, and both supersede the retired `refactor/sdk-units-formatting` copy whose only difference was stale flash measurements) |
| `Docs/Simulator-Linux.md`, screenshots | `experiments` (superset of the `old-linux-simulator` copy by 23 lines) |
| `Docs/Investigations/2026-06-15-heart-beat-vs-ppg/**` | `feat/beat-event-probe`, plus the PR #167 discussion, which existed only on GitHub |
| `RR_INTERVAL_PR.md` | `docs/rr-interval-pr` |
| `proto-tests/` | `rfc/logger-if-constexpr-lifetime` (identical tree on `refactor/logger-if-constexpr-gating`) |

**Nothing was destroyed.** Every branch that was deleted or rewritten in this consolidation
was first tagged on `origin` as `archive/<original-branch-name>`. To see what a retired
branch held, or to bring one back:

```sh
git fetch origin 'refs/tags/archive/*:refs/tags/archive/*'
git log --stat archive/experiments                    # inspect
git branch experiments archive/experiments             # resurrect
```

`git ls-remote --tags origin 'refs/tags/archive/*'` lists all of them. Once you are satisfied
nothing is needed, the tags can be dropped — they are a safety net, not a permanent record.

---

## 7. What is deliberately *not* here

- **Branches attached to a live pull request** were not touched at all, in any way: `#260`
  `refactor/timer-retire-sender`, `#249` `ci/ratcheting-warning-gate`, `#234`
  `fix/reproducible-builds-macro-prefix-map`, `#220` `feat/rr-interval-contract`, `#214`
  `fix/simulator-shutdown-pure-virtual`, `#134` `bug/drawpartialbitmap-y-clipping-repro`.

  The one exception is `#167` `feat/beat-event-probe`, a declined PR whose whole purpose was to
  ask a question. Both the probe and the answer are captured in §4a and the branch was retired;
  GitHub keeps the PR and its discussion regardless.
- **Live code.** Research documents were *copied* here, not moved: the branches that carry the
  corresponding implementation still carry their copy. This branch is for reading, not for
  building or merging from.
- **Feature documentation** that ships with unmerged work — tutorial `ARCHITECTURE.md` files,
  the GpsLab app README — stayed with the feature branch it documents
  (`feat/gpstrack-tutorial`, `feat/rawtilesmap-tutorial`, `feat/gps-quality-logging`).

---

## 8. Apps now live in `tobymurray/watch-apps`

Four apps were subtree-split out of this SDK into a separate repo:
**Barcode**, **GpsLab**, **RustGuiPoc**, **Squash**. They build against a released SDK found
through `$UNA_SDK`, so do not re-add them here. `Barcode`, `Squash` and `RustGuiPoc` are fully
gone from this repo; `poc/rust-customgui-frontend` was retired once watch-apps had moved past
it (see `archive/poc/rust-customgui-frontend`).

**One migration is still incomplete.** `feat/gps-quality-logging` is deliberately still here,
because it is *ahead* of the GpsLab in watch-apps, not behind it:

- Two developer fields watch-apps does not have — `DF_HR_TRUST` (kernel-arbitrated HR
  confidence) and `DF_BATTERY_MAH` (coulomb-counted cumulative discharge, integrated from
  `BATTERY_METRICS` average current rather than diffed from state-of-charge).
- `Tests/Host/apps/GpsLab/ActivityWriter_test.cpp` — a host test that has **not** been carried
  over. Subtree splits miss these, because app host tests live in the SDK tree under
  `Tests/Host/apps/`, not inside the app directory. Squash's tests were moved by hand
  (`test(squash): bring the recorder host tests out of the SDK tree`); GpsLab's were not.
- SDK-side changes that belong to the SDK regardless: `Libs/Header/SDK/Fit/FitProfile.hpp`,
  `Docs/FitFiles-Structure.md`, `Tests/Host/CMakeLists.txt`.

Port those forward into watch-apps before retiring that branch.

Note that an app moving out does **not** mean its SDK support moves with it —
`feat/fit-profile-racket-squash` (squash = sport 64 / sub_sport 94) has to stay in the SDK for
the migrated Squash app to build against it.
