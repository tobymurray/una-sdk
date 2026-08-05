# Rawtiles as the una-sdk map interface — evaluation and next step

2026-08-05. Subjects: `github.com/tobymurray/rawtiles` @ `38d4d26` (spec v0.6, wire (1,0),
Provisional) and `feat/rawtilesmap-tutorial` @ `7fc2602c` (2 ahead / 7 behind `main`).
Evidence bundle: `Docs/Investigations/2026-08-05-rawtiles-map-evaluation/` (conformance
matrices, fuzz logs, simulator build logs + screenshots, arithmetic, full audit reports).
Everything below is labeled **CONFIRMED** (traced/executed) or **PLAUSIBLE** (reasoned, with
the settling step named). Nothing was posted to GitHub.

---

## 1. Verdict

**(a)** The format and reader core are real and verified — both readers pass all 17 golden
fixtures with all 70 tile hashes, and the Container survived 2,052 adversarial mutations
under ASan/UBSan with zero crashes — but everything user-facing is simulator theatre:
the branch does not build as committed, the pack path is `#if defined(SIMULATOR)`, the
"map" never reads GPS, no writer exists, and the `.uapp` pipeline has no slot for a data
file, so no user can put a map on a watch today by any means but hand-copy of a file no
published tool can produce. **(b)** Yes — the spec is state-of-the-art for its niche (no
published byte-level spec + conformance corpus exists anywhere for raw-pixel tile packs;
the one known MCU tile project independently converged on the same storage model), and
raster-first is the right altitude for the end goal — but it is not implementable as *the*
SDK interface until its two biggest debts are paid: the RGB565/RLE surface has zero
conformance fixtures despite §14.3/§14.5 claiming them, and the v1.0 gate is unsatisfiable
in spirit while every consumer is the same author. **(c)** The next step is a one-week
device spike — one tile, read via `IFileSystem` from `1:/maps/`, blitted on the physical
watch — because its findings (eMMC latency, blitCopy-on-hardware, the forced move from
zero-copy to a copying tile cache) dictate the Container API shape that everything else
freezes; the PR stack then lands as spec-debt fixes (rawtiles repo, parallel), then a
conformance+tests+streaming Container PR, then the writer, then the tutorial.

## 2. Status quo, audited

### Proven (CONFIRMED)

- **The format core round-trips through two implementations.** Container: 17/17 golden,
  70/70 §14.5 tile hashes, 57/79 negatives; reference reader: 17/17 golden, 66/79
  negatives. Both agree byte-for-byte on `stanley.rawtiles` (787,008 B; 12 tiles, z12:2 /
  z13:4 / z14:6 — which also settles the lineage's "12 tiles" vs "2×3 at z14" discrepancy).
  Bundle: `conformance/*_matrix.txt`.
- **The Container is memory-safe under attack.** 52 targeted structural mutations
  (truncations at every boundary, CRC flips, index swaps, `zoom_offsets` leaks, wild
  `extensions_offset`/lengths, CRC refixed so validators are actually reached) + 2,000
  random bit-flip mutations, all under ASan+UBSan with recovery disabled: **0 crashes, 0
  reports, 0 timeouts**. The bounds discipline is real: every served pointer is
  pre-validated with overflow-safe u64 arithmetic (`Container.cpp:281-284,382,386-394`).
  This is better bounds hygiene than most of what already ships in `Libs/Source`.
- **The simulator render is real.** Built from the branch in docker amd64, the app boots
  and renders a coherent, seam-free Stanley Park / Lions Gate Bridge map
  (`simulator/01-boot-map-screen.png`), zero-copy from pack bytes via
  `dynamicBitmapCreateExternal` + hand-pre-clipped `blitCopy`.
- **The debugging record is exemplary in method.** The cell-render-bug bundle
  (8 ruled-out hypotheses, diffs+logs+screenshots each, honest red-herring retraction)
  matches and arguably exceeds the house convention — though it cites four commit hashes
  that resolve nowhere (pre-squash history never pushed) and lives under the tutorial
  instead of `Docs/Investigations/`.

### Simulator-only theatre (CONFIRMED)

- **The branch does not build as committed.** Its simulator Makefile still references
  `FitHelper.cpp` and the vendored Garmin FIT SDK that `main` removed in the SDK::Fit
  migration (`659a7a5f` / upstream PR #171), and omits `ImuRunningCadence.cpp`; plus the
  missing `config/gcc/app.mk`. Three fixes were needed to build (`simulator/build*.log`).
- **The entire pack path is `#if defined(SIMULATOR)`** (`Model.cpp:44-188`), discovered via
  a `RAWTILES_PATH` env var (`Model.cpp:64`) or a six-entry relative-path walk. On hardware
  `mTiles` is never opened and the map screen renders a slate rectangle. `Model.hpp:78`
  still holds the Container unconditionally, so 878 LOC of parser links into the ARM ELF
  it can never use.
- **The map never sees GPS.** No lat/lon→tile math exists anywhere on the branch (the only
  trig is a compass `atan2f`); the viewport centre is the pack's first index entry at
  `zoom_max` (`Model.cpp:110-140`). E3 proved it live: the simulator's canned fix
  (49.23N 28.47E) changes nothing. "Tile-based map renderer" currently means "static
  quadrant viewer of a bundled pack".
- **Whole-pack-in-RAM cannot port.** `std::fopen` + `std::vector` slurp
  (`Container.cpp:110,133-134`), not `SDK::Interface::IFileSystem`. The pack is 768.6 KiB
  and the raw 3×3 mosaic is 576 KiB against a 500 KB (service) / 600 KB (GUI) per-process
  linker budget (`cmake/una-app.cmake:217,:281`) that must also hold TouchGFX heap and
  stacks; the experiments lineage itself measured 640 KB for the viewport.
  `Container.hpp:166-168`'s "fine for … the embedded target" is refuted. (Premise
  correction: the prompt's "256 KB app RAM" exists only as a Mermaid label,
  `architecture-deep-dive.md:1628` — do not cite it as documented. The conclusion stands
  under the real 600 K figure.)
  What the spec already provides for the device path and the Container lacks: pread-style
  I/O (`RAWTILES_IO_PREAD` in the reference reader), pixel-RLE at a measured 27%-of-raw
  (3×3 compressed-resident working set ≈ 156 KiB — fits), row-streaming decode within
  ~520 B of state (possible per spec via mid-run suspension; demonstrated by no code
  anywhere), and lazy validation. Note `IFile` is absolute-seek + `char*` read with no
  positioned read (`IFileSystem.hpp:213,:229`), so "pread mode" means seek+read
  bookkeeping, and on-device zero-copy is impossible — tiles must be *copied* into RAM,
  which changes the rendering architecture, not just the I/O calls.
- **TouchGFX device-draw risk is live, and the only mitigation is compiled out on
  hardware.** The Y-clipping defect is documented as reproducing on STM32U595 hardware
  (`Docs/touchgfx-drawpartialbitmap-y-clipping-bug-report.md:36` @ `ff098549`), yet
  `DynamicBitmapDraw.hpp`'s workaround is gated `#if defined(SIMULATOR) && defined(__linux__)`
  (line 110) and addresses only the negative-X defect. RawTilesMap dodges both only because
  `TileCanvas` hand-pre-clips and never calls `drawPartialBitmap` — that discipline is
  currently folklore in one widget, not an SDK guarantee.

### Missing entirely (CONFIRMED)

- **Writer:** `slippypack` — named as the pack's generator in commit `8897daf7` — exists
  nowhere (branch, main, `$PATH`, public web). The conformance generators hand-assemble
  fixtures and contain no PNG decode, no RGB565 quantiser, and no RLE encoder; a real
  writer is ~40% seeded (byte assembly + Appendix A UUID pipeline are done). The sample
  pack is an unreproducible blob.
- **Deployment/discovery:** the `.uapp` container is five fixed slots (header, two icons,
  service ELF, GUI ELF) with no data slot; `RESOURCES_PATH` feeds exactly two hardcoded
  icon filenames; deploy is USB hand-copy; there are zero volume-path literals and zero
  `.rawtiles` mentions on `main`. A discovery convention must be invented, not adopted.
- **Tests and CI:** zero host tests touch the Container (the branch's only test additions
  are two byte-identical-except-strings ActivityWriter suites); `rawtiles_check` is wired
  into no CI on either tree and needs a `.hashes` sidecar no in-tree tool can produce.
- **Spec currency:** the Container is pinned to v0.2 but is *functionally v0.5-structural*:
  the 22 negative-corpus failures are all wrong ACCEPTs confined to extension-payload
  validation (AFFN #22/#34/#35/#36, NAME #26/#29b/#37, duplicate-tag #29a, padding #33,
  SRCD/ATTR #38a–i) — it validates framing and tag syntax but never payload contents. It
  also rejects v0.6-valid RGB565/RLE packs outright (fail-closed; `Container.cpp:210,339`).
  The one silent under-enforcement a v0.2 pin causes is #38(b)(c); everything else fails
  safe.

## 3. Spec assessment

**Ruling: state-of-the-art for its niche — genuinely novel, not merely unusual — with
localized, fixable debt.** (CONFIRMED via ecosystem survey; sources in
`findings/ecosystem.md`.) Raw-pixel MCU map tiles are a recurring *pattern* (an LVGL
RGB565-from-SD component, Bangle.js openstmap's pre-dithered images, the ESP32 PMTiles
project's RGB565 SD cache) but nobody has published a specified container: header, flat
index, canonical quantisers, canonical RLE, CRC, golden + negative corpus. The
alternatives all fail this device class: PMTiles v3 needs inflate + varints + Hilbert IDs
+ 12–18 KB directory RAM and still delivers payloads needing an image decoder — and the
only known MCU PMTiles consumer caches rendered tiles to SD as raw RGB565, independently
converging on rawtiles' model; MBTiles means ~590 KB of SQLite (zero MCU precedent — even
Meshtastic chose loose PNGs); Garmin's KMZ proves raster-on-wearable ships commercially
but caps at a 100-JPEG index, and JNX is the same header+index+raster shape locked behind
DRM. Vector on MCU is demonstrated-possible and consistently not chosen. Raster-first is
the correct altitude for "users bring anything": any source flattens to pixels offline,
and the codepoint space (`pixel_format`/`compression`) leaves a clean door for future
profiles without a major-version break.

**The three design decisions that earn the ruling:**
1. Framebuffer-native pixels behind a flat 20-byte-stride index — zero decode, O(1)-RAM
   binary search over pread, no library dependencies. (The honest version of the pitch:
   TJpgDec does JPEG in ~3.5–8.5 KB ROM, so the win is RAM — PNG wants ≥48 KB free — and
   per-pan CPU, not flash.)
2. The eager, overflow-conscious 38-rule validation model with a traceable ~1 KB
   structural-state budget — empirically vindicated by the zero-crash fuzz run against an
   implementation that never read the reference C.
3. Pixel-level RLE with ≤8 B decoder state and a canonical encoder — the single feature
   that makes both budgets work (156 KiB RAM working set; a 10×10 km z15 layer at 1.9 MiB
   *just* fits NOR, and metro-scale packs fit eMMC).

**Debts that would block v1.0** (all CONFIRMED in `findings/spec-audit.md`):
1. **§14.3/§14.5 claim RGB565/RLE golden fixtures that do not exist** — the entire
   v0.4+v0.6 raison d'être has zero conformance coverage, and no RLE encoder exists in any
   repo to produce them. Worst single defect in the project.
2. **The reproducibility headline overclaims.** Appendix A promises "byte-identical across
   implementations" while §4.9 grants ±1 µ° libm drift into `bbox` — a pack_uuid input —
   and §A.4 leaves the decode/resample pipeline implementation-defined. The real contract
   is same-writer determinism; the spec should say so. For the actual use case (a user
   side-loading one pack) the machinery is not load-bearing — `supersedes_uuid` dedup is a
   nice-to-have, so this is honesty debt, not design debt.
3. **Smaller:** the v0.6 changelog's "stale fixtures" note is itself stale (neg-07b/g were
   fixed one minute before the 0.6 commit); §14.6's manifest-flag MUST for neg-33 is
   unmet; §9.11 never requires exact consumption of the encoded stream, so two conforming
   readers can disagree on a pack's validity (the reference reader is stricter than the
   letter); the reference reader's "v0.6-conformant" headline sits atop 13 must-reject
   accepts (disclosed as planned in its own table).
4. **Limits:** u32 offsets cap ~121 k RGB565-RLE tiles (a country at z16 or metro z18
   breaks it — irrelevant to watches, relevant to the spec's own kiosk audience; no
   sharding story). SingleImage's W=H square requirement is the sharper 16-bit constraint.
   24 zooms and microdegree bbox never bind. The §9.1.1/§9.2.1 quantisers ARE airtight
   (integer-only, test-vectored) — two independent writers will agree given identical
   RGB888 input.

**The honest stabilization criterion:** the current gate ("a second independent consumer",
`rawtiles.md:9`) is unsatisfiable in spirit — both consumers are the same author, and the
repo has 0 stars/forks/issues/watchers after 2.5 months of silence (six spec versions in
four days, then nothing since 2026-05-18), so no external consumer is coming
uninvited. Propose instead: v1.0 when (i) the corpus actually covers every legal
`pixel_format × compression` cell including per-tile hash tables, (ii) one reader written
*from the spec text alone* by someone who has not read the reference C passes the full
corpus, (iii) a second writer reproduces `golden-canonical-uuid` byte-identically, and
(iv) the una-sdk Container — same author but a genuinely separate codebase, which E1
showed catches real divergence (its wrong-accept set strictly contains the reference
reader's) — passes as corroborating, not qualifying, evidence.

## 4. Interface ruling

The end goal's two halves resolve cleanly **iff the external contract is the pack file
plus a path convention, and nothing else**. Today nothing leaks — because nothing exists;
the contract can be drawn correctly from scratch:

1. **User contract (freeze at v1.0, never after):** the `.rawtiles` wire format + a
   documented convention — `1:/maps/*.rawtiles` on the eMMC user volume (packs don't fit
   NOR; E4 arithmetic), enumerated via `IFileSystem::readNext`. The `RAWTILES_PATH` env
   var and candidate-path walk are simulator dev conveniences; they must stay out of every
   doc that describes the user contract. The constraints envelope must be *written down*
   as part of it, or users discover it by failure: WebMercator/Quadtree or
   SingleImage+AFFN; ABGR2222 (+RGB565/RLE once the reader is v0.6-current);
   `tile_dim_px = 256` recommended; useful zoom ≤ 16 (float GPS resolves 0.36 px at z16,
   1.4 px at z18 — CONFIRMED arithmetic, and the GPS stack is structurally float with no
   double lane in the sensor union); pack ≤ 4 GB; edge behaviour = slate sentinel.
2. **SDK surface (UNA iterates freely behind it):** one class returning raw tile bytes is
   too little — every map app would re-derive the five hard-won lessons fossilized in the
   experiments lineage (negative-anchor SIGSEGV, bitmap-origin anchoring, seam-centre
   selection, dynamic-bitmap rotation, the blitCopy pivot) plus the cache-slot-count
   footgun. UNA's half of the goal requires: (i) the streaming Container (open/validate
   over `IFileSystem`, per-tile or per-row reads into caller buffers), (ii) a
   lat/lon→(z,x,y)→screen transform utility (float; ~30 lines; its absence is why the
   tutorial isn't a map), (iii) an SDK `TileCanvas`-equivalent widget owning the mosaic,
   pre-clipping, and TouchGFX-workaround discipline, and (iv) a pack
   discovery/enumeration helper. `TrackMapBuilder` composes rather than competes — it is
   an overlay source (breadcrumbs, `uint8_t` screen points) over the tile basemap; the
   shared geo→screen transform in (ii) is the composition point neither currently has.
3. **Convention now vs later:** now — path + enumeration + NAME tag for a picker UI
   (reader-side NAME/ATTR validation is also where the Container's 22 conformance
   failures live, so this work pays twice). Later — `supersedes_uuid` updates, multi-pack
   stitching, ATTR display obligations.
4. **The format's TLV extension mechanism (lowercase app-private tags) is the right
   division of labour** with the SDK's compile-time seams: user data rides *in the pack*,
   so third-party packs can carry app-private payloads without UNA changes — coherent,
   PLAUSIBLE until a real second app exercises it.
5. **Authoring:** until a writer is published, the honest status is **open in principle,
   closed in practice** — the openness claim currently rests on an unpublished script.
   Minimum viable: slippy-dir/PNG → ABGR2222(+RLE) pack, living in the rawtiles repo
   (una-sdk must not own the format's tooling), seeded from the generators + the §A
   pipeline; the missing 60% is PNG ingest, the RGB565 quantiser, and the canonical RLE
   encoder.

## 5. The next step

**B before A — a device spike first — because device feasibility is unproven and its
findings dictate the API shape A would otherwise freeze wrong.** The simulator proof
deliberately exploited two things hardware forbids: whole-pack RAM residency and zero-copy
bitmaps over pack memory (no mmap on FatFs/eMMC — every tile must be copied). The device
port is therefore an architecture change, and three unknowns gate its design: eMMC read
latency through `IFileSystem` (sizes the tile cache and pan strategy), `blitCopy`
behaviour on the hardware LCD path (the sibling drawPartialBitmap defects are confirmed
live on STM32U595; blitCopy has only ever been validated in the Linux simulator's prebuilt
lib), and the real RAM headroom of a GUI process holding a 4×64 KiB tile cache plus
TouchGFX heap. One week, one tile, evidence per the investigation convention. If the spike
fails, everything downstream re-plans; if it succeeds, its numbers are the constraints
envelope's first real entries.

The ordered stack, respecting one-reason-per-branch:

0. **Spec debt, in the rawtiles repo, in parallel** (no una-sdk branch): write the
   canonical RLE encoder in the generators, ship the missing RGB565/RLE goldens §14.5
   promises, fix the stale v0.6 changelog note, the §14.6 manifest flag, the §9.11
   exact-consumption rule, and reword Appendix A to same-writer determinism. Cheap, and it
   defines the v0.6 target the SDK reader must hit.
1. **`spike/rawtiles-device-proof`** (B): hand-copied pack to `1:/maps/`, minimal
   `IFileSystem` read of one tile into a static buffer, blit on hardware. Investigation
   bundle, not a PR — its README feeds PRs 2–4.
2. **Rebase + land the base:** `feat/gpstrack-tutorial` first (the RawTilesMap branch
   stacks on its own divergent copy of GpsTrack, which is not upstream; both are stale vs
   the SDK::Fit migration), then rebase the map work onto it.
3. **`feat/rawtiles-container` PR** (A): Container to v0.6 (fix the 22 extension-payload
   accepts; add RGB565 + RLE with row-streaming decode; the racy CRC-table init;
   `IFileSystem` open + tile-read API shaped by the spike), gtest host tests with the
   vendored conformance corpus wired into `Tests/Host` (vendor the fixtures — 96 small
   files; a network fetch in CI is a flake source), `rawtiles_check` retired or demoted to
   a wrapper. This is the contract-shaped, ~6-file-class PR that this repo's review
   culture merges fast — and the maintainer will correctly treat the wire-format
   dependency as the irreversibility item, so PR 0's v1.0 path must be answerable by then.
4. **Writer + deployment convention** (D): publish the writer in the rawtiles repo;
   document `1:/maps/` + the constraints envelope in una-sdk `Docs/`.
5. **`feat/rawtilesmap-tutorial` PR** (E): rebased, Makefile ported to SDK::Fit (+
   `ImuRunningCadence`, `app.mk`), ARCHITECTURE.md actually written, tutorials registered
   in `Docs/index.rst`, GPS-following viewport via the new transform utility (or honestly
   reframed as a pack viewer), investigation bundle relocated. Self-verification in the PR
   body per the #171 pattern, since it will exceed CodeRabbit's file limit.

What each deferral costs: C-first would polish a spec nobody can feed (D's writer is
needed for C's fixtures anyway — they meet in the RLE encoder); D-first publishes a writer
whose output no device can display; E-first asks upstream to review an API the device
spike may invalidate. The chosen order retires risk in strictly decreasing size:
concept-on-hardware → wire-format currency → openness → polish.

## 6. Spin off separately

| Finding | Where it belongs |
|---|---|
| Y-clipping workaround gated out on the platform the bug is confirmed on; nothing mitigates Y-overhang anywhere; helper hard-codes ABGR2222 | `docs/touchgfx-drawpartialbitmap-y-clipping-bug-report` follow-up (extend the gate per its own README; consider an SDK-level safe-draw wrapper) |
| GpsTrack/RawTilesMap `ActivityWriter` + tests are byte-identical copies | the existing `refactor/sdk-activity-writer` branch |
| `Commands.hpp:53-55` double-width lat/lon IPC fields feeding float endpoints (soft-float on `-mfpu=fpv5-sp-d16` too) | small `refactor/` on the tutorial stack |
| `architecture-deep-dive.md` hardware diagram contradicts port doc + code (320×300 RGB, GT911); "256KB" Mermaid label undocumented; fourth volume `3:/` missing from prose | own `docs/` fix branch |
| `SensorDataParserGpsLocation.hpp:42-43` enum comments say metres for lat/lon | fold into any sensors docs branch |
| Duplicate MyST anchor `(tutorials/sensors/architecture)=` in three files; `docs.yml` lacks `-W` so orphan pages publish silently | `docs/` branch (the anchor fix rides the tutorial rebase) |
| `rawtiles_check`'s inlined SHA-256 + sidecar design | superseded by PR 3's gtest harness; delete rather than maintain |
| Reference-reader README: "v0.6-conformant" headline vs 13 planned-gap accepts; §14.5 row says planned but is implemented | rawtiles repo, rides PR 0 |

## 7. Questions only the maintainer can answer

1. **eMMC read performance through `IFileSystem` on the real watch** — a 64 KiB tile read
   in ~10 ms vs ~200 ms is the difference between read-on-pan and background prefetch with
   a bigger cache; nothing in the repo measures it, and the spike's design starts here.
2. **Would UNA accept an SDK module whose wire format is specified in a personal external
   repo** — or does landing require vendoring the spec + conformance corpus (a
   `ThirdParty/`-style snapshot) or moving the spec under a neutral org first?
3. **Is a second implementation target worth courting for the v1.0 gate** (the v0.4
   changelog names the InfiniTime/PineTime class), or should v1.0 be redefined as §3(iv)
   proposes and frozen on UNA's shipping schedule alone?
4. **Is the 600 K GUI `RAM_LENGTH` negotiable per-app** (it is a cmake `defsym`, technically
   overridable) — i.e., may a map app buy a bigger tile cache, or is 600 K a firm platform
   contract?

---

## Addendum, 2026-08-05 (later): the § 5 device spike ran — results and what they change

`spike/rawtiles-device-proof` executed on the physical watch (bundle with photos and UART
trace: `Docs/Investigations/2026-08-05-rawtiles-device-proof/` on that branch). H1–H4
CONFIRMED; H5 answered in a way that overturns part of § 4.

**The concept survives hardware, decisively.** The GUI process reads packs directly
through `IFileSystem`; a 64 KiB tile costs **7–9 ms cold, 6–9 ms warm** (7 reruns,
CRC byte-exact every time), so refilling all four visible tiles is ~30 ms and a full 3×3
prefetch ~70 ms — **read-on-pan is viable with raw, uncompressed tiles**. This demotes
RLE from RAM-enabler to flash-capacity feature: the device path can ship v1 with
`Compression::None` + a 2–4-tile cache, and add RLE for pack size later. One caveat: the
first filesystem touch after app start cost ~113 ms once (4 ms thereafter) — open the
pack at app start, never lazily on first pan. And `blitCopy` renders correctly at every
overhang geometry including the exact (60,220) case that breaks `drawPartialBitmap` —
the TileCanvas discipline is validated as *the* device render path and should become the
SDK widget's.

**§ 4's path convention is overturned.** No absolute `N:/...` path resolves from an app
on hardware — apps see a per-app sandbox, and the one convention that works (borrowed
from watch-apps/Barcode) is a **bare relative path resolving to the app's own folder**,
which USB mass storage and BLE file transfer both address as `Apps/<App>/`. So the user
contract becomes *simpler*: "drop `*.rawtiles` next to the app" — no path configuration
at all, and discovery is enumerating the app's own sandbox. The cost: **cross-app pack
sharing is impossible without kernel support**, which becomes a new top maintainer
question — would UNA expose a shared, read-only `maps/` mount into app sandboxes, or is
per-app pack duplication acceptable? Until answered, the SDK discovery helper should
take a directory handle rather than hard-code any location.

**Two device-wide operational findings** (bundle §§ Results 1 and 4, both spin-off
worthy): (a) `main`'s `KERNEL_INTERFACE_VERSION` 2→3 bump (`68676e7c`) makes every app
built from current `main` fail on v2-firmware watches as an instant, unlogged `App PID`
fault — the spike pinned v2 locally; device verification on this watch needs that pin
until the firmware updates, and the version gate deserves a visible error. (b) USB-MSC
writes while the watch BLE-syncs are two uncoordinated writers on one exFAT partition
and corrupted files mid-session (same size, same mtime, different bytes — likely the
root cause of `Update-Watch-Apps.ps1`'s long-standing "silent bit-level corruption"
warning); `Docs/deploy.md` should say "disable BLE sync while copying". Also: the round
bezel clips the square framebuffer's corners — map pixels don't care, but any text/HUD
in a future map UI must fit the inscribed circle.

**Plan impact:** § 5's ordering stands, step 1 is done and green. Step 3 (the Container
PR) now has concrete design inputs: sandbox-relative discovery, open-at-start,
single-`read()` 64 KiB tile fetches, small raw-tile cache, blitCopy-only rendering, and
RLE as a follow-on rather than a prerequisite.
