# Investigation — rawtiles spec adequacy against the full needs-space

**Date:** 2026-08-06
**Subject:** `github.com/tobymurray/rawtiles` @ `38d4d26` (spec v0.6, wire `(1, 0)`, Provisional),
`github.com/tobymurray/slippypack` @ `1f9132d`, `origin/feat/rawtiles-container` @ `b957aa62`.
**Report:** `RAWTILES_SPEC_ADEQUACY.md` (repo root).
**Nothing was posted to GitHub.** `gh` was not invoked; both external repos were cloned read-only.

Four questions were settled by experiment or by arithmetic on measured constants rather than by
argument. Two of my own hypotheses were falsified in the process; both are recorded below, because
the falsified one changed a recommendation I would otherwise have made.

---

## E1 — Does `pack_uuid` identify the pack's bytes?

**Hypothesis.** Appendix A's canonical descriptor (§ A.3) lists twelve top-level keys and
`compression` is not among them. Compression is a *per-tile-index* byte (§ 5.1, § 8.5), not a
header field, so it cannot enter the descriptor implicitly. Therefore two packs built from the
same source with different `compression` derive the **same** `pack_uuid` while having entirely
different tile blobs — which contradicts § A.4's promise that a recipient holding a cached
`pack_uuid` "is entitled to assume *byte-identical* tile blobs, not just 'same logical inputs'".

**Method.** Build the same fixed synthetic source three times with slippypack (built from
`1f9132d`, `cargo build --release`), varying one flag at a time, and compare header bytes 8..24
(`pack_uuid`, § 4 header table) and whole-file SHA-256. `--pixel-format` is the control: that key
*is* in the descriptor, so it must change the UUID.

```
slippypack make --source synthetic --out none.rawtiles   --compression none
slippypack make --source synthetic --out rle.rawtiles    --compression rle8
slippypack make --source synthetic --out rgb565.rawtiles --pixel-format rgb565 --compression none
```

**Log.**

| file | size (B) | `pack_uuid` (bytes 8..24) | SHA-256 (first 16 hex) |
|---|---:|---|---|
| `none.rawtiles`   | 4,712 | `f098e3da7a305ecbbced36a1b8bf8370` | `1a4fd573e8fda02a` |
| `rle.rawtiles`    |   680 | `f098e3da7a305ecbbced36a1b8bf8370` | `386bc4513a2e3fe7` |
| `rgb565.rawtiles` | 8,808 | `84cbff35e7a851d293a1b82c4b9c581c` | `aa72f603614d94e7` |

Fixtures are committed alongside this README.

**Verdict — CONFIRMED.** Two packs, **identical `pack_uuid` `f098e3da-7a30-5ecb-bced-36a1b8bf8370`,
6.9× different in size, no bytes in common in the tile blob.** The control behaved as predicted
(`rgb565` got its own UUID), which localises the defect precisely to keys missing from § A.3
rather than to a writer bug. slippypack's own `--compression` help text states the consequence
outright: "two compressed and uncompressed builds of the same region produce the same `pack_uuid`
and the recipient compares pack bytes to detect the drift" — i.e. the writer author already knew,
and the mitigation named ("compare the bytes") is exactly the work the UUID exists to avoid.

**Corollaries, confirmed by the same reading of § A.3 (no experiment needed — the key list is
exhaustive and these keys are absent):**

- `supersedes_uuid` (§ 4.4) is not in the descriptor. A pack whose only difference from pack *X*
  is that it supersedes *X* therefore derives *X*'s UUID, so `supersedes_uuid == pack_uuid`:
  **self-supersedence.** A reader using § 4.4 to drive eviction evicts the pack that just arrived.
- No extension payload (`NAME`, `ATTR`, `SRCD`) is in the descriptor. Stripping attribution or
  changing the display name leaves `pack_uuid` unchanged.

---

## E2 — Where does the 4 GiB cap actually bite, and how big is the index there?

**Method.** `scale.py` (committed here) computes exact WebMercator tile counts per bbox × zoom
range using the standard slippy-map formulas, then pack size as
`292 + 20·n + n·tile_dim²·bpp + 4` (§ 3 layout, uncompressed, ignoring 0–3 B tile padding which
is zero for these tile sizes). The cap is § 3's `file_size ≤ 2^32 − 1`.

**Log (excerpt; `tile_dim = 128`, ABGR2222, zoom 0..Z inclusive).**

| region | zooms | tiles | index | pack | > 4 GiB? |
|---|---|---:|---:|---:|:--:|
| Stanley Park ≈4×3 km | 0–15 | 60 | 1.2 kB | 0.9 MiB | |
| Vancouver city ≈20×14 km | 0–16 | 2,354 | 47 kB | 36.8 MiB | |
| Greater Vancouver ≈90 km | 0–16 | 27,627 | 0.53 MiB | 432.2 MiB | |
| Switzerland | 0–15 | 146,843 | 2.80 MiB | 2.24 GiB | |
| Switzerland | 0–16 | 585,449 | 11.2 MiB | 8.94 GiB | **YES** |
| United Kingdom | 0–14 | 564,154 | 10.8 MiB | 8.62 GiB | **YES** |
| France (metro) | 0–14 | 576,945 | 11.0 MiB | 8.81 GiB | **YES** |
| CONUS | 0–14 | 5,094,894 | 97.2 MiB | 77.84 GiB | **YES** |

Cap-bound maximum tile counts (the ceiling is on *bytes*, so it implies a tile-count ceiling):

| `tile_dim` | ABGR2222 | RGB565 |
|---:|---:|---:|
| 128 | 261,824 tiles (index 4.99 MiB) | 130,992 (2.50 MiB) |
| 256 | **65,516 tiles** (index 1.25 MiB) | 32,762 (0.62 MiB) |

**Verdict — CONFIRMED.** The cap is reached at **country scale even at z14**: the UK (8.62 GiB) and
France (8.81 GiB) exceed 4 GiB at zoom 0–14 with 128 px tiles; Switzerland — a small country — fits
at 0–15 (2.24 GiB) and fails at 0–16 (8.94 GiB). With the 256 px tiles the shipped
`stanley.rawtiles` actually uses, a pack can never hold more than **65,516 tiles**, which at z16 is
a ~156 × 156 km square (a z16 256 px tile spans 611.5 m at the equator). The format's honest ceiling
is "large region", not "country". This collides with a planned product feature: slippypack's
`PLAN.md` § "File-size ceilings (PWA)" contemplates 1–3 GB packs via OPFS streaming and a "Whole
country" preset.

RLE moves the ceiling by roughly one zoom level, not by an order of magnitude: at the 27.0%
compression ratio the spec's own v0.6 changelog measured on `stanley.rawtiles`, UK z0–14 lands
near 2.3 GiB (fits) and z0–15 near 9.3 GiB (does not).

---

## E3 — What does eager validation cost at scale on the real device?

**Falsified hypothesis (recorded because it changed a recommendation).** I initially reasoned from
the measured "64 KiB tile read = 6–9 ms" datum that the device sustains ~7–10 MB/s, and concluded
that § 11.2's *mandatory* eager index walk (the rule-specific note making #18's padded-sum
equality and #32's tight-layout check eager for every reader) would make country-scale packs take
seconds-to-minutes merely to open — and that the walk should therefore be demoted to a writer-side
obligation checked by host validators.

**That is wrong, and the device evidence says so.** The container evidence bundle's on-watch
readout (`origin/tmp/rawtiles-container-pr-description`,
`Docs/Investigations/2026-08-05-rawtiles-container/device/README.md`) records
`scan 113ms open 27ms` for the 787,008-byte `stanley.rawtiles`, where `open` is the *full* eager
pass: index walk, every extension section, and the whole-file CRC-32. 787,008 B / 27 ms =
**27.8 MiB/s** (29.1 MB/s) — the 64 KiB tile-read figure is dominated by per-call latency, not
bandwidth, and must not be used as a throughput constant.

**Recomputed with 27.8 MiB/s.**

| pack size | eager-open cost |
|---:|---:|
| 0.75 MiB (measured) | 27 ms |
| 10 MiB | 0.4 s |
| 100 MiB | 3.6 s |
| 432 MiB (Greater Vancouver z0–16) | 15.5 s |
| 2.24 GiB (Switzerland z0–15) | 82.6 s |
| 4 GiB (the cap) | 147 s |

**Verdict.**

- **The eager index walk is cheap and the v0.5 decision to mandate it is correct.** The 4 GiB cap
  bounds the index at ≤ 4.99 MiB (128 px) or ≤ 1.25 MiB (256 px), i.e. ≤ 0.18 s at the ceiling.
  My demotion recommendation is withdrawn. CONFIRMED by E2's cap arithmetic + the measured rate.
- **The whole-file CRC-32 is the term that does not scale**, and it is ~99.9% of the open cost for
  any real pack (a 143 MiB pack's index is 179 kiB). Eager or streaming CRC verification (§ 10) costs
  ~3.6 s at 100 MiB and ~147 s at the format's own ceiling, so device readers will be pushed to
  § 10's "caller-asserted trust" mode — which means **large packs ship with no integrity checking
  at all**, on hardware with a *documented* corruption mode (USB-MSC writes concurrent with BLE
  sync corrupting exFAT, recorded in the same evidence bundle). No per-tile or per-region integrity
  exists in the wire format; § 14.5's per-tile SHA-256 tables are conformance *sidecar* files, not
  pack content.
- Extrapolation from a single measured size is **PLAUSIBLE**, not confirmed. Settling step: re-run
  the device probe against a ~100 MB and a ~1 GB pack and record `open` — worth doing before v1.0
  because the integrity recommendation's urgency scales with the real curve.

---

## E4 — Does the shipped writer emit v0.6-conforming RLE?

**Hypothesis.** Spec v0.6 (Appendix B) renamed `compression = 1` from `RLE8` (byte-level) to `RLE`
(pixel-level, operating in `bytes_per_pixel` units) and justified the rename as costless because
"no v0.5 `RLE8` packs exist in the wild to invalidate".

**Method.** Read `slippypack-core/src/format/rle8.rs` and the CLI's `--compression` help text at
`1f9132d`.

**Log.** `rle8.rs` module doc: *"Byte-level run-length encoding for `compression = 1` per rawtiles
spec § 9.11 … `H ∈ [0x00, 0x7F]`: literal run. Payload is `(H + 1)` literal **bytes**"*. CLI help:
*"`rle8` emits the spec § 9.11 byte-level run-length encoding"*.

**Verdict — CONFIRMED.** The shipped writer implements the **v0.5 byte-level** semantics. For
`ABGR2222` this is a no-op (`P = 1`), so `none.rawtiles`/`rle.rawtiles` above are unaffected. For
`RGB565` the writer emits streams a v0.6 decoder will mis-decode. The spec's "no packs in the wild"
claim is therefore false as of today in the one place it matters: the only published writer emits
the old semantics, and the only published reader (`feat/rawtiles-container`) fails closed on RLE,
so nothing currently catches the divergence. Routed to slippypack as a spin-off.

---

## Method notes / what would falsify the report's main claims

- Every spec claim in the report cites a `§`; every code claim cites `file:line` or a `git show`
  path at a named commit. Spec quotations are from `38d4d26`, the commit the SDK reader names.
- E1 is reproducible in two commands and its control case is what makes it a defect in § A.3
  rather than a writer bug. To falsify: show a descriptor key that encodes per-tile compression.
- E2 is pure arithmetic; to falsify, dispute the tile-count formula or the § 3 size expression.
- E3's *ratios* are arithmetic on one measured constant; to falsify, measure a large pack on device.
- The second falsified hypothesis, recorded for completeness: I expected a degenerate pack with
  many zero-length tile entries to force an unbounded open-time walk. It does not produce a *new*
  cost — such a walk is the same order as the CRC pass the reader already performs — so the finding
  was downgraded from "DoS" to "add a `length ≥ 1` minimum for `compression ≠ None` and recommend a
  documented reader cap", which is where it appears in the report.

---

## E5 — How much smaller can a pack get, and at what decoder cost?

**Question (raised after the report was written).** `compression = RLE` is the only compression v1
defines, and § 8.5 reserves `QOI` (2) and `LZ4` (3) for the future. Is that the right ladder — and
does better compression change the coverage ceiling `E2` found?

**Method.** Extract the 12 real 256×256 ABGR2222 tiles from `stanley.rawtiles` (786,432 B of pixels)
and compress **each tile independently**, as the format requires (§ 5.1 carries `compression`
per-entry, so a tile must be decodable alone). Scripts committed here: `compress.py` (codec
comparison, including a from-scratch § 9.11 canonical RLE encoder), `window.py` (deflate window and
tile-size sweeps), `fit.py` (coverage consequences).

**Log — content census first.** Across all 12 tiles there are **29 distinct byte values** (18–22 per
tile) out of ABGR2222's 64, alpha is `3` everywhere as § 9.1 requires, and the order-0 entropy is
**2.297 bits/px** — a memoryless-coder floor of 28.7% of raw.

**Log — codecs, per-tile, lossless unless marked.**

| scheme | bytes | % raw | ratio | decoder cost |
|---|---:|---:|---:|---|
| raw (`compression = None`) | 786,432 | 100.0% | 1.00× | none |
| **spec `RLE` (§ 9.11)** | 254,708 | 32.4% | 3.09× | ~30 lines C, O(1) RAM, row-streamable |
| LZ4 `-1` / `-9` / `-12` | 227,595 / 141,610 / 136,056 | 28.9 / 18.0 / **17.3%** | 5.78× | ~200 lines; 64 KiB window |
| deflate, fixed-Huffman, 2 KiB window | 134,365 | 17.1% | 5.85× | **no Huffman tables + 2 KiB ring** |
| deflate, dynamic-Huffman, 1 KiB window | 103,296 | 13.1% | 7.61× | ~11 KB tables + 1 KiB ring |
| **deflate, dynamic-Huffman, 2 KiB window** | 102,127 | **13.0%** | **7.70×** | ~11 KB tables + 2 KiB ring |
| deflate, 32 KiB window (i.e. zlib default) | 103,359 | 13.1% | 7.61× | ~11 KB tables + 32 KiB ring |
| bzip2 / LZMA / zstd `-19` | 101,296 / 93,008 / 92,626 | 12.9 / 11.8 / 11.8% | 8.5× | far too large for MCU |
| deflate + PNG `Sub` filter | 123,031 | 15.6% | 6.39× | *worse than unfiltered* |
| `RLE` then deflate | 121,391 | 15.4% | 6.48× | *worse than deflate alone* |
| 4bpp palette, 16 colours (**LOSSY**) | 393,216 | 50.0% | 2.00× | 16 B palette + LUT expand |
| 4bpp palette + RLE (**LOSSY**) | 182,710 | 23.2% | 4.30× | LUT expand + RLE |

**Verdict — five results, three of them negative.**

1. **A 2 KiB deflate window is as good as 32 KiB** (13.0% vs 13.1%). Every useful match in this
   content is short-range, so the classic reason to reject deflate on an MCU — the 32 KiB sliding
   window — simply does not apply. With a 2 KiB ring buffer the decoder stays row-streamable
   (back-references never reach further back than the window), which is the property § 9.11 and the
   § 11.1 resource sketch are built around. Decoder state is ~13 KB all-in (Huffman tables
   dominate), or **~2 KiB with fixed-Huffman-only** at 17.1%.
2. **`RLE` leaves ~2.5× on the table.** 32.4% vs 13.0%. (Note: the spec's Appendix B v0.6 row quotes
   27.0% for this corpus — that figure is for **RGB565**, not ABGR2222, so it is consistent with
   this measurement, not contradicted by it.)
3. **LZ4 — the codepoint the spec already reserves at 3 — is dominated on both axes.** 17.3% at best
   against small-window deflate's 13.0%, while needing a 64 KiB window against deflate's 2 KiB. If
   only one more compression is ever added, the evidence says deflate, not LZ4.
4. **PNG-style row filtering makes it worse** (15.6% vs 13.2%), and so does RLE-then-deflate
   (15.4%). Both destroy the literal structure LZ77 matches on. This content is palette-like, not
   continuous-tone; the usual image-compression instincts invert.
5. **The indexed/palette idea is a dead end.** 4bpp is 50% of raw before compression — *above* the
   2.297 bits/px entropy floor — so any fixed-width representation loses to an entropy coder, and
   the lossy 16-colour version (23.2%) is still nearly twice the size of lossless deflate (13.0%).
   This kills the "land `L4` and halve the pack" idea that § 8.1's reserved codepoint invites.

**Consequence for `E2`'s ceiling** (`fit.py`, 128 px tiles, 4 GiB cap):

| region | zooms | tiles | None | RLE (0.324) | deflate (0.130) | fits in 4 GiB with |
|---|---|---:|---:|---:|---:|---|
| Greater Vancouver | 0–16 | 27,627 | 0.42 G | 0.14 G | 0.06 G | all three |
| Switzerland | 0–16 | 585,449 | 8.94 G | 2.91 G | 1.17 G | RLE, deflate |
| United Kingdom | 0–14 | 564,154 | 8.62 G | 2.80 G | 1.13 G | RLE, deflate |
| France (metro) | 0–14 | 576,945 | 8.81 G | 2.86 G | 1.16 G | RLE, deflate |
| United Kingdom | 0–16 | 8,993,050 | 137 G | 44.6 G | 18.0 G | **nothing** |
| CONUS | 0–14 | 5,094,894 | 77.8 G | 25.3 G | 10.2 G | **nothing** |

**This corrects the report.** `E2` computed the cap against `compression = None` only, and the
report's § 1 verdict and `U6` row read as "a country pack is impossible". That is too strong: **with
`RLE` as already specified, the UK, France and Switzerland all fit at z0–14, and Switzerland fits at
z0–16.** What genuinely does not fit under any compression is *country-at-z16+* or *continent
scale*. The report has been amended accordingly.

**Confidence and what would falsify this.** **PLAUSIBLE, not CONFIRMED, as a general claim.** This
is n = 12 tiles, one rendering style, one small area, three adjacent zooms. Ratios are strongly
content-dependent: the theoretical span for ABGR2222 runs from ~13% (flat vector-rendered styles
like this one) to ~75–80% (content using all 64 colours with per-pixel variation, e.g. dithered
satellite imagery). The *ranking* of codecs is likely robust; the *magnitudes* are not. Settling
step before acting: rebuild the same corpus over a satellite/aerial source and a contour-heavy topo
style, at 128 px as well as 256 px, and re-run `compress.py`.
