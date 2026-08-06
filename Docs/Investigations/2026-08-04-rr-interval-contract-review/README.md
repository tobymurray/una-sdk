# RR_INTERVAL (0x44) contract review — experiments

**Date:** 2026-08-04
**Subject:** [UNAWatch/una-sdk#220](https://github.com/UNAWatch/una-sdk/pull/220),
branch `feat/rr-interval-contract`, head `6d58e239`, base `main`.
**Why experiments:** #220 freezes a wire contract. Arguments about a wire contract
are cheap; the questions worth settling are the ones a mutated header or a saturated
queue can answer. Three ran. Two produced findings that changed the review's
conclusion; one refuted a hypothesis I had written down as fact.

Everything below reproduces from a clean checkout. Findings are cited back from
`RR_INTERVAL_REVIEW.md` in the repo root.

---

## 0. Build environment

The macOS host has no `cmake`; host tests run in Docker (see
`Docs/unit-testing.md` and the `git archive` export recipe). `Tests/Host` is
C++17, `-Wall -Wextra -Wpedantic`, `CMAKE_BUILD_TYPE=Debug` in CI
(`.github/workflows/host-tests.yml`), so `assert()` is live.

```bash
S=/tmp/rr-review
git archive feat/rr-interval-contract | tar -x -C "$S/build-tree"
mkdir -p "$S/build-tree/ThirdParty/coreJSON"
cp -R ThirdParty/coreJSON/source "$S/build-tree/ThirdParty/coreJSON/"   # git archive skips submodules

docker build -t una-hosttests - <<'EOF'
FROM ubuntu:24.04
RUN apt-get update -qq && apt-get install -y -qq cmake g++ git ca-certificates python3
EOF

docker run --rm -v "$S/build-tree:/src" -w /src una-hosttests bash -c '
  cmake -S Tests/Host -B /src/_b -DCMAKE_BUILD_TYPE=Debug
  cmake --build /src/_b -j"$(nproc)"
  /src/_b/una-sdk-host-tests'
```

**Baseline (logs/host-tests-full.log, logs/host-tests-rr.log):** 224 tests from 24
suites, all green. `RrIntervalParser` contributes **14** cases (the PR description
says 13; the suite has 14). Also green under `-DCMAKE_BUILD_TYPE=Release`.

**Warning cleanliness:** the new header and test compile clean not only at the
project's `-Wall -Wextra -Wpedantic` but additionally under `-Wconversion
-Wsign-conversion -Wold-style-cast -Wdouble-promotion -Wshadow -Wuseless-cast`.
Nothing for #249's warning ratchet to catch.

---

## Experiment A/B/C/D — does the sensor path actually deliver every beat?

**File:** `RrDelivery_experiment.cpp` (drop into `Tests/Host/simulator/`)
**Log:** `logs/rr-delivery-experiment.log`

### Hypotheses

| | Hypothesis |
|---|---|
| A | A producer that stamps every interval of one `0x2A37` notification with the single arrival instant loses beats in the simulator **even though it registered `Mode::EVENT_BASED`** — i.e. the header's SIMULATOR CAVEAT (`SensorDataParserRrInterval.hpp:73-81`) is true of the code as it stands. |
| B | Back-dating each frame to the beat instant the interval ends on delivers every beat **with no simulator change**. |
| C | With back-dating, intervals shorter than half the driver's min period are still dropped, silently. |
| D | Frame order, the `u32` `SOURCE`/`FLAGS` metadata, and the identity `t[n] − t[n−1] == rr[n]` all survive the driver → queue → `DataBatch` → parser round trip. |

### Patch

A standalone gtest target linking the real simulator components — the same set the
`fix/simulator-sensor-deadlock` branch already links for its host target:

```cmake
find_package(Threads REQUIRED)
add_executable(rr-delivery-experiment
    simulator/RrDelivery_experiment.cpp
    support/SimSystemDouble.cpp                     # from fix/simulator-sensor-deadlock
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/Components/SensorDriver.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/Components/SensorManager.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/Components/SensorDataQueue.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/Components/SensorDataSample.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/Components/SampleRateAdapter.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/OS/OS.cpp")
target_include_directories(rr-delivery-experiment PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}" "${UNA_SDK_ROOT}/Libs/Header")
target_link_libraries(rr-delivery-experiment PRIVATE
    sdk_host_test_support GTest::gtest Threads::Threads)
```

One harness accommodation, unrelated to the contract: `feat/rr-interval-contract`
branches from `c90ec9e1`, which predates `3a807156` (*move HALSDL2 include from
Mock/System.hpp to System.cpp*). `SimSystemDouble.cpp` includes `Mock/System.hpp`,
so the experiment uses `main`'s copy of that one header. No sensor-layer or parser
source is modified.

The producer writes exactly what the contract mandates: stride
`RrInterval::getFieldsNumber()`, `Mode::EVENT_BASED`, `f[RR_MS]`, `u[SOURCE]`,
`u[FLAGS]`, one interval per frame. Beats are a real-looking series
`{812, 986, 1170, 903, 855, 971, 1004, 890, 940, 1010}` ms, `DISCONTINUITY` on the
first only.

### Log

```
[ RUN      ] RrDelivery.A_SharedArrivalTimestampDropsBeats
[A] pushed=10 delivered=1 callbacks=1
[  FAILED  ] RrDelivery.A_SharedArrivalTimestampDropsBeats

[ RUN      ] RrDelivery.B_BackdatedPerBeatTimestampsDeliverAll
[B] pushed=10 delivered=10 callbacks=10
[B]   beat 0 rr=812 src=2 disc=1 ts=91271
[B]   beat 1 rr=986 src=2 disc=0 ts=92257
[B]   beat 2 rr=1170 src=2 disc=0 ts=93427
[       OK ] RrDelivery.B_BackdatedPerBeatTimestampsDeliverAll

[ RUN      ] RrDelivery.C_BackdatedButFasterThanHalfMinPeriod
[C] pushed=10 delivered=5 callbacks=5
[  FAILED  ] RrDelivery.C_BackdatedButFasterThanHalfMinPeriod

[ RUN      ] RrDelivery.D_OrderAndMetadataSurviveTheWire
[       OK ] RrDelivery.D_OrderAndMetadataSurviveTheWire
```

(A and C are written to *fail* when a beat is lost; the failure is the result.)

### Verdicts

**A — CONFIRMED. 1 of 10.** `Simulator/Components/SensorDriver.cpp:79-92`
(`pushDataSample`) calls the rate-adapter-gated `DataQueue::pushData` for every
listener regardless of `mMode`; `SensorDataQueue.cpp:143-148` consults
`SampleRateAdapter::shouldEmit(ts)`, whose rule
(`SampleRateAdapter.cpp:58-77`) is `emit ⟺ updatePeriod ≥ P || updatePeriod +
samplePeriod > P`. Two frames carrying the same timestamp give `0 ≥ P || 0 > P` —
false. Beat 1 emits, beats 2..10 vanish. rryles measured 1-of-5 on the periodic
path against a 1000 ms listener; this is the same mechanism reached through
`EVENT_BASED`, which is supposed to bypass it.

The header's parenthetical is also confirmed: `Driver::connect`
(`SensorDriver.cpp:118-149`) forces `latency == period == sdcGetMinPeriod()` for
`EVENT_BASED`, so `DataQueue::computeCapacity` yields 1 and the queue *is*
single-slot. Capacity was never the problem, and B shows 10 pushes → 10 callbacks.

**B — CONFIRMED. 10 of 10, no simulator change.** This is the finding that matters.
The contract already *states* the right semantic — "the frame timestamp is the beat
instant the interval ends on" (`SensorDataParserRrInterval.hpp:64`) — and then
un-requires it seven lines later: "Distinct per-beat timestamps work as well but
are not required" (`:71`). Making that sentence normative makes the contract
truthful **and** fully exercisable in today's simulator, and costs a producer three
lines: for `k` intervals arriving at `T` in chronological order,
`t[i] = T − Σ_{j>i} rr[j]`.

**C — CONFIRMED, and previously undocumented. 5 of 10 at 15 ms spacing with
`minPeriod` 40 ms.** The emit rule reduces, for evenly spaced samples, to
`s > P/2`. So even a well-behaved back-dating producer has its sub-`P/2` intervals
silently decimated. Those are physiologically impossible intervals — which is
exactly the artefact the contract says the *consumer* must see and reject on its own
terms (`SensorDataParserRrInterval.hpp:188-194`). The simulator removes them first.
Worth one sentence in the simulator caveat, because it bounds what a replay mock can
be trusted to prove.

**D — CONFIRMED.** Beat order is preserved end to end (`DataQueue::forceData`
notifies exactly when `mIndex` wraps to 0, so the ring is always handed over
oldest-first; `SensorListener::onSdlNewData` memcpy's it contiguously). `u32`
`SOURCE`/`FLAGS` survive the writer → wire → `DataView::u` round trip. And the
identity `t[n] − t[n−1] ≈ rr[n]` holds to <1.5 ms under back-dating — which is the
only drop-detection mechanism available to a consumer, since nothing in the frame
carries a sequence number.

---

## Experiment E — does the test suite protect the frozen numbers?

**File:** `mutate.py` (run inside the container against the configured build dir)
**Log:** `logs/mutation-run.log`

### Hypothesis

A contract test suite that survives a mutated contract is not protecting the
contract. Twelve single-edit mutations of the frozen surface; each should kill at
least one of the 14 tests.

### Patch

```bash
docker run --rm -v "$S/build-tree:/src" -w /src una-hosttests python3 /src/mutate.py
```

Each mutation edits `SensorDataParserRrInterval.hpp` only, rebuilds
`una-sdk-host-tests`, runs `--gtest_filter=RrIntervalParser.*`, then restores the
original.

### Log

```
MUTATION                                                   VERDICT
M1  invert NO_SKIN_CONTACT                                 KILLED (10 tests)
M2  getFieldsNumber() returns 1 (the parse minimum)        KILLED (2 tests)
M3  swap SOURCE/FLAGS indices                              *** SURVIVED ***
M4  read rr_ms through .u instead of .f                    KILLED (10 tests)
M5  isDataValid() drops the fieldCount>=1 short-circuit    KILLED (abort: assert)
M6  isDataValid() drops the isfinite/positive guard        KILLED (2 tests)
M7  Source::ECG drifts 3 -> 4                              *** SURVIVED ***
M8  Source::EXTERNAL drifts 2 -> 5                         KILLED (compile error)
M9  swap DISCONTINUITY / ARTIFACT_SUSPECT bits             *** SURVIVED ***
M10 rr_ms reinterpreted as seconds (getBpm uses 60.0f)     KILLED (4 tests)
M11 getBpm() drops the infinity guard                      KILLED (2 tests)
M12 timestamps no longer gate on validity                  KILLED (2 tests)
```

### Verdicts

**Nine of twelve killed, and the kills are the right ones.** M8 is killed at
*compile* time by the three `static_assert`s at `:132-140` — the mechanism rryles
and the PR both claim, working. M1/M4/M6/M10/M11/M12 confirm the review fixes are
genuinely load-bearing rather than decorative. M2 confirms the stride decision is
pinned.

**M5 — hypothesis refuted, in the reviewer's favour.** I expected the zero-field
short-circuit in `isDataValid()` to be protected only by `assert(idx < fieldCount)`
in `DataView::FloatView::operator[]` (`SensorDataView.hpp:32`), and therefore only
in Debug. In Debug it does abort — but re-running the same mutation against a
`Release` (`NDEBUG`) build **also fails, cleanly**, via
`RrIntervalParser.ZeroFieldFrameIsInvalid`. The ordering is protected in both
configurations.

**M3, M7, M9 — SURVIVED, one root cause.** Every fixture in
`RrIntervalParser_test.cpp` writes through the same enum symbols the parser reads
back (`:80-81`, `:95-96`, `:138-139`, …). The suite is therefore invariant under
*any* permutation of the contract's numbers:

- M3 (`SOURCE=2, FLAGS=1`): both writer and reader move together. Even
  `UnknownSourceForOutOfRangeValue` still passes — for the wrong reason (with
  `SOURCE=2` and `fieldCount=2` the guard returns `UNKNOWN` before reading).
- M7 (`Source::ECG = 4`): the three `static_assert`s cover only the values shared
  with `HeartRateEx`. `ECG` is unpinned in both directions.
- M9 (bit0 ↔ bit1): the flag *values* are never asserted, only their symbols.

For ordinary code this would be a minor test-quality note. For a frame whose whole
purpose is to be read by a binary somebody else compiled, it is the defect: the
positions and values are the contract, and nothing pins them. See
`RR_INTERVAL_REVIEW.md` §2 (B2).

**Follow-up, `b2545f40`.** `RrIntervalParser.WireNumbersAreFrozen` and
`ParsesAFrameWrittenWithRawWireIndices` kill M3 and M9; a `static_assert` on
`Source::ECG == 3`, beside the three that lock the shared values to `HeartRateEx`,
kills M7 at compile time. Re-running `mutate.py` against `b2545f40`: **12 of 12
killed.** The `TIMESTAMP` rule landed separately in `6be71a49`, which makes
Experiment A's 1-of-10 a producer bug rather than a contract hazard.

---

## Refuted hypotheses (mine, and the review prompt's)

Recorded because they changed the conclusion, and because a review that only lists
its confirmations is not evidence.

1. **"`0x44` would be the first mixed-type frame in the SDK."** False.
   `GpsLocation` ( `[0]` float, `[1]` u32, `[2..4]` float), `GpsSpeed`
   (`[0]` float + two u32), `StepCounter`, `RunningCadence`, `WristMotion`,
   `Touch`, `ActivityRecognition`, `MotionDetect` and `FusionRaw` (i32) all mix, and
   six simulator sensors already write through `DataSample::u`. Mixed-type frames
   are established idiom, not a novelty, which materially strengthens the
   float→`u32` change rather than making it exotic.

2. **"`u32` may not be writable by the real (kernel) producer."** Effectively
   refuted. `GpsLocation::isDataValid()` requires `mData.u[COORDS_VALID] <= 1`
   (`SensorDataParserGpsLocation.hpp:60`); a kernel writing `1.0f` there would give
   `0x3F800000` and GPS would never validate on hardware. GPS demonstrably works in
   the shipped Running app, so the kernel writes real `u32` today.

3. **"An older kernel that doesn't know `0x44` may fail silently on subscribe."**
   False. `Connection::subscribe()` (`SensorConnection.cpp:133-149`) sends
   `RequestDefault`; the dispatcher answers `FAIL` when
   `Manager::getDefaultSensor(type)` finds no driver
   (`KernelMessageDispatcher.cpp:273-284`), `req.ok()` is false, `subscribe()`
   returns false and `connect()` returns false. Clean, checkable failure. No
   per-type table, min-period map or `switch (Type)` anywhere SDK-side needs to
   learn `0x44`, and no `KERNEL_INTERFACE_VERSION` interaction — the bump gate
   (`system.cpp:97`) is about `IKernel` layout, and adding an enumerator changes no
   struct.

4. **"The single-slot event queue can be overwritten before a busy consumer
   drains it."** Not in the simulator: `DataQueue::forceData` calls
   `onSdlNewData` *synchronously* on the wrapping push, so "drain" is the same
   call (Experiment B: 10 pushes → 10 callbacks). The real loss point is one layer
   down — `SensorListener::onSdlNewData` (`SensorListener.cpp:58-73`) drops the
   frame on pool-allocation or `sendMessage` failure with nothing but a
   `LOG_ERROR`, and no sequence number lets a consumer notice.

5. **"`feat/rr-interval-tooling` diverges from the contract."** No — it is
   stale-based. It sits on `af8bb1e6`, an earlier revision of the contract commit
   (`6d58e239` is not an ancestor), where `SOURCE`/`FLAGS` were still `float`
   (`af8bb1e6:…/SensorDataParserRrInterval.hpp:144,206`). The ~200-line "reduction"
   is the pre-review header, not a walk-back. **But** it has an important
   consequence: its producer still writes
   `sample.f[Parser::Field::SOURCE]` / `sample.f[Parser::Field::FLAGS]`
   (`SensorRrIntervalReplay.hpp:234-235`), which under the current `u32` contract
   yields `SOURCE = 0x40000000 → UNKNOWN` and `FLAGS = 0x3F800000 → no flags set` —
   silently, and indistinguishably from a producer that simply did not populate
   metadata. Fix before that branch lands; it is the first live demonstration of the
   mixed-type write hazard.

---

## Files

| File | What it is |
|---|---|
| `RrDelivery_experiment.cpp` | Experiments A–D. Also usable as-is as the regression test for a future simulator `EVENT_BASED` fix. |
| `mutate.py` | Experiment E. |
| `logs/host-tests-full.log` | 224/224 baseline. |
| `logs/host-tests-rr.log` | 14/14 `RrIntervalParser`. |
| `logs/rr-delivery-experiment.log` | A–D raw output. |
| `logs/mutation-run.log` | E raw output. |
