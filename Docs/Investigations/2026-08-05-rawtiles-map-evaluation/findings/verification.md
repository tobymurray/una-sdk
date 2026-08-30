# RawTiles verification report — 2026-08-05

Verification leg of the rawtiles evaluation. All work done under the scratchpad;
neither /Users/tobymurray/git/una-sdk nor the worktree was modified.

- SDK worktree (feat/rawtilesmap-tutorial): `scratchpad/wt-rawtiles`
- rawtiles repo (spec v0.6 @ 38d4d26 "conformance: bump manifest SPEC_VERSION to 0.6"): `scratchpad/rawtiles`
- Build area: `scratchpad/build` (sources copied out of the worktree; nothing built in-tree)

## 0. Corpus provenance

`spec/conformance/{golden,negative}` contain **committed** fixtures — no generation
needed. `python3 verify.py` (corpus self-check against manifest.json SHA-256s):
**PASS: 96 of 96 fixtures clean** (17 golden + 79 negative, manifest spec_version 0.6).

Note on the brief's attention items: fixtures named `neg-07b-pixfmt-2` and
`neg-07g-comp-1` **do not exist** in the v0.6 corpus. Those are the pre-v0.4 names.
v0.4 reassigned enum values (pixel_format 2 = RGB565, compression 1 = RLE became
*valid*), and the reserved-value fixtures moved with the reservations:
they are now `neg-07b-pixfmt-5` (L4 indexed, reserved) and `neg-07g-comp-3`
(LZ4, reserved). Both are present, both were run, both are correctly rejected by
both readers (see matrices). Generator comments confirming the rename:
`spec/conformance/generators/neg_07_reserved_enums.py` lines 25–35.

## 1. Builds

| Artifact | Command | Result |
|---|---|---|
| `build/rawtiles_check` (plain) | `clang++ -std=c++17 -O2 -Wall -Wextra -I src src/main.cpp src/Container.cpp` | clean, no warnings |
| `build/rawtiles_check_asan` | same + `-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all` | clean, no warnings |
| `build/rawtiles_conformance` (C99 reference reader + harness) | `clang -std=c99 -O2 -Wall -Wextra -Wpedantic -Iinclude src/rawtiles.c tests/conformance.c` | clean, no warnings |
| `build/refdump` (12-line header-dump driver over the reference reader API) | `clang -std=c99 -O2 ... refdump.c src/rawtiles.c` | clean |

Sources compiled: worktree `Utilities/Scripts/rawtiles_check/main.cpp` +
`Libs/Source/RawTiles/Container.cpp` + `Libs/Header/SDK/RawTiles/Container.hpp`
(copied verbatim to `build/src/`). No cmake was needed anywhere (docker unused);
the reference reader's README documents the direct-cc invocation used.

Apple clang 21.0.0, arm64-apple-darwin25.6.0.

## 2. Container (SDK C++) — TRUE conformance matrix

Driver contract: exit 0 = accept (and, for goldens, all § 14.5 tile hashes match),
exit 1 = reject/fail. Goldens were run WITH their `.hashes` sidecars.
ASan+UBSan build re-run over the whole corpus: **identical verdicts, zero sanitizer
reports**. Full log: `build/container_matrix.txt`.

**Golden: 17/17 accepted, all 70 tile hashes matched. Negative: 57/79 correctly
rejected, 22 wrongly accepted.**

| Fixture | Expected | Container actual | Container result code |
|---|---|---|---|
| golden-smallest | accept | accept | ok (1/1 hashes) |
| golden-grid | accept | accept | ok (16/16 hashes) |
| golden-empty-quadtree | accept | accept | ok |
| golden-singleimage-affn | accept | accept | ok (1/1) |
| golden-attr | accept | accept | ok (1/1) |
| golden-orientation | accept | accept | ok (1/1) |
| golden-orientation-mosaic | accept | accept | ok (16/16) |
| golden-minor-1 | accept | accept | ok (1/1) |
| golden-ancillary-tag | accept | accept | ok (1/1) |
| golden-supersedes | accept | accept | ok (1/1) |
| golden-pyramid | accept | accept | ok (21/21) |
| golden-tms | accept | accept | ok (1/1) |
| golden-zmax | accept | accept | ok (1/1) |
| golden-names-multilocale | accept | accept | ok (1/1) |
| golden-canonical-uuid | accept | accept | ok (1/1) |
| golden-png-to-pack-1tile | accept | accept | ok (1/1) |
| golden-png-to-pack-5tiles | accept | accept | ok (5/5) |
| neg-01-short-file | reject | reject | FileTooShort (§11 #1) |
| neg-02-bad-magic | reject | reject | BadMagic (§11 #2) |
| neg-03-major-2 | reject | reject | BadVersion (§11 #3) |
| neg-05-uuid-zero | reject | reject | BadUuid (§11 #5/#6) |
| neg-06-parent-nonzero | reject | reject | BadUuid (§11 #5/#6) |
| neg-07a-pixfmt-0 | reject | reject | BadEnum (§11 #7) |
| **neg-07b-pixfmt-5** | reject | **reject** | BadEnum (§11 #7) |
| neg-07c-proj-0 | reject | reject | BadEnum |
| neg-07d-proj-2 | reject | reject | BadEnum |
| neg-07e-addr-0 | reject | reject | BadEnum |
| neg-07f-axis-0 | reject | reject | BadEnum |
| **neg-07g-comp-3** | reject | **reject** | BadEnum (§11 #7) |
| neg-08a-webmerc-singleimage | reject | reject | BadEnumPair (§11 #8) |
| neg-08b-locallinear-quadtree | reject | reject | BadEnumPair |
| neg-09-tiledim-zero | reject | reject | BadDimensions (§11 #9) |
| neg-10a-zmax-24 | reject | reject | BadZoomRange (§11 #10) |
| neg-10b-zmin-gt-zmax | reject | reject | BadZoomRange |
| neg-11a-lon-overflow | reject | reject | BadBbox (§11 #11) |
| neg-11b-lat-overflow | reject | reject | BadBbox |
| neg-11c-lon-inverted | reject | reject | BadBbox |
| neg-11d-lat-inverted | reject | reject | BadBbox |
| neg-12a-flags-nonzero | reject | reject | BadTileEntry (§11 #12/#14/#16/#31/#32) |
| neg-12b-reserved-nonzero | reject | reject | BadTileEntry |
| neg-13a-z-non-monotone | reject | reject | BadTileOrder (§11 #13) |
| neg-13b-xy-not-strict | reject | reject | BadTileOrder |
| neg-13c-duplicate-zxy | reject | reject | BadTileOrder |
| neg-14a-offset-misaligned | reject | reject | BadTileEntry |
| neg-14b-offset-below-blob | reject | reject | BadTileEntry |
| neg-14c-offset-past-ext | reject | reject | BadTileEntry |
| neg-14d-length-overrun | reject | reject | BadTileEntry |
| neg-15a-z-above-zmax | reject | reject | BadTileZoom (§11 #15) |
| neg-15b-z-below-zmin | reject | reject | BadTileZoom |
| neg-16-length-mismatch | reject | reject | BadTileEntry |
| neg-17a-count-mismatch | reject | reject | BadZoomDirectory (§11 #17) |
| neg-17b-offset-mismatch | reject | reject | BadZoomDirectory |
| neg-17c-offset-nonzero-empty | reject | reject | BadZoomDirectory |
| neg-18a-extoff-misaligned | reject | reject | BadExtensionsOffset (§11 #18) |
| neg-18b-extoff-past-crc | reject | reject | BadExtensionsOffset |
| neg-18c-extoff-below-blob | reject | reject | BadExtensionsOffset |
| neg-18d-extoff-wrong-sum | reject | reject | BadTileEntry (right verdict, entry-rule code) |
| neg-19a-section-overruns | reject | reject | BadExtensionFraming (§11 #19) |
| neg-19b-section-padding-nonzero | reject | reject | BadExtensionFraming |
| neg-19c-stranded-bytes | reject | reject | BadExtensionFraming |
| neg-20-unknown-uppercase-tag | reject | reject | BadExtensionTag (§11 #20/#27/#28) |
| **neg-22-locallinear-no-affn** | reject | **ACCEPT — MISMATCH** | ok |
| neg-23a-singleimage-tilecount | reject | reject | BadSingleImage (§11 #23) |
| neg-23b-singleimage-entry-nonzero | reject | reject | BadSingleImage |
| neg-23c-singleimage-zmax-nonzero | reject | reject | BadSingleImage |
| neg-23d-singleimage-axis-tms | reject | reject | BadSingleImage |
| neg-23e-singleimage-zoomoffsets-leak | reject | reject | BadZoomDirectory |
| neg-24-crc-flipped | reject | reject | CrcMismatch (§11 #24) |
| neg-25-index-offset-296 | reject | reject | BadIndexOffset (§11 #25) |
| **neg-26a-name-payload-empty** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-26b-name-tag-overruns** | reject | **ACCEPT — MISMATCH** | ok |
| neg-27-tag-digit-first | reject | reject | BadExtensionTag |
| neg-28-tag-nonprintable | reject | reject | BadExtensionTag |
| **neg-29a-duplicate-uppercase** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-29b-duplicate-name-locale** | reject | **ACCEPT — MISMATCH** | ok |
| neg-31a-x-overflow | reject | reject | BadTileEntry |
| neg-31b-y-overflow | reject | reject | BadTileEntry |
| neg-32a-tile-gap | reject | reject | BadTileEntry |
| neg-32b-tile-overlap | reject | reject | BadTileEntry |
| **neg-33-padding-nonzero** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-34-affn-length** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-35a-affn-nan** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-35b-affn-inf** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-36-affn-with-webmercator** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-37a-name-bad-utf8** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-37b-name-bcp47-bad-case** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-37c-name-bcp47-3-letter** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38a-srcd-bad-utf8** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38b-attr-bad-utf8** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38c-attr-crlf** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38d-attr-bare-cr** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38e-attr-c0-control** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38f-attr-nel** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38g-attr-ls** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38h-attr-trailing-lf** | reject | **ACCEPT — MISMATCH** | ok |
| **neg-38i-attr-empty** | reject | **ACCEPT — MISMATCH** | ok |

### The 22 Container mismatches, grouped

All 22 are **wrong ACCEPTs** of packs the spec says MUST be rejected; there are
no wrong rejects. They cluster entirely in extension **payload** validation —
the Container validates framing (#19) and tag syntax (#20/#27/#28) but never
inspects section contents:

1. **AFFN semantics not checked** (5): neg-22 (LocalLinear pack with no AFFN,
   §11 #22), neg-34 (AFFN payload wrong length), neg-35a/b (NaN / Inf
   coefficients), neg-36 (AFFN present in a WebMercator pack).
2. **NAME payload rules not checked** (5): neg-26a (empty payload), neg-26b
   (tag_length overruns payload), neg-29b (duplicate locale), neg-37a/b/c
   (UTF-8 validity, BCP-47 case, 3-letter subtag).
3. **Duplicate-section rule not checked** (1): neg-29a (duplicate upper-case tag).
4. **Section padding not checked** (1): neg-33 (non-zero pad bytes after payload).
5. **SRCD/ATTR text rules not checked** (10): neg-38a–i plus neg-38a's SRCD
   variant (bad UTF-8, CRLF/CR/NEL/LS/C0 controls, trailing LF, empty ATTR).

Additional spec-currency note (does not affect this corpus): the Container
accepts only `pixel_format == 1` (`Container.cpp` line 210: `if (pixByte != 1)`)
and only `compression == 0` (line 339). Under spec v0.6, `pixel_format 2`
(RGB565) and `compression 1` (RLE) are *valid* — the Container would reject
conforming RGB565/RLE packs. No golden fixture exercises them yet, so this does
not show in the matrix, but the reader is v0.3-era in this respect (its own
header dump hardcodes "ABGR2222").

## 3. C99 reference reader — matrix (side-by-side)

Harness: `tests/conformance.c` (opens each fixture; goldens also verify every
§14.5 tile hash; negatives must be rejected *with the matching rule code*).
Full log: `build/reference_matrix.txt`.

**Golden: 17/17 accepted, 70/70 tile hashes matched. Harness score: 82 passed,
14 failed. By accept/reject verdict: 66/79 negatives rejected (13 wrongly
accepted); one more (neg-18d) rejected with the wrong rule code.**

| Fixture | Expected | Container (C++) | Reference reader (C99) |
|---|---|---|---|
| all 17 goldens | accept | accept, hashes ok | accept, hashes ok |
| neg-01 … neg-17c | reject | reject | reject, correct rule codes |
| neg-18a/b/c | reject | reject | reject (#18) |
| neg-18d-extoff-wrong-sum | reject | reject (entry code) | reject but **rule 114 (#14) not 118 (#18)** — harness FAIL, verdict still reject |
| neg-19a/b/c, neg-20 | reject | reject | reject, correct codes |
| neg-22-locallinear-no-affn | reject | **ACCEPT** | reject (#22) |
| neg-23a–e, neg-24, neg-25 | reject | reject | reject, correct codes |
| neg-26a/26b | reject | **ACCEPT** | reject (#26) |
| neg-27, neg-28 | reject | reject | reject, correct codes |
| neg-29a-duplicate-uppercase | reject | **ACCEPT** | reject (#29) |
| neg-29b-duplicate-name-locale | reject | **ACCEPT** | **ACCEPT** (documented "planned" gap) |
| neg-31a/b, neg-32a/b | reject | reject | reject, correct codes |
| neg-33-padding-nonzero | reject | **ACCEPT** | reject (#33) |
| neg-34, neg-35a/b, neg-36 (AFFN) | reject | **ACCEPT** (all 4) | reject (#34/#35/#36) |
| neg-37a/b/c (NAME text) | reject | **ACCEPT** (all 3) | **ACCEPT** (documented "planned" gap) |
| neg-38a–i (SRCD/ATTR text) | reject | **ACCEPT** (all 9) | **ACCEPT** (documented "planned" gap) |

Scoreboard:

| | Golden | Negatives correctly rejected (verdict) | Wrong accepts |
|---|---|---|---|
| Container (SDK C++) | 17/17 | 57/79 | 22 |
| Reference reader (C99) | 17/17 | 66/79 | 13 (all documented as planned gaps in its README) |

The two readers' wrong-accept sets overlap exactly on the reference reader's 13
(29b, 37a–c, 38a–i); the Container additionally wrongly accepts 9 more
(22, 26a, 26b, 29a, 33, 34, 35a, 35b, 36) that the reference reader catches.

## 4. stanley.rawtiles sanity check

File: `wt-rawtiles/Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles`
(note: at `Docs/Tutorials/RawTilesMap/Resources/`, not `<worktree>/Resources/`).
Size **787,008 bytes**, sha256 `e92b3cca252d…369e282ef`.

Both readers **accept** it and agree on every field:

| Field | Container | Reference reader |
|---|---|---|
| open result | ok | 0 (ok) |
| pack_uuid | 9e134d5e-1043-5399-9f53-72bac27b1434 | same |
| pixel_format | ABGR2222 (1) | 1 |
| compression (entries) | None (0) | 0 |
| projection / addressing / axis | WebMercator / Quadtree / XYZ | 1 / 1 / 1 |
| tile_dim_px | 256 | 256 |
| zoom_range | [12, 14] | [12, 14] |
| tile_count | 12 (z12: 2, z13: 4, z14: 6) | 12 |
| bbox (µ°) | lon [−123155000, −123120000], lat [49290000, 49315000] | same |
| bbox (deg) | lon [−123.155, −123.120], lat [49.290, 49.315] | — (µ° API) |
| extensions_offset | 786964 | — |
| build_timestamp | 1778880801 | — |

## 5. Casual fuzzing of Container (ASan+UBSan build)

Mutator: `build/fuzz.py`, log: `build/fuzz_log.txt`. Crash detection: signal
death, ASan exitcode 99 (`ASAN_OPTIONS=exitcode=99`), any
AddressSanitizer/UBSan report text, or 30 s timeout. UBSan was
`-fno-sanitize-recover=all`, so any UB aborts. Crashing inputs would be saved
to `build/fuzz/crashes/` — the directory is **empty**.

### Phase 1 — targeted structural mutations (52 cases, stanley + golden-pyramid)

Every case was cleanly rejected with a sensible result code; none accepted
except none, none crashed. Where a mutation would otherwise die at the CRC
gate, the footer CRC-32 was recomputed (`refix_crc`) so the targeted validator
was actually exercised. Highlights:

| Mutation | Outcome |
|---|---|
| truncate at 0 / 3 / 100 / 291 / 292 bytes (mid-header) | reject FileTooShort (§11 #1) |
| truncate at 296 / mid-index / index-end ±1 | reject "tile_count exceeds file bounds" |
| truncate mid-blob | reject BadExtensionsOffset (§11 #18) |
| truncate EOF−5 / −4 / −1 (footer clipped) | reject §11 #19 / #18 |
| flip each of the 4 CRC footer bytes | reject CrcMismatch (§11 #24) |
| swap index entries 0/1 (stale CRC and refixed CRC) | reject BadTileEntry / BadTileOrder path |
| zoom_offsets[zmin].count+1 (leak into next zoom), CRC refixed | reject BadZoomDirectory (§11 #17) |
| zoom_offsets[zmin].offset = 0xFFFFFFF0, CRC refixed | reject BadZoomDirectory |
| extensions_offset = EOF+4096 and 0xFFFFFFFC, CRC refixed | reject (index/extensions bounds check) |
| entry0 length extended into the footer, CRC refixed | reject BadTileEntry |
| entry0 length / offset = 0xFFFFFFF0, CRC refixed | reject BadTileEntry |

### Phase 2 — random mutation loop

2000 iterations (seed 20260805), each: pick a random golden fixture, flip 1–8
random bits, 50% of runs recompute the footer CRC so mutations pass the CRC
gate and reach the structural validators, run the ASan binary.

Result: **accept 181, reject 1819, crash 0, timeout 0.** The 181 accepts are
CRC-refixed mutations whose flips landed in non-validated bytes (tile pixel
data, timestamps, UUID bits, unvalidated extension payload bytes) — consistent
with §2's finding that extension payloads are unvalidated; not memory-safety
issues.

### Fuzzing verdict

**Zero crashes, zero sanitizer reports, zero timeouts across 2052 mutated
inputs** (52 targeted + 2000 random) against the ASan+UBSan build, plus the
full 96-fixture corpus run under ASan. No memory-safety finding in
`SDK::RawTiles::Container`.

## 6. Completeness

All six tasks were performed as specified. Deviations from the brief, all
benign: (1) fixtures neg-07b-pixfmt-2 / neg-07g-comp-1 do not exist in v0.6 —
their renamed successors neg-07b-pixfmt-5 / neg-07g-comp-3 were run; (2)
stanley.rawtiles lives under Docs/Tutorials/RawTilesMap/Resources/, not
Resources/; (3) no docker/cmake needed — everything hand-compiled with clang.
Nothing was posted to GitHub; no repo files were modified.

Evidence files (all under `scratchpad/build/`): `container_matrix.txt`,
`reference_matrix.txt`, `fuzz_log.txt`, `fuzz.py`, `run_corpus.py`, `refdump.c`,
binaries `rawtiles_check`, `rawtiles_check_asan`, `rawtiles_conformance`, `refdump`.
