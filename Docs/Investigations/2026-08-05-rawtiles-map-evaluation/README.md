# 2026-08-05 — rawtiles / map-interface evaluation: evidence bundle

Supporting evidence for `RAWTILES_MAP_EVALUATION.md` (repo root). Subject: the rawtiles
format spec (`github.com/tobymurray/rawtiles` @ `38d4d26`, spec v0.6) and the una-sdk map
work on `feat/rawtilesmap-tutorial` (@ `7fc2602c`). All builds ran from throwaway copies;
no repo tree was modified, nothing was posted to GitHub.

Environment: macOS arm64, Apple clang 21.0.0 (conformance/fuzz, no cmake or docker needed);
`docker --platform linux/amd64 gcc:13` + SDL2/ruby-nokogiri (simulator); Python 3 (fuzz
mutator, arithmetic).

## E1 — Conformance: both readers vs the v0.6 corpus

**Hypothesis:** the SDK `Container` (docstring-pinned to spec v0.2) fails chunks of the
current corpus; the C99 reference reader passes it.

**Method:** hand-compiled `Utilities/Scripts/rawtiles_check` (+`Container.cpp`) and the
reference reader's `tests/conformance.c` with clang; ran both over all 96 committed
fixtures (17 golden + 79 negative; `verify.py` corpus self-check 96/96 clean first).

**Verdict: partially wrong in an interesting direction.**
- Container: golden **17/17** incl. all 70 §14.5 tile hashes; negatives **57/79** — the 22
  failures are all wrong ACCEPTs, confined to extension-payload validation (AFFN, NAME,
  SRCD/ATTR text, duplicate tags, padding). Structurally it is v0.5-current, not v0.2.
- Reference reader: golden **17/17** + 70/70 hashes; harness **82/96** — 13 wrong accepts
  (its own README's documented "planned" gaps: #29b, #37, #38) + neg-18d rejected under
  rule #14 instead of #18.
- The fixtures the brief called stale (`neg-07b-pixfmt-2`, `neg-07g-comp-1`) do not exist:
  renamed/refixed in spec commit `e33d145`; both successors correctly rejected by both readers.
- Both readers agree on `stanley.rawtiles` (787,008 B): ABGR2222, WebMercator/Quadtree/XYZ,
  256 px, z∈[12,14], 12 tiles (z12:2, z13:4, z14:6), bbox lon[−123.155,−123.120] lat[49.290,49.315].

Logs: `conformance/container_matrix.txt`, `conformance/reference_matrix.txt`.

## E2 — Fuzzing the Container (ASan+UBSan, `-fno-sanitize-recover=all`)

**Hypothesis:** a 878-LOC pointer-arithmetic parser of attacker-supplied files has at least
one crash. **Verdict: refuted.** 52 targeted structural mutations (truncations at every
boundary, CRC flips, index swaps, zoom_offsets leaks, wild extensions_offset/lengths — CRC
refixed where needed so the targeted validator was actually reached) → all cleanly rejected.
2,000-iteration random loop (seed 20260805, 1–8 bit flips, 50% CRC-refixed): 181 accept /
1,819 reject / **0 crashes, 0 sanitizer reports, 0 timeouts**. The 181 accepts landed in
unvalidated bytes (pixels, timestamps, extension payloads) — consistent with E1's
fail-open-extensions finding, not memory-safety issues.

Script + log: `conformance/fuzz.py`, `conformance/fuzz_log.txt`.

## E3 — Simulator build + render (`feat/rawtilesmap-tutorial`)

**Hypothesis:** the tutorial builds and renders Stanley Park in the Linux simulator.

**Verdict: renders, but the committed tree does NOT build.** Two failures first
(kept per convention):
1. `simulator/build.log` — the app's gcc Makefile still lists `FitHelper.cpp` + the vendored
   Garmin FIT SDK that ancestor `659a7a5f` removed (the branch predates the port; its own
   `ActivityWriter.cpp` already uses `SDK::Fit` — only the Makefile is stale).
2. `simulator/build2.log` — missing `ImuRunningCadence.cpp` in the source list (link failure).
3. Plus the usual missing `config/gcc/app.mk`.

After those three build-copy-only fixes (`simulator/build3.log`): clean build, boots
(`GUI is now running`, `simulator/sim.log:52`), and `simulator/01-boot-map-screen.png` shows
a coherent 240×240 Lions Gate Bridge / Vancouver Harbour map — no seams, no slate patches,
4/9 viewport cells filled covering the full visible area. Pack found via the
`RAWTILES_PATH` env var (`Model.cpp:64`). **The simulator's canned GPS fix (49.23N 28.47E)
has no effect — the viewport is pack-driven, not GPS-driven.** Key '1' toggles to the Main
sensor screen (`02-`, `03-*.png`).

## E4 — Feasibility arithmetic

`arithmetic/arith.txt` (script alongside): one 256 px ABGR2222 tile = 64 KiB; the current
3×3 raw mosaic = 576 KiB and the whole pack = 768.6 KiB vs a 500–600 KB per-process linker
budget (the "256 KB app RAM" figure exists only as a Mermaid label) — whole-pack-in-RAM
cannot port. RLE at the spec's measured 27%-of-raw puts a compressed-resident 3×3 working
set at ~156 KiB. Float lat/lon resolves ≤0.36 px through z16 (marginal at z17–18). Flash:
10×10 km at z15 alone is ~2.9 MiB RLE'd → packs live on eMMC (`1:/`), not the 2 MiB NOR.

## Findings reports

`findings/` holds the full audit reports this bundle supports: `sdk-audit.md`,
`spec-audit.md`, `verification.md`, `simulator.md`, `ecosystem.md`, plus the delegated
research passes (`main-repo-audit.md`, `branch-mining.md`, `upstream-review-culture.md`).
