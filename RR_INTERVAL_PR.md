# feat(sensor): RR_INTERVAL — experimental beat-to-beat pathway for HRV

Follows the HRV discussion on #167. That thread established that today there is **no**
app-reachable beat-to-beat data: `HEART_BEAT` (0x40) emits nothing (frequency-domain HR),
20 Hz single-channel PPG is below the HRV floor, and an external strap's R-R intervals are
collapsed to BPM before the SDK sees them. HRV / DFA-α1 — the foundation of resting-readiness
and threshold metrics — need those intervals.

This PR lands the **SDK-side half** of an RR pathway so the HRV pipeline can be built and
validated now (against a simulator replay of real Polar H10 data), leaving the producer to the
firmware side. It is deliberately marked **EXPERIMENTAL** — I'm not asking you to freeze a wire
shape, I'm asking whether this is the shape you'd want to produce.

## What's here
- `SDK::Sensor::Type::RR_INTERVAL = 0x44` — additive, next free cardio slot.
- `SensorDataParser::RrInterval` — **one interval per frame** (the only shape the `Data` model
  honestly supports: field count is derived from the per-driver stride, there's no in-band
  length). A burst = several frames in one `DataBatch`. Lenient upper bound so a producer may
  append `source` / `flags` fields later without an ABI break (mirrors `HeartRateEx`).
- A **header-only simulator mock** (`RrIntervalReplay`, default-off behind `RR_REPLAY_SIM_ENABLE`)
  that replays a Polar Sensor Logger `*_RR.txt` as `RR_INTERVAL` frames, event-based, verbatim.
- A consumer example in the Sensors tutorial `Service`. Verified end-to-end in the Linux sim:
  replayed fixture → frame → app logs the intervals.

## Open questions (these gate the design — your call)
1. **Should RR be a first-class sensor type at all, or ride on `HEART_RATE_EX`** (appended fields /
   companion frame) so it inherits the existing source + arbitration contract? This decides whether
   `0x44` should exist.
2. Given `Data` has no in-band length and stride is fixed per driver, do you accept
   **one-interval-per-frame** as permanent, or want a reserved count/source header (changes the
   layout before any merge)?
3. Does the BLE path expose **per-beat instants**, or only notification-arrival time? The correct
   timestamp semantics — and whether apps can reconstruct a valid beat timeline — hinge on this.
4. How should RR signal **discontinuities** (strap dropout, off-wrist, ectopics) — a flag, a
   sentinel, or a delivery guarantee — and who owns artifact correction, kernel or app?
5. Is surfacing the strap RR the kernel already parses from `0x2A37` genuinely "just plumbing," or
   is it entangled with the BPM-fusion / arbitration path?

I'm happy to take the SDK side wherever you land on these, and to adjust the shape once the firmware
reality is clear. (Also still keen on the broader "generic BLE characteristic passthrough" idea from
#167 as a way to prototype accessory features before promoting them into firmware.)
