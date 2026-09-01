# Backlight control from an app: what the SDK actually publishes

Status: **ANSWERED AND DEMONSTRATED. Outcome 2: the kernel will not dim the
light, and an app can.** Phases A, B, D and E complete on hardware, the circuit
confirmed against the published schematics, and a six-level brightness ladder
driven from an ordinary `.uapp` and filmed. Phase C is not needed.

> **Correction, same day.** An earlier revision of this file concluded Outcome 3,
> "this hardware cannot dim". That was wrong, and wrong in the direction that
> tells the next person to stop looking. The register evidence proves the kernel
> holds the pin static; it does not prove the pin cannot be modulated, and
> `UNAWatch/una-hardware` shows the circuit behind it is an ordinary
> PWM-dimmable LED switch. The error was reading a firmware configuration as a
> physical constraint.

No firmware dump was used and none is needed. The SDK-side findings were read
out of `una-sdk` and `watch-apps` and can be re-checked by content; the
hardware-side findings come from one run of `BacklightProbe` on the watch, whose
raw output is committed alongside the app in
`watch-apps@feat/backlight-probe`, `BacklightProbe/Output/`; and the circuit
comes from `UNAWatch/una-hardware`, which publishes the schematics for all six
boards under CC BY 4.0.

The handoff prompt that set the objective and the phase plan is not kept: its Q1 to Q12 are
restated with their verdicts in the table below, and the rest of it was SDK facts anyone can
read out of the tree. Q8 is the only one still open and nothing depends on it.

---

## The answer

**The kernel drives the front-light as a plain on/off enable, so `brightness` is
inert. The hardware itself dims perfectly well. The gap is firmware, not
silicon.**

### What the kernel does

The actuator is **PF3**, and the register diff is unambiguous:

| Register | Dark | Lit (every level, 100 down to 1) | Meaning |
| --- | --- | --- | --- |
| `GPIOF ODR` bit 3 | `1` | `0` | The light. Active low. |
| `GPIOF MODER` bits [7:6] | `01` | `01` | General purpose output, as configured |
| `GPIOF OTYPER` bit 3 | `1` | `1` | Open drain |
| `GPIOF AFRL` | `0x00000000` | `0x00000000` | No alternate function selected |
| `RCC` (all 64 words) | identical | identical | No clock enabled or disabled |

`ODR` bit 3 is byte-identical at 100, 75, 50, 25, 10 and 1. The kernel reads
`brightness`, tests it against zero, and throws the value away.

Software PWM is excluded too, and the ladder is what excludes it. If the kernel
were toggling PF3 to make brightness 1 look like 1 percent, a sweep taken at an
arbitrary moment would find the pin low about 1 percent of the time. Six lit
sweeps were taken, and **every one read the pin low**. For the brightness-1 sweep
alone that is a 1-in-100 coincidence.

The pin is held, not modulated.

### What the hardware could do

`UNAWatch/una-hardware`, `UNAview_LS012 Rev1.3 Schematic.pdf`, sheet 2, block
"Backlight Switch and FPC":

```
        3V3
         |
        R1 10K            gate pull-up: off by default
         |
BACKLIGHT_ON --- gate ---[ Q1  NTK3139PT1G ]--- source --- 3V3
                          drain
                            |
                          R2 82R
                            |
                       BACKLIGHT_A ---> J3 (FPC) ---> LED ---> BACKLIGHT_K ---> GND
```

A **P-channel MOSFET high-side switch with an 82 ohm series resistor, straight off
the fixed 3V3 rail.** That is the whole circuit.

Note how completely it corroborates the register measurement: a P-channel gate
turns on when pulled *low*, which is why lit reads `ODR = 0`; the 10K pull-up to
3V3 is why the pin can be open drain and why `PUPDR` needs no internal pull.
Every electrical detail lines up with what the sweep saw.

And it is the textbook dimmable arrangement. There is **no LED driver IC, no boost
converter, no charge pump and no inductor** anywhere in it. Nothing here has a
soft-start to violate, an inrush to manage, or a magnetic component to make
audible noise. A resistor-limited LED behind a FET is what every PWM dimmer in
existence drives. Chopping `BACKLIGHT_ON` at a few hundred Hz dims it, linearly in
duty cycle, with no ill effects.

The gate RC is not a constraint either: 10K against the gate capacitance of a
SOT-923 part is well under a microsecond, which is negligible at any sane PWM
frequency.

### So the gap is firmware

`brightness` describes something the board can do and the firmware does not. That
is a much better position than the one this investigation expected to find, and it
is what makes Phase E worth running rather than moot.

The pin is **PF3, ball D3**, confirmed twice over: `GPIOF ODR` bit 3 is the bit
that moves, and the schematic's pin table shows `BACKLIGHT_ON` on PF3 at D3, with
PF2 taken by `HAPTIC_RST_N` and PF1 by `TOUCH_RST_L`.

### PF3 has no timer output, so hardware PWM is not available on it

From ST's own pin database for this exact part, `STM32U5A5QJI` in UFBGA132
(via Zephyr's autogenerated `stm32u5a5qjixq-pinctrl.dtsi`, which is
`genpinctrl.py` output from the CubeMX tables):

| AF | Function |
| --- | --- |
| AF2 | `LPTIM3_IN1` |
| AF5 | `OCTOSPIM_P2_IO3` |
| AF6 | `MDF1_CCK0` |
| AF7 | `USART6_CTS` |
| AF8 | `UART5_TX` |
| AF12 | `FMC_A3` |
| ANALOG | `ADC` |

There is exactly one timer entry and it is the wrong direction: `LPTIM3_IN1` is
the timer's **input** capture and trigger pin, not an output channel. No
`TIMx_CHy` and no `LPTIMx_OUT` is routed to PF3 on this package.

So the pin genuinely cannot be handed to a timer to modulate. That is the one
place where the hardware does constrain this, and it is worth stating precisely
because it is narrower than "the hardware cannot dim": the LED circuit is fine,
the pin simply has no PWM peripheral behind it.

### Which leaves two firmware routes, and the second is the good one

**Software PWM.** A timer interrupt toggling `ODR` or `BSRR`. Works on any GPIO,
costs an ISR per edge, and jitters with interrupt latency. Perfectly adequate for
a backlight at a few hundred Hz, and the obvious first thing to try.

**Timer-triggered DMA into `GPIOF->BSRR`.** A timer's update and compare events
drive a DMA channel that writes a half-word to `BSRR`, so the timer generates the
timing in hardware and the CPU does nothing per edge. This is a standard STM32
technique for exactly this situation, a pin with no timer output. It gives
jitter-free PWM on any pin at the cost of a DMA channel and two small buffers.

Either is firmware-only. Neither needs a board change, an ABI change, or a new
message.

---

## Verdict table

| # | Question | Phase A verdict | Confidence | Falsified by |
|---|---|---|---|---|
| Q1 | Kernel handles `0x02080000`, from which process kinds | **Yes, and from both.** Every one of the 20 requests came back `SUCCESS`, `completed=Y`, in 0 to 1 ms, from a Service and from the GUI alike. The "GUI only" comment on the block above does not reach this one | **CONFIRMED** (device, 2026-08-27) | `PENDING` or `TIMEOUT` against a non-zero send timeout, or a GUI-sent request differing from a service-sent one |
| Q2 | Where `brightness` dies | **In the kernel handler.** `GPIOF ODR` bit 3 is byte-identical at 100, 75, 50, 25, 10 and 1: the handler tests the field against zero and discards the value. Note this is a choice, not a necessity; the circuit downstream would have honoured it | **CONFIRMED** (device, 2026-08-27) | Any register differing between two non-zero brightnesses. Across 22 blocks and six levels, none does |
| Q3 | `autoOffTimeoutMs = 0` semantics | **The header is right and the simulator is wrong.** 0 disables auto-off on device: the light held for the full 30 s observation window. The mock blanks within about 50 ms of the same request | **CONFIRMED** (device, 2026-08-27) | The light going out inside the window. It did not, at 0 or at `0xFFFFFFFF` |
| Q4 | `brightness = 0` off, and timer interaction | **Both as documented.** `brightness = 0` turns the light off immediately, beating a 60 s timer already armed; and a second request replaces a running timer rather than racing it (a 1 s timer re-armed to 60 s produced no dim at the 1 s mark) | **CONFIRMED** (device, 2026-08-27) | A dim at ~1 s in the cancel test, or the light surviving `brightness = 0` |
| Q5 | State readable back | **No SDK route.** No response field on `RequestBacklightSet`, no backlight event type, no `IBacklight` reachable from an app | CONFIRMED (repo) | An undocumented event type or IID. Phases B/C |
| Q6 | Kernel policy overriding an app | **No clamp up to 60 s**, and none at all on an indefinite hold: `t = 60000` fired at 60 s within about 400 ms, and `t = 0` was still lit at 30 s. The wrist-raise and idle-blanking halves are still untested | **PARTIAL** (device, 2026-08-27) | A maximum on-time clamp shorter than 60 s. There is none |
| Q7 | Unallocated IIDs return a live `IBacklight` | **No. All six return null on device.** `0x00050000` through `0x000A0000` each answered `null`. Closed | **CONFIRMED** (device, 2026-08-27) | Any non-null pointer. There were none |
| Q8 | Undocumented adjacent message types | Open. The type encoding leaves all 16 low bits free on every system type, so subcodes are structurally possible; nothing in the SDK uses them | n/a | Phase C dispatcher table |
| Q9 | What physically drives the light | **PF3, ball D3**, open drain, active low, into the gate of `Q1` (`NTK3139PT1G`, P-channel) on the `UNAview_LS012` board, then `R2` 82R and the LED over FPC `J3`. `R1` 10K holds the gate up when the pin floats. Confirmed twice: `ODR` bit 3 is the bit that moves, and the schematic names `BACKLIGHT_ON` on PF3/D3 | **CONFIRMED** (device + schematic) | The pin not tracking the light, or a driver IC in the path |
| Q10 | Smallest direct-drive workaround | **A software PWM writing `GPIOF BSRR`, and it works.** Six duty cycles delivered within a few points of request; the app wins the pin outright, including while the kernel has been told the backlight should be off. The cost is a full CPU thread per modulated rung, which starves the GUI and reboots the watch unless the rungs are kept short and separated | **CONFIRMED** (device, 2026-08-28) | The kernel reasserting the pin faster than the app can hold it. It never did |
| Q13 | Whether the DMA route removes that cost | **Yes.** TIM7 update drives GPDMA1 ch15 from a RAM buffer into `GPIOF BSRR` at 247 Hz with no CPU per edge. The ladder is monotonic and steady: variation within a rung is at the measurement noise floor, against the full off-to-on distance when the waveform is gated | **CONFIRMED** (device, 2026-08-29) | Flicker at any rung, or a rate that tracked the driving thread's behaviour in every mode |
| Q14 | What gates the hardware waveform | **A long blocking wait, and only that.** Counted buffer passes give 247 Hz spinning, 247 yielding, 247 across a run of one-millisecond sleeps, and **7 Hz** across a single long sleep. The kernel stops the clocks TIM7 and GPDMA1 need when it idles deeply | **CONFIRMED** (device, 2026-08-29) | Any mode other than the long sleep falling short, which would have meant the waveform could not outlive the core going idle at all |
| Q11 | Can the hardware dim at all | **Yes, though not with a timer output on that pin.** The LED circuit is a P-channel FET high-side switch with an 82R resistor off a fixed 3V3 rail: no driver IC, no boost, no inductor, nothing that objects to being chopped. PF3 carries no `TIMx_CHy`, so PWM must come from software or from timer-driven DMA into `BSRR` rather than from a `CCRx` | **CONFIRMED** (schematic + device + ST pin table) | A boost or driver IC in the LED path, or a timer output on PF3. Neither exists |
| Q12 | Kernel dims to its own setting, reachable from an app | **No such setting can exist.** The SDK carries no display or brightness field in `ISettings`, `RequestSystemSettings` or `RequestDisplayConfig`, and Q11 shows there is no duty cycle for one to control | **CONFIRMED** (repo + device) | A watch settings item that changed `ODR` behaviour. There is no duty cycle for one to change |

---

## What Phase A confirmed, corrected and added (kept as the record of the no-hardware pass)

### Confirmed as stated in the handoff

Re-read by content, not by line number:

- `IBacklight` declares `on(uint32_t timeout = 0)`, `off()`, `isOn()`, and nothing else.
- `IKIP::IntfID` lists `IID_SYSTEM 0x00010000`, `IID_LOGGER 0x00020000`,
  `IID_APP_MEMORY 0x00030000`, `IID_APP_COMM 0x00040000`, `IID_FILESYSTEM 0x000B0000`,
  `IID_COUNT`. No backlight, buzzer or vibro ID. The `0x00050000` to `0x000A0000`
  gap is real and six wide.
- `REQUEST_BACKLIGHT_SET = 0x02080000`, in a "Backlight/Audio/Haptic control" block
  that follows a separate "Display control (GUI only)" block.
- `RequestBacklightSet` carries `uint8_t brightness` ("0-100%, 0 = off", default 100)
  and `uint32_t autoOffTimeoutMs` ("Auto-off timeout, 0 = disabled", default 0),
  `static_assert`ed at 40 bytes.
- `GpsLab` and `Squash` each carry a byte-identical `Service::backlightOn(timeoutMs)`
  that sets `brightness = 100`, `autoOffTimeoutMs = timeoutMs` (default 5000) and
  sends fire-and-forget via `MessageGuard::send()` with no timeout. Neither reads
  the result. No other brightness value, no zero timeout, no off, appears anywhere.
- The simulator dispatcher collapses the message to
  `if (brightness > 0) on(autoOffTimeoutMs) else off()`, so the mock cannot tell 1
  from 100.
- `MessageBase` constructs `mNeedsResponse = false`, and `IAppComm::sendMessage`
  with a non-zero `timeoutMs` is documented to return only once the kernel has
  filled the message in place, leaving `getResult()` and `isCompleted()` observable.
  `MessageGuard` already exposes both as `send(timeout)` and `ok()`, so the Phase B
  instrument needs no new SDK code.
- `HomeWidget.hpp` is the in-repo precedent for wrapping a `REQUEST_*` message
  behind a small header-only named class using `SDK::make_msg`.

### Corrected

**The three orphaned interfaces are simulator-only, which is stronger than
"unreachable".** `IBacklight`, `IBuzzer` and `IVibro` are referenced by exactly two
kinds of thing: the simulator (`KernelMessageDispatcher`, the `Mock::` classes) and
generated MSVS project file lists. No device-side SDK code names them. `IKernel`
exposes a single member, `kip`, so `queryInterface` is genuinely the only door and
these three interfaces are not behind it.

**The backlight is not absent from `Docs/`. It is documented wrongly.** The handoff
lists "the backlight appears nowhere in `Docs/`" as defect 3. That is false, and
the truth is worse:

- `Docs/sdk-overview.md` and `Docs/development-workflow.md` both list
  `REQUEST_BACKLIGHT_SET // Set screen brightness`, asserting the inert behaviour.
- `Docs/Simulator.md` documents the mock's on/off logging as the platform's
  behaviour, including "A wrist detection event activates the backlight for
  5 seconds".
- `Docs/architecture-deep-dive.md` asserts `Backlight / Real PWM Control` and
  `LCD Backlight / PWM Controlled / 20-50mA`.

A gap invites a reader to go and measure. Two positive assertions that the field
sets brightness invite them not to. Defect 3 should be restated as a correction,
not an addition.

### Calibration: `architecture-deep-dive.md` is not a hardware source

It names the PMIC as **STPMIC1**. The 2026-07-29 ledger confirmed **PCA9420**, from
the firmware's own driver class strings. So the document contains a
confirmed-wrong part number in the same subsystem area we are investigating, and
both of its PWM claims are Mermaid box labels, which is exactly the class of
artefact that produced the bogus 256 KB RAM figure in the rawtiles work. Treat
every hardware claim in it as UNVERIFIED.

It does carry one claim shaped like recovered code rather than a diagram label,
and that one is worth chasing in Phase C:

```cpp
mpBacklight = new Backlight(gpio);
```

A driver constructed from a GPIO object, where the neighbouring line constructs
the GPS as `GpsAiroha(uart, power)`. If the light were timer-driven you would
expect a timer or channel in that constructor. Weak on its own, but it points the
same way as the interface shape.

### New: the vendor's own precedent is a percentage over discrete levels

This is the strongest circumstantial support for the Q2 verdict, and it is one
struct away in the same header.

`RequestBuzzerPlay::Note::volume` is documented as:

```
uint8_t volume = 100;   // 0-100%, 0 - no sound. (Currently supported only 4 levels (0, 33, 66, 100))
```

and `IBuzzer::Note::level` is `// Sound level 1,2,3, 0 - no sound`. The simulator
dispatcher bridges them with `melody[i].level = buzzMsg->notes[i].volume / 33`.

So the SDK demonstrably publishes 0-100 percentage fields in its message layer over
small discrete level enums in its interface layer, and in the buzzer's case it says
so out loud. `RequestBacklightSet::brightness` is the same veneer with the level
count at two, and without the parenthetical.

That reframes the SDK defect. The problem is not that someone forgot to implement a
field. It is that the message layer's percentage convention was applied to a
subsystem whose interface never had levels, and nobody wrote the parenthetical.

### New: `IID_COUNT` is not a count

```cpp
IID_FILESYSTEM  = 0x000B0000,
IID_COUNT                       // Number of entries
```

`IID_COUNT` has no initialiser, so it is `0x000B0001`, not 6. Nothing in the repo
uses it today, so this is latent rather than live, but any future bounds check of
the form `iid < IID_COUNT` would pass every value in the gap, and anything sizing
an array by it would ask for 720,897 entries. Add it to the defect list.

### New: the simulator mock's zero-timeout bug, with its mechanism

`Mock::Backlight::on(timeout)` calls `mTimer.start(timeout, ...)` unconditionally,
and its callback calls `off()`. `OS::OneShotTimer::start(0, cb)` sets
`fireTime = Clock::now()`, and `run()` polls every 50 ms firing anything whose
`fireTime` has passed. So `on(0)` in the simulator blanks the light on the next
poll, roughly 50 ms later.

Both `IBacklight`'s own comment ("0 - no automatic turn off") and
`RequestBacklightSet`'s ("Auto-off timeout, 0 = disabled") say the opposite. The
mock contradicts the contract it implements, and it does so in the direction that
would make an app author think a hold-indefinitely request had failed.

Note also that `off()` does not cancel a pending timer, while `on()` does. Harmless
today because the stale timer only calls `off()` again, but it is the same class of
mistake and it is what Phase B's Q4 sequence will be compared against.

### New: `MessageResult` gives Phase B a real instrument, and a caveat

`getResult()` returns `PENDING`/`SUCCESS`/`FAIL`/`TIMEOUT`, and `MessageGuard::ok()`
already tests it. The caveat is that the **simulator dispatcher signals `SUCCESS`
unconditionally**, for the backlight case as for every other case, so a green result
in the simulator says nothing at all. On device this is still the separator between
"a handler ran" and "the message died in a queue", which is exactly what Q1 needs.

---

## What Phase A changed about the plan

### Phase D is cheaper than the handoff assumed. Run the existing sweep first.

`FwDump`'s `DeviceContext::kSweep` already reads:

- `RCC` at `0x46020C00`, 64 words, i.e. offsets `0x00` to `0xFC`. On STM32U5 that
  span is expected to include the AHB and APB peripheral clock-enable registers,
  which is where "is any timer clocked" is answered. **UNVERIFIED against RM0456**,
  which is not on this machine. Check it before relying on it.
- `GPIOA` through `GPIOH`, 12 words each, i.e. `MODER`, `OTYPER`, `OSPEEDR`,
  `PUPDR`, `IDR`, `ODR`, `BSRR`, `LCKR`, `AFRL`, `AFRH` and two more.

`ODR` plus `MODER` plus `AFRL/AFRH` across all eight ports is already enough to
answer the coarse form of Q9 and Q11: which pin changes when the light comes on,
and whether that pin is configured as a plain output or as an alternate function.
Adding the `TIM` bases is needed only for the finer step, reading `CCRx`/`ARR` to
measure a duty cycle once a timer is identified.

So the sequencing should be: get a dark-versus-lit diff out of the sweep that
already exists, read the GPIO mode of whatever pin moved, and only then decide
which timer blocks are worth adding. That inverts the handoff's "adding the timer
blocks is probably Phase D's entire implementation".

### Phases B and D must be one app, for a reason the handoff did not give

`FwDump` writes its context file, sweep included, once at app start. There is no
way to sweep twice with the light in two states unless the app itself changes the
light between sweeps. So the probe is necessarily `FwDump`'s sweep plus the
`RequestBacklightSet` matrix plus a button-triggered re-sweep, in one binary. It
was already the efficient choice; it is actually the only workable one.

### Phase C is blocked on artefacts that are not on this machine

Searched for and absent: `flash_real.bin`, `flash_dump.bin`, `flash_strings.txt`,
and any local copy of RM0456 or DS13543. They will need to be produced from
wherever they live, or a fresh dump taken with `FwDump`, before Phase C or the
RM0456 checks above can run.

---

---

## Phase B on hardware, 2026-08-27

One run of `BacklightProbe` on the watch, Suite 2 read off video against the
on-screen counter. The probe app and the full timing table live in
`watch-apps@feat/backlight-probe`, `BacklightProbe/README.md`.

**Every finite timeout fired within a few hundred milliseconds of what was
requested.** 100 ms, 1 s, 5 s and 60 s all landed. There is no scaling bug in the
timeout path and no kernel clamp below a minute.

**The one substantive finding is `t = 0`, and it settles a live contradiction.**
`IBacklight`'s header and `RequestBacklightSet`'s comment both say 0 disables
auto-off. The SDK simulator's mock starts a zero-length timer and blanks within
about 50 ms, the exact opposite. On hardware the light held for the entire 30
second window. The header is right; the mock is wrong; and anyone validating this
path against the simulator alone would conclude the reverse of what the device
does. That promotes SDK defect 2 from "contradicts its own header" to "contradicts
the device", and it decides which side of the contradiction gets fixed: **fix the
mock, not the header.**

Q1, Q4 and half of Q6 came back as documented, which is worth saying plainly
because it is the first time any of them has been more than folklore.

### Two measurement notes worth reusing

**Counting the DMA's own block repeats** measures the waveform's output rate
directly, on the watch, with no camera and nothing assumed about the clock tree.
Three builds were shipped with the rate computed from an assumed clock and all
three were wrong, in two different directions; the first instrument that measured
the output instead settled it in one run. Where a peripheral counts its own work,
read that counter.

**Reading bands down the sensor's readout direction** separates a flickering light
from a dim one on ordinary 30 fps phone footage. A light toggling faster than the
frame rate paints stripes within a single frame; one that toggles slower makes
whole frames uniformly bright or dark. Static banding that repeats identically
frame to frame is the scene, not the light.

### A measurement note worth reusing

The operator read the blank off the screen's **colour** rather than its
brightness: the front-lit panel reads distinctly bluer than the same panel lit by
ambient once the light is off. That is a cleaner single-frame signal than raw
luminance, and it is the kind of thing that only turns up by doing it. Anyone
repeating this should start there.

### What this run did not answer

Suite 1's register sweeps are the pivotal Q9/Q11 pair and **their results have not
been reported**. Neither has the `SET` result-code column from
`backlight_probe.txt`, which is the direct evidence for whether the kernel signals
completion on this message at all.

Until the dark-versus-lit diff exists, the investigation's central question is
exactly where Phase A left it: `brightness` is inert, and whether that is a kernel
that discards it or hardware that could never have honoured it is unsettled. The
Suite 2 findings above are real and worth having, but not one of them bears on it.


---

## Phase D on hardware, 2026-08-27

One run, eight labelled sweeps of 22 register blocks each, committed at
`watch-apps@feat/backlight-probe`, `BacklightProbe/Output/`.

### The diffs, in the order the brief specified

**`sweep_dark.txt` vs `sweep_dark_after.txt`: identical apart from the header.**
Run this one first. It is the control, and it is what makes the other two mean
anything: nothing drifted across the four minutes between them.

**`sweep_dark.txt` vs `sweep_lit_b100.txt`: three blocks differ, one of them the
answer.**

```
SWP GPIOF     42021410: 0000200D 0000200C ...     dark
SWP GPIOF     42021410: 00002005 00002004 ...     lit
                        ^IDR     ^ODR
```

`ODR` goes `0x200C` to `0x2004`: bit 3 clears. `IDR` follows it, which is the pin
reading back what it is being driven to. The light is **PF3, active low**.

The static half of the same block is what settles Q11:

```
SWP GPIOF     42021400: C4FFFF50 00000008 00000000 02000001
                        ^MODER   ^OTYPER
SWP GPIOF     42021420: 00000000 00000000 ...
                        ^AFRL
```

`MODER = 0xC4FFFF50`, so bits [7:6], which are pin 3, are `01`: general purpose
output. Not `10`, which is what an alternate function would read. `AFRL` is zero
across the whole port. `OTYPER` bit 3 is set, so the pin is open drain, which is
what an active-low enable through a low-side switch looks like.

**`sweep_lit_b100.txt` vs `sweep_lit_b001.txt`: `GPIOF` is byte-identical.**

The only difference anywhere is one bit of `GPIOD IDR`, an input the kernel does
not drive and which does not track brightness. A hundred percent and one percent
produce the same pin state, the same port configuration, and the same clock tree.

### The three things this rules out, and how

These rule out every mechanism by which the *running firmware* could be varying
the light. They say nothing about what the pin is capable of, which is a separate
question the schematic answers.

| Mechanism | Ruled out by |
| --- | --- |
| Hardware PWM, as configured | `MODER[7:6] = 01` and `AFRL = 0`. The pin is presently a plain output, whatever alternate functions it may have |
| Software PWM (bit-banged `ODR`) | Six lit sweeps, including brightness 1, all read the pin low. At a 1 percent duty that single sample is a 1-in-100 coincidence |
| An off-chip dimmer over a bus | All six `I2C` blocks and both `SPI` blocks are identical in all eight sweeps |
| A clock being enabled for the light | `RCC`, all 64 words, identical in all eight sweeps |

### The timer blocks were never swept, and still do not need to be

The run used the confirmed 22-block set; `sweep_timers.enable` was not present, so
the unconfirmed timer bases were not read.

That remains the right call, but for a narrower reason than the earlier revision
of this file claimed. Reading every timer would establish which ones are running.
It would not establish whether any of them *can be routed to PF3*, because that is
a pin-multiplexing fact from ST's pin table rather than a register value. So the
timer sweep answers a question nobody needs answered, and the question that
matters was answered by the pin table: PF3 has no timer output at all.

Nobody needs to enable those bases, and nobody should enable them expecting this
to settle Q10.


---

## Phase E on hardware, 2026-08-28

An ordinary `.uapp`, `watch-apps@feat/backlight-pwm`, writing one register:
`GPIOF BSRR` bit 3, plus two debug registers to start the cycle counter. No
`MODER`, no `AFRL`, no `RCC`, nothing near the option bytes. PF3 is already an
open-drain output, so there is no configuration to change and none to restore.

### The ladder, filmed and measured

Luminance is the mean of a blank patch of the screen, read frame by frame off a
30 fps video. Achieved duty is the app's own arithmetic, on-time against the
rung's wall clock.

| Rung | Requested | Achieved | Luminance | Share of range |
| --- | --- | --- | --- | --- |
| off | 0 | 0 | 42.2 % | 0 % |
| d100 | 100 | 100 | 75.8 % | 100 % |
| d75 | 75 | 79 | 73.6 % | 94 % |
| d50 | 50 | 52 | 69.2 % | 81 % |
| d25 | 25 | 26 | 59.2 % | 51 % |
| d10 | 10 | 10 | 50.6 % | 25 % |
| d1 | 1 | 1 | 43.2 % | 3 % |
| d100_again | 100 | 100 | 75.7 % | 100 % |

Six separated levels over a 33.6 point range, monotonic, and `d100_again` lands
within 0.1 points of `d100`, so nothing drifted across the run. Even one percent
duty is distinguishable from off.

**Set beside `BacklightProbe`'s Suite 1, this is the whole finding.** The same six
numbers sent to the kernel produced one brightness and byte-identical registers.
Sent to the pin, they produce six.

### The contest: the app wins the pin

Two rungs at the end provoke the kernel deliberately.

`contest_autooff` asks the kernel for full brightness with a two second auto-off
and keeps modulating at 50 percent for six. `contest_off` tells the kernel the
backlight should be **off** while the app carries on driving it.

Both measured **81 percent of range, identical to `d50`**. The app held its
commanded duty throughout, including against the kernel's stated intent that the
light be off.

The app's own detector reported `kernel_writes = 0` across every rung and every
sample, but that is the weaker evidence and the log says so: the sample is taken
immediately after the app's own write, so a kernel write landing between two of
ours is overwritten within a millisecond and never seen. Zero means the app's
writes dominate. **The light staying lit is the answer that counts.**

### What it costs, which is the real limit

A modulated rung spins a full CPU thread, and that is not a choice:

- `ISystem::delay` is roughly millisecond-granular, a quarter of the 4 ms period,
  and measurably inconsistent between one and two milliseconds. It cannot place
  edges.
- `DWT_CYCCNT` stops while the core sleeps, so a spin after a sleep cannot tell
  how much of the period is left.

Spinning starves the GUI thread. Thirty consecutive seconds of it froze the
screen and rebooted the watch; `ISystem::yield()` did not help, because it gives
up the rest of a slice and is scheduled straight back. What works is keeping each
modulated rung short and putting a dark idle gap after every one, where the
service holds the pin and blocks on the message queue.

So an app **can** dim this light, and cannot do it politely. That is the honest
shape of the workaround, and it is the argument for the timer-driven DMA
alternative rather than against dimming.

> **Superseded by Phase F, 2026-08-29.** The DMA alternative was built and it
> works. Everything in this section is true of the software PWM and false of the
> technique: the hardware engine costs no CPU per edge and produces a steady
> light. Kept as written because the reasoning about `delay` granularity and
> `DWT_CYCCNT` is still why a software PWM has to spin.

### Incidental: the core clock

Measured at runtime because the app needs it to place edges: **about 160 MHz**,
with individual runs reading 151, 160 and 162. The spread is the calibration's
own error, roughly four percent from a 25 ms window against a 1 ms tick, so the
right claim is "about 160 MHz" and not any one of those figures. The part's
maximum is 160 MHz, which the readings straddle.

That is worth recording beyond this app: the 2026-07-29 investigation captured
`RCC` three times and left the clock tree undecoded because the bit diagrams in
the extracted PDF were unreadable. This is an empirical answer to it, to a few
percent.

### Three failures worth keeping

Every one of them produced a plausible-looking run, which is why they are here.

| Attempt | Symptom | Cause |
| --- | --- | --- |
| No yielding at all | Watch rebooted after 44 s | Service never handed back the CPU; `getMessage(msg, 0)` is non-blocking |
| Calibrating across `delay()` | Ladder collapsed to three levels; `1 MHz` on screen | `DWT_CYCCNT` stops when the core sleeps, so the measurement counted only waking cycles |
| Sleeping between bursts | Light flashed at 100 Hz; duty scaled to 0.72 | An 8 ms burst then 2 ms dark is a full-depth envelope on the carrier |
| Publishing status at 5 Hz | Light flashed at 5 Hz | Each publish wakes the GUI, which preempts the service mid-pulse and holds the pin on |

The last one is worth dwelling on: it is exactly the confound this investigation
warned about in `BacklightProbe`, that a screen repainting during a measurement is
part of the measurement, and it was then built into the app that measures.


## Phase F on hardware, 2026-08-29: the DMA engine, and the cost that went away

Phase E's conclusion was that an app can dim this light and cannot do it politely,
because a software PWM on this part has to spin. **That is now false.** The
timer-triggered DMA route this file recommended in the abstract has been built and
run, and it removes the cost entirely: the waveform is generated in hardware, the
CPU does nothing per edge, and the light is steady.

### The engine

TIM7's update event drives GPDMA1 channel 15, one word per event, from a 128-word
buffer in RAM into `GPIOF BSRR`. Each word is either bit-reset 3 or bit-set 3, so
the buffer *is* the waveform and the duty is how many of its words are which. The
timer never reaches a pin, which is why this works on a pin with no timer output:
`BSRR` does not care who wrote it.

Two details worth carrying to anyone reimplementing it. Block-repeat mode loops
without a linked-list node in RAM, which is fewer registers to get wrong blind.
And the channel is configured with `EN` clear, then every register is read back
and compared before it is enabled, `CDAR` against `GPIOF BSRR` above all: a wrong
destination would have the DMA writing into memory rather than into a peripheral,
and `GPIOF` also carries the touch and haptic reset lines.

Measured rate 247 Hz at `ARR` 5038, from which the timer's input clock is 160 MHz.

### The only thing that stops it, which is a long blocking wait

The waveform's rate was measured four ways, by counting completed passes of the
buffer over four tenths of a second of wall clock, read from the block-repeat
counter the DMA maintains in hardware. This is an instrument worth reusing: it
measures the output rather than the clock, needs no camera, and cannot be fooled
by anything upstream of it.

| How the thread passes the time | Waveform rate |
| --- | --- |
| Spinning, never gives the core up | 247 Hz |
| `ISystem::yield()` in a loop | 247 Hz |
| A run of one-millisecond `delay()` calls | 247 Hz |
| One `delay()` for the whole window | **7 Hz** |

So the waveform is genuinely autonomous, and the single thing that kills it is
blocking for a long time. The kernel evidently takes a long wait as licence to
stop the clocks that TIM7 and GPDMA1 depend on, and takes a short one as something
to idle through. An app that wants a dimmed backlight must therefore avoid long
blocking waits for as long as the light is dimmed, which is a real constraint but
a mild one next to spinning a thread.

The failure mode when it *is* gated deserves recording, because it is worse than
being slow and does not look like what it is. When the waveform stops, the pin
holds whatever the last word wrote, so the light freezes full on or full off for
as long as the core stays down, in stretches of about a tenth of a second. It
advances properly for the fraction of the time the core is up. Nothing about that
reads as a dimmer light; it reads as a hard strobe. Three consecutive builds were
misdiagnosed as divider errors on the strength of it.

### The ladder, flicker-free

Filmed and measured the same way as Phase E, mean green level over the watch face,
against the light-off baseline in the gaps between rungs.

| Requested duty | Level above off | Variation within the rung |
| --- | --- | --- |
| 100 | 36.0 | 1.4 |
| 75 | 29.7 | 1.6 |
| 50 | 20.2 | 1.5 |
| 25 | 10.3 | 1.1 |
| 10 | 4.3 | 0.6 |
| 1 | 0.9 | 0.2 |

Monotonic, and the variation column is the finding. On every gated run the same
column read about 30, which is the full distance between off and fully on: the
light was not dim, it was blinking. Here it is at the noise floor of the
measurement. The camera values are not linear in duty and should not be read as
though they were, because of the sensor's gamma; what they establish is order and
steadiness, not a transfer function.

A separate check on the same footage: reading 24 bands down the sensor's readout
direction, a light toggling faster than the frame rate paints stripes *within* one
frame. The banding is there, but it is identical frame to frame, so it is the
front-light's own spatial falloff across the face and not temporal flicker. That
same read is what diagnosed the gated runs, where whole frames were uniformly on
or uniformly off in blocks of three or four.

### The contest, again, and the app still wins

Both contest rungs held at 19.0 above baseline, against 20.2 for the same duty
with no interference: the app keeps the pin at its dimmed level while the kernel
has been told to run a two-second auto-off, and again while it has been told
outright to turn the backlight off. No disagreement was ever sampled between what
the app wrote and what the pin read back.

### What this changes

Phase E's cost section stands as a description of the software engine and is
wrong as a description of the technique. The honest summary is now:

- An app **can** dim this light, in hardware, at a few hundred Hz, steadily.
- The CPU cost per edge is zero. The cost is one timer, one DMA channel, 512
  bytes of RAM, and a constraint on how the driving thread waits.
- Every earlier claim in this file about spinning applies to the software PWM
  only.

### Open: whether a dimmed backlight actually saves power

Not measured, and it does not follow from anything above.

The saving is real but small. The front-light draws roughly 4 to 7 mA, so half
duty saves two or three of them while the light is lit. Against that, the
constraint this phase established has a cost of its own: a long blocking wait is
what stops the waveform, so the driving thread cannot let the core idle deeply for
as long as the light is dimmed, and that can easily be worth more than a few
milliamps.

The two probably do not overlap much. The backlight is on for a few seconds at a
time, and during those seconds the watch is being looked at and is unlikely to be
idling deeply in any case, which would make the marginal cost near zero and
dimming a straight win. That is an argument, not a measurement, and the opposite
case is equally coherent: if the kernel does drop into a deep idle between screen
refreshes with the light on, dimming is a straight loss.

What would settle it is a soak against `REQUEST_BATTERY_STATUS`, holding one duty
for ten minutes and another for ten more, run twice with the order swapped so a
drifting discharge curve cannot be read as a difference between the two.

Nothing else in this phase depends on the answer. The waveform, the ladder and the
contest are all established independently of it.

## SDK defects established so far

All four are independent of what the hardware turns out to do, and each is its own
branch and its own PR.

1. **A published field that does nothing.** `RequestBacklightSet::brightness` is
   documented "0-100%" and is inert on device. It sits in a `static_assert`-fixed
   40 byte slot the kernel parses, so it cannot be removed. The fix is the
   parenthetical the buzzer already has, plus any app-facing wrapper not offering
   it. Wait for Phase B's measured matrix before writing the exact wording.
2. **A simulator mock that contradicts the device.** `Mock::Backlight::on(0)`
   blanks after about 50 ms; `IBacklight` and `RequestBacklightSet` both say 0
   disables auto-off, and the 2026-08-27 run confirms the device holds the light
   indefinitely. Q3 is settled, so the direction is settled with it: **fix the
   mock**, by not starting a timer at all when the timeout is 0. This one is ready
   to go now.
3. **`Docs/` asserts the inert behaviour works.** `sdk-overview.md` and
   `development-workflow.md` both describe `REQUEST_BACKLIGHT_SET` as setting
   screen brightness; `Simulator.md` presents mock behaviour as platform
   behaviour; `architecture-deep-dive.md` asserts PWM brightness control and names
   the wrong PMIC. Needs a correction plus a real capability section, both backed
   by the Phase B capability map.
4. **`IID_COUNT` is not a count.** It evaluates to `0x000B0001`. Latent, unused,
   one-line fix, and independent of everything else here.

---

---

## Outcome 2, and what follows from it

The brief set out three outcomes. This is the second: **the kernel will not do it,
but the hardware will.**

That is a materially better position than the one the brief expected, and it is
worth being explicit about what changed. The register evidence alone looks exactly
like Outcome 3, and an earlier revision of this file called it that. The
difference between "the pin is not being modulated" and "the pin cannot be
modulated" is not visible in any register, and reading the first as the second is
the mistake to avoid here.

### What the vendor would need to change, and it is cheaper than it looked

No board change. No ABI change. The message already carries a 0-100 field, the
kernel already parses it, and the circuit already tolerates a duty cycle. What is
missing is a handler that maps the field onto one.

PF3 carries no timer output, so it is not a `CCRx` write. It is either a software
PWM in the kernel's own timer tick, or a timer-triggered DMA into `GPIOF->BSRR`,
which is hardware-timed and costs no CPU per edge. Either lives entirely inside
firmware the vendor already ships.

Phase F built the second one from an app and it works, which makes this a stronger
ask than it was: the technique is demonstrated on this exact pin and this exact
part, and the kernel is better placed to use it than an app is. It already owns
the timers and the DMA channels, so it need not go looking for a free one, and it
controls its own idle path, so the long-blocking-wait constraint that binds an app
does not bind it.

So the ask is one thing, not two:

1. **Honour the field**, at whatever granularity is convenient. The buzzer's
   precedent is instructive: it publishes 0-100 and implements four levels, and
   says so in the comment. Four backlight levels would be a complete answer to
   this investigation.

And in the meantime, one thing that costs nothing:

2. **Say what is true today.** `brightness` is on/off, and two of the SDK's own
   documents currently assert otherwise.

### Phase E, run and answered

It was run on 2026-08-28 and it worked. See the Phase E section above for the
ladder, the contest and the cost. In summary: an app can dim this light, it wins
the pin outright against the kernel, and it cannot do it politely because a
software PWM on this part has to spin.

Phase F, on 2026-08-29, removed that last clause. The timer-triggered DMA engine
this file had recommended in the abstract was built and run, and it dims the light
in hardware with no CPU per edge and no visible flicker. The one constraint left
is that the driving thread must not block for long stretches, because the kernel
stops the clocks TIM7 and GPDMA1 need when it does.

The electrical guardrail the brief worried about turned out to be moot: 82 ohms
off a fixed 3V3 rail through a P-channel FET means a stuck-on drive is
electrically identical to the on state the kernel already uses. The risk that
mattered was the one nobody had listed, which was starving the GUI thread.


## Ledger

| Claim | Confidence | Source |
|---|---|---|
| `IBacklight` exposes no brightness parameter | CONFIRMED | `Libs/Header/SDK/Interfaces/IBacklight.hpp` |
| No `IID_BACKLIGHT`/`IID_BUZZER`/`IID_VIBRO` exists in the repo | CONFIRMED | grep for `IID_` across the tree; `IKIP.hpp` |
| The three interfaces are referenced only by simulator code and MSVS file lists | CONFIRMED | grep for the type names across `una-sdk` |
| `IKernel` exposes only `kip`, so `queryInterface` is the only door | CONFIRMED | `Libs/Header/SDK/Interfaces/IKernel.hpp` |
| `brightness` is inert on device | CONFIRMED, field observation | Repo owner, carried forward from the handoff. Not re-measured here |
| `brightness` dies in the kernel handler | CONFIRMED (device) | `GPIOF ODR` bit 3 identical at six brightness levels. The Phase A structural argument (no parameter on `IBacklight`) predicted this and was right, but the register diff is what settles it |
| The SDK publishes percentage fields over discrete interface levels | CONFIRMED | `RequestBuzzerPlay::Note::volume` comment, `IBuzzer::Note::level`, `volume / 33` in the simulator dispatcher |
| `Mock::Backlight::on(0)` blanks after about 50 ms | CONFIRMED | `Mock/Backlight.cpp` plus `OneShotTimer::start`/`run` |
| The simulator signals `SUCCESS` for the backlight case unconditionally | CONFIRMED | `Libs/Source/Simulator/App/KernelMessageDispatcher.cpp` |
| No display or brightness field in `ISettings`, `RequestSystemSettings` or `RequestDisplayConfig` | CONFIRMED | The three declarations |
| `IID_COUNT` evaluates to `0x000B0001` | CONFIRMED | `IKIP.hpp`, C++ enum rules |
| `architecture-deep-dive.md` names the PMIC STPMIC1; the ledger confirmed PCA9420 | CONFIRMED | The doc, and the 2026-07-29 hardware inventory |
| A timer-driven DMA into `BSRR` dims the light from an app with no CPU per edge | **CONFIRMED** (device, 2026-08-29) | TIM7 + GPDMA1 ch15 at 247 Hz; monotonic ladder at 100/75/50/25/10/1 with within-rung variation at the noise floor |
| The APB1 timer clock is 160 MHz | **CONFIRMED** (device, 2026-08-29) | `ARR` 5000 over a 128-word buffer measured 250 Hz, which fixes the input clock. Independent of the `DWT_CYCCNT` core-clock figure and agreeing with it |
| A long blocking wait stops the waveform; yielding and short sleeps do not | **CONFIRMED** (device, 2026-08-29) | 247/247/247/7 Hz across spin, yield, one-millisecond sleeps and one long sleep |
| A gated waveform reads as a hard strobe, not as a dim light | **CONFIRMED** (device + video, 2026-08-29) | When the DMA stops the pin holds the last word, so the light freezes fully on or fully off in stretches of about a tenth of a second |
| The backlight is a discrete front-light with no LED driver IC | **CONFIRMED, primary source** | `UNAview_LS012 Rev1.3 Schematic.pdf`: the entire circuit is `R1` 10K, `Q1` NTK3139PT1G, `R2` 82R and an LED over FPC `J3`. The 2026-07-29 ledger's inference by elimination was right |
| The real `Backlight` driver is constructed from a GPIO | CONFIRMED (device) | `mpBacklight = new Backlight(gpio)` in `architecture-deep-dive.md` turned out to be right, and PF3 is the GPIO. The same document's neighbouring "Real PWM Control" label is wrong as a description of the firmware, though the board would support it |
| `RCC` words 0-63 span the peripheral clock-enable registers | UNVERIFIED, and no longer load-bearing | Recalled STM32U5 layout; RM0456 is still not on this machine. Q11 rests on `MODER` and `AFRL` at the pin, not on the RCC decode |
| The circuit behind PF3 tolerates PWM | **CONFIRMED, primary source** | `UNAview_LS012 Rev1.3 Schematic.pdf`, sheet 2: P-channel high-side FET, 82R series resistor, fixed 3V3 rail. No driver IC, no boost, no charge pump, no inductor, so nothing in the path objects to being chopped |
| PF3 is open drain because the gate has an external pull-up | CONFIRMED (schematic + device) | `R1` 10K to 3V3 on the gate, and `GPIOF PUPDR` bits [7:6] read `00`. The board supplies the pull the pin does not |
| PF3 has a timer output alternate function | **CONFIRMED NEGATIVE** | ST's pin database for `STM32U5A5QJI` UFBGA132, via Zephyr's autogenerated `stm32u5a5qjixq-pinctrl.dtsi`. PF3 offers AF2 `LPTIM3_IN1` (an input), AF5, AF6, AF7 `USART6_CTS`, AF8 `UART5_TX`, AF12 and ANALOG. No `TIMx_CHy`, no `LPTIMx_OUT` |
| An app can dim the light through PF3 | **CONFIRMED, demonstrated** | 2026-08-28 run: six duty cycles delivered within a few points of request, filmed and measured frame by frame at 42.2 % off to 75.8 % full. Raw record in `pwm-run/backlight_pwm.txt` |
| The app wins the pin against the kernel | **CONFIRMED** | Both contest rungs measured identical to `d50` while the kernel had been asked for a 2 s auto-off and then told the backlight should be off |
| The core clock is about 160 MHz | CONFIRMED to a few percent | Runtime calibration over a 25 ms window; runs read 151, 160 and 162, and the part's maximum is 160. Answers an item the 2026-07-29 ledger left open |
| A software PWM can be made polite on this part | **REFUTED** | `delay()` is a quarter of a period coarse and inconsistent; `DWT_CYCCNT` stops while the core sleeps. Spinning is the only accurate option, and it starves the GUI |
| PF3 is ball D3 and carries `BACKLIGHT_ON` | **CONFIRMED, two independent methods** | `GPIOF ODR` bit 3 tracks the light across eight sweeps; and the rendered `UNAcore Rev3.2` pin table shows `BACKLIGHT_ON` on PF3/D3, with PF2 on `HAPTIC_RST_N` |
| A `pdftotext -layout` reading of a dense schematic pin table | **REFUTED as a method** | Its row grouping put `BACKLIGHT_ON` one row high, on PF2, which would have implied a timer output was available (PF2 has `LPTIM3_CH2`). Rendering the page and reading the wire alignment settled it. Text extraction from schematic PDFs needs corroboration, in the same way a `strings` hit does |

### Negatives worth recording

- No `IID` for backlight, buzzer or vibro exists anywhere in the SDK, including in
  comments, docs and generated project files.
- No message type other than `REQUEST_BACKLIGHT_SET` mentions the backlight.
- No brightness or display field exists in any settings or display message.
- No app-facing wrapper class for the backlight exists, unlike `HomeWidget`.
- The flash dump and the two reference manuals are not on this machine.

### What was run

```
grep -rn "IID_" --include="*.hpp" --include="*.cpp" --include="*.h" --include="*.c" --include="*.md" .
grep -rn "IBacklight\|IBuzzer\|IVibro" .
grep -rni "backlight\|brightness" Docs --include="*.md"
grep -n "constexpr Type" Libs/Header/SDK/Messages/MessageTypes.hpp
```

plus the two schematics that settled the circuit,

```
UNAWatch/una-hardware  una-watch/electronics/UNAview_LS012/UNAview_LS012 Rev1.3 Schematic.pdf
UNAWatch/una-hardware  una-watch/electronics/UNAcore/UNAcore Rev3.2 Schematic.pdf
```

and direct reads of `IBacklight.hpp`, `IBuzzer.hpp`, `IVibro.hpp`, `IKIP.hpp`,
`IKernel.hpp`, `ISettings.hpp`, `IAppComm.hpp`, `MessageBase.hpp`,
`MessageGuard.hpp`, `CommandMessages.hpp`, `KernelMessageDispatcher.cpp`,
`Mock/Backlight.{hpp,cpp}`, `OS/OneShotTimer.cpp`, `FwDump`'s
`DeviceContext.{hpp,cpp}`, and the two `Service::backlightOn` implementations in
`watch-apps`.

### What was not run

- No disassembly, and no `strings` pass: the dump is not on this machine, and
  after Phase D it is not needed to answer the question.
- **No DS13543 PDF was read**, because st.com is unreachable from this machine.
  The pin table was taken instead from ST's own CubeMX pin database as
  republished in Zephyr's `hal_stm32`, file `dts/st/u5/stm32u5a5qjixq-pinctrl.dtsi`,
  which is `genpinctrl.py` output for this exact orderable part. Worth confirming
  against the datasheet if PF3's AF list ever becomes load-bearing for something
  more than "no timer output".
- No RM0456 check. The `RCC` coverage claim in the ledger depends on it, but
  nothing load-bearing does any more.
- The `0x00050000` to `0x000A0000` IIDs were not probed. Note for whoever does:
  there are at least eight kernel-service-shaped interfaces in the SDK without an
  ID (`IBacklight`, `IBuzzer`, `IVibro`, `ISettings`, `ITime`, `IMutex`,
  `ISemaphore`, `ISensorData`) competing for six slots, so there is no reason to
  expect the backlight to be among them. Probe all six and log the pointers; do
  not call through any of them before Phase C confirms the vtable.

---

## Next

The question is answered and demonstrated. Everything left is tidying, and none
of it is blocked.

1. **SDK defect 2, the simulator mock.** Settled since 2026-08-27: the device
   holds the light indefinitely at `timeout = 0`, so the mock should not start a
   timer at all in that case. Its own branch, ready.
2. **SDK defect 1, the field's documentation.** Must say "not implemented", never
   "not possible": an app dims this light with one register.
3. **SDK defect 3, the docs that assert the field works**, plus
   `architecture-deep-dive.md`'s unreliable hardware claims.
4. **SDK defect 4, `IID_COUNT`.** Independent one-liner.
5. **The vendor request**, which is now worth making and is one sentence: honour
   `brightness` at whatever granularity is convenient. No board change, no ABI
   change, no new message. Four levels would close this completely, and the
   buzzer's "currently supported only 4 levels" is the in-house precedent.

Phase C is still not needed. Q8, the undocumented adjacent message types, is the
only question it would answer that remains open, and nothing depends on it.

### If anyone takes the workaround further

`BacklightPwm` is a demonstration, not a shippable technique: it holds a pin the
kernel owns and spins a thread to do it. The production shape is a timer driving
DMA into `GPIOF->BSRR`, which needs no alternate function on the pin, no CPU per
edge and no accurate sleep. That is what the vendor should build, and this app is
the evidence it is worth building.
