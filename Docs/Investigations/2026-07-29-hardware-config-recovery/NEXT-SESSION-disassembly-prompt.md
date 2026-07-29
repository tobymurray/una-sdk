# Handoff prompt: disassemble the UNA Watch kernel dump

Paste everything below into a fresh session (this repo's directory: `/home/toby/git/cpp/una-sdk`).

---

## Context

I own a UNA Watch (STM32U5A5 fitness watch) and I'm doing hardware/firmware research on my own
device — legitimate ownership, not cloning or key theft. A prior session already: got a full
read/write primitive from a sideloaded `.uapp` (apps run unsandboxed — MPU disabled, CPU
privileged, no TrustZone), dumped and verified all 4 MB of flash, and used `strings` on the dump
to identify most of the onboard hardware. That work is fully written up — **read it first**:

- **`Docs/Investigations/2026-07-29-hardware-config-recovery/README.md`** — the living
  investigation doc: full verification ledger (every claim tagged CONFIRMED/LIKELY/REFUTED/
  UNVERIFIED with its method), the hardware inventory table, round-by-round history.
- **Memory index** at `/home/toby/.claude/projects/-home-toby-git-cpp-una-sdk/memory/MEMORY.md`
  — look up `project_una_hw_config_recovery`, `reference_una_kernel_ota_encrypted`, and
  `project_una_teardown_hardware` for cross-session context. This memory system persists
  across sessions in this project directory, so it should already be loaded for you — use it,
  don't re-derive facts it already has.

**Adversarial-verification discipline carries forward**: don't trust a decompiled string or a
guessed calling convention on one read. Corroborate. The investigation doc's ledger is full of
examples where a first guess (DRV2605, MAX17048, INA226) turned out wrong on a second check —
keep that same skepticism here.

## What's already done (don't redo it)

- Full 4 MB flash dump, verified two independent ways (per-chunk CRC32 + whole-image CRC32,
  both recomputed host-side and matched the device's self-report).
- Real firmware image is **only ~2.04 MB** (flash offset `0x0`–`0x20A140`); everything after
  that is erased (`0xFF` fill) — don't waste analysis time on the blank tail.
- **Two confirmed vector tables**, both structurally valid (parsed directly from the dump):
  - `0x08000000`: `SP=0x201F0000, Reset=0x08001C45` — a small bootloader stage.
  - `0x08060000`: `SP=0x20250000, Reset=0x0806CE45` — the main kernel.
- Hardware inventory mostly resolved via kernel `Hardware::<ClassName>` driver strings — see
  the doc's table. **Open items this session should try to close** (all need real code
  addresses, not more guessing — this is exactly why we're disassembling now):
  - `Hardware::MS5837` (barometer) and `Hardware::PAH8316LS` (PPG/HR) — driver classes
    confirmed to exist, but their I2C bus/address was never found (tested and NOT present on
    any of I2C1–I2C6 at their standard default addresses — the address must be non-default,
    read from option bytes/NVM, or the scan technique missed something).
  - `Hardware::PCA9420` (PMIC) — address `0x61` is a plausible guess (matches an unexplained
    I2C1 ACK) but not code-confirmed.
  - The magnetometer: a ProdTest string names `BMM350`, but a register read at the assumed
    CHIP_ID address/offset (I2C4/`0x14`, reg `0x00`, expected `0x33`) came back `0x00` — either
    wrong register, wrong init state, or wrong chip. Needs the real driver code.
  - `0x40` (I2C2) and `0x58` (identical, structured, non-`0xFF` response on 4 separate I2C
    peripheral blocks — genuinely unexplained anomaly, flagged in the ledger) — find what code
    actually talks to these addresses, if any.
  - The OTA decrypt path — **locate only, do not extract or reproduce the key/algorithm.**

## Where the data is

Durable, non-git location (do NOT commit these into the una-sdk repo — proprietary vendor
firmware, keep it out of git):

```
~/una-firmware-dumps/2026-07-29-flash-dump/
├── flash_dump.bin      # full 4 MB, offset 0 = flash address 0x08000000
├── flash_real.bin      # first 0x20A140 bytes only (the real, non-blank image) - use this one
├── flash_strings.txt   # `strings -n 6` output already run on flash_real.bin
└── chunks/              # the 32 original 128KB dump chunks + manifest, if you need to re-verify
```

## Tooling

`radare2` is **not installed** but is in the official Arch repo:
```
sudo pacman -S radare2
```
(`arm-none-eabi-objdump`/`gcc`/`gdb`-less toolchain IS already installed and was used for all the
firmware builds so far — `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb
--adjust-vma=0x08000000 flash_real.bin` works right now with zero setup as a quick fallback, but
it's a flat linear disassembly with no function/xref analysis, which is not enough for this task
— get radare2 for real analysis (auto function detection, string xrefs, Thumb-aware).**

Open it as:
```
r2 -b 16 -m 0x08000000 -a arm -e asm.bits=16 ~/una-firmware-dumps/2026-07-29-flash-dump/flash_real.bin
```
(Cortex-M33 is Thumb2 almost everywhere — force 16-bit ARM/Thumb mode; verify a few known-good
addresses like the confirmed vector table / reset handlers disassemble sanely before trusting
auto-analysis broadly, since a wrong bitness/mode assumption at just the entry point can still
produce plausible-looking-but-wrong code downstream.)

**Seed symbols before analyzing**: `Libs/Source/AppSystem/linker/LibC/libc_exports_0.0.3.ld` in
this repo has 336 `PROVIDE(name = 0x0803xxxx)` lines — real libc function addresses inside this
exact kernel image. Write a small script to turn these into `f` (flag) or `afn` (function
rename) commands in r2 so libc calls resolve to real names instead of `sub_xxx` — this alone
will make the driver code much more readable by process of elimination (anything NOT a
recognized libc call in the hot paths near your target strings is more likely to be
UNA-specific driver code).

## Method for the specific open items

For each unresolved chip (MS5837, PAH8316LS, PCA9420, BMM350, the `0x40`/`0x58` mystery):
1. Find the string's address in `flash_strings.txt`'s source binary (r2: `/ Hardware::MS5837` or
   similar, or `axt` on the string's data address once located to find cross-references).
2. Walk backward/forward from the xref to the actual driver `config()`/`init()` call — I2C
   addresses are typically passed as an 8-bit immediate (`MOVS r1, #0xXX` or similar) right
   before the call, or loaded from a small const/data table nearby.
3. Cross-check anything you find against the existing register-probe evidence in the doc's
   ledger — e.g. if you find PCA9420's real init code passes `0x61`, that upgrades the existing
   UNVERIFIED ledger entry to CONFIRMED; if it passes something else, that's a clean refutation
   worth recording just as clearly.
4. Update `Docs/Investigations/2026-07-29-hardware-config-recovery/README.md`'s ledger and
   hardware-inventory table with what you find, tagged with the method (e.g. "disassembly:
   `Hardware::PCA9420::config` call site at flash offset 0xNNNNN, I2C address operand = 0xNN").

Also worth pursuing while in there (from the original brief, still open): BLE HCI
framing/GATT UUIDs from the `Ble::` namespace code, the GPS/Airoha command protocol, power
management (sleep modes, wake sources), and — for the "extend in place" feasibility question —
any RAM-resident function-pointer tables/vtables that would make good hook points, and whether
the kernel exposes/uses `FLASH_NSKEYR`-unlock + program/erase routines an app could call into.

## Guardrails (carry forward from the original brief)

- Own hardware, defensive/interoperability research — this is legitimate, but stay within it:
  don't extract or reproduce the OTA decryption key/algorithm, don't redistribute the dumped
  firmware or large decompiled excerpts of it.
- This phase is pure static analysis on files already on disk — no device interaction needed,
  so none of the physical-access safety notes from earlier rounds apply here. If you do end up
  wanting a new live register/I2C round based on what you find in the disassembly, that's a
  separate step requiring the physical build/deploy/cold-boot workflow documented in the
  `project_build_deploy_apps` memory — flag it as a proposed next step rather than assuming you
  can execute it from a static-analysis session.
