# Backlight control from an app: what the SDK actually publishes

Status: **Phase A complete. Phases B, C, D, E not started.**
No device was used. No firmware dump was used. Everything below was read out of
`una-sdk` (`main`, 8cdb7314) and `watch-apps` (`main`), and can be re-checked by
content without hardware.

The objective, the question list and the phase plan live in
`BACKLIGHT_INVESTIGATION_PROMPT.md`. This file records what Phase A settled,
what it changed about the plan, and what is still open.

---

## Headline

Phase A produced a stronger answer to Q2 than the plan expected it to, and it did
it without a device.

**The SDK's own kernel-facing interface for the backlight has no brightness
parameter at all.** `IBacklight` is `on(timeout)` / `off()` / `isOn()`. That is
the interface the real driver implements and the interface the kernel's component
factory hands out. There is no argument on it for a level to travel through.

So `RequestBacklightSet::brightness` is not a field that the driver ignores. It is
a field with nowhere to go: on the far side of the message boundary the
abstraction the kernel actually uses is binary. That is consistent with the
measured device behaviour (a request for 70 percent does not produce 70 percent)
and it locates the death of the field at the interface boundary rather than in the
driver or the silicon.

This does **not** settle Q11. A binary software abstraction can sit on top of a
PWM channel running at a fixed duty. Phase D is still the experiment that decides
between outcome 2 and outcome 3. What Phase A does is make outcome 3 the leading
hypothesis and give the write-up its spine.

---

## Verdict table (Phase A only)

| # | Question | Phase A verdict | Confidence | Falsified by |
|---|---|---|---|---|
| Q1 | Kernel handles `0x02080000`, from which process kinds | Open. Nothing in the SDK gates the message by process kind; the "GUI only" comment governs the `0x0206`/`0x0207` block above it, not the `0x0208` block | n/a | Phase B |
| Q2 | Where `brightness` dies | **At the interface boundary.** `IBacklight` has no level parameter, so nothing downstream of the message handler can carry one | **LIKELY** (repo, structural) | A kernel `IBacklight` with a wider vtable than this header declares; a handler that writes a duty cycle without going through `IBacklight`. Phase C settles it |
| Q3 | `autoOffTimeoutMs = 0` semantics | Open on device. **The simulator is wrong**: it blanks after about 50 ms where both headers say 0 disables auto-off | CONFIRMED (simulator) | Phase B on device |
| Q4 | `brightness = 0` off, and timer interaction | Open on device. Simulator maps `brightness > 0` to `on()` and everything else to `off()`, and `on()` cancels a pending timer while `off()` does not | CONFIRMED (simulator) | Phase B |
| Q5 | State readable back | **No SDK route.** No response field on `RequestBacklightSet`, no backlight event type, no `IBacklight` reachable from an app | CONFIRMED (repo) | An undocumented event type or IID. Phases B/C |
| Q6 | Kernel policy overriding an app | Open. `Docs/Simulator.md` says a wrist-raise activates the backlight for 5 s, which is the simulator's own model, not measured device policy | n/a | Phase B |
| Q7 | Unallocated IIDs return a live `IBacklight` | Open. Confirmed there is no `IID_BACKLIGHT`, `IID_BUZZER` or `IID_VIBRO` anywhere in the repo, and that the three interfaces are referenced **only** by simulator code | CONFIRMED (repo) | Phase B probe, Phase C `queryInterface` switch |
| Q8 | Undocumented adjacent message types | Open. The type encoding leaves all 16 low bits free on every system type, so subcodes are structurally possible; nothing in the SDK uses them | n/a | Phase C dispatcher table |
| Q9 | What physically drives the light | Open. The only in-repo claim is `mpBacklight = new Backlight(gpio)` in `architecture-deep-dive.md`, from a document with a confirmed-wrong part number in it | UNVERIFIED | Phase D |
| Q10 | Smallest direct-drive workaround | Not reached | n/a | Gated on Q11 |
| Q11 | Can the hardware dim at all | Open, and still the pivotal question | n/a | Phase D |
| Q12 | Kernel dims to its own setting, reachable from an app | **SDK half settled negative.** Neither `ISettings` nor `RequestSystemSettings` nor `RequestDisplayConfig` carries any display or brightness field | CONFIRMED (repo) | Phase D step 4 for the kernel half |

---

## What Phase A confirmed, corrected and added

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

## SDK defects established so far

All four are independent of what the hardware turns out to do, and each is its own
branch and its own PR.

1. **A published field that does nothing.** `RequestBacklightSet::brightness` is
   documented "0-100%" and is inert on device. It sits in a `static_assert`-fixed
   40 byte slot the kernel parses, so it cannot be removed. The fix is the
   parenthetical the buzzer already has, plus any app-facing wrapper not offering
   it. Wait for Phase B's measured matrix before writing the exact wording.
2. **A simulator mock that contradicts its own header.** `Mock::Backlight::on(0)`
   blanks after about 50 ms; `IBacklight` and `RequestBacklightSet` both say 0
   disables auto-off. Fix the mock to match the headers, or the headers to match
   the device once Phase B settles Q3. Do not fix this one before Q3.
3. **`Docs/` asserts the inert behaviour works.** `sdk-overview.md` and
   `development-workflow.md` both describe `REQUEST_BACKLIGHT_SET` as setting
   screen brightness; `Simulator.md` presents mock behaviour as platform
   behaviour; `architecture-deep-dive.md` asserts PWM brightness control and names
   the wrong PMIC. Needs a correction plus a real capability section, both backed
   by the Phase B capability map.
4. **`IID_COUNT` is not a count.** It evaluates to `0x000B0001`. Latent, unused,
   one-line fix, and independent of everything else here.

---

## Ledger

| Claim | Confidence | Source |
|---|---|---|
| `IBacklight` exposes no brightness parameter | CONFIRMED | `Libs/Header/SDK/Interfaces/IBacklight.hpp` |
| No `IID_BACKLIGHT`/`IID_BUZZER`/`IID_VIBRO` exists in the repo | CONFIRMED | grep for `IID_` across the tree; `IKIP.hpp` |
| The three interfaces are referenced only by simulator code and MSVS file lists | CONFIRMED | grep for the type names across `una-sdk` |
| `IKernel` exposes only `kip`, so `queryInterface` is the only door | CONFIRMED | `Libs/Header/SDK/Interfaces/IKernel.hpp` |
| `brightness` is inert on device | CONFIRMED, field observation | Repo owner, carried forward from the handoff. Not re-measured here |
| `brightness` dies at the interface boundary rather than in the driver | LIKELY | Structural: no parameter exists on `IBacklight` to carry it. Needs Phase C to confirm the handler ends in `IBacklight::on` |
| The SDK publishes percentage fields over discrete interface levels | CONFIRMED | `RequestBuzzerPlay::Note::volume` comment, `IBuzzer::Note::level`, `volume / 33` in the simulator dispatcher |
| `Mock::Backlight::on(0)` blanks after about 50 ms | CONFIRMED | `Mock/Backlight.cpp` plus `OneShotTimer::start`/`run` |
| The simulator signals `SUCCESS` for the backlight case unconditionally | CONFIRMED | `Libs/Source/Simulator/App/KernelMessageDispatcher.cpp` |
| No display or brightness field in `ISettings`, `RequestSystemSettings` or `RequestDisplayConfig` | CONFIRMED | The three declarations |
| `IID_COUNT` evaluates to `0x000B0001` | CONFIRMED | `IKIP.hpp`, C++ enum rules |
| `architecture-deep-dive.md` names the PMIC STPMIC1; the ledger confirmed PCA9420 | CONFIRMED | The doc, and the 2026-07-29 hardware inventory |
| The backlight is a discrete front-light with no LED driver IC | LIKELY | 2026-07-29 ledger, by elimination from the recovered inventory |
| The real `Backlight` driver is constructed from a GPIO | UNVERIFIED | `architecture-deep-dive.md`, a document with a known-wrong hardware claim in it |
| `RCC` words 0-63 span the peripheral clock-enable registers | UNVERIFIED | Recalled STM32U5 layout. RM0456 is not on this machine |

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

1. **Phase D, coarse form, with the sweep that already exists.** Extend `FwDump`
   into `BacklightProbe`: keep the sweep, add the `RequestBacklightSet` matrix, add
   a button-triggered re-sweep. Dark, then lit at 100, then lit at 25, then lit
   at 1, then once per value of the watch's own brightness setting if it has one.
   Diff. `ODR` and `MODER`/`AFR` on whichever port moved answer the coarse Q9 and
   Q11 between them.
2. **Phase B rides along in the same binary**, since it has to. Measure the
   brightness curve with a fixed camera or a light meter rather than by eye,
   including the flat curve if that is what it is.
3. **Ask for the dump and the two manuals** before planning Phase C.
4. Defect 4 (`IID_COUNT`) is independent of all of the above and can go now.
