# Recovering the UNA Watch hardware configuration from a user app

**Date:** 2026-07-29
**Target:** UNA Watch (STM32U5-class MCU), UNA SDK app platform
**Result:** the complete running hardware configuration (MCU identity, flash layout,
clock source, GPIO pin-mux, enabled peripherals, interrupt map) read directly off
live silicon — without SWD, without decrypting the firmware.

This document is self-contained: it explains the technique, gives every command and
source edit needed to reproduce it from scratch, and lists the recovered values.

---

## 1. Summary / why this works

UNA apps are ordinary position-independent binaries (`.uapp`) that the watch kernel
loads into RAM and runs. Empirically, **the kernel does not sandbox an app's memory
access** (no MPU isolation): a running app can read arbitrary addresses — kernel
flash, the ARM System Control Space, and peripheral registers — without faulting.

The SDK gives every app a logger (`ILogger` / `LOG_INFO`). On real hardware that log
output is emitted on the debug UART. So a minimal "peek" app can read any memory it
likes and print the values over the debug channel, where a host captures them. That
turns the app platform into a live memory reader — enough to dump the entire hardware
register configuration (and, in principle, the decrypted kernel image, which lives in
plaintext in flash).

Nothing here needs an SWD probe or the (encrypted) firmware image.

---

## 2. Prerequisites

### Hardware
- UNA Watch.
- **UNA Dev Tool 2.0** (the FTDI-based adapter the watch clips onto). It exposes an
  `FT231X USB-UART` that taps the watch's `DEBUG TX` line → host `/dev/ttyUSB0`.
- **Debug-enable jumper installed inside the watch.** Open the case (4× T3 screws)
  and fit the jumper on the pads marked with a debug symbol + arrow, next to the
  battery. Without it the debug UART stays silent.
- The watch's normal **USB data cable** (for deploying the app as mass storage).
- Linux host.

### Software
- `arm-none-eabi-gcc` (mainline/distro build is fine — tested with GCC 16.1.0).
- The UNA SDK checkout, with `UNA_SDK` pointing at it:
  `export UNA_SDK=/path/to/una-sdk`
- Python 3 with `pyelftools` and `pillow` (used by the SDK's `.uapp` packer).

### One SDK build fix for a non-ST toolchain
`cmake/una-app.cmake` passes `-fcyclomatic-complexity`, which only ST's
"GNU Tools for STM32" accept; mainline `arm-none-eabi-gcc` rejects it. Either remove
that single line from the `add_compile_options(...)` list, or guard it:

```cmake
# replace the bare "-fcyclomatic-complexity" line with a probe:
if(CMAKE_CXX_COMPILER)
    set(_p "${CMAKE_BINARY_DIR}/fc.cpp")
    file(WRITE "${_p}" "int main(){return 0;}\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -fcyclomatic-complexity -c "${_p}" -o "${_p}.o"
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(_rc EQUAL 0)
        add_compile_options(-fcyclomatic-complexity)
    endif()
endif()
```

---

## 3. Key facts about the connection (learned the hard way)

- **Baud rate is 921600**, 8N1, on `/dev/ttyUSB0`. (Not 115200 — that was the single
  biggest time sink; every capture at the wrong baud is silent.)
- **Use the dev tool, not the data cable, to read logs.** The data cable enumerates
  USB, which puts the watch into charge/USB-mass-storage mode where apps do not run.
  The dev tool only supplies power (no USB host), so the watch runs normally and its
  apps execute — and `DEBUG TX` carries their logs.
- There is *also* a minimal interactive console on the watch's own USB CDC interface
  (`/dev/ttyACM0`, DTR-gated, `una>` prompt) but it only offers `time`/`button`/
  `reset`/`cls` — not app logs and not memory access. Ignore it for this task.
- The peek runs **once, at service start**. The service is persistent across GUI
  open/close, so to re-run the probe you must **cold-boot the watch** and relaunch
  the app while capturing.

---

## 4. Build the peek app

Base it on the `HelloWorld` tutorial (a known-good, launchable Activity). Only its
service file is edited; the GUI is untouched.

Edit `Docs/Tutorials/HelloWorld/Software/Libs/Sources/Service.cpp`:

1. Add near the top includes:
   ```cpp
   #include <cstdint>
   ```

2. Add this helper *above* `void Service::run()`:
   ```cpp
   // reads nwords (multiple of 4) starting at `base`, printing each line's own
   // address so a dropped line on the flaky link is still reassemblable.
   static void peekRange(const char* nm, uint32_t base, unsigned nwords)
   {
       const volatile uint32_t* p =
           reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(base));
       for (unsigned i = 0; i < nwords; i += 4) {
           uint32_t a = base + i * 4u;
           LOG_INFO("SWP %-7s %08lX: %08lX %08lX %08lX %08lX\n",
                    nm, (unsigned long)a,
                    (unsigned long)p[i + 0], (unsigned long)p[i + 1],
                    (unsigned long)p[i + 2], (unsigned long)p[i + 3]);
       }
   }
   ```

3. At the very top of `Service::run()` (right after the existing
   `LOG_INFO("thread started\n");`) add the sweep. Order reads safest-first: a bad
   address bus-faults and ends the run, so put confident bases before uncertain ones.

   ```cpp
   LOG_INFO("SWP === start ===\n");
   // --- identity / memory (all safe) ---
   peekRange("SCB",    0xE000ED00, 8);    // [0]=CPUID  [2]=VTOR (kernel vector table)
   peekRange("DBGMCU", 0xE0044000, 4);    // [0]=IDCODE : exact chip + rev
   peekRange("UID",    0x0BFA0700, 4);    // 96-bit unique id
   peekRange("FLSZ",   0x0BFA07A0, 4);    // [0] low16 = flash size (KB)
   peekRange("FLASH",  0x40022000, 4);    // ACR: wait-states
   peekRange("FLASHOPT",0x40022040, 8);   // OPTR [0]: RDP byte + TZEN  -> can we SWD-flash?
   // --- kernel flash (proves plaintext-in-flash) ---
   peekRange("VEC0",   0x08000000, 8);    // boot vector table
   // --- clock tree (RCC base 0x46020C00 on U5) ---
   peekRange("RCC",    0x46020C00, 48);   // CR, CFGR1, PLLxCFGR/DIVR, AHB/APB ENR
   // --- pin-mux: GPIO A..H (AHB2 0x42020000, +0x400/bank) ---
   peekRange("GPIOA",  0x42020000, 12);   // per bank: MODER,OTYPER,OSPEEDR,PUPDR,...,AFRL,AFRH
   peekRange("GPIOB",  0x42020400, 12);
   peekRange("GPIOC",  0x42020800, 12);
   peekRange("GPIOD",  0x42020C00, 12);
   peekRange("GPIOE",  0x42021000, 12);
   peekRange("GPIOF",  0x42021400, 12);
   peekRange("GPIOG",  0x42021800, 12);
   peekRange("GPIOH",  0x42021C00, 12);
   // --- interrupts ---
   peekRange("NVIC",   0xE000E100, 8);    // ISER0-7 : enabled IRQ bitmap
   peekRange("IPR",    0xE000E400, 32);   // interrupt priorities
   // --- peripherals (standard STM32 bases; SRD/LTDC bases below are less certain) ---
   peekRange("I2C1",   0x40005400, 8);    // [4]=TIMINGR : bus speed
   peekRange("I2C2",   0x40005800, 8);
   peekRange("SPI1",   0x40013000, 8);
   peekRange("USART3", 0x40004800, 8);    // [3]=BRR : baud
   peekRange("LPUART1",0x46002400, 12);   // SRD domain — base less certain, keep last
   peekRange("I2C3",   0x46002800, 8);
   peekRange("SPI3",   0x46002000, 8);
   peekRange("LTDC",   0x40016800, 24);   // panel timing — GUESSED base, keep LAST
   LOG_INFO("SWP === done ===\n");
   ```

Build:
```bash
export UNA_SDK=/path/to/una-sdk
cd "$UNA_SDK/Docs/Tutorials/HelloWorld/Software/Apps/HelloWorld-CMake"
cmake -G "Unix Makefiles" -S . -B build      # first time only
cmake --build build -j4
# -> build/HelloWorld_<version>.uapp
```

---

## 5. Deploy (USB mass storage)

```bash
# connect the watch's DATA cable; it enumerates as an exFAT drive "UNA WATCH"
udisksctl mount -b /dev/sda1
MP=$(findmnt -n -o TARGET /dev/sda1)
rm -f "$MP/Apps/HelloWorld/"*.uapp
cp build/HelloWorld_*.uapp "$MP/Apps/HelloWorld/"
sync
udisksctl unmount -b /dev/sda1
# disconnect the data cable
```
On boot the watch regenerates `Apps/app_list.json` from the `.uapp` header, so no
manifest editing is needed.

---

## 6. Capture

```bash
# unplug the data cable, connect the DEV TOOL
stty -F /dev/ttyUSB0 921600 -echo -icrnl
cat /dev/ttyUSB0 > sweep.log &
# cold-boot the watch (full power cycle), wait for the watchface, launch HelloWorld
# the SWP lines appear right after "thread started"; then Ctrl-C the cat
```
The service runs the sweep once at start. If you missed it, cold-boot and relaunch
with the capture already running.

---

## 7. Decode

`SWP` lines are `SWP <name> <addr>: <w0> <w1> <w2> <w3>`. GPIO and NVIC decode with a
fixed format; the following Python turns the GPIO words into a pin-mux table and the
NVIC words into an enabled-IRQ list.

```python
# paste the captured MODER/OTYPER/OSPEEDR/PUPDR/AFRL/AFRH per bank (word offsets
# 0,1,2,3,8,9 of each GPIO block) into `gpio`, then:
MODE=['IN','OUT','AF','AN']; PUPD=['-','PU','PD','?']
def af(b,p): return (b['AFRL']>>(4*p))&0xF if p<8 else (b['AFRH']>>(4*(p-8)))&0xF
for bk,r in gpio.items():
    for p in range(16):
        m=(r['MODER']>>(2*p))&3
        if m==3: continue
        pu=PUPD[(r['PUPDR']>>(2*p))&3]; od='OD' if (r['OTYPER']>>p)&1 else 'PP'
        extra=f"AF{af(r,p)}" if m==2 else ""
        print(f"P{bk}{p:<2} {MODE[m]:3} {extra:4} {od} {pu}")

# NVIC ISER0..n -> IRQ numbers:
iser=[...]  # the four SWP NVIC words
print([b*32+i for b,w in enumerate(iser) for i in range(32) if (w>>i)&1])
```

**Key single-word decodes:**
- `SCB` word[0] = CPUID (`0x410FD214` = Cortex-M33 r0p4); word[2] = **VTOR** (running
  kernel's vector table address).
- `DBGMCU` word[0] = IDCODE: `DEV_ID = [11:0]`, `REV_ID = [31:16]`.
- `FLSZ` word[0] low 16 bits = flash size in KB.
- `FLASHOPT` word[0] = `FLASH_OPTR`: `RDP = [7:0]` (`0xAA`=level 0 open,
  `0xCC`=level 2 locked, else level 1); `TZEN = [31]`. **This decides whether you can
  ever SWD-flash your own firmware.**
- GPIO alternate-function numbers map to peripherals via the STM32U5 datasheet AF table
  (AF0=SYS/SWD, AF4=I2C, AF5=SPI, AF7=USART, AF8=UART/LPUART, AF12=SDMMC1, …).

---

## 8. Recovered configuration (this unit)

### Core / memory
| Item | Value | Meaning |
|---|---|---|
| CPUID `0xE000ED00` | `0x410FD214` | ARM Cortex-M33 r0p4 |
| DBGMCU_IDCODE `0xE0044000` | `0x30036481` | **DEV_ID 0x481 = STM32U59x/U5Ax**, REV 0x3003 |
| Flash size `0x0BFA07A0` | low16 `0x1000` | **4 MB flash** |
| Initial SP (@`0x08000000`) | `0x201F0000` | top of SRAM ⇒ ~2.5 MB SRAM (U5A5-class) |

### Flash layout
- Boot/reserved: `0x08000000`–`0x08060000` (reset vector here: SP `0x201F0000`,
  Reset_Handler `0x08001C44`).
- **Running kernel: `0x08060000`** (from `SCB VTOR = 0x08060000`), up to the 4 MB end.

### Clock
- `RCC_CFGR1` selects **SYSCLK = PLL1R**; `RCC_CR` shows MSIS on and PLL1 on+ready;
  PLL1 fed from MSIS. (Exact MHz requires decoding MSIRANGE + PLL M/N/R against RM0456.)

### Pin-mux (from GPIO AF registers)
| Function | Pins | AF |
|---|---|---|
| **SWD debug** | PA13 (SWDIO), PA14 (SWCLK), PB3 (SWO) | AF0 |
| **I²C ×4** | PB6/7, PB13/14, PC0/1, PD12/13 | AF4, open-drain |
| **SDMMC1 (eMMC)** | PC8–12 + PD2 | AF12 |
| **SPI** | PB5; PG2/PG3 | AF5 |
| **USART** | PC4 | AF7 |
| **UART/LPUART** (candidate GPS) | PA2/PA3 | AF8 |

GPIOA–G are clocked; GPIOH reads `0xFFFFFFFF` (absent/unclocked).

### Interrupts
Enabled external IRQs (from `NVIC ISER`):
`2,11,12,15,16,18,19,20,23,26,27,29,30,55,56,57,58,59,66,71,73,78,82,83,88,89,100,101`.

---

## 9. What this unlocks / open items

- **Firmware flashing** for a from-scratch replacement is via **SWD** on
  PA13/PA14/PB3 (the 1.27 mm header in the dev-tool kit is the ARM Cortex-Debug
  connector). Whether it is possible at all is decided by the `FLASH_OPTR` RDP byte
  (see §7): level 0 = free, level 1 = one-way mass-erase, level 2 = permanently locked.
- **Full kernel image**: flash is plaintext in-place; a chunked `peekRange` walk of
  `0x08060000`→end streams the decrypted 4 MB kernel out the same log channel.
- **Still to capture:** exact clock frequencies (decode PLL fields), LTDC panel
  timing (needs the correct LTDC base), per-bus I²C/SPI/USART config + baud, and I²C
  device addresses (a bus-scan app).

---

## 10. Note to the vendor

The root cause that makes all of this possible is that **user apps run without memory
isolation** — an app can read (and presumably write) kernel flash and peripheral
registers. Encrypting the OTA image does not protect anything once an app can read the
decrypted image straight out of flash at runtime. Enforcing an MPU/privilege boundary
on apps would close this.
