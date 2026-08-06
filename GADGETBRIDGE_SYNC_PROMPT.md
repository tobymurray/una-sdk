# Prompt: Add activity-file sync to Gadgetbridge's UNA Watch support

You are an experienced Android/Kotlin engineer working on **Gadgetbridge**, a large, conservative, volunteer-run FOSS project. Your task is the follow-up to the merged UNA Watch MVP: pull recorded activity files off the watch over its custom File Transfer Service and import them, reusing Gadgetbridge's existing FIT stack rather than writing anything new.

Work incrementally and verify against the real codebase at every step. **Do not assume — read the code.** Almost every claim below carries a `file:line`; check them, because the tree moves.

---

## 1. Context: what already shipped

**Gadgetbridge PR #6504 merged 2026-08-01** (commit `8a532410`, merged by `joserebelo`, one review, two inline comments, both cosmetic). It added MVP support — connect, battery, firmware/hardware revision, time sync — in five files:

- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/una/UnaDeviceCoordinator.kt`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/una/UnaDeviceSupport.kt`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/model/DeviceType.java` (`UNA_WATCH`)
- `app/src/main/res/values/strings.xml` (`devicetype_una_watch`)
- `app/src/test/java/nodomain/freeyourgadget/gadgetbridge/devices/BluetoothNameTest.java`

**Website PR #270 merged** — `device_support.yml` entry `una_watch`, `docs/gadgets/wearables/una.md`, `mkdocs.yml` nav.

The watch matches `^UNA Watch \d+$`, uses `BONDING_STYLE_BOND` (standard BLE bonding, no app-layer secret), and reads Battery (0x180F) / Device Information (0x180A) / Current Time (0x1805) through the stock `BatteryInfoProfile` / `DeviceInfoProfile` helpers.

### Carry-over items agreed with the maintainer — fold into this PR

1. **Round icon.** joserebelo: *"I missed it in the screenshot, but ic_device_miwatch or ic_device_zetime would be round."* Toby agreed to roll it into the next PR. Use `R.drawable.ic_device_miwatch` (22 coordinators use it; `ic_device_zetime` has 4).
2. **Docs block placement.** In `docs/gadgets/wearables/una.md` the `--8<-- "blocks.md:non_released_gadget"` line sits above the `## UNA Watch {{ device("una_watch") }}` heading. Every other page puts it *below* — see `ollee.md:9-11`, `amazfit.md:65,74,144,200`. Move it when you update the docs for this PR.

---

## 2. The decisive insight: this repo is the spec

**You do not need reverse-engineering notes, and you must not use any.** The UNA SDK — this repo, public — documents exactly what the watch writes:

- **`Docs/FitFiles-Structure.md`** (1,744 lines) — the complete guide to the `.fit` files `ActivityWriter` produces: message ordering, developer fields, scaling, per-activity variations, validation.
- **`Libs/Header/SDK/Fit/`** — `FitWriter.hpp`, `FitProfile.hpp`, `FitBaseType.hpp`, `FitCrc.hpp`, `FitRecordCadence.hpp`, `RecordingMarker.hpp`.
- **`Libs/Source/Fit/`** — the implementations.
- **`Examples/Apps/Workout/`** — a real app doing FIT recording with crash recovery; the reference `ActivityWriter`.

`Libs/Header/SDK/Fit/FitProfile.hpp:57-61` is your identity block:

```cpp
enum class Manufacturer : uint16_t { Development = 255, Una = 351 };
// Product IDs are Una-scoped (not allocated by Garmin); assign our own.
enum class Product : uint16_t { UnaWatch = 1 };
constexpr char kProductName[] = "UNA Watch";
```

`FitProfile.hpp` also carries `MesgNum`, `Sport`, `SubSport`, `Event`, `EventType`, `File`, `Intensity`, `ActivityType`. That enumerates precisely which FIT messages Gadgetbridge will have to decode.

**Provenance rule, non-negotiable.** Gadgetbridge's `CONTRIBUTING.md` promises rejection *"without further explanations"* for anything derived from *"leaked, proprietary, decompiled"* sources. Describe every fact as sourced from the public UNA SDK (link it) or from your own BLE observation on hardware you own. Never write "disassembly", "decompiled", or "firmware teardown" in a commit message, PR body, or code comment. This bit the first PR and had to be rewritten.

---

## 3. Phase 0 — investigate before writing anything

### 3a. In this repo

1. Read `Docs/FitFiles-Structure.md` end to end. Produce a concrete inventory: **which FIT message types the watch emits, in what order, with which fields populated**, and which are optional/conditional.
2. Cross-read `Libs/Source/Fit/FitWriter.cpp` and `Libs/Header/SDK/Fit/FitProfile.hpp` against the doc — the code wins where they disagree, and note any disagreement.
3. Find how files are named and where they live on the watch filesystem (`Libs/Header/SDK/Interfaces/IFileSystem.hpp`, plus whatever `Examples/Apps/Workout/` does). Directory layout and filename convention drive the sync logic.
4. Determine whether developer fields are used, and if so what they contain — Gadgetbridge's importer handles them, but you need to know whether anything important is only in a developer field.

### 3b. In Gadgetbridge

Clone `https://codeberg.org/Freeyourgadget/Gadgetbridge.git`, base branch `master`. Read:

- `service/devices/igpsport/IGPSportDownloadManager.java` — **your primary template**, 302 lines, a non-Garmin device that downloads `.fit` files.
- `devices/igpsport/IGPSportAbstractCoordinator.java:57-66` — the coordinator wiring.
- `service/devices/garmin/fit/FitImporter.java`, `FitAsyncProcessor.java`, `FitFile.java`.
- `model/FitActivityTrackProvider.java` — generic, device-agnostic, despite living next to Garmin code.
- `service/devices/garmin/fit/enums/GarminSport.java`.

### 3c. What you must locate, and stop if you cannot

The FTS protocol details (service/characteristic UUIDs, directory-listing command, chunked read framing, completion signalling) and any captured `.fit` files are **not on this machine** under obvious names — a search of `~` for `*fts*`, `*una*protocol*`, `*gadgetbridge*`, `*.fit` found nothing. Ask Toby where the write-up and the phone-free Linux client live before starting the transfer layer. **Do not guess at UUIDs or framing.** Everything else in this prompt can proceed without them.

---

## 4. Established facts — do not re-derive these

Verified against `Gadgetbridge@a7dff08`:

- **The FIT stack is already device-agnostic and already reused by a non-Garmin device.** `IGPSportAbstractCoordinator.java:65` is literally `return new FitActivityTrackProvider();` and `:57-59` returns `new GarminWorkoutParser(context)`. `FitImporter` does not branch on manufacturer — verify this still holds for `Manufacturer::Una = 351`, but expect it to.
- **The whole import is one call.** `IGPSportDownloadManager.java:115-131`:
  ```java
  final FitAsyncProcessor fitAsyncProcessor = new FitAsyncProcessor(getContext(), support.getDevice());
  fitAsyncProcessor.process(filesToProcess, false, new FitAsyncProcessor.Callback() {
      @Override public void onProgress(final int i) { transferNotification.setTotalProgress(i); }
      @Override public void onFinish() {
          setLastSyncTimestamp(...);
          support.getDevice().unsetBusyTask();
          GB.signalActivityDataFinish(support.getDevice());
          transferNotification.finish();
          support.getDevice().sendDeviceUpdateIntent(getContext());
      }
  });
  ```
- **Squash already maps.** `GarminSport.java:160` is `SQUASH(64, 94, ActivityKind.SQUASH)` and `:157` is `RACKET(64, 0, ActivityKind.RACKET)` — the exact sport/sub_sport pair the SDK writes. No mapping code needed. Confirm the SDK's `Sport`/`SubSport` values all have `GarminSport` entries and report any that don't.
- **You can validate FIT files before writing a byte of transfer code.** Gadgetbridge ships `activities/fit/FitViewerActivity`, reachable from the in-app file manager (`activities/files/FileManagerAdapter.java:130`). Side-load a `.fit` produced by this SDK and open it there. **Do this first** — if the decoder chokes, the whole plan changes.
- **Sample providers are generated now** (`e2c375ea`, 2026-01) and `ActivityTrackProvider` exists (`58d6d670`, 2026-03). Do not hand-write a sample provider. Ollee's `OlleeActivitySampleProvider.kt` is *not* your model — it emits raw step samples, not FIT.
- **`supportsActivityTracks` was renamed `supportsRecordedActivities`** (`f732621a`).
- **A Kotlin settings DSL exists** (`6c2947ec`, 2026-06-28, by joserebelo). `getSupportedDeviceSpecificSettings` is `@Deprecated → use getDeviceSettings`. If this PR adds any setting, use `deviceSettings { }` — see `devices/sinilink/SinilinkCoordinator.kt`. Ollee merged a month after the DSL landed still using the old API; don't copy that.
- **`DeviceInfoProfile.toDeviceEvents(info)`** is already adopted in `UnaDeviceSupport`. Leave it.

### Open question, deliberately unresolved

`UnaDeviceSupport.useAutoConnect()` returns `true` with no comment. In Gadgetbridge this does **not** map to Android's `connectGatt(autoConnect=true)` — `BtLEQueue.java:368` says *"connectGatt with true doesn't really work ;("* and passes `false` on both paths. Its only effect for this device is `DeviceCommunicationService.java:512`, keeping broadcast receivers registered.

If the watch's post-bond advertising window really is short, the correct mechanism is coordinator-seeded prefs — `UltrahumanDeviceCoordinator.java:72-95`:

```java
// a low powered BLE gadget with gadget initiated connections
editor.putBoolean(PREF_CONNECTION_PRIORITY_LOW_POWER, true);
editor.putBoolean(GBPrefs.DEVICE_CONNECT_BACK, true);   // → BluetoothConnectReceiver.java:94
editor.putBoolean(GBPrefs.DEVICE_AUTO_RECONNECT, true);
```

**Gather evidence during real sync sessions before deciding.** If reconnect is reliable, drop to `false`. If not, add a `createDevice()` override. Either way, base it on observed behaviour, not on the reasoning in this file.

---

## 5. Implementation plan

**Phase 1 — validate.** Side-load an SDK-produced `.fit` into the merged Gadgetbridge build, open it in `FitViewerActivity`. Report exactly which messages decode and which don't. Stop and reassess if anything essential fails.

**Phase 2 — transfer.** `UnaDeviceSupport.onFetchRecordedData()` → a `UnaDownloadManager` mirroring `IGPSportDownloadManager`: list the FTS directory, filter to files newer than the last sync, pull each with chunked reads, write to Gadgetbridge storage. Use `device.setBusyTask()` / `unsetBusyTask()` and `GBProgressNotification` with `GB.NOTIFICATION_CHANNEL_ID_TRANSFER`. Persist a last-synced marker so re-syncs are incremental.

**Phase 3 — import.** Hand the file list to `FitAsyncProcessor.process(...)`; on finish call `GB.signalActivityDataFinish(device)`.

**Phase 4 — coordinator.** Four overrides in `UnaDeviceCoordinator.kt`, plus the icon fix:

```kotlin
override fun supportsDataFetching(device: GBDevice): Boolean = true
override fun supportsRecordedActivities(device: GBDevice): Boolean = true
override fun getActivityTrackProvider(device: GBDevice, context: Context): ActivityTrackProvider = FitActivityTrackProvider()
override fun getActivitySummaryParser(device: GBDevice, context: Context): ActivitySummaryParser = GarminWorkoutParser(context)
override fun getDefaultIconResource(): Int = R.drawable.ic_device_miwatch
```

Set `supportsActivityTracking` / `supportsSleepMeasurement` / `supportsActivityDistance` honestly — only what the watch actually records.

**Phase 5 — tests.** Unit-test whatever is pure: filename/timestamp parsing, incremental-sync filtering, any framing decode. Do not test the BLE layer. Ollee's `service/devices/ollee/*Test.kt` files show the expected granularity.

**Phase 6 — docs.** Update `docs/gadgets/wearables/una.md`: move "Activity sync" from Missing to Supported, fix the `--8<--` placement, and consider `feature_partial → feature_most` in `device_support.yml`. If you publish an FTS protocol page, add `docs/internals/specifics/una-protocol.md` plus a `&link_una_protocol` alias and a `links: protocol:` key — see Ollee's website PR #262. Separate PR to `Freeyourgadget/website`.

---

## 6. House rules

- **Separate branch, one reason to merge.** This is a feature PR against `master`; do not fold in unrelated fixes. Stack dependent branches rather than combining them.
- **Never post comments** on Codeberg PRs or issues — push and read only.
- **Commits** authored and committed as `Toby Murray <toby.murray@protonmail.com>`. Never mention Claude or AI tooling as an author or co-author.
- **Disclose AI assistance in the PR body**, as `CONTRIBUTING.md` requires. The MVP PR opened with: *"🤖 AI tooling was used in preparation of this PR (I'm still a human and I have the watch and tried it out)"*. That worked; reuse the shape.
- **Commit message style:** terse, mostly *why* not *what*. Amend pre-review; separate commit once reviewed.
- **Match the house idiom.** Before hand-rolling anything, grep for an existing helper — the recurring review note on new-device PRs is *"I think this is the same as `builder.writeChunkedData`?"* Logging matters too: joserebelo on Ollee, *"Should we log something? Silent failures are a pain to debug."* Use slf4j, never `e.printStackTrace()`.
- **Rebase, don't merge.** The PR template asks for `git rebase`, and no edits to translated `strings.xml` variants (Weblate owns those).

## 7. Definition of done

A rebased branch against `Gadgetbridge` master where: a bonded UNA Watch syncs recorded activities on demand; workouts appear in the workout list with correct sport labels (squash included) and a track where GPS exists; re-sync is incremental; `./gradlew lint` and the unit tests pass; the icon is round; docs are updated in a companion website PR; and the PR body states what was tested, on which Android version and phone, with AI use disclosed and provenance attributed to the public UNA SDK.
