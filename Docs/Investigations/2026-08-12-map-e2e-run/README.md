# Investigation — the power-user map workflow, end to end

**Date:** 2026-08-12 (started; verdicts below are unfilled until each link is run)
**Card:** `E3` in `MAP_TOOLCHAIN_PROMPT.md` (repo root).
**Subject:** `slippypack` @ `origin/map-delivery-workflow` `b8d5464`, `rawtiles` @ `main`
`38d4d26` (spec v0.6), `una-sdk` @ this branch, AthensRun PoC app.
**Nothing is to be posted to GitHub.** `gh` read-only; see § 0 of the card.

The question is not "can a pack be built" — the Athens pack proved that. It is **whether one
person can go from nothing to a map on the watch by a route that is compliant at every step,
and write it down so the next person does not improvise.** The card predicts this run will
reorder Group D more reliably than the board's guesses about which friction matters; the
reordering is therefore a required output, not a by-product.

**An unfilled Verdict means the link has not been run.** Do not infer success from silence,
and record the failures — per § 0 they are usually the useful part.

---

## Why this route is legitimate, stated once

No tiles are fetched from anyone's tile server. What is downloaded is **vector data through a
bulk channel the provider publishes for exactly that** — the Protomaps Basemap, an ODbL
Produced Work (`MAP_COMPLIANCE_APPENDIX.md` § 3.2, verdict **PERMITS**, with the ask that you
copy the tileset to your own storage rather than hotlink). Raster tiles come into existence
only on this machine, out of a renderer we run. The only tile host `slippypack` contacts is
loopback.

The distinction is the whole board's through-line: no compliant ready-to-quantise raster
basemap source exists, so the project renders its own. Fetching a hundred tiles from
`tile.openstreetmap.org` would be as prohibited as fetching a hundred thousand — the policy
names pre-emptive fetching and archive-building, not a volume.

## Environment, as found 2026-08-12

| tool | state |
|---|---|
| `pmtiles` CLI | **absent** — needed for L1 |
| `tileserver-gl` | **absent** — needed for L2 |
| `docker` | present (`/usr/bin/docker`) |
| `node` / `npm` | present |
| `cargo` | present |
| `slippypack` clone | `~/git/rust/slippypack`, on `main` @ `1f9132d` — **fetch and branch off `origin/map-delivery-workflow` first** |
| `rawtiles` clone | none on this machine; `git clone http://nas:3000/toby/rawtiles.git` (Gitea is HTTP on 3000, not SSH) |

Record the versions actually installed as you go — `tileserver-gl` in particular is a render
input, and `B1`/`B2` exist because render inputs that are not captured produce identical
`pack_uuid`s over different bytes.

## Where the bytes live

**Not in this repository.** The PMTiles extract and the built packs stay outside the worktree
(`~/maps/` or similar); `Examples/Output/` already holds a third of a gigabyte of earlier
packs. Ignore rules for the output directories and `*.pmtiles` are on branch
`fix/ignore-build-outputs`.

Commit here only: this README, any scripts worth rerunning, `slippypack inspect` output, and
small evidence — a panel photo, a screenshot. Follow
`Docs/Investigations/2026-08-06-rawtiles-spec-adequacy/`, whose committed fixtures are a few
KB each.

---

## L1 — Get a regional extract without touching a tile server

**Hypothesis.** `pmtiles extract` against the remote planet archive pulls an Athens-sized
subset over HTTP range reads, so no full-planet download (~120 GB) is needed, and nothing in
the transaction is a tile fetch.

**Method.** Install the `pmtiles` CLI. Extract the Athens, Ontario region — reuse the bbox
from `Docs/Investigations/2026-08-06-athens-pack/` so the result is comparable to the existing
pack. Record: bytes transferred, wall time, resulting file size, and the source URL with
retrieval date.

**Log.** `pmtiles` v1.31.2 (`a3e4951`, built 2026-07-22), prebuilt `Linux_x86_64` release
binary installed to `~/.local/bin/`. Run 2026-08-12 20:20 local.

```
pmtiles extract https://build.protomaps.com/20260812.pmtiles athens.pmtiles \
  --bbox=-76.015,44.590,-75.889,44.662
```

| measurement | value |
|---|---|
| wall time | 5.98 s, 4 download threads |
| transferred | 2.3 MB, **overfetch 0.05**, 47 HTTP range requests |
| result | `~/maps/athens.pmtiles`, 2,255,635 B |
| SHA-256 | `23975d648ace2c5360e67f50cba088429fac794ff141f5215eec2ed088312733` |
| contents | PMTiles spec 3, tile type **mvt**, gzip, 230 tile entries, clustered |
| zooms present | **0–15** |
| data freshness | `planetiler:osm:osmosisreplicationtime 2026-08-12T04:00:00Z`, seq 121965 |
| producer | `planetiler:version 0.10.2` (`0e5588c4`), Protomaps Basemap `version 4.15.2` |

Build-URL availability: `20260812`, `20260811` and `20260810` all answer HTTP 206 to a range
request; `20260805` and `20260801` are already **404**. Retention on the daily builds is
roughly a week, so the source URL in a runbook goes stale fast — pin the *date you used*, not
a "latest" URL.

**Verdict. Works, and it is cheap — far cheaper than the card assumed.** 2.3 MB and six
seconds for the whole Athens region, against ~120 GB for the planet, because `pmtiles extract`
range-reads only the region's chunks (overfetch 0.05 = about 5 % waste). Compare the existing
raster pack: 43 MiB of ABGR2222 for z12–16 of the same bbox. The vector extract carries z0–15
of it in 2.2 MB. `CONFIRMED` by the run.

On compliance: ranged reads against the published download URL are what `pmtiles extract`
exists to do, and the appendix's § 3.2 quote asks that you "copy the tileset to your own Cloud
Storage" rather than hotlink. Reading that as *do not hotlink when serving*, while a one-off
extract to local disk is the intended use, is `PLAUSIBLE` — not `CONFIRMED`. What would settle
it: Protomaps' own wording about `extract` as a client. It costs nothing to be safe here, so
if this becomes a repeated or automated step, mirror the archive first.

### Four findings that touch other cards

1. **The vector source stops at z15; AthensRun's pack wants z16.** Not a blocker — vector
   tiles overzoom at render time, so L2 can serve z16 raster from z15 vector — but it means
   **z16 raster detail is interpolated geometry, not surveyed detail at that zoom**, and label
   density at z16 is whatever the style does with z15 data. Confirm what it looks like at L2
   before assuming the zoom ladder in `MAP_CARTOGRAPHY_SPEC.md` survives contact.
2. **Card `D6` has a better answer than the one on the board.** The card proposes
   `--timestamp now` or source mtime for local builds. But the extract *carries* real data
   freshness — `planetiler:osm:osmosisreplicationtime`, an actual OSM replication timestamp —
   which is exactly what § 4.10's `build_timestamp` is supposed to mean. Deriving it from
   PMTiles metadata beats both proposals: `now` records when you happened to run a build, and
   mtime records when a file was written. Neither is source-data freshness.
3. **`B2`'s enumeration is incomplete.** The extract's metadata carries `planetiler:version`,
   its githash, and the basemap `version` — data-side provenance that changes pixels and that
   nothing in the descriptor captures. `B2` lists render inputs (fonts, sprites, dither
   policy); it should also list the *data* producer's version, upstream of the renderer.
4. **The metadata's attribution string cannot be used verbatim.** It is HTML:
   `<a href="https://www.openstreetmap.org/copyright">&copy; OpenStreetMap</a>`. The pack's
   `ATTR` section is plain UTF-8 and the ODbL obligation wants `© OpenStreetMap contributors`.
   Anything automating `--attribution` from source metadata has to strip markup and knows to
   append "contributors" — do not wire them together naively.

## L2 — Stand up a renderer we own

**Hypothesis.** `tileserver-gl` serves raster tiles from the PMTiles extract with no account,
no key, and no third-party request path. It is BSD-3 and unrelated to MapTiler's data terms
(`MAP_COMPLIANCE_APPENDIX.md` § 2.3 covers the licensed MapTiler product; this is not it).

**Method.** Run it under docker against the L1 file. Confirm a tile renders in a browser
before involving slippypack. Record the style used and its provenance — the style is a render
input and `MAP_CARTOGRAPHY_SPEC.md` has opinions the default style will not satisfy.

**Log.** `maptiler/tileserver-gl:latest`, digest
`sha256:3a9ccdb24820b6814c8119bcc8a4376c39867cb0ffe69d62919ef898b90c2427`, reporting
**tileserver-gl v5.6.0**. Port 8080 was already occupied on this machine, so 8081 throughout.

Pointed straight at the extract, it refuses to render anything:

```
$ docker run -d -p 8081:8080 -v /home/toby/maps:/data --name tsgl \
    maptiler/tileserver-gl:latest --file /data/athens.pmtiles
WARN: PMTiles not in "openmaptiles" format. Serving raw data only...
"styles": {}
```

`GET /styles.json` → `[]`. A raster request 404s. The vector path is fine —
`/data/pmtiles/14/4735/5917.pbf` returns 7,906 B, z12 12,441 B, z15 11,871 B — so the failure
is precisely "no style", not "no data". (An earlier 204 was my own wrong tile coordinate, not a
gap in the extract.)

Getting to raster took three fixes, each of which is a finding below:

1. a style for the **Protomaps** schema, generated with `protomaps-themes-base` 4.5.0 —
   `scripts/gen-protomaps-style.js`;
2. every `text-font` remapped to `Noto Sans Regular`, the only stack in the image;
3. the theme passed as an **object**, not a name.

With `scripts/tileserver-config.json` and fonts copied out of the image to `~/maps/fonts/`:

```
$ docker run -d -p 8081:8080 -v /home/toby/maps:/data --name tsgl \
    maptiler/tileserver-gl:latest -c /data/config.json
$ curl -o athens_z14_render.png http://localhost:8081/styles/watch/14/4735/5917.png
HTTP 200  20,179 B  image/png   → PNG 256 × 256, 8-bit RGBA
```

| tile | rendered |
|---|---|
| z12 `1183/1479` | `images/athens_z12_render.png`, 29,688 B |
| z14 `4735/5917` | `images/athens_z14_render.png`, 20,179 B (SHA-256 `975441aadacbeee1…`) |
| z16 `18941/23673` | `images/athens_z16_render.png`, 31,591 B — **overzoomed from z15 data** |

**Verdict. Raster renders, but tileserver-gl is not a turnkey step for this source, and the
card reads as though it were.** `CONFIRMED`: a legible map of Athens with road, street and
place labels, from a renderer on this machine, with no account and no third-party request in
the path. The z16 overzoom hypothesis from L1 is `CONFIRMED` as well — z16 is crisp, with
labels re-placed rather than scaled, because vector geometry rescales cleanly. What z16 does
*not* gain is features absent from z15 data.

The style shipped here is the stock Protomaps **light** theme, and it is obviously wrong for
the panel: pale grey on white, hairline roads, small labels. That is expected, not a defect —
it is what `MAP_CARTOGRAPHY_SPEC.md` exists to replace. L3 can proceed against it, because
L3's question is whether the pipeline moves bytes, not whether the map is readable at arm's
length.

### Findings

5. **`E3`'s recipe hides a prerequisite: "serve raster locally" needs a style, and a style for
   *this schema*.** The Protomaps basemap is not OpenMapTiles-schema — its layers are `earth`,
   `landuse_park`, `roads_minor`, `boundaries` — and tileserver-gl's bundled `basic-preview`
   style targets OpenMapTiles. The server detects the mismatch and degrades to serving raw
   vector, which reads like success until you request a PNG. **Cartography stops being a later
   phase at exactly this point**: you cannot render without a style, so somebody chooses one
   here whether or not they meant to.
6. **This re-prices `F1`'s ranking on an axis the ranking does not consider.** `F1` ranks
   permission-first, which is right, and Protomaps wins. But OpenFreeMap — its number two — is
   **OpenMapTiles-schema**, so it would have rendered with the bundled style and no style work
   at all. Permission does not distinguish them; tooling cost does, in Protomaps' disfavour.
   Worth recording against `F1` and `C1`: the primary source choice carries a style bill that
   the compliance analysis had no reason to surface.
7. **The renderer image ships exactly one font stack.** `Noto Sans Regular`, nothing else. Any
   style asking for Medium, Bold or Italic silently loses those labels. This is not a
   footnote — it is item one on `B2`'s list of pixel-changing inputs ("font stack availability
   and fallback"), and here it is, live, on the first render anyone attempts.
8. **`protomaps-themes-base` 4.5.0 fails soft on a bad theme argument, and tileserver-gl fails
   hard on the result.** `layers(src, 'light', …)` — a plain string — returns a style with
   `null` where colours should be, silently. MapLibre GL JS tolerates that; tileserver-gl's
   validator rejects the whole style with
   `layers[2].paint.fill-color[2]: Expected color but found null instead`. The correct call is
   `layers(src, namedTheme('light'), …)`. Both halves are worth knowing: the generator lies
   quietly, and the consumer's error message names a layer index rather than the real cause.

## L3 — Build a pack from loopback

**Hypothesis.** `slippypack make --source 'http://localhost:…/{z}/{x}/{y}.png'` works today
against a local renderer, unmodified.

**Method.** Build from `origin/map-delivery-workflow` (`cargo build --release`), then build a
pack. Expected friction, each of which should be confirmed or falsified rather than assumed:

- `--compression none` is required — the vendored reader fails closed on RLE. Capacity
  feature, not latency: a 64 KiB tile read measured 7–9 ms on hardware.
- loopback is throttled to the non-OSM default of 4 req/s (card `D1`), which on a local
  renderer is pure waiting. Measure what it costs on a real bbox — that number is `D1`'s
  justification.
- `tile_dim_px` is hardcoded to 256 (card `D2`), while the cartography spec prescribes 128.
  Note the size and RAM consequence; do not change the default here.
- a local source sends no `Last-Modified`, so the build warns and writes the zero freshness
  sentinel (card `D6`).

Record the full invocation, the pack size, and the per-zoom byte breakdown. **Sanity-check
against the board's premise traps:** RLE ≈ 7.7 % and each zoom level ≈ 2.2×, not the 32.4 %
and 4× that older documents claim.

**Log.** Built from `origin/map-delivery-workflow` `b8d5464` in a throwaway worktree so the
existing clone's checkout was left alone; `cargo build --release -p slippypack-cli` under the
pinned 1.95.0 toolchain, 32 s cold.

```
slippypack make \
  --source 'http://localhost:8081/styles/watch/{z}/{x}/{y}.png' \
  --out athens-e2e.rawtiles --bbox=-76.015,44.590,-75.889,44.662 --zoom 12-16 \
  --compression none --attribution "© OpenStreetMap contributors"
```

Silent on success — no progress output at all, which for a run of this length reads as a hang.

| | value |
|---|---|
| wall time, default rate | **172 s** |
| wall time, `--rate-per-sec 10000` | **4 s** |
| size | 45,037,308 B |
| tiles | 687 — z12: 6, z13: 16, z14: 42, z15: 143, z16: 480 |
| `tile_dim_px` | 256 |
| `pack_uuid` | `8520f25f-4786-5b2a-919e-79f510ed25c1` |
| `build_timestamp` | 1786581717 = 2026-08-13T00:41:57Z |
| `ATTR` | `© OpenStreetMap contributors` |

An aborted first attempt is worth recording because the failure was clean: I restarted the
renderer while a build was in flight, and it died in 8 s with
`url-template source: HTTP transport error: io: Connection refused`, leaving **no `.partial`
file behind**. The atomic-rename contract holds under transport failure.

**Verdict. The pipeline works end to end on the writer side, and three things it revealed
matter more than that it worked.** `CONFIRMED` by the run.

### Findings

9. **`D1` is worth 43×, not "materially".** 172 s throttled versus 4 s at an effectively
   unlimited rate, over the same bbox against the same local renderer. 687 tiles at the
   non-OSM default of 4 req/s has a floor of 171.5 s, and the measurement lands on it — so
   **the rate limiter is essentially the entire build time, and rendering is free by
   comparison.** Every style iteration against a local renderer currently pays three minutes
   to look at a two-minute-old idea. `D1` should move to the front of Group D.
10. **`D6`'s premise is false, and the truth is worse than the card assumes.** tileserver-gl
    *does* send `Last-Modified`, so there is no warning and no zero sentinel — but the value is
    **the renderer process's start time**, which the pack then records as its freshness.
    Measured: `build_timestamp` 2026-08-13T00:41:57Z against underlying OSM data from
    2026-08-12T04:00:00Z. So the pack claims freshness it does not have, restarting the
    container changes the claim, and nothing warns. A loud "unknown" (the zero sentinel) is
    better than a confident wrong answer. The right value is in the PMTiles metadata (finding
    2), and `--timestamp` already exists to carry it — documented as a "CI override", but it is
    the actual fix.
11. **Pack size carries no information about content.** This pack is 45,037,308 B — the same
    size, to the byte, as the 2026-08-06 pack built from OSM CDN raster over the same bbox,
    because uncompressed ABGR2222 is 65,536 B per tile regardless of what is in it. Anything
    that treats size as a content check — a sync, a cache, a "did this change" heuristic — is
    checking nothing.

### `B1`'s defect, demonstrated

The card argues the renderer can re-enter the identity hole that `M1` closed for compression.
It can, and it takes two commands. Regenerate the style with the **dark** theme, keeping the
style id and therefore the URL template identical, and rebuild:

| pack | `pack_uuid` | size | SHA-256 |
|---|---|---:|---|
| light theme | `832cd0e7-f9aa-5d83-9196-a4d9058094dd` | 393,672 | `3722741984e1470f…` |
| dark theme | `832cd0e7-f9aa-5d83-9196-a4d9058094dd` | 393,672 | `6a28c485a9406294…` |

Same UUID, same size, completely different map. `CONFIRMED`. `PackDescriptor` does carry
`style_hash`, but it is documented as "SHA-256 of the `--style` JSON … `None` for non-renderer
builds" — and a URL-template build against a local renderer *is* a non-renderer build as far as
the descriptor is concerned. The style lives on the far side of an HTTP boundary the descriptor
cannot see. A recipient caching on `(pack_uuid, size)` cannot tell these apart at all.

### The render is not byte-reproducible — and `B1` cannot fix this

Five builds, identical inputs, same renderer instance, same binary. **Three distinct
outputs:**

| build | SHA-256 (first 24) |
|---|---|
| throttled | `c5043a7680ff05f8c564715a` |
| unthrottled | `dc2b094a1e8e3d2b316e0ad6` |
| run3 | `98d3cf0b943caa27a19a6680` |
| run4 | `dc2b094a1e8e3d2b316e0ad6` (= unthrottled) |
| run5 | `98d3cf0b943caa27a19a6680` (= run3) |

The divergence is tiny and precisely located: **two pixels**, at byte offsets 17,475,603 and
32,825,851, each flipping between two values (`0xEE`↔`0xFE` and `0xFF`↔`0xEB`), plus the footer
CRC reacting. In ABGR2222 those are single-step changes in one or two channels — the signature
of an antialiasing coverage tie resolved differently run to run, not corruption. Descriptor
metadata is identical across all five, so `pack_uuid` is stable while the bytes are not.

**This is a third instance of the identity problem, and it is not fixable by adding descriptor
keys.** `M1` was compression; `B1` is renderer version and glyph/sprite URLs; this is the same
renderer, same version, same style, disagreeing with itself. Recording more inputs cannot make
a nondeterministic rasteriser deterministic, so § A.4's promise that a cached `pack_uuid`
entitles a recipient to assume *byte-identical* tile blobs is **unachievable for rendered
packs** as the pipeline stands. Either the render becomes deterministic, or the guarantee gets
weakened, or something downstream canonicalises the output.

Which points at card `C3`. Snapping to declared palette slots would plausibly collapse a
one-step AA tie onto the same slot and restore determinism — which would make `C3` not merely
"small, high leverage" but **load-bearing for reproducibility**. That is `PLAUSIBLE`, not
`CONFIRMED`; what would settle it is running these five builds again with snap-to-slots
enabled and checking whether the two unstable pixels stop moving.

## Notes — the identity problem, and the shape of the way out

Written after L3, because L3's nondeterminism reads like a dead end and is not one. It looked
that way because three findings were being filed under one name.

### Two problems, not three instances

| | what it is | fixable by descriptor keys? |
|---|---|---|
| `M1` — compression absent | the derivation is **incomplete**: two different builds collide | **yes**, and that is what `M1` does |
| `B1`/`B2` — renderer, style, fonts invisible | same: incomplete derivation | **yes**, and that is what those cards do |
| L3 — same renderer disagrees with itself | the bytes are **not a function** of the inputs | **no.** The descriptor is already complete and correct here |

The first two are defects in a derivation. The third is not: nothing is missing from the
descriptor, the render is simply not a function of it. That is why adding keys cannot touch it,
and why it felt terminal.

**§ A.4 is where the conflation lives.** It takes an identifier derived from *inputs* and
promises it delivers *byte* identity. That promise is the bug — not the renderer.

### The two questions one field is being asked to answer

- **"Did I ask for the same thing?"** — build avoidance, catalog lookup, deciding whether to
  download at all. Must be computable **before** the pack exists, which is exactly what
  `debug uuid` is for. This is `pack_uuid`'s real job and it should stay derived from the
  descriptor.
- **"Do I have the same bytes?"** — integrity, dedup, resumable transfer. Only answerable
  **from** the bytes.

Replacing `pack_uuid` with a content hash is therefore *not* the fix: it would answer the
second question by destroying the ability to answer the first.

### Three parts to the way out

1. **Publish a content digest alongside the derived UUID.** The format already streams a
   whole-file CRC-32 over every byte as it writes (`format/crc.rs`), so hashing during write is
   mechanically in place. What is missing is a *strong* digest, exposed where a recipient can
   compare it **before** transferring tens of MB rather than buried in the footer. This is
   `G7`'s neighbourhood — the verifiable prefix wants per-region integrity for the same reason —
   and the two should be designed together.
2. ~~**The determinism may come free from `C3`.**~~ **Tested — it does not.** See
   "The snap experiment" below: snapping reduces exposure roughly four-fold but does not reach
   zero, so it is mitigation, not a fix. `CONFIRMED` negative.
3. **Weaken § A.4 to what is true.** Even with 1 and 2 done, the honest statement is that the
   UUID identifies the recipe and the digest identifies the bytes. Free during v0.x, expensive
   after.

### How much this matters, and when

Two pixels in a map is visually nothing. The harm is operational, and concentrated:

- **Resumable or chunked transfer** — resume against a rebuilt pack and the chunks do not
  stitch. This is the real one, and it is the BLE story (`G4`, `G7`).
- Any reproducible-build gate.
- Delta updates and dedup.

So the urgency tracks the transfer work, not the map's appearance. While delivery is a
whole-file USB-MSC copy, this can wait. The moment BLE transfer of a large pack becomes real,
it cannot.

### The snap experiment

Run after the notes above were written, and it answers the open question with a **no**.
Scripts: `scripts/probe_render.py`, `scripts/snap_test.py`, `scripts/exposure.py`.

**First, the isolation L3 skipped.** L3 inferred that the renderer was nondeterministic from
pack bytes; it never checked the renderer directly. Fetching all 480 z16 tiles three times over:
**one tile, `16/18941/23671`, returned more than one distinct PNG.** Fetching that tile 40 times
returned **three distinct PNGs**. So the nondeterminism is upstream of slippypack entirely —
`CONFIRMED`, not inferred.

**What the difference actually is.** Between two variants: **one pixel**, at (63, 142), RGB
`(244,244,243)` versus `(244,243,242)`. A ±1 jitter in a channel — sub-perceptual, and today's
quantiser maps both to the same slot.

**So the quantiser is an amplifier, and that is the real mechanism.** The renderer errs by
about 1/255. The pack errs by a whole ABGR2222 level — 85/255 — because the ABGR2222 levels are
{0, 85, 170, 255} and a ±1 jitter flips the result *only* when the pre-quantisation value sits
next to a decision boundary. That is exactly what L3's two pack pixels were: values parked at
~212.5, flipping a channel between level 2 and level 3.

Confirmation from the slot codes: L3 saw `0xFF`↔`0xEB`, and `0xEB` is **not among the nine
slots this style declares** (`0xd5 0xd8 0xe5 0xe9 0xea 0xee 0xef 0xfe 0xff`). It is a quantised
antialiasing blend — a colour the style never asked for. Which is precisely the palette-first
problem, showing up as an identity problem.

**Exposure, measured exactly** — perturb each pixel by ±1 per channel and count how many change
slot, over five tiles spanning z12–z16:

| quantiser | pixels exposed | rate |
|---|---:|---:|
| nearest of all 64 slots (today) | 13,623 / 327,680 | **4.16 %** |
| nearest of the 9 declared slots (`C3` snap) | 3,241 / 327,680 | **0.99 %** |

**Verdict. Snapping cuts exposure about four-fold and does not eliminate it.** It moves the
decision boundaries further apart, so fewer pixels sit beside one — but a pixel landing near a
boundary between two *declared* colours still flips, and when it does the flip is larger,
because declared slots are further apart than adjacent ABGR2222 levels.

So `E5`'s finding needs a precision, not a correction: snapping recovers the palette-first
**appearance** at negligible byte cost. It does not recover palette-first **determinism**. Those
were being treated as the same property and they are not. Only a render that never produces
intermediate values — genuinely aliased output, or a renderer emitting palette indices directly —
removes the boundary, because then there is nothing to round.

`C3` remains worth doing on its own merits. It is just not the answer to this.

### On writing our own deterministic renderer

Recorded because it is a live question and the answer has a cheap decision point in front of it.

**The choice is not MapLibre-versus-DIY; there is a third option in between.** L3's
nondeterminism is characteristic of a GPU or thread-ordered pipeline. A **single-threaded CPU
rasteriser is deterministic close to by construction** — no GPU driver, no worker-order
dependence — and maintained ones exist (mapnik/AGG; in Rust, a `tiny-skia`-class rasteriser).
Evaluating a renderer swap is much cheaper than owning one.

Arguments genuinely in DIY's favour, and they are not weak:

- Determinism **by construction**, permanently, rather than "plausibly, if snapping helps".
- **Palette-first output natively.** `E5`'s snap-to-slots is a workaround for not having a
  palette-first renderer; a renderer of ours would emit palette indices directly, and with no
  antialiasing there is no coverage arithmetic to tie.
- It collapses `B1`/`B2` almost entirely: no glyph or sprite URLs, no upstream version to track,
  and the style becomes a small local file that is trivially hashed into the descriptor.
- The target is 240 × 240 at 64 colours with hairline-to-3px strokes. Scanline fills and
  integer-edge lines for that are textbook and small.

What it actually costs, and the reason not to start now:

- **Text.** Glyph rasterisation, and above all label *placement* — collision, priority,
  along-line labels, continuity across tile seams — is the majority of the work and the majority
  of what separates a professional-looking map from a hobby one. The stated bar is the best map
  on any watch at this price, and labels are where that is won or lost. Latin-only at fixed
  sizes from a prebaked atlas is tractable; the general case is not.
- You would own the schema semantics (kinds, sort ranks, per-zoom visibility) — bounded, but
  real, and the appendix's verdict on raw OSM ("maximum control, maximum work") applies to
  rendering too.

**Recommendation, revised after the snap experiment.** The measurement removed the cheap escape:
snapping does not deliver determinism, so "keep MapLibre and quantise the problem away" is off
the table. What is left, in order:

1. **Decide whether byte-reproducibility is required at all**, because that is now a product
   decision rather than a bug to fix. While delivery is a whole-file USB copy, one pixel in a
   45 MB pack costs nothing, and parts 1 and 3 above (a published content digest, and § A.4
   saying what is true) make living with it safe and honest. That is the cheap path and it is
   defensible.
2. **If reproducibility is required, aliased rendering is the requirement** — not snapping. Price
   a deterministic CPU rasteriser first (mapnik/AGG, or a `tiny-skia`-class Rust stack,
   single-threaded, antialiasing off). Maintained, and it removes the boundary problem at the
   source rather than downstream of it.
3. **DIY only if palette-first native output is wanted for its own sake** *and* someone will own
   label placement. The determinism argument alone does not justify it, because option 2 buys
   determinism without the text bill. If it happens anyway, scope it to fills and lines with a
   prebaked glyph atlas — not a general-purpose renderer.

Note what the exposure number implies for option 2's payoff: at 4.16 % of pixels exposed and
roughly one jittering pixel observed per few hundred rendered tiles, this is a rare event with a
visible-but-trivial effect. It is worth engineering away only where *byte* equality is load
bearing — resumable transfer, dedup, a reproducible-build gate — which is the same conclusion the
"how much this matters" section reaches from the other direction.

## L4 — Validate independently

**Hypothesis.** The pack passes an independent reader, not just the writer's own round-trip.

**Method.** There is no `slippypack verify` yet (card `D4`) — use `spec-validator-cpp` from
the slippypack branch, and note what the absence of a subcommand costs in steps. Remember the
identity defect is **live** at v0.6: `pack_uuid` does not determine the bytes, so a matching
UUID proves nothing here (cards `B0`, `B1`).

**Log.** Cheaper than the card implies: **`spec-validator-cpp` is vendored in `slippypack`**, so
no `rawtiles` clone is needed. `make` in that directory builds `build/rawtiles_validate` with
`g++ -std=c++17` under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`,
silently. It re-derives parsing from the layout tables and calls no slippypack code.

All three packs from L3 pass, exit 0 — the full z12–16 pack and both the light and dark smoke
packs:

```
OK  8520f25f-4786-5b2a-919e-79f510ed25c1
    version 1.0   tile_dim_px 256   zoom_range 12..16
    tile_count 687   file_size 45037308 bytes   crc32 0x9c01a4d4
```

Worth noticing in its own output: the light and dark smoke packs print the **same
`pack_uuid` and different `crc32`**, side by side. The format already carries a bytes-identifier
that distinguishes them; it is simply not published or used as one, and CRC-32 is error
detection rather than a digest.

**A green result is not evidence, so the validator was mutated against** (`scripts/mutate_pack.py`).
Each of these is rejected with a specific, accurate message: a payload bitflip (CRC mismatch), a
corrupted magic, truncation by one byte and by half the file, a zeroed CRC, an all-zero
`pack_uuid`, a `tile_count` reduced below the entries actually present, and `tile_dim_px` set to
zero. Structural checks fire independently of the CRC — each mutation above had the footer CRC
repaired where relevant, so the checksum could not be doing the work. **The validator earns
trust.**

Two mutations were accepted, and that is the finding.

**Verdict. The pack is well-formed by an independent reader — `CONFIRMED`, and the reader is
worth believing.** Two gaps below, plus one property of validation that should not be
over-read.

### Findings

12. **`tile_dim_px` is checked for `> 0` and never cross-checked against the tiles.** Setting it
    to **128** — or to 255 — on a pack whose tile blobs are 65,536 bytes validates **clean**, and
    `slippypack inspect` reports the false value back as fact. For ABGR2222 the relation is
    exact and trivial (bytes per tile = `tile_dim_px²` at one byte per pixel), so this is
    checkable in a line. It matters now because card `D2` is about to expose `--tile-dim` and
    later move the default from 256 to 128: **the field most likely to be wrong during that
    change is the one nothing verifies**, and the failure surfaces as garbage on the panel, with
    tile offsets drifting, rather than as a validation error on a laptop.
13. **`pack_uuid` is unattested by the pack.** Overwrite the header's UUID with `deadbeef…`,
    repair the CRC, and both the independent validator and `slippypack inspect` accept it and
    report it back as the pack's identity. Only all-zero is rejected. So the break runs **both
    ways**: the UUID does not determine the bytes (L3), *and* the bytes do not attest the UUID.
    A recipient cannot check either direction. CRC-32 does not help, because anything editing the
    file repairs the CRC as a matter of course — it is error detection, not authentication. The
    detached-signature work that was on the lost `spec-0.7-adequacy-fixes` branch is the
    mechanism that closes this, which gives card `B0` a second reason to exist beyond `M1`.
14. **"Validated" means well-formed, never correct.** Flip a pixel, repair the CRC, and the pack
    passes — as it must; nothing in the file says what the pixels *should* be. Worth stating
    plainly in the runbook, because a green validator on a 45 MB pack invites exactly the wrong
    inference after a style change.

## Panel preview (card `E1`, minimal) — run out of order, because L5 needs hardware

L5 needs the watch mounted and it is not connected, so this was done instead. It turns out to be
the most informative thing in the whole run.

`scripts/panel_preview.py` decodes tiles **back out of the pack** — layout re-derived from the
spec tables, calling no slippypack code — composes a 240 × 240 viewport centred on Athens at each
zoom, and writes it at 1× and 3×. Two jobs in one: the verification L4 structurally cannot do, and
the cartography preview card `E1` asks for.

**As verification: the pack is right.** Tiles decode, every payload is exactly 65,536 B =
`tile_dim_px²` (so this pack's `tile_dim` is honest, even though finding 12 says nothing checks
it), the 3 × 3 neighbourhood is complete at z13–z16, and the image is recognisably Athens with
its street grid, buildings and park in the right places. **This is the first evidence that the
bytes are the bytes we meant** — L4 could only say they were well-formed.

**As cartography: the map is unusable, and not for the reason expected.**

`images/panel_before_after_z16.png` is the same viewport rendered (left) and quantised (right).
On the left, a legible street map: grey-beige ground, white roads, grey buildings, green park. On
the right, the road network **has vanished entirely**, buildings are **yellow**, and the park is
**neutral grey**.

Where the colours went, measured on one z16 tile — 719 distinct source colours collapse to seven:

| source | share | lands on |
|---|---:|---|
| `(226,223,218)` ground | 66.2 % | `(255,255,255)` — pure white |
| `(215,213,211)` buildings | 8.2 % | `(255,255,170)` — **yellow** |
| `(156,211,180)` park | 1.7 % | `(170,170,170)` — **neutral grey** |
| `(247,247,247)` roads | 1.2 % | `(255,255,255)` — pure white |

**Roads are invisible because road-white and ground both land on `0xff`.** The figure/ground
distinction is destroyed by rounding, not by a colour choice.

**And the false hue is the real discovery.** ABGR2222 gives two bits per channel — levels
{0, 85, 170, 255} — and each channel rounds independently of the others. A *neutral* grey at
(215, 213, 211) has channels that straddle the 212.5 boundary in different directions, so it
emerges saturated yellow. Nearest-in-3D picks the same slot, so this is not a quantiser bug to
fix: **there is no neutral light grey between 170 and 255 to land on.** That is the light-end twin
of the known dark-end gap (no neutral between L* 23.7 and L* 66.5), and it is why the viewport
also contains pink and magenta pixels — `0xeb`, `0xfb`, `0xe6`, **none of which the style
declares.**

Lightness distribution in the z16 viewport, which is the number to remember: **about 94 % of
pixels sit above L\* 93**, the only sub-L\* 40 code in use is `0xd5` at 0.1 % of pixels, and the
roads-that-should-be-there would have been the 6.1 % of mid-grey at L\* 69.6 against white. On a
transflective panel in daylight that is a white screen with faint marks.

### Findings

15. **A light-background cartography is not achievable on this panel by any quantisation
    strategy.** Snapping to declared slots (`C3`) does not rescue it either — the nearest declared
    slot to the building grey is white, so buildings would *vanish* instead of turning yellow.
    Different failure, same verdict. The style must be **designed against the palette**, which is
    what palette-first means and why `MAP_CARTOGRAPHY_SPEC.md` exists. The stock Protomaps light
    theme is not a starting point to adjust; it is the wrong end of the space.
16. **`E1` earns the board's description of it — "highest-value new surface for cartography" — and
    it is cheaper than the card assumes.** The card debates whether it should be a
    `slippypack preview` subcommand or a separate tool, worrying about pulling in the renderer
    dependency. It needs no renderer at all: reading tiles out of a finished pack is enough, and
    it is a short script. Card `E1` should be re-scoped from "decide the seam" to "productionise
    the thing that already exists here".
17. **The pipeline working and the product working are very different claims.** L1–L4 all passed;
    the artifact validates; and the map is unusable. Nothing upstream of a rendered preview would
    have told anyone that, which is the strongest argument on this board for building `E1` before
    more of Group C or D.

## Cartography — what the preview unblocks, and the one thing blocking it

The panel preview settles that the cartography must be designed against the palette, and
`MAP_CARTOGRAPHY_SPEC.md` **already does that** — § 3 spends 14 of 64 slots with exact bytes,
roles and rules R1–R5, and § 2 is titled "Palette-first, and why quantise-after loses". So this
run did not discover a gap in the spec; it demonstrated the spec's own argument end to end with a
real pack. The remaining work is implementation.

Measured against § 3, the stock-theme pack is wrong in exactly the predicted places:

| feature | spec | this pack |
|---|---|---|
| ground | `paper` `0xFF` | `0xFF` — right by accident |
| major road | `road_major` `0xC0`, L\* 23.7, **25 : 1** | `0xFF` — invisible at 1 : 1 |
| minor road | `road_minor` `0xC1`, 10.7 : 1 | `0xFF` — invisible |
| building | `building` `0xEA`, "context only" | `0xEF` — yellow |
| park | `landuse` `0xEE` / `wood_lt` `0xDD` | `0xEA` — neutral grey |

### Finding 18 — § 4's pixel weights and § 7's zoom ladder cannot both hold with `tile_dim = 128`

§ 4 specifies line weights in pixels: major road **4 px**, roads floored at 2 px, text 11–12 px.
§ 7 gives z14 = 6.75 m/px with 240 px spanning 1.6 km. **Those m/px figures are standard 256 px
tile pixels** — verified, every row of § 7's table reproduces
`156543.034 · cos 45° / 2^z` to two decimals. The same section then prescribes `tile_dim = 128`.

At 128 px per tile each pixel covers twice the ground, so **`z@128` ≡ `(z−1)@256`**. Build the
ladder as literally written — z12–16 at `tile_dim = 128` — and every zoom lands one level
coarser than intended: z14 becomes 13.5 m/px, 240 px spans 3.2 km, and a 4 px "major road"
covers 54 m of ground rather than 27.

**The weights are not the problem — they are already `tile_dim`-independent.** They are panel
pixels, and the panel is 240 × 240 whatever the tiles do. It is the *zoom numbers* that are
`tile_dim`-dependent, and § 7 states them for 256.

Restated on the invariant (m/px), so the ladder survives any `tile_dim` change:

| m/px | 240 px spans | role | zoom @256 | zoom @128 |
|---:|---:|---|---:|---:|
| 54.05 | 12.97 km | region orientation | z11 | z12 |
| 27.02 | 6.49 km | route overview | z12 | z13 |
| 13.51 | 3.24 km | | z13 | z14 |
| 6.76 | 1.62 km | **default running zoom** | z14 | z15 |
| 3.38 | 0.81 km | | z15 | z16 |
| 1.69 | 0.41 km | junction detail — **the floor** | z16 | **z17** |

So at `tile_dim = 128` the ladder is **z12–z17**, and § 7's objection that "z17 is below the
useful range … z16 is the floor" does not apply to it: that objection is about ground span, and
z17@128 spans 405 m — identical to z16@256. It would apply to z18@128.

Nothing else in the § 7 argument for 128 px is affected, and the RAM win is real: worst-case
viewport coverage is four 256 px tiles (256 KiB) against nine 128 px tiles (**144 KiB**), and
total pack bytes are unchanged, since four times the tiles at a quarter the size each is a wash.

**A dependency this creates, which the board does not record:** if the ladder is 128 px, then
iterating cartography at the correct pixel scale needs **`D2`** first, because `tile_dim_px` is
hardcoded to 256 and there is no flag. Judging 4 px strokes at 256 px and shipping at 128 px
would invalidate every judgement in § 4 — which § 4 itself already flags as the part most likely
to change after a hardware legibility trial. Neither `E1` nor `C3` mentions needing `D2`.

### The palette-locked style — written 2026-08-12, at `tile_dim` 256

`scripts/gen-watch-style.js` emits a MapLibre style for the Protomaps schema using **only** § 3's
slots. Built at 256 px, where § 7's published ladder and its m/px column agree, so § 4's weights
are meaningful as written; finding 18 is untouched by this and still needs settling before 128 px.

**The palette lock is mechanical, not aspirational.** Every colour is derived from its ABGR2222
byte rather than typed, and the script asserts that no colour literal outside the declared slots
survives into the output — it throws otherwise. `layers=16 colours used=10 all legal slots=yes`.

Schema targets came from the data rather than from guesswork: `scripts/mvt_kinds.py` is a
dependency-free MVT reader that enumerates what the extract actually contains. For Athens the
roads layer holds only `major_road:secondary`, `minor_road:residential` and
`minor_road:service` — no highways, no paths — so `path` `0xD0` is unexercised here and the
warm/cool ink split of R1 cannot be judged on this area. Worth knowing before anyone concludes
R1 works. (The same probe confirms L1's overzoom from the other side: a z16 *vector* tile 404s
while a z16 *raster* tile renders.)

Structural choices worth stating, since § 4 leaves them implicit:

- **All road casings are a separate pass beneath all road fills.** Per-road casing-then-fill
  would let one road's `paper` halo overdraw the adjacent road it exists to separate.
- **Weights are fixed, not zoom-interpolated.** § 4 gives single px values with no ramp, so that
  is the faithful reading — but it means a 4 px major road at z12 is the same stroke as at z16,
  and that is very likely wrong. Flagged rather than invented.
- **The spec wants bold label text and the renderer has only `Noto Sans Regular`** (finding 7).
  Recorded as a gap instead of silently substituting Regular and calling § 4 satisfied.

**Result, measured.** Roads are visible for the first time: `road_major` `0xC0` at 1.3 % of pixels
and `road_minor` `0xC1` at 1.5 % at z16, against 0 % before — previously both collapsed into
`paper`. Off-palette pixels fall to **2.1 % at z16 and 3.3 % at z14**, and every one of them is an
antialiasing blend *between two legal slots* — the dominant one is mid-grey `(85,85,85)`, which is
black road blended with white halo. That is precisely what `C3` snaps away, and it is a much
better-posed problem than the stock theme's, where whole features landed on wrong slots.

`images/watch_z16_aseen.png`, `images/watch_z14_aseen.png`.

### Finding 19 — a device-RGB preview oversaturates, and every image before this one did

§ 3 is explicit that the `preview` sRGB column is "the sRGB rendering of what the eye sees on the
panel once adapted", **not** a colour to paste into a style. The corollary is the one I missed:
it is also what a *preview* must display. `panel_preview.py` and the earlier before/after images
show device RGB — the ×85 channel expansion — so `wood_lt` `0xDD` appears as vivid `#55FF55`
when the panel renders it as pale `#D0EDCD`.

`images/watch_device_vs_seen.png` is the same viewport both ways. The left is what the bytes are;
the right is what the wearer sees, and it is a restrained, legible map rather than the garish one
the left implies. **Anyone judging cartography from a device-RGB preview will "fix" saturation
that does not exist.** `E1` should apply the § 3 mapping by default and offer raw device RGB only
as a debugging view.

This does not change finding 15: that was about *lightness* collapse — roads and ground landing on
the same slot, ~94 % of the viewport above L\* 93 — which is unaffected by how saturation is
displayed.

### End to end with the watch style, and what it did to the identity problem

Rebuilt with the palette-locked style: **3 s** unthrottled, 687 tiles, 45,037,308 B, validated
clean by `spec-validator-cpp`. `images/panel_stock_vs_watch_z14.png` is the stock-theme pack and
this one at the default running zoom, both through the § 3 perceptual mapping — one is a
near-empty white field, the other a complete street network. Same data, same bbox, same
pipeline.

**And the same `pack_uuid`: `8520f25f-4786-5b2a-919e-79f510ed25c1`, identical to the unusable
pack, at identical size.** `B3`'s defect at full scale on a real deliverable, not a smoke test.

`--timestamp 1786507200` also demonstrates `D6`'s fix: `build_timestamp` now decodes to
2026-08-12T04:00:00Z, the actual OSM replication time from the PMTiles metadata, rather than
whenever the renderer happened to start. Existing flag, no code change, and `pack_uuid` is
unaffected because `build_timestamp` is not in the descriptor.

### Finding 20 — palette-first cuts the exposure surface tenfold and does not fix determinism

Re-measuring the ±1 perturbation exposure against the palette-locked style:

| style | exposed @ 64 slots | exposed @ declared slots |
|---|---:|---:|
| stock Protomaps light | 4.16 % | 0.99 % |
| palette-locked watch | **0.42 %** | 0.77 % |

A tenfold reduction, because most pixels now sit exactly *on* a slot value — maximally far from
any decision boundary — instead of clustered in the 215–250 range where the boundary at 212.5
lives.

**I was about to conclude that palette-first therefore buys determinism. It does not.** The direct
test — five builds, identical inputs, palette-locked style — still yields **three distinct
outputs** with five to six differing bytes, exactly the pattern the stock theme gave:

```
8e56e86fc4ea44ebab7e4135   (×2)
a6bc5f5dc84ce2ae9d453df6   (×1)
9d3bd376f1d0b7fe9b86bf61   (×2)
```

**So exposure is a misleading proxy for determinism, and that is the finding.** It counts pixels
that *could* flip. The renderer's jitter is not spread across them — it is localised to a handful
of specific antialiased edge pixels, and those are precisely the ones still exposed under any
quantisation strategy. Shrinking the exposed population by 10× removed none of the actual
divergence.

`G8` therefore stands unchanged: its "yes" branch still requires **aliased rendering**, and no
styling or quantiser choice substitutes for it. What did change is the cheap branch's price —
palette-first is worth doing on its own merits and costs nothing here.

### Finding 21 — `C3`'s measured benefit was an artifact of the style it was measured against

Note the second column above: snapping to declared slots **improves** the stock theme
(4.16 % → 0.99 %) and **worsens** the palette-locked style (0.42 % → **0.77 %**). The mechanism is
sparsity — with only ten declared slots, an antialiasing blend more often lands near the midpoint
*between* two of them than it would near some slot centre in the dense 64-code lattice.

So `C3` was measured against an off-palette style, and its case does not survive the spec's own
palette-first mandate being satisfied:

- **Determinism: no.** Finding 20 — nothing here reaches it.
- **Appearance: now an open design question rather than an assumed win.** With the watch style, the
  off-palette pixels are antialiasing fringes at feature edges — 2.1 % at z16 — and every one of
  them is a *legal, displayable* panel code. § 4 assumes "no anti-aliasing available" and specifies
  aliased text, but the panel can in fact show those intermediate shades, and a softened road edge
  may be an asset rather than a defect. Nobody has looked at that on hardware.
- **`R4`: yes, and this is the reason `C3` survives.** § 3's R4 says "one code per feature class,
  no exceptions — this is what makes § 5's LUT work." An antialiasing fringe introduces codes no
  feature class owns, which is exactly what a per-activity LUT cannot map predictably. So `C3`'s
  real justification is the activity variants (`G6`), not determinism and not obviously appearance.

Re-scope `C3` accordingly, and stop budgeting it as the fix for anything else.

### One design question this raises for the spec

At z16 over a village, the built-up wash `landuse` `0xEE` covers **72.5 %** of the viewport and
`paper` covers **9.2 %**. § 3 casts `paper` as "ground" and `landuse` as a wash "one step off
paper", but in a settlement where nearly every polygon is residential the wash *is* the ground and
paper becomes the exception. Not wrong, but likely not intended, and it is a one-line change to
test the inverse (paper as built-up, tint reserved for denser urban fabric).

### The implementation order this implies

1. **Settle the ladder in m/px** — the table above is the proposed restatement. Until it is
   settled every line weight in § 4 is unfalsifiable, because "4 px" has no ground meaning
   without a stated m/px.
2. **`D2`** if the answer is 128 px — now a prerequisite, not an ergonomic nicety.
3. **Write the palette-locked style**: Protomaps schema layers → the 14 slots, honouring R1–R5.
   Mechanical given § 3. Note the spec's own warning — the `preview` sRGB column is *not* what
   goes in a style file; the ABGR2222 byte is authoritative, so `road_major` is `#000000` in the
   style and the panel renders it as roughly `#383838`.
4. **Iterate on the fast loop** — render a viewport, quantise, look. Seconds per cycle, and it
   needs neither a pack build nor slippypack. Only then rebuild a pack and re-run the panel
   preview end to end.

Labels are where this is won or lost. § 4 wants aliased 11–12 px text with a 1 px `halo` ring,
and the renderer antialiases, so label edges will blend off-palette — which is where `C3` earns
its keep, and the one part of § 4 that a laptop preview cannot settle on its own.

## Dense urban — what Athens could not have shown

Everything above was measured on Athens, Ontario, because that is the bbox the PoC pack used and
comparability was worth having. Athens is a village. With a watch connected (L5 below), packs of
**downtown Toronto** (`-79.42,43.63,-79.35,43.68`) and **Gatineau Park**
(`-75.92,45.46,-75.82,45.53`) were built with the palette-locked watch style and deployed, and
dense urban geometry broke things rural geometry cannot reach. Four of the five findings below
are about `MAP_CARTOGRAPHY_SPEC.md`, and none of them are cases the spec got wrong — they are
cases it never had in front of it.

22. **The app centres on the pack, which is what makes bench testing possible at all.**
    `Model.cpp:533-536` (`poc/athensrun`) initialises the map centre to the **pack's bbox
    centre** the moment the pack opens; a GPS fix overrides it later
    (`Model.cpp:417-428`). `CONFIRMED` from the source. So a pack of anywhere renders on the
    bench with no fix — which is the only reason a tester in rural Ontario could evaluate
    Toronto cartography on real hardware. The same mechanism has a matching failure: **once a
    fix arrives from outside the pack's bbox the view pans off-pack and goes blank**, so the
    thing that makes bench testing work makes field testing of a foreign pack impossible.
    Worth stating in the runbook, because "the map went blank when I walked outside" is
    otherwise indistinguishable from a corrupt pack.
23. **Sidewalks swamp the map, and R1 needs a rule about which paths earn ink.** Toronto's roads
    layer carries `path:sidewalk`, `path:crossing`, `path:footway`, `path:cycleway`, `path:path`,
    `path:steps`, `path:pedestrian`, `path:alley` and `path:corridor`. Athens carried **none** of
    these — only `major_road:secondary`, `minor_road:residential` and `minor_road:service`.
    Downtown has a sidewalk on both sides of every street and a crossing at every corner, and all
    of it renders as 2 px `path` `0xD0` cool ink, producing a dense mesh that competes with the
    road network it is supposed to sit beneath. `CONFIRMED` by the render. R1 fixes the *hue*
    meaning — warm `0xC1` vehicular, cool `0xD0` foot — and that part holds; what it does not say
    is **which path kinds deserve ink at all, or at which zooms**, and on urban data that is the
    question that decides whether the map is readable. Note this also finally exercises R1: the
    warm/cool split could not be judged on Athens because no path existed there.
24. **Antialiased label text lands off-palette, and § 3 has no neutral mid-grey to catch it.**
    § 4 specifies 11–12 px `ink` text with a 1 px `halo` ring "rendered aliased". MapLibre
    antialiases and cannot be told not to. Blending `ink` `0xC0` (0,0,0) against `halo` `0xFF`
    (255,255,255) produces mid-greys such as `0xD5` (85,85,85), which is **not one of § 3's
    fourteen slots** — the palette has no neutral between `0xEA` (170,170,170) and `0xC0`. So
    every label's edge pixels are colours the style never declared. `CONFIRMED`. This is the
    concrete casualty of the antialiasing question that `C3` and `G8` circle at arm's length: it
    is not a fringe on the odd building corner, **it is the text**, which is the part § 4 calls
    the thing the map is won or lost on.
25. **§ 3 has no rail slot, and cities have railways.** Toronto carries `rail:rail`,
    `rail:subway` and `rail:tram`; the palette's fourteen slots contain nothing for any of them,
    so rail corridors render as **nothing at all** — not miscoloured, absent. `CONFIRMED` that
    the gap exists. The spec was written against a rural PoC where the case never arose, and
    there is room to fix it: § 3 spends 14 of 64 codes and calls the remaining 50 deliberate
    headroom. But the fix is constrained twice over. The code has to come from the ~29 codes § 3
    describes as mutually separable at ΔE2000 ≥ 10, and it has to respect **R4** ("one code per
    feature class, no exceptions", which is what makes § 5's activity LUTs work). Borrowing
    `contour` `0xC5` is tempting because terrain is deferred out of v1 (§ 6) — and it would
    violate R4 the moment terrain ships, which on § 6's own schedule is v1.5. So: the gap is
    `CONFIRMED`, the right code is `PLAUSIBLE` pending the ΔE separability check that `E1` ran
    when it picked the original fourteen.
26. **Buildings crowd a dense viewport.** § 3 casts `building` `0xEA` as "context only". In a z15
    downtown viewport buildings cover roughly **40 %** of the area, which is not context. Compare
    finding 15's rural measurement from the other direction — the built-up wash at 72.5 % of a
    village viewport against 9.2 % `paper`. Both point at the same thing: **§ 3's area-fill
    hierarchy was tuned on sparse geometry**, and the roles it assigns ("ground", "wash one step
    off paper", "context only") describe proportions that only hold there.

**Also worth recording, because it re-prices finding 21.** Off-palette codes per viewport rose
from 4 distinct at z16 on the rural pack to 16 at z16 and 17 at z14 on Toronto. The
antialiasing fringe therefore **scales with feature density**, which finding 21 had no way to
see: it argued `C3`'s remaining justification is R4 and the activity LUTs, and this says the R4
violation grows with exactly the content the product is most likely to be used on. The argument
gets stronger in cities, not weaker.

### Three additions `MAP_CARTOGRAPHY_SPEC.md` now needs

Recorded here rather than edited in: `Docs/External/slippypack/` is an explicitly read-only
proxy of another repository and its own README says not to edit files there. These belong on a
`slippypack` branch.

1. **Path-kind selection** (finding 23) — which of the `path:*` kinds get ink, and from which
   zoom. § 4 gives `path` a weight and a dash; nothing gives it a *filter*.
2. **A rail treatment** (finding 25) — a slot from the separable set, plus a weight and a
   pattern, or an explicit statement that rail is deliberately not drawn.
3. **An answer on aliased text** (finding 24) — either a neutral mid-grey slot so blends land
   somewhere declared, or `C3` snapping text edges, or the acceptance that label edges are
   off-palette and R4 is a rule with a stated exception. § 4 currently assumes a property of the
   renderer ("rendered aliased") that no renderer in this pipeline has.

## L5 — Get it onto the watch

**Hypothesis.** USB mass storage delivery works today, at an app-sandbox-relative path.

**Method.** No deploy command exists (card `E2`), so this is manual, and two traps are live:
USB-MSC writes and the watch's own BLE sync collide on the same exFAT partition and **will
corrupt files**, and no absolute `N:/`-style volume path resolves from an app. Byte-verify
after copying. Whatever the safe sequence turns out to be is `E2`'s specification.

**Log.** Watch connected over its own data cable. Three things had to be established before
any bytes moved — where the app looks, how the volume behaves, and what makes the app *trust*
what it finds — and only the first of those is in the card.

**Where the app looks.** `kMapPackCandidatePaths` in
`Examples/Apps/AthensRun/Software/Libs/Header/MapPackPaths.hpp` (branch `poc/athensrun`) holds a
single **sandbox-relative** entry, `../SharedData/maps/athens.rawtiles`. That is deliberately
the *shared* MapManager directory rather than a copy private to this app, so every app reads the
same already-verified location. The card's "no absolute `N:/`-style path resolves" trap is
therefore already avoided by the app, not something the deploy step has to work around — what
the deploy step has to do is resolve that relative path against the volume itself.

**How the volume behaves.** `/dev/sda1`, exFAT, label `UNA WATCH`, 3.3 GB. It arrives
**unmounted** on this machine, so there is a step the earlier `.uapp` deploy notes do not have:

```
udisksctl mount -b /dev/sda1     # → /run/media/toby/UNA WATCH
```

**What makes the app trust the pack.** A **trust marker** beside the pack, `<pack>.trust`,
documented in `Examples/Apps/AthensRun/Software/Libs/Header/MapPackTrustMarker.hpp`
(`poc/athensrun`). 16 bytes, fixed layout, little-endian:

| bytes | field |
|---|---|
| `[0..3]` | magic — `MPT1` (Good) or `MPTX` (Bad) |
| `[4..11]` | pack size, u64 |
| `[12..15]` | the pack's declared footer CRC-32, u32 |

Normally it is the separate MapManager app that writes this, after a full CRC scan. A
hand-installed pack has nobody to write it, so **the marker has to be published by hand or the
pack is never trusted** — and the marker binds to `(size, CRC)`, which means *replacing a pack
without rewriting its marker leaves the app seeing a mismatch* rather than seeing the new pack.
That is the single most surprising thing in this link, and it is invisible from the file system:
the pack is present, well-formed, and ignored.

**What was deployed.** The point of this run was no longer Athens — see the urban section
above — so three packs went across, named so that any of them can be made the live one by a
rename:

| file | area | size (B) | tiles |
|---|---|---:|---:|
| `athens.rawtiles` | downtown Toronto | 17,044,896 | 260 |
| `athens-trails.rawtiles` | Gatineau Park | 35,662,800 | 544 |
| `athens-rural.rawtiles` | Athens, Ontario | 45,037,308 | |

The pre-effort pack was set aside as `athens.rawtiles.old-preeffort` rather than deleted —
201,257,272 B, `build_timestamp` 0, an OSM-CDN-era artifact from before this workflow existed.

**Verification, in three independent directions.** Source and on-device copies are **SHA-256
identical**; `spec-validator-cpp` reports `OK` **reading directly off the watch** rather than
off a copy; and the published marker's size and CRC both cross-check against the pack. Then
the volume was unmounted cleanly, which is not hygiene but a **requirement** — while the host
holds the partition as USB mass storage the watch cannot read its own filesystem, so a pack left
mounted is a pack the app cannot open.

**Verdict. USB-MSC delivery works today at the app-sandbox-relative path — `CONFIRMED` — and
the safe sequence is longer than "copy and verify".** It is: mount, copy, **publish a trust
marker**, byte-verify, unmount before testing. Two of those five steps are ones a careful person
would not think to take, and both fail silently: no marker means an ignored pack, and a still-
mounted volume means an unreadable one. Written up as verified instructions in `RUNBOOK.md`.

**What was not verified: whether BLE sync was active during the write.** That is precisely the
known corruption mode — USB-MSC writes and the watch's own BLE sync collide on the same exFAT
partition — and it is the reason card `E2` exists. This write verified clean in every direction
checked, which is **evidence of one good outcome, not evidence of safety**; a race that is not
observed is not a race that did not happen. What would settle it is knowing the sync state at
write time, which nothing here exposes — and the right response is not to run the experiment
deliberately but to make the state observable and the write refusable, which is `E2`'s whole
content. Until then, treat "it worked last time" as the worthless evidence it is.

## L6 — See it

**Hypothesis.** AthensRun renders the pack, auto-centred, with the live GPS trace overtop.

**Method.** The app builds — `.uapp` artifacts dated 2026-08-07 are in its `Output/` — but its
source tree is largely untracked and
`Software/Libs/Header/MapPackTrustMarker.hpp` currently sits in an unresolved conflict state
in the index. Resolve that before trusting a build. Photograph the panel; a screenshot of the
simulator is not evidence about the real display.

**Log.** Run 2026-08-12 night, indoors, well-lit room. Toronto pack (style v3) as the live
`athens.rawtiles`, started with no GPS fix so the view stayed on the pack's bbox centre. Zoom
cycled z12→z16 with R2 and photographed at each step, twice over: once **backlight on** and once
**backlight off**. The hand-written trust marker was accepted — tiles drew, no error and no
"validating" state — so L5's marker layout is `CONFIRMED` end to end.

**Read the backlight-off set for anything about colour.** With the backlight on the whole panel
reads strongly **cyan-blue**: `paper` `0xFF` looks like a light blue rather than white, and every
fill shifts with it. Reflectively — the panel's normal mode, and the mode § 3's adaptation model
describes — `paper` reads as a light warm grey, water is a clear distinct blue, and the greens are
visible as separate tones. Same bytes, different apparent palette.

Legibility by zoom, reflective, judged on the panel rather than from the photographs:

| zoom | m/px | verdict on the panel |
|---:|---:|---|
| z12 | 27.0 | **good** — grid, lake edge and the parkway curve all read |
| z13 | 13.5 | **worst of the five** — uniform texture, no skeleton |
| z14 | 6.76 | **bad** — texture; minor roads dominate the majors |
| z15 | 3.38 | **acceptable** — structure readable |
| z16 | 1.69 | **good** — sparse and clean |

**Verdict. The pack renders on the watch and the delivery path is settled, so `E3`'s six links are
complete. The cartography is not.** `CONFIRMED`: legibility is **U-shaped in zoom** — the two
extremes work and the middle fails — and § 7's designated **default running zoom, z14, is on the
wrong side of the crossover**, which sits between z14 and z15. A runner's default view is the one
that does not work.

### Findings from the hardware trial

27. **Any colour judgement must be made with the backlight off.** Backlit, the panel reads
    markedly blue and `paper` looks light blue rather than white. Recorded because it will
    otherwise bias a palette decision later: a photograph taken with the backlight on says
    nothing reliable about § 3's slots, and the reflective appearance is both the normal mode and
    the one the spec models. Not yet checked in daylight — this was night, indoors.
28. **Street names are unreadable on the panel, and photographs say otherwise.** The names are
    legible in every close-up photograph and illegible on the watch at glance distance. The
    arithmetic agrees with the eye, not the camera: at 126 µm pitch, § 4's specified 11–12 px text
    has an x-height subtending about **8 arcminutes** at 30 cm, which is detection rather than
    reading; comfortable reading needs roughly **28 px**, about 2.3× the spec, and only eight such
    lines fit across the panel.
29. **§ 4's own evidence for text size is invalid, and so were this investigation's previews.**
    § 4 argues "aliased text at 11 px is blocky but readable at 1:1" and cites a 1:1 render. A
    desktop monitor's pixel pitch is roughly 0.25 mm against this panel's 0.126 mm, so **a "1:1"
    render is physically about twice the size it will be on the watch.** Every legibility judgement
    made that way — including finding 24's conclusion that labels survive, drawn from photographs
    — overstates the panel. Judge text on the panel or not at all.
30. **R1's hue split inverts the hierarchy at urban density.** `road_minor` `0xC1` is warm and
    saturated, `road_major` `0xC0` is neutral black. R1 treats that as a distinction; on the panel
    the *saturated* class is also the far more numerous one, so at z13–z14 the maroon minor mesh
    reads as louder than the black skeleton it is supposed to sit beneath. Hue-coding the more
    numerous class makes it dominant. `CONFIRMED` on the panel; the fix is `PLAUSIBLE` — thin or
    drop minors at the wide zooms, and let the neutral ink carry the hierarchy — and what would
    settle it is another trial with minors suppressed below z15.
31. **Garmin's answer to the label problem, from a side-by-side on comparable hardware.** Three
    things are visible and survive the confound below:
    - **Offset beside the line rather than drawn over it**, so the road stroke does not run through
      the glyphs and the halo has nothing to fight.
    - **Larger** — cap height is roughly a twentieth of the screen height, so of the order of
      20 px, in the range the arcminute maths calls marginal-to-comfortable rather than
      threshold. Note this is a *ratio* measured against a known 240 px panel, which is why it
      survives photography where finding 29's legibility judgements do not.
    - **Sparse**, a handful per screen, with correspondingly **thin** roads leaving the text room.

    This style does the opposite on all three: labels sit on the line, at 11–12 px, as densely as
    collision detection permits.

    **What is *not* established: whether Garmin holds labels horizontal.** They appear horizontal
    in the photograph, but the comparison area is an Ontario survey grid where roads run nearly
    north–south and east–west, so text placed *along* a road would look horizontal too. The
    evidence cannot separate the two, and the first version of this finding wrongly asserted it.
    What would settle it: a label on a road that runs diagonally **on screen** — either somewhere
    with non-grid roads, or, if that map face is course-up rather than north-up, by facing a
    different direction and seeing whether the labels re-orient with the map or stay level.

    The orientation question matters on its own terms regardless of what Garmin does: angled
    strokes rasterise worst at 126 µm with no antialiasing to soften them, so rotated text is
    likely the wrong choice here even if a competitor uses it.
32. **The `Acquiring GPS …` overlay ruins the map screen, and always.** Confirmed always that
    prominent: large text across the lower third, over live map content, for the whole
    fix-acquisition window — which is exactly the first minute of a run, when a runner is checking
    where they are going. App-side rather than cartography, but it defeats the map face regardless.

---

## Exit criteria

The card's bar is: **someone else can follow this without improvising.** Concretely, this
investigation is done when

1. every link above has a Verdict, failures included;
2. a runbook exists — the commands in order, with the versions they were run against — such
   that a second person reaches a map on a watch without asking a question;
3. **Group D is reordered** against measured friction, with the reasoning recorded, and the
   board updated to match;
4. anything that turned out to be a spec, tool, or app defect is filed as its own card or
   branch rather than left in this prose.

## Runbook

**`RUNBOOK.md`, beside this file** — written incrementally rather than last, so the steps get
recorded while the exact commands and versions are still known rather than reconstructed. It
covers the installs and L1–L5 as verified instructions with expected output — through mounting
the watch, publishing the trust marker, byte-verifying off the device and unmounting before a
test — and stops hard at L6 with "not yet verified" rather than guessing ahead. Extend it as each
link lands.
