# Adversarial review — `RR_INTERVAL` (0x44) wire contract, PR #220

**Subject:** [UNAWatch/una-sdk#220](https://github.com/UNAWatch/una-sdk/pull/220)
`feat(sensor): add experimental RR_INTERVAL beat-to-beat pathway`
branch `feat/rr-interval-contract`, head `6d58e239`, base `main` (branched at `c90ec9e1`).

**Status.** B1 and B2 below are **applied** on `feat/rr-interval-contract` as of
`6be71a49` and `b2545f40` (not pushed). Everything else in this report still stands
against `6d58e239`. Post-fix: 226/226 host tests green in Debug and Release (16
`RrIntervalParser`, up from 14), and **all twelve** contract mutations now die — M3,
M7 and M9 included.

**Verification performed.** Host tests built and run in Docker on the PR head:
**224/224 green, of which 14 are `RrIntervalParser`** (the PR description says 13).
Green in `Debug` and `Release`. The new header and test are warning-clean under the
project's `-Wall -Wextra -Wpedantic` *and* under `-Wconversion -Wsign-conversion
-Wold-style-cast -Wdouble-promotion -Wshadow -Wuseless-cast`. Five experiments ran
against real simulator components; evidence bundle at
`Docs/Investigations/2026-08-04-rr-interval-contract-review/`. Nothing was posted to
GitHub.

---

## 1. Verdict

Yes, it holds its own, and yes it will last — because the surface this PR actually
freezes turns out to be very small (three field indices and their types, one unit,
three enum values, three bit positions), and I confirmed that everything else people
will want later — extra fields, extra `Source` values, extra flag bits — is
appendable with no ABI break in either direction. That property is the design's real
achievement and it is worth saying plainly: the "cheap now, expensive later" pressure
that dominated the prior review mostly does not apply, and the confidence field and
the contact tri-state can both wait. Two things should change before merge and both
are free: the timestamp semantic is stated and then un-required in the same paragraph,
and it is the one decision that genuinely cannot be appended around; and no test pins
a single numeric wire value, so three mutations of the frozen layout leave the suite
green. Fix those two and merge — the design, the reasoning and the review responses
are otherwise in better shape than most shipped sensor types in this SDK.

---

## 2. Blocking before merge

Two items. Both cost documentation and test lines, not design.

### B1 — The timestamp semantic is declared and then made optional, and it is the only decision here that cannot be fixed by appending later

**CONFIRMED** (code read + four experiments).

`Libs/Header/SDK/SensorLayer/DataParsers/SensorDataParserRrInterval.hpp:64` states
the semantic:

> The frame timestamp is the beat instant the interval ends on

and `:71`, seven lines later, withdraws it:

> Distinct per-beat timestamps work as well but are not required.

Under the second sentence a conforming producer may stamp all `k` intervals of one
`0x2A37` notification with the single arrival instant, which makes the first sentence
false. Three consequences, in ascending order of cost:

**(a) The contract becomes unexercisable in the only environment that exists.**
Experiment A: ten beats pushed through a real `Sensor::Driver` registered
`Mode::EVENT_BASED` with stride `getFieldsNumber()`, all sharing one arrival
timestamp → **1 of 10 delivered**. Experiment B: the identical producer, back-dating
each frame → **10 of 10 delivered, with no simulator change at all**.

The mechanism is exactly as the header's SIMULATOR CAVEAT (`:73-81`) says:
`Libs/Source/Simulator/Components/SensorDriver.cpp:79-92` calls the rate-adapter-gated
`DataQueue::pushData` for every listener regardless of `mMode`, and
`SampleRateAdapter::shouldEmit` (`SampleRateAdapter.cpp:58-77`) is
`emit ⟺ updatePeriod ≥ P || updatePeriod + samplePeriod > P`, which is false for
`0 , 0`. The header's parenthetical about queue capacity is also confirmed:
`SensorDriver.cpp:118-149` forces `latency == period == sdcGetMinPeriod()` for
`EVENT_BASED`, `computeCapacity` yields 1, and B shows 10 pushes → 10 callbacks.

**(b) Silent frame loss becomes undetectable.** Loss is real and reachable:
`Libs/Source/Simulator/Components/SensorListener.cpp:58-73` drops a frame on
pool-allocation or `sendMessage` failure with nothing but a `LOG_ERROR`; nothing in
the frame carries a sequence number; and upstream's in-flight `SLStatistic`
(`upstream/feature/sl-burst-sdk:Docs/Tutorials/Sensors/Software/Libs/Header/SLStatistic.hpp:17-18`)
counts *received* packages and samples — it cannot count what never arrived. A
dropped beat destroys RMSSD and DFA-α1 in a way the consumer cannot see, and
`DISCONTINUITY` cannot help because it is producer-set and knows nothing about
downstream loss.

Back-dating is the fix, because it makes the frame self-checking: a consumer verifies
`t[n] − t[n−1] ≈ rr[n]` and a gap of `rr[n] + rr[n−1]` is a dropped beat.
Experiment D asserts exactly this identity end to end and passes (<1.5 ms). Under
arrival-stamping the identity is meaningless and no check exists.

**(c) Beat instants are wrong for anything that needs them** — α1 window
construction, aligning HRV against GPS/power, overnight segmentation.

**Failure scenario, concrete.** Strap at 70 bpm, notification every 3 s carrying 3–4
RR values, producer stamps arrival. Simulator replay: 1 of every 3–4 beats survives,
so an HRV pipeline validated in the simulator computes RMSSD over a series with 70 %
of its beats missing and no indication that anything is wrong. On hardware, the same
producer delivers all beats (per rryles' account of the on-device event path), so the
pipeline's numbers change silently between simulator and device — which reads as the
contract failing when it isn't.

**Proposed change** (doc-only, no ABI change, no new field). Replace `:71` with a
normative producer rule, and state the consumer's entitlement:

> For the `k` intervals of one notification arriving at instant `T`, in the
> chronological order the wire delivers them, a producer MUST stamp
> `t[i] = T − Σ_{j>i} rr[j]` so that each frame's timestamp is the instant its
> interval ends on. If that would underflow (a notification arriving less than
> `Σ rr` after boot), clamp to 0 and set `DISCONTINUITY`.
> `rr_ms` remains authoritative for the interval *value*; timestamps are
> authoritative for *contiguity*. A consumer may therefore treat
> `t[n] − t[n−1] ≉ rr[n]` as a lost or reordered frame.

Note also that `mTimeStamp` is `uint32_t` milliseconds, so `getTimestampUs()`
(`SensorDataView.hpp:72-75`, correctly widened before multiplying) wraps at ~49.7 days
of uptime; one bogus `t[n] − t[n−1]` per wrap is the consumer-visible effect and is
worth the same sentence that covers the boot-time underflow.

If firmware cannot back-date, the alternative is a `[3] seq` `uint32_t` monotonic beat
counter, `0 = not reported`, producers starting at 1 and wrapping `0xFFFFFFFF → 1`.
It is strictly more robust, and — see §6 — it is *appendable later*, so it is not
itself a merge blocker. Making the timestamp rule normative is, because it is
semantic rather than structural and therefore cannot be added around: a consumer that
starts relying on back-dating breaks the moment a second producer stops, and vice
versa. ANT+ carries both a beat-event time and a beat count for precisely this reason.

---

### B2 — No test pins a single numeric wire value; three mutations of the frozen layout leave the suite green

**CONFIRMED** (mutation run, `logs/mutation-run.log`).

Twelve single-edit mutations of `SensorDataParserRrInterval.hpp`; nine were killed,
including the ones that matter for the review fixes (invert `NO_SKIN_CONTACT` → 10
tests; `getFieldsNumber()` → 1 → 2 tests; drop the `isfinite`/positive guard → 2;
read `rr_ms` through `.u` → 10; drop the `getBpm` infinity guard → 2; ungate the
timestamps → 2; `Source::EXTERNAL` drift → *compile* error, from the `static_assert`s
at `:132-140`). Three survived:

| Mutation | Result |
|---|---|
| `SOURCE = 2, FLAGS = 1` (swap the two indices) | **14/14 still green** |
| `Source::ECG = 4` | **14/14 still green** |
| `DISCONTINUITY = 1<<1, ARTIFACT_SUSPECT = 1<<0` (swap the bits) | **14/14 still green** |

One root cause: every fixture in `Tests/Host/sensorlayer/RrIntervalParser_test.cpp`
writes through the same enum symbols the parser reads back
(`:80-81`, `:95-96`, `:109-110`, `:137-139`, `:152-154`, `:169`). The suite is
therefore invariant under *any* permutation of the contract's numbers. It tests the
parser's internal consistency, which is not what a wire contract needs tested.
`UnknownSourceForOutOfRangeValue` (`:119-128`) still passes under the index swap for
the wrong reason — with `SOURCE = 2` and `fieldCount = 2` the guard returns `UNKNOWN`
before reading anything.

**Failure scenario, concrete.** A firmware author writes the producer against header
rev N. Later, someone reorders `Field` or renumbers `Source` — a rename, a tidy-up, a
merge resolution — and the app is rebuilt at rev N+1. Both compile. All 14 tests stay
green. On hardware `getSource()` returns `UNKNOWN` and every flag predicate returns
false. And because of the zero-means-not-reported invariant (`:44-48`), that is
*precisely indistinguishable* from a producer that registered the full stride and
simply didn't populate metadata. The invariant that makes an under-populating producer
safe is the same invariant that makes a misnumbered one silent.

This is not hypothetical. `feat/rr-interval-tooling`'s producer already writes
`sample.f[Parser::Field::SOURCE]` and `sample.f[Parser::Field::FLAGS]`
(`SensorRrIntervalReplay.hpp:234-235`) — correct against `af8bb1e6`, wrong against
`6d58e239`, compiles clean, and yields `SOURCE = 0x40000000 → UNKNOWN` and
`FLAGS = 0x3F800000 → no flags set`. The only producer that exists is already writing
the wrong member, silently. (That branch is stale-based, not divergent — see §5 — but
the failure mode it demonstrates is the contract's, not the branch's.)

**Proposed change** (~15 lines, in the file's own idiom):

```cpp
// beside the three existing static_asserts at :132-140
static_assert(static_cast<uint8_t>(Source::ECG) == 3,
              "RR_INTERVAL Source::ECG == 3 is a reserved wire value");
```

```cpp
TEST(RrIntervalParser, WireNumbersAreFrozen)
{
    // These numbers ARE the contract. A producer and a consumer compiled from
    // different revisions of this header must agree on them, and nothing else
    // in this suite would notice if they drifted.
    EXPECT_EQ(RrInterval::RR_MS,  0); EXPECT_EQ(RrInterval::SOURCE, 1);
    EXPECT_EQ(RrInterval::FLAGS,  2); EXPECT_EQ(RrInterval::COUNT,  3);
    EXPECT_EQ(static_cast<uint32_t>(RrInterval::Source::UNKNOWN),  0u);
    EXPECT_EQ(static_cast<uint32_t>(RrInterval::Source::OPTICAL),  1u);
    EXPECT_EQ(static_cast<uint32_t>(RrInterval::Source::EXTERNAL), 2u);
    EXPECT_EQ(static_cast<uint32_t>(RrInterval::Source::ECG),      3u);
    EXPECT_EQ(RrInterval::Flag::DISCONTINUITY,    1u << 0);
    EXPECT_EQ(RrInterval::Flag::ARTIFACT_SUSPECT, 1u << 1);
    EXPECT_EQ(RrInterval::Flag::NO_SKIN_CONTACT,  1u << 2);
}

TEST(RrIntervalParser, ParsesAFrameWrittenWithRawWireIndices)
{
    // Written the way a producer built from the documentation writes it —
    // literal offsets and literal values, no symbols shared with the reader.
    RrData m;
    m->mValue[0].f   = 855.f;
    m->mValue[1].u32 = 2u;   // EXTERNAL
    m->mValue[2].u32 = 5u;   // DISCONTINUITY | NO_SKIN_CONTACT

    RrInterval p(SDK::Sensor::DataView(*m.data(), 3));
    EXPECT_FLOAT_EQ(p.getRrMs(), 855.f);
    EXPECT_EQ(src(p.getSource()), src(RrInterval::Source::EXTERNAL));
    EXPECT_TRUE(p.hasDiscontinuity());
    EXPECT_FALSE(p.isArtifactSuspect());
    EXPECT_TRUE(p.isSkinContactLost());
}
```

That kills M3, M7 and M9. It is blocking by *consequence*, not by defect severity:
the PR's own thesis is "the decisions below are settled", and right now nothing
enforces a single one of the numbers.

The `static_assert` on `ECG == 3` pins the value against edits *here*; it cannot stop
the kernel HR arbiter from later assigning 3 to something else. That is a human
question (§8), not something this repo can settle.

---

## 3. Should fix in this PR

Real, but not contract-freezing.

**S1 — The PR body still carries the premise rryles asked to retire.** The PR body
says *"Per #167, the watch can't produce them as HR detection is frequency-domain and
the 20 Hz single-channel PPG is below the HRV floor."* The durable record is already
clean — commit `6d58e239`'s message never makes that claim, and the header opens on
"Opt-in companion to HEART_RATE / HEART_RATE_EX" with `Source::OPTICAL` a first-class
value. That is what rryles actually asked to protect ("before it becomes the quotable
justification in the commit history"), so this is a GitHub text edit, not a code
change. Suggested replacement for the *Why this exists?* paragraph:

> HRV and DFA-alpha1 — the basis of readiness, recovery and training-threshold
> metrics — are computed from beat-to-beat R-R intervals. No existing sensor type
> carries the intervals themselves: `HEART_RATE` and `HEART_RATE_EX` both carry a
> rate. Two independent producers are plausible — a BLE strap, which already reports
> the intervals in the same `0x2A37` notification as the BPM, and a wrist-optical
> path — so `Source` distinguishes them from the start rather than pretending one is
> hypothetical.

The same edit should drop the stale framing from
`docs/rr-interval-pr:RR_INTERVAL_PR.md:3-12`, which is where the PR body came from.

**S2 — The `0x2A37` energy-expended offset hazard exists nowhere in the repository.**
`git grep -i 'energy.expended\|2A37'` across every local and remote ref returns
**zero** hits outside the PR text. rryles' implementation note — that the kernel
decoder today knows only the 16-bit-value, contact-detected and contact-supported
bits, and that a producer must skip the optional 2-byte energy-expended field (flags
bit 3) which sits *before* the R-R array — is durable knowledge that currently lives
only in a comment on an open PR. One paragraph under `Docs/ExternalSensors.md:120`,
in a "Notes for a producer" subsection:

> The R-R array is the last field of the `0x2A37` notification and its offset is
> variable. Parse the flags byte first: bit 0 selects a uint8 or uint16 HR value,
> bit 3 inserts a 2-byte *energy expended* field, and only then does the R-R array
> (bit 4) begin, as uint16 values in units of 1/1024 s, oldest first. A decoder that
> assumes a fixed offset produces plausible garbage on any strap that reports energy
> expended.

**S3 — `Docs/SensorsLayer.md` sensor-type table stops at `0x42`.** Lines 23-25 list
`HEART_BEAT`/`HEART_RATE`/`HEART_RATE_METRICS`; the per-type sections at `:136` and
`:149` likewise stop there. `0x44` should get a row and a section here — it is this
PR's own type, so documenting it is inside this PR's one reason. The pre-existing
`0x43` gap is a separate reason and belongs on its own branch (§5). #176 and #131
show stale tables in this file get flagged.

**S4 — `Docs/ExternalSensors.md:99-107` states a fact that is not true.** It says
"Three things differ from every other sensor frame", and lists mixed field types among
them. Mixed-type frames are ordinary here: `GpsLocation` (`[0]` float, `[1]` u32,
`[2..4]` float), `GpsSpeed` (`[0]` float + two u32), `StepCounter`, `RunningCadence`,
`WristMotion`, `Touch`, `ActivityRecognition`, `MotionDetect`, and `FusionRaw` (i32).
Six simulator sensors already write through `DataSample::u`. The correct claim is the
narrower one already in the header (`:27-31`): the frame is not all-float *like
`HEART_RATE_EX`*, and each field's type is fixed by position. This matters beyond
pedantry — the true version is *reassuring* (the u32 choice is mainstream SDK idiom),
where the current version reads as a warning that `0x44` is exotic.

**S5 — The simulator caveat is duplicated, and it is the paragraph most likely to go
stale.** It appears at `SensorDataParserRrInterval.hpp:73-81` and again at
`Docs/ExternalSensors.md:117-120`. When the simulator gap is closed, both must change,
and the header is the copy nobody will remember. Recommend the header keep the
*contract* rationale (a producer author reads it in isolation — that duplication is
the right call) and reduce the simulator caveat to one line pointing at
`Docs/ExternalSensors.md`, which is where the volatile platform state belongs.

**S6 — The simulator drops sub-`minPeriod/2` intervals even with correct per-beat
timestamps, and this is not documented.** Experiment C: ten 15 ms intervals,
back-dated, `minPeriod` 40 ms → **5 of 10 delivered**. The emit rule reduces to
`s > P/2` for evenly spaced samples. Those are physiologically impossible intervals —
which is exactly what the contract says the *consumer* must see and reject on its own
terms (`:188-194`), and what `ARTIFACT_SUSPECT` exists for. The transport removes them
first. One sentence in the simulator caveat, because it bounds what a replay mock can
be trusted to prove.

---

## 4. Prior comments — disposition

| # | Item | Disposition | Evidence |
|---|---|---|---|
| 1 | `getFieldsNumber()` = stride, not parse minimum | **addressed** | `:277-280` returns `Field::COUNT` (3); `isDataValid()` stayed lenient at `>= 1` (`:171`); the asymmetry is documented in three places a producer reads — the header preamble `:50-53`, the accessor's own note `:270-275`, and `Docs/ExternalSensors.md`. rryles' premise verified: `HeartRate` returns 2, `HeartRateEx` 7, and **nine** simulator sensors pass `getFieldsNumber()` straight into the `Driver` ctor (`SensorPressure.cpp:28`, `SensorHeartRate.cpp:32`, `ImuStepCounter.cpp:27`, …). Mutation M2 kills it. |
| 2 | float vs `u32` for `SOURCE`/`FLAGS` | **addressed** | Both read via `mData.u[...]` (`:222`, `:288`); no float→int conversion remains anywhere in the parser, and the file is clean under `-Wconversion -Wsign-conversion`, so CodeRabbit's UB class is gone rather than guarded. The 2^24 argument is correct as written and pinned by `UndefinedFlagBitsDoNotDisturbKnownOnes` (`:163-176`). Additional support the PR does not claim: this is **not** the first mixed-type frame (see S4), so the choice is idiomatic. |
| 3 | `isDataValid()` accepting NaN/inf/0/negative | **addressed** | `:169-174`; `getBpm()` cannot return inf (`:207-208`, test `:226-239`); both timestamp accessors gate on validity (`:260`, `:265`); all five bad values covered (`:182-188`). Mutations M6, M11, M12 all killed. |
| 4 | Missing combined-flags test | **addressed** | `KnownFlagsCombine` (`:130-146`, `DISCONTINUITY \| ARTIFACT_SUSPECT`) and `AllFlagsSetAtOnce` (`:148-161`). Each predicate reads its own bit, not "mask non-zero" — mutation M1 (invert one bit) kills 10 tests, which is the proof. |
| 5 | Reserved-identifier include guard | **addressed, in-idiom** | `:86-87` `SDK_SENSORLAYER_DATAPARSERS_SENSOR_DATA_PARSER_RR_INTERVAL_HPP`. Matches the three newest SDK headers (`SDK_VARIANT_CONFIG_HPP`, `SDK_UTILS_CLOCKTIME_HPP`, `SDK_METRICS_RESETTABLE_MONOTONIC_COUNTER_HPP`); the repo still has 100 `__`-prefixed guards, so this is the direction, not the norm yet. |
| 6 | Graded confidence at `[3]` | **not addressed — correctly declined, but for a different reason than given** | The premise "cheap now, expensive later" does not hold: appending `[3]` later is ABI-safe in **both** directions. A consumer built at `COUNT = 3` against a stride-4 producer gets `fieldCount = 4` from `DataBatch::calcFieldCount` and every accessor still works (`isDataValid() >= 1`; `getSource()`/`flags()` guard on `fieldCount`); a `COUNT = 4` consumer against a stride-3 producer reads "not reported". The header states this at `:29-31` and it verifies. So: ship without it. **If** it is added, pin it as `u32` percent `1..100` with `0 = not reported` — never a `0..1` float, which makes "not reported" and "zero confidence" the same bit pattern and breaks the invariant at `:44-48`. |
| 7 | Stale "wrist is below the HRV floor" premise | **partially addressed** | Commit message `6d58e239` and the header are clean — which is what was actually asked for. The **PR body is still stale**. See S1 for replacement wording. |
| 8 | `0x2A37` energy-expended layout hazard | **not addressed** | Zero hits across all refs. See S2. |
| 9 | `RrData` storage lifetime (C++17) | **correctly declined for this branch** | `Tests/Host/sensorlayer/HeartRateExParser_test.cpp:20-27` uses a byte-identical `HrExData`. CodeRabbit is right by the letter of `[basic.life]` — `reinterpret_cast` over `alignas`'d bytes starts no object lifetime, and `std::launder` or placement-new is the remedy — and benign in practice for a standard-layout, trivially-copyable type with no non-trivial member. The one-reason rule makes "fix here only" wrong (it creates an inconsistency the next reader must explain) and "fix both" out of scope. Explicit ruling: **carry as-is on #220**, spin off a test-hygiene sweep (§5) that fixes both together. |
| 10 | `HeartRateEx::getSource()` narrowing spillover | **correctly excluded** | Local branch `fix/heart-rate-ex-source-narrowing` (`ae1a3dce`, *match HEART_RATE_EX source in float space*) exists; **not** an ancestor of `upstream/main` and has **no open PR**. Keep it off #220 and push it separately. |
| 11 | Simulator `EVENT_BASED` gap | **documented claim verified, and measured** | `pushDataSample()` (`SensorDriver.cpp:79-92`) does call the gated `pushData()` for every listener regardless of `Mode`; Experiment A measures 1-of-10. **Ruling:** shipping is acceptable **if and only if B1 lands**. With the timestamp rule made normative, the contract becomes fully exercisable in today's simulator (Experiment B: 10 of 10), so the simulator fix drops from prerequisite to cleanup. Without B1, #220 ships a contract whose only testable environment cannot honour its mandated delivery mode — and that is not a position to freeze a wire format from. B1 replaces the simulator fix as the blocker. |

---

## 5. Spin off separately

| Finding | Branch | Why it cannot ride on #220 |
|---|---|---|
| `HeartRateEx::getSource()` narrows an unvalidated float (`SensorDataParserHeartRateEx.hpp:93`) | `fix/heart-rate-ex-source-narrowing` — **already exists at `ae1a3dce`, unpushed** | Different sensor type, different reason to merge. Push it. |
| `Docs/SensorsLayer.md` has no `0x43` row or section | `docs/sensorslayer-heart-rate-ex` | `HEART_RATE_EX` predates #220; documenting it is a separate reason. Pairs naturally with S3 if you'd rather do both in one doc commit — that is also one reason ("document the cardio types the table is missing"), and then S3 moves here too. |
| Simulator `pushDataSample()` ignores `Mode::EVENT_BASED` | `fix/simulator-event-based-delivery` | A simulator behaviour fix, not a contract change. `Docs/Investigations/2026-08-04-rr-interval-contract-review/RrDelivery_experiment.cpp` is already a working regression test for it — Experiments A and C are the failing cases. |
| `SensorDataQueue.cpp:145` computes `d.mTimeStamp * 1000` in `uint32_t`, so the value handed to the rate adapter wraps at ~71.6 min of uptime — unlike `DataView::getTimestampUs()` (`SensorDataView.hpp:74`), which widens to `uint64_t` first | `fix/simulator-sra-timestamp-overflow` | Pre-existing, simulator-only, affects every sensor. `SampleRateAdapter::passed()` treats the wrap as a `uint64_t` wrap, so the effect is one spurious emit per wrap rather than a stall — small, but wrong, and it is the same class of arithmetic the RR path depends on. |
| `DataQueue::reinit()` (`SensorDataQueue.cpp:119-122`) replays the old ring from index 0 regardless of `mIndex` rotation, re-delivering stale frames and never-written zero slots | `fix/simulator-queue-reinit-order` | Pre-existing, simulator-only. Unreachable for `EVENT_BASED` (period/latency never change), so it does not affect `0x44` today. |
| `RrData` / `HrExData` fixture lifetime (CodeRabbit #9) | `test/sensorlayer-fixture-lifetime` | Fix both fixtures in one commit; see §4 item 9. |
| FIT `hrv` message support | `feat/fit-hrv-message` | `Libs/Header/SDK/Fit/FitProfile.hpp:28-38` has no `Hrv` mesg num and `FitWriter` has no array-field path. Substantial, and it needs a producer first to be worth writing. |
| `feat/rr-interval-tooling` writes `SOURCE`/`FLAGS` through the float writer | on that branch, before it opens | It is stale-based (sits on `af8bb1e6`, where those fields *were* float — `6d58e239` is not an ancestor), so this is a rebase task, not a defect report. But it must be fixed to `sample.u[...]` or the replay mock silently emits `UNKNOWN` + no flags. |

Not needed, checked and ruled out: no build-system enumeration must learn `0x44`. The
MSVS `<ClInclude>` parser lists (e.g.
`Docs/Tutorials/HelloWorld/…/Application.vcxproj:206-216`) are IDE convenience entries,
not compile inputs, and they are long stale — 11 entries against the SDK's 29 parsers,
one of which (`SensorDataParserGPS.hpp`) no longer exists, and with `HeartRateEx`,
`GpsLocation` and `GpsSpeed` all missing. `check_msvs_sources.py` (#223) compares
*source* lists between the Makefile and the vcxproj, not SDK headers, so nothing in CI
looks at these. Adding only `RR_INTERVAL` would make them less consistent, not more.

---

## 6. What's left to do overall

**Required before `0x44` can be called stable**

1. **A real producer.** Everything else is inference. The commit message already says
   this and it is the right framing.
2. **The `feat/rr-interval-tooling` rebase**, including the `.f` → `.u` fix above.
   That branch is the highest-value next step precisely because it is the first thing
   that writes the contract rather than reading it — and it already found the mixed-type
   write hazard for us.
3. **Simulator `EVENT_BASED` bypass** — *required only if B1 is not adopted*. With B1
   it becomes **later**, because back-dating already delivers 10 of 10 today.
4. **Confirmation that the kernel writer emits `u32`** — effectively already settled.
   `GpsLocation::isDataValid()` requires `mData.u[COORDS_VALID] <= 1`
   (`SensorDataParserGpsLocation.hpp:60`); a kernel writing `1.0f` would give
   `0x3F800000` and GPS would never validate. GPS works in the shipped Running app, so
   the kernel writes real `u32` fields today. This drops from "assumption the firmware
   author must confirm" to "confirmed by five shipping sensor types". **CONFIRMED by
   inference; a firmware author's yes would make it CONFIRMED outright.**

**Later**

5. Graded confidence at `[3]` (§4 item 6) — appendable, so it waits for a producer
   that actually has a confidence value to report.
6. `CONTACT_SUPPORTED = 1 << 3` for the contact tri-state (§7) — also appendable:
   `UndefinedFlagBitsDoNotDisturbKnownOnes` (`:163-176`) proves undefined bits do not
   disturb known ones, so new flag bits are free at any time.
7. FIT `hrv` plumbing.
8. `Docs/SensorsLayer.md` `0x43` row.
9. `HeartRateEx` narrowing fix (`ae1a3dce`) — push.

**The frozen surface, stated once, because it is short.** Field indices 0/1/2 and
their types; `rr_ms`'s unit; the values of `Source` 1/2/3; the bit positions of
`Flag` 0/1/2; and — currently under-specified, which is B1 — the timestamp semantic.
Everything else, including additional fields, additional `Source` values (`default:
UNKNOWN` at `:226`) and additional flag bits, is extensible without breaking a shipped
producer or a shipped consumer.

---

## 7. State-of-the-art assessment

Graded against what an HRV / training-readiness platform needs, not against the rest
of this SDK. The FIT comparison is read from the vendored SDK in-tree; the BLE and
vendor-SDK comparisons are external knowledge and are not verifiable from this
repository — treat them as `PLAUSIBLE`.

**Against BLE HRS `0x2A37`, the actual source format.** `0x44` preserves everything an
HRV consumer wants: the intervals themselves, losslessly (see below), and the contact
signal. What it loses is *energy expended* — not HRV-relevant, and correctly out of
scope — and the distinction between "contact not detected" and "contact detection not
supported", which the wire carries as two bits (detected, supported) and `0x44`
collapses into one. The collapse fails in the safe direction: `NO_SKIN_CONTACT = 0`
means "do not discard", which is the right default for unknown. And it is repairable
at any time by adding bit 3. **Better than the wire format** in three ways `0x2A37`
cannot express at all: per-interval provenance (`Source`), an explicit continuity
boundary (`DISCONTINUITY`), and a producer artefact hint (`ARTIFACT_SUSPECT`).

The unit argument holds. One tick is 125/128 ms exactly; `n × 125 ≤ 65535 × 125 =
8,191,875 < 2^24`, so every value a 16-bit tick field can produce (up to 63,999.02 ms)
is exactly representable in float32, and the producer-side conversion is lossless.
Downstream, float32's relative precision (~1e-7, an ULP of 61 ns at 1000 ms) is three
orders of magnitude below the source's own 1/1024 s quantization, so successive
differences and a log-log DFA regression over hundreds of beats are limited by the
measurement, not the wire type. Consumers will accumulate in `double` — that is an
implementation detail, not a wire question. `u32` microseconds or raw `u16` ticks would
carry no additional information. **Float milliseconds is the right and durable choice.**

**Against FIT.** `fit_hrv_mesg.hpp:26-79` (vendored at
`upstream/feature/sl-burst-sdk:ThirdParty/FitSDKRelease_21.171.00/`) defines exactly
one field: `time`, an array, units seconds, comment "Time between beats". A writer is
buildable from this contract with **no extra information** — divide `rr_ms` by 1000 and
accumulate into the array. `0x44` is strictly richer than FIT `hrv`: FIT carries no
source, no quality, no per-interval timestamp, and quantizes to 1 ms. (`hrv_value` and
`hrv_status_summary` are the derived nightly-status messages, a different concern.)
Nothing is missing at the seam; the work is `FitWriter` array-field support, which the
current writer does not have.

**Against what DFA-α1 and RMSSD pipelines actually require.** They need raw,
*uncorrected* intervals (α1's sensitivity to the correction method is a published
result, so the pipeline must own the policy), artefact identification, gap boundaries,
and windows of a couple of minutes. `0x44` supplies all four inputs: raw intervals it
promises not to filter (`:188-194`), a producer hint, an explicit discontinuity flag,
and — under B1 — true beat instants for windowing. Three things it cannot express,
each nameable and each appendable: a **graded** artefact likelihood (only 1 bit today),
a per-interval **error estimate** in milliseconds, and a raw-versus-already-corrected
distinction, which matters the moment a producer does any internal blocking.

**Against comparable device SDKs.** Polar's PPI stream is the closest analogue and the
sharpest comparison: it carries `ppi`, an `errorEstimate` in ms, a blocker bit, and
`skinContactStatus` *and* `skinContactSupported` separately. Its `errorEstimate` is the
one field genuinely worth stealing — graded quality expressed in the units of the
measurement, which a consumer can threshold however it likes. ANT+'s HRM pages carry a
beat-event time in 1/1024 s **plus a beat count** — the sequence number B1 discusses,
and the reason B1's alternative is not exotic. Wear OS Health Services attaches a
graded accuracy (no-contact / unreliable / low / medium / high) to each heart-rate
sample rather than a boolean. Movesense exposes RR arrays with no per-interval quality
at all — `0x44` is ahead of that.

**Straight verdict: it holds its own, and it will last.** Three decisions earn that.
(1) One interval per frame, argued from `Data` having no in-band length field rather
than from convenience — it is the only honest shape and it makes the stride
deterministic. (2) The lenient upper field bound combined with per-field `fieldCount`
guards and `default: UNKNOWN`, which is what makes the contract genuinely extensible
and therefore keeps the frozen surface to five items. (3) Refusing to range-check
physiology, which is correct for HRV specifically and is the difference between a
contract an HRV pipeline can build on and one it has to work around. The minimum change
that keeps it durable is B1 — one paragraph — and the minimum change that keeps it
*enforced* is B2 — one static_assert and two tests.

---

## 8. Questions only a human can answer

1. **Can the kernel HR arbiter reserve `HrSource == 3` for ECG?** `Source::ECG = 3` is
   by convention only; the three `static_assert`s at `:132-140` cover just the values
   shared with `HeartRateEx`, and mutation M7 confirms nothing catches `ECG` drifting.
   If the arbiter later takes 3 for something else, the same wire value means two
   things across two frames. Blocking for stability, not for merge.
2. **Will the producer back-date per-beat timestamps (B1)?** If firmware can compute
   `t[i] = T − Σ_{j>i} rr[j]` — it has the arrival instant and the array is
   chronological — the contract needs no new field. If it cannot, decide on the `[3]
   seq` counter, which is appendable but is better decided before two producers exist.
3. **Is `Source::OPTICAL` a per-beat interval producer, or a pre-computed HRV
   producer?** #167 mentions "getting HRV to be calculated by the PPG chip itself". A
   chip emitting RMSSD directly does not belong on `0x44` at all — it is a different
   type (an `HRV_METRICS 0x45`), and `0x44` should not be stretched to hold it. Worth
   confirming before the optical path is designed against `0x44`.
4. **Does any currently-supported strap set `0x2A37` flags bit 3 (energy expended)?**
   Determines whether S2's producer note is a correctness requirement or a
   future-proofing note.
5. **On device, does the event-based dispatch have any drop path a consumer cannot
   see?** rryles states it goes straight into a single-slot per-listener queue with no
   rate adapter. If that queue can be overwritten before the app drains it, or if the
   IPC hand-off can fail the way `SensorListener.cpp:58-73` fails in the simulator,
   then B1's back-dating drop-check is not merely useful — it is the only thing
   standing between a lost beat and a wrong RMSSD.
