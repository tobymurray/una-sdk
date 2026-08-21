# Live probe of CANS and FTS v5 — 2026-08-21

Firmware 1.4.0, hardware revision 3.1, probed from Linux/BlueZ with no phone involved.
Transcripts: `v5_probe.jsonl`, `v5_final.jsonl`, `cans_probe.jsonl`, `cans_open.jsonl`,
`cans_uid.jsonl`, `cans_screen.jsonl`. Every frame in both directions is timestamped there; the
figures below are extracted from those files, not retyped from the terminal.

## Headline

**FTS protocol 5 is confirmed and measured. The CCS event channel is decoded and turns out to
announce saved activities. CANS could not be exercised at all, and why remains unexplained.** Thirteen notification events across five runs — every action, every
category, seven UID encodings, idle and occupied screen — drew **zero** replies of any kind. No
error, no status, no retry, nothing after 100 seconds of waiting.

This is a characterised negative result rather than a solved problem, and the list of what has
been ruled out is the useful part.

## FTS protocol 5 — confirmed, with one surprise

| Question | Result |
|---|---|
| V1 `adaf0001` on firmware 1.4.0 | **5** (`05000000`). Firmware 1.3.0 read 4 |
| V3 DIGEST `0x70` | Works. `size=4410 crc32=0xbd278c9e`, matching `zlib.crc32` of the bytes read back |
| V5 activity archive | 9 `.fit` files, 156,245 bytes, largest 63,456, across 22 app directories |

### Windowing, measured

One 4410-byte file, read four times:

| window | notifications | requests | notif/req | seconds | B/s | notification sizes | header mismatches |
|---|---|---|---|---|---|---|---|
| 201 | 22 | 22 | 1.00 | 2.07 | 2129 | 189–201 | 0 |
| 4096 | 23 | 2 | 11.50 | 1.04 | 4255 | 76–201 | 0 |
| 16384 | 22 | 1 | 22.00 | 0.99 | 4465 | 189–201 | 0 |
| 65536 | 22 | 1 | 22.00 | 0.99 | 4456 | 189–201 | 0 |

All four returned byte-identical content.

**2129 → 4255 B/s independently reproduces the reference bench's 2.1 → 4.2 kB/s**, from a different
client, on a different host, against a different file. That is worth more than either measurement
alone.

**Diminishing returns are real and start immediately after 4096.** Going 4096 → 65536 buys 4.7%,
because past one round trip per file there is nothing left to save. UNA's "larger wins little"
guidance is confirmed rather than merely repeated.

**No ceiling was found.** 65536 is accepted and answered in a single request. Whatever bound exists
is above anything worth asking for.

**una-sdk#272 is fixed across the whole range, not just at the size it was reported at.** Zero
header mismatches at every window including 65536. The 4096 row is the one that shows the firmware
respecting a window edge exactly: its 76-byte notification is the tail of the first window
(20 × 201 = 4020, + 76 = 4096), after which pacing resumes. Delivery is exactly
`min(window, remaining)`, split at the boundary rather than rounded to it.

### MTU: 220 is the watch's ceiling, not Android's

BlueZ negotiated **220**, the same value Android settled on after asking for 247. Two unrelated
centrals arriving at the same number makes 220 the watch's limit. So the open question of whether
the chunk-length clamp stays honest above MTU 220 is **moot on real hardware** — that MTU is not
reachable. Maximum observed notification payload is 201 = `mtu - 19`, so the firmware does size to
the MTU; it simply has little MTU to size to.

## CANS: silent, and why

Thirteen events, zero replies. What was varied, and ruled out:

| Varied | Values tried | Result |
|---|---|---|
| UID | 9000, 9100, 9200, unix seconds, seconds truncated to 24 bits, ms truncated, `0x00FFFFFF`, `0xFFFFFFFF`, full 32-bit seconds | all silent |
| Category | Message, Call, Other | all silent |
| Action | Add | silent |
| Screen | watchface idle, and notification screen open | all silent |
| Patience | 6 s, 25 s, 100 s after a single event | no retry, no timeout frame, nothing |

The UID theory deserves recording because it was well-founded and is now **refuted**. The firmware
carries `[UID: %u] Skip old notification at %u, now %u`, and the vendor app builds a UID as
`(hash(appId + '-' + key) & 0xFF) << 24 | (a0 & 0xFFFFFF)` — the low 24 bits are caller-supplied,
and `formatTimestamp` is defined immediately after it. A UID of 9100 would then be a 1970
timestamp and rightly dropped. Seven encodings including a current truncated clock reading were all
equally silent, so whatever `Skip old notification` guards, it is not what is stopping this.

### Ruled out

**The UID.** Well-founded and refuted. The firmware carries `[UID: %u] Skip old notification at
%u, now %u`, and the vendor app builds a UID as `(hash(appId + '-' + key) & 0xFF) << 24 |
(a0 & 0xFFFFFF)` — the low 24 bits caller-supplied, with `formatTimestamp` defined immediately
after. A UID of 9100 would be a 1970 timestamp and rightly dropped. Seven encodings including a
current truncated clock reading, `0x00FFFFFF` and `0xFFFFFFFF` were all equally silent. Whatever
`Skip old notification` guards, it is not this.

**A missing notifications glance.** Initially concluded from `/Apps/app_list.json`, which lists 20
apps with no notifications entry, and appeared to match `Notification dropped: no callback
attached`. **Withdrawn:** the device's owner confirms the notifications glance is firmware-provided
and was never installed, so its absence from the installed-app list is not evidence of anything.
Recorded because the reasoning was sound and the conclusion was wrong; anyone re-deriving it from
`app_list.json` should stop here.

**Whether the writes are malformed enough to be rejected outright.** Deliberately invalid frames —
ActionID `0x7F`, CategoryID `0x7F`, a truncated 2-byte frame, 20 zero bytes — are all accepted by
BlueZ exactly as a well-formed event is, which says only that the local stack queued them.

### Not ruled out, and not distinguishable from here

`CANS-0001` is `write-without-response` only: a write with response is refused with
`Write not permitted`. So there is no ATT acknowledgement to be had, and **"the write never
arrived" and "the write arrived and was discarded" look identical** from the central. Every
observation this session is consistent with either.

Remaining candidates, in the order worth testing:

1. Something the vendor app establishes that this probe does not — a state, a handshake, or simply
   being recognised as the phone the watch was set up with.
2. A watch-side precondition that is real but not the glance: `Notification dropped: no callback
   attached` still describes the symptom exactly, and the callback may attach only under conditions
   not reproduced here.
3. A frame detail wrong in a way that fails silently. The format is read from the app's own
   encoder and byte-verified against a shipped, hardware-validated CCS builder, so this is the
   least likely — but it cannot be excluded while nothing answers.

The decisive experiment is not another guess: it is **watching the vendor app do it**. A BLE
sniffer capturing the phone sending one real notification would settle in a single frame what this
session could not settle in thirteen.

## The GATT table is unchanged from 1.3.0

8 services, 19 characteristics, identical to the firmware 1.3.0 dump. **Still no Nordic UART
Service**, which UNA's published `BLE-Services-Overview.md` lists. That discrepancy is now
confirmed across two firmware versions rather than assumed to be a 1.3.0 quirk.

## Two findings nobody was looking for

### The CCS event characteristic announces saved activities — recovered

CCS `-0002-` is notify-only and had never been seen carrying anything. It carries two opcodes.

**`0x01 0x00 <appId: u64 LE>` — an activity was saved.** Ten bytes. The trailing eight are an app
ID that matches `/Apps/app_list.json` exactly:

| frame | decoded app ID | app |
|---|---|---|
| `010035608f2cb9e4d7a1` | `A1D7E4B92C8F6035` | Treadmill |
| `0100824af0c9b7d3e5a1` | `A1E5D3B7C9F04A82` | Walk |

Three such events were observed across two listening windows, and the watch's filesystem then held
exactly three new activity files, matching one-for-one in app and order. In the second window the
wearer deliberately saved one workout and discarded another: **one file, one event**. Discarding
leaves no trace on either the wire or the filesystem.

So this is `sendEventActivityEnded`, and it means "an activity was saved in this app". The `.fit`
is written and `/Apps/latest_activity.txt` is updated. That is precisely the signal a companion
needs to sync on completion rather than by polling, and it names the app, so the archive path can
be narrowed before walking anything.

**`0x04 0x00 0x00` — unidentified, latched.** Emitted within 0.1 s of subscribing in all three runs
where the channel was subscribed, with the watch untouched, and never at any other time. The
reproducibility and the timing suggest something queued being flushed on CCCD enable rather than a
live event. `sendEventFindPhoneAlert` is the other sender named in the firmware, but the device's
owner reports find-phone does not appear to be implemented in the watch UI, so `0x04` is more
likely something else. Unresolved.

### LISTDIR of a missing directory answers `50 03`

Every app without an `Activity/` directory answered `50 03` — the **request** opcode `0x50` with
status `0x03` (`ERROR_NO_FILE`), not the `0x51` entry opcode. A client expecting only `0x51`
frames sees this as unparseable rather than as "no such directory". Worth knowing before walking
the archive: it is normal, common, and not an error condition.

## What this changes

For a Gadgetbridge notification implementation: **do not start yet.** The wire format is known
but unvalidated, and a companion built against it would be indistinguishable from a broken one,
because the failure mode observed all session is complete silence with no error to surface. The
windowed-read work, by contrast, could be validated the moment it was written.

For the windowed-read work already merged: it is confirmed correct on hardware, the clamp is
belt-and-braces rather than load-bearing at MTU 220, and 4096 is the right window.

## Open, in priority order

1. **Sniff the vendor app sending one notification.** Everything else is guessing; this is the one
   move that converts a silent channel into a known one.
2. Identify CCS event `0x04 0x00 0x00`. `0x01` is now understood; `0x04` is not.
3. Q1–Q10 for CANS, all still unanswered and all blocked on (1).
