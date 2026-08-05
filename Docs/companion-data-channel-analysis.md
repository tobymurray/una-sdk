# Companion data channel: analysis and proposal

**Status:** analysis / request for comment. Not an implemented feature, and not
official SDK documentation — this is a contributor's write-up intended to ground
an upstream discussion.

**Scope of the analysis:** the public `UNAWatch/una-sdk` repository as of commit
`2764a3e7`. Every claim below about what does or does not exist was checked
against the files cited; where something is documented but absent from this
repository, that is stated explicitly rather than assumed.

---

## 1. The problem

There is currently no supported way for a third-party watch app to receive
arbitrary data from outside the watch.

Any app that needs a user-specific value — an athlete id, an account token, a
setting chosen on a website, a list the user curated on their phone — has
nowhere to get it from. The value must be compiled in, or typed on the watch
itself. For a device whose input is four buttons, on-watch entry is not a
serious answer for anything longer than a few characters.

This limits what third-party apps can be. Apps that are purely self-contained
(a stopwatch, a compass) work fine. Apps that represent *the user's* data from
some external system — the motivating case here was displaying a parkrun
athlete barcode, but the shape generalises to transit passes, membership ids,
loyalty codes, API tokens, saved routes — currently cannot be built at all.

---

## 2. What exists today

### 2.1 Implemented, with real code behind it

**App distribution.** Two documented paths, both real: copying a `.uapp` over
USB mass storage, and a web portal for closed-source apps
(`Docs/deploy.md:5`, `Docs/deploy.md:24`). `Docs/app-config-json.md` defines
the `config.json` metadata (`APP_ID`, `minKernelVersion`) and describes it as
being consumed by a mobile app for install matching — though no mobile app
source exists in this repository.

**Per-app IPC contract.** Each app defines its own message types and a `Sender`
helper wrapping `allocateMessage` / `sendMessage` / `releaseMessage`, carrying
a state snapshot between its Service and GUI halves. See
`Examples/Apps/Stopwatch/Software/Libs/Header/Commands.hpp` as the cleanest
example. This pattern is the natural landing point for externally-delivered
data: a kernel-side handler could construct the same message struct and post it
to an app's queue, indistinguishable from an internally-generated one.

**Settings persistence.** `Settings.hpp` (a plain struct) plus
`SettingsSerializer.{hpp,cpp}` (JSON save/load over `SDK::Interface::IFileSystem`)
is implemented independently in **five** apps: Cycling, Hiking, Running,
Treadmill, and Workout. `Running`'s and `Treadmill`'s `SettingsSerializer.cpp`
are byte-identical; Cycling and Hiking differ only in whitespace and one field.
The duplication is real and predates this analysis.

### 2.2 Contract only — no backend in this repository

**`RequestSetCapabilities`** (`Libs/Header/SDK/Messages/CommandMessages.hpp`)
lets an app declare that it wants phone notifications, music control, or the
USB charging screen. Several apps send it for real. But the only handler that
ships here — `Libs/Source/Simulator/App/KernelMessageDispatcher.cpp:97` — logs
`"Sets capabilities"` and returns `SUCCESS`. There is no phone or BLE stack
behind it in this repository.

**Accessory messages** (`REQUEST_ACCESSORY_PREPARE` / `_RELEASE`,
`EVENT_ACCESSORY_STATUS`) define a documented lifecycle for acquiring an
external BLE sensor, and multiple apps call it. A search of `Libs/Source/`
finds **zero** handlers. It is a contract awaiting a kernel that is not part of
this repository.

Neither of these is a criticism — a public SDK exposing contracts whose
implementation lives in closed firmware is a reasonable design. It does mean
neither can be *used* as a data channel from app code today.

### 2.3 Documented, but absent from this repository

`Docs/architecture-deep-dive.md:172-174` diagrams an `IBleManager` and a set of
BLE services — **DIS / CTS / BAS / FTS / NTS / CCS** — covering device info,
time, battery, file transfer, notifications, and a custom command service.
`CCS` and `FTS` are precisely the slots an arbitrary-data channel would use.

None of it exists as code here. `Libs/Source/Kernel/KernelBuilder.cpp:5-9`
wires exactly five interfaces — `ISystem`, `ILogger`, `IAppMemory`,
`IAppComm`, `IFileSystem` — and nothing BLE-shaped.

`Docs/app-config-json.md` additionally describes a CBOR activity-data format
intended for a phone app to consume; no encoder for it exists in `Libs/`.

---

## 3. Why the transport has to be the phone

BLE is the only radio. There is no Wi-Fi anywhere in this SDK — no `IWifi`, no
WiFi reference in `Libs/Header` or `Docs`. A watch app therefore cannot reach a
website directly, and any "get data from a website" story is necessarily:

```
website  ->  phone app  ->  BLE  ->  watch  ->  app
```

The first hop is ordinary web/app work and is not a watch-SDK concern. The
third hop is the missing piece. The fourth already exists.

This is the same architecture Garmin and Apple use, arrived at from the same
constraints:

- **Garmin** (Connect IQ): the user enters a value in the Garmin Connect phone
  app's per-app settings screen; Connect syncs it over BLE into the watch app's
  persistent storage, which the app reads at runtime. Notably, Connect IQ has
  no native barcode widget either — community parkrun apps render the bars
  themselves, exactly as this SDK would have to.
- **Apple**: not an app at all. A `.pkpass` bundle carries a `barcode` field
  (message + format, e.g. `PKBarcodeFormatCode128`), is generated server-side,
  added to Wallet on the phone, and syncs to the Watch automatically. No
  third-party watch code renders it; one system component draws any pass.

The Apple model is the more instructive one: don't make every app that needs to
show a user id solve provisioning and rendering independently. Provision once,
through a shared mechanism.

---

## 4. Constraints any solution must respect

These bound the design, and are the reason a general-purpose "sync framework"
answer would be wrong here:

| Constraint | Source |
|---|---|
| Kernel message pool blocks are **256 bytes**; a message struct must fit | `static_assert` in `Examples/Apps/Stopwatch/Software/Libs/Header/Commands.hpp:54` |
| Message pool RAM total: **4 KB** | `Docs/architecture-deep-dive.md:1627` |
| App memory: **256 KB**; kernel objects (mutexes/semaphores): 8 KB | `Docs/architecture-deep-dive.md:1622,1628` |
| Target is Cortex-M33 (STM32U575/585) | `target.config`, every example app |
| BLE only — intermittent, phone-initiated, power-costed. No Wi-Fi | absence of any `IWifi`/WiFi reference |
| Service and GUI communicate only via the kernel message bus | every example app's `Service`/`Model` split |

The 256-byte ceiling is the sharpest one: it forces any payload larger than
roughly 200 bytes to be explicitly chunked, and makes "just send the data" a
non-answer. Note that the ceiling is *documented and real* but only enforced by
`static_assert` in one app today (Stopwatch); the simulator's allocator does not
enforce it at runtime.

---

## 5. Proposed direction

Deliberately modest, and shaped so the closed BLE implementation is the *only*
piece that must come from the platform owner:

1. **A generic envelope** — app id, data type, version byte, and a bounded
   payload — that any app can receive external data through, instead of each
   app inventing a bespoke message. Chunk fields present and validated from v1
   so multi-chunk support is not a breaking change later.

2. **A `Transport` interface** that a real CCS/FTS handler would implement.
   This interface *is* the deliverable — it is the concrete seam the platform
   side would fill, rather than prose describing one.

3. **An explicit opt-in**, following the existing `RequestSetCapabilities`
   precedent, so an app must declare that it accepts external data. Nobody
   wants every installed app silently writable from the phone.

4. **A generic settings serializer**, extracted from the five existing copies.
   Note that only the *file plumbing* generalises — the per-app structs differ
   in enum values, nested types, and load-time normalisation invariants — so
   the split is a per-app codec over shared plumbing, not a templated
   "settings struct".

The phone side is intentionally out of scope. Whether the data originated from
a website, manual entry, or another device is not the watch's concern once it
arrives.

This is also not Una-specific. InfiniTime/Gadgetbridge, Bangle.js, and WatchY
each solved the same problem separately for custom watch faces and per-app
settings. A clean specification here would be useful beyond one SDK.

---

## Appendix A — draft discussion post

> **RFC: a generic app data channel (phone → watch) — open to collaborating on
> a shared spec?**
>
> I've been building example apps against the public SDK (most recently a
> Code 128 barcode display — useful for things like a parkrun id), and kept
> hitting the same wall: any app that needs a user-specific value has nowhere
> to get it from except a hardcoded default. There's no way for a phone-side
> companion app to hand data to an arbitrary third-party app on the watch.
>
> Digging through the architecture docs, it looks like this was already
> anticipated — `architecture-deep-dive.md` reserves a BLE slot for exactly
> this ("CCS" alongside DIS/CTS/BAS/FTS/NTS) — but there's no spec for what
> rides on that slot. Meanwhile the public SDK already has two patterns that
> are most of what a solution needs: the per-app `Commands.hpp` + `Sender`
> message contract, which is the exact shape a BLE-delivered payload should
> land as; and `Settings.hpp`/`SettingsSerializer.*`, which is already a
> working save/load pattern — the missing piece is just populating it from
> outside the watch. (That serializer is currently copy-pasted across five
> apps, two of them byte-identical, so a generic version looks overdue on its
> own merits.)
>
> Roughly what I have in mind: a generic envelope (app id + data type +
> version + bounded payload) that the kernel demuxes off CCS into the target
> app's existing message queue, so app code needs no new API; an opt-in flag
> so apps must declare they accept external data; chunking designed around the
> 256-byte message-pool ceiling from day one; and a version byte so this can
> evolve. The phone side is deliberately out of scope for a v1 spec.
>
> This isn't Una-specific either — InfiniTime/Gadgetbridge, Bangle.js and
> WatchY all had to solve this for custom watch faces and per-app settings.
>
> What I can offer: a written spec for the envelope and opt-in, and a worked
> example entirely inside the public SDK/simulator — an app that persists a
> value and receives it through a stubbed transport, demonstrating the full
> loop with no BLE involved. Once there's a real CCS implementation to target,
> happy to help wire up the demuxing.
>
> What I'm asking for: not a commitment — just a "yeah, send the spec, let's
> talk CCS" or a "nope, that slot's spoken for, here's why".

## Appendix B — implementation brief

Retained as the specification handed to an implementer for the proof of
concept. Corrections applied after review are noted inline.

**Objective.** A small, generic, reusable library plus one real consumer,
grounding the RFC in working code. Not a BLE implementation.

**Hard constraints.** As section 4 above. Additionally: BLE and kernel
internals are closed-source and off-limits — design the interface boundary a
real CCS/FTS handler would implement against; do not write or fake BLE. The
whole PoC must build and run in the Linux simulator
(`UNA_SDK=<repo root> make -f simulator/gcc/Makefile` from an app's
`TouchGFX-GUI` directory) with no hardware.

**Reuse, don't reinvent.** The `Commands.hpp` + `Sender` idiom; the five
existing settings serializers; the `RequestSetCapabilities` opt-in shape; the
accessory request/release/status lifecycle as precedent for subscribe
semantics; `SDK::Interface::IFileSystem` for storage; `Tests/Host/` (gtest,
with existing kernel and in-memory-filesystem test doubles) for verification.

**Deliverables.** (1) a generic settings serializer under the SDK,
parameterised over an app's own struct, with a per-app codec; (2) a generic
envelope; (3) a `Transport` interface with a single simulator-only
implementation, with the production transport left as an explicit extension
point; (4) one example app migrated to be the first consumer, with no bespoke
wire-format code left in it.

**Non-goals.** No BLE/GATT code of any kind, real or stubbed. The simulator
transport is not proposed as the production design — it exists so the app-side
contract is exercisable. No changes to `KernelBuilder.cpp` or
`Libs/Header/SDK/Interfaces/`. No abstraction for transports this device does
not have (Wi-Fi, NFC).

**Quality bar.** Match existing conventions — Doxygen file headers, comments
explaining *why* rather than *what*, static/bounded buffers over dynamic
allocation. Verify serialization and envelope logic standalone with
byte-level golden vectors before wiring anything into the message bus; an
embedded wire format's correctness should not be inferred from "it compiled".

**Corrections applied after first review.** An earlier draft of this brief
asserted that the 256-byte `static_assert` appears in every app's
`Commands.hpp` (it appears in one), that the settings serializer is duplicated
across three apps (five), and that the RAM budget is ~8 KB (that figure is
kernel objects; message pools are 4 KB and app memory 256 KB). The corrected
figures are used throughout this document.
