# Backlight control from an app: what the SDK actually publishes

Status: **ANSWERED. Outcome 3: the backlight is a binary enable and this
hardware cannot dim.** Phases A, B and D complete on hardware 2026-08-27. Phase C
is no longer needed to settle the question. Phase E is moot and must not be run.
No firmware dump was used and none is needed. The SDK-side findings were read
out of `una-sdk` and `watch-apps` and can be re-checked by content; the
hardware-side findings come from one run of `BacklightProbe` on the watch, whose
raw output is committed alongside the app in
`watch-apps@feat/backlight-probe`, `BacklightProbe/Output/`.

The objective, the question list and the phase plan live in
`BACKLIGHT_INVESTIGATION_PROMPT.md`.

---

## The answer

**The front-light is a plain GPIO enable. It cannot be dimmed, by an app or by
the kernel, because there is no duty cycle anywhere to set.**

The actuator is **PF3**, and the evidence is a clean register diff taken on the
watch:

| Register | Dark | Lit (every level, 100 down to 1) | Meaning |
| --- | --- | --- | --- |
| `GPIOF ODR` bit 3 | `1` | `0` | The light. Active low. |
| `GPIOF MODER` bits [7:6] | `01` | `01` | **General purpose output.** Not an alternate function. |
| `GPIOF OTYPER` bit 3 | `1` | `1` | Open drain. |
| `GPIOF AFRL` | `0x00000000` | `0x00000000` | No alternate function on any GPIOF pin. |
| `RCC` (all 64 words) | identical | identical | No clock was enabled or disabled. |

A pin in general-purpose-output mode is a pin no timer channel can reach. There
is no `CCRx` to write because nothing is connected to write it. That is Outcome 3
from the brief, and it means there is no workaround to find: **no app-side trick
produces 70 percent from a switch.**

`brightness` is therefore not a field the kernel discards on the way to a driver
that could have honoured it. It is a field describing a capability the hardware
does not have.

### Why this also rules out software PWM

A plain GPIO can still be dimmed by bit-banging it, so "no timer" is not by itself
"no dimming". The ladder rules that out too, and does it with a number.

If the kernel were toggling PF3 in software to make brightness 1 look like 1
percent, then a sweep taken at an arbitrary moment would find the pin low about 1
percent of the time. Six lit sweeps were taken, at 100, 75, 50, 25, 10 and 1, and
**every one of them read the pin low**. For the brightness-1 sweep alone that is a
1-in-100 coincidence; across the ladder it is not a coincidence at all.

The pin is held, not modulated.

### What the control sweeps say

`sweep_dark.txt` and `sweep_dark_after.txt` are byte-identical apart from their
header line. Nothing drifted across the four-minute run, so every diff taken
between them is trustworthy rather than merely suggestive.

Across the whole ladder only three blocks ever differ: `GPIOB`, `GPIOD` and
`GPIOF`. `RCC`, `SCB`, `NVIC`, all six `I2C` blocks, both `SPI` blocks and both
UARTs are identical in all eight sweeps. So the light is not an off-chip part
being reconfigured over a bus either, which was the other live possibility.

The `GPIOB` and `GPIOD` differences are **not** the backlight, and it is worth
saying why rather than leaving them looking like loose ends. Both are in `IDR`,
the input register, which the kernel does not drive. Neither tracks brightness:
`GPIOD IDR` bit 14 reads the same at brightness 25 and 1 as it does in the dark,
and `GPIOB IDR` bit 2 alternates with no pattern at all. They are other signals on
the board doing other things during a four-minute run.

---

## Verdict table

| # | Question | Phase A verdict | Confidence | Falsified by |
|---|---|---|---|---|
| Q1 | Kernel handles `0x02080000`, from which process kinds | **Yes, and from both.** Every one of the 20 requests came back `SUCCESS`, `completed=Y`, in 0 to 1 ms, from a Service and from the GUI alike. The "GUI only" comment on the block above does not reach this one | **CONFIRMED** (device, 2026-08-27) | `PENDING` or `TIMEOUT` against a non-zero send timeout, or a GUI-sent request differing from a service-sent one |
| Q2 | Where `brightness` dies | **In the kernel handler, and it could not have done otherwise.** `GPIOF ODR` bit 3 is byte-identical at 100, 75, 50, 25, 10 and 1. There is no duty cycle downstream for the field to reach | **CONFIRMED** (device, 2026-08-27) | Any register differing between two non-zero brightnesses. Across 22 blocks and six levels, none does |
| Q3 | `autoOffTimeoutMs = 0` semantics | **The header is right and the simulator is wrong.** 0 disables auto-off on device: the light held for the full 30 s observation window. The mock blanks within about 50 ms of the same request | **CONFIRMED** (device, 2026-08-27) | The light going out inside the window. It did not, at 0 or at `0xFFFFFFFF` |
| Q4 | `brightness = 0` off, and timer interaction | **Both as documented.** `brightness = 0` turns the light off immediately, beating a 60 s timer already armed; and a second request replaces a running timer rather than racing it (a 1 s timer re-armed to 60 s produced no dim at the 1 s mark) | **CONFIRMED** (device, 2026-08-27) | A dim at ~1 s in the cancel test, or the light surviving `brightness = 0` |
| Q5 | State readable back | **No SDK route.** No response field on `RequestBacklightSet`, no backlight event type, no `IBacklight` reachable from an app | CONFIRMED (repo) | An undocumented event type or IID. Phases B/C |
| Q6 | Kernel policy overriding an app | **No clamp up to 60 s**, and none at all on an indefinite hold: `t = 60000` fired at 60 s within about 400 ms, and `t = 0` was still lit at 30 s. The wrist-raise and idle-blanking halves are still untested | **PARTIAL** (device, 2026-08-27) | A maximum on-time clamp shorter than 60 s. There is none |
| Q7 | Unallocated IIDs return a live `IBacklight` | **No. All six return null on device.** `0x00050000` through `0x000A0000` each answered `null`. Closed | **CONFIRMED** (device, 2026-08-27) | Any non-null pointer. There were none |
| Q8 | Undocumented adjacent message types | Open. The type encoding leaves all 16 low bits free on every system type, so subcodes are structurally possible; nothing in the SDK uses them | n/a | Phase C dispatcher table |
| Q9 | What physically drives the light | **`GPIOF` pin 3**, a general-purpose open-drain output, active low: `ODR` bit 3 is 1 when dark and 0 when lit. No PMIC or I2C part is involved; every I2C and SPI block is identical in all eight sweeps | **CONFIRMED** (device, 2026-08-27) | The pin not tracking the light, or an I2C block moving with it |
| Q10 | Smallest direct-drive workaround | **Moot, and that is the finding.** Q11 says there is no duty cycle to drive. Writing `ODR` bit 3 directly would reproduce exactly the on/off the message already provides, at the cost of fighting the kernel for a pin it owns | **CONFIRMED** by Q11 | Q11 turning out differently |
| Q11 | Can the hardware dim at all | **No.** `MODER[7:6] = 01` (general-purpose output, not alternate function) and `GPIOF AFRL = 0`, so no timer channel can reach the pin. `RCC` is identical across every sweep. Software PWM is excluded too: six lit sweeps including brightness 1 all read the pin low | **CONFIRMED** (device, 2026-08-27) | `MODER` reading `10`, a non-zero `AFRL` nibble for pin 3, or any lit sweep reading the pin high |
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

| Possibility | Ruled out by |
| --- | --- |
| Hardware PWM on this pin | `MODER[7:6] = 01` and `AFRL = 0`. A general-purpose output has no timer connected to it |
| A timer running elsewhere driving it | Same. Whatever any timer is doing, it is not reaching PF3 |
| Software PWM (bit-banged `ODR`) | Six lit sweeps, including brightness 1, all read the pin low. At a 1 percent duty that single sample is a 1-in-100 coincidence |
| An off-chip dimmer over a bus | All six `I2C` blocks and both `SPI` blocks are identical in all eight sweeps |
| A clock being enabled for the light | `RCC`, all 64 words, identical in all eight sweeps |

### The timer blocks were never swept, and did not need to be

The run used the confirmed 22-block set; `sweep_timers.enable` was not present, so
the unconfirmed timer bases were not read. That turned out not to matter, and the
reason is worth recording: the question was never "is any timer running" but "can a
timer reach the pin that drives the light". `MODER` and `AFRL` answer that at the
pin, which is the safer place to ask and needs no address that has not already been
read successfully on this unit.

Nobody needs to enable those bases to reproduce this result.


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

## Outcome 3, and what follows from it

The brief set out three outcomes and asked for the deliverable each one implies.
This is the third: **not possible on this hardware.**

### The residual, handed off as the hardware question it is

What has been settled is that **the MCU has no way to dim this light**: the pin is
a plain output, nothing modulates it, and no bus carries a level to anything else.

What has *not* been settled, and cannot be from firmware, is whether the
front-light module or its boost stage has a dimming input at all: an analog or PWM
dim pin, or a current-set resistor, sitting unused on the board. The recovered
hardware inventory names no LED driver IC, which is consistent with the enable
being wired straight to a switch, but absence from a `strings` pass is not a
schematic.

Answering that needs a board inspection or the part number of whatever PF3 drives.
It is a hardware question and it should be asked as one. Nothing in firmware will
answer it, and no further register sweep will either.

### What the vendor would need to change

Worth stating precisely, because it is the thing a request would be built from,
and because it is smaller than it looks. The SDK change is not the problem: the
message already carries a 0-100 field and the kernel already parses it. What is
missing is underneath, and it is a **board change, not a firmware one** unless the
front-light already has an unused dim input.

So the honest ask is in two parts, and the first part costs nothing:

1. **Say so.** Document `brightness` as on/off, and the panel's front-light as a
   binary enable. Today the field invites every app author to attempt something
   the hardware cannot do, and two of the vendor's own docs assert it works.
2. **On a future revision**, route the front-light enable to a timer-capable pin
   and drive it as a PWM channel, or fit a driver with a dim input. Either makes
   `brightness` mean what it already says it means, with no ABI change at all,
   because the field is already there and already parsed.

### Phase E must not be run

The brief gated direct hardware drive on Q11 saying dimming is physically
possible. It says the opposite. There is nothing to drive: writing `ODR` bit 3 by
hand reproduces exactly the on/off that `REQUEST_BACKLIGHT_SET` already delivers,
while taking a pin the kernel owns and fighting its auto-off timer for it.

Skipping Phase E is the correct outcome here, not a shortfall.


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
| The backlight is a discrete front-light with no LED driver IC | LIKELY, strengthened | 2026-07-29 ledger by elimination, plus every I2C and SPI block reading identical across all eight sweeps: nothing on a bus is being told anything when the light changes |
| The real `Backlight` driver is constructed from a GPIO | CONFIRMED (device) | `mpBacklight = new Backlight(gpio)` in `architecture-deep-dive.md` turned out to be right, and PF3 is the GPIO. Note the same document's neighbouring "Real PWM Control" label is now confirmed **wrong**: right about the constructor, wrong about the mechanism |
| `RCC` words 0-63 span the peripheral clock-enable registers | UNVERIFIED, and no longer load-bearing | Recalled STM32U5 layout; RM0456 is still not on this machine. Q11 rests on `MODER` and `AFRL` at the pin, not on the RCC decode |

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

plus direct reads of `IBacklight.hpp`, `IBuzzer.hpp`, `IVibro.hpp`, `IKIP.hpp`,
`IKernel.hpp`, `ISettings.hpp`, `IAppComm.hpp`, `MessageBase.hpp`,
`MessageGuard.hpp`, `CommandMessages.hpp`, `KernelMessageDispatcher.cpp`,
`Mock/Backlight.{hpp,cpp}`, `OS/OneShotTimer.cpp`, `FwDump`'s
`DeviceContext.{hpp,cpp}`, and the two `Service::backlightOn` implementations in
`watch-apps`.

### What was not run

- Nothing on the device. No sweep, no probe, no measurement.
- No disassembly, and no `strings` pass: the dump is not on this machine.
- No RM0456 or DS13543 check, for the same reason. The `RCC` coverage claim above
  is the one load-bearing thing that depends on it.
- The `0x00050000` to `0x000A0000` IIDs were not probed. Note for whoever does:
  there are at least eight kernel-service-shaped interfaces in the SDK without an
  ID (`IBacklight`, `IBuzzer`, `IVibro`, `ISettings`, `ITime`, `IMutex`,
  `ISemaphore`, `ISensorData`) competing for six slots, so there is no reason to
  expect the backlight to be among them. Probe all six and log the pointers; do
  not call through any of them before Phase C confirms the vtable.

---

## Next

The question this investigation existed to answer is answered. What remains is
tidying up after it, and none of it is blocked on anything.

1. **SDK defect 2, the simulator mock.** Settled and unblocked: the device holds
   the light indefinitely at `timeout = 0`, so the mock should not start a timer
   at all in that case. Its own branch.
2. **SDK defect 1, the documentation.** Now writable with register evidence behind
   it. Its own branch.
3. **SDK defect 3, the two docs that assert the field works.** `sdk-overview.md`
   and `development-workflow.md` both describe `REQUEST_BACKLIGHT_SET` as setting
   screen brightness. `architecture-deep-dive.md` additionally claims "Real PWM
   Control" and names the wrong PMIC; its hardware claims should be marked
   unreliable rather than quietly corrected, since the same document is cited
   elsewhere.
4. **SDK defect 4, `IID_COUNT`.** Independent one-liner.
5. **The hardware question**, handed to whoever can look at the board.

Phase C is not needed. It would corroborate the handler and enumerate the message
table, which is interesting but changes nothing: the pin cannot dim regardless of
what the handler does with the field. If the dump turns up for another reason, Q8
is the only question left that it would answer.
