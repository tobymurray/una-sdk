# rawtiles spec adequacy — grading v0.6 against the whole needs-space

**Date:** 2026-08-06.
**Subject:** `github.com/tobymurray/rawtiles` @ `38d4d26` — spec v0.6, wire format `(1, 0)`,
status *Provisional*; `spec/rawtiles.md` (1,067 lines) read in full and treated as normative.
**Corroborating sources:** `github.com/tobymurray/slippypack` @ `1f9132d` (the only writer),
`origin/feat/rawtiles-container` @ `b957aa62` (the only reader), the container evidence bundle on
`origin/tmp/rawtiles-container-pr-description`, and the device measurements from
`origin/spike/rawtiles-device-proof`.
**Evidence bundle:** `Docs/Investigations/2026-08-06-rawtiles-spec-adequacy/` — five settled
questions (`E1`–`E5`), two falsified hypotheses, one self-correction, reproducible fixtures and
scripts.
**Nothing was posted to GitHub.**

**Not in scope.** Whether rawtiles is the right altitude versus PMTiles / MBTiles / vector tiles —
`RAWTILES_MAP_EVALUATION.md` settled that (verdict: yes, for blit-ready raster on this hardware)
and this report does not reopen it. The question here is narrower and binds later: *given* that
direction, does the spec's **shape** survive the whole space of needs, or only the two artefacts
that exist today (a 12-tile park pack and a synthetic fixture)?

**Citation convention:** `§ N` always refers to the **rawtiles spec**; this report's own sections are
cited as `part N`. Every claim carries a `§` (spec), a `file:line` (code), or an experiment ID
(`E1`–`E5`, bundle).
Findings are labelled **CONFIRMED** (traced to text or executed) or **PLAUSIBLE** (reasoned, with
the settling step named).

---

## 1. Verdict

**Complete? — No, conditionally.** The format is complete for one basemap per pack, which is what
both existing artefacts are. Two needs squarely on the product's own roadmap have *no*
representation at all: incremental integrity for BLE/companion transfer (`U13`), and transparency
for the overlay packs slippypack's `PLAN.md` already names a file path for (`U7`). A third — very
large coverage — is bounded rather than absent: the 4 GiB cap (`U6`) stops continent scale and
country-at-z16+, but **not** a country at z0–14, which `RLE` already fits today.
*Strongest supporting finding:* **`E1` — two packs built by the shipped writer from the same source
share `pack_uuid` `f098e3da-7a30-5ecb-bced-36a1b8bf8370` while differing 6.9× in size and sharing
no tile-blob bytes**, which falsifies § A.4's promise that a cached UUID entitles a recipient to
assume byte-identical tile blobs. Identity is the one thing a distribution system cannot work
around, and it is currently unsound.

**Flexible? — Conditional.** Genuinely flexible where it was designed to be: two pixel formats with
a clean `bytes_per_pixel` abstraction (§ 6.2), both axis conventions (§ 8.4), any zoom sub-range,
metadata-only packs, and a 24-slot zoom directory that makes per-zoom coverage readable from the
292-byte header alone. Rigid on exactly the axes the personas push hardest: one projection family
can be tiled (§ 8.6), no alpha in either pixel format (§ 9.1, § 9.2), one `supersedes_uuid` (§ 4.4),
and `zoom_max < 24` welded into the header's array size (§ 4.12).
*Strongest supporting finding:* § 8.6's legality table makes `LocalLinear × Quadtree` illegal, so
every non-WebMercator map is exactly one tile — but this is much less damaging than it first
appears, because georeferencing into WebMercator is the standard practice and z23 gives 1.87 cm/px.
The rigidity is defensible; it is the *un-named escape hatches* that are the problem.

**Extensible? — Yes, with one asterisk that reframes half this report.** The lowercase/ancillary
TLV half of § 7.2 is genuinely unlimited and genuinely additive: old readers MUST accept and MAY
ignore unknown lowercase tags (§ 11 #21), and `golden-ancillary-tag.rawtiles` pins that behaviour.
Plus the header keeps a real forward-compatibility hole — `reserved_v1_0`, 2 bytes at offset 6,
which readers MUST accept at any value (§ 4 header table), constrained to additive use by § 13.1.
**The asterisk:** uppercase-tag and enum growth are *old-reader-rejecting by design* (§ 7.2, § 8,
§ 13.1: "A v1.0 reader MUST accept v1.x packs, but the per-§ 7.2 / § 8 rules cause it to reject any
v1.x pack that uses newly-allocated SDK-reserved values it doesn't know"). So **"WIRE-MINOR" in
this spec does not mean "old readers still open new packs"** — it means they reject them cleanly.
Extensible ≠ forward-compatible. Every `VIA-EXTENSION` grade below is only cheap because its sketch
picks a *lowercase* tag; that is a deliberate constraint on the sketches, not an accident.

**The one-sentence version.** The wire format is sound and the reader-conformance model is unusually
rigorous for this niche; the defects that must be fixed this month are almost all in **Appendix A**
(identity) and in **metadata the format declines to carry** (integrity, licence, collection) — not
in the header, index, or blob layout, which survived every stress test in part 4. The one substantive
*efficiency* finding (part 5) is that the compression ladder the spec has reserved points at the wrong
next rung.

---

## 2. Method

Two passes, deliberately ordered so the prompt's seed list could not anchor the inventory.

**Pass 1 (independent).** Personas first, needs second, spec third: for each persona I wrote down
what they would actually *do* with a map on a 240×240 watch, then went looking for the mechanism.
This is what surfaced `E1` (identity vs. compression), the self-supersedence corollary, the
sub-byte-pixel-format contradiction, and the signature-seating conflict — none of which appear in
the seed list.

**Pass 2 (merge and extend).** Merged the seed needs, mined PMTiles / MBTiles / Garmin IMG+JNX+KMZ /
Coros / OruxMaps+Locus / mapsforge for capabilities rawtiles lacks (part 6), and folded in the
container PR's four open questions.

**Pass 3 (measure).** Where a question was cheaper to settle by experiment than by argument, it was:
`E1` (identity), `E2` (scale arithmetic), `E3` (device open cost), `E4` (writer/spec RLE drift),
`E5` (codec benchmark on the real pack).

**Grading rubric.** `SATISFIED` (mechanism exists, cited) · `VIA-EXTENSION` (representable today as
a TLV tag with no wire change — *each one carries a sketch in part 8; a grade without a sketch would be
a guess*) · `WIRE-MINOR` (additive bump; see the asterisk above for what that really promises) ·
`WIRE-MAJOR` (breaking) · `OUT-OF-SCOPE` (whose job it is instead) · `SDK-LAYER` (spec is fine;
the reader/platform must build it).

**Three of my own conclusions were overturned mid-audit** and all three are recorded, because each
changed a recommendation:

1. I expected § 11.2's mandatory eager index walk to make country packs unopenable and was going to
   recommend demoting it. The device's measured `open 27ms` for 787,008 B (27.8 MiB/s) plus the
   4 GiB cap's implied index ceiling (≤ 4.99 MiB) killed that: the walk costs ≤ 0.18 s at the
   format's own ceiling. **Recommendation withdrawn**; what survived is the *CRC* cost (`U13`).
2. I wrote that country-scale packs are impossible. That was computed against `compression = None`
   only. **`RLE` as already specified fits the UK, France and Switzerland at z0–14** (`E5`).
   Corrected throughout.
3. I had "land the reserved `L4` indexed format and halve the pack" queued as a recommendation.
   `E5` measured it: 4bpp is 50% of raw *before* compression, worse than lossless deflate. **Idea
   dropped.**

---

## 3. The needs matrix

### 3a. User needs

| # | Need — who wants it, what they'd do | Mechanism | Grade |
|---|---|---|---|
| U1 | **Arbitrary bbox × zoom pack** — everyone; "give me this park at z12–15" | § 4.9 `bbox`, § 4.8 `zoom_min/max`, § 5 index; `slippypack make --bbox --zoom` | **SATISFIED** |
| U2 | **Carry several packs** — traveller, hiker with two trailheads | Each pack self-identifying via § 4.3 `pack_uuid`; nothing in the format limits count | **SATISFIED** (format) + **SDK-LAYER** (enumeration/discovery; PR Q4) |
| U3 | **Know coverage before opening** — pack picker UI | Header at offset 0 carries `bbox`, `zoom_min/max`, `tile_count`, `build_timestamp` and per-zoom counts (§ 4.12) in 292 B; `NAME`/`ATTR`/`SRCD` need one more read at `extensions_offset` (§ 4.13, known from the header) | **SATISFIED** for the numeric summary; **VIA-EXTENSION** for real coverage — `bbox` is a single rectangle, so a route- or coastline-shaped pack over-claims wildly (see `covr`, part 8.4) |
| U4 | **Update a pack wholesale** — map changed, replace it | § 4.4 `supersedes_uuid`; `golden-supersedes.rawtiles` pins it | **SATISFIED** but single-valued (one region pack cannot retire three city packs) → **VIA-EXTENSION** (`sups`). **Also defective:** § A.3 omits `supersedes_uuid`, so a rename-plus-supersede pack derives the UUID it supersedes → **self-supersedence** (`M1`/`M2`) |
| U5 | **Partial / delta update** — "just the tiles that changed" | Not representable. `parent_uuid` is reserved for "future pack-compositing support" and MUST be all-zero in v1 (§ 4.5, § 11 #6) | **OUT-OF-SCOPE** for the wire + **SDK-LAYER** policy. Transfer-layer block diff is the right home and works *well for `compression = None`* (every tile is exactly `tile_dim²·bpp`, § 11 #16, so a changed tile is a localised byte change) and *badly for any compression* (a length change shifts every later offset under § 11 #32's tight layout). Worth stating in the spec as a documented consequence — and note it pulls against part 5's compression recommendation |
| U6 | **Country-scale pack** — "whole country" preset | § 3 caps `file_size` at `2^32 − 1`; all offsets u32 | **WIRE-MAJOR** to lift — but compression moves the wall a long way first. Uncompressed (`E2`): UK 8.62 GiB, France 8.81 GiB at z0–14; Switzerland 8.94 GiB at z0–16 — all over. **With `RLE` (measured 32.4%) all three fit; with small-window deflate (13.0%, `E5`) they fit with ~3× headroom.** What fits under *no* compression: country-at-z16+ (UK z0–16 = 18.0 GiB deflated) and continent scale (CONUS z0–14 = 10.2 GiB deflated). Judgement: **keep the cap** (u32 is load-bearing for MCU readers), **improve compression** (`M8`), and **name the escape hatch** — segmentation across packs, which is exactly what Garmin and Coros do (part 6) |
| U7 | **Overlays vs basemaps** — route highlight, POI layer, piste over satellite | **Blocked in-pack.** § 9.1: "Writers MUST set `A = 3` (fully opaque) for every pixel in v1 packs. v1 readers MUST treat any pixel as opaque regardless of the alpha bits." § 9.2: RGB565 has no alpha at all | **WIRE-MINOR** via a *new* `pixel_format` codepoint (7–255 reserved, § 8.1). **Do not relax alpha under codepoint 1** — old readers accept such a pack and render it wrong, which is worse than rejection. Collides with slippypack `PLAN.md`, which already names `/maps/overlay.rawtiles`; OruxMaps/Locus confirm layered-overlay-with-alpha is normal for this product category (part 6). For routes/POIs specifically, app-drawn vector overlay is the better answer → **SDK-LAYER** |
| U8 | **Day/night themes** | Nothing in the format; but a reader is free to tone-map 64-colour ABGR2222 at blit time | **SDK-LAYER**. A palette-indexed format (§ 8.1 reserves `L4`) would make theme-swap a palette write — but `E5` shows it would *cost* space, not save it, so this is the only thing that would justify landing `L4` and it is a weak reason |
| U9 | **Non-WebMercator sources** — nautical charts, floor plans, site plans | `LocalLinear` (§ 8.2 value 3) exists, but § 8.6 makes `LocalLinear × Quadtree` illegal: every such map is **one tile**, no pyramid. Escape hatch that actually works: georeference into WebMercator + Quadtree — standard practice, and what OruxMaps/Locus calibration files do (part 6) | **OUT-OF-SCOPE** mostly — warping serves it. Residual honest holes: latitudes beyond ±85.051129° are unrepresentable (§ 4.9) so **polar coverage is impossible**, and warping degrades charts whose native geometry is legally meaningful. **WIRE-MINOR** for a polar codepoint; need is weak |
| U10 | **Sub-cm resolution** — drone orthomosaic of a farm/mine | `zoom_max < 24` (§ 4.8), welded to `zoom_offsets[24]` (§ 4.12). z23 = 1.87 cm/px at the equator; a 256 px tile spans 4.78 m | **WIRE-MAJOR** to lift (header array size is frozen per major, § 13.1). Judgement: **right call** — 1.87 cm/px exceeds what a 240×240 panel can use |
| U11 | **Localized pack names** — non-English-locale user | § 7.4 restricts BCP-47 to `[a-z]{2}` or `[a-z]{2}-[A-Z]{2}`. **No script subtags** (`zh-Hant` / `zh-Hans`, `sr-Latn`) and **no 3-letter languages** (`fil`, `ceb`, `yue`); `neg-37c-name-bcp47-3-letter.rawtiles` pins the rejection. § 11 #37 makes a bad tag a **whole-pack rejection** — a cosmetic field killing the map | **WIRE-MINOR** if deferred, **free now**. Fix before readers ship (`M3`) |
| U12 | **Attribution — ODbL compliance** | § 7.3 `ATTR`: UTF-8, LF-separated, NFC, ordered per § A.4, no trailing LF / no zero length / no C0 controls (§ 11 #38). Content is legally sufficient — "© OpenStreetMap contributors" plus a licence URL fits in free text | **SATISFIED** for the legal minimum. Gaps: `ATTR` is optional (nothing compels it); no structured licence identifier for a store to enforce share-alike (**VIA-EXTENSION** `lics`); and **`ATTR` bytes are not in § A.3's descriptor, so an attribution-stripped pack keeps its `pack_uuid`** (`M1`) |
| U13 | **Get a multi-MB pack onto the watch, resumably, with integrity** — the live Gadgetbridge/companion direction | **Gap.** The only wire integrity is the § 10 whole-file CRC-32 footer, whose cost is linear in pack size (**`E3`: 27.8 MiB/s measured on device ⇒ 3.6 s at 100 MiB, 147 s at the 4 GiB cap**), and **no prefix of a pack is verifiable**, so resumable chunked transfer must invent its own integrity. § 14.5's per-tile SHA-256 tables are *conformance sidecars*, not pack content. The conforming escape (§ 10 "caller-asserted trust") means large packs ship with **no integrity checking at all** — on hardware with a *documented* corruption mode (USB-MSC concurrent with BLE sync corrupting exFAT) | **VIA-EXTENSION — define now.** `blkh` (part 8.1). Highest-value item after `M1`. Note `M8` compounds favourably: better compression shrinks the same map area ~7.7×, and open cost is O(file bytes), so it shrinks the CRC problem proportionally |
| U14 | **Storage management / which pack to evict** | Header alone gives `bbox`, zoom range, per-zoom counts, `tile_count`, `build_timestamp`, `pack_uuid`, `supersedes_uuid` (§ 4); file size from the filesystem | **SATISFIED** + **SDK-LAYER** policy |
| U15 | **Route-shaped pack** — trail runner with a planned route | Sparse coverage works at tile granularity: absent tiles return the absent outcome (§ 5.3 step 4) with no wasted bytes | **SATISFIED**; `bbox` over-claim per U3 |
| U16 | **POI / waypoint / geocache data** | Not a raster tile container's job | **OUT-OF-SCOPE** — app data files |
| U17 | **Sailor — charts** | See U9. Chart licensing/encryption (UKHO, S-63) | **OUT-OF-SCOPE** — distribution layer's job. Note Garmin *does* put DRM in the container (JNX carries an encrypted unit ID, part 6); rawtiles deliberately does not |
| U18 | **Skier — piste map / hand-drawn panorama** | `SingleImage` + `AFFN` (§ 7.3), `golden-singleimage-affn.rawtiles`. `tile_dim_px` is u16 so one image ≤ 65,535 px/side, further bounded by § 3's 4 GiB | **SATISFIED** for a single-scale panorama; no pyramid means no cheap zoom-out (U9), and only *one* image per pack (part 6, Garmin KMZ comparison) |
| U19 | **Orienteer** | Local scale, warp-to-WebMercator, z ≤ 23 ample | **SATISFIED** |
| U20 | **Traveller, storage-constrained** — many cities, small flash | Many small packs + § 4.4 supersedence + § 8.5 `RLE` | **SATISFIED**, and `M8` is the biggest available win here. Tradeoff worth documenting: any compression forfeits row-random-access — a partial-tile redraw must decode from the tile start, whereas `None` permits a windowed seek+read |

### 3b. Platform needs

| # | Need — who wants it | Mechanism | Grade |
|---|---|---|---|
| P1 | **New display: RGB888** — SDK maintainer, future watch | Stress-tested in part 4(a): `bytes_per_pixel = 3` flows through § 6.2's length formula, § 11 #16's equality, § 9.11's `P`-byte runs, and § 6.1's 4-byte padding without a single special case | **WIRE-MINOR**, clean. The `bytes_per_pixel` abstraction earns its keep |
| P2 | **Sub-byte formats** — `L4` (5) and `L1` (6) are *already reserved* in § 8.1 | **They cannot land as specified.** § 2 defines `bytes_per_pixel` as "the on-disk uncompressed byte count for one pixel"; § 6.2's `tile_dim² × bytes_per_pixel`, § 11 #16's exact-length equality, and § 9.11's pixel-unit RLE all require an integer ≥ 1. `L4` is ½, and § 9.11's runs cannot address half-bytes | **WIRE-MINOR** to land; **fix the wording now** (`M5`). But `E5` removes the *motivation*: 4bpp is 50% of raw, above the 2.297 bits/px entropy floor, so an indexed format is a size **regression** versus compressing ABGR2222. Land it only if palette-swap theming (U8) justifies it |
| P3 | **New `tile_dim`, e.g. 512 px** | u16, one per pack (§ 4.7). 512 px ABGR2222 = 256 KiB/tile = 43% of the 600 K GUI budget (`cmake/una-app.cmake:281`); ~4× the measured 6–9 ms 64 KiB read | **SATISFIED** (viable, ill-advised). Mixed `tile_dim` across zooms is impossible — **right call**; it would break § 11 #16's single equality. `E5` shows tile size barely affects compression (12.8–13.1% across 64/128/256 px), so this is purely a RAM/latency decision |
| P4 | **New compression** — enum reserved, § 8.5 | Path exists, three snags: (a) § 9.11's "O(P) working memory, row-streamable" promise is **RLE-specific prose, not a format-wide invariant**; (b) compression is **per-tile-entry, not declared in the header**, so a reader cannot pre-flight support from the header alone — though the § 11.2-mandated open-time index walk already touches every entry, so the check is free in practice; (c) § 14.1 requires a *canonical encoder* per compression or reproducibility breaks | **WIRE-MINOR**; and `E5` says the reserved ladder is mis-ordered — see part 5 and `M8` |
| P5 | **Per-tile metadata** — timestamps, source revision, for delta updates | § 5.1 has `flags` + `reserved` = 2 spare bytes/entry, but § 11 #12 makes readers reject non-zero, so using them is old-reader-rejecting | **VIA-EXTENSION** — `tmet`, a parallel array indexed by tile-index position (part 8.4) |
| P6 | **Signing / tamper-evidence** — pack-store operator | **No clean seat.** § 10's CRC scope is "every byte from offset 0 up to the CRC", so it covers any signature payload; and § 12.1 orders sections by ascending tag, so a `SIGN` section lands *between* `NAME` and `SRCD` — mid-region — making the signed byte-range a hole | **Decide now** (`M7`): either a § 12.1 ordering exception plus an explicit signed-range rule, or state that signatures are **detached sidecars / OUT-OF-SCOPE** |
| P7 | **Cross-app pack sharing / dedup** — PR open question 4 | Appendix A's canonical `pack_uuid` is the right idea and *most* of the answer — but it is currently unsound as a byte-dedup key (`E1`) | See part 10. **Format contribution: `M1` + `blkh`** |
| P8 | **Two packs covering the same area** — priority/ordering | No priority field, but `build_timestamp` (§ 4.10) + `bbox` + zoom range give a workable newest-wins policy from the header alone | **SDK-LAYER** |
| P9 | **Pack collections / ordered layering** | `parent_uuid` is reserved for exactly this (§ 4.5) and unusable in v1 | **VIA-EXTENSION** (`coll`, part 8.3) — but part 6 shows the industry solves this with a *filesystem naming convention*, which needs no spec change at all. Prefer the convention; add the tag only if in-file grouping proves necessary |
| P10 | **Diagnostics** — "which tool made this broken pack?" | Header gives `pack_uuid`, `format_version`, `build_timestamp`. **No writer identity:** `SRCD` is optional and § 7.3 requires reproducibility-claiming writers to **omit** it | **VIA-EXTENSION** — `genr`, ancillary and deliberately *outside* the descriptor so it never churns UUIDs (part 8.4) |
| P11 | **Bounded validation cost** — security reviewer | § 11.5 bounds `tile_count` by file size with an explicitly overflow-safe formulation; § 11.4 permits documented resource refusal through a distinct error path | **SATISFIED**, with two small gaps: **`compression ≠ None` has no minimum `length`** (§ 11 #14 bounds only the upper end), so all-but-the-last entry may be zero-length and a 4 GiB file may declare ~214 M entries; and the spec never *recommends* that readers document a cap. Both cheap (`M6`) |
| P12 | **Malicious packs / crafted extensions** | Genuinely strong: § 7.1 section-extent bounds, overflow-safe formulations spelled out inline in § 11 #19 and § 11.5, unknown-uppercase rejection (#20), padding checks (#19a, #33), tag-byte range checks (#27, #28), the § 11.1 byte-leak prohibition. Corroborated by the reader's 472,995 ASan+UBSan mutations with zero reports | **SATISFIED** — the best-engineered part of the spec |
| P13 | **Firmware: shared storage, quota** | Format's only obligation is stable identity for dedup = Appendix A | **OUT-OF-SCOPE** + `M1` |
| P14 | **Pack store: distribution, dedup, licensing** | Needs byte-identity distinct from recipe-identity (`M1`), licence metadata (`lics`), signing (`M7`) | **VIA-EXTENSION** + `M1` + `M7` |
| P15 | **Arbitrary key-value metadata** (MBTiles parity) | No generic KV store. The lowercase tag space is large but is a **flat, unmanaged namespace** — § 7.2 says writers MAY emit any lowercase tag, with no registry, so two independent apps both choosing `meta` collide silently | **VIA-EXTENSION** + **cheap process fix now**: a non-normative tag registry in the rawtiles repo and a stated convention |
| P16 | **Partial fetch / HTTP range** (PMTiles parity) | Deliberately not this format's job — a single seekable local file is the design centre | **OUT-OF-SCOPE**; the adjacent *transfer* need is real and is U13 |
| P17 | **Multi-language POI model** (mapsforge parity) | Raster format; POIs aren't in it | **OUT-OF-SCOPE** |
| P18 | **Multi-product / subscription fields** (Garmin JNX parity) | Maps onto store licence metadata | **VIA-EXTENSION** (`lics`) / **OUT-OF-SCOPE** for entitlement enforcement |

---

## 4. Extensibility stress tests

Each drilled as if implementing, recording where the spec bends and where it breaks.

**(a) Add RGB888. — Bends cleanly. WIRE-MINOR.** Set `pixel_format = 3` (already reserved for it,
§ 8.1), define `bytes_per_pixel → 3` in § 6.2's table and the intra-pixel byte order in a new § 9.3.
Then: § 6.2's `tile_dim² × bytes_per_pixel` is correct; § 11 #16's equality is correct; § 6.1's 0–3
byte padding absorbs the odd width (a 240² RGB888 tile is 172,800 B, already 4-aligned; a 241² one
is 174,243 B and pads to 174,244); § 9.11's RLE works with `P = 3`; `length` stays inside u32 for
any `tile_dim` the § 3 cap permits. **No rule needed a special case for a non-power-of-two width** —
the strongest evidence in this report that the § 6.2 abstraction was drawn in the right place.

**(b) Add an LZ-family compression. — Bends, but the spec's implicit promise doesn't generalise.**
Mechanically fine: § 6.2's compressed-layout clause already delegates to the compression's
subsection, per-tile `length` is already variable, and § 11 #16 already exempts compressed entries.
**What breaks is a promise made in prose:** § 9.11 advertises O(P) working memory and row-streamable
decode, and § 11.1's resource sketch budgets "≤ 8 B" of decoder state on that basis. A general LZ
decoder needs a window. § 5 measures exactly how big that window has to be — the answer (2 KiB) is
much friendlier than the spec's framing assumes, but it is still not 8 bytes, and the spec has **no
field declaring a pack's decoder-memory requirement**.

**(c) Add per-tile timestamps for delta updates. — VIA-EXTENSION, no wire change.** `tmet` sketch in
part 8.4: an ancillary lowercase section holding a `count == tile_count` parallel array in tile-index
order, so no key is repeated and lookup is the index position the reader already has. The tempting
alternative — the two spare `flags`/`reserved` bytes in each entry (§ 5.1) — is a trap: § 11 #12
makes readers reject non-zero, so it is old-reader-rejecting *and* only 2 bytes.

**(d) Add a signature block. — Breaks. Needs a decision before v1.0.** Two independent obstacles.
(i) § 10's CRC scope is every byte before the footer, so a signature section is *inside* the
integrity scope it is trying to establish. (ii) § 12.1 mandates section order by ascending 4-byte
tag, so `SIGN` sorts between `NAME` and `SRCD` — the excluded range lands mid-file. Clean
resolutions: declare signatures **detached** (a `.sig` sidecar — simplest, and the store is the
natural owner), or define the tag with an explicit § 12.1 ordering exception pinning it last so the
signed range is the prefix `[0, sign_section_start)`. Either is one paragraph now; neither is
available later without contradicting § 12.1.

**(e) Ship a 6 GiB country pack. — Breaks at § 3, but later than it first appears.**
`file_size ≤ 2^32 − 1`, every offset u32, and § 3's writer obligation is to "fail with a 'pack too
large' error rather than overflow". Quantified in `E2`/`E5`: uncompressed, the wall arrives at
country scale even at z14; with `RLE` it moves past a country at z0–14; with small-window deflate
past a country at z0–16. **Judgement: keep the cap.** u32 offsets are what let the § 11.1 lazy
profile work in ≤ 1 KB. But the spec should say out loud that the answer to "a continent" is **N
packs**, because right now a writer hitting the cap is told only that it failed — and part 6 shows
that is precisely what Garmin and Coros do.

**(f) Define a "pack collection" (ordered layering of N packs). — Undefined; solvable without the
wire format.** § 4.5's `parent_uuid` is reserved for precisely "future pack-compositing support" but
MUST be all-zero in v1, so the 16 bytes are present and unusable. An ancillary `coll` section
(part 8.3) does the job with no wire change and degrades correctly. **But part 6's prior art suggests
something simpler still:** Coros segments its maps into 1° × 1° files with the coordinates in the
folder path — the filesystem *is* the index. Ordering/priority needs no new field either:
`build_timestamp` plus `bbox` plus zoom range already support newest-wins.

**(g) Stream output without knowing the tile count up front. — Breaks for a non-seekable sink, and
breaks *unconditionally* for a reproducible writer.** The header precedes the index which precedes
the blob (§ 3), and the header carries `tile_count`, `bbox`, `zoom_min/max`, all 24 `zoom_offsets`
and `extensions_offset`, while the index carries every tile's `offset` and `length`. So byte 0
depends on the whole tile set. Two sub-cases:

- With `compression = None`, every `length` is `tile_dim²·bpp` (§ 11 #16) so all offsets are
  computable from `tile_count` alone, and the `(z, x, y)` set is analytically determined by a
  bbox × zoom request. A writer with a **seekable** sink can stream tiles into the blob and seek
  back to fill the header/index — exactly what slippypack's `.partial`-then-rename does. With a
  pipe, impossible. **Any compression removes even this**, since lengths become data-dependent.
- With a reproducible `pack_uuid`, impossible either way: § A.4's `content_hash` is SHA-256 over the
  writer's pre-quantisation RGB888 stream for the source's **complete** tile set, so the header's
  `pack_uuid` cannot be written until the last source tile has been seen. **A canonical-identity
  writer is inherently two-pass.** A live constraint on slippypack's planned OPFS-streamed multi-GB
  pipeline (`PLAN.md` Phase 8).

**(h) Split one pack into two regions, then re-merge byte-identically. — Holds under re-fetch; has a
gap under re-assembly.** Determinism survives re-partition because § A.4's per-source `content_hash`
is defined over the source's *complete configured* tile set and explicitly does not depend on
conflict resolution, while the top-level `bbox` and `zoom_range` keys differentiate the parts.
**The gap:** § A.4's canonical kind ordering is
`dir < geotiff < image < mbtiles < pbf < pmtiles < style < synthetic < url` — **there is no
`rawtiles` kind.** So a pack built by subsetting or merging *existing packs* (trim a region pack to
a route; merge two city packs) has no canonical descriptor and cannot claim a reproducible UUID.
That is a natural companion-app operation, and the fix is cheap and non-breaking: adding a kind name
to a lexicographically-compared list changes no existing descriptor.

---

## 5. Storage efficiency — what compression actually buys (`E5`)

The spec defines one compression (`RLE`, § 9.11) and reserves two (`QOI` at 2, `LZ4` at 3, § 8.5).
Since pack size drives flash use, BLE transfer time, the § 3 cap *and* — because open is O(file
bytes) — the `E3` CRC cost, this ladder is worth measuring rather than assuming.

**Method.** The 12 real 256×256 ABGR2222 tiles from `stanley.rawtiles` (786,432 B of pixels),
compressed **each tile independently** as § 5.1's per-entry `compression` byte requires.
Scripts in the bundle: `compress.py`, `window.py`, `fit.py`.

**Content census.** 29 distinct byte values across all 12 tiles (18–22 per tile) out of ABGR2222's
64; alpha is `3` everywhere as § 9.1 requires; order-0 entropy **2.297 bits/px** — a memoryless
floor of 28.7% of raw.

| scheme | % of raw | ratio | decoder cost |
|---|---:|---:|---|
| `compression = None` | 100.0% | 1.00× | none |
| **spec `RLE` (§ 9.11)** | 32.4% | 3.09× | ~30 lines C, O(1) RAM, row-streamable |
| LZ4 `-1` / `-9` / `-12` | 28.9 / 18.0 / **17.3%** | 5.78× | ~200 lines; **64 KiB window** |
| deflate, fixed-Huffman, 2 KiB window | 17.1% | 5.85× | **no Huffman tables** + 2 KiB ring |
| **deflate, dynamic-Huffman, 2 KiB window** | **13.0%** | **7.70×** | ~11 KB tables + 2 KiB ring |
| deflate, 32 KiB window (zlib default) | 13.1% | 7.61× | *no better than 2 KiB* |
| bzip2 / LZMA / zstd `-19` | 12.9 / 11.8 / 11.8% | ~8.5× | far too large for MCU |
| deflate + PNG `Sub` row filter | 15.6% | 6.39× | *worse than unfiltered* |
| `RLE` then deflate | 15.4% | 6.48× | *worse than deflate alone* |
| 4bpp palette, 16 colours (**LOSSY**) | 50.0% | 2.00× | 16 B palette + LUT expand |
| 4bpp palette + RLE (**LOSSY**) | 23.2% | 4.30× | LUT expand + RLE |

**Five results, three of them negative.**

1. **A 2 KiB deflate window is as good as 32 KiB** (13.0% vs 13.1%). Every useful match in this
   content is short-range, so the classic reason to reject deflate on an MCU — the sliding window —
   does not apply. With a 2 KiB ring the decoder stays **row-streamable**, the property § 9.11 and
   the § 11.1 resource sketch are built around. All-in state is ~13 KB (Huffman tables dominate),
   or **~2 KiB with fixed-Huffman-only** at 17.1%.
2. **`RLE` leaves ~2.5× on the table.** 32.4% vs 13.0%. (The spec's Appendix B v0.6 row quotes 27.0%
   for this corpus — that figure is the **RGB565** case, so it is consistent with this ABGR2222
   measurement, not contradicted by it.)
3. **LZ4 — the codepoint § 8.5 already reserves at 3 — is dominated on both axes.** 17.3% at best
   against small-window deflate's 13.0%, needing a 64 KiB window against deflate's 2 KiB. If only
   one more compression is ever added, the evidence says deflate.
4. **PNG-style row filtering makes it worse** (15.6%), and so does RLE-then-deflate (15.4%). Both
   destroy the literal structure LZ77 matches on. This content is palette-like, not continuous-tone;
   the usual image-compression instincts invert.
5. **The indexed/palette idea is a dead end for size.** 4bpp is 50% of raw *before* compression —
   above the 2.297 bits/px floor — and the lossy 16-colour version (23.2%) is still nearly twice
   lossless deflate. This removes the size motivation for § 8.1's reserved `L4` (`P2`, `U8`).

**Consequence for the § 3 cap** (`fit.py`, 128 px tiles):

| region | zooms | tiles | None | RLE (0.324) | deflate (0.130) | fits in 4 GiB with |
|---|---|---:|---:|---:|---:|---|
| Greater Vancouver | 0–16 | 27,627 | 0.42 G | 0.14 G | 0.06 G | all three |
| Switzerland | 0–16 | 585,449 | 8.94 G | 2.91 G | 1.17 G | RLE, deflate |
| United Kingdom | 0–14 | 564,154 | 8.62 G | 2.80 G | 1.13 G | RLE, deflate |
| France (metro) | 0–14 | 576,945 | 8.81 G | 2.86 G | 1.16 G | RLE, deflate |
| United Kingdom | 0–16 | 8,993,050 | 137 G | 44.6 G | 18.0 G | **nothing** |
| CONUS | 0–14 | 5,094,894 | 77.8 G | 25.3 G | 10.2 G | **nothing** |

**Two costs to weigh against `M8`.** Any compression forfeits row-random-access (a partial redraw
decodes from the tile start, where `None` permits a windowed seek+read, `U20`), and it defeats
transfer-layer block diffing, because variable lengths shift every later offset under § 11 #32's
tight layout (`U5`).

**Confidence: PLAUSIBLE, not CONFIRMED, as a general claim.** n = 12 tiles, one rendering style, one
small area, three adjacent zooms. The codec *ranking* is likely robust; the *magnitudes* are not —
the ABGR2222 span runs from ~13% (flat vector-rendered styles) to ~75–80% (all 64 colours varying
per pixel, e.g. dithered satellite). Settling step before writing an encoder: rebuild the corpus
over a satellite/aerial source and a contour-heavy topo style, at 128 px as well as 256 px, and
re-run `compress.py`. Also unmeasured: inflate throughput on the actual Cortex-M. Reasoning (not
measurement) says ~1–3 ms per tile against a 6–9 ms read that also shrinks ~7.7×, so the time cost
is likely a wash; that needs a device probe before it is asserted.

---

## 6. Prior-art mining — what the neighbours do differently

Walked for *forgotten needs*, not to relitigate the format choice. The Garmin/Coros rows are
verified against published format documentation rather than recalled (sources at the end).

### 6a. The architectural finding: nobody ships raw pixels

| System | What it actually stores |
|---|---|
| **Garmin basemaps (`.IMG`)** | **Vector** — TRE/RGN/LBL/NET/NOD subfiles, with a bespoke bit-packing scheme that "squeezes polylines and polygons into as few bytes as possible without having to resort to data compression" |
| **Coros (`.csm`)** | **The same thing** — `.csm` files are Garmin `.img` renamed. Segmented into **1° × 1°** files (1° × 0.5° when large), with the coordinates encoded in the folder path; ~10.6 MB per segment |
| **Garmin BirdsEye (`.JNX`)** | **Raster, JPEG payload** — a levels table, a tile table, then tiles; up to ~5,000 tiles per level; plus an encrypted device unit ID for DRM |
| **Garmin Custom Maps (`.KMZ`)** | **Raster, JPEG payload** — georeferenced images in a zip with KML corner pins |
| **rawtiles** | **Raw display-native pixels** — the whole point, and the whole cost |

Three things follow that matter to this review:

- **rawtiles' structural peer is JNX, not IMG.** JNX is almost exactly this design — levels table,
  tile table, tiles — and the only material difference is the payload: JPEG versus raw. Vector is
  off the table for this device (rasterizer, fonts, styling — settled in the prior evaluation), so
  the honest question is not "why not vector" but "how far up the compression ladder should the
  payload go", which is part 5's question. Deflate at 13% is the defensible middle: lossless, ~2 KB of
  decoder, no artefacts fighting the 2-bit-per-channel quantisation, no IDCT.
- **The 4 GiB cap is not unusual, and the industry fix is the escape hatch this report recommends.**
  Garmin `.img` carries the same sub-4 GB limit and the documented answer is multiple files. Coros's
  1° × 1° segmentation is the `coll` idea — except the **filesystem path is the index**, which
  requires no format change at all. That is a simplification of part 8.3's sketch, not a confirmation
  of it (`P9`, stress test (f)).
- **Garmin puts DRM in the container; rawtiles deliberately does not.** JNX's encrypted unit ID is
  the mechanism a paid-chart or subscription product needs (`U17`, `P18`). Keeping it out is the
  right call for an open format — but it means the store, not the pack, owns entitlement, and the
  report says so rather than leaving it implicit.

### 6b. Capability-by-capability

| Prior art | Capability | Transfers? |
|---|---|---|
| **MBTiles** | `metadata` table = arbitrary string KV | **Partly** — `P15`; the lowercase TLV space is the equivalent but is an unmanaged namespace. A registry + convention is the cheap answer |
| **MBTiles** | `grid`/UTFGrid interactivity | No — needs a hit-test model this raster path doesn't have |
| **PMTiles** | Internal compression + directories for **partial HTTP fetch** | No (`P16`) — deliberate. The *adjacent* need (resumable BLE transfer) is real and is `U13` |
| **PMTiles** | Root/leaf directory hierarchy for huge tile counts | No — § 4.12's flat 24-slot zoom directory plus a sorted index is cheaper, and the 4 GiB cap bounds tile count to ≤ 262 k (128 px). Confirmed by `E2` |
| **PMTiles** | Duplicate-tile deduplication | **Yes, and it is foreclosed.** § 11 #32's tight layout means two `(z,x,y)` entries cannot share bytes. Ocean/blank tiles are highly repetitive, so this costs real flash — though `E5` shows compression recovers most of it (a solid tile deflates to a few hundred bytes). Judged **right call**: tightness is what makes the open walk a single accumulator |
| **Garmin JNX** | Multi-product / subscription + expiry + DRM | `P18`/`lics` for metadata; entitlement enforcement **OUT-OF-SCOPE** (part 6a) |
| **Garmin KMZ** | **Many independently-georeferenced images per file** | **Yes — the sharpest single lack.** KMZ carries N ground overlays; rawtiles' `LocalLinear` is capped at exactly one (§ 8.6). The real shape of `U9`/`U18` is not "tile my floor plan" but "carry my six trail-sign photos". Currently: six packs. **WIRE-MINOR**, need moderate; a naming convention or `coll` covers most of it |
| **Garmin `.img` / Coros `.csm`** | Sub-4 GB cap solved by **spatial segmentation with coordinates in the path** | **Yes** — this is the answer to `U6`/`P9`, and it is cheaper than the in-file `coll` tag § 8.3 sketches |
| **OruxMaps / Locus** | Calibrated arbitrary-projection maps | Serves `U9` the same way — warp, or accept single-image. Confirms warping is the industry's normal answer |
| **OruxMaps** | Layered map + overlay **with transparency** | **Yes** — `U7`. Independent confirmation that overlay-with-alpha is a normal expectation of this category |
| **mapsforge** | Multi-language POI/label model | No (`P17`) — vector concern |
| **mapsforge** | Per-zoom-interval sub-files with differing tile geometry | No — mixed `tile_dim` deliberately foreclosed (`P3`), judged right |
| **All of them** | A published byte-level spec with a negative-fixture conformance corpus | rawtiles **exceeds** the prior art; 17 golden + 79 negative fixtures with per-rule access patterns (§ 14.6) has no equivalent above |

---

## 7. Tradeoff audit — the deliberate limits

| Limit | Verdict | Reasoning |
|---|---|---|
| **u32 offsets / 4 GiB cap** (§ 3) | **Right call — needs its escape hatch named** | Load-bearing for the § 11.1 ≤ 1 KB lazy profile. `E5` shows it binds at continent scale, not country scale, once compression is on. The spec must say the answer is N packs (part 6a), not just "fail" |
| **One `pixel_format`, one `tile_dim` per pack** | **Right call** | Both are what make § 11 #16 a single exact-length equality — the cheapest possible per-entry check. `E5`: tile size barely affects compression (12.8–13.1% at 64/128/256 px), so nothing is lost |
| **Two projections; only one tileable** (§ 8.6) | **Right call, one honest hole** | Warping into WebMercator serves nearly every case (part 6b). The hole is polar (> ±85.051129°, § 4.9), which no warping fixes |
| **`RLE` as the only compression** (§ 8.5) | **Right call for v1; the ladder above it is mis-ordered** | Decoder ~30 lines, O(P) state, row-streamable — the correct v1 choice. But `E5`: deflate at a **2 KiB** window reaches 13.0% vs RLE's 32.4% and stays row-streamable, while **LZ4, reserved at codepoint 3, is dominated on both axes**. See `M8` |
| **CRC-32 at the footer only** (§ 10) | **Wrong call as it stands — needs an escape hatch before v1.0** | `E3`: 147 s at the format's ceiling; the only conforming alternative is trusting the pack entirely. No per-region integrity exists, and the device has a documented corruption mode. This is `U13`/`blkh` |
| **Attribution as free text** (§ 7.3) | **Right call** for the legal minimum | ODbL's "reasonably calculated" standard is a content test, and free text meets it. A store needs structure — that is `lics`, not a redesign |
| **No DRM / entitlement in the container** | **Right call** | Garmin bakes an encrypted unit ID into JNX (part 6a). Keeping it out keeps the format open; the consequence — the store owns entitlement — should be stated, not implied |
| **Flat TLV, uppercase=critical / lowercase=ancillary** (§ 7.2) | **Right call, and stronger than it looks — but only on one half** | Verified exact rule: uppercase first byte ⇒ readers MUST reject unknown; lowercase ⇒ MUST accept and MAY ignore; any other ⇒ MUST reject. The lowercase half is a genuinely unlimited additive channel and carries every `VIA-EXTENSION` grade here. The uppercase half and the enums are old-reader-rejecting (§ 13.1), so they are a *versioning* mechanism, not an extension mechanism. **The spec should say this plainly** — the single most important thing an implementer needs to know about growing the format |
| **Mandatory eager index walk** (§ 11.2 note on #18/#32) | **Right call — my initial objection was falsified** | The cap bounds the index at ≤ 4.99 MiB (128 px) / ≤ 1.25 MiB (256 px) ⇒ ≤ 0.18 s at 27.8 MiB/s measured. v0.5's reasoning holds. Bundle `E3` |
| **Tight tile blob, no dedup** (§ 11 #32) | **Right call, worth documenting** | Enables the one-u32-accumulator open walk; costs duplicate-tile dedup (part 6b) and in-place partial update (`U5`), and blocks transfer-layer block diffing once compression is on (part 5) |
| **`parent_uuid` reserved-but-unusable** (§ 4.5) | **Neutral, trending vestigial** | 16 header bytes idle. If part 6a's filesystem-convention answer to collections wins, these bytes will never be used — worth admitting rather than leaving a reader to wonder |

---

## 8. Extension-tag sketches

These are the *proofs* behind the `VIA-EXTENSION` grades. All are **lowercase/ancillary** (§ 7.2) —
that is what makes them free: a reader that has never heard of them MUST accept the pack and MAY
ignore the section (§ 11 #21), so nothing fragments. All obey § 7.1 framing (4-byte tag, u32 LE
length, payload, zero-pad to 4) and § 12.1 ordering (ascending tag, so they naturally follow the
uppercase reserved tags). All must be **excluded from § A.3's descriptor** unless noted, so they
never churn `pack_uuid`.

### 8.1 `blkh` — block integrity (define now; serves U13, E3's gap, P7, resumable transfer)

```
off  size  field
  0     1  algo         1 = CRC-32/ISO-HDLC (4-byte digests), 2 = SHA-256 (32-byte)
  1     1  reserved     0
  2     2  reserved     0
  4     4  block_size   bytes per block; power of two, >= 4096
  8     4  block_count  == ceil(extensions_offset / block_size)
 12   ...  digests      block_count digests, digest_width(algo) each, in file order
```

**Covered range is `[0, extensions_offset)`** — header + index + tile blob. Deliberately *not* the
whole file: that avoids the self-reference problem that sinks a signature (part 4(d)), since `blkh`
itself lives at or after `extensions_offset`, and it covers exactly the region that is expensive to
verify and whose corruption produces wrong pixels. The small extension region stays covered by the
§ 10 footer CRC, which is cheap.

**Why this shape.** A reader can verify *only the blocks a tile actually touches* — at
`tile_dim = 128` uncompressed a tile is one 16 KiB block, so per-tile verification costs about what
the read already costs. A companion app transferring in `block_size` chunks verifies each chunk on
arrival and resumes from the first bad one. Shared-storage firmware verifies once and lets every app
trust. And a store gets a byte-identity handle: with `algo = 2` the digest list is the content
commitment the recipe-based `pack_uuid` (`M1`) does not provide.

**Interaction with `M8`.** Under compression, tiles are variable-length and no longer align to
blocks, so a tile may straddle two blocks and a block may hold several tiles. That is fine for
transfer and scrub, and only slightly worsens per-tile verification (2 blocks instead of 1); pick
`block_size` ≥ the largest expected compressed tile to keep it at 2.

**Why ancillary.** A reader that ignores it falls back to § 10 exactly as today — strictly no loss.

### 8.2 `sups` — multi-supersedence (define now; serves U4)

```
off  size  field
  0     4  count        number of superseded UUIDs, >= 1
  4  16×n  uuids        16-byte UUIDs, sorted ascending as unsigned bytes (determinism)
```

Complements § 4.4's single `supersedes_uuid` (which SHOULD carry the first entry so old readers
retire *something*). Ancillary: a reader that ignores it keeps stale packs — degraded, not wrong.

### 8.3 `coll` — collection membership (specify only if a naming convention proves insufficient)

```
off  size  field
  0    16  collection_uuid
 16     2  index        0-based position in the collection
 18     2  total        member count, 0 = unknown/open-ended
 20     1  name_len
 21   ...  name         UTF-8, NFC per § 7.3's text rule
```

This is the named escape hatch for the 4 GiB cap: a continent is a collection of region packs.
**Downgraded from "define now" by part 6a:** Garmin and Coros solve the identical problem with spatial
segmentation and coordinates in the file path, needing no format support whatsoever. Prefer that;
reach for `coll` only when membership must survive the file being moved or renamed — which is
exactly the case a store cares about, so this tag is likely to be wanted eventually, just not first.

### 8.4 Second tier — reserve the names now, specify when needed

| Tag | Payload sketch | Serves |
|---|---|---|
| `tmet` | `u8 kind` (1 = u32 epoch-seconds, 2 = u32 source-revision), `u8` reserved, `u16 stride`, `u32 count == tile_count`, then `count × stride` records **in tile-index order** | P5, U5 delta updates |
| `covr` | `u8 zoom`, `u32 x0, y0, w, h`, then `ceil(w·h/8)` bits row-major; bit set = some tile at some zoom covers that cell | U3, U15, P8 — fixes `bbox` over-claim |
| `lics` | `u8 id_len`, SPDX-style identifier (e.g. `ODbL-1.0`), then optional UTF-8 URL | U12, P14, P18 |
| `genr` | UTF-8 `name/version` (e.g. `slippypack/0.3.1`) | P10 diagnostics — **must** stay out of § A.3 so writer upgrades don't churn every UUID |

---

## 9. Ranked gap list

Ranked strictly by **cost of late discovery** — what a fix costs *after* the wire format freezes and
people have packs on disk, divided by what it costs today.

### 9a. Must fix before the v1.0 freeze

| # | Gap | Break | Fix | Is the need real enough to pay now? |
|---|---|---|---|---|
| **M1** | **§ A.3's canonical descriptor under-determines the pack bytes.** Missing keys: `compression` (per-tile, so it can never enter implicitly), `supersedes_uuid`, and every extension payload. Consequences all CONFIRMED: (a) **same UUID, different tile blobs** — `E1`, 4,712 B vs 680 B, UUID `f098e3da-…`; (b) **self-supersedence** — a rename+supersede pack derives the UUID it supersedes, so a § 4.4-driven eviction evicts the new arrival; (c) attribution/name strip is UUID-invisible | Falsifies § A.4's explicit promise ("entitled to assume *byte-identical* tile blobs"). Any store, companion app, or device cache keyed on `pack_uuid` can serve or skip the wrong pack | Add `compressions` (sorted array of the distinct per-tile compression bytes actually used), `supersedes` (hex or `null`), and a digest over the ordered extension sections. **And state in § A.2 whether `pack_uuid` is recipe-identity or byte-identity** — if recipe, define the separate content commitment (`blkh` with `algo = 2`) that consumers needing byte-identity must use | **Yes, and this is the whole reason to audit now.** A descriptor change invalidates *every* `pack_uuid` ever derived. Today that is zero real packs; after slippypack ships and people hoard, it is a migration of every store index and device cache. Highest cost-of-late-discovery in the report by a wide margin. **`M8` sharpens it**: if a deflate codepoint lands, the same-UUID-different-bytes hazard doubles |
| **M2** | Nothing forbids `supersedes_uuid == pack_uuid` | A conforming pack can instruct readers to evict itself | One reader rule + one negative fixture. Belt-and-braces alongside M1, which fixes the canonical path but not a non-canonical writer | Yes — one line, one fixture |
| **M3** | **§ 7.4's BCP-47 subset is too narrow**: `[a-z]{2}` / `[a-z]{2}-[A-Z]{2}` only, so no `zh-Hant`/`zh-Hans`, no `sr-Latn`, no 3-letter ISO-639-3. § 11 #37 makes a non-conforming tag a **whole-pack rejection** | Widening later is a *relaxation*, and § 13.1's minor-bump rules mean readers built before it **reject the entire pack** — a display-name nicety killing the map | Widen to `language(2–3) + optional script(4, Titlecase) + optional region(2)`; keep the case rules normative (they exist to prevent cross-writer divergence, and this shape preserves that). Refresh `neg-37c`, add a script-subtag golden | **Yes.** Chinese script distinction is the most common real-world need for this field; cost is a grammar line plus two fixtures now versus a fragmented reader population later |
| **M4** | **Transparency is foreclosed with no reserved path.** § 9.1 mandates `A = 3` *and* mandates readers ignore alpha; § 9.2 has no alpha. slippypack already names `/maps/overlay.rawtiles`; OruxMaps confirms the expectation (part 6b) | If alpha is later enabled under `pixel_format = 1`, old readers **accept and silently mis-render** — the worst failure mode available | Reserve a codepoint (e.g. `pixel_format = 7 = ABGR2222_A`) in § 8.1's table and add one sentence: codepoint 1's alpha semantics are frozen forever; alpha-bearing content uses its own codepoint | **Yes** — a table row and a sentence now, converting a future silent-misrender into a clean rejection |
| **M5** | **Reserved codepoints that cannot land as specified.** (a) `L4`(5)/`L1`(6) contradict § 2's integer `bytes_per_pixel`, § 6.2's length formula, § 11 #16's equality, and § 9.11's pixel-unit runs; "`L4` indexed" also has **no palette mechanism anywhere**. (b) § 9.11's bounded/row-streamable decoder promise is RLE-shaped, but § 11.1's resource sketch budgets against it as if format-wide | Discovering this when landing a new format means either a wire-major or quietly retracting a promise readers were told to size against | Make the width function bits-aware (`ceil(·/8)`; identical for all v1 formats); state that sub-byte formats need their own compression codepoint; state in § 8.5 that bounded decoder state is per-compression and any future compression MUST declare its working-set bound | **Yes as wording** — no wire change, keeps the reservations honest. But `E5` guts the *motivation* for `L4`: it is a size regression. Fix the text; don't plan to land it |
| **M6** | **No minimum tile `length` for `compression ≠ None`** (§ 11 #14 bounds only the upper end), so all-but-the-last entry may be zero-length and a 4 GiB file may declare ~214 M entries; and the spec never recommends readers document a resource cap | Not a DoS *beyond* the CRC cost the reader already pays (hypothesis tested and downgraded — `E3`), but it admits absurd packs and leaves reader divergence undocumented | Require `length ≥ 1` for `compression ≠ None`; optionally the computable bound `length ≥ ceil(tile_dim²/128) × (1 + P)` (256 B min-encoded for `tile_dim = 128, P = 1`); add § 11.4 wording recommending readers document their cap. Ships with the existing PR-0 § 9.11 exact-consumption clarification | Yes — small, and it lets a reader bound `tile_count` from the header alone |
| **M7** | **No clean seat for a signature** — § 10's CRC covers any signature payload, and § 12.1's ascending-tag ordering puts `SIGN` mid-region, making the signed range a hole | § 12.1's ordering is normative, so a "signature sorts last" exception is a *change* to a frozen rule once v1.0 lands | Decide and write it down: either "signatures are detached sidecars, out of scope" (recommended — the store owns trust, as it does for Garmin's JNX DRM) **or** reserve `SIGN` with an explicit § 12.1 ordering exception pinning it last | **Yes, as a decision** — a paragraph now versus contradicting a frozen rule later. Implementation can wait |
| **M8** | **The compression ladder is mis-ordered** (part 5). `RLE` = 32.4%; **deflate at a 2 KiB window = 13.0%**, still row-streamable; **LZ4, reserved at § 8.5 codepoint 3, is dominated on both axes** (17.3%, 64 KiB window). Negative results in the same run: PNG-style filtering hurts, RLE-then-deflate hurts, and 4bpp palette is a size regression | Nothing breaks; but the reserved codepoints signal the wrong next step, and every pack written before a better compression exists is ~2.5× larger than it needed to be — in flash, in BLE transfer time, and in `E3` open cost | Reserve a `deflate` codepoint in § 8.5 **with a declared maximum window size** (2 KiB), so the § 11.1 resource sketch stays honest. Reconsider the LZ4 reservation. Pair with `M5`'s per-compression working-set requirement | **As a reservation, yes** — one table row, costs nothing, and it stops LZ4 being implemented first on the strength of a reservation the data contradicts. **As an implementation, not yet:** `E5` is n = 12 tiles of one style, and RLE is adequate for region-scale packs. Measure on satellite and topo corpora first |

### 9b. Should be defined as extension tags now (so early writers agree)

Wire-free; the cost of late discovery is *incompatible private tags* in packs already in circulation.

1. **`blkh`** (part 8.1) — block integrity. Unblocks resumable BLE/companion transfer, gives large packs
   any integrity at all, and gives a store byte-identity. Highest value here; the Gadgetbridge
   direction will otherwise invent a private equivalent.
2. **`sups`** (part 8.2) — multi-supersedence. Cheap, and eviction logic is exactly what two
   implementations will otherwise solve differently.
3. **Reserve `tmet`, `covr`, `lics`, `genr`** (part 8.4) with sketches, unspecified until needed.
4. **A tag registry + lowercase-namespace convention** in the rawtiles repo (non-normative). § 7.2
   invites every app to mint lowercase tags with no collision management; a registry file costs
   nothing now and cannot be retrofitted onto packs in the wild.
5. **`coll`** (part 8.3) — *demoted* from this list by part 6a. Try a filesystem naming convention first,
   as Garmin and Coros do; specify the tag when membership must survive a rename.

### 9c. SDK-layer backlog (spec is fine; the reader/platform must build it)

- **Pack discovery / enumeration** across a sandbox-relative filesystem (PR Q4), plus the
  coordinate-in-path naming convention part 6a recommends.
- **Multi-pack composition** and a newest-wins overlap policy — `build_timestamp` + `bbox` + zoom
  range are sufficient inputs (P8).
- **Viewport → tile math** (absent on the tutorial branch today) and a bounded tile cache.
- **Windowed sub-tile reads** for pan — `readTileRows()` already exists on
  `feat/rawtiles-container`; note part 5's warning that compression forfeits this.
- **Day/night tone mapping** at blit time (U8).
- **RLE decoder** (PR Q2) — and note `E4`: the only writer currently emits *v0.5 byte-level* RLE.
- **A documented trust mode** for large packs — § 10 requires an explicit opt-in and documentation;
  `Container` has no such option today (`Libs/Source/RawTiles/Container.cpp:875`), which `E3` says
  becomes untenable above ~100 MiB.
- **Coverage probing** until `covr` exists.

### 9d. Won't fix, with rationale

| Item | Rationale |
|---|---|
| `zoom > 23` | z23 = 1.87 cm/px; a 240×240 panel cannot use it. Lifting means growing `zoom_offsets[24]` → wire-major |
| Single pack > 4 GiB | u32 offsets make the ≤ 1 KB lazy reader profile possible; the answer is segmentation (part 6a) |
| `LocalLinear × Quadtree` | Warping into WebMercator is the industry-standard answer and is fully served today (§ 8.6, part 6b) |
| Polar projections | Real but tiny population; `WIRE-MINOR` whenever someone actually needs it |
| Multiple `tile_dim` per pack | Would break § 11 #16's single exact-length equality, and `E5` shows tile size barely affects compression anyway |
| In-format DRM / entitlement | Distribution layer's job; Garmin's in-container approach is the wrong model for an open format (part 6a) |
| Arbitrary KV metadata store | Lowercase TLV + a registry is the same capability with better bounds (P15) |
| HTTP-range partial fetch | Deliberate non-goal; design centre is a local seekable file (P16) |
| Duplicate-tile dedup | § 11 #32's tightness makes the open walk one accumulator, and compression recovers most of the loss. Document, don't fix |
| Lossy / JPEG payload (the JNX model) | Fights ABGR2222's 64-colour quantisation, needs an IDCT, and gives up the zero-decode property that motivates the whole format (part 6a) |

---

## 10. Answers to the container PR's open questions 3 and 4

**Q3 — "Is commit-pinning against a personal external spec repo acceptable?"**

**The pinning is fine; the *freeze* is the risk, and this audit sharpens why.** The PR's core claim
is "freeze the API, not the wire format", and that claim **survives**: none of `M1`–`M8` changes the
header layout, the index layout, or any `Container` signature. `M1` is an Appendix A change,
invisible to a reader treating `pack_uuid` as 16 opaque bytes. `M3` changes *what* § 11 #37 rejects
— a behaviour change inside an existing method. `M4`/`M5`/`M8` are enum codepoints, exactly the path
RLE already demonstrates. So the PR can land on a pinned commit without betting the API on the spec.

What the audit *does* say is that the v1.0 gate is not near. Every defect in part 9a was found by the
activity a "second independent consumer" performs — reading the spec against a needs inventory
rather than against a reference implementation. `E1` is the proof that the single-author loop misses
things: the writer's own help text *documents* the same-UUID-different-bytes behaviour as expected,
because with one author there was no second party to be surprised by it. Concretely: **do not
declare v1.0 until `M1` is settled**, because it is the one item whose late fix invalidates
artefacts users have already made.

**Q4 — "Cross-app pack sharing: per-app duplication, or a shared read-only mount?"**

The matrix says the spec is **already almost done here**, and names the missing piece.

- **Stable cross-app identity exists and is the right key.** § A.1's fixed namespace plus § A.2's
  UUIDv5 over a canonical descriptor gives two writers the same UUID for the same logical inputs —
  precisely the dedup key a shared store needs, and no spec change is required to *share* packs.
- **But it is currently unsound as a byte-dedup key** (`E1`). Two packs with one UUID and different
  blobs, in a shared mount, is a wrong-pixels bug rather than a wasted-bytes bug. **`M1` is a
  prerequisite for shared storage**, not an independent nicety.
- **`blkh` is what makes sharing safe as well as sound.** In a shared mount the verifier and the
  consumer are different processes; a whole-file CRC costing 3.6 s at 100 MiB (`E3`) cannot run
  per-app-per-open. Block digests let firmware verify once at install and each app verify only the
  blocks it reads.
- **Recommendation.** Per-app duplication is **acceptable today and stops being acceptable at the
  scale `E2`/`E5` show users will reach**: region packs are tens of MiB against multi-GB eMMC, so
  two copies is affordable; at the 432 MiB / 2.24 GiB sizes a Greater-Vancouver or Switzerland pack
  implies, it is not. So the ask to firmware is real but not urgent, and it is small: **one
  read-only shared location, with `pack_uuid` as the dedup key and the pack's own bytes as the
  contract** — no new kernel API, no per-app quota negotiation. The format's contribution is `M1`
  (make the key sound) plus `blkh` (make verification per-app-affordable). Given the kernel's long
  lead time, the useful thing to hand firmware now is that two-sentence requirement, and it does not
  depend on any remaining spec question.

---

## 11. Change list for the `rawtiles` repo

Mergeable into the existing "PR 0 spec debt" stack (RLE encoder + missing RGB565/RLE goldens +
§ 9.11 exact-consumption wording). Ordered so each commit stands alone.

**Spec edits**

| # | Section | Change |
|---|---|---|
| 1 | § A.3, § A.2 | `M1`: add `compressions`, `supersedes`, and an extension-section digest key. State whether `pack_uuid` is recipe- or byte-identity, and name the byte-identity mechanism. Update § A.5's worked example, its intermediate SHA-1, and its derived UUID |
| 2 | § 4.4, § 11 (new rule #40) | `M2`: `supersedes_uuid` MUST NOT equal `pack_uuid`; readers MUST reject |
| 3 | § 7.4, § 11 #37 | `M3`: widen the restricted BCP-47 subset to `language(2–3)[-Script(4)][-REGION(2)]`, keeping case normative |
| 4 | § 8.1 | `M4`: reserve an alpha-bearing codepoint; state that `pixel_format = 1`'s alpha semantics are frozen |
| 5 | § 2, § 6.2, § 8.1, § 8.5, § 9.11, § 11.1 | `M5`: bits-aware width function; sub-byte formats need their own compression codepoint; indexed formats will need a palette tag; bounded decoder state is per-compression and future compressions MUST declare their working-set bound |
| 6 | § 5.2, § 11 #14, § 11.4 | `M6`: minimum `length` for `compression ≠ None`; note recommending readers document their resource cap |
| 7 | § 10 or § 12.1 | `M7`: the signature-seating decision (detached, or `SIGN` with an ordering exception) |
| 8 | § 8.5 | `M8`: reserve a `deflate` codepoint with a declared max window (2 KiB); reconsider the LZ4 reservation, which `E5` shows is dominated on both ratio and RAM |
| 9 | § 7.3 (new subsection) + registry file | part 9b tags: `blkh`, `sups` specified; `tmet`, `covr`, `lics`, `genr`, `coll` reserved with sketches; non-normative lowercase-tag registry + convention |
| 10 | § A.4 | Add `rawtiles` to the canonical kind ordering (non-breaking — the list is lexicographic) and define its `content_hash` domain, so pack-derived packs can claim reproducible identity (stress test (h)) |
| 11 | § 3 / § 12 | Name the cap's escape hatch: a writer that would exceed 4 GiB SHOULD emit a segmented set, with a recommended coordinate-bearing naming convention (part 6a), not merely fail |
| 12 | § 14.1 | Note that a canonical-identity writer is inherently two-pass (`content_hash` spans the complete source tile set, and the header precedes the blob) — stress test (g) |
| 13 | § 8.5 / § 9.11 | Document the compression tradeoffs `E5` surfaced: no row-random-access, and (with § 11 #32) compression defeats transfer-layer block diffing |
| 14 | Appendix B | Correct the v0.6 claim that "no v0.5 `RLE8` packs exist in the wild": the only published writer emits byte-level RLE today (`E4`) |
| 15 | § 5 / § 11 #32 | Note that tightness forecloses duplicate-tile dedup, and why that is the right trade |

**New fixtures**

| Fixture | Pins |
|---|---|
| `neg-40-supersedes-self.rawtiles` | `M2` |
| `golden-names-script-subtag.rawtiles` (+ `.hashes`) | `M3` — `zh-Hant`, `zh-Hans`, `sr-Latn`, a 3-letter language, and the § 12.1 payload-byte-order trap across mixed tag lengths |
| `neg-37c` refresh | Repoint at a shape still illegal after `M3` |
| `neg-41-rle-zero-length.rawtiles` | `M6` |
| `golden-rgb565-grid`, `golden-rle-abgr`, `golden-rle-rgb565` (+ `.hashes`) | Already promised by § 14.3/§ 14.5 and absent — pre-existing PR-0 debt; `E4` shows the gap is actively hiding a writer/spec divergence |
| `golden-blkh.rawtiles` | The new ancillary tag, and the § 11 #21 accept path over a *known* lowercase tag |
| `neg-07b` / `neg-07g` repurpose | Already flagged stale in Appendix B's v0.6 row |

**Registry entries:** `blkh`, `sups` (specified); `tmet`, `covr`, `lics`, `genr`, `coll` (reserved).

---

## 12. Spin-offs (real, out of scope here)

| Finding | Target |
|---|---|
| **`E4`: slippypack emits v0.5 *byte-level* RLE** (`crates/slippypack-core/src/format/rle8.rs`; CLI `--compression` help), so `--compression rle8 --pixel-format rgb565` produces packs a v0.6 decoder mis-decodes | `slippypack` — rename to pixel-level `RLE` per § 9.11, add the § 14.8 RGB565 cases as tests |
| **slippypack's `--compression` help documents the `M1` defect as expected behaviour** ("the recipient compares pack bytes to detect the drift") | `slippypack` — update once `M1` lands |
| `DECISIONS.md` F-002 ("header is exactly 322 bytes") and F-005 ("index entry is 24 bytes") are stale; the code is correct at 292/20 (`format/header.rs:32`, `format/tile_index.rs:23`) | `slippypack` — docs only |
| A canonical-identity writer cannot be single-pass (stress test (g)), constraining `PLAN.md` Phase 8's OPFS-streamed pipeline | `slippypack` — `PLAN.md` |
| `PLAN.md` names `/maps/overlay.rawtiles`, which the spec cannot represent (`U7`/`M4`) | `slippypack` + the watch-side map plan |
| `Container` has no § 10 caller-asserted-trust option, needed above ~100 MiB (`E3`) | `feat/rawtiles-container` follow-up |
| Re-run the device probe against ~100 MiB and ~1 GiB packs to replace `E3`'s single-point extrapolation with a curve; and measure inflate throughput on-device before acting on `M8` | `spike/rawtiles-device-proof` |
| Re-run `compress.py` over satellite/aerial and contour-topo corpora at 128 and 256 px before committing to a compression codepoint | `rawtiles` / `slippypack` |

---

## 13. What a reviewer should take away

- **What the format can serve today:** one blit-ready raster basemap per pack, any bbox × zoom
  ≤ 23, two pixel formats, both axis conventions, sparse/route-shaped coverage, whole-pack
  supersedence, localized names (Latin-script two-letter locales), ODbL-sufficient attribution,
  hand-georeferenced single images — up to 4 GiB, which with `RLE` is a country at z0–14.
- **What it can absorb without breaking:** everything metadata-shaped, through the lowercase TLV
  channel (integrity, collections, per-tile data, coverage, licence, provenance); and new pixel
  formats and compressions through enum codepoints — at the cost that old readers reject packs
  using them.
- **What it can never do:** exceed 4 GiB in one pack, exceed z23, tile a non-WebMercator projection,
  reach the poles, or carry transparency under `pixel_format = 1`.
- **What it is leaving on the table:** ~2.5× of pack size. `RLE` is 32.4% of raw where deflate at a
  2 KiB window is 13.0% and still row-streamable — while the codepoint the spec actually reserves
  next (LZ4) is worse on both ratio and RAM (part 5).
- **What must change this month, while breaking the wire format is still free:** `M1` — Appendix A's
  descriptor must determine the bytes, because `pack_uuid` is the one contract that becomes a
  migration rather than an edit the moment users have packs. Everything else in part 9a is wording, a
  table row, or a fixture.

---

**Sources for part 6** (published format documentation; consulted 2026-08-06):
[Garmin Raster Maps (KMZ/JNX)](https://www.bluemarblegeo.com/knowledgebase/global-mapper/Formats/Garmin_Raster_Maps_(KMZ_JNX).htm) ·
[Garmin BirdsEye JNX format hacking](https://www.gpspower.net/creating-maps/168766-garmin-birdseye-jnx-format-hacking.html) ·
[Exploring Garmin's IMG format (TRE/RGN/LBL/NET/NOD)](https://www.pinns.co.uk/osm/docs/expl_img2015.pdf) ·
[OSM Map On Garmin/Download](https://wiki.openstreetmap.org/wiki/OSM_Map_On_Garmin/Download) ·
[Custom maps on Coros watches](https://panaetius.github.io/swiss-topo-maps-on-coros/) ·
[Downloading Maps to Your COROS Watch](https://support.coros.com/hc/en-us/articles/4405711354900-Downloading-Maps-to-Your-COROS-Watch)
