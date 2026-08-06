# Prompt: Adversarially review the RR_INTERVAL contract (PR #220)

You are a hostile-but-fair reviewer of a **wire/ABI contract**, not of ordinary code. Your subject is
[UNAWatch/una-sdk#220](https://github.com/UNAWatch/una-sdk/pull/220) — `feat(sensor): add experimental
RR_INTERVAL beat-to-beat pathway`, branch `feat/rr-interval-contract`, base `main`, head `6d58e239`.

The bar is not "does this compile and pass tests". The bar is: **once a firmware producer ships against
`0x44`, every decision in this PR is frozen.** Changing a field's type, index, unit, polarity, or
delivery mode after that point is a silent breaking change to a binary contract consumed by
independently-built `.uapp` binaries. So the only question that matters is:

> Is there anything in this contract that a producer, a second producer, or an HRV/DFA-α1 consumer will
> force us to change later — and can it be fixed now, while it is still free?

Answer that, and answer the user's two direct questions: **(a) is anything left to do, and (b) does this
hold its own as a state-of-the-art interface that will last and meet real needs?**

---

## 0. Ground rules

- **Never post anything to GitHub.** No PR comments, no review submissions, no `@coderabbitai` triggers,
  no issue comments. Read-only via `gh api` / `gh pr view` / `gh pr diff`. Your deliverable is a local
  markdown report. This is a hard constraint, not a preference.
- **Verify, don't trust.** Every claim in this prompt is a *hypothesis to confirm or refute against the
  code*, including the ones stated confidently. Several may be wrong. Cite `file:line` for everything you
  assert. Label each finding `CONFIRMED` (you traced the code path) or `PLAUSIBLE` (reasoned, not proven)
  and say what would settle a `PLAUSIBLE` one.
- **A resolved comment is not a resolved concern.** For each prior review point, check whether the *fix*
  addressed the *reason the reviewer raised it*, or only the symptom they happened to name.
- **Scope discipline.** This repo's convention is one reason to merge per branch. Anything you find that
  is real but not RR-contract scope goes in a separate "spin off a branch/PR" list, not a demand on #220.
- Read `CLAUDE.md` and `Docs/` before judging idiom. Do not propose patterns the SDK does not use.

---

## 1. What is actually in the PR

Five files (`gh pr diff 220 --repo UNAWatch/una-sdk`):

| File | Substance |
|---|---|
| `Libs/Header/SDK/SensorLayer/SensorTypes.hpp` | `RR_INTERVAL = 0x00000044` |
| `Libs/Header/SDK/SensorLayer/DataParsers/SensorDataParserRrInterval.hpp` | the whole contract: field layout, `Source`, `Flag`, validity, `getBpm()`, ~100 lines of rationale header comment |
| `Tests/Host/sensorlayer/RrIntervalParser_test.cpp` | 13 host tests |
| `Tests/Host/CMakeLists.txt` | registers the test |
| `Docs/ExternalSensors.md` | consumer-facing section |

The contract as it stands:

- `[0] rr_ms` **float**, required, milliseconds, converted from the BLE 1/1024 s tick producer-side.
- `[1] source` **u32** — `UNKNOWN=0, OPTICAL=1, EXTERNAL=2, ECG=3`, first three `static_assert`ed against
  `HeartRateEx::Source`.
- `[2] flags` **u32** — `DISCONTINUITY=1<<0`, `ARTIFACT_SUSPECT=1<<1`, `NO_SKIN_CONTACT=1<<2`.
- `getFieldsNumber() == 3` = the **delivery stride** a producer registers with its `Driver`.
- `isDataValid()` = `fieldCount >= 1 && isfinite(rr_ms) && rr_ms > 0`; timestamps gate on it too.
- One interval per frame; producers **must** register `Driver::Mode::EVENT_BASED`.
- Invariant: zero means "not reported" for every field but `[0]`, resting on `DataSample` zero-fill.
- No physiological plausibility filtering — deliberately the consumer's job.

Context you need: `Libs/Header/SDK/SensorLayer/SensorData.hpp` (`Data` is a `{float,u32,i32}` union with a
trailing `mValue[1]`, `sizeof == 12`), `SensorDataView.hpp` (`.f` / `.u` / `.i` sub-views, `assert(idx <
fieldCount)`), `Libs/Source/Simulator/Components/SensorDataSample.cpp` (writer side —
`FloatWriter`/`U32Writer`/`I32Writer`), `Libs/Source/Simulator/Components/SensorDriver.cpp`
(`pushDataSample()`), and `SensorDataParserHeartRateEx.hpp` + its host test as the sibling precedent.

---

## 2. Repository intelligence to mine first

This repo carries far more evidence than `main`'s working tree shows. **Do not review #220 from the diff
alone** — the prior exploration and the commit history are the record of what has already been learned,
argued, and settled, and much of it bears directly on the questions below. Survey this before forming any
opinion.

### The lineage of this specific question

- **`feat/beat-event-probe`** (`72a10777` + fixes, = PR #167) — `Utilities/BeatProbe/README.md` and
  `BeatProbe.hpp`: a **runnable diagnostic** built to answer "does `HEART_BEAT` (0x40) emit anything, and
  is a PPG waveform available?". This is the empirical parent of #220. Read the probe *and* its README:
  what it measured, what it could not measure, and what it therefore left assumed. Every assumption
  #220's contract inherits from it is a place the contract could be wrong.
- **`feat/rr-interval-tooling`** (`eaaf0c1b`, *RR_INTERVAL replay mock, sample fixture, and Sensors
  consumer*) — the planned follow-up already exists locally: `SensorRrIntervalReplay.hpp` (~280 lines),
  an `InstanceSensorLayer` registration, and a real sample fixture. It is **stale against `main`** and its
  diff shows it also *reduces* `SensorDataParserRrInterval.hpp` by ~200 lines and cuts
  `RrIntervalParser_test.cpp` by ~160 — figure out whether that is genuine divergence or just an old base,
  because **a producer written against the contract is exactly the evidence rryles said would "find what a
  contract missed"**. If that branch already discovered something, it is the highest-value input to this
  review. Diff the two versions of the parser header and explain every difference.
- **`docs/rr-interval-pr`** (4 commits, e.g. *reframe RR_INTERVAL PR description*, *surface the decimation
  caveat, tighten claims*) — the framing history of the PR body itself. Relevant to §3 item 7: check
  whether the stale premise was already deliberately edited out somewhere and just not pushed.
- **`fix/heart-rate-ex-source-narrowing`** (`ae1a3dce`) — the "not yours, but noted" spillover, already
  branched.
- **`Docs/Tutorials/Sensors/`** and `upstream/feature/sl-burst-sdk`'s heavily-extended `Sensors` app
  (`SLStatistic.hpp/.cpp`, `Commands.hpp`, per-sensor verbosity, ~760 changed lines in `Service.cpp`) —
  UNA's *own* in-flight direction for the sensor layer, including sensor-layer statistics. Check whether
  it counts dropped/late samples: if upstream is already building drop accounting, that is directly
  relevant to §5 item 1, and #220 should compose with it rather than invent a parallel mechanism.

### Authorities already vendored in-tree

- **The Garmin FIT SDK is vendored on upstream branches** (`upstream/feature/sl-burst-sdk`,
  `upstream/claude/lvgl-migration`): `ThirdParty/FitSDKRelease_21.171.00/cpp/fit_hrv_mesg.hpp`,
  `fit_hrv_value_mesg.hpp`, `fit_hrv_status_summary_mesg.hpp`. Read the real field definitions from those
  headers rather than citing FIT from memory — it makes the §6 FIT benchmark verifiable. Cross-check
  against this repo's own `Libs/.../Fit/`, `Docs/FitFiles-Structure.md`, `Tests/Host/fit/`, and the
  precedent set by #229 (Garmin-allocated manufacturer ID) and `feat/fit-profile-racket-squash` for how
  FIT-profile values get extended here.

### The investigation convention, and the standard you are being held to

- `Docs/Investigations/<date>-<slug>/` is an established pattern on several branches
  (`origin/docs/una-watch-hardware-recovery`, `origin/docs/hardware-config-recovery`,
  `origin/docs/una-ble-companion-re-prompt`, `origin/docs/una-seam-hunt-re-prompt`, and the
  `2026-05-16-touchgfx-drawpartialbitmap-negative-x` / RawTilesMap cell-render bundles on `experiments`).
  Look at one of the RawTilesMap experiment bundles — hypothesis, patch, log, screenshot, verdict, per
  experiment (A, B, C, C2), including the experiments that **failed**. That is the evidentiary bar in this
  repo: claims come with a reproduction. Hold your own findings to it, and consider whether any of your
  contract questions are settleable by an experiment (a mutated stride, a saturated queue, a
  shared-timestamp burst through the simulator) rather than by argument.
- `Docs/companion-data-channel-analysis.md` (`origin/docs/companion-data-channel-analysis`) and the
  BLE-companion protocol work under `Docs/Investigations/2026-07-29-hardware-config-recovery/` —
  independently reverse-engineered knowledge of the watch's BLE/companion behaviour. Check it for anything
  that constrains how strap data or per-beat data actually moves, and for anything that contradicts #220's
  assumptions about the `0x2A37` path.

### The git history is the design record

Commit messages in this repo are long-form and carry the *why* (see #220's own commit body, or
`c3f2cfd3 harden(sdk): clamp toHMS to zero on negative input`). Mine it rather than only reading diffs:

```bash
git log --all --format='%h %s' -- Libs/Header/SDK/SensorLayer/                 # sensor-layer evolution
git log --all -p --follow -- Libs/Header/SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp
git log --all --format='%h %s%n%b' --grep -iE 'sensor|HRV|heart|arbiter|EVENT_BASED|stride|field'
git log --all --format='%h %s' -- Libs/Source/Simulator/Components/SensorDriver.cpp
```

Specifically establish, from history rather than assumption: how `HEART_RATE_EX`'s 7-field all-float
layout came to be and whether its choices were regretted; why `DataView` grew `.f`/`.u`/`.i` sub-views and
whether any existing frame uses `.u` (if `0x44` would be the **first** mixed-type frame in the SDK, that is
a significant fact for §5 item 6 and should be stated plainly in the report); what #228's lost-subscribe
fix actually changed about the subscribe path; and whether `EVENT_BASED` has ever been exercised
end-to-end by any shipped sensor.

Where an exploration doc or a commit message already answers one of the questions in §4–§6, **cite it and
move on** — do not re-derive it. Where it *contradicts* #220, that is a finding.

---

## 3. Phase 1 — every existing comment, re-litigated

Pull the full comment set yourself:

```bash
gh api repos/UNAWatch/una-sdk/issues/220/comments --paginate --jq '.[] | "=== \(.user.login) ===\n\(.body)\n"'
gh api repos/UNAWatch/una-sdk/pulls/220/comments  --paginate --jq '.[] | "=== \(.user.login) | \(.path):\(.line // .original_line) ===\n\(.body)\n"'
```

There are two sources: three CodeRabbit findings, and one long human review from **rryles** (2026-07-31),
which also answered the PR's three open questions. The head commit postdates that review, so most of it
should be addressed — your job is to prove it, item by item, and to find what was quietly dropped.

**Claimed-addressed — confirm the fix is real and complete:**

1. `getFieldsNumber()` returning the parse minimum instead of the stride. rryles' argument was that
   producers pass it straight into the `Driver` ctor, so a 1-field registration makes `SOURCE`/`FLAGS`
   permanently undeliverable. Confirm `== 3` now, confirm `isDataValid()` stayed lenient at `>= 1`, and
   confirm the *asymmetry is documented where a producer will actually read it*. Cross-check the claim
   against `HeartRate` (2), `HeartRateEx` (7), and the simulator sensors that pass it through.
2. float-vs-`u32` for `SOURCE`/`FLAGS`. Confirm both are read via `mData.u[...]`, that no
   float→int narrowing remains anywhere in the parser (CodeRabbit's UB finding), and that the
   `2^24` sparse-bit argument for the bitmask is actually correct as written.
3. `isDataValid()` accepting NaN/inf/0/negative `rr_ms`. Confirm the guard, confirm `getBpm()` cannot
   return inf, confirm the timestamp accessors gate on validity, and confirm tests cover all five bad
   values.
4. Missing combined-flags test. Confirm a real one exists (`DISCONTINUITY | ARTIFACT_SUSPECT`), and that
   each predicate reads *its own bit* rather than "mask non-zero".
5. Reserved-identifier include guard (`__SENSOR_...`). Confirm the new form, and confirm it matches the
   direction rryles said the repo is moving in.

**Explicitly raised and, as far as I can tell, NOT addressed — verify and rule on each:**

6. **A graded confidence field at `[3]`.** rryles: an optical producer can supply per-beat confidence,
   projecting it onto 1-bit `ARTIFACT_SUSPECT` discards information that matters because DFA-α1 is
   notoriously artifact-sensitive, and consumers will want their own threshold. "Cheap now, expensive
   later." It is absent from the head commit. Decide: is this a **blocking** addition, or is
   `ARTIFACT_SUSPECT` plus consumer-side filtering genuinely sufficient? If you add it, pin its type,
   range, and the meaning of `0` **under the zero-means-not-reported invariant** — note that a naive
   `[3] confidence: 0..1 float` makes "not reported" indistinguishable from "zero confidence", which is
   exactly the trap the invariant exists to avoid. Say how you resolve that.
7. **The stale premise in the PR description.** rryles asked specifically that "the wrist sensor is below
   the HRV floor" not become the quotable justification in the commit history: a wrist-optical per-beat
   path is now being evaluated, which makes `Source::OPTICAL` a real near-term value, not a placeholder.
   Check the PR body, the commit message, the header comment, and `Docs/ExternalSensors.md` for any text
   that still frames this as strap-only or watch-can't. Propose exact replacement wording.
8. **The producer-side `0x2A37` layout hazard.** rryles' implementation note: the kernel decoder today
   knows only the 16-bit-value, contact-detected and contact-supported bits, and a producer must also
   account for the optional 2-byte **energy-expended** field (flags bit 3), which sits *before* the R-R
   array — get that wrong and every offset is wrong on straps that report it. Is that durably captured
   anywhere a firmware author will find it, or does it live only in a PR comment that will be forgotten?
9. **CodeRabbit's `RrData` storage-lifetime finding** (`RrIntervalParser_test.cpp:21-25`): under C++17,
   `reinterpret_cast`ing a `uint8_t` buffer to `SDK::Sensor::Data*` without placement-new starts no
   object lifetime. Unlike the others this one carries no "✅ Addressed" marker. Before ruling, note that
   `Tests/Host/sensorlayer/HeartRateExParser_test.cpp` uses the *identical* `HrExData` idiom — so this is
   pre-existing precedent, and "fix it here only" creates an inconsistency while "fix both" exceeds this
   branch's one-reason rule. Rule on it explicitly with that trade-off named; do not silently ignore it.
10. **The `HeartRateEx::getSource()` narrowing spillover** rryles filed as "not yours, but noted". A local
    branch `fix/heart-rate-ex-source-narrowing` (`ae1a3dce`, *match HEART_RATE_EX source in float space*)
    appears to exist already. Confirm whether it is upstreamed, and keep it out of #220 either way.
11. **The simulator `EVENT_BASED` gap.** `Simulator/Components/SensorDriver.cpp::pushDataSample()` calls
    the rate-adapter-gated `pushData()` for every listener regardless of `Mode`, so a replay mock will
    drop beats in the simulator while the same producer works on hardware. #220 *documents* this in both
    the header and the docs. rryles said it is a simulator gap worth fixing "as part of, or just before,
    the follow-up PR". Verify the documented claim is still true of the code, and decide whether shipping
    a contract whose only testable environment cannot honour its mandated delivery mode is acceptable —
    or whether the simulator fix must land first, since it is the thing that would let the contract be
    *validated* rather than merely asserted.

---

## 4. Phase 2 — what this repo's reviewers have historically caught

Mine the actual history rather than guessing; then apply each pattern to #220 as a concrete check.

```bash
for pr in 169 170 176 175 166 131 130 228 236 231 245 247 249; do
  gh api repos/UNAWatch/una-sdk/pulls/$pr/comments --paginate \
    --jq '.[] | "=== '"$pr"' \(.user.login) | \(.path) ===\n\(.body)\n"'
  gh api repos/UNAWatch/una-sdk/issues/$pr/comments --paginate \
    --jq '.[] | select(.user.login != "coderabbitai[bot]") | "=== '"$pr"' \(.user.login) ===\n\(.body)\n"'
done
```

Patterns already visible, each of which implies a check here:

- **#167 → #220 is a direct lineage.** rryles' answer on #167 established: no `HEART_BEAT` events at all,
  20 Hz single-channel PPG, HRV possibly moving into the PPG chip, and "this will only ever be HRV at
  rest" optically. Does the contract survive *all* of those producer shapes — including a PPG chip that
  emits pre-computed HRV rather than intervals, and a resting-only optical path that starts and stops?
  Or does it silently assume a continuous strap-like stream?
- **#169's review disposition style.** rryles answers with `**Disposition: fixed in <sha>` / `won't fix —
  intentional design` / `not a bug`, each backed by a named commit or code path. Write your findings so
  they can be dispositioned that way: one defect, one location, one falsifiable claim. Vague
  "consider maybe" findings get closed as noise.
- **ABI compatibility is the recurring first-order concern** (#169: `HEART_RATE` stays the 2-field default
  so existing apps need no rebuild; #236 bumps `KERNEL_INTERFACE_VERSION` for an IPC change). Ask: does
  `0x44` need any version gate, capability query, or `KERNEL_INTERFACE_VERSION` interaction at all? What
  happens on an older kernel when an app subscribes to `0x44` — clean failure, or silent nothing? Trace
  the subscribe path (`Libs/Source/SensorLayer/SensorConnection.cpp`, and whatever #228's "recover a lost
  sensor subscribe" touched) and check whether any per-type table, min-period map, or `switch (Type)`
  must learn `0x44` for a subscription to succeed. A contract nobody can subscribe to is not a contract.
- **Stale doc tables get flagged** (#176 "fix stale parser table", #131 "fix description for
  SensorDataParsers"). `Docs/SensorsLayer.md` has a sensor-type table that stops at `0x42` and lacks even
  `HEART_RATE_EX` (0x43). Decide whether #220 should add `0x44` there, and whether the pre-existing `0x43`
  omission is a separate spin-off. Sweep *every* doc index that enumerates types or parsers.
- **Build-system enumeration drift gets its own CI check** (#223 MSVS/Makefile source-list sync, #240,
  #207, #205). A header-only parser plus one test file is low risk, but confirm nothing else enumerates
  headers, parsers, or host-test sources that #220 must update.
- **Small hygiene nits are routinely raised and are cheap to pre-empt** (#245 missing `<new>`, #247
  include/format nits, #249 compiler-warning ratcheting). Check the new header's includes are exactly
  what it uses (`<cmath>`, `<cstdint>`, the `HeartRateEx` include pulled in only for the
  `static_assert`s — is that dependency worth it, or does it couple two contracts unnecessarily?), and
  that the header is warning-clean under the strictest flags CI applies.

---

## 5. Phase 3 — attack the contract on the merits

These are the questions a producer or a serious consumer will eventually force. Treat each as a
hypothesis: prove it, refute it, or state what evidence is missing. Add your own.

1. **Silent beat loss is invisible.** rryles states event-based listeners get a **single-slot** queue so
   every frame is delivered immediately. Read the queue code. If a single-slot queue can be *overwritten*
   before a busy consumer drains it, beats are lost silently — and RMSSD/SDNN/DFA-α1 are destroyed by
   missing beats in a way a consumer cannot detect, because `DISCONTINUITY` is *producer*-set and knows
   nothing about consumer-side loss. Does the contract need a monotonic beat counter / sequence field, or
   a documented "reconstruct expected arrival from `rr_ms` and detect gaps" rule? This is the single most
   consequential thing I suspect is missing; settle it with code, not reasoning.
2. **Timestamp semantics vs. reality.** The header says the frame timestamp is "the beat instant the
   interval ends on". For a strap, every interval in one notification shares one *arrival* instant unless
   the producer back-dates. `EVENT_BASED` stops them being *dropped* — it does not make the timestamps
   *true*. Any consumer that reconstructs the beat series from timestamps rather than from `rr_ms` gets
   garbage. Must the contract mandate back-dating (`t_beat = arrival − Σ subsequent intervals`), or
   explicitly declare `rr_ms` authoritative and timestamps advisory? Pick one and say why. Also check the
   32-bit millisecond timestamp wrap (~49.7 days) against `getTimestampUs()`'s
   `mTimeStamp * 1000 + mTimeStampUs` composition, and against overnight-HRV use.
3. **Ordering.** Is frame order guaranteed to be beat order? Anywhere in the path? If not, say so.
4. **`NO_SKIN_CONTACT` collapses three states into one bit.** BLE `0x2A37` carries *contact supported*
   **and** *contact detected*. The contract has one bit whose zero, under the zero-means-not-reported
   invariant, reads as "contact is fine" — which is also what "the strap doesn't support contact
   detection" and "the producer didn't populate flags" read as. Is "contact status unknown"
   representable? Should it be? This is the same "cheap now, expensive later" class as the confidence
   field, and it is the kind of thing a second producer exposes immediately.
5. **`Source::ECG = 3` is by convention only.** The `static_assert`s cover the three values shared with
   `HeartRateEx`, which in turn locks to the kernel HR arbiter enum. Nothing stops the kernel later
   assigning `3` to something else. Can that be defended now — e.g. asserting `HeartRateEx` has no `3`,
   or reserving the value in a comment the arbiter's author will see? Also: is `OPTICAL` vs `ECG` even the
   right axis, given a chest strap *is* electrical and an evaluated wrist path is optical — does a
   consumer need modality (optical/electrical) separately from location (wrist/chest)?
6. **Is `u32` writable by the real producer?** The simulator has `DataSample::U32Writer`, so the
   simulator side is fine. The on-device kernel writer is not in this repo. Confirm there is nothing in
   the SDK-visible path that assumes all-float frames (parsers, logging, FIT writers, tutorials, the
   sensor-log example), and flag the kernel-side writer as an assumption the firmware author must confirm
   before `0x44` is declared stable — it is the one part of this contract the repo cannot prove.
7. **One-interval-per-frame vs. batching.** The header tells consumers to iterate `DataBatch::size()` so a
   future batching producer does not silently lose beats. Verify `DataBatch` exists with that API, verify
   the docs' guidance matches it, and check whether a batching producer is actually reachable given the
   fixed per-driver stride — or whether that advice is aspirational.
8. **`float` milliseconds.** The exactness argument (1 tick = 125/128 ms, `n × 125` under `2^24`) should
   hold — verify it, including the largest value a 16-bit tick field can produce. Then ask the harder
   question: does `float` survive *downstream* HRV math (successive differences, log-log DFA regression
   over hundreds of beats) or will consumers immediately promote to `double`/fixed-point anyway — and if
   so, is `u32` microseconds or `u16` ticks the more durable wire choice? Argue it either way, but argue
   it, because this is the field that can never change.
9. **Sentinel-zero returns.** `getRrMs()`, `getBpm()`, and both timestamps return `0` for invalid. Is
   conflating "invalid frame", "overflowed bpm", and a legitimately-zero timestamp acceptable? Check the
   SDK's precedent before calling it a defect — but confirm the *tests* pin the distinction where it
   matters.
10. **Is the ~100-line rationale header a feature or a liability?** It is unusually good documentation of
    *why*. It is also documentation that can go stale against the code it describes (the simulator caveat
    especially). Should the settled decisions live in `Docs/ExternalSensors.md` with the header pointing
    at them, or is duplication the right call for something a producer author reads in isolation?

---

## 6. Phase 4 — state of the art, honestly benchmarked

Do not grade on a curve against the rest of this SDK. Grade against what a serious HRV/training-readiness
platform needs, and against the interfaces this one will interoperate with:

- **BLE Heart Rate Service `0x2A37`** — the actual source format: flags byte, optional energy-expended,
  RR array in 1/1024 s, contact-supported/detected bits. Does `0x44` preserve everything `0x2A37` carries
  that an HRV consumer could want, or does it lose something at the seam?
- **FIT** — the `hrv` message (`time` array, seconds, scale 1000) and the RR-related record fields. If the
  point of this pathway is recording HRV to FIT, can a FIT writer be built from this contract without
  extra information? `Libs/.../Fit/` and `Tests/Host/fit/` are in-repo. If something is missing, that is a
  concrete "left to do", possibly a spin-off.
- **What DFA-α1 and RMSSD pipelines actually require** — artifact detection and correction, minimum
  window lengths, the fact that α1's sensitivity to correction method is a published result. Does the
  contract give a consumer enough to implement *its own* correction policy, which is the stated design
  intent? Name specifically what it cannot express.
- **Comparable device SDKs** (Polar, Movesense, ANT+ HRM pages, Wear OS/Health Services `HEART_RATE_VARIABILITY`/IBI
  streams with per-sample accuracy). What do they carry per interval that `0x44` does not? Where `0x44`
  is *better*, say so — this is not an exercise in finding fault.

Then give a straight verdict: **does this hold its own, and will it last?** If yes, say yes plainly and
name the two or three decisions that earn it. If no, name the minimum change that fixes it.

---

## 7. Verification (do not skip; do not fake)

There is no `cmake` on this machine and the host tests run in Docker — use the project's established
recipe (see `Docs/unit-testing.md` and the git-archive-export + copied-`coreJSON` approach; note `ctest`
reports 2 "tests" which hide the gtest suites, so read the gtest output, not the ctest summary). Build and
run the host tests on `feat/rr-interval-contract`, and report the real numbers.

Also actively try to **break** the tests rather than admire them:

- Do the 13 tests fail if you invert `NO_SKIN_CONTACT`? If `getFieldsNumber()` returns 1? If `SOURCE` and
  `FLAGS` indices are swapped? If `rr_ms` is read as `.u` instead of `.f`? A contract test suite that
  survives a mutated contract is not protecting the contract. Report which mutations survive.
- Does anything test the `assert(idx < fieldCount)` boundary in `DataView`, or the zero-field
  short-circuit ordering in `isDataValid()`?
- Is there a test that would catch a `Source` value drifting from the kernel arbiter, beyond the
  compile-time `static_assert`s?

If you cannot build, say so explicitly and mark every code-behaviour claim `PLAUSIBLE`. Do not report a
green suite you did not run.

---

## 8. Deliverable

Write `RR_INTERVAL_REVIEW.md` in the repo root (do not commit it unless asked). Structure:

1. **Verdict** — 5 sentences max. Does it hold its own? Will it last? Should it merge as-is?
2. **Blocking before merge** — things that are cheap now and unfixable after a producer ships. Each:
   claim, `file:line`, failure scenario with concrete values, proposed change, `CONFIRMED`/`PLAUSIBLE`.
3. **Should fix in this PR** — real but not contract-freezing.
4. **Prior comments: disposition table** — one row per item in §3, with `addressed / partially / not
   addressed / correctly declined` and the evidence.
5. **Spin off separately** — out-of-scope findings, each with the branch it belongs on and why it cannot
   ride along on #220.
6. **What's left to do overall** — the honest roadmap: simulator `EVENT_BASED` bypass, replay mock +
   fixture (`feat/rr-interval-tooling` exists locally and is stale against `main`), a real producer, FIT
   `hrv` plumbing, the `0x43`/`0x44` doc-table gap, `HeartRateEx` narrowing. Mark each
   **required-before-`0x44`-is-stable** or **later**.
7. **State-of-the-art assessment** — the §6 benchmark, with the specific gaps and the specific wins.
8. **Questions only a human can answer** — firmware/kernel facts the repo cannot settle. Keep this list
   short and each item genuinely blocking.

If you ran experiments to settle anything (mutated stride, saturated queue, shared-timestamp burst through
the simulator), bundle the evidence the way this repo already does it —
`Docs/Investigations/<today>-rr-interval-contract-review/` with a README per experiment: hypothesis, patch,
log, verdict, **including the experiments that refuted your own hypothesis**. Reference it from the report
rather than inlining raw logs.

Rank by consequence, not by count. **Ten padded findings are worse than three that are right.** If the
honest answer is "this is in good shape, merge it, and here are the two things to do next", say exactly
that — but only after you have genuinely tried to break it.
