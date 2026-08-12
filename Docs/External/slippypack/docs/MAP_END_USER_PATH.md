# The end user's path — what to recommend, and in what order to build it

**Date:** 2026-08-12 · **Status:** recommendation, for decision
**Companions:** `MAP_DELIVERY_WORKFLOW.md` (candidate scoring, sequencing) ·
`MAP_COMPLIANCE_APPENDIX.md` (source verdicts) · `MAP_CARTOGRAPHY_SPEC.md` (pack defaults) ·
`PLAN.md` (phasing)

This answers one question the companions leave open: **if users supply their own tiles, what
is the default we recommend to them?** The compliance appendix says which sources are
permitted; the workflow doc says which delivery shape to build. Neither says what a person
should be told to do, in order, on their first day.

---

## 1. The answer

**There is no raster tile service that can be recommended as a default.** Every one that is
ready to quantise either prohibits this use case (OSM tile CDN, MapTiler Cloud) or gates it
commercially (MapTiler Server/Data, Thunderforest), and the one with an offline carve-out
(Stadia) reaches only a strictly client-side workflow whose application to a `.rawtiles` file
on a watch is unresolved. See `MAP_COMPLIANCE_APPENDIX.md` § 6.

So the prescription inverts the question:

> **For the overwhelming majority of end users, the default is a pack they download — not a
> source they configure.** BYO is a power-user and developer path, and its default is
> **a permitted vector source fetched once through that source's own bulk channel**
> (Protomaps basemap, then OpenFreeMap), **never an API key.**

"Bring your own tile source" as the mainstream onboarding step should be retired. It hands the
compliance question to someone with no way to answer it, and the plausible thing a
non-technical owner types into a URL-template box — `tile.openstreetmap.org` — is prohibited by
name. `MAP_DELIVERY_WORKFLOW.md` § 3 already calls that a defect in candidates W-A/W-B; this
document is the constructive half of that finding.

### What must change in `PLAN.md`

`PLAN.md:452-457`'s first-run flow lists sources "in order of expected friction": **MapTiler
first, Stadia second.** It predates the audit. As written it walks a new user into a source
whose terms forbid the only thing the tool does. This is spin-off **S2** and it is still open —
the fix is not just deleting a recommendation but replacing the ordering with the one in § 3.

---

## 2. The three tiers, and what each is told

| tier | who | what they configure | what they need built |
|---|---|---|---|
| **T1 — the Saturday runner** | most owners | **nothing** | catalog (W-F) |
| **T2 — the specific region** | owners with an unusual destination | **nothing** — draw a box | archive + browser slicer (W-E) |
| **T3 — BYO** | power users, developers, other devices | a vector source, once | `pmtiles`/`mbtiles` source kinds + renderer |

**T1 is the product.** The prescription is a single sentence: *load one "my region" pack on day
one, not one pack per run.* A metro-scale region is 25–30 MiB at z12–16 with `tile_dim` 128, so
this is a once-a-season action, and the failure mode that actually bites — "I forgot to load a
map" — is designed out rather than warned about. The watch's empty state should say this.

**T2 changes nothing about sourcing.** A box-drag against the pre-rendered archive is a range
read, not a fetch from a third party, so the user inherits our compliance posture instead of
acquiring one of their own.

**T3 is where a default source recommendation is actually needed** — and it is a data source
plus a local render, not a tile API.

---

## 3. The BYO default, ranked

For anyone building their own packs, recommend in this order. Ranking is by *permission first*,
then by work required.

### 1. Protomaps basemap — the default

A single PMTiles archive of the planet (z0–15, ~120 GB), daily builds, distributed as an ODbL
Produced Work. `pmtiles extract` cuts a region without downloading the planet.

- **Why it is first:** bulk download is the advertised channel, the licence is unambiguous, and
  regional extraction is a first-class documented operation.
- **Tell the user:** copy the tileset to your own storage rather than hotlinking — their docs
  ask for this directly, and URLs may change.
- **Attribution that must reach the pack's `ATTR`:** `© OpenStreetMap contributors`.

### 2. OpenFreeMap — the alternative

Weekly full-planet downloads in MBTiles or Btrfs, commercial use allowed, no key, no account.

- **Use the planet downloads, not the public instance.** "No limits on requests" is written
  about serving a live map; nothing on their site addresses systematically prefetching the CDN
  to build archives. The bulk channel exists — use it. This is the conservative reading and the
  polite one, and it costs nothing because a local copy is wanted anyway.
- **Attribution:** `OpenFreeMap © OpenMapTiles Data from OpenStreetMap`.

### 3. Geofabrik / BBBike raw OSM extracts — the fallback

Maximum control, maximum work: you own a schema and a rendering pipeline. Recommend only if
both sources above become unavailable, or for someone who wants a bespoke cartography.

### 4. Stadia Maps — the only raster key worth mentioning, with conditions

Recommend **only** where all three hold: the fetch happens in the user's own browser, the pack
is for the user's own device, and the volume stays under their 100 MB-per-device ceiling. Do not
present it as a general default and do not use it for anything we host or redistribute — that is
server-side caching and systematic compilation, both prohibited. **Blocked pending legal item
L3** (whether a `.rawtiles` file on a watch is "cached per device in a mobile application", and
whether 100 MB is at-a-time or cumulative).

### 5. Thunderforest, Small Business plan or above — the documented priced route

The one source with an explicit, friendly, priced path to bulk-download permission. Worth
recommending to a user who specifically wants pre-rendered raster cartography and will pay for
it. Not needed by the recommended architecture.

### Never — and the UI should enforce this, not document it

`tile.openstreetmap.org` · MapTiler Cloud · OpenMapTiles' pre-generated tilesets (the *schema*
is fine; the tilesets are a commercial product) · Carto, Mapbox, Esri, Google (all API-metered,
none advertising bulk offline derivation at consumer scale).

---

## 4. Turn the prescription into code, not prose

A default that lives only in documentation will be ignored by exactly the users who most need
it. Four mechanisms:

1. **Refuse the prohibited hosts.** A URL-template box that accepts
   `tile.openstreetmap.org/{z}/{x}/{y}.png` and prints a warning is still a tool for violating
   the OSM policy. It should decline, name the policy, and link the permitted alternatives. Same
   for MapTiler Cloud hostnames.
2. **Order the picker vector-first.** Uploaded PMTiles/MBTiles and "point me at my own copy of
   Protomaps" go above anything requiring a key.
3. **Auto-fill attribution per source kind.** `ATTR` may not be empty, and a free-text box will
   be left blank or filled with junk. Each recommended source has exactly one correct string —
   ship them, and let the field be overridden rather than authored. Note the pack format carries
   **one** `ATTR` string, so a compound source needs the credits concatenated.
4. **Keep the pre-build size estimate** (`PLAN.md` Phase 5). It is pure math, needs no network,
   and it is the only thing standing between a user and a multi-gigabyte surprise. Its stated
   purpose should shift from "don't blow your API quota" to "don't blow your watch's flash".

---

## 5. Build order

The honest starting point: **no compliant BYO path is implemented today.** The CLI supports
`synthetic` and HTTPS URL templates; `dir`, `mbtiles` and `pmtiles` are unlanded Phase 1.x work
and the vector renderer is unlanded Phase 2. So every real source that works right now is one
the appendix rules out in practice. That is the gap this order closes.

| # | step | unlocks | notes |
|---|---|---|---|
| 1 | **S2 — fix the defaults** | honesty | Remove MapTiler/Stadia-first from `PLAN.md`, add the § 3 ordering, add the host refusal, add a compliance note to `--source` help. Hours, not weeks. Do this first because everything downstream quotes it |
| 2 | **`pmtiles` + `mbtiles` source kinds** | T3 for people who already have vector tiles | Phase 1.x, already planned |
| 3 | **Palette + snap-to-declared-slots quantiser** | quality floor | `MAP_CARTOGRAPHY_SPEC.md` §§ 3–4 and E5. Small, and a precondition: without palette-first rendering the output looks wrong no matter how good the source, and a blit-time LUT cannot rescue an anti-aliased pack |
| 4 | **Phase 2 renderer, Linux, server-side** | everything | The long pole. Turns permitted vector data into watch tiles. Running it centrally rather than on users' machines removes its three named risks (untested Windows, `cargo install` without a C++ toolchain, cross-OS CI) |
| 5 | **W-F catalog** | **T1 — a working product for most owners** | ~50 rendered regions as static files with a browsable index. No slicer needed. v1 ends here |
| 6 | **W-E archive + browser slicer** | T2 | Also the point at which T3 becomes nearly free: the renderer exists, so exposing it as a CLI path costs little |
| 7 | **W-G companion transfer** | phone-first | Gated on R2 (can UNA's app carry files, at what throughput) and change C2 (no resumable-transfer integrity story until the format has a verifiable prefix) |

Steps 3 and 4 are needed by T1 and T2 regardless, which is the reason this ordering is cheap:
**the BYO path is a by-product of building the catalog, not a parallel effort.**

---

## 6. Pack defaults to state once and reuse everywhere

Every recommended path should produce packs with the same shape, so that catalog packs, sliced
packs and BYO packs are interchangeable:

- **`tile_dim` 128**, not 256. A 256 px ABGR2222 tile is 64 KiB and a 240 × 240 viewport can
  straddle four of them — 43 % of the GUI RAM budget on tile cache alone. At 128 px the
  nine-tile worst case is 144 KiB, and the format's tile ceiling rises with it.
- **z12–16, plus z11 for orientation.** z16 is the floor: z17 spans 203 m, which is off-screen
  before it can be read at running pace.
- **Compression on.** Measured at 7.7 % over a full real pack, with the ratio *improving* with
  zoom, so an extra level costs ≈ 2.2×, not 4×.
- **Attribution displayed for 5 s on first open** — what the OSMF safe harbour asks for, and no
  more.

---

## 7. What this document does not settle

- **Legal item L3** decides whether Stadia can be recommended at all (§ 3.4). Until then it is
  listed, not recommended.
- **Risk R2** — whether UNA's mobile app can carry arbitrary files — decides whether the
  end-user path is phone-first or desktop-first. It does not change any recommendation above,
  because USB mass storage works today, but it does decide whether the catalog should default to
  city-sized packs (a 29 MiB metro pack over BLE is 5–16 minutes; a 3 MiB city pack is under two).
- **Whether the catalog needs regional cartography variants.** Deferred with terrain, past v1.
