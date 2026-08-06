# Upstream review culture (UNAWatch/una-sdk PRs #220, #231, #171)

Source: relayed read-only gh research from a peer session, 2026-08-05. Nothing was posted to GitHub.

## PR #220 — feat(sensor): RR_INTERVAL beat-to-beat pathway (tobymurray, OPEN)
- 6 files, +716/-0, contract-only (type constant 0x44, header-only parser, host tests, docs; no producer).
- CodeRabbit: two rounds, 3 inline comments, all functional-correctness (NaN/fractional rejection, non-finite RR rejection, C++-standard test question).
- Maintainer rryles: ~6,800-char review a week after opening; answered the PR's open design questions by MEASURING kernel behavior (rate-adapter decimation at various BPM, EVENT_BASED bypass). Two explicit BLOCKING items: getFieldsNumber() must return full stride (3) per SDK convention; float-vs-u32 for SOURCE/FLAGS must be decided pre-merge because "after something ships against 0x44 it's a silent ABI break". Plus "worth adding while it's still free" (graded-confidence field), reserved `__` include-guard point, test-gap note, and a "not yours, but noted" latent HeartRateEx bug.
- Author addressed everything, pushed back with reasoning on the confidence field (accepted as deferrable). rryles: "Happy to see this land once those are addressed." Still open pending fresh CodeRabbit review (Aug 5).

## PR #231 — feat(sdk): app-pushed home-screen widget IPC channel (sdvsaienko, MERGED same day, 2026-07-30)
- 6 files, +432/-7. Contract-only: message IDs 0x0330–0x0332, packed RequestWidgetUpdate POD (104 B = kernel pool-2 fit), SDK::HomeWidget helper, sim dispatcher acceptance, copyUtf8 host tests. Kernel rendering + first consumer deferred.
- rryles "Verdict: solid, approvable": itemized what he VERIFIED (struct size, message-ID non-collision, fire-and-forget semantics, copyUtf8 edges, NaN clamp, bounded %.*s logging), one pre-merge API footgun (implicit WidgetShow→float overload resolution; `= delete` fix), second round caught the delete making int percents ambiguous (verified against arm-none-eabi-g++). Author fixed, approved, self-merged ~9 h after opening.

## PR #171 — feat(fit): native SDK::Fit encoder, remove vendored FIT SDK (rryles, MERGED ~15 h)
- 479 files, +4,822/-153,084 (deletions = vendored Garmin FIT SDK). Landed with ZERO reviews: CodeRabbit skipped ("Too many files"), no human review. Safety story was exhaustive self-verification in the PR body: 132/132 host tests incl. byte-exact encoding, CRC known-answer 0xBB3D, encode→decode round-trip, third-party fitparse validation, real Strava import, ARM .uapp builds through CI Docker.

## Review-culture observations
- Two-tier: CodeRabbit on every PR (CHILL profile) catches input-validation nits; rryles is the human gatekeeper for contract PRs and verifies claims EMPIRICALLY (compiles repros, measures kernel delivery, traces struct packing).
- Human pushback priority: ABI/wire-format irreversibility ("last moment it's free") > API footguns > SDK-internal conventions > test gaps >> naming/style.
- Explicit blocking / worth-adding / not-yours-but-noted grading; reasoned author pushback on non-blocking items is accepted.
- Well-scoped contract-only PRs (6 files) can merge in a day with two human rounds; external-contributor contract PRs get slower, deeper treatment; maintainer mega-PRs can bypass review with self-verification as the safety story.
- Authors self-merge after review sign-off; sign-off, not merge mechanics, is the gate.

## Implications for a rawtiles PR stack (my read)
- rryles WILL treat the .rawtiles wire-format dependency and the Container's public API as ABI-irreversibility items — "decide before something ships against it" is exactly his lens. A reader pinned to a Provisional v0.6 spec with an unmet v1.0 gate will draw that question immediately.
- Contract-only, ~6-file PRs are the shape that merges fast. A Container+tests PR without the tutorial matches that shape; the tutorial app (hundreds of generated TouchGFX files) will exceed CodeRabbit's 150-file limit like #171 did, so its safety story must be self-verification in the PR body.
- Empirical verification in the PR body (host tests, conformance matrix, device screenshot) is the house currency.
