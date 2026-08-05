# Prompt: Build `feat/rawtiles-container` — the streaming, v0.6-conformant, tested SDK reader

You are implementing the first upstream-bound piece of the una-sdk map plan: a
production-grade `SDK::RawTiles::Container`. This is the "conformance first" step of
`RAWTILES_MAP_EVALUATION.md` § 5 (read it, including the 2026-08-05 device addendum — it is
the design authority for this work), now unblocked because the device spike retired every
feasibility unknown. Your deliverable is a branch ready to open as a PR, not an opened PR.

The bar: this reader's public API and the `.rawtiles` wire format are the two things that
freeze forever once an app ships against them. The maintainer who will review this
(rryles) verifies claims empirically and blocks on exactly one theme — "after something
ships against this, changing it is a silent break." Design like he's watching.

---

## 0. Ground rules (hard constraints)

- **Never post anything to GitHub** — no PRs, no comments, no issues, on any repo. `gh` is
  read-only. You prepare the branch and a PR-description file; the maintainer opens the PR.
- **One reason to merge.** This branch is the Container + its tests + its cmake wiring.
  No tutorial, no widget, no discovery helper, no doc rewrites beyond the module's own.
  Real findings outside that scope go in a spin-off list with their target branch.
- Base on `upstream/main` (== `origin/main`; UNAWatch/una-sdk is `upstream`, the fork is
  `origin`). Branch name: `feat/rawtiles-container`. Commits: terse, mostly *why*; amend
  rather than stack fixups pre-review.
- House evidence convention: anything you settle by experiment goes in
  `Docs/Investigations/<date>-<slug>/` (hypothesis / method / log / verdict, failures
  included). Cite `file:line` for claims about existing code.
- Verify, don't trust this prompt: every fact below was true on 2026-08-05; re-check
  anything load-bearing before building on it.

## 1. Where the truth lives (read before writing code)

- `RAWTILES_MAP_EVALUATION.md` (repo root, untracked) — full evaluation + device addendum.
- Evidence bundles: `Docs/Investigations/2026-08-05-rawtiles-map-evaluation/` (main
  worktree, untracked — conformance matrices, fuzz logs, audits) and
  `Docs/Investigations/2026-08-05-rawtiles-device-proof/` (committed on
  `origin/spike/rawtiles-device-proof` — device photos, timings, the probe code).
- The existing reader: `feat/rawtilesmap-tutorial` carries
  `Libs/Header/SDK/RawTiles/Container.hpp` (249 lines) +
  `Libs/Source/RawTiles/Container.cpp` (629 lines). **Port it, don't rewrite it** — its
  validation walk is the best-audited code in this effort (17/17 golden fixtures with all
  70 tile hashes, zero crashes across 2,052 ASan/UBSan mutations; every served pointer
  pre-validated with overflow-safe u64 arithmetic, `Container.cpp:281-284,382,386-394`).
  Its defects are enumerated in § 3 below.
- The spec: clone `https://github.com/tobymurray/rawtiles` (HEAD was `38d4d26`, spec v0.6,
  wire `(1,0)`, status Provisional). `spec/rawtiles.md` is normative;
  `spec/conformance/{golden,negative}/` holds 96 committed fixtures (17 golden + 79
  negative; `verify.py` self-checks them); `reference-reader/` is a C99 reader whose
  harness (`tests/conformance.c`) hand-compiles with plain clang — no cmake needed:
  `clang -std=c99 -O2 -Iinclude src/rawtiles.c tests/conformance.c`.
- The sample pack: `feat/rawtilesmap-tutorial:Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles`
  (787,008 B; 12 tiles z12–14; first z14 tile x2587 y5604 at offset 393748, CRC-32
  `0x26E5979B`; `zoom_offsets[].offset` is an **absolute byte offset** into the file).

## 2. Design inputs that are now facts (from the device spike — do not re-litigate)

| Fact | Design consequence |
|---|---|
| 64 KiB tile read via `IFileSystem`: 7–9 ms cold, 6–9 ms warm (7 runs, CRC byte-exact) | Read-on-pan with raw tiles is viable. RLE is a flash-capacity feature, not a RAM/latency prerequisite — it may land in this PR or a follow-up, your call, but `None` must be first-class. |
| First filesystem touch after app start ≈ 113 ms, then ~4 ms | The API must make open-at-start natural; document it. |
| Apps see a **sandbox-relative** filesystem; absolute `N:/...` paths never resolve on hardware | No path conventions inside the Container. It takes what the caller gives it. |
| No mmap; tiles must be **copied** into caller-owned buffers; GUI RAM budget 600 K (`una-app.cmake:281`) | Whole-pack `std::vector` residency is banned. Caller-owned buffers, zero heap surprises. |
| `IFile` is absolute-`seek(size_t)` + `read(char*, size_t, size_t&)`, no pread (`Libs/Header/SDK/Interfaces/IFileSystem.hpp:213,:229`); `IFileSystem::file(path)` returns `unique_ptr<IFile>` (`:63`) | The streaming backend does seek+read bookkeeping over one handle. |
| `LCD::blitCopy(const uint8_t*, Bitmap::ABGR2222, source, blitRect, alpha, false)` is the validated render path | Not this PR's code, but `getTile`-style access must hand back exactly what blitCopy consumes: contiguous ABGR2222 rows. |
| Kernel v2/v3 gate: apps from current `main` fault instantly on v2-firmware watches (`68676e7c`) | Irrelevant to host tests/CI; only matters if you do the optional on-device re-verify (pin `KERNEL_INTERFACE_VERSION (2)` on a throwaway branch, never on this one). |

## 3. What must change from the tutorial-branch Container

Confirmed defects/gaps to fix (each verified 2026-08-05; matrices in the evaluation bundle):

1. **I/O model**: `std::fopen` + whole-pack `std::vector` (`Container.cpp:110,133-134`).
   Replace with a two-backend design — memory view (host tests, `openFromMemory`) and
   `IFile` streaming (device). Eager open-time validation must run in bounded memory per
   spec § 11.1–11.2 (~1 KB structural state: resident 292 B header, one 20 B entry +
   previous-entry key for the ordering walk, one u32 padded-sum accumulator, streamed CRC).
   The whole-index-resident shortcut is acceptable only if you cap and justify it
   (24-entry zoom directory + binary search per § 5.3 needs no resident index at all).
2. **22 wrong-accepts, all extension-payload validation** (the Container validates framing
   `#19` and tag syntax `#20/#27/#28` but never section contents): AFFN `#22/#34/#35/#36`
   (missing-when-LocalLinear, length ≠ 48, non-finite coefficients, present-with-WebMercator),
   NAME `#26a/#26b/#29b/#37a-c` (empty payload, tag overrun, duplicate locale, UTF-8/BCP-47),
   duplicate uppercase tag `#29a`, non-zero section padding `#33`, SRCD/ATTR text rules
   `#38a-i`. Note the C99 reference reader itself still misses 13 of these (29b, 37a-c,
   38a-i) — passing all 79 negatives makes this SDK reader the **most conformant rawtiles
   reader in existence**, which is exactly the second-consumer evidence the spec needs.
3. **Spec currency**: pinned to v0.2. v0.6 legalises `pixel_format 2` (RGB565, LE,
   2 B/px) and `compression 1` (pixel-level RLE) — enum values that were *reserved-reject*
   in ≤0.3 (old fixtures renamed `neg-07b-pixfmt-5`, `neg-07g-comp-3`). RLE per § 9.11:
   1-byte header `H`; bit7=0 → literal run of `(H&0x7F)+1` pixels; bit7=1 → repeat run,
   payload one P-byte pixel; runs never split pixels; decode stops at exactly
   `tile_dim² · P` output bytes; ≤ 8 B decoder state; runs cross row boundaries, so a
   row-streaming decode suspends mid-run (repeat pixel + remaining count). Beware: § 9.11
   does not explicitly require the encoded stream be consumed exactly — the reference
   reader rejects trailing bytes (`src/rawtiles.c:881`); match the strict behaviour and
   flag the spec gap in the PR description.
4. **Racy CRC table init**: plain `static bool tableReady` guard (`Container.cpp:77-88`) —
   formally a data race. Magic static or `constexpr` table.
5. **Docstring drift**: header claims rules `#1–#19,#25,#30,#31` enforced but the code
   also enforces `#20,#23,#24,#27,#28,#32`; `describeResult(BadEnum)` mislabels the
   per-tile compression reject. Make the docs match the code you ship.
6. **Missing test coverage**: zero host tests exist. This PR's centrepiece: gtest suites
   in `Tests/Host/` running the **vendored** conformance corpus (commit the 96 fixtures +
   `manifest.json` under something like `Tests/Host/rawtiles/corpus/` — a CI network fetch
   is a flake source; record the upstream commit hash for refresh). Assert per-fixture
   accept/reject, golden tile-hash verification (§ 14.5 sidecars), plus targeted unit
   tests for the RLE decoder (spec § 14.8 cases) and quantiser-independent paths. Wire
   into `Tests/Host/CMakeLists.txt`.

**Corpus gap you must handle**: the corpus contains **zero RGB565 or RLE fixtures**
(spec §§ 14.3/14.5 claim them; they don't exist — worst known spec defect). If you
implement RGB565/RLE, generate your own fixtures with a small Python script that follows
spec § 9 byte-for-byte (the corpus generators in `spec/conformance/generators/` show the
pack-assembly idiom; there is no RLE encoder anywhere — you'll write the first). Ship the
script + fixtures in-tree, label them "local fixtures pending upstream corpus", and list
upstreaming them to the rawtiles repo as a spin-off. If instead you defer RGB565/RLE to a
follow-up branch, the reader must still **reject** those enum values cleanly (fail-closed,
as today) and the API must not need breaking to add them later.

## 4. API sketch (starting point, not a straitjacket)

Keep the good bones: 24-value `OpenResult` with § 11 rule citations, `describeResult`,
`Header` accessor, `getTile(z,x,y)` binary search with the `z >= 24` guard. Change the
shape to match the facts in § 2 — something like:

```cpp
OpenResult openFromMemory(const uint8_t* data, size_t size);          // borrows, host tests
OpenResult openFromFile(SDK::Interface::IFileSystem& fs, const char* path); // owns unique_ptr<IFile>
TileInfo   findTile(uint8_t z, uint32_t x, uint32_t y) const;         // index lookup only, no I/O
ReadResult readTile(const TileInfo&, uint8_t* dst, size_t dstSize);   // full tile into caller buffer
ReadResult readTileRows(const TileInfo&, uint16_t firstRow, uint16_t rowCount,
                        uint8_t* dst, size_t dstSize);                // streaming/RLE-suspend path
```

Decisions to make deliberately (and defend in the PR description): does `open` validate
eagerly in full (recommended — the spike showed open cost is a non-issue) or offer the
spec's lazy profile; does `readTile` re-verify the § 14.5 hash (no — that's a validator's
job, not a renderer's); what the move/copy semantics are with an owned `IFile`. Follow
`Libs/` idiom for naming and error style — read `SDK::Fit::FitWriter` and the RR_INTERVAL
parser (PR #220) as the house exemplars of contract code, including how #220's review
treated field conventions and "decide-before-ship" ABI items.

## 5. Verification (do not skip; do not fake — report what actually ran)

No cmake on this Mac; docker at `/usr/local/bin/docker`.

1. **Host tests**: `git archive` the branch to a scratch tree, copy
   `ThirdParty/coreJSON/source` in (git archive skips submodules), then
   `docker run --rm -v tree:/src -w /src ubuntu:24.04` → apt cmake/g++ →
   `cmake -S Tests/Host -B /tmp/b && cmake --build /tmp/b && ctest --output-on-failure`.
   ctest hides gtest suites — check `--gtest_list_tests` output that your suites are
   actually compiled in; a green ctest alone proves nothing.
2. **Conformance matrix**: run your reader against all 96 fixtures; produce the same
   accept/reject table the evaluation produced (bundle has the format). Target:
   17/17 golden + hashes, 79/79 negatives. Run the C99 reference reader side-by-side and
   report where you're stricter (expected: the 13 NAME/ATTR cases + neg-18d rule code).
3. **Fuzz**: rebuild the ASan+UBSan fuzz loop from the evaluation bundle
   (`Docs/Investigations/2026-08-05-rawtiles-map-evaluation/conformance/fuzz.py` is the
   seed script) against the new reader — targeted mutations + ≥2000 random iterations,
   zero tolerance for crashes/reports. New I/O layer = new attack surface: add
   truncation-mid-`readTileRows` and short-read (`br < btr`) cases the old whole-pack
   reader couldn't hit.
4. **ARM compile**: the module must build under `arm-none-eabi` —
   `docker run --rm --platform linux/amd64 -v tree:/src -e UNA_SDK=/src una-armgcc:latest`
   building any Examples app with `UNA_SDK_SOURCES_RAWTILES` added to its GUI sources
   (upstream/main has the `-fcyclomatic-complexity` probe now; no flag workaround needed).
   No exceptions/RTTI surprises, no heap in the streaming path.
5. **Optional but high-value**: rebase the spike's probe (`origin/spike/rawtiles-device-proof`)
   onto the new API on a throwaway branch and confirm the same numbers on the watch
   (needs the `KERNEL_INTERFACE_VERSION (2)` pin there; **disable BLE sync before any
   USB copy** — two writers on that exFAT corrupt files, learned the hard way).

## 6. Deliverables

1. Branch `feat/rawtiles-container` off `upstream/main`, pushed to `origin` (plain push,
   never force — other sessions share these branches).
2. `tmp/rawtiles-container-pr-description.md` (or a `tmp/…` branch, matching the
   `tmp/rr-interval-pr-description` precedent): PR body with the #171-style
   self-verification story — conformance matrix, fuzz numbers, host-test count, ARM build
   proof, device numbers if step 5.5 ran — plus the open design questions you want
   review on, stated the way #220 stated its (each with your recommendation). Address the
   Provisional-spec question head-on: the wire format is v0.6 `(1,0)`, the v1.0 gate and
   this reader's role as validating consumer, and why the API is safe to freeze anyway.
3. Investigation bundle for anything experiment-settled.
4. Spin-off list: at minimum expect entries for upstreaming RLE/RGB565 fixtures + the
   § 9.11 exact-consumption wording to the rawtiles repo, the kernel-version-gate error
   surfacing, and the `Docs/deploy.md` BLE/USB warning. Do not fold any of them in.

**What "done" looks like:** a ~contract-sized diff (`Libs/Header/SDK/RawTiles/`,
`Libs/Source/RawTiles/`, `Tests/Host/rawtiles/`, `cmake/una-sdk.cmake`) where every § 11
rule the docs claim is enforced has a fixture proving it, every fixture result is
reproducible with one documented docker command, and a reviewer who trusts nothing can
re-run the whole story in twenty minutes.
