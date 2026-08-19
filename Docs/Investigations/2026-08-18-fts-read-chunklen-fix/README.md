# FTS read path: validating the `real_chunklen` fix on firmware 1.4.0

**Date:** 2026-08-18 · **Firmware:** 1.4.0 (hardware 3.1) · **MTU:** 220 · **Verdict: FIXED**

Upstream issue [UNAWatch/una-sdk#272][issue] — reported from this investigation's 2026-08-07
write-path pass — is **confirmed fixed on firmware 1.4.0**, on real hardware. Both of the
maintainer's claims hold, and the fix roughly **doubles usable read throughput**.

[issue]: https://github.com/UNAWatch/una-sdk/issues/272

## What was broken (firmware 1.3.0)

The whole-file read (`0x10`/`0x11`) clamped the advertised `real_chunklen` to `MTU-16` (the FTS
response header) without also subtracting the 3-byte ATT opcode+handle overhead every Handle
Value Notification consumes. At MTU=220 it advertised up to 204 bytes but only ever delivered
201, and sent no continuation — so any `chunk_len` between 202 and 204 made a header-trusting
client hang forever. 201 was the practical ceiling.

Original evidence: [`../2026-08-07-ble-write-path/raw/m2_real_chunklen_clamp.txt`](../2026-08-07-ble-write-path/raw/m2_real_chunklen_clamp.txt).

## What 1.4.0 does

**Claim 1 — the clamp is correct.** `real_chunklen` never exceeds the bytes actually present.
Re-running the *original* repro script unchanged (`../2026-08-07-ble-write-path/debug_chunk.py`):

| requested `chunk_len` | 1.3.0 | 1.4.0 |
|---|---|---|
| 201 | 201 / 201 delivered ✓ | 201 / 201 ✓ |
| 202 | 202 advertised / 201 delivered → **hang** | 201 + 1 = 202 ✓ |
| 204 | 204 advertised / 201 delivered → **hang** | 201 + 3 = 204 ✓ |
| 244 | clamped to 204 / 201 delivered → **hang** | 201 + 43 = 244 ✓ |
| 512 | clamped to 204 / 201 delivered → **hang** | 201 + 201 + 110 = 512 ✓ |

Raw: [`raw/boundary_sweep_fw140.txt`](raw/boundary_sweep_fw140.txt).

**Claim 2 — larger chunks stream instead of clamping.** One `0x10` at `chunk_len=4096` now
returns the remainder across follow-up notifications rather than truncating. A full
177,756-byte `.fit` read in 44 requests / 912 notifications came back byte-exact, with zero
header-accounting mismatches, every request delivering exactly `min(chunk_len, remaining)`, and
the file's own trailing FIT CRC-16 validating (`0xFEB6` stored = computed).
Raw: [`raw/full_read_177k_crc.txt`](raw/full_read_177k_crc.txt),
[`raw/stream_small_vs_reference.txt`](raw/stream_small_vs_reference.txt).

## The framing change this implies

This is the part worth carrying forward, because it is **not** what this investigation predicted.

Every continuation notification carries **its own 16-byte header**, whose `offset` and
`real_chunklen` describe just that notification — they are not headerless raw payload appended
to the first one. The 1.3.0-era `read_file()` in
[`../2026-07-29-hardware-config-recovery/prototype/una_ble_client.py`](../2026-07-29-hardware-config-recovery/prototype/una_ble_client.py)
had a reassembly branch built on the headerless hypothesis. That branch was never exercised —
1.3.0 sent no continuations at all — and on 1.4.0 it is unreachable, since `real_chunklen` now
always equals the payload present. It has been rewritten to accumulate whole framed
notifications, and to advance `offset` by **bytes actually delivered** rather than by the
advertised `real_chunklen`. That distinction is what makes the client correct on both
firmwares: on 1.3.0 an oversized `chunk_len` now costs one 8s stall per chunk instead of
hanging outright.

## Throughput: the fix is worth ~2x

Same file, same session, updated `read_file()` — all three agree byte-for-byte
([`raw/read_file_throughput_177k.txt`](raw/read_file_throughput_177k.txt)):

| `chunk_len` | throughput | note |
|---|---|---|
| 128 | 1.4 kB/s | what UNA's phone app requests |
| 201 | 2.1 kB/s | the old 1.3.0 ceiling |
| 4096 | **4.2 kB/s** | only possible post-fix |

128 and 201 reproduce the 2026-08-07 pass's 1391 B/s and 2163 B/s almost exactly, which is a
useful cross-check that nothing else about the channel moved between firmware versions. Read is
now ~2x the write path's ~2.2 kB/s ceiling — **reads are no longer the bottleneck for map-pack
work; writes still are.**

## Limitations

- **Only MTU=220 was exercised.** Whether the new clamp is genuinely MTU-aware or merely lands
  on 201 is *untested*. Confirming it needs a second negotiated MTU, which means changing BlueZ
  config and re-establishing the bond. Do not assume MTU-awareness without that test.
- Single device, single session. Nothing here speaks to 1.4.0 stability over long transfers —
  the longest run was ~42s. The write path's 29 MiB / 3h48m endurance result has **not** been
  re-run on 1.4.0.
- The `0x12` continuation opcode (spec §2.2) was not re-tested; all reads here repeat `0x10`.

## Reproducing

The watch must already be paired/bonded (see `../2026-07-29-hardware-config-recovery/prototype/`).
Pass the device address as the first argument.

```sh
python3 read_dis.py <addr>                          # confirm firmware revision first
python3 verify_stream.py <addr> <remote-file> 4096  # stream vs a chunk_len=128 reference
python3 verify_large.py  <addr> <remote-file> 4096  # full read + FIT CRC-16 check
python3 check_client.py  <addr> <remote-file>       # read_file() agreement across chunk sizes
python3 timed_read.py    <addr> <remote-file> 128 201 4096   # throughput by chunk size
```

`verify_large.py` and `timed_read.py` assume a `.fit` file (the CRC check does); the others work
on any file. All are read-only — none of them writes to the watch.
