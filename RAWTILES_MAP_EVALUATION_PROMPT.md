# Prompt: Evaluate rawtiles as the una-sdk map interface, and pick the next step

You are a hostile-but-fair evaluator of a **format + SDK-surface strategy**, not of ordinary code. Your
subject is the intersection of two bodies of work by the same author:

1. **The rawtiles spec** — https://tobymurray.github.io/rawtiles/ (source:
   https://github.com/tobymurray/rawtiles), a binary tile-pack format for offline map delivery to
   constrained devices. Spec-document version **0.6** (2026-05-17), wire format `(1, 0)`, status
   **Provisional**: "v1.0 stabilizes the wire format once a second independent consumer has validated
   against this spec." The repo also carries a v0.6-conformant C99 `reference-reader/` and a conformance
   corpus (`spec/conformance/{golden,negative,generators,manifest.json,verify.py}`).
2. **The una-sdk map work** — all on unmerged fork branches, nothing upstream. `feat/rawtilesmap-tutorial`
   (2 commits ahead of `main`, 7 behind) carries `SDK::RawTiles::Container`
   (`Libs/Header/SDK/RawTiles/Container.hpp` + `Libs/Source/RawTiles/Container.cpp`), a RawTilesMap
   TouchGFX tutorial app (`Docs/Tutorials/RawTilesMap/`), a 769 KB sample pack
   (`Resources/stanley.rawtiles`), and a host conformance driver
   (`Utilities/Scripts/rawtiles_check/`). The `experiments` branch holds the ~45-commit debugging lineage
   the tutorial was squashed from.

The end goal, verbatim from the maintainer: **an SDK surface that supports UNA building whatever
map-based functionality they need, while staying open so users can supply any possible map they desire
(that fits the device constraints) without needing UNA to make changes.**

The bar is not "does the simulator show Stanley Park". The bar is: **once packs circulate and a shipped
app opens them, the wire format and the SDK reader API are both frozen.** So the three questions that
matter, in order:

> **(a)** What is the true state of this work — what is proven, what is simulator-only theatre, and what
> is missing entirely?
> **(b)** Is rawtiles a state-of-the-art spec for its niche that can and should be implemented as *the*
> una-sdk map interface — or should the effort pivot to an existing format or a different altitude?
> **(c)** What is the single next step, and in what order do the pieces land given this repo's
> one-reason-per-branch discipline?

---

## 0. Ground rules

- **Never post anything to GitHub.** No PR comments, no issues, no `@coderabbitai` triggers — on either
  repo. Read-only via `gh api` / `gh pr view` / raw fetches. Your deliverable is a local markdown report.
  Hard constraint, not a preference.
- **Verify, don't trust.** Every claim in this prompt is a *hypothesis to confirm or refute against the
  code and spec text*, including the ones stated confidently — several may be wrong or stale. Cite
  `file:line` (or spec `§`) for everything. Label each finding `CONFIRMED` (traced) or `PLAUSIBLE`
  (reasoned) and say what would settle a `PLAUSIBLE` one.
- **Scope discipline.** This repo's convention is one reason to merge per branch, dependent branches
  stacked. Anything real but out of scope goes in a "spin off" list with the branch it belongs on.
- **Grade the spec against the world, not against this SDK.** And grade the SDK work against the end
  goal, not against "it renders".
- Read `CLAUDE.md` and `Docs/` before judging idiom. The investigation convention
  (`Docs/Investigations/<date>-<slug>/` with hypothesis / patch / log / screenshot / verdict per
  experiment, failures included) is the evidentiary bar — see
  `Docs/Tutorials/RawTilesMap/Investigations/2026-05-16-cell-render-bug/` on the tutorial branch for the
  house exemplar, and `Docs/Investigations/2026-08-04-rr-interval-contract-review/` on `main`. Settle
  questions by experiment where an experiment is cheaper than an argument.

---

## 1. What actually exists (inventory to verify, then build on)

### The spec side (github.com/tobymurray/rawtiles)

- `spec/rawtiles.md` — 1067 lines, RFC-2119 normative, five-section pack layout (292-byte header,
  20-byte/entry tile index, aligned tile blob, TLV extensions, CRC-32 footer). Two pixel formats
  (`ABGR2222`, `RGB565`), two compressions (`None`, pixel-level `RLE`), Quadtree/WebMercator +
  SingleImage/LocalLinear (via 48-byte `AFFN` affine), XYZ/TMS axis conventions, 24-entry per-zoom
  directory, canonical `pack_uuid` derivation (Appendix A) for cross-writer byte-identical
  reproducibility, and an explicit eager-vs-lazy reader validation model sized for MCU-class readers
  (~1 KB structural state, `O(P)` RLE decoder state).
- `spec/conformance/` — golden fixture generators, a ~60-case negative corpus, `manifest.json`,
  `verify.py`, per-tile SHA-256 hash tables (§ 14.5). The v0.6 change history *itself* flags two stale
  negative fixtures (`neg-07b-pixfmt-2`, `neg-07g-comp-1` — they assert codepoints v0.4 legalised) as
  known debt.
- `reference-reader/` — C99, no-heap, caller-owned buffers, two I/O modes (`RAWTILES_IO_MEMORY`,
  `RAWTILES_IO_PREAD`), result codes that encode spec rule numbers. Its README status table says eager
  validation is complete but lazy mode, NAME/SRCD/ATTR text validation, streaming CRC, and § 14.5
  hash-table verification are **planned, not done**.
- **No published writer.** The sample pack is "slippypack-generated" (commit `8897daf7` body) but no
  `slippypack` exists in either repo or on this machine; the only writer code that exists publicly is
  the Python fixture generators (`spec/conformance/generators/_lib.py` et al.).

### The SDK side (this repo, fork branches)

- `SDK::RawTiles::Container` — C++ reader, header docstring pins it to **spec v0.2** "to the extent
  required by a Quadtree / WebMercator / ABGR2222 reader": `PixelFormat{ABGR2222}` only,
  `Compression{None}` only, whole-pack-in-RAM via `std::vector<uint8_t>` + `std::fopen` (not the SDK's
  `IFileSystem`), ~25-value `OpenResult` enum citing spec § 11 rules. It is **four spec revisions
  stale** (v0.3 ATTR rejections, v0.4 RGB565+RLE, v0.5 conformance consolidation, v0.6 pixel-RLE).
- `Utilities/Scripts/rawtiles_check/` — 261-line host driver with inlined SHA-256, self-described dev
  artifact for running the Container against golden/negative fixtures. **Not wired into any CI.**
- **No host tests for Container** — `Tests/Host/` on the branch tests only the tutorial's
  `ActivityWriter` (a pre-existing GpsTrack copy). The reader that enforces ~30 rejection rules has zero
  gtest coverage.
- The RawTilesMap app — `TileCanvas` widget mosaics a 3×3 grid of 256 px tiles onto the 240×240 screen
  via `Bitmap::dynamicBitmapCreateExternal` (zero-copy: pack bytes are TouchGFX-native ABGR2222) and
  `LCD::blitCopy`. The blitCopy choice is the survivor of a real investigation: prebuilt
  `libtouchgfx.a`'s `LCD8bpp_ABGR2222::drawPartialBitmap` silently drops negative-X draws (bundle in
  `Docs/Tutorials/RawTilesMap/Investigations/`; the Y-clipping variant reproduces **on STM32U595
  hardware** per `docs/touchgfx-drawpartialbitmap-y-clipping-bug-report`).
- **The entire pack-loading path is `#if defined(SIMULATOR)`** in `Model.cpp`. There is no device path.
- `ARCHITECTURE.md` in the tutorial is stale — verbatim Sensors-tutorial text, never rewritten.
- SDK wiring is the canonical optional-module pattern: `UNA_SDK_SOURCES_RAWTILES` in
  `cmake/una-sdk.cmake`, apps opt in via `GUI_SOURCES`.

### The device constraints the design must survive

| Constraint | Value | Source |
|---|---|---|
| MCU | STM32U595 (Cortex-M33), native ARM ELF apps | `Docs/architecture-deep-dive.md`, `Docs/platform-overview.md` |
| Display | 240×240, 8bpp ABGR2222, software render, no DMA | `Docs/TouchGFX-Port-Architecture.md` |
| Framebuffer | static `uint8_t[57600]` | `Docs/TouchGFX-Port-Architecture.md` |
| App RAM | **256 KB** ("App Memory RAM — Loaded Applications") | `Docs/architecture-deep-dive.md` |
| Internal flash | 2 MB NOR; external eMMC; volumes `0:/`, `1:/`, `2:/` USB | `Docs/architecture-deep-dive.md`, `Docs/sdk-overview.md` |
| Filesystem API | `SDK::Interface::IFileSystem` / `IFile` (seek/read), `skMaxPathLen=256` | `Libs/Header/SDK/Interfaces/IFileSystem.hpp` |

Do the arithmetic yourself and put it in the report: the 769 KB sample pack vs 256 KB app RAM; a single
256 px ABGR2222 tile = 64 KB, the current 3×3 mosaic = 576 KB of tile bytes; RLE at the spec's measured
27%-of-raw on RGB565 (v0.6 change history, measured **on this very stanley corpus**) vs the 2 MB NOR
budget. The simulator architecture (whole pack resident, dynamic bitmaps over pack memory) **cannot
port to the device as-is** — confirm or refute that, and identify what the spec already provides for it
(pread I/O mode, row-streaming RLE, lazy validation) versus what the Container lacks.

### The openness gaps (the end goal's second half)

- **Deployment:** `app_packer.py` packs only ELFs + icons; `cmake/una-app.cmake` uses `RESOURCES_PATH`
  for two icon files. A `.rawtiles` pack reaches the watch only by hand-copy over USB mass storage
  (`Docs/deploy.md`). No discovery convention (path, volume, filename) exists.
- **Authoring:** without a published writer tool, *no user can actually supply their own map today.*
  The openness claim currently rests on an unpublished script.
- **Extension mechanism:** the SDK has no runtime plugin seam (all four extension seams are
  compile-time); the *format's* extension mechanism (lowercase app-private TLV tags, uppercase reserved
  tags) is what "users extend without UNA changes" would actually lean on. Assess whether that division
  of labour is coherent.

---

## 2. Repository intelligence to mine first

- `git log --all -i --grep=rawtiles --format='%h %s'` — the ~45-commit development lineage on
  `experiments` (orientation flips, seam-centre selection, dirty-rect clipping, the
  drawPartialBitmap→blitCopy pivot). The commit bodies are long-form design records; mine them before
  re-deriving anything.
- The four TouchGFX-bug branches (`docs/touchgfx-drawpartialbitmap-negative-x`,
  `docs/touchgfx-drawpartialbitmap-y-clipping-bug-report`, `bug/drawpartialbitmap-y-clipping-repro`,
  plus the helper `Libs/Header/SDK/GUI/DynamicBitmapDraw.hpp` on the first) — these constrain which
  TouchGFX draw paths a device renderer may use. Check whether the helper's
  `#if defined(SIMULATOR) && defined(__linux__)` gate means the workaround is simulator-only while the
  bug is hardware-real.
- `SDK::TrackMapBuilder` (`Libs/Header/SDK/TrackMap/`) — the only geo code on `main`; note
  `TrackMapScreen::Point` is `uint8_t x,y`. Decide whether the map work should compose with, replace, or
  ignore it.
- Upstream currents: PR #220 (RR_INTERVAL) for how contract PRs are reviewed here; merged #231
  (widget IPC), #171 (native FIT encoder) for how new SDK modules land; `Docs/SensorsLayer.md` +
  `SensorDataParserGpsLocation.hpp` (floats, not doubles — does float lat/lon at zoom 14+ even resolve a
  pixel? compute it).
- The spec's own git history (`gh api repos/tobymurray/rawtiles/commits`) — six spec versions in four
  days (0.1 → 0.6, 2026-05-14 → 05-17), then **nothing since**. Two and a half months of silence while
  the SDK reader sits at v0.2 is itself evidence about process; weigh it.

---

## 3. Phase 1 — status quo, audited

Confirm or refute, with evidence:

1. **The v0.2 → v0.6 delta is unimplemented in the SDK.** Diff the spec versions (Appendix B) against
   `Container.cpp`. Enumerate exactly which v0.3 reader-rejection rules, v0.4 additions, and v0.5/0.6
   conformance-model changes the Container fails to enforce or cannot read. Then run
   `rawtiles_check` against the **v0.6 corpus** and report the real pass/fail matrix — that, not the
   docstring, is the ground truth of staleness.
2. **The Container has no test harness in CI and no host tests.** Is `rawtiles_check` even buildable
   from a fresh clone of the branch? Does anything run the negative corpus?
3. **The device path does not exist.** `#if defined(SIMULATOR)` in `Model.cpp`, `std::fopen` in
   `Container.cpp`, whole-pack RAM residency, and the bitmap-cache arithmetic. Sketch (numbers, not
   vibes) what a device-feasible read-render path looks like within 256 KB: pread-style `IFile` reads,
   per-row or per-tile-strip decode, blit during `Widget::draw`. State what the Container API must
   become to support it (the reference-reader's `RAWTILES_IO_PREAD` is the model).
4. **The pack pipeline has a missing link (writer) and a missing convention (deployment/discovery).**
   Verify slippypack's absence; check whether the conformance generators could seed a real writer.
5. **The tutorial's paper trail is inconsistent** — stale `ARCHITECTURE.md`, squashed-away fixes,
   branch 7 behind `main` (which has since gained reproducible-build CI gates that any new app Makefile
   must satisfy — check `ci(apps)` commits on `main`). What must be true before this branch could even
   open as a PR stack?

---

## 4. Phase 2 — the spec on its merits, honestly benchmarked

Do not grade on a curve because it is the maintainer's own spec. Benchmark against what exists:

- **PMTiles v3** — the closest modern single-file tile archive. Its directory model
  (clustered, varint, gzip'd directories; Hilbert tile IDs) targets HTTP range reads on hosts, not MCU
  flash. Compare honestly: what does rawtiles gain by rejecting it (decode budget, alignment,
  no-inflate), and what does it lose (ecosystem, tooling, generality)?
- **MBTiles** — SQLite dependency; presumably disqualifying on-device. Say so with numbers (code size
  of SQLite vs the ~600-line Container).
- **Garmin's custom-map formats** (KMZ/JNX/IMG) and **Bangle.js / InfiniTime / wasp-os** map
  approaches — the actual competitive set for "user-supplied maps on a watch". What do their
  constraints and failure modes teach? Is anyone else shipping pre-rasterized packs with a published
  byte-level spec and negative corpus? (If the answer is "no, rawtiles' rigor is genuinely unusual for
  this class", say that plainly — this is not an exercise in finding fault.)
- **Raster vs vector** — the strategic fork. Vector tiles (MVT) would give runtime styling, rotation,
  and small packs, at the cost of a renderer the 240×240/no-GPU/256 KB target may not afford, and a
  much larger seam UNA would own forever. Pre-rasterized raster is the "users bring anything" position:
  *any* source (OSM raster, satellite, nautical charts, floor plans via SingleImage+AFFN) flattens to
  pixels offline. Rule on whether raster-first is right for the end goal, and whether the spec leaves a
  clean door open for a future vector or hybrid profile (new `pixel_format`/`compression` codepoints vs
  new major version).
- **Spec-quality specifics to stress:**
  - The v1.0 gate says "a second **independent** consumer". The SDK Container is the same author. Is
    the gate satisfiable in spirit? What would count — the InfiniTime/PineTime-class reader the v0.4
    change history explicitly courts? Propose an honest stabilization criterion.
  - 24-entry zoom directory, u32 offsets (4 GB pack ceiling), 16-bit `tile_dim_px`, microdegree bbox:
    which of these will a real use case break first?
  - `ABGR2222` is a TouchGFX-ism; `RGB565` is everyone else. Is the pixel-format enum the right
    extensibility axis, and is quantization (§ 9.1.1) specified tightly enough that two writers agree?
  - The reproducibility/`pack_uuid` machinery (Appendix A) is unusual rigor — but is it load-bearing
    for the actual use case (a user side-loading one pack), or complexity tax? Who dedups?
  - Known debt the spec admits: the two stale negative fixtures; lazy mode unimplemented in the
    reference reader. Anything else `verify.py` or the manifest reveals?

Then give a straight verdict on (b): **is this spec state-of-the-art for its niche, and should it be
implemented as the una-sdk map interface?** If yes, name the two or three design decisions that earn
it. If no, name the minimum change — or the alternative — that would.

---

## 5. Phase 3 — the interface question (the end goal's real test)

The end goal has two halves that pull in opposite directions: UNA needs a *rich internal surface*
(position→tile math, viewport composition, track overlay, live GPS); users need a *frozen external
surface* (the file format + where to put the file). Evaluate whether the current shape draws that line
correctly:

1. **The external contract should be the pack, not the API.** If the `.rawtiles` file format + a
   documented path convention (e.g. `1:/maps/*.rawtiles`) is the entire user-facing surface, users
   supply any map without UNA changes *by construction*, and UNA iterates the SDK internals freely. Is
   anything in the current design leaking SDK internals into the user contract (e.g. app-ID-specific
   paths, simulator env vars like the `RAWTILES_PATH` hack in `Model.cpp`)?
2. **What is the SDK surface, concretely?** Today it is one class (`Container`) returning raw tile
   bytes, and every app hand-rolls Mercator math, mosaic seams, and TouchGFX workarounds (the tutorial's
   `TileCanvas` is 300+ lines of hard-won, bug-dodging rendering). If every map app must copy that,
   UNA's half of the goal is unmet. Should the SDK grow a `TileCanvas`-equivalent widget and a
   lat/lon→(z,x,y)→screen transform utility? Where does `TrackMapBuilder` (breadcrumb overlay) fit?
3. **Pack discovery and lifecycle** — enumeration of `1:/maps/`, `NAME`/`ATTR` extension tags for a
   picker UI, `supersedes_uuid` for updates, multi-pack coexistence, and what happens at the bbox edge
   (the slate-sentinel behaviour). Which of these need convention now vs later?
4. **The constraints envelope** ("any possible map *that fits the constraints*") — write it down as the
   user-facing contract it implicitly is: max zoom span on flash budget, tile_dim vs RAM, ABGR2222-only
   for now, WebMercator or SingleImage. If the constraints are unstated, users discover them by
   failure.
5. **Authoring path** — what is the minimum viable published writer (PNG/slippy-dir → pack, per the
   golden generators) and where should it live (rawtiles repo, not una-sdk)? Until it exists, is the
   honest status "open in principle, closed in practice"?

---

## 6. Phase 4 — the next step, decided

Candidate next steps, deliberately in tension. Pick one (or a strictly-ordered stack), justify against
everything above, and size it:

- **A. Conformance first:** bring `Container` to spec v0.6, add gtest host tests + wire the golden and
  negative corpora into `Tests/Host/` (the corpus is fetchable; decide vendored-fixtures vs generator),
  restructure for `IFileSystem`/pread streaming. This is simultaneously the spec's
  second-consumer validation gate and the device-feasibility prerequisite. Own PR; tutorial rides
  later.
- **B. Device proof first:** get *any* tile on the physical watch (even one tile, `None` compression,
  hand-copied pack) to retire the biggest unknown — that the whole concept survives 256 KB + the
  TouchGFX hardware draw path (where the Y-clipping bug is confirmed live). Evidence per the
  investigation convention.
- **C. Spec first:** close the v0.6 debt (stale fixtures), specify what v1.0 actually requires, and
  freeze — on the theory that shipping readers against a moving provisional spec is the root risk.
- **D. Pipeline first:** publish the writer + deployment convention, on the theory that an interface
  nobody can feed is not an interface.
- **E. Upstream first:** rebase, split, and open the PR stack (SDK reader / tutorial app / utilities)
  to get UNA's review signal before investing further.

State explicitly which risks each option retires, which it defers, and what ordering the
one-reason-per-branch rule forces. Recommend one sequence. If your investigation shows the honest
answer is "B before A because device feasibility is unproven and would invalidate A's API shape", say
exactly that.

---

## 7. Verification (do not skip; do not fake)

- There is no `cmake` on this machine; host tests and simulator builds run in Docker — use the
  project's established recipes (`Docs/unit-testing.md`, `Docs/Simulator.md`; the git-archive-export +
  copied-`coreJSON` approach; `ctest` hides gtest suites, read the gtest output).
- Build the RawTilesMap simulator from `feat/rawtilesmap-tutorial` and reproduce the render (screenshot
  it, per the in-repo screenshot recipe on `experiments`: `36084ac3`).
- Build `rawtiles_check` and run it against the **current** conformance corpus (clone
  `tobymurray/rawtiles`); report the true pass/fail matrix including the two known-stale negative
  fixtures. Also build and run the C99 `reference-reader` conformance harness so you have both readers'
  results side by side.
- Actively try to break the Container: truncated pack, flipped CRC, out-of-order index, zoom_offsets
  leak, oversized `extensions_offset`. It parses attacker-supplied files with pointer arithmetic —
  fuzz it at least casually (the negative corpus is a seed set) and report anything that crashes rather
  than rejects.
- If you cannot build, say so explicitly and mark every behaviour claim `PLAUSIBLE`. Do not report a
  green run you did not perform.

---

## 8. Deliverable

Write `RAWTILES_MAP_EVALUATION.md` in the repo root (do not commit unless asked). Structure:

1. **Verdict** — 5 sentences max, answering (a), (b), (c) directly.
2. **Status quo** — what is proven / simulator-only / missing, each with evidence and
   `CONFIRMED`/`PLAUSIBLE`.
3. **Spec assessment** — the § 4 benchmark: state-of-the-art ruling, the decisions that earn it, the
   gaps that would block v1.0, the honest stabilization criterion.
4. **Interface ruling** — the § 5 line between UNA's surface and the user contract, with the specific
   changes that enforce it.
5. **The next step** — the § 6 decision, as a concrete branch/PR sequence with the first branch's scope
   spelled out.
6. **Spin off separately** — real-but-out-of-scope findings, each with its branch.
7. **Questions only the maintainer can answer** — short, each genuinely blocking (e.g. eMMC read
   latency, whether UNA would accept a `ThirdParty` spec dependency, PineTime as a second target).

If you ran experiments, bundle evidence per the house convention
(`Docs/Investigations/<today>-rawtiles-map-evaluation/`), failures included, and reference it from the
report. Rank by consequence, not count. **Ten padded findings are worse than three that are right.** If
the honest answer is "the concept is sound, the spec is unusually rigorous for its class, and the next
step is X", say exactly that — but only after you have genuinely tried to break both the format and the
plan.
