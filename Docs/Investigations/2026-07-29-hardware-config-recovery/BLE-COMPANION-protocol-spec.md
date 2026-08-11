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

### 1.2 The live GATT table — REFUTES the NUS hypothesis below

A full enumeration of the connected watch's GATT table on firmware 1.3.0
(`prototype/una_gatt_dump.py`, which reads BlueZ's resolved cache and puts nothing on the air)
returns **8 services and 19 characteristics**, and **there is no Nordic UART Service on the
device**. No `6e400001`, no RX, no TX.

That kills the working hypothesis stated immediately below: FTS does not ride an NUS-compatible
byte pipe, because no such pipe is exposed. The NUS constructor really is in the firmware image
(§1.1's `FUN_08093a80` is not in doubt) but it is evidently not registered on this build. The
`0xFEBB`/`adaf0002` characteristic carries the whole protocol on its own, exactly as the working
prototype has been doing all along.

AMS and ANCS are likewise absent, which is expected and not a contradiction: the watch is the
*client* for those, consuming them from an iOS phone, so they would never appear in its own
GATT server.

The complete table, with the property flags — the flags are the valuable part, since they settle
which direction each vendor channel runs:

| Service | Characteristic | Flags |
|---|---|---|
| GAP `1800` | `2a00` name, `2a01` appearance, `2a04` PPCP, `2aa6` central addr resolution | read (+write on the first two) |
| GATT `1801` | `2a05` service changed | indicate |
| DIS `180a` | `2a29` manufacturer, `2a24` model, `2a25` serial, `2a26` firmware, `2a27` hardware | read |
| CTS `1805` | `2a2b` current time | read, write, notify |
| CTS `1805` | `2a0f` local time info | read, write |
| BAS `180f` | `2a19` battery level | read, notify |
| FTS `febb` | `adaf0001` | **read** |
| FTS `febb` | `adaf0002` | write-without-response, notify |
| CCS `554e4100-a2cf-…-0000` | `…-0001` command | write-without-response, notify |
| CCS `554e4100-a2cf-…-0000` | `…-0002` event | **notify only** |
| CANS `554e4100-28e7-…-0000` | `…-0001` | **write-without-response only** |
| CANS `554e4100-28e7-…-0000` | `…-0002` | write-without-response, notify |

Three things fall straight out of the flags:

- **CANS `-0001-` is write-only.** Pushing a notification to the watch is fire-and-forget; there
  is no reply on that characteristic. Anything ANCS-like where the watch asks the phone for more
  detail (`androidFetchAttr`) must therefore come back on `-0002-`, which is the only CANS
  channel that notifies.
- **CCS `-0002-` is notify-only**, confirming it is purely the watch→phone event push channel —
  where `sendEventActivityEnded` and `sendEventFindPhoneAlert` (§3) should surface. Still
  unexercised.
- **`adaf0001` is readable**, and reading it returns 4 bytes: `04 00 00 00`, i.e. `uint32` = 4.
  Almost certainly an FTS protocol version. Adafruit's own File Transfer Service defines exactly
  such a version characteristic, so this is the one place the ADAF resemblance goes beyond the
  UUID. Nothing observed depends on it, but a companion could use it to detect a protocol change.

**Working hypothesis for the FTS transport (REFUTED — see §1.2 above; retained for the record):**
FTS appears to be built from
*two* GATT building blocks together: a small `ADAF`-styled pair (service+1 characteristic, or
2 characteristics of one service — ambiguous from strings alone) for something version/identity
adjacent, **plus** a literal clone of Nordic's 3-UUID NUS pattern (service + RX write + TX notify)
for the actual byte pipe. If true, the bulk file bytes ride over what is, at the raw BLE layer, an
NUS-compatible serial pipe, with UNA's own command/offset/length framing layered on top in the byte
stream itself (see §2) — **not** that any off-the-shelf NUS client can pull a `.fit` file without
also implementing that framing.

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
observed, possibly an explicit terminator — worth confirming, not load-bearing). A later walk of
the same directory on firmware 1.3.0 returned 24 entries; the count simply tracks what is
installed.

#### 2.2.1 The trailing 12 bytes of a `0x51` entry — SOLVED

The bytes previously recorded as "`mtime:u64LE?`" and reserved are now identified, by walking the
real filesystem and correlating each entry against sizes obtained independently from `0x11` read
headers (`prototype/una_fts_walk.py`):

```
51 <flags:u8> <name_len:u16LE> <index:u32LE> <total_count:u32LE> <attr:u32LE>
   <mtime:u64LE, MICROSECONDS since the Unix epoch> <size:u32LE> <name ASCII>
```

- **bytes 16..23 — `mtime`, microseconds since the Unix epoch.** Always second-granularity in
  practice (the low 6 digits are zero). `dh_20260728.json` → `1785326400000000` µs →
  2026-07-29 12:00:00 UTC, i.e. the day after the data it covers, consistent with a file
  finalised once the day closed.
- **bytes 24..27 — `size`, `uint32` little-endian.** Correlated exactly against `0x11`
  `total_size` for every file under 64 KiB, and is the *true* size for files above it (see the
  ceiling warning above). Directories always report `0`.

`attr` bit 0 set = directory, clear = file, as previously documented.

**Gotcha, learned the hard way:** a `0x51` entry carries no request identifier, so the *only*
thing binding a response to its request is that the previous exchange finished first. A walker
that keeps waiting after the firmware's `50 03` error reply will have the next listing swallow
the stragglers, producing a tree that looks entirely plausible but attributes files to the wrong
directories — the first run of the walk showed `/Apps/GlanceActivity/` containing
`Cycling_1.3.0.uapp`, shifted by one directory the whole way down. Stop the exchange dead on any
non-`0x51` reply, and let the link go quiet before the next request.

**Whole-file read — the mechanism that pulled the `.fit` files** (`0x10` request / `0x11` streamed
response), 12-byte request header + path, 16-byte response header + chunk, fields confirmed by
diffing multiple chunks of the same transfer against each other (offset increments by exactly the
chunk length each step; total stays constant; the header is **not** a naive packed struct — every
multi-byte field is a `uint16`, each preceded by 2 reserved/pad bytes, still not fully explained
but empirically exact across two independent 14–16-chunk transfers):

```
Request  (Write Command):
  10 00 <path_len:u16LE> <offset:u32LE> <requested_chunk_len:u32LE> <path ASCII, no NUL>

Response (Notification, repeated until offset+chunklen == total, no per-chunk ack needed):
  11 01 00 00 <offset:u32LE> <total_size:u32LE> <chunklen:u32LE> <chunklen bytes of file data>

  (An earlier revision recorded these as uint16 fields each followed by 2 reserved bytes. That
   was wrong -- see the resolved note below. Every file observed at the time was under 64 KiB,
   so the high halves were always zero and both readings fit the data.)
```
Confirmed concretely: request chunk length was `128` in every observed transfer; the server
honored it exactly (every chunk except the final remainder was 128 bytes); **the client issues
exactly one request per file and the server streams the entire file back unprompted** — there is
no per-chunk request/ack round-trip, which is good news for a companion implementation (no flow
control puzzle — read notifications until the byte count matches `total_size`, exactly as the
firmware's `Reading: %u%% (%u/%u bytes)` log string in §2.3 already implied).

**✅ There is no size ceiling. The header fields are plain `uint32`, and the earlier
"`uint16` + 2 reserved bytes" reading was simply wrong — RESOLVED, and this supersedes an
earlier revision of this document that recorded a truncating 64 KiB limit.**

The mistake was inherited from the original phone-capture transcription and repeated by both
prototypes: every observed file was under 64 KiB, so the high half of each field was always zero
and the layout looked like `u16` values separated by padding. Reading a 519652-byte file settles
it. Raw header, requested offset 0:

```
11 01 0000 00000000 e4ed0700 40000000
             ^offset  ^total   ^chunklen      all uint32 little-endian
```

`0x0007ede4` = **519652**, matching the `0x51` directory entry exactly, with no wrapping. The
corrected framing is:

```
Request  (Write Command):
  10 00 <path_len:u16LE> <offset:u32LE> <requested_chunk_len:u32LE> <path ASCII, no NUL>

Response (Notification):
  11 01 0000 <offset:u32LE> <total_size:u32LE> <chunklen:u32LE> <chunklen bytes of file data>
```

Offsets beyond 65535 are addressable and work — verified by requesting 65536, 131072 and 519552
against that file, each echoing the requested offset back and returning the correct 64-byte
payload. So **large files are fully readable**, and activity backfill is not blocked by any size
limit.

The `0x51` directory entry's `uint32` size (§2.2.1) agrees with the read header rather than
correcting it, and remains useful for knowing a file's size before starting to read it.

Both `prototype/una_ble_client.py` and `prototype/una_fts_walk.py` have been corrected to build
and parse these fields as `uint32`. Gadgetbridge's `UnaFtsProtocol` already did, so it was never
affected.

**A secondary, still-unexplained command pair** (`0x30` request / response byte `0x02` or `0x01`)
was also observed, issued *after* a successful `0x10` read of the same path, and also probed
against a hypothetical `<name>.json` sidecar for the same activity. This does **not** gate or
precede the read (order observed: `0x10` read completes fully, *then* `0x30` fires) — likely a
"check for a companion metadata file" or a post-sync bookkeeping call. Not blocking; flagged as an
open item, not a re-run of the mis-hypothesized "open" step from an earlier, incomplete capture
(see git history of this file for that superseded guess).

**Large-file upload** (`0x20`/`0x21`/`0x22` family) was also observed carrying a large binary blob
*from phone to watch* (Write Commands to path `/GPS_EPO/GPS.DAT`) — almost certainly the EPO/AGPS
assistance-data push tied to CCS's `epoStatusHandler` (§3). Confirms the same characteristic
multiplexes several sub-protocols by leading opcode byte. Framing not fully decoded this pass
(secondary to the read-path goal); revisit if a companion needs to push EPO data too.

### 2.2.2 The on-watch filesystem layout — walked, firmware 1.3.0

A read-only recursive walk (`prototype/una_fts_walk.py`) gives the real tree. This matters
because a companion relying on `/Apps/latest_activity.txt` sees only what the firmware considers
*pending* — a freshly-paired phone finds a user's entire history invisible.

```
/
├── settings.json, settings.json.bak            (+ local_settings.json, accessories.json, .bak)
├── DailyHealth/
│   └── <YYYYMM>/dh_<YYYYMMDD>.json             one JSON per day
├── Apps/
│   ├── app_list.json, app_sorting.json, latest_activity.txt, QZSS.DAT
│   └── <AppName>/
│       ├── <AppName>_<version>.uapp            the app binary
│       └── Activity/
│           ├── summary.json
│           └── <YYYYMM>/
│               ├── activity_<YYYYMMDD>T<HHMMSS>.fit
│               └── activity_<YYYYMMDD>T<HHMMSS>.json   sidecar
├── GPS_EPO/     GPS.DAT, GLO.DAT, GAL.DAT, BD.DAT      (the EPO/AGPS push target)
├── Crash/       Activity/, Debug/Debug/dump_<serial>_<n>_<ts>_<fw>.bin
├── Update/
├── .Trash-1000/
└── _disabled_apps_backup/
```

Two corrections to earlier notes:

- The archive directory is **`Activity/`**, not `ActivityArchive/` as the prototype's usage
  example says (that example predates this walk and should be read as illustrative).
- **The `.json` sidecar beside each `.fit` is real.** §2.2 recorded the mysterious `0x30` command
  being probed against a hypothetical `<name>.json` sidecar and flagged it as unexplained; the
  sidecar genuinely exists on disk, so `0x30` is very likely a "does this companion metadata file
  exist" check. Still not proof of what `0x30` *does*, and it remains unsafe to fire blind.

#### 2.2.3 `/DailyHealth/` — a full day of per-minute HR in a single file read

This turned out to be the most useful thing in the tree, and it **supersedes the CCS commands as
the best way to get health history**.

Reading `/DailyHealth/202608/dh_20260810.json` (3470 bytes) gives:

```json
{
  "dailySteps": 1666,
  "dailyFloorsUp": 0,
  "dailyFloorsDown": 0,
  "dailyActivityMinutes": 0,
  "restingHeartRate": 67,
  "averageHeartRate": 77,
  "hrPerMinute": [ ...exactly 1440 integers... ]
}
```

**`hrPerMinute` is the whole day, one entry per minute, midnight to midnight**, with `0` meaning
no reading exactly as in the CCS `0x14` payload.

**Cross-validated against CCS, exactly.** `hrPerMinute[720:780]` — the 12:00 hour — is
element-for-element identical to the `0x14` response for `2026-08-10 12:00`, including the
isolated zero at minute 2 and the 108/107 spikes. Two entirely independent transports agreeing
byte for byte confirms both decodings, and shows `0x14` is simply serving 60-element slices of
this same on-disk array.

Why this is the better source:

| | CCS `0x14` | `dh_<date>.json` over FTS |
|---|---|---|
| Data per exchange | 60 minutes | **1440 minutes + the day's aggregate** |
| History available | ≥3 days (unverified beyond) | **14 days present on disk** (2026-07-28 … 08-10) |
| Floors | single combined figure | **split into `dailyFloorsUp` / `dailyFloorsDown`** |
| Cost for one full day | 24 round trips, ~2.2 s | one ~3.5 KB read, ~2.5 s |

The day currently in progress is not yet in a dated file — it lives in `/DailyHealth/dh.tmp`,
which is presumably renamed into `<YYYYMM>/dh_<YYYYMMDD>.json` when the day closes. A companion
wanting today's partial data should read `dh.tmp` (or fall back to CCS, which does serve the
current day).

`/DailyHealth/200001/dh_20000101.json` also exists — a year-2000 directory, almost certainly
data recorded before the clock was ever set.

Per this investigation's guardrails, no retrieved health file is committed here; the structure
above is transcribed, the contents are the device owner's own data.

### 2.3 Firmware-string corroboration (the static substream, now doubly confirmed by the capture)

Handler function names from `flash_strings.txt`, all under `BLE.SRV.FileTransfer`, match the live
behavior exactly:

| Handler | Evidence (verbatim log strings) | Confirmed by capture |
|---|---|---|
| `readHandler` | `File not exist [%s]`, `Reading: %u%% (%u/%u bytes) [%s]` | **Yes** — exactly matches the observed offset/total/chunk streaming in §2.2 |
| `writeHandler` | `Invalid offset...`, `Writing: %u%% (%u/%u bytes) [%s]` | Consistent with the `0x20/0x21/0x22` upload family observed for `GPS.DAT` |
| `listDirHandler` | `Directory not exist [%s]`, `List directory [%s]`, `Cycling detected [%s]` | **Yes** — exactly matches the `0x50/0x51` listing in §2.2 |
| `deleteHandler` / `moveHandler` / `makeDirHandler` | (see original evidence) | not exercised in this capture; no reason to doubt given the rest lined up |
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
- Whether `total_size`'s 16-bit width is a real ceiling on file size (see above).
- The `0x30` secondary command's exact purpose.
- The `0x20/0x21/0x22` upload framing (secondary to the read-path goal).

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

### 3.1 CCS transport and the two daily-health commands — CONFIRMED by live probing

**Both daily-health commands are now fully recovered and validated against a real watch**
(UNA Watch 403795, firmware 1.3.0 / hardware 3.1), by writing requests directly over BlueZ and
recording the replies — no phone and no vendor app involved. Probe script:
`prototype/una_hr_probe.py`. Twenty requests, all answered. Every command below is a read;
nothing is written to the watch.

CCS UUIDs, confirmed live at these paths (`service0029/char002a` in the probed session):

| Role | UUID |
|---|---|
| Service | `554e4100-a2cf-4df8-0000-7e1e48595106` |
| Command characteristic (both commands ride this) | `554e4100-a2cf-4df8-0001-7e1e48595106` |
| Event characteristic (watch→phone; not yet exercised) | `554e4100-a2cf-4df8-0002-7e1e48595106` |

Requests are ATT Write Commands; replies are notifications on the same characteristic,
multiplexed by an echoed leading opcode byte exactly as FTS does. Observed **notify MTU 220**
and **~90 ms per round trip** (89–180 ms, the outlier being the first).

**`0x10` — daily aggregate** (`dailyHealthHandler`):

```
Request  (6 bytes):  10 00 <year:u16LE> <month:u8> <day:u8>
Response (22 bytes): 10 01 <steps:u32LE> <floors:u32LE> <activeMinutes:u32LE>
                           <restingHR:u32LE> <averageHR:u32LE>
```
Observed: `1000ea07080b` → `1001fe02000002000000000000004100000048000000`
= 2026-08-11, steps 766, floors 2, active 0 min, RHR 65, AHR 72. Note `activeMinutes` is
frequently **0** even on a day with a full heart-rate record, so it is not a reliable figure.

**`0x14` — hourly HR matrix** (`dailyHealthHrHandler`) — the per-minute counterpart:

```
Request  (7 bytes):  14 00 <year:u16LE> <month:u8> <day:u8> <hour:u8>
Response (62 bytes): 14 01 <60 x u8>     one bpm per minute-of-hour, 0 = no reading
```
Observed: `1400ea07080b09` → `14013e3e3c3c3c6c3f...` = 2026-08-11 09:00, 60/60 minutes measured,
range 57–111 bpm. Month is 1-based; all fields are **local wall-clock**.

**The single most important behavioural finding: the watch always replies, always with status
`0x01`.** A non-OK status was never observed for any input:

| Probe | Result |
|---|---|
| Hour with data | `14 01` + real values |
| Hour not worn | `14 01` + **60 zero bytes** |
| Date two days in the **future** | `14 01` + 60 zero bytes |
| `hour=25`, out of range | `14 01` + 60 zero bytes |

**`0x10` behaves identically** — the same edge cases were run against it separately rather than
assumed to carry over, since a client walking days has the same stall exposure as one walking
hours:

| Probe | Result |
|---|---|
| 60 days back | `10 01` + all-zero payload |
| A year back | `10 01` + all-zero payload |
| Two days in the future | `10 01` + all-zero payload |
| `month=13, day=32` | `10 01` + all-zero payload |

So "no data" and "invalid request" are indistinguishable, the firmware does no input validation
on *either* command, and **nothing ever goes silent** — a client walking many hours or days does
not strictly need a per-request timeout (the vendor app uses a 5 s one regardless). The real "no
data" signal is an all-zero payload, so a client must filter zeros rather than read a status code.

Two further practical notes for anyone consuming this:

- **Zeros occur sporadically mid-hour**, not just as a trailing gap — 11:00 had minute 5 zero
  between two good readings. Drop them per minute.
- **Values frequently repeat in adjacent pairs** (`105 105 105 105 105 113 113 71 71 71`),
  suggesting the underlying sample interval is coarser than a minute and the watch expands it.
  Sixty entries is the resolution exposed, not sixty independent measurements.

**Retention is at least 3 days**, well beyond the 12 hours a companion might assume: `-1d`
returned 59/60 real readings and `-3d` returned 60/60 — though that `-3d` hour was a *flat
constant 63 bpm for all 60 minutes*, which looks more like a stored default or decayed record
than a real measurement. Treat old hours with more suspicion than recent ones.

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

**CCS daily-health commands: done.** Both `0x10` and `0x14` are fully recovered and validated
against a real watch by direct probing — request framing, response layout, error behaviour and
retention (§3.1). `prototype/una_hr_probe.py` is the working proof. This closes the "worth
keeping in mind as an alternate/simpler sync channel" note left open in §3: it is not merely an
alternate channel, it is the *only* source of a per-minute heart rate timeline, since the `.fit`
files carry discrete workouts rather than all-day monitoring.

Remaining work, roughly in priority order:
1. **CANS (notifications).** The largest remaining gap for any companion, and the one thing that
   makes a watch a watch. Service `554e4100-28e7-4811-0000-141f8b92ee40`, characteristics
   `-0001-` and `-0002-`. The watch is the GATT server, so it is a plain characteristic write —
   far cheaper than the AMS/ANCS path, which would need the companion to *run* a GATT server.
   Expect a fragmentation/reassembly layer (multi-KB buffers and a worker thread in the
   constructor; strings `parseBuffer`, `BLE.ParserCANS`, `androidFetchAttr`, `notifHandler`).
2. The CCS **event characteristic** `-0002-`, still completely unexercised. `sendEventActivityEnded`
   and `sendEventFindPhoneAlert` (§3) should surface here — the first would let the watch trigger
   a sync instead of the companion polling.
3. ~~Can a file over 64 KiB be read at all?~~ **DONE — yes.** The ceiling never existed; the
   header fields are `uint32` and offsets past 65535 work (§2.2). Nothing blocks activity
   backfill on size grounds. Worth doing once for real: pull a complete >64 KiB `.fit` end to end
   and CRC-check it, the same way §2.1 validated the two small ones.
4. ~~Read a `dh_<date>.json` and diff it against `0x10`.~~ **DONE — see §2.2.3.** It carries a
   full 1440-entry per-minute HR array plus the daily aggregate, matches CCS `0x14` exactly, and
   14 days are on disk. Follow-up worth doing: confirm `/DailyHealth/dh.tmp` really is the
   in-progress day and is renamed on rollover.
5. Decode the `0x30` secondary command and the `0x20/0x21/0x22` upload framing. `0x30` is now
   known to be probed against a `.json` sidecar that really exists (§2.2.2), so the "does
   companion metadata exist" reading is more likely — but still unproven and unsafe to fire blind.
6. Understand why the phone capture appeared to need only one `0x10` request per file while the
   standalone prototype needed one per chunk (§6a) — a real discrepancy, not yet explained.
7. Explain the flat-constant-63-bpm hour seen 3 days back (§3.1) — real resting record, or a
   stored default the firmware emits for decayed history?

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
