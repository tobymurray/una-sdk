# Handoff prompt: hunt injectable seams in the UNA Watch kernel for per-peripheral Rust replacement

Paste everything below into a fresh session. This prompt is **self-contained** — it
aggregates every relevant finding so far so you don't re-derive them. Facts are
tagged with confidence; treat `CONFIRMED` as load-bearing and `LIKELY`/`UNVERIFIED`
as a lead to re-check. Read the **Guardrails** before doing anything.

---

## 0. Objective

Find and document the **seams** in the closed vendor kernel where a Rust
implementation can be swapped in for a single peripheral **at runtime**, toggled
against the native driver — the incremental "eat the OS" migration path.

The strategy: you cannot refactor a closed monolith function-by-function, but the
kernel is **C++ with vtable-dispatched drivers and a polymorphic HAL**, so the
dispatch boundaries are already seams. Keep the vendor kernel running (power, BLE,
scheduler intact) and replace **one peripheral's driver** with Rust — validated in
the real running system, A/B toggleable, fully reversible via SWD.

Concretely, produce a **seam catalog**: for each peripheral, the injectable seam,
its target address(es), the object/vtable layout, the live-object locus, the
FreeRTOS concurrency context it runs in, the hook mechanism, and a risk rating —
enough for someone to write a runtime hook that intercepts a native driver call,
call through to the original, and flip to a Rust replacement.

**This phase is analysis, not implementation.** Recovering and documenting the
seams is the deliverable; writing the hook harness and the Rust drivers is
downstream work that this catalog makes possible.

---

## 1. Guardrails (read first)

- **Own hardware, own data, interoperability/defensive research.** Building an
  independent Rust firmware for a watch you own is the whole point.
- **Do not extract or reproduce the OTA decryption key/algorithm.** Locate the
  auth/crypto path if you cross it; do not lift secrets. (A prior memory flagged
  the OTA image as encrypted.)
- **Keep the dumped firmware and any decompilation OUT of the `una-sdk` git repo** —
  proprietary vendor firmware. Only the *analysis* (addresses, vtable layouts,
  seam catalog, recipe) belongs in the investigation doc.
- **This phase is pure static analysis** on files already on disk — no device
  interaction. If you later propose *live* hook experiments, that is a separate,
  riskier step: flag it explicitly. Live hooking of a running RTOS can hard-fault
  or hang the watch; **RDP=0 + SWD means you can always re-flash to recover**, but
  expect crashes during bring-up. Design every hook to **default to native** on
  fault.

---

## 2. Resources — what to ask me for

**Ask me to provide any of these not already in your environment.**

Firmware & analysis artifacts (from the hardware-recovery phase; likely on a
different machine — **request them if absent**):

```
una-firmware-dumps/2026-07-29-flash-dump/
├── flash_dump.bin      # full 4 MB, file offset 0 == flash address 0x08000000
├── flash_real.bin      # first 0x20A140 bytes (the real, non-blank image) — USE THIS ONE
├── flash_strings.txt   # `strings -n 6` output on flash_real.bin
└── chunks/             # 32x128 KB dump chunks + manifest (re-verify if needed)
```
Integrity anchor: whole-image CRC32 = **`0xBCD2F8E0`** (device + host agree).
Re-verify before trusting.

Reference docs (request if not present):
- **Arm Cortex-M33 TRM** and the **ARMv8-M Architecture Reference Manual** — for
  the exact **FPB** (Flash Patch & Breakpoint) register layout + comparator count,
  **VTOR** semantics, and the SCB/SCS block (0xE000E000–0xE000EFFF). These are the
  primitives the hooks use.
- **RM0456** (STM32U5 reference manual) — peripheral base addresses + IRQ/NVIC map,
  to tie ISRs to peripherals. A prior session had `rm0456-*.pdf` locally.
- **This repository** (`una-sdk`) — grounds the app-visible interface names
  (Section 3).
- **FreeRTOS source** (public) — to recognize `xQueue*`/`xSemaphore*`/`vTask*`/the
  port context-switch handlers as landmarks for concurrency context.

Tooling: **Ghidra** (preferred here — its decompiler makes vtable-builder code and
constructor object layout far more legible than raw disasm; ask me to install it if
needed) or **radare2**. `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb
--adjust-vma=0x08000000 flash_real.bin` is the zero-setup fallback for spot checks.

---

## 3. Known state — the app-visible / SDK side (grounds the interface names)

From the public `una-sdk` repo (verify line refs still hold before citing):

- **The kernel is FreeRTOS-based** (`Docs/sdk-overview.md`: "FreeRTOS: the
  underlying real-time operating system (kernel-side)"; boot flow shows
  `HAL_Init -> FreeRTOS Start -> main`). So expect FreeRTOS symbols as landmarks:
  `PendSV_Handler`, `SysTick_Handler`, `xTaskCreate`, `xQueueSend/Receive`,
  `xSemaphoreTake`, and the port context switch.
- **App<->kernel ABI is C++**: `IKernel::queryInterface` hands apps vtable-based
  interfaces (`ISystem`, `ILogger`, `IAppMemory`, `IAppComm`, `IFileSystem`). The
  app-facing surface is small; the *internal* HAL is richer.
- **The internal HAL is polymorphic too.** Driver signatures recovered from the
  dump name kernel-internal interface types — e.g.
  `Hardware::BlueNRG2::config(Interface::ISpi*, Interface::IGpo*, Interface::IExti*, Interface::IGpo*)`.
  So `Interface::ISpi / II2c / IGpo / IExti`-style **bus interfaces exist as vtables**
  and are passed into drivers at construction. These are the **coarse (bus-level)
  seams**; the `Hardware::<Chip>` driver objects are the **fine (per-peripheral)
  seams**.
- **Message/GUI display path** (`Libs/Header/SDK/Messages/*`): apps already have a
  clean seam for the display via `RequestDisplayUpdate`, so the *display* may not
  need a kernel hook at all — prioritize seams for peripherals with **no** clean
  app API (raw sensors, power, BLE, GNSS).

---

## 4. Known state — the firmware side (from the hardware-recovery phase)

All CONFIRMED unless noted. Full ledger: `README.md` in this folder.

- **MCU:** STM32U5A5, Cortex-M33. **RDP Level 0**, **TZEN=0**, **MPU disabled**
  (`MPU_CTRL.ENABLE=0`), app thread **privileged** (`CONTROL=0x6`, nPRIV=0). => code
  running in an app (or injected) can read/write any memory, set `VTOR`, program the
  `FPB`, and rewrite live vtable pointers. This is what makes runtime seams viable.
- **Flash:** real image ~2.04 MB (`0x0`–`0x20A140`), rest `0xFF`. **Use
  `flash_real.bin`.**
- **Two vector tables**, parsed from the dump:
  - Bootloader @ `0x08000000`: `SP=0x201F0000`, `Reset=0x08001C45`.
  - **Main kernel @ `0x08060000`: `SP=0x20250000`, `Reset=0x0806CE45`, `VTOR=0x08060000`.**
    The kernel's IRQ vector table starts here — this IS one of your seams (Section 6).
- **Symbol seeding:** `Libs/Source/AppSystem/linker/LibC/libc_exports_0.0.3.ld` in
  this repo has **336 `PROVIDE(name = 0x0803xxxx)`** lines — real libc addresses in
  this exact image. Script them into flags/symbols so libc calls resolve by name;
  what's left in the hot paths near driver strings is UNA driver code.
- **Driver classes present in the dump** (string evidence; these name your
  per-peripheral seams):
  - `Hardware::LS012B7DD06A` (display, SPI memory LCD)
  - `Hardware::BMI270` (IMU, I2C4/0x68, `CHIP_ID=0x24` verified)
  - `Hardware::MAX17262::readReg/writeReg(uint8_t, uint16_t&)` (fuel gauge, I2C1/0x36)
  - `Hardware::DRV2625::readReg/writeReg(uint8_t, uint8_t)` (haptic, I2C1/0x5A)
  - `Hardware::PCA9420::config/isReady/readReg/writeReg` (PMIC — power, high value)
  - `Hardware::N24S64B::readUniqueId/readBytes/writePage` (EEPROM)
  - `Sensor::MS5837::getSubSensor` (baro), `Hardware::PAH8316LS` (PPG/HR)
  - `Ble::PeripheralBlueNRG`, `Ble::CoreBlueNRG`, `Hardware::BlueNRG2::config(...)` (BLE, SPI)
  - `Airoha::AG3335M::start(...)`, `Airoha::Aiding`, `Airoha::ContextPool` (GNSS, LPUART)
- **Cortex-M33 hooking primitives are present:** the SCS block (0xE000E000) is
  standard; `MPU_TYPE=0x00000800` confirms the SCS is reachable and decoded. `VTOR`
  (SCB, `0xE000ED08`) and the `FPB` (`0xE0002000`, `FP_CTRL`/`FP_COMP[]`) are the two
  no-reflash redirection mechanisms — confirm FPB comparator count from the TRM.

---

## 5. Tooling & primitives (the three hook mechanisms you're finding targets for)

1. **Vtable-pointer swap** (fine, per-peripheral). A C++ object's first word is its
   vtable pointer (Itanium ABI). Build a shadow vtable in RAM, repoint the *live
   object's* vtable pointer at it, entries call through to native or into Rust.
   Target to recover: the driver's **vtable address**, its **method order/layout**,
   and **where the live instance lives** (RAM address / how it's reached).
2. **VTOR -> RAM vector table** (IRQ-driven peripherals). Copy the vector table to
   RAM, redirect specific peripheral IRQ handlers to your ISR (chain to native),
   set `VTOR`. Target to recover: the **IRQ table entries** and which peripheral each
   used handler serves.
3. **FPB comparators** (flash function redirect, no reflash; limited count). Target
   to recover: **function entry addresses** worth redirecting, and the FPB
   comparator budget from the TRM.

All three are **reversible** (restore vtable ptr / restore VTOR / clear FP_COMP).

---

## 6. The plan — adversarially verified, per seam type

Two evidence streams cross-check each other: **static** (the dump) and, if/when a
live experiment is approved, **dynamic** (SWD reads of the running device). **No
seam target is CONFIRMED on static evidence alone if a cheap live read can confirm
it.** Each step states its **falsification test** — the observation that would prove
the candidate wrong. If you can't state what would falsify a seam, you haven't
verified it.

### Phase A — Symbolize & map the landscape

Seed libc symbols from the linker file. Identify the FreeRTOS core (`xTaskCreate`,
queue/semaphore APIs, `PendSV_Handler`, `SysTick_Handler`) so you can later classify
any function as **task-context** or **ISR-context**. Locate the `Hardware::`/`Ble::`/
`Airoha::`/`Sensor::` strings and their xrefs.
**Falsification:** if the reset vector / a known libc address doesn't disassemble as a
sane Thumb prologue, your bitness/base is wrong — fix before trusting anything.

### Phase B — Per-peripheral driver vtables (the fine seams)

For each driver class (start with `LS012B7DD06A`, `BMI270`, `PCA9420`):
1. From the class string xref, find the **constructor** (writes a vtable pointer into
   the object) and thus the **vtable address** in flash `.rodata`.
2. Recover the **method table order/layout** (which slot is `init`/`read`/`write`/
   `flush`…) by disassembling each entry and matching behavior (an entry that emits
   an I2C address immediate or touches the SPI register block is the transfer method).
3. Recover the **object layout** (offsets of the bus-interface pointer, config, DMA
   state) so a call-through preserves state.
4. Find **where the live instance is** — a kernel global/singleton, or reachable via a
   known registry — so a hook can locate it at runtime.
**Falsification:** a real vtable's entries all point into `.text` as valid Thumb
function prologues, and the constructor demonstrably stores that vtable address into
an object. If an "entry" isn't a function, or nothing stores the vtable, it's not the
vtable. Confirm method identity by the peripheral it touches (I2C address / register
base), not by guessed slot order.

### Phase C — Bus-level HAL interface vtables (the coarse seams)

Recover the concrete `Interface::ISpi / II2c / IGpo / IExti` implementations (named by
the `BlueNRG2::config(Interface::ISpi*, ...)` signature): their vtables, the transfer
method layout, and where instances are constructed and handed to drivers. Hooking here
intercepts **all** traffic on a bus — powerful for observation and for splicing your
own devices, but affects every peripheral on that bus.
**Falsification:** the candidate ISpi/II2c transfer method must actually drive the
SPI/I2C register block (RM0456 base). If it doesn't touch that peripheral, it's the
wrong vtable.

### Phase D — IRQ vector table (VTOR seam)

Parse the kernel vector table at `0x08060000`. Map each **used** entry to its
peripheral (SPI/I2C/DMA/EXTI/USART/RTC) via the register block the handler touches.
Note which are FreeRTOS-critical (`SysTick`, `PendSV`, `SVC`) — **do not** hook those.
**Falsification:** a handler claimed to serve peripheral X must reference X's register
base (or its NVIC line must match the RM0456 IRQ number). Otherwise the mapping is
wrong.

### Phase E — Concurrency & timing context (the safety-critical RE)

For each candidate seam from B–D, determine: **task or ISR context?** (reached from an
`xTaskCreate`'d entry vs. from the vector table), **what locks it takes** (xSemaphore/
mutex handles), and its rough **timing budget**. This is what stops a hook from racing
or hanging the RTOS. A function calling `...FromISR` APIs is ISR-context; one calling
blocking `xQueueReceive` is task-context.
**Falsification:** classify context by the FreeRTOS APIs actually called, not by
assumption; a wrong context classification is the most likely cause of a hang.

### Phase F — FPB feasibility

From the TRM, confirm the FPB comparator count on this M33 and the `FP_CTRL`/`FP_COMP`
encoding for **instruction-address remap**. Identify a few high-value flash function
entry points (e.g., a driver `init`) worth an FPB redirect where a vtable swap is
awkward.
**Falsification:** an FPB plan is only real if the comparator count and remap encoding
are quoted from the TRM and the target addresses are function entries (aligned Thumb
prologues), not mid-function.

---

## 7. Deliverables

1. **Seam catalog** — one row per peripheral:
   `Peripheral | Seam type (vtable / bus-HAL / IRQ / FPB) | Target address(es) |
   Vtable + object layout | Live-object locus | Concurrency context | Locks | Hook
   mechanism | Call-through/toggle notes | Risk | Confidence (+ method, both sources
   where possible)`.
2. **Hook + toggle recipe** — the concrete shape of a per-peripheral hook: RAM shadow
   vtable / RAM vector table / FPB comparator; an atomic per-peripheral toggle flag
   (native vs Rust); a **call-through** that preserves `this`/AAPCS registers to reach
   the original; and a **safe-default-to-native** fallback (ideally an independent
   watchdog that clears hooks if the Rust side faults). Keep hook install behind a
   debug gate.
3. **Recommended first target** — a low-risk, **observe-only** hook to prove the
   mechanism before replacing anything: intercept one leaf call (e.g. the display
   flush or a sensor register read), log/pass-through, chaining to native. Pick a
   peripheral that is leaf-like and **off** the RTOS-critical path (display or a
   sensor read; do **power/BLE last**).
4. **Updated ledger** in this folder's `README.md` with every seam claim tagged.

---

## 8. Output & bookkeeping

- Write findings as a new doc in this folder (e.g. `SEAM-CATALOG.md`) plus `README.md`
  ledger updates. **Keep the dump and any decompilation out of git.**
- Every claim: **tag CONFIRMED / LIKELY / UNVERIFIED / REFUTED with its method**, and
  state what would falsify each CONFIRMED claim. Record refutations as clearly as
  confirmations (the README's DRV2605->DRV2625, MAX17048->MAX17262 entries are the
  model).
- If you need a resource you don't have (the dump, the TRM, Ghidra, an approved live
  SWD experiment), **stop and ask me** with the exact thing and why.

---

## Appendix — one-paragraph orientation

The watch is an STM32U5A5 running a closed FreeRTOS kernel (verified ~2.04 MB dump,
`flash_real.bin`, CRC32 `0xBCD2F8E0`, kernel vector table at `0x08060000`). Apps run
privileged with the MPU off and TrustZone off, and SWD is open (RDP=0), so injected
code can rewrite live vtable pointers, relocate `VTOR`, and program the `FPB` — and any
crash is recoverable by re-flashing over SWD. The kernel's drivers are C++ vtable
objects (`Hardware::LS012B7DD06A`, `Hardware::BMI270`, `Hardware::PCA9420`,
`Ble::PeripheralBlueNRG`, …) built over a polymorphic HAL (`Interface::ISpi/II2c/…`),
so the dispatch boundaries are ready-made seams. The goal is to catalog those seams —
driver vtables (fine), bus HAL vtables (coarse), the IRQ table (VTOR), and FPB targets
— with each one's address, layout, live locus, and **FreeRTOS concurrency context**,
so a Rust replacement can be hooked in per peripheral, A/B-toggled against native, and
proven inside the running vendor OS before you ever boot your own kernel. Verify every
seam against two independent signals where possible, refuse to trust a guessed vtable
slot without confirming the peripheral it touches, and never hook `SysTick`/`PendSV`.
Locate but never extract the OTA crypto; keep the firmware out of git.
