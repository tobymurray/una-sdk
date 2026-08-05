# Can the UNA Watch produce HRV? — `HEART_BEAT` vs raw PPG

**Date:** 2026-06-15 → 2026-07-02
**Status:** **ANSWERED by UNA, authoritatively.** The probe was never needed — a maintainer
answered from firmware knowledge instead. PR [#167](https://github.com/UNAWatch/una-sdk/pull/167)
was closed without merging, which was the intended outcome: the PR existed to ask a question,
not to land code.

This page is the durable record of that answer. The diagnostic that was built to answer it
empirically is kept alongside, still runnable, because the answer has a "not today" shape and
will change.

---

## 1. The question

HRV (heart-rate variability) underpins training-readiness and recovery metrics, and HRV is built
from **RR intervals** — the gaps between consecutive heartbeats. So the whole feature rests on one
question about the firmware, which has two very different answers:

- If `Type::HEART_BEAT` (0x40) emits **timed peak events**, RR is simply the difference between
  consecutive beat timestamps, and HRV is cheap streaming statistics.
- If only the raw `PPG` (0xF0) **waveform** is available, RR requires an on-watch peak detector,
  and the sample rate becomes the limiting factor.

Without a device to test on, the question was posed as PR #167 with a runnable probe attached, so
anyone with hardware could answer it by wearing the watch for 90 seconds.

## 2. The answer (UNA maintainer `rryles`, 2026-07-01)

Answering the four questions the PR asked, in the maintainer's own terms:

| # | Question | Answer |
|---|---|---|
| 1 | Does `HEART_BEAT` (0x40) deliver ~one event per heartbeat? | **No events at all.** HR detection "isn't detecting individual beats — it's more of a frequency domain algorithm." |
| 2 | Is a beat timestamp the detection instant or the IPC delivery time? | Moot — there are no beat events. |
| 3 | Does the beat event carry an RR/IBI payload or quality flag? | Moot — there are no beat events. |
| 4 | Is a continuous PPG waveform available, at what rate and how many channels? | **Yes: 20 Hz, single channel.** That is all the PPG sensor does today (measuring HR). Described as "at the low end for HRV extraction." |

So the probe's verdict, had it run, would have been **B** — and marginally so, because 20 Hz
single-channel is a weak basis for peak detection.

### What UNA said about the path forward

- **HRV is wanted:** "HRV is definitely something we want to do. However, it's going to take some
  work." Bottom line: "not today, but we are working on it."
- **A higher-rate PPG mode is the likely route:** they would "probably want to switch to a higher
  frequency (and higher power consumption) mode temporarily when measuring HRV." So the 20 Hz
  figure is a property of the current HR-measuring mode, not a hardware ceiling.
- **On-chip HRV is being explored:** they are "looking into the possibility of getting HRV to be
  calculated by the PPG chip itself" — which would put HRV behind a firmware API rather than
  requiring app-side signal processing.

### The constraint that shapes everything downstream

> "This will only ever be HRV at rest. Measuring HRV while moving (e.g. mid exercise) can't be
> done optically — it has to be an electrical measurement. Overnight or resting morning HRV is
> perfect for training readiness/recovery metrics though."

This is a **physical** limit, not a firmware roadmap item. Optical PPG cannot yield HRV under
motion at any sample rate. Any design that assumes mid-exercise HRV from the wrist sensor is
wrong, and no amount of firmware work will fix it.

## 3. Why this matters beyond HRV

The rest-only constraint is what makes the **chest strap** path structurally important rather than
merely convenient. A strap is an electrical measurement, so it yields HRV *during exercise* — the
case the optical sensor can never serve.

The watch already reads the standard Heart Rate Service notification (0x2A37) via
`AccessoryMessage` / `Kind::HRM` → `HEART_RATE_EX`. **RR intervals ride in that same
notification**, alongside the BPM the watch already extracts — but the kernel collapses the packet
before an app sees it, so the beat-to-beat data is discarded at the SDK boundary.

That observation is what became **PR [#220](https://github.com/UNAWatch/una-sdk/pull/220)**
(`feat/rr-interval-contract`, still open) — surfacing the RR array as a new frame. Its rationale
document is [`RR_INTERVAL_PR.md`](../../../RR_INTERVAL_PR.md) on this branch. Read that next; it
is the direct descendant of this investigation.

## 4. Open threads — asked, never answered

Both questions below were raised on 2026-07-02, after the PR had been closed the previous day.
Neither received a reply, so both are genuinely open rather than declined:

1. **Can the kernel surface the RR array as a new frame?** Superseded in practice by PR #220,
   which proposes a concrete pathway rather than asking.
2. **Could the SDK expose a generic BLE passthrough?** — "connect to this device, subscribe to
   this characteristic, hand me the raw bytes." The framing: `RequestPrepare` only accepts a
   bitmask of *kernel-defined* kinds, so there is no way to point the watch at an arbitrary BLE
   device. Bluetooth is noticeably less accessible than the rest of the platform. A passthrough
   would let community features be prototyped in an app and "promoted" into firmware once proven.

Thread 2 is the same structural gap analysed from a different angle in
[`Docs/companion-data-channel-analysis.md`](../../companion-data-channel-analysis.md) on this
branch. Treat the two as one open question about platform extensibility, not two.

## 5. The diagnostic

Kept because the answer is explicitly time-limited: a higher-rate PPG mode or on-chip HRV would
change every answer in §2, and re-probing is then the fastest way to find out what changed.

| File | What it is |
|---|---|
| `BeatProbe.hpp` | The probe. Observes four streams the Sensors tutorial already subscribes to — `HEART_BEAT` (0x40), `PPG` (0xF0), `HEART_RATE` (0x41) as a cross-check, `TOUCH_DETECT` (0x140) to distinguish "not worn" from "no data". Accumulates RR intervals and RMSSD, estimates PPG sample rate, and prints a self-interpreting A/B/C verdict every 15 s and at 90 s. Integer math only — no `<cmath>`, no float `printf` — to stay robust on the MCU's newlib. Log-only: no GUI, no filesystem, no FIT. |
| `USAGE.md` | Build, flash, wear, and grep instructions, with an annotated sample log. Verbatim from the branch, where it lived at `Utilities/BeatProbe/README.md`. |
| `Service.cpp.patch` | The wiring into the Sensors tutorial's `Service.cpp` — four taps in `onSdlNewData()`, a `tick()` per main-loop iteration, and a `flushFinal()` on `COMMAND_APP_STOP`. Apply against `Docs/Tutorials/Sensors/Software/Libs/Sources/Service.cpp`. |

To re-run: drop `BeatProbe.hpp` next to the Sensors tutorial's `Service.hpp`, apply
`Service.cpp.patch`, then follow `USAGE.md`. Verdict **A** would mean beat events now exist;
**B** means PPG-only (check the reported `est_rate` — if it is above 20 Hz, the higher-frequency
mode landed); **C** means no cardio data, so check worn state and HR calibration first.

Original branch: `feat/beat-event-probe`, retired after this capture. The commits survive as
`archive/feat/beat-event-probe` — see §6 of [`RESEARCH-INDEX.md`](../../../RESEARCH-INDEX.md).
