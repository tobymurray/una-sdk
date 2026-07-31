# Handoff prompt: reverse the UNA Watch BLE GATT profile to build an independent companion (Gadgetbridge / standalone) that syncs without the Una app

Paste everything below into a fresh session. This prompt is **self-contained** — it aggregates
every relevant finding and effort so far so you do not have to re-derive them. Where it names a
file, address, or fact, that fact was established by a prior session and is tagged with its
confidence; treat `CONFIRMED` as load-bearing, `LIKELY`/`UNVERIFIED` as a lead to re-check, never
as settled. Read the **Guardrails** section before doing anything.

---

## 0. Objective (this phase)

Prior phases characterized the hardware and dumped the firmware. **This phase pivots to a new
goal:** recover enough of the watch's **BLE GATT profile and sync protocol** to build an
**independent phone-side companion** — a Gadgetbridge device plugin or a small standalone app —
that can **pull activity data (`.fit` files) off the watch and set its time, entirely bypassing
the proprietary Una companion app.**

Concretely, produce a verified specification of:

1. **The GATT table** the watch exposes: every service + characteristic UUID, properties
   (read/write/notify/indicate), and which of the six named services (DIS/CTS/BAS/FTS/NTS/CCS)
   each corresponds to.
2. **The File Transfer Service (FTS) protocol** — the one that matters most. It carries "OTA
   updates **and data exchange**" per the SDK architecture docs. Recover its command framing:
   how a client lists files, requests a file, and receives it (chunk size, sequencing,
   acknowledgement, CRC/length, end-of-transfer). This is the channel that pulls `.fit`
   activities off the device.
3. **The authentication / pairing model gating FTS and CCS.** This is the single biggest unknown
   and it determines whether this project is a weekend or a month. Answer definitively: does FTS
   require only standard BLE bonding/encryption, or is there an **app-level challenge/response or
   vendor key** on top? **Locate the mechanism; do NOT extract, reproduce, or redistribute any
   key or the OTA decryption algorithm** (see Guardrails — a prior memory already flagged the OTA
   image as encrypted).
4. **The Custom Command Service (CCS) surface** — secondary. Enough command framing to control/
   configure the watch (at minimum whatever is needed to trigger or complete a sync).
5. **CTS/DIS/BAS mapping** — confirm these are the standard SIG services (`0x1805` / `0x180A` /
   `0x180F`) so a companion can reuse stock implementations for free.

The **deliverable** is a written protocol spec + an updated confidence ledger (same discipline as
the hardware-recovery README), sufficient for someone to implement a Gadgetbridge `DeviceSupport`
plugin. **Writing the plugin itself is out of scope for this phase** — proving the protocol is the
gate; the plugin is downstream work once the protocol is confirmed end-to-end.

---

## 1. Guardrails (read first — carried forward and extended from prior phases)

- **Legitimacy.** Own hardware, own data, defensive/interoperability research. Building an
  independent companion to access *your own* activity data over *your own* BLE link is the
  textbook Gadgetbridge use case and is the whole point.
- **Do not cross into key/crypto exfiltration.** Locate the auth path and the OTA-decrypt path;
  **do not extract or reproduce the key or algorithm**, and do not build anything that forges
  firmware or impersonates the vendor's signing. Pulling your own `.fit` with your own device's
  own credentials is fine; cloning secrets is the line.
- **Do not redistribute the dumped firmware** or large decompiled excerpts of it. Keep the dump
  and any decompilation **out of the `una-sdk` git repo** — it is proprietary vendor firmware.
  Only the *analysis* (UUIDs, protocol framing, spec, ledger) belongs in the investigation doc.
- **This phase is primarily static analysis + passive BLE observation** — both non-destructive.
  If a step later calls for *writing* to the watch (e.g. actively driving FTS from a script), that
  is a separate, riskier step: propose it explicitly, never blind-write, and confirm you can
  recover the device (power-cycle / re-flash) before attempting it.
- **Adversarial discipline is mandatory** (Section 6). The prior ledger is full of first guesses
  that were wrong on a second check (DRV2605→DRV2625, MAX17048→MAX17262, INA226 refuted, BMM350
  register refuted). A single decompiled string, a single guessed calling convention, or a single
  packet reading is **a lead, not a finding**. Corroborate by an independent method before you
  tag anything CONFIRMED.

---

## 2. Resources — what I have, and what to ask me for

**Ask me to provide any of these that are not already present in your working environment.** Do
not proceed on assumptions if a resource is missing — request it.

Firmware & analysis artifacts (from the prior hardware-recovery phase; may be on a different
machine than this session — **request them if absent**):

```
una-firmware-dumps/2026-07-29-flash-dump/
├── flash_dump.bin      # full 4 MB, file offset 0 == flash address 0x08000000
├── flash_real.bin      # first 0x20A140 bytes (the real, non-blank image) — USE THIS ONE
├── flash_strings.txt   # `strings -n 6` output already run on flash_real.bin
└── chunks/             # 32×128 KB original dump chunks + manifest (re-verify if needed)
```
Integrity anchor: whole-image CRC32 of the dump is **`0xBCD2F8E0`** (device-reported and
host-recomputed agree). If you receive the dump, re-verify this before trusting it.

Datasheets / references (request if not present):
- **RM0456** — STM32U5 reference manual (register-level; needed for SPI/EXTI/GPIO peripheral
  layout when tracing the BlueNRG SPI driver). A prior session had it locally as
  `rm0456-*.pdf`.
- **DS13543** — STM32U5A5 datasheet (pinout / AF tables; for mapping the BlueNRG SPI CS/IRQ pins
  if that becomes necessary). Prior local copy: `stm32u5a5aj.pdf`.
- **BlueNRG-2 / BlueNRG-MS ACI programming manual** (ST **PM0257** or equivalent) — the ACI/HCI
  command set the kernel uses to build the GATT table. This is the Rosetta stone for the static
  path (Section 4); **request it if you don't have it.**
- **This repository** (`una-sdk`) — the public SDK. Grounds the app-visible half of the contract
  (Section 3).

Live-capture assets (needed for the dynamic path, Section 5 — **ask me to produce these**; I have
physical access to the watch and the Una app):
- An **Android Bluetooth HCI snoop log** captured while the *official Una app* performs a full
  sync (Developer Options → enable Bluetooth HCI snoop log → sync → pull
  `/sdcard/.../btsnoop_hci.log` or via `adb bugreport`). This is usually the fastest route to the
  FTS framing.
- **or** an **nRF Sniffer** capture (nRF52840 dongle + Wireshark) of the same session.
- The watch's **advertised name / MAC**, and whether the Una app pairs/bonds (shows in Android
  Bluetooth settings) — a one-line answer from me that immediately narrows the auth question.

If I have not supplied a capture yet, **tell me exactly how to capture it** (the precise steps for
my platform) rather than blocking — the static path (Section 4) can start in parallel with no
device interaction at all.

---

## 3. Known state — the app-visible half (grounded in this public SDK)

Established from `una-sdk` itself (verify line references still hold before citing):

- **Radio reality.** BLE is the *only* radio. No Wi-Fi anywhere (`IWifi` absent from
  `Libs/Header` and `Docs`). GNSS is an Airoha AG3335M; BLE is a **BlueNRG-2 over SPI**. Any
  "sync with a server" is necessarily `server → phone → BLE → watch`; the watch never reaches a
  server itself. A companion therefore only needs to speak BLE to the watch — the server half is
  ordinary app work and out of scope.
- **The BLE stack is entirely kernel-internal and closed.** `Libs/Source/Kernel/KernelBuilder.cpp`
  wires exactly five app-facing interfaces — `ISystem`, `ILogger`, `IAppMemory`, `IAppComm`,
  `IFileSystem` — and **nothing BLE-shaped**. `Libs/Header/SDK/Interfaces/` contains no `IBle*`.
  So the GATT profile exists only in firmware, not in this repo — which is *why* we disassemble.
- **The six named services** (`Docs/architecture-deep-dive.md`, BLE Service Stack section):
  **DIS** (Device Info), **CTS** (Current Time, TZ/DST), **BAS** (Battery), **FTS** (File Transfer
  — "OTA updates **and data exchange**"), **NTS** (Notification — described as **iOS ANCS**),
  **CCS** (Custom Command Service — "proprietary commands"). DIS/CTS/BAS are standard SIG services
  (expected UUIDs `0x180A` / `0x1805` / `0x180F`); **FTS and CCS are proprietary** and are the
  reverse-engineering targets. NTS-as-ANCS implies the watch is the *notification client* of an
  iOS phone — Android notification interop will differ and is not needed for activity sync.
- **DIS firmware-revision** is the standard characteristic **UUID `0x2A26`** — the Una app reads it
  to match app `minKernelVersion` (`Docs/app-config-json.md`). A free confirmation datapoint that
  DIS is stock.
- **Activity files.** Apps write standard Garmin **`.fit`** files named
  `activity_YYYYMMDDTHHMMSS.fit` (e.g. `Examples/Apps/Running/.../ActivityWriter.cpp`), the kernel
  auto-registers them, and the filesystem is exposed as USB-MSC volume `2:/`. Once a `.fit` is off
  the watch it is a standard file — parsing/upload is a solved problem. **The entire difficulty is
  the FTS transfer + any auth**, nothing downstream of it.
- **Inbound-channel context** (`Docs/companion-data-channel-analysis.md`, on branch
  `docs/companion-data-channel-analysis`): there is no supported way to push arbitrary data *into*
  a third-party app today; `RequestSetCapabilities` and the accessory messages are contracts with
  **no handler in the public repo** (only a simulator stub). Relevant here only as evidence that
  the real CCS/FTS handlers live exclusively in the closed kernel you are about to disassemble.
- **App sandbox reality** (from prior phase, CONFIRMED): apps run **privileged, MPU off, TZEN=0** —
  no isolation. Not needed for the companion goal, but it means a watch-side `.uapp` remains an
  available *secondary* instrument if you ever need to observe the BLE stack from inside the device
  (e.g. log an SPI/ACI transaction) rather than only from the phone side.

---

## 4. Known state — the firmware half (from the hardware-recovery phase)

All CONFIRMED unless noted. Full ledger: `README.md` in this same investigation folder.

- **MCU:** STM32U5A5 (3 independent methods). **RDP Level 0**, **TZEN=0**, **MPU disabled**, app
  thread **privileged** — factory-default-open security posture.
- **Flash dump:** full 4 MB dumped and verified two ways; **real image is only ~2.04 MB**
  (`0x0`–`0x20A140`), rest is `0xFF`. **Use `flash_real.bin`.**
- **Two vector tables**, parsed directly from the dump:
  - Bootloader stage @ `0x08000000`: `SP=0x201F0000`, `Reset=0x08001C45` (Thumb).
  - **Main kernel @ `0x08060000`: `SP=0x20250000`, `Reset=0x0806CE45`** — this is where the BLE
    stack lives; anchor analysis here.
- **BLE driver classes present in the dumped kernel** (driver-string evidence, CONFIRMED part):
  - `Hardware::BlueNRG2::config(Interface::ISpi*, Interface::IGpo*, Interface::IExti*, Interface::IGpo*)`
    — the SPI transport (ISpi + two GPO + one EXTI/IRQ line).
  - `Ble::PeripheralBlueNRG` (main controller) and `Ble::CoreBlueNRG` (SPI driver) — named in
    `Docs/architecture-deep-dive.md`; expect their mangled symbols/strings in the dump.
  - `Backend` is the service manager that wires DIS/CTS/BAS/FTS/NTS/CCS to the peripheral and
    implements `IBleStatusCallback` (`onConnect(Address)`, `onDisconnect(uint8_t)`).
- **Symbol-seeding asset:** `Libs/Source/AppSystem/linker/LibC/libc_exports_0.0.3.ld` in this repo
  has **336 `PROVIDE(name = 0x0803xxxx)`** lines — real libc addresses *inside this exact kernel
  image*. Script these into r2 flags/symbols so libc calls resolve by name; anything in the hot
  paths that is NOT a recognized libc call is more likely UNA driver code. (These live in the
  `0x0803xxxx` range — inside the kernel region above `0x08060000`, consistent.)

---

## 5. Tooling

**Disassembler.** `radare2` (preferred — Thumb-aware, auto-analysis, xrefs). Fallback with zero
setup: `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x08000000
flash_real.bin` (flat, no xref analysis — use only to spot-check). Ghidra is an even better choice
than r2 for this specific job if available (its P-code decompiler makes GATT-table-builder and
protocol state machines far more legible than raw disasm) — **ask me to install Ghidra if you
judge it worth it**; otherwise proceed with r2.

Open the image (Cortex-M33, Thumb2 almost everywhere):
```
r2 -b 16 -m 0x08000000 -a arm -e asm.bits=16 <path>/flash_real.bin
```
**Verify bitness before trusting auto-analysis**: disassemble the two known reset vectors
(`0x08001C44`/`0x0806CE44` — clear the Thumb low bit) and confirm they decode as sane function
prologues. A wrong mode at entry produces plausible-looking-but-wrong code downstream.

**Dynamic-capture tooling.** Wireshark (with the nRF Sniffer plugin if using a dongle) or any
btsnoop viewer for the HCI log. `bleak` (Python) for a later active-probe PoC *if* an active step
is approved.

---

## 6. The plan — state of the art, adversarially verified at every step

Two independent evidence streams — **static** (disassembly) and **dynamic** (packet capture) —
are run so each *falsifies* the other. **No claim is CONFIRMED on one stream alone.** Each phase
below states its **falsification test**: the specific observation that would prove the current
hypothesis wrong. If you cannot articulate what would falsify a finding, you have not verified it.

### Phase A — GATT table recovery (both streams, cross-checked)

**Static.** In `flash_real.bin`, find the GATT-table construction. BlueNRG builds services via ACI
calls — `aci_gatt_add_serv` / `aci_gatt_add_char` (and `aci_gatt_update_char_value`). Locate them
by: (a) string/xref search for `Ble::`, `BlueNRG`, `FTS`/`CTS`/`Backend` symbols; (b) finding the
128-bit custom UUID **constants** (16 contiguous bytes, high-entropy, referenced near the add-char
calls) for FTS and CCS; (c) reading the 16-bit SIG UUIDs (`0x1805/180A/180F/2A26`) as immediates to
anchor which call sequence is which service.

**Dynamic.** From the HCI/sniffer capture, extract the *actual* advertised services and the full
service/characteristic discovery (the `ATT Read By Group Type` / `Read By Type` responses). This
yields the live UUID list and handle map directly.

**Cross-check / falsification.** The custom UUIDs recovered from disassembly **must byte-match** the
UUIDs seen on air. If a UUID appears in the capture but not in the disassembly (or vice versa), the
GATT-table location is wrong or incomplete — **do not proceed to Phase B until they reconcile.**
Record each UUID with *both* sources in the ledger; a UUID with only one source is `LIKELY`, not
`CONFIRMED`.

### Phase B — Authentication / pairing model (the pivotal question)

**Dynamic first** (cheapest decisive signal). In the capture, inspect the connection setup: is
there an **SMP pairing/bonding** exchange? Is link encryption enabled before FTS is used? Then look
at the **first writes to FTS/CCS after connect** — is there a challenge/response (watch sends a
nonce/notify, phone writes back a derived value) *before* any file operation is permitted? A sync
that works immediately after standard bonding ⇒ no app-layer auth (best case). A
write-then-notify-then-write dance before file listing ⇒ app-layer auth (harder case).

**Static corroboration.** In the FTS/CCS handlers, look for a state gate: a boolean/flag checked
before file ops, set only after a specific characteristic write; trace what validates that write.
If it calls into a crypto routine (AES/HMAC/compare against a stored secret), that is the auth
mechanism — **document its shape and location, then STOP: do not extract the key or reproduce the
algorithm** (Guardrail). It is enough to know *that* auth exists and *what class* it is.

**Falsification.** If you conclude "no app-layer auth," prove it by showing the capture performs a
file transfer with no unexplained pre-transfer writes AND the static handler has no pre-op gate. If
you conclude "auth exists," prove it by showing the gated flag in the handler AND the corresponding
handshake packets on air. Either conclusion needs both streams. This phase's output sets the entire
difficulty estimate — flag it as the top-line result.

### Phase C — FTS protocol (the payload)

**Dynamic.** Isolate the FTS characteristic handles from Phase A. Reconstruct one complete file
transfer from the capture: the command the phone writes to start a list/get, the notification/
indication stream carrying data, the chunk size (expect it bounded by ATT MTU and/or an internal
framing size — the SDK notes a **256-byte** kernel message ceiling, a plausible chunk bound),
sequence numbering, per-chunk or whole-file CRC/length, and the end-of-transfer signal. Then
**validate semantically**: carve the transferred bytes and confirm they are a valid `.fit` file
(FIT header: `.FIT` signature at bytes 8–11, header CRC, file CRC) — a byte stream that doesn't
parse as FIT means the framing is misread.

**Static corroboration.** Trace the FTS handler's read of the file off `IFileSystem` and its
chunking loop; confirm the chunk size, header layout, and CRC routine match what the capture shows.

**Falsification.** The reassembled file **must parse as a valid FIT file** (independent FIT parser,
not "it looked right"). If it doesn't, the framing (chunk boundaries, header size, endianness) is
wrong — iterate. A protocol description that hasn't round-tripped an actual `.fit` off *your* watch
is `UNVERIFIED`.

### Phase D — CCS command surface (secondary)

Map the CCS characteristic(s) and the minimal command set needed to drive a sync (and, useful:
set time via CTS to confirm the write path end-to-end on a low-risk standard service first).
Verify each command dynamically (seen in the Una app's capture) *and* statically (handler exists
and does what the packet implies). Time-set via CTS is the ideal first **active** write to attempt
(if active steps are approved) because it's a standard service with a bounded, recoverable effect —
a safe end-to-end proof that the companion can write, before touching proprietary CCS.

### Phase E — Spec + feasibility writeup

Produce: (1) the GATT/FTS/CCS **protocol spec**; (2) the **auth verdict** and its difficulty
implication; (3) a **Gadgetbridge-plugin feasibility note** — what maps to stock support
(DIS/CTS/BAS) vs. what needs custom code (FTS/CCS), and whether the auth model is compatible with
Gadgetbridge's pairing flow; (4) an updated **confidence ledger** in the investigation README with
every claim tagged and dual-sourced.

### Opportunistic secondary targets (only if cheaply answerable while in the disassembly)

Carried forward from the hardware-recovery open items — do NOT let these displace the BLE goal, but
capture them if a driver init call site is right there: the still-unmapped I2C addresses for
`Hardware::MS5837`, `Hardware::PAH8316LS`, `Hardware::PCA9420`, the `BMM350` confirmation, and the
`0x40`/`0x58` anomalies — each resolved definitively by finding the driver `config()` call site and
reading the I2C address immediate operand (the method item #7 in the README's "Next experiments").

---

## 7. Output & bookkeeping

- Write findings into **this investigation folder** as a new spec doc (e.g.
  `BLE-COMPANION-protocol-spec.md`) plus updates to `README.md`'s ledger. Keep the **dump and any
  decompilation out of git** (Guardrail).
- Every claim: **tag CONFIRMED / LIKELY / UNVERIFIED / REFUTED with its method and both sources.**
  Re-state what would falsify each CONFIRMED claim. When a first guess is refuted (it will happen),
  record the refutation as clearly as the confirmation — the prior README's DRV2605→DRV2625 and
  MAX17048→MAX17262 entries are the model.
- If you hit a step needing a resource you don't have (a capture, a datasheet, Ghidra, an approved
  active-probe step), **stop and ask me** with the exact thing you need and why — do not guess past
  a missing input.

---

## Appendix — one-paragraph orientation for the impatient

The watch is an STM32U5A5 with a BlueNRG-2 BLE radio (SPI), no Wi-Fi, apps fully unsandboxed, and a
verified ~2.04 MB kernel dump in hand (`flash_real.bin`, CRC32 `0xBCD2F8E0`). It exposes six BLE
services; three are stock (DIS/CTS/BAS), one is Apple-notification (NTS/ANCS), and two are
proprietary: **FTS** (file transfer — how `.fit` activities leave the watch) and **CCS** (custom
commands). The goal is to reverse FTS/CCS enough to build an independent Gadgetbridge/standalone
companion that syncs without the Una app. The single decisive unknown is whether FTS is gated by
app-layer auth beyond BLE bonding. Recover the GATT table and that auth answer from **two**
independent streams — disassembly of the dump and a passive HCI/sniffer capture of the real Una app
syncing — and refuse to call anything confirmed until both streams agree and a real `.fit` has been
round-tripped off your own watch. Locate, but never extract, any key or the OTA crypto.
