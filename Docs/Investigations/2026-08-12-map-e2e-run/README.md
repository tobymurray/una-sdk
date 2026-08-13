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

## L4 — Validate independently

**Hypothesis.** The pack passes an independent reader, not just the writer's own round-trip.

**Method.** There is no `slippypack verify` yet (card `D4`) — use `spec-validator-cpp` from
the slippypack branch, and note what the absence of a subcommand costs in steps. Remember the
identity defect is **live** at v0.6: `pack_uuid` does not determine the bytes, so a matching
UUID proves nothing here (cards `B0`, `B1`).

**Log.**

**Verdict.**

## L5 — Get it onto the watch

**Hypothesis.** USB mass storage delivery works today, at an app-sandbox-relative path.

**Method.** No deploy command exists (card `E2`), so this is manual, and two traps are live:
USB-MSC writes and the watch's own BLE sync collide on the same exFAT partition and **will
corrupt files**, and no absolute `N:/`-style volume path resolves from an app. Byte-verify
after copying. Whatever the safe sequence turns out to be is `E2`'s specification.

**Log.**

**Verdict.**

## L6 — See it

**Hypothesis.** AthensRun renders the pack, auto-centred, with the live GPS trace overtop.

**Method.** The app builds — `.uapp` artifacts dated 2026-08-07 are in its `Output/` — but its
source tree is largely untracked and
`Software/Libs/Header/MapPackTrustMarker.hpp` currently sits in an unresolved conflict state
in the index. Resolve that before trusting a build. Photograph the panel; a screenshot of the
simulator is not evidence about the real display.

**Log.**

**Verdict.**

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
covers the installs and L1 as verified instructions with expected output, and stops hard at L2
with "not yet verified" rather than guessing ahead. Extend it as each link lands.
