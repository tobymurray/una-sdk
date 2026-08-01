# Scoping: UNA Watch support in Gadgetbridge

Research date: 2026-08-01. Sources: live Codeberg repo (`git clone https://codeberg.org/Freeyourgadget/Gadgetbridge.git`,
HEAD `83285d9`, 2026-07-18 — **note the GitHub mirror is stale, frozen at a Dec 2024 snapshot with only
63 of the current 98 device packages; do not use it for source review, only Codeberg**), the Codeberg
Gitea API (`/api/v1/repos/Freeyourgadget/Gadgetbridge/pulls/...`) for real merged-PR diffs and review
threads, `gadgetbridge.org/internals/development/*`, and our own investigation doc at
`Docs/Investigations/2026-07-29-hardware-config-recovery/BLE-COMPANION-protocol-spec.md`.

---

## 0. Headline architecture finding (read this before the checklists)

Gadgetbridge already ships a **transport-agnostic Garmin FIT binary-format decoder**:
`nodomain.freeyourgadget.gadgetbridge.service.devices.garmin.fit.FitFile.parseIncoming(byte[])`
(`app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/garmin/fit/FitFile.java`).
Despite living under the `garmin` package, `parseIncoming` takes a raw byte array and returns generic
`RecordData` messages — it has no dependency on Garmin's BLE transport, ConnectIQ, or Garmin's DAO
entities. Since our investigation doc **confirms UNA's `.fit` files are genuine, standard Garmin-format
FIT files** (byte-exact header arithmetic + independently-recomputed CRC-16 match on two real files,
§2.1 of the spec), a UNA `DeviceSupport` can hand the raw bytes pulled over FTS straight to this class
instead of writing a FIT parser from scratch. The per-message → per-sample-table mapping
(`FitImporter.java`, `service/devices/garmin/fit/FitImporter.java`) is Garmin-entity-specific and *not*
reusable as-is, but the parsing layer is. **Flag this for a maintainer early** — reusing a class from
another device's package is a purposeful architectural bet, not an accident, and a maintainer may want
it either left alone, given a shared home, or reimplemented independently; don't just silently import it
across package boundaries and hope nobody minds in review.

---

## 1. Gadgetbridge's contribution process — concrete, checkable requirements

| Requirement | Evidence | Detail |
|---|---|---|
| License header | Every `.java`/`.kt` file, verified against current files (e.g. `DeviceType.java`, `OlleeDeviceCoordinator.kt`) | AGPLv3 header block, first line `/*  Copyright (C) <year(s)> <your name>`, ending `... GNU Affero General Public License ... <https://www.gnu.org/licenses/>. */`. New files use just the new contributor's name (see `OlleeDeviceCoordinator.kt`: `Copyright (C) 2026 Ken Blizzard-Caron`) — you do **not** need to append yourself to the giant existing multi-author block used in shared files like `DeviceType.java` unless you're editing that file, in which case leave the existing block as-is (Ollee's PR only added one import line + one enum entry to `DeviceType.java`, no header churn). |
| No `e.printStackTrace()` | `.gitea/pull_request_template.md` (verbatim: *"Do not use `e.printStacktrace()`, use slf4j logger or `GB.toast` for logging"*) | Mechanical PR-template rule, cited with a link to the project-overview doc. |
| No manual `strings.xml` translation edits | Same template (*"Do not add translations by editing the language variants of strings.xml ... use Weblate"*) | Only touch the base (English) `strings.xml`; other locales come from Weblate. |
| Rebase, not merge, to update your branch | Same template (*"Use `git rebase` to bring your branch up to date, do not use a merge commit"*) | **Corroborated live**: in PR #6403 a contributor force-pushed after a rebase and asked "if this is against some rule, let me know" — maintainer ThomasKuehne replied *"Rebase and forced push are actually usually preferred."* |
| Do not touch `CHANGELOG.md` yourself | Real review comment, PR #6401 (`gloryfitpro: initial support for DM58`), reviewer joserebelo on the `CHANGELOG.md` diff hunk: **"Please do not update the changelog."** | Maintainers manage `CHANGELOG.md`; a first PR that edits it will get bounced. Not documented anywhere except this review comment — a good example of an unwritten rule this scoping exercise exists to surface. |
| CI gates on every PR (Woodpecker CI, `.woodpecker/*.yml`) | `can_master_build.yml`: `./gradlew assembleDebug --stacktrace`; `run_lint.yml`: `./gradlew lint`; `run_tests.yml`: `./gradlew :app:testBangleDebugUnitTest :app:testMainDebugUnitTest` | Three separate automated gates fire on every push/PR: **build**, **Android lint**, and **JVM unit tests (Robolectric) on both the `bangle` and `main` flavors**. This means any unit test you add (protocol parsing tests, `BluetoothNameTest` entries) is not optional politeness — it runs in CI and must pass. |
| Device icon / image asset | Not required for a first PR — verified by example, not by docs | Both recent case studies reuse a generic stock icon rather than adding a new one: Ollee Watch One uses `R.drawable.ic_device_default`; GloryFitPro DM58 uses `R.drawable.ic_device_amazfit_bip` (a different existing device's icon, reused as a placeholder). No PR feedback asked for a proper icon in either case. |
| Wiki / device-list update | Real evidence: PR #6411 comment *"Docs PR https://codeberg.org/Freeyourgadget/website/pulls/262"*; PR #6457 body *"PR for the website https://codeberg.org/Freeyourgadget/website/pulls/265"* | This is a **separate PR against the `Freeyourgadget/website` repo** (not the main Gadgetbridge repo), expected to land alongside or shortly after the code PR. Exact shape, from website PR #262's real diff: (1) add an entry under `device_support:` in `device_support.yml` (vendor, `type: wearable`, a `flags:` list e.g. `[feature_partial, pair_free]`, optional `links: {protocol: *link_x_protocol}` anchor); (2) add `docs/gadgets/wearables/<device>.md` — short page with a `{{ device("device_key") }}` macro, a `{{ NNNN\|pull }}` macro linking the originating PR, a supported-features bullet list, and a "Missing features" bullet list; (3) optionally `docs/internals/specifics/<device>-protocol.md` for a reverse-engineered protocol write-up; (4) register both new doc pages in `mkdocs.yml`'s `nav:` tree. This is realistic and low-effort to prepare given how much protocol detail our investigation doc already has. |
| Tests mandatory? | Not written down as a rule, but functionally yes | See CI gates above — `testMainDebugUnitTest` will fail the whole pipeline if you add a broken/no test for parsing logic that other tests exercise, and reviewers explicitly ask for missing coverage (see §3). |
| Changelog/wiki/settings string conventions beyond the above | Not found as a separate written style guide | No dedicated Java/Kotlin style guide or checkstyle config exists in the repo (`find . -iname "*checkstyle*"` returns nothing) — style is enforced by `./gradlew lint` (Android Lint) plus human review, not a separate linter config. |

Primary process docs (for completeness, all fetched and cross-checked against the live repo since the
docs drift from source — see §4):
- `gadgetbridge.org/internals/development/new-gadget/` — the 7-part tutorial the task brief already
  summarized; confirmed accurate in *shape* but several concrete class/method names it uses have since
  been superseded (see §4).
- `gadgetbridge.org/internals/development/project-overview/` — architecture overview (`DeviceCoordinator`,
  `DeviceSupport`, `BtLEQueue`, `GBApplication`, greenDAO, slf4j logging convention, vector-icon
  convention, `colors.xml`).
- `gadgetbridge.org/internals/topics/support/` — device-request process; explicitly tells hardware/firmware
  makers to "study the Bangle.js implementation" and prefer standard GATT services when possible.
- Codeberg wiki `Support-for-a-new-Device` is now a stub redirecting to the URL above (confirms the wiki
  → website migration is real and already done).

---

## 2. Real merged "add a new device" PRs — case studies

Found via the Codeberg Gitea API (`GET /repos/Freeyourgadget/Gadgetbridge/pulls?state=closed&type=all`),
filtered to recently-merged, genuinely new BLE GATT device families (not Bluetooth Classic, not an
existing family's variant-only addition, not headphones/scales). All four below are from July 2026.

### 2.1 PR #6411 — "Add Ollee Watch One support" (merged 2026-07-24) — **primary structural template**

Ollee is the closest analog to UNA in scope: a smartwatch-class BLE device with connect + time-sync +
activity-step sync + a small custom frame protocol over a standard transport (Nordic UART Service —
same NUS UUIDs UNA's firmware clones). All-Kotlin, single author, single quick review round.

**Files touched** (18 files, 2233 additions, 0 deletions — a clean net-new PR, nothing else disturbed):
```
GBDaoGenerator/.../daogen/GBDaoGenerator.java                (+11, entity registration)
devices/ollee/OlleeActivitySampleProvider.kt                 (+46, new)
devices/ollee/OlleeConstants.kt                               (+50, new)
devices/ollee/OlleeDeviceCoordinator.kt                       (+78, new)
model/DeviceType.java                                         (+2,  enum entry + import)
service/devices/ollee/OlleeActivityRecords.kt                 (+45, new)
service/devices/ollee/OlleeAlarm.kt                            (+135, new)
service/devices/ollee/OlleeDeviceSupport.kt                    (+363, new)
service/devices/ollee/OlleeFacesTable.kt                       (+73, new)
service/devices/ollee/OlleeProtocol.kt                         (+255, new)
service/devices/ollee/WeekdayRegisterComposer.kt               (+111, new)
res/values/strings.xml                                         (+1,  device name string)
test/.../devices/BluetoothNameTest.java                        (+1,  discovery test entry)
test/.../service/devices/ollee/OlleeActivityRecordsTest.kt      (+139, new)
test/.../service/devices/ollee/OlleeAlarmTest.kt                (+308, new)
test/.../service/devices/ollee/OlleeFacesTableTest.kt           (+227, new)
test/.../service/devices/ollee/OlleeProtocolTest.kt             (+189, new)
test/.../service/devices/ollee/WeekdayRegisterComposerTest.kt   (+199, new)
```
Note the test-to-source ratio: **1062 lines of unit tests against ~950 lines of device logic** — tests
outweigh implementation. This is the realistic bar for a PR that sails through review in one round.

**Real review feedback** (Codeberg PR #6411, reviewer `joserebelo`, inline comments — quoted verbatim,
paraphrase in brackets where the exact wording was mine to compress):
- *"Could you add it to BluetoothNameTest?"* — the discovery-by-advertised-name test file
  (`app/src/test/java/.../devices/BluetoothNameTest.java`) is expected to gain an entry mapping a sample
  advertised name to the new `DeviceType`, even though nothing forces this mechanically outside review.
- *"Should we log something? Silent failures are a pain to debug. Same on invalid header."* — flagged two
  places in `OlleeProtocol.kt` that swallowed parse failures; author's fix, confirmed merged: *"`parseFrameRaw` now warns on both invalid header and CRC mismatch."*
- *"I think this is the same as `builder.writeChunkedData`?"* — flagged a hand-rolled chunked-write loop
  in `OlleeProtocol.kt` that duplicated an existing `TransactionBuilder` helper; author's fix: *"writeFrame now calls the built-in `builder.writeChunkedData(getCharacteristic(RX), frame, MAX_ATT_CHUNK)`."* — **maintainers actively check for reinvented wheels against the existing `TransactionBuilder`/`AbstractBTLEDeviceSupport` API surface.**
- *"Don't forget to call `GB.signalActivityDataFinish(device)`"* — a UI-signaling call at the end of an
  activity-data drain/fetch, easy to omit and not caught by the compiler or tests.
- Final review verdict: *"Some very minor comments, but overall LGTM. Feel free to remove the WIP flag
  once you're ready to merge it."* — merged eight days after that comment, contributor thanked the
  reviewer for "the extremely speedy review."

**Structural template read in full**: `OlleeDeviceCoordinator.kt` and `OlleeDeviceSupport.kt` (see §3 for
the extracted pattern).

### 2.2 PR #6401 — "gloryfitpro: initial support for DM58" (merged 2026-07-25)

Smaller, MVP-scoped PR establishing a brand-new device *family* package (`gloryfitpro`) meant to host
multiple future watches sharing one BLE dialect — directly analogous to how a UNA family could be
structured if Toby expects more than one UNA-firmware-variant watch eventually.

**Files touched** (5 files, 922 additions):
```
devices/gloryfitpro/GloryFitProCoordinator.kt          (+112, new — abstract family base coordinator)
devices/gloryfitpro/watches/DM58Coordinator.kt          (+35,  new — concrete per-model coordinator)
service/devices/gloryfitpro/GloryFitProSupport.kt       (+772, new — MVP: connect + firmware read + time)
model/DeviceType.java                                    (+2)
res/values/strings.xml                                   (+1)
```
No test files were added in this PR at all — and it merged anyway. This is a real, useful data point
against over-indexing on Ollee's test-heavy example: **test coverage is valued and asked for, but not a
hard gate the maintainers will block a merge on if the PR is otherwise small and low-risk.** (It went
through active community back-and-forth for a week post-merge about which other watches share the
dialect — see the PR thread — which is normal ongoing device-family expansion, not pre-merge blocking
feedback.)

**Real review feedback** (reviewer `joserebelo`):
- *"Might be worth to make this return `GloryFitPro`, so it's clearer in the test device dialog."* — on
  the concrete `DM58Coordinator`'s manufacturer/display-name string; a naming-clarity ask specific to
  Gadgetbridge's debug "add test device" UI, not something you'd know to check without this evidence.
- *"Please do not update the changelog."* (already covered in §1).

### 2.3 PR #6403 / #6457 — AK102 / AK86 ("TopStep / FitCloud" platform)

A two-PR pattern worth copying if we ever expect more than one UNA hardware SKU: #6403 built the shared
`fitcloud` driver plus the first concrete model (AK102); #6457 then added a second model (AK86) as "a
thin coordinator that reuses the existing FitCloud driver, with per-device capabilities gated at runtime
by the device-info feature bitmap" — i.e., **one shared protocol/support class, thin per-model
coordinators that just declare capabilities**, not a new support class per SKU.

**Real review feedback** (reviewer `ThomasKuehne`, PR #6403, all inline, all addressed and merged):
- *"How about simply using `GenericSpo2Sample` instead of a custom table?"* and, separately, *"How about
  using `GenericStressSample` instead of a custom table?"* — on the `GBDaoGenerator.java` diff. **Concrete, recurring ask: don't add a bespoke greenDAO entity for a metric type (SpO2, stress, etc.) that Gadgetbridge already has a shared/generic sample table for** — check `devices/GenericSpo2SampleProvider`, `GenericStressSampleProvider`, `GenericMetricSampleProvider`, `GenericTrainingLoadAcuteSampleProvider` etc. (all present in the current tree) before adding a new entity. This directly matters for our fast-follow activity-sync PR — see §5.
- *"Please guard the whole content of the dispose method with `synchronized (ConnectionMonitor){…}`"* — a
  concurrency-correctness ask on the `DeviceSupport`'s teardown path.
- *"I think Singapore and Malaysia are also using the simplified version"* — a data-correctness nit on a
  locale/region constants table, addressed with "done." (Not generalizable beyond "expect nitpicks on any
  hardcoded regional/locale tables you ship.")
- A follow-up comment showed the reviewer proactively suggesting a *specific* code change to unlock a UI
  feature: *"If you change this to: `activitySample.addIntProperty("distanceCm")...` / override
  `supportsActiveCalories` and `supportsActivityDistance`... Gadgetbridge is able to display this
  data."* — i.e., **the coordinator's `supportsXxx()` boolean overrides gate whether data you already
  wrote to the DB actually surfaces in the UI**; a data column can be present and correctly populated and
  still show nothing if the corresponding `supportsXxx()` isn't flipped to `true`.
- Rebase/force-push exchange already covered in §1.

### 2.4 PR #6400 — "Initial support for MoYoung L70" (merged 2026-07-24)

Smallest of the four (207 additions, 7 files) — adds one new model onto an *existing* device family
(`moyoung`) rather than a new coordinator/support pair from scratch, so it's a weaker structural template
but a good source of one more genuine review nit:
- *"Should this be `L70` without a space? (Does the bluetooth name have a space as the unit test?)"* on the
  `strings.xml` display name — reviewer cross-checking the human-readable name against the exact
  advertised-name string used in the discovery test, since the two are allowed to differ but any
  mismatch is worth a deliberate second look. Author's answer: *"I included the space because the
  bluetooth name has a space. _and it felt annoying_"* — accepted as-is.

---

## 3. Minimal first PR — file-by-file checklist (coordinator + connect + battery/firmware)

Current actual class names/packages, verified directly against the live Codeberg tree (not the tutorial
docs — see §4 for where they've drifted). Modeled on `OlleeDeviceCoordinator.kt` /
`OlleeDeviceSupport.kt`, cross-checked against `GloryFitProCoordinator.kt`.

| # | Item | Concrete GB location | What we already know (cite investigation doc) vs. new plumbing |
|---|---|---|---|
| 1 | `DeviceType` enum constant | `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/model/DeviceType.java` — add one `UNA_WATCH(UnaDeviceCoordinator.class)` entry + one import line, anywhere in the (not strictly alphabetized, but roughly grouped) list | **New plumbing.** No UNA-specific knowledge needed; this is Gadgetbridge's device registry, not protocol. |
| 2 | `DeviceCoordinator` subclass | New file `devices/una/UnaDeviceCoordinator.kt`, extends `nodomain.freeyourgadget.gadgetbridge.devices.AbstractBLEDeviceCoordinator` (**not** the tutorial's `AbstractDeviceCoordinator` — see §4) | **Mixed.** `getSupportedDeviceName()` returns `Pattern.compile("^UNA Watch \\d{6}$")` — we already know the exact advertised-name pattern (`"UNA Watch %06d"`, spec §investigation doc header). `getBondingStyle()` → `BONDING_STYLE_BOND` (or `BONDING_STYLE_ASK`) is safe to pick with confidence: the spec's §4 conclusion is CONFIRMED triple-corroborated that **standard BLE bonding + link-layer encryption is the entire gate**, no app-layer secret, so there's no reason to pick `BONDING_STYLE_REQUIRE_KEY`. `getManufacturer()`, `getDeviceKind()` (→ `WATCH`), `getDeviceSupportClass()`, `getDefaultIconResource()` (reuse a stock icon — precedent in §2.1/§2.2, no new asset needed for a first PR) are boilerplate. |
| 3 | `DeviceSupport` subclass | New file `service/devices/una/UnaDeviceSupport.kt`, extends `nodomain.freeyourgadget.gadgetbridge.service.btle.AbstractBTLESingleDeviceSupport` (confirmed current class, `app/src/main/java/.../service/btle/AbstractBTLESingleDeviceSupport.java`; `AbstractBTLEMultiDeviceSupport` also exists for multi-device pairing scenarios, not our case) | **Mixed.** `initializeDevice(builder)` shape (`setDeviceState(INITIALIZING)` → register notifies → `setDeviceState(INITIALIZED)`) is generic GB plumbing, directly modeled on `OlleeDeviceSupport.initializeDevice`. What UNA-specific value we bring: we already know the *exact* handle/UUID and read sequence for Battery (`0x180F`, handle `0x0021`), DIS firmware/hardware strings (handles `0x0016`/`0x0018`), and CTS (byte-exact SIG layout, confirmed from a live capture) — this is unusually well-pinned-down compared to most first-time device PRs, which are often reverse-engineering blind going into the coordinator/support split. |
| 4 | GATT UUID constants | New file `devices/una/UnaConstants.kt` (established convention — every device with custom UUIDs defines them locally, e.g. `OlleeConstants.kt`, `WaspOSConstants.java`; **there is no shared registry for non-SIG-standard UUIDs to add to**) | **New plumbing for the custom UUIDs** (FEBB/ADAF pair, NUS clone, CCS, CANS — all already fully enumerated with confidence tags in the investigation doc §1), but for the *standard* SIG services (`0x180F` Battery, `0x180A` DIS, `0x1805` CTS) **reuse the existing constants already in `GattService.java`**: `GattService.UUID_SERVICE_BATTERY_SERVICE`, `UUID_SERVICE_DEVICE_INFORMATION`, `UUID_SERVICE_CURRENT_TIME` (verified present, lines 29/34/37) — don't redefine these. |
| 5 | Characteristic-notify wiring in `initializeDevice()` | Same `UnaDeviceSupport.kt`, pattern: `init { addSupportedService(UnaConstants.UUID_SERVICE_...) }` in the class body/constructor, then `builder.notify(characteristicUuid, true)` inside `initializeDevice()` | **Boilerplate**, directly copyable from `OlleeDeviceSupport`'s `init { addSupportedService(...) }` + `builder.notify(OlleeConstants.UUID_CHARACTERISTIC_TX, true)`. For standard-services reads (battery/firmware), the tutorial's `DeviceInfoProfile`/`BatteryInfoProfile` helper classes (`service/btle/profiles/deviceinfo/DeviceInfoProfile.java`, `service/btle/profiles/battery/BatteryInfoProfile.java`) still exist and are used by several current devices (e.g. `PolarH10DeviceSupport`, `YawellRingDeviceSupport`) — worth using these instead of hand-rolling battery/firmware reads. |
| 6 | Discovery/pairing bonding-style declaration | `getBondingStyle()` override, same coordinator file (item 2) | **We already have the answer**, high confidence: `BONDING_STYLE_BOND` given the confirmed "standard bonding, no extra secret" finding — this is exactly the kind of decision most new-device PRs have to guess at from scratch and we don't. |
| 7 | Discovery test | `app/src/test/java/nodomain/freeyourgadget/gadgetbridge/devices/BluetoothNameTest.java` — add one `put("UNA Watch 123456", DeviceType.UNA_WATCH);`-style entry | **Explicitly requested in real review** (§2.1) even though nothing mechanically forces it beyond `testMainDebugUnitTest` running in CI once added. Do this proactively, don't wait to be asked. |
| 8 | Display-name string resource | `app/src/main/res/values/strings.xml` — one `devicetype_una_watch` entry | Boilerplate; only nuance from real review (§2.4) is to match whatever exact spacing/casing you use in the discovery test and the advertised name, deliberately, since a reviewer may double-check this. |
| 9 | Website companion PR | Separate PR against `codeberg.org/Freeyourgadget/website` — `device_support.yml` entry, `docs/gadgets/wearables/una.md`, optional `docs/internals/specifics/una-protocol.md`, both new docs registered in `mkdocs.yml`'s `nav:` | **New plumbing, low effort.** We have unusually strong material to write the protocol doc from — the investigation doc is already most of a first draft. |

Not required for a minimal first PR (deliberately deferred, matching the Ollee/GloryFitPro precedent):
CHANGELOG.md edits (maintainers do this — don't touch it), a bespoke device icon, per-device settings
screens beyond what `getDeviceSpecificSettings`/`getDeviceSettings` needs for basic connect.

---

## 4. Where the tutorial docs have drifted from the current source (flagged explicitly, not papered over)

- **Coordinator base class**: `gadgetbridge.org/internals/development/new-gadget/` names
  `AbstractDeviceCoordinator` as the class to extend. The current source has *both*
  `devices/AbstractDeviceCoordinator.java` (still present, more generic) **and**
  `devices/AbstractBLEDeviceCoordinator.java` — and every BLE device support PR merged in the last month
  (Ollee, GloryFitPro) extends `AbstractBLEDeviceCoordinator`, not the plain one. Use the BLE-specific
  base class; the tutorial is not wrong exactly, just not current for a BLE device.
- **Per-device settings API is two generations deep in churn, and the version the recent examples use is
  itself already deprecated.** The tutorial describes `getSupportedDeviceSpecificSettings()` returning
  `int[]` of `R.xml.*` resources. The current `DeviceCoordinator` interface marks that
  **`@Deprecated`** in favor of `getDeviceSpecificSettings()` returning a `DeviceSpecificSettings`
  DSL object (what both Ollee and GloryFitPro actually use, merged July 2026) — but *that* method is
  **also now `@Deprecated`**, in favor of a brand-new `getDeviceSettings()` returning a
  `DeviceSettingsSpec`, built via `activities/devicesettings/dsl/DeviceSettingsDsl.kt`
  (`DeviceSettingsDslKt.deviceSettings` builder). This DSL was introduced/last touched 2026-07-18 (PR
  #6350, "Introduce DSL for device settings," merged 2026-06-30) and as of this research only 5 device
  packages use it (`sony/headphones`, `xiaomi_scooters`, `shokz`, `sinilink`, `aawireless`) — it's
  genuinely brand-new, newer than either of our template PRs. **Recommendation: for the minimal first PR
  (no meaningful settings screen needed yet), skip all three and rely on defaults; for the fast-follow,
  check with a maintainer or a very recent merge before picking which of the three settings APIs to
  target**, since "copy what Ollee did a week ago" would already copy a method the interface itself says
  not to use.
- **`onCharacteristicChanged` signature**: current signature is
  `onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray): Boolean`
  — the `value` is a parameter, not read off `characteristic.getValue()`. Older tutorial-era code and
  some older device implementations still call `characteristic.getValue()`; don't copy that pattern from
  an old device for reference.
- **greenDAO entities are generated, not committed.** `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/entities/`
  exists as a directory but contains no committed `.java` files for any device-specific sample type (verified: `OlleeActivitySample.java` doesn't exist in git even though the PR that "adds" `OlleeActivitySample` is merged) — `preBuild.dependsOn(":GBDaoGenerator:genSources")` in `app/build.gradle` regenerates them from `GBDaoGenerator.java` on every build. **Practical implication: never hand-write or commit an entity class; only ever edit `GBDaoGenerator.java` and let the build generate the rest.** Also: adding a new per-device entity did **not** require bumping the schema version constant (`new Schema(136, ...)` in `GBDaoGenerator.java`) in the Ollee PR — that number apparently only moves for actual breaking schema changes, not every new device table.

---

## 5. Activity-sync fast-follow — checklist

This maps the "real activity sync, not just battery/firmware" goal onto the greenDAO + provider pattern,
using Ollee's activity-sample plumbing as the structural template and flagging the FIT-reuse angle from §0.

| # | Item | Concrete GB location | Notes |
|---|---|---|---|
| 1 | New/generic sample entity | `GBDaoGenerator/src/nodomain/freeyourgadget/gadgetbridge/daogen/GBDaoGenerator.java` — either add `addUnaActivitySample(schema, user, device)` (pattern: copy `addOlleeActivitySample`, ~10 lines) **or, per real review feedback in §2.3, first check whether an existing `Generic*SampleProvider`/entity already covers the metric** (steps almost certainly still wants its own `AbstractActivitySample`-derived table per device, since `normalizeType`/`normalizeIntensity`/`toRawActivityKind` are inherently per-device semantics — but if UNA's `.fit` files also carry HR, SpO2, stress, etc., check `GenericSpo2Sample`, `GenericStressSample`, `GenericMetricSample`, `GenericTrainingLoadAcuteSample`/`ChronicSample` before inventing new tables for those.) | **Real, cited maintainer preference** — adding a bespoke table for a metric type GB already has a generic table for is a specific, named thing reviewers push back on (§2.3). |
| 2 | Sample provider | New `devices/una/UnaActivitySampleProvider.kt`, extends `devices.AbstractSampleProvider<UnaActivitySample>` | Directly modeled on `OlleeActivitySampleProvider.kt` (46 lines) — `normalizeType`, `toRawActivityKind`, `normalizeIntensity`, `createActivitySample`, `getSampleDao`, `getRawKindSampleProperty`, `getTimestampSampleProperty`, `getDeviceIdentifierSampleProperty`. |
| 3 | Coordinator wiring | `UnaDeviceCoordinator.getSampleProvider(device, session)` returns the provider above; `supportsActivityTracking(device) = true`; `supportsDataFetching(device) = true`; and — per the real review finding in §2.3 — **make sure every `supportsXxx()` flag matching data you actually populate is flipped to `true`**, or the data silently won't render even though it's correctly in the DB. | |
| 4 | File-transfer → FIT parsing | `UnaDeviceSupport` implements the FTS wire protocol from the investigation doc §2.2 (`0x50`/`0x51` directory listing, `0x10` first chunk + `0x12` subsequent chunks, `AcquireNotify`-equivalent on Android — note: Android's own BLE APIs don't expose BlueZ's `AcquireNotify`, that caveat from §6a is Linux/BlueZ-specific and **does not carry over to Android's `BluetoothGatt` notification callback path**, worth explicitly re-verifying it isn't an issue at all on Android rather than assuming), then feeds the reassembled bytes into `FitFile.parseIncoming(byte[])` per §0 instead of writing a bespoke FIT decoder. | **This is where "we already have FTS fully working" cashes out concretely** — the read protocol (opcodes, header layout, 32-bit fields, no size ceiling) is fully spec'd in our doc; the only genuinely new GB-side work is wiring the notify/write plumbing through `TransactionBuilder`/`AbstractBTLESingleDeviceSupport` and picking apart FIT record types into our sample provider. |
| 5 | Data fetching trigger | `onFetchRecordedData(dataTypes: Int)` override, still the current mechanism per both the tutorial and current `DeviceSupport` interface (unchanged, unlike the settings API) | Directory-list (`0x50`) then per-file read is a natural fit for this callback. |
| 6 | Completion signal | `GB.signalActivityDataFinish(device)` at the end of the fetch/drain | **Explicitly called out in real review** (§2.1) as an easy thing to forget; not caught by tests or the compiler. |
| 7 | `deleteDevice()` in coordinator | Remove UNA activity samples on device removal, per tutorial pattern (unchanged, no drift found here) | |
| 8 | Tests | Unit tests for the FTS chunk-reassembly logic and any UNA-specific framing at minimum (mirroring `OlleeProtocolTest.kt`'s scope) | Given §2.2's PR merged with **zero tests** for GloryFitPro DM58, tests are valuable but not a hard gate for a small fast-follow — invest proportionally to how much bespoke parsing logic (beyond the already-generic `FitFile`) you actually write. |

---

## 6. Pre-submission checklist — distilled from real maintainer feedback (with sources)

Everything below is something a Gadgetbridge maintainer or reviewer actually said in a real, cited PR
thread — not a generic software-review platitude:

1. **Add the advertised name to `BluetoothNameTest.java`.** (joserebelo, PR #6411)
2. **Never leave a parse/frame error silently swallowed — log it (slf4j `LOG.warn`, not a silent `return`/`catch`).** (joserebelo, PR #6411)
3. **Before hand-rolling chunked/fragmented writes, check `TransactionBuilder.writeChunkedData(characteristic, data, maxChunkSize)` — it probably already does what you're about to reimplement.** (joserebelo, PR #6411)
4. **Call `GB.signalActivityDataFinish(device)` at the end of an activity-data fetch/drain.** (joserebelo, PR #6411)
5. **Do not edit `CHANGELOG.md` in your PR — maintainers own that file.** (joserebelo, PR #6401)
6. **Before adding a new greenDAO sample entity for a common metric (SpO2, stress, generic training load, etc.), check whether `Generic<Metric>SampleProvider`/`Generic<Metric>Sample` already exists and reuse it.** (ThomasKuehne, PR #6403)
7. **Guard `dispose()`/teardown paths with proper synchronization if they touch shared connection-monitor state.** (ThomasKuehne, PR #6403)
8. **A populated DB column doesn't automatically show in the UI — the matching `supportsXxx()` coordinator override has to be flipped too.** (ThomasKuehne, PR #6403, re: `supportsActiveCalories`/`supportsActivityDistance`)
9. **Rebase + force-push to update a PR branch is fine and preferred — don't apologize for it.** (ThomasKuehne, PR #6403)
10. **Double-check that your human-readable display-name string and your discovery-test advertised-name string are deliberately consistent (spacing, casing) — reviewers do compare them.** (joserebelo, PR #6400)
11. **For a device-family package meant to host multiple future models, prefer one shared support/protocol class with thin per-model coordinators that only vary capability flags — not one support class per SKU.** (structural pattern, PRs #6403/#6457, TopStep/FitCloud family)
12. **Plan a companion PR to the `Freeyourgadget/website` repo** (`device_support.yml` + `docs/gadgets/wearables/<device>.md` [+ optional protocol doc], registered in `mkdocs.yml`) — normal and expected, evidenced by both #6411 and #6457's linked website PRs, not optional polish.

---

## 7. Architectural fit risks — flagged, not papered over

- **Chunking model**: UNA's FTS uses a stateful "open on `0x10`, continue on `0x12`" per-chunk
  request/response loop with the file implicitly "open" server-side between chunks (investigation doc
  §2.2). This is a bespoke, small state machine, not something Gadgetbridge's `TransactionBuilder`/
  `AbstractBTLEDeviceSupport` layer has a generic abstraction for (that layer is built for queueing
  discrete GATT operations, not a stateful open-file/streaming-read protocol) — expect to write this as
  ordinary application-level `DeviceSupport` code reacting to notifications, the same way Ollee's
  `OlleeFrameReassembler`/`handleFrame()` state machine works, not something GB gives you a shortcut for.
  Not a blocker, just don't expect a built-in "file transfer service" abstraction to exist to plug into.
- **Connection lifecycle / advertising window**: our own §6a findings document that the UNA watch's
  advertising window is short and connections have timed out repeatedly from a non-phone Linux central,
  and recommend "patient retry/reconnect logic." **This finding came from a Linux/BlueZ central and has
  not been separately validated against Android's own BLE stack** (which Gadgetbridge necessarily runs
  on) — Android's connection/retry behavior, GATT caching, and RPA-rotation handling are meaningfully
  different from BlueZ's. Don't assume the Linux-observed flakiness transfers 1:1 to Android; but also
  don't assume Android will be fine just because it's a different stack. This needs its own
  Android-specific validation pass (e.g. a throwaway `nRF Connect`-style manual test against the real
  watch from an Android phone) before or during the coordinator/support implementation — it's exactly
  the kind of thing that would surface as a frustrating, hard-to-diagnose review-round issue ("device
  won't reconnect reliably") if left unchecked until the PR is already up.
- **`FitFile.parseIncoming` reuse (see §0)** is a genuine opportunity but also a genuine judgment call —
  it means importing a class from `service.devices.garmin.fit` into a `service.devices.una` support
  class, which is unusual coupling between two unrelated device packages. Surface this explicitly in the
  PR description rather than silently doing it; the maintainers may prefer we vendor/duplicate the parser
  logic, or may be happy to have it shared (possibly worth a quick issue/discussion before or alongside
  the PR rather than finding out via review rejection).
- **The 0x30 secondary command and 0x20/0x21/0x22 upload family are explicitly still unexplained/
  unconfirmed** per the investigation doc §2.4/§2.2 — neither is required for the minimal first PR or the
  activity-sync fast-follow (both only need the read path), but don't scope EPO/AGPS push or the mystery
  `0x30` call into either PR; they're genuinely open research items, not just deferred features.

---

## 8. Open questions / things to verify before writing code

- Which of the three device-specific-settings APIs (`getSupportedDeviceSpecificSettings` /
  `getDeviceSpecificSettings` / `getDeviceSettings`) to target for anything beyond the MVP — see §4; worth
  asking in the PR description or an issue rather than guessing, since the two examples we have as
  templates already used the middle (now-also-deprecated) one.
- Handle `0x0027`'s exact UUID is still not independently confirmed against a *fresh* discovery response
  in the investigation doc (§2.4) — it's pinned down enough for a same-firmware companion that hardcodes
  the handle, which Gadgetbridge's `addSupportedService`/characteristic-lookup-by-UUID pattern does not do
  (GB looks characteristics up by UUID after service discovery, not by hardcoded handle) — so this
  actually needs to be resolved (or empirically re-derived from Android's own `BluetoothGatt.discoverServices()` result) before writing `UnaDeviceSupport`, it's not optional the way it was for the raw-handle Linux prototype.
- Real per-unit advertised-name suffix behavior (does `%06d` ever have leading zeros dropped, does the
  regex need to tolerate a space or punctuation variant) should be checked against more than one physical
  unit if possible, given §2.4's evidence that reviewers do check exact name-string handling.
- Whether AMS/ANCS/CANS notification support (present in firmware, not required for the first two PRs)
  is worth a third fast-follow PR once activity sync lands — out of scope for this scoping request, noted
  only so it isn't forgotten.
