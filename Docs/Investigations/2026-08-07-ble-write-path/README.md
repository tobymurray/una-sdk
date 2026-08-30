# BLE File Transfer Service write-path measurement

How fast can a desktop push a file to the UNA watch over BLE, and what framing does it? Extends the working
read-path client and investigation from `../2026-07-29-hardware-config-recovery/`.

> **Firmware applicability:** every measurement in this document was taken on **firmware 1.3.0**.
> The read-path `real_chunklen` bug found in M2 was filed as [#272][i272] and fixed in **1.4.0**,
> which raises the read ceiling from 2.1 kB/s to 4.2 kB/s — see
> [`../2026-08-18-fts-read-chunklen-fix/`](../2026-08-18-fts-read-chunklen-fix/). The **write**
> numbers below have *not* been re-measured on 1.4.0 and should not be assumed to still hold.

## Headline number

**Sustained write throughput: ~2220-2237 B/s (≈2.2 kB/s), CONFIRMED flat across a 30x size
range (1 MiB to 29 MiB), on Linux/BlueZ, single device pair, 4 real full-size writes.**

Conditions: MTU=220 (negotiated by BlueZ, not requested), connection interval=45.00ms/latency=0
(CONFIRMED via live HCI capture), chunk size capped at 205 bytes (ATT_MTU-3-12, a client-side
cap this investigation had to add -- the firmware's own `free_space` hint is not MTU-aware and
will happily promise more than fits in one ATT Write Command). Round-trip-bound, not
bandwidth-bound: ~90ms per request regardless of chunk size, ≈2x the connection interval.

| Size | Write time | Write rate | Verify |
|---|---|---|---|
| 1 MiB (1,048,576 B) | 468.8s (7m 49s) | 2236.8 B/s | full readback, byte-exact MATCH |
| 3 MiB (3,145,728 B) | 1411.0s (23m 31s) | 2229.5 B/s | full readback, byte-exact MATCH |
| 8 MiB (8,388,608 B) | 3770.1s (62m 50s) | 2225.0 B/s | spot-check (start/mid/end), PASSED |
| 29 MiB (30,408,704 B) | 13704.8s (3h 48m 25s) | 2218.8 B/s | spot-check (start/mid/end), PASSED |

The read path, for comparison (M1/M2, same watch, same session): 1394.7-1413.3 B/s at the
phone-app's default chunk_len=128, or up to 2163.3 B/s at chunk_len=201 (the largest chunk the
firmware actually honors at this MTU -- see M2). Write is very slightly *faster* than read at a
comparable chunk size, both bound by the same ~90ms round trip. *(On firmware 1.4.0 that last
sentence no longer holds: read reaches 4.2 kB/s at chunk_len=4096, roughly 2x write, because a
single request is no longer capped at one notification. Write remains ~2.2 kB/s.)*

**A 3 MiB map pack over this channel takes ~23.5 minutes, not the optimistic "0.9 minutes"
case and not quite the full pessimistic "20 minutes" either** -- it lands just past the bad end
of the range the originating prompt named. See §4 below for what this means for the four
decisions in the prompt's §1.

1 M runs behind each number above (this was expensive to run -- ~5 hours of continuous BLE
transfer across M5 alone -- so each size was run once, not repeated 3x like M1). Every number
here is the honest result, including the ~4-hour 29 MiB run; nothing was retried to get a
better one. Full byte-exact verification (1 MiB, 3 MiB) vs. spot-check verification (8 MiB,
29 MiB) was a deliberate time tradeoff, not an oversight -- see §3, M5.

## Method

- Hardware: UNA Watch 403795 (`E8:DF:D5:49:4C:40`), paired/bonded via BlueZ on Linux
  (`endeavouros`, kernel 7.1.4-arch1-1), Bluetooth adapter `AC:12:03:D7:FC:5C`.
- **USB was unplugged for the entire investigation** -- confirmed via `lsusb`/`mount`/`lsblk`
  before starting; no USB-MSC mount was active at any point, so the documented USB+BLE write
  collision risk (SDK docs, and independently rediscovered on `spike/rawtiles-device-proof`
  per this project's own memory) does not apply here.
- Client: `../2026-07-29-hardware-config-recovery/prototype/una_ble_client.py`, extended this
  session with `write_file()`, a large-chunk reassembly fix for `read_file()`, and a hardcoded
  write-path allowlist (`WRITE_PATH_ALLOWLIST_PREFIXES = ("/Apps/HelloWorld/ble_bench_",)`).
  Connection handling used `../2026-07-29-hardware-config-recovery/prototype/auto_connect_pull.py`
  and `auto_pair.py` (event-driven connect/pair, pre-existing from the read-path investigation)
  -- BlueZ's plain `bluetoothctl connect` reliably lost the race against the watch's short
  advertising window; on one occasion the bond itself was desynced (watch showing a fresh
  pairing prompt while BlueZ still reported it bonded) and had to be re-paired from scratch via
  `bluetoothctl remove` + `auto_pair.py`.
- All scripts in this directory (`bench_m1.py` .. `bench_m5.py`, `debug_*.py`, `m6_*.py`,
  `verify_write.py`, `spot_verify.py`, `probe_delete.py`, `cleanup.py`) are throwaway
  investigation tooling, kept for reproducibility, not meant as a polished library.
- All writes targeted `/Apps/HelloWorld/ble_bench_*` -- HelloWorld is a pre-existing throwaway
  dev/test app directory nothing else on the watch reads from (same directory used for
  USB-side scratch probes in earlier sessions, per project memory). `check_write_path_allowed()`
  in `una_ble_client.py` hard-refuses any path outside that prefix; every write/delete script in
  this bundle calls it before touching the device. `0:/ble.ota` (the firmware OTA staging path)
  was never targeted at any point.
- **A full copy of the watch's read-accessible data was not re-pulled before this session**
  because the read path was already exhaustively validated (byte-exact `.fit` reads with CRC-16
  match) in the prior investigation, and this session never wrote to any path holding real user
  data -- only to the throwaway HelloWorld scratch prefix. All 23 scratch files created were
  deleted at the end (§ Cleanup below); the watch's storage was left as found modulo one 3000-byte
  leftover.
- M3's connection-interval capture required root (`sudo btmon`); the user ran it directly in
  their own terminal per the assistant's request (passwordless sudo was not available, and a
  password cannot be safely relayed through this session) -- see `raw/m3_hci_connection_interval.txt`.
- Raw timing data for every measurement lives in `raw/`, one file per M-step, JSONL where the
  data is structured/repeatable and plain-text terminal transcripts where it's a one-off probe.

## Verdict per measurement

### M1 — baseline read throughput + MTU: CONFIRMED
1394.7-1413.3 B/s (mean 1401.0 B/s), 3/3 runs, chunk_len=128, against a real 63,456-byte
activity `.fit` file (`/Apps/Running/Activity/202608/activity_20260804T161810.fit`).
MTU=220 (from `AcquireNotify`'s reply, previously captured but unused).
Raw: `raw/m1_read_baseline.jsonl`.

### M2 — round-trip-bound vs. bandwidth-bound: CONFIRMED, round-trip-bound
Per-request latency stayed flat at ~91-93ms across every working chunk_len (128/180/201);
throughput scaled almost exactly linearly with chunk_len (128->1391 B/s, 180->1976 B/s,
201->2163 B/s -- ratios track the chunk-size ratios within ~2%). The connection interval
(M3) is the actual multiplier, not the ATT payload size.

Also found and fixed a real bug in `read_file()`'s large-chunk handling, and found a real
firmware bug along the way: at MTU=220 the firmware clamps its advertised `real_chunklen` to
`MTU-16=204` regardless of what's requested, but only ever delivers `MTU-16-3=201` bytes in the
single notification and never sends a continuation -- a request for `chunk_len` above 201 just
hangs. The "one notification may not carry one full response" premise trap named in the prompt
turned out true in spirit but not the anticipated shape: it's not multi-notification
fragmentation to reassemble, it's a firmware short-delivery bug with no continuation at all.
201 is the largest `chunk_len` that works cleanly at this MTU.
Raw: `raw/m2_chunk_sweep.jsonl`, `raw/m2_real_chunklen_clamp.txt`.

> **UPDATE 2026-08-18 — reported as [#272][i272] and FIXED in firmware 1.4.0.** Everything above
> describes **firmware 1.3.0**, which is what this whole investigation ran on. On 1.4.0 the clamp
> is correct and oversized chunks stream across follow-up notifications instead of hanging, which
> lifts the practical ceiling well past 201 and takes read throughput from 2.1 kB/s to 4.2 kB/s.
> The anticipated multi-notification shape *does* now occur — but each continuation carries its
> own header, not headerless payload. Full retest: [`../2026-08-18-fts-read-chunklen-fix/`](../2026-08-18-fts-read-chunklen-fix/).

[i272]: https://github.com/UNAWatch/una-sdk/issues/272

### M3 — connection parameters: CONFIRMED (interval), PLAUSIBLE (whether it can be shortened)
Connection interval = 45.00ms, latency = 0, supervision timeout = 420ms -- CONFIRMED via a live
HCI capture (`sudo btmon`) of the actual `LE Enhanced Connection Complete` event, not the
requested range. This is the granted value, and it's within the adapter's own requested
30-50ms policy range. It explains M2's ~90ms round trip almost exactly (2x interval).

**Not attempted: actively requesting a shorter interval.** BlueZ exposes no D-Bus API for a
central to request a GAP connection parameter update on a live connection, and doing it via a
raw HCI `LE Connection Update` command would need custom privileged tooling beyond a one-off
debugfs read -- judged out of proportion to the likely gain (45ms is already a fairly tight
interval; Apple's iOS accessory guidelines start around 15ms minimum, so there may be headroom,
but the peripheral has to *accept* a shorter interval too, and that's genuinely unknown).
**What would settle it:** either raw HCI tooling to issue `LE Connection Update` from the
central and observe what the watch grants, or evidence from the phone app's own connection
parameters (a second HCI capture of the real Una app's session would show what it negotiates).
Raw: `raw/m3_hci_connection_interval.txt`.

### M4 — write framing: CONFIRMED, with real corrections to the initial Adafruit-shaped hypothesis
Folded into `BLE-COMPANION-protocol-spec.md` §2.2 (new subsection) to the same standard as the
read path. Summary of what's real vs. what the naive "read/listdir are byte-exact Adafruit
clones, so write probably is too" starting hypothesis got wrong:

- Opcodes 0x20 (start) / 0x21 (device pacing ack) / 0x22 (data) — **CONFIRMED**, byte layout
  matches Adafruit's reference exactly for the header shapes.
- `free_space` in the ack is **not** an MTU-aware buffer hint (**CONFIRMED wrong** vs. naive
  Adafruit reading) -- it just echoes bytes-remaining. The client must independently cap each
  0x22 payload to `ATT_MTU - 3 - 12` bytes or the underlying D-Bus `WriteValue` call fails.
- The 4th field of a 0x22 packet must be the *actual attached payload length*, not the stale
  `free_space` value from the previous ack (**CONFIRMED**: echoing a mismatched value gets a
  2-byte `[0x22, 0x04 ERROR_PROTOCOL]` rejection).
- `current_offset` in **every** 0x21 data ack reads back `0`, not a continuation cursor
  (**CONFIRMED the hard way** — an earlier version of this code trusted it and looped forever,
  see `raw/m4_infinite_loop_bug.txt`). The client's own running byte count is the only correct
  position tracker for a data ack.
- The 0x20 **start** ack is different: it *does* report a meaningful `current_offset`/
  `free_space` when resuming an existing partial file (0 for brand-new, true byte count for an
  existing partial write) -- see M6.
- The 2-byte `[echoed_opcode, error_status]` envelope, already documented for the read path's
  malformed-request case, is confirmed protocol-wide: seen identically for write-start
  rejection and for delete rejection.
- Bonus, not asked for but free once the framing was understood: **0x30/0x31 confirmed as
  DELETE/DELETE_STATUS**, closing the spec's previously-"still-unexplained" `0x30` open item
  from the original phone capture.
Raw: `raw/m4_write_framing_probes.txt`, `raw/m4_infinite_loop_bug.txt`, `raw/m4_multichunk_smoke.txt`.

### M5 — write throughput at realistic sizes: CONFIRMED
See the headline table. Throughput is flat within <1% across the entire 1 MiB-29 MiB range
(2218.8-2236.8 B/s) -- no degradation with offset or file size, and it completes reliably,
including the ~3h48m 29 MiB transfer with zero connection drops.

1 MiB and 3 MiB were verified byte-exact with a full readback (doubling their wall-clock cost).
8 MiB and 29 MiB were spot-checked instead (start/middle/last-200-bytes, `spot_verify.py`) --
a full readback would have cost another ~1h / ~4h respectively and mostly re-proven what the
smaller sizes' full verification already established about correctness at this flat rate. This
tradeoff was made explicitly (see the session's own back-and-forth on total time budget before
committing to running all four sizes for real) and is disclosed here, not silently substituted.
Raw: `raw/m5_write_sizes.jsonl`, `raw/m5_verify.jsonl`, `raw/m5_spot_verify_8mib_29mib.txt`.

### M6 — failure-mode behavior: CONFIRMED, all three sub-questions
- **Resume works** when the client supplies the file's true current offset -- byte-exact final
  result confirmed end-to-end (start a write, stop it 3 chunks in, reconnect, resume from the
  correct offset, finish, read back, MATCH).
- **A partial file is truncated exactly at the last successfully-written offset** -- not
  zero-padded to the declared total, not absent, not garbage; content up to that point is
  correct.
- **The firmware validates almost nothing.** The only rejection observed anywhere is a nonzero
  0x20 start offset on a path that doesn't exist as a file yet (`ERROR_PROTOCOL`). Once *any*
  file exists at a path -- even a 1-chunk partial one -- the device accepts a resume or a
  mid-session data chunk at **any** offset, in **any** order, and silently zero-fills whatever
  gap results. Sending a data chunk at offset 1000 with no prior chunk covering 205-999, in a
  still-open session with no reconnect, was simply accepted. **There is no integrity or
  ordering guarantee at the firmware level whatsoever** -- any resume/integrity guarantee for a
  real companion has to be built entirely client-side (or come from an outer container
  format's own checksums, which is exactly the `rawtiles` C6 question this investigation was
  commissioned to unblock -- see §4 below).
Raw: `raw/m6_failure_modes.txt`.

### Cleanup (not one of M1-M6, but a real finding worth keeping)
`deleteHandler` (0x30/0x31) works and was used to remove all 23 scratch files this
investigation created, including the four M5 files (~41 MiB reclaimed). 22/23 succeeded; the
one failure (`ble_bench_m6_outoforder.bin`) was the file from M6's out-of-order-chunk test,
whose write session was deliberately left open and never completed -- consistent with delete
refusing a file that still has an open write session (`status=0x02 ERROR` vs. `0x01 OK` for a
cleanly-completed one). That one 3000-byte file is the only leftover on the watch from this
investigation.

## Premise-trap outcomes (§4 of the originating prompt)

- **"The read path is not the write path"** — held up completely. The write framing needed
  three real corrections beyond the read-path-derived Adafruit hypothesis (see M4).
- **"`chunk_len=128` is a client choice, not a firmware limit"** — confirmed; 201 works and is
  faster (M2). But a *real*, previously-unknown firmware limit was found at 202+: not a
  documented ceiling, an undocumented short-delivery bug. *(2026-08-18: fixed in fw 1.4.0 — the
  limit is gone entirely, and 4096 now works at 2x the throughput of 201.)*
- **"One notification ≠ one response" at large chunk sizes** — the anticipated shape (multiple
  notifications, needing client-side reassembly) never actually occurred; the real failure mode
  was single-notification short delivery, which reassembly code can't fix (see M2). *(2026-08-18:
  on fw 1.4.0 the anticipated shape finally does occur — but with a per-notification header, so
  the reassembly this prompt imagined would still have been the wrong code. Reassemble by each
  notification's declared offset, not by concatenating headerless remainders.)*
- **"BlueZ grants what the peripheral allows"** — M3 reports the granted 45ms interval from a
  live capture, not a requested value.
- **"A fast desktop-Linux number may not transfer to Android"** — still true and still
  unmeasured here. Every number in this document is a **Linux/BlueZ number**. Android's
  BlueZ-equivalent stack, GATT caching, and RPA-rotation handling are meaningfully different
  (this project's own `GADGETBRIDGE-scoping.md` already flags this for the read path); nothing
  here should be assumed to transfer to a phone companion without separate measurement.
- **"Advertising windows are short"** — very much still true; getting a stable connection at
  all required event-driven connect tooling (`auto_connect_pull.py`) and, once, a full
  bond-desync recovery (remove + re-pair). This cost real session time and is worth knowing
  about for anyone reproducing this.

## Four decisions, one paragraph each

**Is transfer phone-first or desktop-only?** At ~2.2 kB/s sustained, a 3 MiB city pack takes
~23.5 minutes and a 29 MiB metro region takes ~3h48m over this exact channel (real numbers,
not extrapolation). That is squarely in the "not a product" range the originating prompt named
-- 20-minute-plus waits with the watch pinned to one BLE connection the whole time are not
something a user will sit through for a routine map update. This channel, as measured, does not
support desktop-direct map delivery at realistic pack sizes. Nothing here rules out a phone
being faster (Android is unmeasured, per the premise-trap note above), but the *desktop* side
of "desktop-only" is now a measured no, not a guess -- if UNA's own phone app achieves
materially better throughput, it's doing something this investigation didn't (different
framing UNA controls that we don't, e.g. writes over a wired conduit, or connection parameters
this Linux central couldn't obtain).

**What size should catalog packs default to?** At this rate, treat the ceiling as a
throughput-times-patience budget: ~2.2 kB/s means roughly 130 kB/min. A pack a user might
plausibly wait for (say, 2-5 minutes) tops out around 260 kB-1.3 MB -- well under even the
smallest "city pack" size named in the source prompt (~3 MiB). If packs must stay at their
currently-planned sizes, the delivery mechanism needs to change (see the `rawtiles` question
below), not the pack size alone.

**Does `rawtiles` need a verifiable prefix?** Given the M5 result (writes complete reliably,
byte-exact, even at 29 MiB over 3h48m with zero drops) *and* the M6 result (the firmware does
zero offset/order validation and will silently zero-fill any gap from a bad resume), the answer
splits in two: resume is not needed for *reliability* (transfers complete cleanly when run to
completion), but a verifiable prefix or per-chunk integrity check absolutely *is* needed for
*correctness*, because nothing in the firmware would ever catch a client-side offset-tracking
bug, a lost ack, or a resumed-from-the-wrong-place transfer -- it would just produce a
silently-corrupted file that looks complete. Given transfers already run long enough
(minutes to hours) that a mid-transfer failure is a real, not theoretical, risk, C6's chunk
size and integrity granularity should be sized around this channel's actual round-trip cost
(~90ms per ~205-byte chunk) so that resuming from a checkpoint doesn't mean re-verifying an
entire multi-hour transfer.

**What do we ask UNA for?** "Please document the write path" now comes with concrete, measured
asks rather than a vague request: (1) the actual usable write throughput on this firmware caps
out around 2.2 kB/s regardless of client tuning -- is there a faster path we're missing, or is
this genuinely the ceiling? (2) the firmware performs no write-offset or chunk-order validation
at all -- is that intentional, or would UNA consider adding it, since a companion app has no
way to detect a corrupted resume without one? (3) ~~the `real_chunklen` accounting bug found in
M2 (advertises `MTU-16` but only delivers `MTU-16-3`) affects the *read* path too and would be
a cheap, low-risk firmware fix that meaningfully raises read throughput for every client,
UNA's own phone app included.~~ **Asked and answered** — filed as [#272][i272] on 2026-08-08,
fixed in firmware 1.4.0, validated on hardware 2026-08-18. The predicted throughput win was
real: 2.1 kB/s -> 4.2 kB/s. UNA's phone app still requests `chunk_len=128` (1.4 kB/s), so it
has not yet claimed that win itself.

Asks (1) and (2) remain open and unanswered.
