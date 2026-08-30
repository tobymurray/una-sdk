# Live probe of CANS and FTS v5 — 2026-08-21

Firmware 1.4.0, hardware revision 3.1, probed from Linux/BlueZ with no phone involved.
Transcripts: `v5_probe.jsonl`, `v5_final.jsonl`, `cans_probe.jsonl`, `cans_open.jsonl`,
`cans_uid.jsonl`, `cans_screen.jsonl`. Every frame in both directions is timestamped there; the
figures below are extracted from those files, not retyped from the terminal.

## Headline

**FTS protocol 5 is confirmed and measured. The CCS event channel is decoded and announces saved
activities. CANS is solved: it was switched off in a settings file on the watch, and once enabled
the format recovered in §3c is byte-for-byte correct.**

The silence that took most of this session to explain was total: thirteen notification events
across five runs — every action, every category, seven UID encodings, idle and occupied screen —
drew zero replies of any kind, no error and no status, after 100 seconds of waiting each. None of
it was a protocol fault. See §CANS.

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

## CANS: it was switched off

Thirteen events across five runs drew nothing. The cause was not the protocol.

`/settings.json` on the watch carries `phone.notifications`, and it was **`false`**:

```json
{"units":"metric","watchFaceId":0,"phone":{"notifications":false},
 "heartRateZones":[92,110,129,147,166,184],
 "dailyGoals":{"activityMinutes":30,"steps":5000,"floors":5},
 "height":190,"weight":90,"gender":"M","dateOfBirth":"1990-01-01","version":2}
```

The watch's "Phone Notifications" toggle is not a BLE command. The vendor app reads `/settings.json`
over FTS, flips the flag, and writes the whole file back. With the flag false, every CANS write is
accepted and silently discarded — no error, no status, no reply — which is exactly
`Notification dropped: no callback attached`, and exactly what was observed. The firmware string
`phone.notifications` names this path and was in the strings dump the whole time.

**This is the single most important thing for anyone building a companion.** A disabled flag is
indistinguishable, on the wire, from a broken implementation. Read `/settings.json` and check the
flag before concluding anything about a notification that never arrives.

### The exchange, once enabled

Captured from the vendor app's own logging, with the notification visible on the watch:

```
Added notification UID=769039210 to store.
Notification sent: UID=769039210, Action=Add      (uid=0x2dd69b6a)
Request Attributes: [{5,32},{1,255},{2,255},{7,16},{8,16},{9,16}]
responseData: 03 6a 9b d6 2d 05 15 00 ...
Sending response: total size=70 bytes, maxPacketSize=217
Request Attributes: [{4,2040}]
Sending response: total size=12 bytes
```

The response bytes confirm §3c.2 exactly: `03`, uid `6a 9b d6 2d` little-endian = `0x2DD69B6A`
matching the logged UID, then attribute `05` with a `u16 LE` length of 21. `maxPacketSize = 217` is
`mtu - 3` at MTU 220, as decoded.

Confirmed independently on the air. Android's HCI snoop log captured the event frame the vendor
app sent, a 7-byte write to handle `0x0032`:

```text
01 00 6a 9b d6 2d 01
│  │  └─ uid 0x2DD69B6A ─┘ └ category 1 = Message
│  └ action 0 = Add
└ constant
```

Structurally identical to the frames this probe had been sending all along
(`01 00 8c 23 00 00 01`), differing only in the UID. The event encoding was never the problem.

Three things the decompile could not have shown:

**The fetch is two-phase.** The watch asks for metadata first — AppIdentifier, Title, Subtitle,
Timestamp and both action labels — and comes back about 240 ms later for the Message body on its
own. A companion must therefore **retain a notification's content across several requests**;
answering once and discarding is not enough.

**`maxLength` is per-attribute and varies widely**: 16 bytes for a timestamp, 255 for a title,
**2040 for the message body**. At 217 bytes per packet a full-length Message needs about ten
fragments, so the fragmentation path is real and reachable — this short test notification simply
did not reach it.

**AppName (6) and MessageContentSize (3) were never requested**, though both exist in the enum.
Implement them, but do not expect them.

### What this retires

The two theories recorded earlier are both dead, and neither was close. The UID-as-timestamp theory
is refuted twice over: the real UID `0x2DD69B6A` has low 24 bits of `0xD69B6A`, which is not a
current clock reading in any unit. The missing-glance theory was already withdrawn.

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

For a Gadgetbridge notification implementation, the format is now validated and the work is
unblocked. Three things it must do that are not obvious from the format alone: check
`phone.notifications` in `/settings.json` before sending, because a disabled flag looks exactly
like a broken implementation; retain content until the watch stops asking, since the fetch comes in
at least two rounds; and honour each attribute's `maxLength`.

For the windowed-read work already merged: it is confirmed correct on hardware, the clamp is
belt-and-braces rather than load-bearing at MTU 220, and 4096 is the right window.

## Also observed of the vendor app

**It deletes activity files after downloading them**, including `/Apps/latest_activity.txt`. A
companion that leaves files in place — as Gadgetbridge does — will silently never see a workout the
vendor app collected first. Running both on one watch means whichever syncs first wins.

**It uses windowed reads.** `0x12` READ_PACING appears as a 12-byte write exactly as documented, and
its progress logging steps in roughly 11 KB increments. Its file writes chunk at 217 bytes, again
`mtu - 3`.

## Open, in priority order

1. Identify CCS event `0x04 0x00 0x00`. `0x01` is understood; this one fires only at CCCD enable.
2. Reach the fragmentation path with a message long enough to need it — a full 2040-byte body.
3. `ExecutePositiveAction` / `ExecuteNegativeAction` from the watch, which nothing has yet
   triggered.
