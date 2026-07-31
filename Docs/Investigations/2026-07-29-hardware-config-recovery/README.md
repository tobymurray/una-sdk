# UNA Watch hardware-config recovery

Own-hardware investigation: characterize the real STM32-class MCU and peripheral config
inside the UNA Watch, using the "no MPU isolation" read/write primitive from a sideloaded
`.uapp`, to support building independent firmware. Physical unit, legitimate ownership.

**Method:** edit `Docs/Tutorials/HelloWorld/Software/Libs/Sources/Service.cpp` to peek raw
32-bit words from candidate peripheral/core addresses, buffer them, write the buffer to a
file on watch storage (`Apps/HelloWorld/peek_sweepN.txt`) via `mKernel.fs`, build with
mainline `arm-none-eabi-gcc`, deploy over USB-MSC, cold-boot the watch, launch HelloWorld
~5s, reconnect, read the file back. See `[[project_build_deploy_apps]]` memory for the
verified build/deploy workflow.

**Ground rule:** nothing here is trusted on a single read. Every claim below is tagged
CONFIRMED / LIKELY / UNVERIFIED / REFUTED with its corroborating method. "No fault" is not
treated as "correct" — an all-zero read can mean wrong base, unclocked peripheral, or a
genuinely absent peripheral on this exact part number.

## Verification ledger

| Claim | Status | Method |
|---|---|---|
| MCU family = STM32U5Ax, DEV_ID 0x481 | **LIKELY** | DEV_ID not yet re-read on this unit (sweep #1 didn't include DBGMCU; sweep #3, in flight, adds it). 0x481→STM32U5Ax is confirmed as a real ST DEV_ID mapping via ST's SESIP security-guidance doc. Exact chip is further narrowed below. |
| Exact part = STM32U5A5 (not U5A9) | **CONFIRMED** | Three independent methods agree: (1) physical teardown already read the chip marking directly off the die as STM32U5A5 ([[project_una_teardown_hardware]] memory, prior session); (2) FLASH_OPTR word2 (0x0BF9007F) decodes to boot address `0x0BF90000`, confirmed by multiple sources as the STM32U5 system-bootloader ROM address, with one source naming STM32U5A5 explicitly; (3) the LTDC-all-zero read matches exactly what the real ST datasheet (DS13543, downloaded and parsed locally) predicts for U5A5 parts — **LTDC = No** for every U5A5 SKU, **LTDC = Yes** only for U5A9/599/5Fx/5Gx. All three independent signals converge; this closes the MCU-identity question. |
| RDP Level 0 (open, unprotected) | **CONFIRMED, primary source** | RM0456 §7.9.13 (`~/Downloads/rm0456-...pdf`, user-supplied, now parsed) gives the exact bit layout: `RDP[7:0]` = bits[7:0], `0xAA` = "Level 0 (readout protection not active)". Our read (`0xAA`) matches exactly. Bonus finding: **RM0456 lists the ST factory production value as `0x1FEF F8AA` — byte-for-byte identical to what we read.** The security config on this unit is the untouched factory default, not something anyone deliberately opened. |
| TZEN = 0 (TrustZone off) | **CONFIRMED, primary source** | RM0456 §7.9.13: bit 31 = TZEN, "0: Global TrustZone security disabled." Our read has bit31=0. Independently corroborated by sweep #3's `SAU_CTRL`/`SAU_TYPE` (both 0, live core state). Two mechanisms (option byte + live SAU) and now a primary-source bit definition all agree. |
| **App isolation / privilege — the core primitive** | **CONFIRMED: no isolation** | Sweep #3, all read-only: `CONTROL`=0x00000006 → **nPRIV=0** (app thread runs **privileged**, not just unprivileged-with-holes), SPSEL=1. `MPU_TYPE`=0x00000800 → 8 regions implemented in silicon, but `MPU_CTRL`=0x00000000 → **ENABLE=0, PRIVDEFENA=0** — the MPU is entirely switched off (all 8 region RBAR/RLAR read back as unprogrammed zero, consistent with an MPU that was never turned on rather than one enforcing a real policy). Combined with TZEN=0 above (no Secure/Non-secure split either), there is no hardware isolation mechanism active at all between this app and the rest of the system. This settles the primitive definitively without needing any risky write test. |
| Write primitive | **CONFIRMED (own SRAM only so far)** | `WRTEST` wrote/read back a 4-word pattern (incl. 0xDEADBEEF) at `0x200FE360` — PASS. This only proves the trivial case (app writing its own static buffer); it does not yet prove cross-region write into kernel-owned SRAM, since with the MPU off and CPU privileged that *should* also succeed but hasn't been tried against anything outside the app's own segment yet — deliberately deferred per the safety guardrails ("never blind-write kernel structures") until kernel SRAM layout is better understood. |
| DBGMCU IDCODE (this exact unit) | **CONFIRMED (DEV_ID), open item (REV_ID letter mapping)** | Read `0x30036481` at 0xE0044000. Low 12 bits = **DEV_ID 0x481**, matching the STM32U5Ax mapping from ST's SESIP doc (now confirmed on-die, not just assumed from family). Upper 16 bits = **REV_ID 0x3003** — a web-sourced table listed 0x481→REV_ID 0x3001 ("rev W") for the family in general, so this unit is evidently a different (likely later) stepping; I don't yet have an authoritative REV_ID→rev-letter table entry for 0x3003 specifically. Minor open question: bits[15:12] of the read value are 0x6, not 0 — datasheet/RM typically documents this nibble as reserved; unconfirmed whether that's expected on this die or a decode-position error, flagged for RM0456 cross-check. |
| `NSBOOTADD0` = 0x08000000, system bootloader = 0x0BF90000 | **CONFIRMED, primary source** | RM0456 §7.9.14 (`FLASH_NSBOOTADD0R`, offset 0x44) gives the ST production value as `0x0800 007F` — matches our word1 exactly — and its own worked example states `NSBOOTADD0[24:0] = 0x017F200` (→ `0x0BF9 0000` after the documented `<<7` granularity) means "Boot from system memory bootloader." Our word2 (`0x0BF9007F`, read from offset 0x48, i.e. the paired `NSBOOTADD1R`) matches this exact documented bootloader-boot encoding. No longer resting on secondary web sources — this is a direct RM0456 match. |
| `FLASH_ACR` = 4 wait states + prefetch enabled | **CONFIRMED** | Read at 0x40022000 = `0x00000104` → LATENCY[3:0]=4, PRFTEN(bit8)=1. FLASH peripheral base 0x40022000 is a standard, extremely well-established STM32 constant across F0–H7–U5. |
| RCC base = 0x46020C00 | **CONFIRMED, primary source (base + register offsets)** | RM0456 Table 6 (memory map, `~/Downloads/rm0456-...pdf`, now parsed) lists RCC at exactly `0x4602 0C00` (nonsecure), matching our base and the earlier Zephyr `stm32u5.dtsi` cross-check. Table 119 (RCC register map) further confirms `RCC_CR` at offset 0x00, offset 0x04 reserved, `RCC_ICSCR1` at 0x08, `RCC_ICSCR2` at 0x0C — our raw dump's offset-0x04 word reads `00000000` exactly as expected for a reserved gap, structurally confirming we're reading real, correctly-aligned RCC registers. Bit-level clock-tree decode (MSIRANGE/PLL) still pending — the bit-position diagram in the extracted text is too visually garbled by the PDF-to-text conversion to decode reliably; needs a cleaner pass (e.g. rendering the actual page image) rather than more text extraction. |
| I2C1/I2C2/I2C3 bases (0x40005400/0x40005800/0x46002800), ~100 kHz (TIMINGR=0x00701F6B), all PE=1 | **CONFIRMED, primary source (bases), REFUTES bus-count claim** | RM0456 Table 6 confirms all three bases exactly, plus gives the family-wide availability grid across 4 device groups (STM32U535/545, U575/585, **U59x/5Ax — our chip**, U5Fx/5Gx): I2C1/2/3 are marked present (`X`) in every group. **I2C4 (`0x4000 8400`) is also marked present in every group, including ours** — confirmed real, safe to probe. I2C5 (`0x4000 9800`) and I2C6 (`0x4000 9C00`) are marked present only in the U59x/5Ax and U5Fx/5Gx groups (not U535/545 or U575/585) — present at the family-group level for our chip, consistent with the datasheet's flat "I2C: 6" count for the whole STM32U5Axxx line (unlike LTDC's row, this wasn't split Yes/No by exact SKU), so likely real on this exact unit too, though not yet directly probed. This directly refutes the original "3–4 I2C buses" claim — it's 6 peripheral blocks, of which we've now confirmed I2C1-4 are genuinely wired into the address map on this exact chip. |
| USART3 = debug UART on PC4 (AF7) | **CONFIRMED (pin-function only)** | The DS13543 alternate-function table lists `PC4 → USART3_TX` in one AF column, independently corroborating the claimed pin-mux (exact AF number not yet extracted from the table this round). |
| SPI1 8-bit master; SPI3 unused | **LIKELY** | SPI1 (0x40013000) CR1=0x1100, CR2=0xF — nonzero/active config. SPI3 (0x46002000) all-zero — consistent with "unused," but per the same caution as LTDC, not yet distinguished from "wrong base." SPI3's base is architecturally a much safer bet than LTDC's (same SRD-domain pattern as the confirmed-plausible LPUART1/I2C3), so this is LIKELY rather than UNVERIFIED. |
| LPUART1 base 0x46002400, BRR configured | **LIKELY** | Nonzero, structured read (BRR-shaped last word 0x0CE200E2) in the same address neighborhood as the now-corroborated RCC base (0x46020C00) and I2C3 (0x46002800) — same SRD/AHB3 peripheral cluster, increasing confidence the whole cluster's base addresses are right. |
| **LTDC base 0x40016800 — REFUTED as "wrong base," reframed as "peripheral absent on this exact SKU"** | **CONFIRMED (base is right, peripheral is absent on this die)** | RM0456 Table 6 confirms `0x4001 6800` **is** the real LTDC base, marked present at the family-group level for U59x/5Ax (and U5Fx/5Gx) — the RM's table only has family-group granularity, it doesn't split U5A5 vs U5A9. The finer-grained ST datasheet DS13543 (Table 2, per-exact-SKU) resolves that ambiguity: **LTDC = No for every literal STM32U5A5\* SKU**, Yes only for U5A9\*. No contradiction between the two documents — the address is correctly allocated in the family memory map, just not implemented/wired on this specific die, which is exactly what an all-zero, non-faulting read means. Display path is therefore SPI/DSI/other-driven or otherwise still unidentified — open item. |
| Cortex-M33 r0p4, ACR 4WS ⇒ ~160 MHz | **UNVERIFIED** | ACR wait-state count is consistent with a range of clock speeds per the U5 ACR table (not yet cross-checked precisely); RCC contents needed to decode PLL M/N/P/Q/R and MSIRANGE. Pending sweep #3 RCC dump + a dedicated decode pass once RM0456 bitfields are in hand. |
| App runs with no MPU isolation, WRITE untested | **PENDING (sweep #3 in flight)** | This round's sweep adds CONTROL/PRIMASK/IPSR, MPU_TYPE/MPU_CTRL + an 8-region RBAR/RLAR walk, and SAU_CTRL/SAU_TYPE — architecturally the correct, decisive, *read-only* test (MPU_CTRL.ENABLE=0 settles it outright; if enabled, the region table shows exactly what's carved out). Also added: a safe write/readback sanity check on the app's own SRAM (baseline only, not proof of no-isolation by itself). |
| I2C bus scan / device inventory | **FIXED and stable across two runs; I2C4 added, present and clean** | Sweep #5 re-ran the scan (I2C1-4) and got **identical results to sweep #4 for I2C1/2/3** (`I2C1 5/112 [36 50 58 5A 61]`, `I2C2 3/112 [40 50 58]`, `I2C3 2/112 [50 58]`), plus a new **I2C4 4/112 `[14 50 58 68]`**. Run-to-run stability of the same three buses is itself good evidence the scan logic is now solid. |
| **IMU = BMI270, CONFIRMED** | **CONFIRMED — exact register match** | `I2C4 addr=0x68 reg=0x00 → 0x24`. This is an **exact match** to BMI270's documented CHIP_ID constant (0x24), on a bus (I2C4) added and probed for the first time this round. This is the strongest single finding of the I2C phase: one clean read, one real device, one confirmed part number. First confirmed row for the hardware inventory table. |
| **Device @ 0x5A — RESOLVED: DRV2625, not DRV2605(L)** | **CONFIRMED (via flash-dump driver strings)** | Sweep #5's register reads correctly showed a real device that didn't match DRV2605(L)'s documented STATUS/MODE patterns. Sweep #6's flash dump explains why: the kernel contains `Hardware::DRV2625::readReg/writeReg` — a related but distinct TI part with its own register map. Register-probing correctly detected "real device, wrong guess"; the flash dump supplied the right name. |
| **Device @ 0x36 — MAX17262 identity CONFIRMED, but register-map guesses don't hold up** | **CONFIRMED (part), register content still uninterpretable** | Chip identity from flash-dump strings stands. Sweep #7 re-read reg 0x08 properly as 16-bit (`b0=0x37 b1=0x1D`) — but `b0` alone still doesn't match either of the two earlier 1-byte reads at the same offset (0x61 in sweep #4, 0xB4 in sweep #5, 0x37 in sweep #7's own 1-byte probe). A real VERSION/ID register should be constant across boots regardless of read width; this one keeps changing, meaning **reg 0x08 on this specific chip almost certainly isn't a VERSION register at all** (more likely a live measurement, e.g. voltage/temperature) — my register-offset guesses were simply wrong for this chip's actual map, not a width problem. Would need the real MAX17262 datasheet to interpret meaningfully; not worth further blind guessing. |
| **0x50 / 0x58 — re-tested, both trending back toward "not clean single real devices"** | **UNVERIFIED, weaker than last round for both** | `0x50`: the N24S64B-EEPROM hypothesis looked promising last round, but sweep #7 got **inconsistent readings within the same run** — I2C2 gave `0x4D` on a 1-byte read and `0x01,0x00` on a 2-byte read of the nominally same register, moments apart. Real EEPROM content shouldn't shift between two back-to-back reads. `0x58`: found something new and genuinely unexplained — reg 0x0F reads **`0xFD`** (not `0xFF` like reg 0x00/0x01) **identically on all four tested buses**. That's a structured, register-dependent, but still cross-bus-identical pattern — too consistent to be random noise, too uniform across 4 independent physical peripheral blocks to be 4 coincidentally-identical real chips. This doesn't fit either the "real device" or "pure electrical artifact" model cleanly; flagging as an open anomaly that likely needs different tooling (an oscilloscope/logic analyzer on the physical bus) rather than more firmware-side guessing. |
| **BMM350 magnetometer — specific register hypothesis REFUTED, identity unconfirmed** | **REFUTED as tested; string-only evidence remains** | Sweep #7 read I2C4/0x14 reg 0x00, expecting `0x33` (BMM350's documented CHIP_ID) — got `0x00` instead. Either the register offset/expected value recalled this session was wrong, the chip needs an init/wake sequence before CHIP_ID is valid, or 0x14 isn't actually BMM350. The flash-dump string (`"Test Magnetometer (BMM350)..."`) is unaffected by this and remains the only evidence either way — net effect is the identification is back to single-source, not upgraded to double-confirmed as hoped. |
| **PCA9420 @ 0x61 — still just an address coincidence** | **UNVERIFIED** | Read reg 0x00/0x01 (no confident expected values available), got `0x01`/`0x00` — same as the earlier generic probes. Neither confirms nor refutes; I don't have a reliable PCA9420 register map to check against. Real device present at 0x61; identity still resting entirely on "0x61 happens to be PCA9420's documented default address," which is suggestive but not evidence-based confirmation. |
| **INA226 MANUFACTURER_ID @ 0x40 — REFUTED** | **REFUTED (INA226 specifically)** | Read reg 0xFE expecting ASCII "TI" (`0x5449`) — got `0x0000`. Clean refutation of the INA226 hypothesis specifically. INA219 (an older, simpler part without a MANUFACTURER_ID register) remains untested/plausible, but unconfirmed either way. 0x40's real identity is open again. |
| **I2C5/I2C6 — scanned, both empty** | **CONFIRMED empty on this unit** | 0/112 ACKs on both buses. MS5837 and PAH8316LS (fixed I2C addresses, no address pins) have now been tested — directly or via full bus scan — on all six I2C buses (I2C1-6) without a single ACK anywhere. Real negative result: their bus/address isn't going to be found by more scanning; the next method has to be different (see below). |
| `0x40` (I2C2), `0x61` (I2C1) — still unidentified | **UNVERIFIED** | No matching driver class name turned up in this round's string search either. `0x40`'s INA219/226-style CONFIG-register guess remains plausible but unconfirmed; `0x61` has no candidate. |
| **Full 4 MB flash dump (0x08000000-0x08400000)** | **CONFIRMED, complete, verified 2 independent ways** | All 32×128 KB chunks written, each with an on-device CRC32; reassembled locally and independently re-verified: every chunk's CRC32 recomputed from the actual transferred bytes matches the device-reported value, the running whole-image CRC32 matches (`0xBCD2F8E0`, device and host agree), and all 3 spot-check byte sequences match between the manifest and the reassembled file. **New finding:** the real firmware image is only **~2.04 MB** (ends at flash offset `0x20A140`, i.e. address `0x0820A140`) — everything from there to `0x08400000` is erased flash (`0xFF` fill), not real content. Notably the erased region starts partway into the second 2 MB bank (`DUALBANK` was already confirmed set in `FLASH_OPTR`), consistent with a single contiguous image that happens to spill slightly past the 2 MB bank boundary before the rest is unused headroom. |
| **Dual vector table (boot region 0x08000000-0x08060000 vs. kernel VTOR=0x08060000)** | **CONFIRMED, directly from the dump** | Parsed the first 16 bytes at both flash offsets: **`0x08000000`**: `SP=0x201F0000, Reset=0x08001C45` (Thumb, odd address). **`0x08060000`**: `SP=0x20250000, Reset=0x0806CE45`. Both are self-consistent, plausible Cortex-M vector tables (valid SRAM stack pointers, reset vectors pointing into flash just past their own table) — this is real structural evidence of two distinct stages: a small bootloader at `0x08000000` and the main kernel at `0x08060000`, matching the original brief's claimed flash layout exactly. |

## Round data

- **Sweep #1** (`peek_sweep.txt`, already on-device, captured 2026-07-29): FLASH/OPTR/SYSTICK/NVIC-IPR/I2C1-3/SPI1/USART3/LPUART1/SPI3/LTDC-guess. Analyzed above.
- **Sweep #3** (`peek_sweep3.txt`, run + retrieved 2026-07-29): CPU privilege+MPU+SAU (isolation primitive — **settled**), DBGMCU IDCODE, RCC raw dump (below, undecoded), SRAM write/readback sanity check (PASS), I2C1/2/3 address scan (**unreliable, bug identified**, see ledger).
  - (Sweep numbering follows the in-code comment history in `Service.cpp`; there is no "sweep #2" data file — #1 in this doc = the code's "SWEEP #2".)
- **Sweep #4** (`peek_sweep4.txt`, run + retrieved 2026-07-29): re-confirmed everything from #3 identically (isolation primitive, DBGMCU, RCC raw, write test — all match). I2C-scan fix **worked** (realistic single-digit ACK counts, see ledger). WHO_AM_I probes got 2 real hits, both need a second corroborating read (see ledger). Candidate table used:

  | Candidate | Addr | Reg | Note |
  |---|---|---|---|
  | LSM6DSO-family (IMU) | 0x6A | 0x0F | expect ~0x6C |
  | LSM6DSO-family (IMU, alt addr) | 0x6B | 0x0F | expect ~0x6C |
  | BMI270 (IMU) | 0x68 | 0x00 | CHIP_ID, expect 0x24 |
  | BMI270 (IMU, alt addr) | 0x69 | 0x00 | CHIP_ID, expect 0x24 |
  | ICM-42xxx (IMU) | 0x68 | 0x75 | same addr as BMI270, different reg |
  | BMP3xx (baro) | 0x76 | 0x00 | CHIP_ID, expect 0x50/0x60 |
  | BMP3xx (baro, alt addr) | 0x77 | 0x00 | CHIP_ID |
  | LPS22-family (baro) | 0x5C | 0x0F | WHO_AM_I, expect 0xB1/0xB3/0xB4 |
  | LPS22-family (baro, alt addr) | 0x5D | 0x0F | WHO_AM_I |
  | MAX17048/55 (fuel gauge) | 0x36 | 0x08 | VERSION reg, raw only |
  | DRV2605(L) (haptic) | 0x5A | 0x00 | STATUS, top 3 bits = DEVICE_ID |

  Not yet covered: touch controller (GT911 uses 16-bit register addressing, needs a different transaction shape — not attempted this round), PMIC/charger (too many plausible parts/addresses to guess confidently), BLE radio and GNSS (both expected to be SPI/UART-based, not I2C), I2C4/5/6 (bases not yet sourced from a primary doc — RM0456 still unavailable this session).

- **Sweep #6** (`peek_sweep6.txt` + `dump_manifest.txt` + `dump_000000.bin`..`dump_3E0000.bin`, run + retrieved 2026-07-29): re-confirmed all register/I2C findings from sweep #5 byte-for-byte identical (isolation, DBGMCU, RCC, I2C ACK lists). Then performed the full 4 MB flash dump — 32×128 KB chunks, on-device CRC32 per chunk plus a running whole-image CRC32, three spot-check byte ranges. Pulled to host and reassembled with `reassemble_dump.py`: **all 32 chunks verified clean, whole-image CRC32 matched exactly (`0xBCD2F8E0`, device vs. independently host-recomputed)**. Real image size ~2.04 MB (offset `0x20A140`); rest is erased flash. A `strings -n 6` pass on the real portion resolved most of the hardware-inventory table (see above) — this is now the primary identification method going forward, far more reliable than blind I2C register probing.

### RCC raw dump (0x46020C00, 64 words) — undecoded, raw evidence for a future precise pass

```
46020C00: 03000535 00000000 048945ED 00084210
46020C10: 00100514 0000012A 00000000 0000000F
46020C20: 00000000 00000000 0006021D 00000000
46020C30: 00000000 00010009 00000000 01010280
46020C40: 00000000 01010280 00000000 00000000
46020C50: 00000000 00000000 00000000 00000000
46020C60: 00000000 00000000 00000000 00000000
46020C70: 00000000 00000000 00000000 00000000
46020C80: 00000000 00000000 D0201101 C800007F
46020C90: 80000000 80000004 00000000 00640014
46020CA0: 00000002 00041000 002038C2 00000000
46020CB0: FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF
46020CC0: 00000000 FFFFFFFF FFFFFFFF FFFFFFFF
46020CD0: FFFFFFFF 00000000 00203800 00000000
46020CE0: 08905420 00000000 00000F44 00000000
46020CF0: 0C00A987 00004400 00000000 00000000
```

Not yet decoded against confirmed RM0456 field offsets (still couldn't fetch RM0456 this
session — ST portal blocked, mirrors 403'd). One structural observation, offered as a
hypothesis only: the block at 0x46020CB0–0x46020CDC reads distinctly as `0xFFFFFFFF` rather
than `0x00000000` like its neighbors — a different reset pattern than the rest of the dump.
A plausible (unverified) explanation given TZEN=0 is confirmed: if these are per-peripheral
security-attribution registers (RCC_AHBxSECSR/APBxSECSR-style), an all-1s read could mean
"everything reads as accessible/non-secure" under a disabled TrustZone config — but this is
a guess pending the real bit layout, not a claim.

## Hardware inventory

The full 4 MB flash dump (see below) turned out to be a far better identification method than
blind I2C register-guessing: the kernel's own driver class names, extracted as plain strings
from the dumped image, name almost every chip directly. `strings -n 6` on the real (non-blank)
2.04 MB of the dump, filtered to `Hardware::<ClassName>` and similar patterns, resolved most of
the table below in a single pass — no register-map guessing needed. This also closes the loop on
two "refuted" I2C guesses from the register-probing phase: they weren't wrong addresses, they
were the *wrong chip family* (DRV2625 vs DRV2605, MAX17262 vs MAX17048 — related parts, different
register maps, which is exactly why those probes returned real-but-nonmatching data).

| Role | Part | Bus / addr | Evidence | Confidence |
|---|---|---|---|---|
| MCU | STM32U5A5 | — | 3 independent methods, see ledger (physical teardown marking, OPTR bootloader-address decode, LTDC-absence datasheet match) | **CONFIRMED** |
| IMU (accel+gyro) | **BMI270** | I2C4 (0x40008400) / 0x68 | `CHIP_ID` reg 0x00 = `0x24` (exact register match) **and** `Hardware::BMI270`-adjacent driver strings ("BMI270 accelerometer", "BMI270 gyroscope", "bmi270_init", etc.) in the dumped kernel — two independent methods | **CONFIRMED** |
| Barometer / pressure | **MS5837** (TE/Measurement Specialties) | UNKNOWN — its fixed address (0x76) never ACKed on any of I2C1-6 | `Sensor::MS5837::getSubSensor`, "MS5837 pressure", "MS5837 temperature" — kernel driver strings, still solid. Bus mapping is now a genuine negative result (sweep #7 exhausted all 6 I2C buses at its only possible address) — either it's on a bus this session hasn't characterized at all (SPI? a 7th I2C instance not in RM0456's table?), or it's simply not populated on this exact unit | part **CONFIRMED**, bus/addr **UNKNOWN — needs a different method, not more I2C scanning** |
| PPG / optical HR AFE | **PAH8316LS** (PixArt) | UNKNOWN | `Hardware::PAH8316LS` driver class name in the dumped kernel — solid. No confidently-known default address was ever tried (PixArt's addressing for this exact part wasn't in hand), so this is a gap rather than a tested negative like MS5837 | part **CONFIRMED**, bus/addr not meaningfully tested |
| Magnetometer | **BMM350** (Bosch), single-source | I2C4 / 0x14 is a real device, identity unconfirmed | Log string `"Test Magnetometer (BMM350)..."` — the only evidence. Sweep #7's confirmation attempt (CHIP_ID @ reg 0x00, expected `0x33`) got `0x00` instead — refuted as tested, not upgraded to double-confirmed like hoped. Real device answers at I2C4/0x14; whether it's actually a BMM350 is still open | **LIKELY** (string only); register-level confirmation attempt failed, needs a different register/init sequence or the real datasheet |
| Fuel gauge / coulomb counter | **MAX17262** (Maxim ModelGauge m5) | I2C1 / 0x36 (real device, confirmed present by register probing) | `Hardware::MAX17262::readReg/writeReg(uint8_t, uint16_t&)` — a **16-bit** register access API, which explains why sweep #4/#5's 1-byte reads at "reg 0x08" returned inconsistent values (0x61 then 0xB4) between boots: wrong access width for this specific chip, not a bad address | **CONFIRMED** (part + address), access method needs redoing as 16-bit reads |
| Haptic / vibration driver | **DRV2625** (TI) | I2C1 / 0x5A (real device, confirmed present by register probing) | `Hardware::DRV2625::readReg/writeReg(uint8_t, uint8_t)` in the dumped kernel — a close relative of DRV2605(L) with a different register map, explaining the earlier STATUS/MODE mismatches exactly | **CONFIRMED** (part + address) |
| PMIC / charger | **PCA9420** (NXP) | not yet mapped to an I2C address | `Hardware::PCA9420::config/isReady/readReg/writeReg` driver class in the dumped kernel; separately, "BCD: Charger detected" log string confirms battery-charger-detection logic exists | **CONFIRMED** (part), bus/addr not yet probed |
| Touch / wear-detect controller | **likely absent — no touchscreen** | — | No touch-controller driver class name found anywhere in the dump; `touchgfx::*` strings are ST's TouchGFX **graphics** library (matches this SDK's own tutorials/examples), not evidence of a touch **input** chip. Combined with `HWButtons`/`Filter::DebounceDaemon` strings, this reads as a **button-only** device, not a touchscreen one | **LIKELY absent**, pending explicit refutation |
| BLE radio | **BlueNRG-2** (STMicro), SPI | not yet mapped to a specific SPI/CS pin | `Hardware::BlueNRG2::config(Interface::ISpi*, Interface::IGpo*, Interface::IExti*, Interface::IGpo*)` — SPI-based exactly as the original brief's candidate expected | **CONFIRMED** (part), SPI bus/CS/IRQ pins not yet mapped |
| GNSS | **Airoha AG3335M** | LPUART1 (candidate, not yet directly confirmed) | `Airoha::AG3335M::start(Airoha::StartType, uint32_t)`, `Airoha::Aiding::config(...)`, `Airoha::ContextPool` — exactly the part named as a candidate in the original brief | **CONFIRMED** (part), exact UART pairing not yet independently verified |
| Display panel + controller | **LS012B7DD06A** (Sharp/JDI memory LCD) | not LTDC — likely SPI | `Hardware::LS012B7DD06A` driver class name. Directly explains the all-zero LTDC read from sweep #1: this display was never LTDC-driven at all, on a chip where LTDC isn't even implemented (see ledger) | **CONFIRMED** (part) |
| External storage | eMMC (part unknown) | SDMMC1 (0x420C8000 per RM0456, not yet probed) | "eMMC CARD_TYPE...", "eMMC recovery", "eMMC wedged" driver/log strings confirm eMMC, not raw SD, as expected | interface **CONFIRMED**, exact chip/CID unread |
| Unidentified extra IC | **N24S64B** (I2C EEPROM, likely board/component unique-ID storage) | I2C1 or I2C2 / plausibly `0x50` (real device ACKed, real device area) | `Hardware::N24S64B::readUniqueId/readBytes/writePage` — classic 24C64-family EEPROM API. `0x50` is the standard base address for this EEPROM family, which fits the earlier "real device but couldn't identify it" finding for 0x50 far better than the crosstalk-artifact hypothesis | **LIKELY** (part), address correlation not yet independently confirmed by a register read |
| Buzzer | PWM-driven, no I2C chip needed | — | `Buzzer::init()`, `ID_BUZZER`/`ID_VIBRO` test-mode strings — no chip class name, consistent with a directly PWM-driven piezo buzzer as originally expected | **CONFIRMED** (no discrete IC) |

**Raw ACK lists (address-phase only, this session):**
- I2C1 (0x40005400): `0x36 0x50 0x58 0x5A 0x61`
- I2C2 (0x40005800): `0x40 0x50 0x58`
- I2C3 (0x46002800): `0x50 0x58`
- I2C4 (0x40008400): `0x14 0x50 0x58 0x68`

Note `0x50`/`0x58` appear on every bus and are now suspected to be a marginal-ACK artifact rather than real devices (see ledger) — not necessarily 4-8 additional real ICs. `0x14` on I2C4 is new and not yet probed with any candidate register.

## Primary sources used this round

- STM32U5Axxx datasheet DS13543 Rev 3 (`~/Downloads/stm32u5a5aj.pdf`, user-supplied local copy) — device feature matrix (Table 2), AF/pinout tables. Most authoritative source available this session; RM0456 (register-level reference manual) could not be fetched automatically (ST portal blocked, mirrors 403'd) — still needed for exact bitfield offsets (FLASH_OPTR fields beyond RDP, RCC register layout).
- Zephyr `stm32u5.dtsi` device tree — independent real-silicon-derived peripheral base address cross-check (RCC).
- ST community/SESIP docs — DEV_ID table, system bootloader address.

## Next experiments (priority order)

1. ~~Retrieve and analyze `peek_sweep3.txt`~~ **done.** Isolation primitive settled; I2C scan bug identified.
2. ~~Retrieve and analyze `peek_sweep4.txt`~~ **done.** I2C scan fix confirmed working (realistic counts); two devices tentatively found at 0x36 and 0x5A on I2C1.
3. ~~Sweep #5 (disambiguation + I2C4)~~ **done.** One clean confirmed win (BMI270 @ I2C4/0x68). DRV2605(L) guess refuted (device real, ID wrong). MAX1704x guess weakened (register content changes between boots — inconsistent with a real VERSION register). 0x50/0x58 cross-bus comparison points toward "not real devices, likely a marginal-ACK artifact," especially 0x58 (uniform `0xFF` on all 4 buses).
4. ~~Get RM0456~~ **done** (user supplied local PDF) — used to primary-source-confirm nearly every base address and several bitfields (see ledger). Clock-tree bit-level decode still open (the extracted bit-diagram text is too garbled to trust; would need the actual page image).
5. ~~Full 4 MB flash dump~~ **done, verified 2 ways.** Confirmed the pivot in item 5 was right: a `strings` pass on the dumped kernel resolved most of the hardware-inventory table in one shot (BMI270 double-confirmed, MS5837, PAH8316LS, MAX17262, DRV2625, PCA9420, BlueNRG-2, Airoha AG3335M, LS012B7DD06A display, N24S64B EEPROM, eMMC, no touch controller found) — see the Hardware inventory table above. Real image is only ~2.04 MB of the 4 MB flash; `flash_dump.bin` + `flash_real.bin` (the trimmed non-blank portion) are on the host now, reassembled via `reassemble_dump.py`, `strings` output in `flash_strings.txt`.
6. ~~Sweep #7 (BMM350 confirm, PCA9420, 16-bit re-reads, I2C5/6, 0x50/0x58 follow-up)~~ **done, mixed results.** BMM350's specific CHIP_ID guess refuted (real device, wrong register/value); PCA9420 still just an address coincidence; MAX17262's register map guesses don't hold up (identity solid, register content uninterpretable without the real datasheet); INA226 MANUFACTURER_ID refuted cleanly; I2C5/I2C6 both empty — MS5837/PAH8316LS confirmed absent from every I2C bus on this unit; 0x50 weakened further, 0x58 deepened into a genuine unexplained anomaly (register-differentiated but cross-bus-identical). See ledger for all of it.
7. **Recommended pivot, stronger than before:** blind I2C register-guessing has now failed on essentially everything it touched this round. The addresses that were never found (MS5837, PAH8316LS) and the ones that resisted every register guess (PCA9420, 0x40, 0x50, 0x58, the exact BMM350 register) all need a fundamentally different method: **disassemble `flash_real.bin`** (Thumb2, load at 0x08000000, real vector tables confirmed at 0x08000000 and 0x08060000) and find the actual driver init call sites — the code that calls `Hardware::MS5837::config(...)` or similar will have the literal I2C address as an immediate operand, which is a definitive answer instead of another guess. This is now the highest-value next step for hardware ID, not another I2C sweep.
8. SWD continuity/attach check (read-only) as a second, independent method to corroborate RDP=0, once physical access to the PA13/PA14/PB3 test points is set up.
9. GTZC (Global TrustZone controller) registers — not read yet; would settle WRP/watermark/GTZC state from the original security-posture ask, now that RM0456 gives exact register offsets (chapter 5).
10. Clock-tree exact frequency decode — RCC raw dump captured three times now (sweeps #4-7, byte-identical every time) but the bit-level MSIRANGE/PLL decode is still blocked on the garbled PDF-extracted bit diagram; would need the actual RM0456 page image, not more text extraction.

## Phase 2 — BLE GATT / FTS / CCS protocol recovery (Phases A/B/C all closed for the read path)

Goal, plan, and guardrails: `BLE-COMPANION-disassembly-prompt.md`. Running findings, full ledger,
and confidence tags: **`BLE-COMPANION-protocol-spec.md`** (this folder) — do not duplicate that
detail here; summary only:

- **A real BLE capture of the Una app syncing (twice) was obtained** via `adb bugreport` from the
  user's own GrapheneOS phone, decoded with `tshark`. This is the dynamic evidence stream the plan
  called for, and it **settled the core objective**: the FTS whole-file-read protocol
  (opcode `0x10` request / `0x11` streamed response, 128-byte chunks, no per-chunk ack needed) was
  fully recovered from real traffic, and **two real `.fit` activity files were reconstructed
  byte-for-byte from the capture and validated three independent ways** — FIT header field
  arithmetic, a from-scratch CRC-16 recomputation matching the stored file CRC exactly on both
  files, and independent recognition by `file(1)`'s own magic database. This is a genuine,
  end-to-end proof that a companion can pull `.fit` files off this watch today.
- **Auth verdict: CONFIRMED, closed.** A Ghidra headless static pass disassembled the actual FTS
  `readHandler`/`writeHandler`/`listDirHandler` bodies directly: **no call to any bonding/security-
  check function appears anywhere in them.** The bonding-check function's only two callers, traced
  exhaustively, are a generic `isBonded()` accessor and the advertising-mode-selection logic — both
  unrelated to file transfer. Combined with the dynamic capture (standard BLE re-encryption from a
  stored bond, zero extra handshake traffic across five real FTS operations) and the static "no
  crypto vocabulary in the firmware strings" lead, this is now triple-corroborated: **security for
  FTS is standard BLE bonding, full stop — no vendor secret, challenge, or key gates it.**
- **GATT table: every custom service UUID group now CONFIRMED by decompiled constructor code**
  (not just string adjacency) — CCS and CANS each resolve to 1 service + 2 characteristics exactly;
  FTS resolves to *two separate* service objects (one reusing Adafruit's real `0xFEBB` 16-bit
  service UUID with UNA's own characteristic numbering, one a literal Nordic UART Service clone).
  Binding the live capture's attribute *handle numbers* to these confirmed UUIDs is the one
  remaining loose end, and doesn't block a same-firmware companion.
- **CTS independently CONFIRMED** by decoding live Current Time / Local Time Information writes
  byte-for-byte against the standard Bluetooth SIG format, on the exact real-world date/time of the
  capture session.
- **REFUTED, recorded per the ground rule:** the `ADAF...`-prefixed UUIDs looked like a drop-in
  match for Adafruit's public open-source BLE File Transfer Service; checking the real Adafruit
  source shows different characteristic-suffix IDs there — the base UUID pattern was borrowed, the
  protocol was not.
- **CONFIRMED** (byte-exact match to public specs): Apple **AMS + ANCS** UUIDs reused verbatim for
  iOS notification/media; a live-observed characteristic also confirms an Android-notification
  bridge (CANS) is real and actively used, matching a firmware-string-only hypothesis.
- **Remaining open items** (none blocking a read-path companion prototype): binding the live
  capture's attribute handle numbers to the now-confirmed UUIDs (only matters for cross-firmware
  portability); the `uint16` file-size field's ceiling on large recordings; a secondary `0x30`
  command and the EPO upload (`0x20/0x21/0x22`) framing. See the spec doc's §6 for the full list.

## Safety notes honored this round

- All sweep #3 reads are read-only except: (a) a write/readback test confined to the app's own static SRAM buffer (self-contained, cannot affect kernel state), and (b) I2C START/STOP probe pulses (bounded, recoverable by power-cycle, no register/option-byte/boot-region writes).
- No RDP/option-byte/TrustZone/WRP writes. No boot-region (0x08000000–0x08060000) writes.
