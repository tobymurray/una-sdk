# feat: experimental RR_INTERVAL beat-to-beat pathway for HRV

Follows the HRV discussion on #167. That thread established there is no
app-reachable beat-to-beat data today: HR detection is frequency-domain
(`HEART_BEAT` emits nothing), 20 Hz single-channel PPG is below the HRV floor,
and an external strap's R-R intervals are collapsed to BPM before the SDK. HRV /
DFA-α1 — the basis of resting-readiness and threshold metrics — need those
intervals.

This lands the **SDK-side half** of an RR pathway so the HRV pipeline can be
built and validated now (against a simulator replay of real Polar H10 data),
with the producer to follow on the firmware side. Marked **EXPERIMENTAL** — the
wire shape is not frozen.

## Scope: this is the electrical-strap pathway
`RR_INTERVAL` is aimed at the **electrical chest strap** — which is also the only
route to *exercise* HRV, since optical can't measure HRV in motion. Per #167,
optical HRV is more likely to arrive later as an **on-chip computed metric**
(RMSSD etc.) than as raw intervals, so this type is not trying to be the
universal HRV answer — just the raw-interval pathway the strap already makes
possible.

## What's here
- `SDK::Sensor::Type::RR_INTERVAL = 0x44` — additive, next free cardio slot.
- `SensorDataParser::RrInterval` — one interval per frame; optional appended
  `source` + `flags` fields.
- A header-only simulator replay mock (default-off behind `RR_REPLAY_SIM_ENABLE`)
  that replays a Polar Sensor Logger `*_RR.txt`.
- A consumer example in the Sensors tutorial `Service`; host unit tests.
  Verified end-to-end in the Linux simulator: replayed fixture → frame → app.

## Design decisions (and why they already fit the platform)
These anticipate the obvious constraints; correct me where the firmware reality
differs:

- **One interval per frame.** `SDK::Sensor::Data` has no in-band length field —
  the field count is derived from the per-driver stride, which is fixed at
  construction. A variable-length burst therefore can't be represented honestly;
  the several RR values in one `0x2A37` notification are delivered as
  **consecutive one-interval frames** (a consumer iterates `DataBatch::size()`).
  The frame is a small fixed shape, which is exactly what the fixed-stride
  delivery path wants.
- **Timestamps are arrival-derived, and that's fine.** BLE HRS carries RR
  *values* (1/1024 s units) but no per-beat absolute instants — only notification
  arrival. RMSSD and DFA α1 are computed from the interval *values*, not absolute
  times, so the metrics are correct regardless; the frame timestamp is best-effort
  (arrival, back-summed), not a claim of hardware beat-timing.
- **Gap signalling from connection state; artifact correction is the app's job.**
  A strap dropout shows only as a jump in arrival time, so `flags` carries a
  `DISCONTINUITY` bit a producer can set at a reconnect boundary — which the
  kernel already knows (`Accessory::EventStatus` surfaces `CONNECTED` / `LOST`).
  Ectopic / artifact correction (Lipponen–Tarvainen etc.) stays in the app,
  consistent with the SDK's "kernel surfaces data, app computes" split.
- **Forward-compatible.** `source` and `flags` are optional appended fields
  (positional, lenient upper bound like `HeartRateEx`), so a minimal producer can
  ship `rr`-only and enrich later with no ABI break. EXPERIMENTAL keeps even the
  base layout changeable.

## The one open decision, and one firmware question
1. **New type, or ride on `HEART_RATE_EX`?** RR is event/streaming-shaped, whereas
   `HEART_RATE_EX` is a fixed ~1 Hz BPM snapshot — cramming beat data into it is
   awkward, and a separate opt-in type mirrors how `HEART_RATE_EX` was itself split
   out from `HEART_RATE`. But it's your call; if you'd prefer a companion frame,
   the parser and mock port easily.
2. **How entangled is the `0x2A37` RR?** You noted the RR already rides in the
   notification the kernel parses for BPM — is surfacing the array mostly
   forwarding, or is it tied into the HR-source arbitration / fusion path?

Happy to take the SDK side wherever you land, and to adjust the shape once the
firmware reality is clear. Still keen on the broader "generic BLE characteristic
passthrough" idea from #167, too.
