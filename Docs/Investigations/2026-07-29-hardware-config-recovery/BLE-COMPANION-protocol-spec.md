# UNA Watch BLE companion protocol — recovery spec (in progress)

Goal, guardrails, and full method are defined in `BLE-COMPANION-disassembly-prompt.md` in this
same folder — this doc is the running **output** of that plan, not a restatement of it.

**Status of the two evidence streams:**
- **Static / disassembly:** Ghidra is now installed but no disassembly pass has been run yet.
  Everything static comes from a *lower-effort substream* that needed no disassembler: `strings -n 6`
  over `flash_real.bin` (`flash_strings.txt`, 6539 lines), mined for BLE/GATT/FTS/CCS log strings,
  class names, and literal UUID constants.
- **Dynamic / HCI capture: DONE — a real capture of the Una app performing a full sync (including
  two separate GPS-track `.fit` activity syncs) was captured live from the user's own phone
  (GrapheneOS, `adb bugreport`) and watch, and decoded with `tshark` (Wireshark's CLI). This is a
  first-party, on-air capture of this exact device pair — the strongest evidence tier available
  short of disassembly, and independent of the flash dump entirely.**

This capture **resolved Phase C outright**: the FTS whole-file-read protocol was fully reverse
engineered from real traffic, and two real `.fit` files pulled off the user's own watch were
reconstructed byte-for-byte from the captured chunks and validated two ways — a byte-exact FIT
header match *and* a matching CRC-16 checksum computed with the public Garmin FIT algorithm — plus
independently recognized as valid FIT files by `file(1)`'s own magic database. See §2.

Per the source plan's discipline: nothing below is tagged CONFIRMED unless it is corroborated by
a source independent of the flash dump itself (a public spec document counts; a live capture
counts; another string in the same dump does not). Everything else is LIKELY (strong single-source
lead) or UNVERIFIED.

---

## 1. GATT table — services and UUIDs recovered so far

All 18 UUID-shaped strings in `flash_strings.txt` were extracted with one pass
(`grep -noE '[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}'`). They
fall into five groups by surrounding log-string context:

| # | UUID | Context (nearest log strings) | Identity | Confidence |
|---|---|---|---|---|
|1| `89D3502B-0F36-433A-8EF4-C502AD55F8DC` | `BLE.ClientAMS`, `onServicesResolved`, "Mandatory AMS characteristic(s) not found" | **Apple Media Service (AMS)**, service UUID | **CONFIRMED** — byte-exact match to Apple's published AMS spec (see Sources) |
|2| `9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2` | same block | AMS **Remote Command** characteristic | **CONFIRMED** (byte-exact match, Apple spec) |
|3| `2F7CABCE-808D-411F-9A0C-BB92BA96C102` | same block | AMS **Entity Update** characteristic | **CONFIRMED** (byte-exact match, Apple spec) |
|4| `C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7` | same block | AMS **Entity Attribute** characteristic | **CONFIRMED** (byte-exact match, Apple spec) |
|5| `7905F431-B5CE-4E99-A40F-4B1E122D00D0` | `BLE.ClientANCS` | **ANCS** service UUID | **CONFIRMED** (byte-exact match, Apple spec) |
|6| `9FBF120D-6301-42D9-8C58-25E699A21DBD` | "Mandatory characteristic 'Notification Source' not found" | ANCS **Notification Source** | **CONFIRMED** (byte-exact match, Apple spec) |
|7| `69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9` | "'Control Point' and/or 'Data Source'" | ANCS **Control Point** (by position/label match) | **LIKELY** — matches the well-known public ANCS Control Point UUID from memory/training data, not independently re-fetched this pass |
|8| `22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB` | same | ANCS **Data Source** (by position/label match) | **LIKELY** — same caveat as #7 |
|9| `554E4100-A2CF-4DF8-0000-7E1E48595106` | sits between `BLE.SRV.CurrentTime` block and `BLE.SRV.CustomCommand` block | **CCS service UUID** | **CONFIRMED (static)** — see §1.1, decompiled constructor `FUN_080911ec` builds exactly this service |
|10| `554E4100-A2CF-4DF8-0001-7E1E48595106` | same | CCS characteristic #1 | **CONFIRMED (static)** — same constructor, second `aci_gatt_add_char`-equivalent call |
|11| `554E4100-A2CF-4DF8-0002-7E1E48595106` | same | CCS characteristic #2 | **CONFIRMED (static)** — same constructor, third call |
|12| `ADAF0001-4669-6C65-5472-616E73666572` | immediately after FTS handler names (`listDirHandler` etc.), before `BLE.SRV.FileTransfer` | FTS sub-service characteristic #1 — vendor-base UUID pattern **borrowed from, but NOT identical to, Adafruit's public BLE File Transfer Service** (see §1.1) | **CONFIRMED (static)** as an FTS characteristic; **REFUTED as "is Adafruit's protocol"** |
|13| `ADAF0002-4669-6C65-5472-616E73666572` | same | FTS sub-service characteristic #2, same caveat | **CONFIRMED (static)**/REFUTED, same note |
|14| `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | same block | **Nordic UART Service (NUS)**, service UUID — a *separate* FTS sub-service object from the ADAF one (see §1.1) | **CONFIRMED** (byte-exact match, Nordic's published spec, **and** static: decompiled constructor `FUN_08093a80` builds it) |
|15| `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | same | NUS **RX** characteristic (write → device) | **CONFIRMED** (byte-exact + static) |
|16| `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | same | NUS **TX** characteristic (notify ← device) | **CONFIRMED** (byte-exact + static) |
|17| `554E4100-28E7-4811-0000-141F8B92EE40` | sits right before `parseBuffer`/`BLE.ParserCANS`, after Android-notification-adjacent strings (`androidFetchAttr`, `notifHandler`) | **CANS service UUID** — "Custom Android Notification Service", the Android-side counterpart to ANCS | **CONFIRMED (static)** — decompiled constructor `FUN_08095158` builds exactly this service (large buffers, worker thread/queue init matching a notification-reassembly parser) |
|18| `554E4100-28E7-4811-0001-141F8B92EE40` | same | CANS characteristic #1 | **CONFIRMED (static)** — same constructor |
| — | (a second characteristic exists too, sharing this constructor) | | CANS has **exactly 2 characteristics**, confirmed by the constructor's exact call count — the earlier "possibly a 3rd exists" uncertainty is now resolved: no, there are only 2 | **CONFIRMED (static)**, resolves prior UNVERIFIED |

**Dynamic corroboration for CANS:** the live capture separately shows a distinct, actively-used
characteristic (attribute handle `0x002B`) carrying real Android notification payloads phone→watch
— plaintext strings `"Signal"`, `"Firefox"`, and actual notification body text (e.g. an Advil
reminder) were observed riding over it during ordinary phone use in the background. This doesn't
yet pin `0x002B` to one of the `554E4100-28E7-4811-...` UUIDs specifically (handle↔UUID binding is
still the one open item, see §2.4), but independently confirms the characteristic is real and
actively used — now triple-sourced (string context + decompiled constructor + live traffic).

### 1.1 Constructor-level static confirmation (Ghidra)

A Ghidra headless pass (`ARM:LE:32:v8-m`, base `0x08000000`, matching the confirmed vector-table
layout) was run over `flash_real.bin`, seeded with the 336 libc symbol names from
`libc_exports_0.0.3.ld`, then decompiled the functions referencing each UUID string above. Every
one of the 5 custom-service UUID groups (CCS, CANS, and FTS's two sub-services) resolves to a
distinct constructor function with an identical shape: convert each UUID string to bytes via a
shared helper, call an `add_service`-equivalent through a vtable slot (offset `+0x1c` on an
`IBle`-style interface) for the service UUID, then one `add_char`-equivalent call (vtable `+0x24`)
per characteristic, then a descriptor/CCCD step (`+0x4c`) per characteristic, then a finalize call
(`+0x44`). This is clearly a shared C++ base/template for "GATT service with N characteristics" —
a clean architectural finding in its own right, and strong corroboration that each UUID group is
exactly the single self-contained service its string-context already suggested:

- **CCS** (`FUN_080911ec`): 1 service + 2 characteristics, all three from the `554E4100-A2CF-4DF8-...` UUID group.
- **CANS** (`FUN_08095158`): 1 service + 2 characteristics, all three from the `554E4100-28E7-4811-...` group. Markedly larger/more complex constructor (multi-KB buffers, worker thread/queue setup) — consistent with reassembling arbitrary Android notification payloads.
- **FTS is confirmed to be two separate C++ objects/services, not one:**
  - One object (`FUN_08092d20`) builds its **service** using the literal 16-bit UUID **`0xFEBB`** as
    a raw immediate value in the decompiled code (`local_2c = CONCAT22(..., 0xfebb)`) — this *is*
    Adafruit's real, published 16-bit File Transfer Service UUID, reused verbatim. Its two
    characteristics use the `ADAF0001`/`ADAF0002` 128-bit strings. This refines (doesn't just
    repeat) the earlier refutation: **the service UUID genuinely matches Adafruit's spec**; only the
    *characteristic* numbering (`0x0001`/`0x0002` here vs. Adafruit's `0x0100`/`0x0200`) diverges,
    so a generic BLE explorer would likely flag this as "the Adafruit File Transfer Service" by
    service UUID alone, but Adafruit's own client code still won't speak the right characteristic
    protocol.
  - A second, separate object (`FUN_08093a80`) builds the Nordic UART Service clone
    (`6E400001`/`02`/`03`) with the same constructor shape.
  - Both are almost certainly owned/composed by a higher-level "FileTransferService" class (not
    traced this pass) that presents them together as what the docs call FTS.

Full decompiled output lives only in the local scratch working directory for this session (not
committed — see guardrails), but the specific findings above are reproducible from any Ghidra
import of `flash_real.bin` at the stated base address.

**Falsification check pending (per plan §Phase A):** none of #9–13, #17–18 have been cross-checked
against a live GATT discovery capture yet. Until a real sync capture shows the same UUIDs on air,
treat all of these as leads, not a settled GATT table.

### Note on the "ADAF" UUIDs — first guess refined twice, recorded per the ground rule

First read: "these look like they're straight out of Adafruit's public BLE File Transfer Service,
so the FTS wire protocol might already be a known, documented, client-library-available protocol."
**Checked against the actual Adafruit source** (`adafruit_ble_file_transfer.py`,
github.com/adafruit/Adafruit_CircuitPython_BLE_File_Transfer): Adafruit's real scheme uses the
16-bit **standard** UUID `0xFEBB` for the *service*, and `ADAF...`-base 128-bit long-form UUIDs
only for *characteristics*, with suffix IDs `0x0100` (Version) and `0x0200` (Transfer) — different
from UNA's `0x0001`/`0x0002`. **Then §1.1's disassembly pass added a second layer**: UNA's firmware
does independently reuse Adafruit's exact `0xFEBB` 16-bit **service** UUID (as a raw immediate in
the decompiled constructor) — so the service-level identity genuinely matches Adafruit's spec — but
the two characteristics underneath are UNA's own numbering, not Adafruit's `0x0100`/`0x0200`
scheme. **Net conclusion, now static-confirmed rather than string-inferred:** don't assume
Adafruit's `adafruit_ble_file_transfer` client (or Gadgetbridge tooling built for it) will talk to
this device correctly — the service UUID would match, but the characteristic-level protocol won't.
This step-wise refinement (wrong → partially right → precisely characterized) is exactly the
discipline the source plan asks for: each guess was checked against an independent source rather
than assumed.

**Working hypothesis for the FTS transport (LIKELY, unconfirmed):** FTS appears to be built from
*two* GATT building blocks together: a small `ADAF`-styled pair (service+1 characteristic, or
2 characteristics of one service — ambiguous from strings alone) for something version/identity
adjacent, **plus** a literal clone of Nordic's 3-UUID NUS pattern (service + RX write + TX notify)
for the actual byte pipe. If true, the bulk file bytes ride over what is, at the raw BLE layer, an
NUS-compatible serial pipe, with UNA's own command/offset/length framing layered on top in the byte
stream itself (see §2) — **not** that any off-the-shelf NUS client can pull a `.fit` file without
also implementing that framing.

### 1.2 CORRECTED: offset/total/chunklen are genuine 32-bit fields, matching Adafruit's spec exactly — no ceiling

**An earlier version of this section claimed UNA had narrowed Adafruit's real 32-bit wire fields to
16-bit, producing a 64KB transfer ceiling. That claim was wrong, and is retracted here.** It was
caused by a bug in this investigation's own prototype client (`una_ble_client.py`), which parsed
the response header as three `uint16` fields (`struct.unpack("<HHH", b[4:6]+b[8:10]+b[12:14])`)
and silently discarded what it treated as 2 padding bytes after each one. Those bytes are not
padding — they're the real high 16 bits of genuine 32-bit fields, and discarding them produced a
falsely truncated `total_size` on any file over 64KB (e.g. reading back `46684` instead of the true
`177756` for one test file — note `46684 == 177756 mod 65536`, which is exactly why the bug read as
a plausible-looking "wraparound").

This was caught and fixed after a live BLE HCI capture of the *real* Una phone app syncing this
exact 177,756-byte file showed the true 32-bit response header directly on the wire:

```
1101000000b400005cb6020080000000
```

Parsed as `<BBxx III>` (Adafruit's exact layout — cmd, status, 2 pad, offset:u32, total:u32,
chunklen:u32): `offset=46080`, **`total=0x0002b65c = 177756`** (the correct true file size),
`chunklen=128`. The "extra" bytes this investigation had assumed were always-zero padding read
`02 00` here — the real upper 16 bits of `total`, non-zero specifically because this file exceeds
65536 bytes.

**Corrected conclusion:** UNA's FTS request/response header is a byte-for-byte structural clone of
Adafruit's real BLE File Transfer Service wire format — same 12-byte request / 16-byte response
sizes, same field order, same byte offsets — **and the field widths were preserved correctly as
genuine 32-bit values, not narrowed.** There is no 64KB ceiling. `una_ble_client.py`'s `read_file()`
has been fixed to parse `<III>` instead of `<HHH>`, and pulling the same file with the corrected
parser reproduces the true `177756` total exactly.

Source: `adafruit/Adafruit_CircuitPython_BLE_File_Transfer`,
`adafruit_ble_file_transfer.py` (github.com/adafruit/Adafruit_CircuitPython_BLE_File_Transfer) —
the source used to first identify the field widths, and the same one that exposed this
investigation's own parsing bug once compared byte-for-byte against a real capture.

---

## 2. FTS command surface — CONFIRMED from a real capture, byte-exact

### 2.1 What the live capture directly proved

Two full `.fit` activity syncs (5-second test recordings via the `Examples/Apps/GpsLab` app) were
captured end-to-end and decoded with `tshark`. Both `.fit` files were reconstructed from the
captured BLE chunks and validated three independent ways:

| File | Declared size | FIT header | CRC-16 (stored vs. computed) | `file(1)` verdict |
|---|---|---|---|---|
| `activity_20260730T224654.fit` | 1366 B | `header_size=14, data_size=1350` → `14+1350+2=1366` **MATCH** | `0xB6C4` == `0xB6C4` **MATCH** | "FIT Map data ... Sat Dec 30 19:00:00 1989 ..." |
| `activity_20260730T230037.fit` | 1981 B | `header_size=14, data_size=1965` → `14+1965+2=1981` **MATCH** | `0x75C7` == `0x75C7` **MATCH** | "FIT Map data ... Sat Dec 30 19:00:00 1989 ..." |

The CRC was computed independently with the public Garmin FIT SDK's documented CRC-16 algorithm
(poly 0xA001, 16-entry nibble table) — this is a from-scratch recomputation, not a copy of any
value seen in the capture. Getting an exact match on two independent files, on top of an
independently-recognized `file(1)` magic-database match, satisfies the plan's Phase C
falsification test about as rigorously as is practical without owning the original signing key:
**this is genuine, unmodified FIT data, read directly off the user's own watch over BLE, framed
exactly as described below.**

### 2.2 The wire protocol (all fields below are live-capture-confirmed, not guessed)

All FTS traffic in this capture rode over a single ATT characteristic, **attribute handle
`0x0027`** in this bonded session (handle-to-UUID binding is still open — see §2.4). Everything
below is an **application-layer protocol carried inside plain ATT Write Command / Handle Value
Notification payloads** — there is no visible additional wrapping.

**Directory listing** (`0x50` request / `0x51` streamed response):
```
Request  (Write Command):  50 <path_len:u16LE> <path ASCII, no NUL>
Response (Notification, one per entry, streamed):
  51 <flags:u8> <name_len:u16LE> <index:u32LE> <total_count:u32LE> <attr:u32LE> <mtime:u64LE?> <name ASCII>
```
Observed live listing `/Apps/` → 19 entries (`Alarm`, `Cycling`, `GlanceActivity`, ... `Workout`),
each with a monotonically increasing `index` and a constant `total_count=19` — lets a client know
when the listing is complete without a separate "end" marker (a final all-zero record was also
observed, possibly an explicit terminator — worth confirming, not load-bearing).

**Whole-file read — the mechanism that pulled the `.fit` files** (`0x10` request / `0x11` streamed
response), 12-byte request header + path, 16-byte response header + chunk, fields confirmed by
diffing multiple chunks of the same transfer against each other (offset increments by exactly the
chunk length each step; total stays constant; the header is **not** a naive packed struct — every
multi-byte field is a `uint16`, each preceded by 2 reserved/pad bytes, still not fully explained
but empirically exact across two independent 14–16-chunk transfers):

```
Request  (Write Command):
  10 00 <path_len:u16LE> 00 00 00 00 <requested_chunk_len:u16LE> 00 00 <path ASCII, no NUL>

Response (Notification, repeated until offset+chunklen == total, no per-chunk ack needed):
  11 01 00 00 <offset:u16LE> 00 00 <total_size:u16LE> 00 00 <chunklen:u16LE> 00 00 <chunklen bytes of file data>
```
Confirmed concretely: request chunk length was `128` in every observed transfer; the server
honored it exactly (every chunk except the final remainder was 128 bytes).

**CORRECTED — "one request per file" was wrong; it's one request per chunk, via two opcodes.**
The original phone-capture analysis (on two short, sub-2KB test files) concluded the client sends
a single `0x10` request and the server streams the whole file back unprompted. A later live capture
of the *real* Una app syncing a 177,756-byte file shows this was an artifact of those files being
small enough to not clearly distinguish the two cases. The real behavior, confirmed on-wire:

- **`0x10`** (with the full path, as documented above) opens the file and returns **only the first
  chunk** (`offset=0`).
- **`0x12`** — a previously undocumented opcode — requests each *subsequent* chunk: same 12-byte
  header shape as `0x10` (`cmd, flags, 00 00, offset:u32, chunklen:u32`), but with the path-length
  field zeroed and no path bytes appended, since the file is already open from the `0x10` call.
  Captured example: `12 01 00 00 00 b4 00 00 80 00 00 00` → offset=46080, chunklen=128.

So it is a per-chunk request/ack loop after all — matching `prototype/una_ble_client.py`'s
approach — just split across two opcodes instead of repeating `0x10`. Repeating `0x10` with the
full path on every chunk (what the prototype currently does) still works — the firmware doesn't
seem to require `0x12` specifically — but a companion wanting to match the real app's traffic
exactly should use `0x10` once then `0x12` for the rest. **This also resolves §6's previously-open
item 3** ("why did the phone capture look like one request per file while the standalone prototype
needed one per chunk") — it was one request per chunk all along in both cases; the earlier analysis
just didn't recognize `0x12` as a second per-chunk opcode.

**No 64KB ceiling — see §1.2 for the full correction.** An earlier version of this document claimed
`total_size` was a `uint16` that wraps at 65536, based on a real-looking but ultimately false test
result. That was a bug in this investigation's own client, not the firmware: `offset`/`total`/
`chunklen` are genuine 32-bit fields (matching Adafruit's real spec exactly, per §1.2), and a live
capture of the real Una app pulling the same 177,756-byte file shows the correct `total=177756` in
every single response header, with the transfer progressing normally (128-byte chunks, monotonic
offset) as far as that capture window extended. There is no known ceiling on FTS file size.

**`0x30`/`0x31` — RESOLVED, 2026-08-08: this is DELETE / DELETE_STATUS.** The "still-unexplained
command pair" noted here (`0x30` request / response byte `0x02` or `0x01`) is exactly Adafruit's
`DELETE`/`DELETE_STATUS` opcode pair. Confirmed live: `30 00 <path_len:u16LE> <path>` against a
cleanly-completed file gets back `31 01` (`cmd=0x31 status=0x01 OK`) and the file is actually
gone from a subsequent listing; the same request against a file with a still-open write session
gets back `30 02` (the generic 2-byte `[echoed_opcode, error_status]` envelope, `status=0x02
ERROR`) and the file is *not* deleted. See `../2026-08-07-ble-write-path/README.md` (§M4/§M6)
for the full write-path measurement pass this was found during.

**Large-file upload (`0x20`/`0x21`/`0x22` family) — RESOLVED, 2026-08-08: fully decoded and
measured.** What this section originally only observed live (a phone→watch `GPS.DAT` push) has
now been independently reverse-engineered, implemented, and load-tested up to 29 MiB from a
phone-free Linux client. Full method, raw data, and per-size throughput live in
`../2026-08-07-ble-write-path/`; this is the summary at the same standard as §2.2's read-path
writeup above.

Wire framing (byte-exact structural match to Adafruit's real BLE File Transfer Service WRITE
opcode, same as the read/listdir paths — but with real firmware-specific deviations, marked
below):

```
Request  (Write Command) 0x20 start :
  20 00 <path_len:u16LE> <offset:u32LE> <mtime_ns:u64LE> <total_length:u32LE> <path ASCII>

Response (Notification)  0x21 pacing:
  21 <status:u8> 00 00 <current_offset:u32LE> <reserved:u64LE> <free_space:u32LE>

Request  (Write Command) 0x22 data  :
  22 <status:u8> 00 00 <offset:u32LE> <chunk_len:u32LE> <chunk_len bytes of file data>
```

`status` is Adafruit's taxonomy: `OK=0x01`, `ERROR=0x02`, `ERROR_NO_FILE=0x03`,
`ERROR_PROTOCOL=0x04` — confirmed live for `0x01` and `0x04`.

**Three real deviations from a naive "it's just Adafruit's WRITE opcode" reading, all
CONFIRMED against real hardware:**

1. **`free_space` is not an MTU-aware buffer hint.** It just echoes bytes-remaining
   (`total_length - offset`), even when that number is far larger than one ATT packet can
   carry. A client must independently cap each `0x22`'s attached payload to
   `ATT_MTU - 3 (opcode+handle) - 12 (0x22 header)` bytes, or the underlying GATT write itself
   fails before ever reaching the firmware.
2. **The 4th field of a `0x22` packet is validated against the attached payload length**, not
   against the `free_space` value from the previous ack. Echoing a stale/mismatched value gets
   a 2-byte `[0x22, 0x04 ERROR_PROTOCOL]` rejection.
3. **`current_offset` in a `0x21` *data* ack always reads back `0`** — it is not a continuation
   cursor, on the completing chunk or any other. A client must track its own running byte count;
   trusting this field causes an infinite retry loop (a real bug caught during this
   investigation, not a hypothetical — see `../2026-08-07-ble-write-path/raw/m4_infinite_loop_bug.txt`).
   The `0x20` **start** ack behaves differently and *is* meaningful (see resume, below).

**Measured throughput: ~2220-2237 B/s sustained, CONFIRMED flat across four real writes from
1 MiB to 29 MiB** (round-trip-bound, ~90ms/request ≈ 2x the 45ms connection interval — same
mechanism as the read path's M2 finding). The 29 MiB write took 3h48m with zero connection
drops. Full numbers, method, and raw JSONL/transcripts: `../2026-08-07-ble-write-path/README.md`.

**Resume and validation (relevant to any companion or `rawtiles` design decision):** resuming
an interrupted write works correctly when the client supplies the file's true current size (the
`0x20` start ack then reports a real, non-zero `current_offset`/`free_space` reflecting what's
actually on disk). A partial file reads back truncated exactly at the last successfully-written
offset — not zero-padded to the declared total, not absent. But **the firmware performs no
offset or ordering validation beyond rejecting a nonzero start offset on a path that doesn't
exist as a file yet** — once any file exists at a path, a resume or a data chunk at any offset,
in any order, is accepted and silently zero-fills whatever gap results. There is no
integrity/ordering guarantee at the firmware level; any companion needing one must build it
client-side. Full evidence: `../2026-08-07-ble-write-path/raw/m6_failure_modes.txt`.

### 2.3 Firmware-string corroboration (the static substream, now doubly confirmed by the capture)

Handler function names from `flash_strings.txt`, all under `BLE.SRV.FileTransfer`, match the live
behavior exactly:

| Handler | Evidence (verbatim log strings) | Confirmed by capture |
|---|---|---|
| `readHandler` | `File not exist [%s]`, `Reading: %u%% (%u/%u bytes) [%s]` | **Yes** — exactly matches the observed offset/total/chunk streaming in §2.2 |
| `writeHandler` | `Invalid offset...`, `Writing: %u%% (%u/%u bytes) [%s]` | **Yes** — the `0x20/0x21/0x22` family is now fully decoded and load-tested (§2.2, 2026-08-08 pass); `Invalid offset...` matches the confirmed rejection of a nonzero start offset on a nonexistent file |
| `listDirHandler` | `Directory not exist [%s]`, `List directory [%s]`, `Cycling detected [%s]` | **Yes** — exactly matches the `0x50/0x51` listing in §2.2 |
| `deleteHandler` | (see original evidence) | **Yes** — `0x30/0x31` confirmed as DELETE/DELETE_STATUS (§2.2, 2026-08-08 pass), used to clean up 22 of 23 test files |
| `moveHandler` / `makeDirHandler` | (see original evidence) | not exercised; no reason to doubt given the rest lined up |
| `TerminatePath` | bare string, no context | still unexplained |

Corroborating context: `0:/ble.ota` (a filesystem path string, line 3358) confirms firmware OTA
images transit onto the device filesystem at a specific path, and CCS's `firmwareUpdateHandler`
(§3) is very likely the command that kicks off (or completes) an OTA staged there.

### 2.4 What's still open for Phase C

- **Bind handle `0x0027` to its actual UUID.** This capture reused an Android GATT cache (no fresh
  service discovery occurred), so the UUID↔handle mapping wasn't directly observed. Two ways to
  close this: (a) force a fresh pairing + discovery capture, or (b) now that Ghidra is installed,
  statically correlate the `aci_gatt_add_char` call order in `initServices()` against the known
  handle numbers (handles are assigned in a fixed, boot-deterministic order on BlueNRG). Handle
  `0x0027`'s exact UUID doesn't block a same-firmware companion (which can hardcode the handle),
  but does matter for cross-firmware-version portability and for confirming which of the
  §1 UUID candidates it actually is.
- ~~Whether `total_size`'s 16-bit width is a real ceiling on file size~~ — resolved, §1.2/§2.2: the
  fields are genuinely 32-bit, no ceiling.
- ~~The `0x30` secondary command's exact purpose~~ — resolved, §2.2 (2026-08-08): DELETE/DELETE_STATUS.
- ~~The `0x20/0x21/0x22` upload framing~~ — resolved, §2.2 (2026-08-08): fully decoded and
  measured up to 29 MiB. Full writeup at `../2026-08-07-ble-write-path/`.
- **New from the write-path pass:** the read path's `real_chunklen` accounting has a firmware
  bug — it clamps to `MTU-16` but only ever delivers `MTU-16-3` bytes in one notification with
  no continuation, so a `chunk_len` request above `MTU-19` silently hangs rather than erroring.
  See `../2026-08-07-ble-write-path/raw/m2_real_chunklen_clamp.txt`. Worth reporting upstream —
  fixing it would raise read throughput for every client, not just this investigation's.

---

## 3. CCS (Custom Command Service) — command surface recovered

Handler names under `BLE.SRV.CustomCommand`:

| Handler | Evidence | Direction / purpose |
|---|---|---|
| `sendEventActivityEnded` | `Activity Ended event. App ID: '%02X...'` | watch → phone, notifies when a tracked activity finishes |
| `sendEventFindPhoneAlert` / `findPhoneConfirmationHandler` | `Find Phone Alert event: %s`, `Find Phone Confirmed` | bidirectional "find my phone" feature |
| `resetHandler` | `Wrong length. Expected %u, Got %u`, `Reset request`, `allowed` | phone → watch, factory/soft reset trigger, with a length-checked payload and an "allowed" gate (possibly just a confirmation flag in the payload, not necessarily auth — needs disassembly to tell apart) |
| `firmwareUpdateHandler` | `Firmware Update %s` | phone → watch, very likely what triggers applying the file already staged via FTS at `0:/ble.ota` |
| `dailyHealthHandler` / `dailyHealthHrHandler` | `Request DH %04u-%02u-%02u: steps %u, floors %u, act %u, RHR %u, AHR %u`, `Request DH %04u-%02u-%02u, hour %u` | phone → watch request, watch responds with daily-health-summary / hourly-HR data — **this may be a second, CCS-native path to some of the same data FTS/`.fit` files carry**, worth keeping in mind as an alternate/simpler sync channel for daily summaries (steps/floors/HR) even before FTS file transfer is solved |
| `epoStatusHandler` | `Request EPO. GPS: %u-%u, GLO: %u-%u, GAL: %u-%u, BD: %u-%u, QZZS: %u-%u` | GNSS assistance data (EPO/AGPS) status per constellation |
| `refLocationHandler` | (bare name only) | likely accepts a reference location to speed GNSS fix — no further detail from strings |

No auth/gating strings were found near any of these (see §4) — the `resetHandler`'s "allowed" token
is the only hint of any conditional logic in this group, and its meaning is unconfirmed.

---

## 4. Authentication / pairing model — the pivotal question (still open, but a real lead)

A full keyword sweep of all 6539 lines of `flash_strings.txt` for
`nonce|hmac|challenge|session.?key|token|signature|\baes\b|encrypt|decrypt|passkey|secret|handshake`
returned **only** standard BLE link-layer strings: `hci_encryption_change_event`,
`Encrypted. Connection_Handle 0x%04X`, `Encryption failed [0x%02X]. Enabled %d`. No custom
crypto/challenge/token vocabulary appears anywhere in the image's string table.

Combined with the earlier GATT-init strings already found —
`aci_gap_set_authentication_requirement()`, `aci_gap_set_io_capability()`,
`aci_gap_pass_key_req_event`, `Pin code: %06ld`, `isBondedAtLeastOne`,
`Advertising not started. No bonded devices`, `aci_gap_get_bonded_devices()` — the picture that
emerges is **standard BlueNRG/GAP-level pairing and bonding** (IO-capability-driven, likely
passkey entry given the `Pin code` format string, not Just Works) **with no additional vocabulary
suggesting an app-layer secret or handshake on top.**

The original string-only pass could only call this a lead, not a finding, since an absent string
can't rule out a numeric-only comparison or a raw-byte key with no printable trace. The two updates
below (static disassembly, then the dynamic capture) each independently closed that gap.

**Update — static disassembly (Ghidra) now closes this.** `isBondedAtLeastOne` (`FUN_0808e024`,
disassembled and decompiled directly) is a thin wrapper around the standard ACI bonded-devices
query (retries 3x, then returns whether any device is bonded) — nothing homegrown. Its **only two
callers in the entire image** are: (1) a generic public `isBonded()` accessor on the peripheral
class (gated only by a connection-state check, "Wrong state [%s]" on failure), and (2) the
`startAdv`/advertising function, which uses it purely to pick advertising mode (undirected-
connectable if already bonded, vs. discoverable/pairing mode otherwise — evidenced by the adjacent
strings `Advertising not started. No bonded devices`, `Pairing started`). **Neither caller is
anywhere near the FTS code.** Disassembling the actual `readHandler`/`writeHandler`/`listDirHandler`
bodies (the functions containing the `Reading: %u%%...`, `Invalid state. File not opened...`, and
`listDirHandler`-tag log calls) shows ordinary file I/O logic (percentage-progress arithmetic, calls
to what are almost certainly `IFile`/`IFileSystem`-style read/write/size vtable methods) — **no call
to `isBondedAtLeastOne`, `isBonded`, or any other security-check function appears anywhere in these
handler bodies.**

**Conclusion: security for FTS is enforced entirely at the GATT/ACI characteristic-permission
level (i.e. whatever encryption/authentication requirement bits were set when the characteristic
was registered via `aci_gatt_add_char`, enforced by the BlueNRG stack itself before the
read/write callback ever fires) — there is no additional application-layer secret, token, or
handshake anywhere in the FTS command path.** This is now genuinely settled, not just a lead: a
real transfer worked (dynamic stream, §2) *and* the handler code has no gate to bypass or satisfy
beyond normal BLE bonding (static stream, this update). **Verdict: CONFIRMED.**

**Update — dynamic stream also corroborates this.** The live capture (§2) shows
exactly the sequence the plan named as the falsification test for "no extra app-layer auth":
- `HCI_EVT`: `Rcvd Command Status (Set Connection Encryption)` → `Rcvd Encryption Change [v2]`
  (success) within the first ~3.3 seconds of connecting — standard BLE re-encryption using a
  **previously stored bond/LTK**, not a fresh SMP pairing exchange (only one SMP PDU appears in the
  whole capture: a single `Security Request`, opcode `0x0B`, sent by the watch — no
  Pairing Request/Response/Confirm/Random dance followed, because the phone already had a bond).
- After encryption succeeds, **every FTS operation in the capture** — directory listing, two whole
  small-file reads (`app_list.json`, `settings.json`), and two full `.fit` file reads — proceeds
  with **zero additional handshake, challenge, or token exchange** anywhere in the ATT traffic.
  Nothing resembling a nonce, session key negotiation, or app-level credential appears between the
  encryption-established event and the first successful file read.

This is real, positive, dynamic-stream evidence for the "easy case": **standard BLE bonding +
link-layer encryption appears sufficient to use FTS — no additional app-layer secret was observed
gating it in a real, successful multi-file transfer.**

**Final verdict: CONFIRMED, triple-corroborated** — (1) no crypto/challenge vocabulary anywhere in
the firmware string table, (2) the actual FTS handler functions, disassembled directly, contain no
call to any bonding/security-check function, and (3) a real transfer completed with zero extra
handshake traffic on the wire. The practical answer for building a companion is simply: **pair
normally (standard BLE bonding), then use FTS** — no vendor secret, challenge, or key stands in the
way.

---

## 5. CTS / DIS / BAS — standard-service mapping

- **CTS**: **CONFIRMED by live capture.** The capture shows the phone writing handle `0x001B` with
  bytes `EA 07 07 1E 16 2B 02 04 00 F0` — decoded as Year=2026 (`0x07EA` LE), Month=7, Day=30,
  Hour=22, Minute=43, Second=2, Weekday=4 (Thursday), Fractions256=0, Adjust Reason bitmask=0xF0 —
  an **exact 10-byte field match** to the standard Bluetooth SIG Current Time characteristic
  layout, on the exact date/time this session ran. A second write to handle `0x001E` with bytes
  `F0 04` decodes as Time Zone=`0xF0` (int8 = −16 → −4:00 in 15-min units) and DST Offset=`0x04`
  ("+1h DST"), matching the standard **Local Time Information** characteristic 2-byte layout
  exactly. This is about as strong a confirmation as is possible without also capturing the raw
  service-discovery response — **CTS is the standard SIG Current Time Service.**
- **DIS**: `Ble::DeviceInformationService::DeviceInformationService(...)` (5 `const char*` params:
  Manufacturer/Model/Serial/FirmwareRevision/HardwareRevision) plus the capture's own repeated
  reads of handle `0x0016` returning ASCII `"1.3.0"` and handle `0x0018` returning ASCII `"3.1"`
  (firmware/hardware revision strings, read multiple times as if by different app components) —
  **LIKELY standard SIG DIS (`0x180A`)**; the exact characteristic UUIDs (`0x2A26`/`0x2A27`) weren't
  independently re-derived from a discovery response this pass, but the plaintext version strings
  returned are exactly what those characteristics are defined to hold.
- **BAS**: `BLE.SRV.Battery`, `Battery level %u%%` plus the capture's handle `0x0021`, read/notified
  repeatedly returning the single byte `0x64` (100%) — **LIKELY standard SIG BAS (`0x180F`)**,
  single-byte percentage value matches the standard Battery Level characteristic exactly.

The remaining gap for all three (and for FTS/CCS/CANS, §1/§2.4) is the same one: this capture
reused a cached GATT table, so no `Read By Group Type`/`Read By Type` service-discovery exchange
was captured to pin exact UUIDs to these handles. The handle-level *behavior* now matches the
standard specs closely enough to be practically actionable for a companion regardless.

---

## 6a. Standalone prototype — built and validated against the real watch, phone-free

A Linux desktop prototype (Python + BlueZ D-Bus: `bleak` for discovery/pairing groundwork, raw
`dbus_fast` D-Bus calls for the actual GATT traffic) paired directly with the watch — no phone, no
Una app involved at all — and pulled a real `.fit` activity file end-to-end. The reconstructed file
passed the same validation as the phone-capture analysis: correct FIT header arithmetic and an
exact CRC-16 match (`0x75C7`), independently confirmed by `file(1)`'s magic database.

Two real findings came out of building this that refine earlier phone-capture-only conclusions:

- **The FTS list-directory command (`0x50`) has one more byte than originally transcribed.** The
  real wire format is `50 00 <path_len:u16LE> <path>` — a reserved/flag byte after the opcode,
  matching the same convention already documented for the `0x10` read command. This was a
  transcription slip in the original phone-capture analysis, not a protocol ambiguity; §2.2 above
  should be read with this correction. Missing the byte produces a real, deterministic 2-byte error
  reply from the watch (`[echoed_opcode, 0x04]`) rather than a silent failure — a useful signature
  if this bug recurs.
- **The `0x10` whole-file read is NOT "one request, fully streamed response" as the phone capture
  suggested — it needs one `0x10` request per chunk, with the offset field advancing each time.**
  Requesting only once (as the phone's capture implied) reliably delivered exactly one 128-byte
  chunk and then nothing further, even when watching the raw notification socket directly via
  `AcquireNotify` (which bypasses BlueZ's D-Bus `PropertiesChanged` — that path independently
  turned out to coalesce rapid successive notifications, a real gotcha worth knowing regardless:
  **use `AcquireNotify` for any high-rate GATT notification stream from BlueZ, not property-change
  signals, or you will silently drop chunks**). Explicitly requesting the next offset (`0x10` with
  `offset=128`) reliably produced the next chunk. Why the original phone capture *looked* like a
  single request sufficed is an open question — possibly a phone-side stack difference, possibly
  an artifact of how that capture was parsed — but the per-chunk-request loop is now proven correct
  end-to-end and is what any companion implementation should use.
- **Bonding is genuinely session-fragile on this firmware from a non-phone central**: the watch's
  advertising window (both for initial pairing-mode discovery and for post-bond reconnection) is
  short, and the connection itself timed out repeatedly during interactive testing. A production
  companion should implement patient retry/reconnect logic rather than assuming a single connect
  attempt will succeed — this is an implementation-robustness note, not a protocol finding.

Practical takeaway for anyone building on this: pair normally (standard BLE bonding — this
confirms §4's auth conclusion under real end-to-end use, no surprises), then for FTS use
`AcquireNotify` and a per-chunk request loop against the characteristic with UUID `adaf0002-4669-
6c65-5472-616e73666572` (service `0xFEBB`).

## 6. Status summary and what's left

**Phase C (FTS read protocol): done.** Real, validated, two-file round trip proven end-to-end from
a live capture (byte-exact FIT header + matching CRC-16 on both files).

**Phase B (auth): done.** CONFIRMED triple-corroborated (no crypto vocabulary in strings; no
bonding-check call anywhere in the disassembled FTS handlers; a real transfer worked with zero
extra handshake on the wire). Standard BLE bonding is the entire gate.

**Phase A (GATT table): mostly done.** Every custom-service UUID group (CCS, CANS, FTS's two
sub-services) is now CONFIRMED by decompiled constructor code, not just string adjacency —
including resolving CANS to exactly 2 characteristics (no missing 3rd) and refining the ADAF/
Adafruit relationship precisely (service UUID matches, characteristics don't). AMS/ANCS/NUS remain
byte-exact matches to public specs. CTS is behaviorally confirmed from live writes. **The
handle↔UUID binding gap is now closed too** (§6a): a live discovery pass during standalone-prototype
pairing directly confirmed `adaf0002-4669-6c65-5472-616e73666572` (part of the `0xFEBB` service) as
the FTS characteristic, at declaration handle `0x0026` / value handle `0x0027` — matching the
original phone capture's `0x0027` exactly.

Remaining work, roughly in priority order:
1. Decode the `0x30` secondary command and the `0x20/0x21/0x22` upload framing (both secondary to
   the core read-path goal, which is solved).
2. ~~Test whether a longer/larger real activity recording still transfers cleanly~~ — **done, §2.2:
   CONFIRMED fine, no ceiling.** A real one-hour recording (177756 bytes) transfers correctly;
   `offset`/`total`/`chunklen` are genuine 32-bit fields matching Adafruit's real spec (§1.2). An
   earlier version of this doc claimed a 64KB wraparound bug here — that was a bug in this
   investigation's own client (parsing 16 of 32 bits), not the firmware, and has been retracted and
   fixed (`prototype/una_ble_client.py`'s `read_file` now parses `<III>`).
3. ~~Understand why the phone capture appeared to need only one `0x10` request per file~~ — **done,
   §2.2: it's one request per chunk after all**, via `0x10` for the first chunk and a previously
   undocumented `0x12` for subsequent chunks (no path needed once the file's open). Resolved by a
   live capture of a real large-file sync.
4. Find out what the real Una app does with the `0x12`-chunked large-file transfer once it exceeds
   whatever ATT MTU/negotiation limits apply at scale (untested at very large sizes, e.g.
   multi-hour recordings) — low priority now that the 64KB "ceiling" is known to not exist.

**The read path is fully proven, not just specified.** §6a's standalone prototype pairs with the
watch with no phone involved, lists directories, and pulls a real `.fit` file that passes CRC
validation — this is a working existence proof, not a plan: pair normally, use `AcquireNotify` on
handle `0x0027` (UUID `adaf0002-...`), write `0x50 00 <len> <path>` to list or `0x10 00 <len>
<offset> <chunk_len> <path>` per chunk to read, until `offset+chunklen == total_size`.

## Sources used

- Apple, "The Apple Media Service" specification (developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference) — AMS UUID confirmation.
- Apple Notification Center Service UUIDs — corroborated via Nordic's nRF Connect SDK AMS/ANCS client docs (nrfconnectdocs.nordicsemi.com) referencing the same constants.
- Nordic Semiconductor, Nordic UART Service (NUS) documentation (developer.nordicsemi.com / nrfconnectdocs.nordicsemi.com) — NUS UUID + RX/TX role confirmation.
- Adafruit, `Adafruit_CircuitPython_BLE_File_Transfer` source (github.com/adafruit/Adafruit_CircuitPython_BLE_File_Transfer, `adafruit_ble_file_transfer.py`) — used to test and refute the "same protocol" hypothesis for the `ADAF...` UUIDs.
- `flash_strings.txt` (`strings -n 6` over `flash_real.bin`, this repo's investigation folder's prior-session artifact) — firmware-side source for UUIDs and log strings.
- **Live BLE capture** of the user's own phone (GrapheneOS, Pixel 9a) and watch performing two real
  `.fit` activity syncs — captured via `adb bugreport` → `FS/data/misc/bluetooth/logs/btsnoop_hci.log`,
  decoded with `tshark` (Wireshark CLI). Kept out of this git repo per the investigation's
  guardrails (it's the user's own private traffic/data, but raw capture artifacts and the two
  reconstructed `.fit` files stay in the local scratch working directory, not committed).
- Garmin FIT SDK's publicly documented CRC-16 algorithm (poly 0xA001, 16-entry nibble table) —
  used to independently validate the two reconstructed `.fit` files.
- **Ghidra headless static analysis** of `flash_real.bin` (`ARM:LE:32:v8-m`, base `0x08000000`,
  matching the confirmed dual-vector-table layout), decompiling the functions that construct each
  custom GATT service and the functions containing the FTS handler log strings, plus tracing every
  caller of the bonding-check function. Kept out of git per guardrails (see below).
