A cut-down of #220. Same type, same fields, far less around them.

## What this is

Heart rate variability (HRV) is computed from the gaps between heartbeats, the R-R intervals. No sensor type carries them: `HEART_RATE` and `HEART_RATE_EX` carry a rate. A chest strap already sends the intervals in the same Bluetooth notification as the BPM, and the kernel keeps the BPM and drops the rest.

This adds the SDK side only: `RR_INTERVAL` (0x44) and `SensorDataParser::RrInterval`, so the firmware producer and the first HRV app can be written against one shape. Experimental until a producer exists.

## The frame

One interval per frame. A notification with several intervals arrives as several frames, and on the device event frames can share one delivery, so iterate `DataBatch::size()`.

- `[0]` the interval in milliseconds (float)
- `[1]` source (u32), the same values as `HEART_RATE_EX`
- `[2]` flags (u32): `DISCONTINUITY`, the first interval after a gap; `ARTIFACT_SUSPECT`, the producer doubts this one

The timestamp is when the beat happened. A producer that only sees the notification arrive back-dates from it. An interval that is not a finite positive number makes the whole frame invalid, metadata included.

## Not here on purpose

A gap check with a built-in tolerance, a skin-contact bit, a detector-stamped bit, an ECG source, and a bpm helper. Each was either sized against something the SDK cannot see, or waits for the producer or app that needs it. All of them can be appended later without breaking anything built against this.

The wrist path produces one interval per beat with a graded confidence (review of 2026-08-08 on #220). That field appends as `[3]` when the producer lands. An expected error in milliseconds, as Polar's PPI stream carries, is the shape to prefer over a percentage, because an app thresholds it in the unit of the measurement.

## Questions

- Which producer comes first? The strap has a named seam in the kernel decoder (first review on #220); the wrist path may need its front end reconfigured to detect beats at all. Either one settles the layout.
- Can a frame be lost between the kernel and an app without the app seeing it? The event message has no sequence number, and `sendMessage` can fail on a full queue. The first producer can settle it: replay a recording with a known beat count, or log a strap session on the watch and on the phone at once, and compare. Until then, comparing `t[n] - t[n-1]` with `rr[n]` is the only check an app has, which is why the timestamp rule is in the contract. If frames can be lost, a sequence number should be appended.
