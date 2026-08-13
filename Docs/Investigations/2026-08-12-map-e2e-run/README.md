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

**Log.**

**Verdict.**

## L2 — Stand up a renderer we own

**Hypothesis.** `tileserver-gl` serves raster tiles from the PMTiles extract with no account,
no key, and no third-party request path. It is BSD-3 and unrelated to MapTiler's data terms
(`MAP_COMPLIANCE_APPENDIX.md` § 2.3 covers the licensed MapTiler product; this is not it).

**Method.** Run it under docker against the L1 file. Confirm a tile renders in a browser
before involving slippypack. Record the style used and its provenance — the style is a render
input and `MAP_CARTOGRAPHY_SPEC.md` has opinions the default style will not satisfy.

**Log.**

**Verdict.**

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

**Log.**

**Verdict.**

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

*Written last, from the logs above. If this section is still empty, the investigation is not
finished.*
