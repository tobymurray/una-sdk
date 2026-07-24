# feat: pass through heart-rate strap R-R intervals (experimental RR_INTERVAL)

In one line: the watch can't produce the beat-to-beat timing that HRV needs, but a
Bluetooth heart-rate strap already sends it and the watch currently throws it
away; this keeps that data and hands it to apps.

## Why
HRV and DFA-alpha1, which underpin readiness and training-threshold metrics, are
built from R-R intervals: the gaps between consecutive heartbeats. The watch
itself cannot provide them. Per the discussion in #167, its HR detection is
frequency-domain (HEART_BEAT emits no events) and its 20 Hz single-channel PPG is
below the floor for HRV.

A Bluetooth heart-rate strap can provide them. The standard Heart Rate Service
notification (0x2A37) that the watch already reads for BPM also carries the R-R
intervals, in the same packet. Today the kernel parses that packet, keeps the
BPM, and discards the R-R values. The core request is to surface them instead.

This is not Polar-specific. R-R intervals are a standard field of the Bluetooth
Heart Rate Service, so any compliant strap (Polar, Garmin, Wahoo, and so on)
provides them the same way. A Polar H10 was used only to record the sample fixture.

## What this PR contains (the SDK-side half)
It lands the SDK contract and the tooling to build and validate the HRV pipeline
now, against a simulator replay of real strap data, with the firmware producer to
follow. The type is marked experimental, so the wire shape is not frozen.

- `SDK::Sensor::Type::RR_INTERVAL` (0x44): one R-R interval per frame, in
  milliseconds, with optional appended source and quality-flag fields.
- `SensorDataParser::RrInterval` to read it, plus host unit tests.
- A header-only simulator sensor that replays a recording as RR_INTERVAL frames
  in real time, off by default (`RR_REPLAY_SIM_ENABLE`). A short real sample
  recording (`rr_fixture.txt`, about 80 beats including a real strap dropout) is
  included so it runs out of the box; point `RR_REPLAY_SIM_FILE` at a longer
  recording to exercise the full pipeline.
- A consumer in the Sensors tutorial that logs each interval and its gap marker.
  Verified end to end in the Linux simulator when the mock is enabled: fixture,
  frame, app.

## The firmware ask, concretely
Stop discarding the R-R field. The kernel already parses the 0x2A37 notification
for BPM, and the R-R values are in the same bytes; the request is to route them
through the sensor layer as RR_INTERVAL frames. The one unknown is whether that
field is discarded somewhere clean, or is buried in the HR-source arbitration path.

One neighbouring field comes almost for free and is worth taking at the same time:

- Sensor-contact status, also in the 0x2A37 flags byte, is a data-quality signal
  for HRV (poor skin contact produces artefacts). It is surfaced here as a quality
  flag on the interval (`NO_SKIN_CONTACT`).

Strap battery level is deliberately left out. It lives on a separate standard
service (0x180F), so it is not the same packet or the same work, and it is not
HRV-relevant.

## Design notes
- One interval per frame. `SDK::Sensor::Data` has no in-band length field; the
  field count comes from the fixed per-driver stride. A burst of R-R values
  arrives as consecutive one-interval frames, so a consumer iterates
  `DataBatch::size()`.
- Arrival-derived timestamps, with one platform caveat. BLE carries R-R values,
  not per-beat instants. RMSSD and DFA-alpha1 use the interval values rather than
  absolute times, so the metrics are correct. The caveat is delivery, not metrics:
  the per-listener sample-rate adapter gates on the frame timestamp, so a producer
  must give each interval a distinct per-beat time (the mock spaces them by the R-R
  value). Stamping a whole 0x2A37 notification's R-R values with one arrival instant
  would drop all but the first beat; removing that dependency (a true no-decimation
  event path for beat-level streams) is noted as platform work.
- Gaps versus artefacts. A strap dropout shows only in arrival time, so a
  discontinuity flag marks a reconnect boundary (the kernel already tracks
  connection state via `Accessory::EventStatus`). Artefact correction stays in the
  app, matching the SDK's "kernel surfaces data, app computes" split.
- Forward-compatible. `source` and `flags` are optional appended fields, so a
  minimal producer can ship R-R only and enrich later with no ABI break.

## Open question
New sensor type, or fields appended to `HEART_RATE_EX`? R-R is event-shaped and
streaming, whereas HEART_RATE_EX is a fixed ~1 Hz BPM snapshot, so a separate
opt-in type fits better and mirrors how HEART_RATE_EX was split from HEART_RATE.
Happy to reshape if a companion frame is preferred.

Separately, ECG-grade exercise HRV needs the strap's proprietary service rather
than the standard one, which is a case for the generic BLE passthrough idea from
#167 rather than for this PR.
