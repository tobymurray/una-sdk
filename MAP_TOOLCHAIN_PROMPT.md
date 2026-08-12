# Prompt: Build the map toolchain the compliance audit implied — as independent tasks

The 2026-08-07 delivery investigation ended with a recommendation and a pile of implied
work spread across three repositories, in no order, with nothing recorded about what
depends on what. This prompt cards that work.

**It is deliberately not one task.** Most cards below are independent and should each land
on their own branch with its own reason to merge. Two are ordering constraints that will
cost real money if ignored (`B1` before `C*`; `A*` before anything user-facing). Pick a
card, or pick a group, and work it to completion — do not attempt the whole board in one
pass.

The through-line, so the cards make sense: **every tile source that permits building
offline archives is vector or raw data.** No compliant, ready-to-quantise raster basemap
source was found. That means the project renders its own tiles, which turns a fetch-and-
quantise pipeline into a render-and-quantise pipeline — and rendering drags in style
identity, palette design, and a new class of reproducibility bug. Most of this board is
the consequence of that one sentence.

---

## 0. Ground rules (hard constraints)

- **Never post anything to GitHub** — no PRs, no comments, no issues, on any repo,
  including `@`-mentions of review bots. `gh` is read-only. Pushing branches to a fork
  origin is fine.
- **Commits** are authored and committed as `toby.murray@protonmail.com`. Commit messages
  are terse and mostly *why*, not *what*. No mention of AI assistance anywhere in a commit,
  branch name, or document.
- **One reason to merge per branch.** Fixes, refactors and features never share a branch;
  stack dependent branches rather than combining them.
- **Never state counts** of tests, cases, mutations, or lines — in reports or in
  conversation. Name what is covered instead.
- **A green suite is not evidence.** Before claiming something is covered, mutate it: flip
  a constant, drop a guard, invert a condition, and confirm a test fails. If nothing fails,
  the coverage is decorative.
- **Do not bulk-fetch tiles from anyone's server** — including "just a few hundred to
  test." If a source's terms prohibit pre-emptive fetching, they prohibit your experiment
  too. Legitimate inputs: `--source synthetic`, the committed Athens pack, data pulled
  through a channel the provider intends for bulk download, or a renderer you are running
  yourself.
- **Verify, don't trust this prompt.** Every fact here was checked on 2026-08-12;
  re-verify anything load-bearing. Cite `file:line` for code, spec `§` for format claims,
  URL + retrieval date for anyone's terms. Label conclusions `CONFIRMED` (traced to text,
  code, or experiment) or `PLAUSIBLE` (reasoned) — and for each `PLAUSIBLE`, say what would
  settle it.
- **You are not a lawyer.** Distinguish "the terms say X" (quotable) from "we would be fine
  doing Y" (not yours to conclude). Anything needing real legal review gets flagged as
  such, precisely, so it can be asked once.
- Evidence for anything settled by experiment goes in `Docs/Investigations/<date>-<slug>/`
  in whichever repo it belongs to — hypothesis, method, log, verdict. Failures included;
  they are usually the useful part.

### The three repos, and a mirror caveat

| repo | role | local |
|---|---|---|
| `una-sdk` | the watch SDK, the PoC app, deploy | `~/git/una-sdk` |
| `slippypack` | the pack writer (Rust CLI + core, PWA later) | `~/git/slippypack` |
| `rawtiles` | the byte format spec + conformance corpus | clone as needed |

**The Gitea at `nas:3000` is authoritative; the GitHub copies of `slippypack` and `rawtiles`
are downstream mirrors of it.** Verified reachable 2026-08-12. Push to the Gitea and confirm
with `git ls-remote` afterwards — a branch pushed only to GitHub can be removed by a sync
from the authoritative side, and **that has already happened once**: see
`Docs/External/rawtiles/README.md`. Never force-push blind on a shared branch either; other
sessions push to these.

---

## 1. Where the truth lives — read before deciding anything

On `slippypack`, branch **`map-delivery-workflow`** (head `b8d5464`, unmerged):

- `MAP_DELIVERY_WORKFLOW.md` — candidate scoring W-A…W-H, the recommendation, sequencing,
  risks R1–R*, spin-offs S1–S6, charter experiments X1–X7 with what was cut.
- `MAP_COMPLIANCE_APPENDIX.md` — every source considered, verbatim clauses, verdicts, the
  ODbL obligations that travel with a pack, and § 5's list of legal-review items.
- `MAP_CARTOGRAPHY_SPEC.md` — palette, line weight, labels, zoom ladder, the render and
  quantise pipeline, activity LUTs.
- `Docs/Investigations/2026-08-07-watch-cartography/` — E1–E7 plus the retrieved terms as
  committed HTML, so every quote is checkable.
- `MAP_END_USER_PATH.md` — **uncommitted in the worktree as of 2026-08-12.** Commit it
  there or fold it in; § 3's ranking is reproduced in card `F1` below so this board does
  not depend on it surviving.
- `PLAN.md` — phasing. Parts of it predate the audit; see `A1` and `C1`.

On `una-sdk` (repo root): `RAWTILES_SPEC_ADEQUACY.md` — the v0.6 grading, needs matrix,
must-fix items M1–M8, E1's identity defect, and § 11's change list. **Read
`Docs/External/rawtiles/README.md` alongside it:** the branch that implemented that change
list no longer exists on any remote, so § 11 is a set of instructions again rather than a
description of something landed. Card `B0` exists because of this.

---

## 2. Settled — do not re-litigate

- **`.rawtiles` is the on-device format.** Blit-ready raster. Vector on the watch is off
  the table (no rasteriser, no fonts, no styling budget).
- **We render our own tiles.** Not a preference — the compliance conclusion, independently
  confirmed by the quality argument.
- **Packs reach the watch by USB mass storage today**, at an app-sandbox-relative path.
  BLE transfer is a later addition, gated on `G4`.
- **The recommended delivery shape** is a pre-rendered archive on static object storage,
  sliced in the browser, with a catalog of ready-made packs in front of it. Runner-up is
  browser-side rendering, and the condition under which it wins is written down.
- **On-watch download is closed.** No radio beyond BLE exists on this hardware.

---

## 3. The board

Each card: **why**, **where it lands**, **what depends on it**, **done when**. Sizes are
rough and deliberately coarse.

### Group A — Make the defaults honest (do first; hours, not weeks)

Everything user-facing quotes these, and right now the tool's own documentation recommends
a source whose terms forbid the tool's only use case.

**A1 — Strip the prohibited default from `PLAN.md`.** `PLAN.md`'s first-run flow lists
sources "in order of expected friction" with **MapTiler first, Stadia second**; MapTiler
Cloud's terms prohibit this product outright and Stadia reaches only a client-side
workflow. Replace the ordering with `F1`'s. Add a compliance note to `slippypack make
--source`'s help. This is logged spin-off **S2**.
→ `slippypack`, `fix/source-compliance-defaults`. Done when: no permitted-source claim in
the repo rests on a source the appendix does not permit, and `--source --help` says which
hosts are refused and why. **Small.**

**A2 — Refuse the prohibited hosts in code, not prose.** A URL-template box that accepts
`tile.openstreetmap.org/{z}/{x}/{y}.png` and prints a warning is still a tool for
violating that policy. Refuse it, name the policy, and point at the permitted
alternatives. Same for MapTiler Cloud hostnames. Note that the existing per-host 2 req/s
cap limits *load* and cures nothing — the prohibition is on purpose, not rate.
→ `slippypack`, `fix/refuse-prohibited-hosts`. Decide and record whether an override flag
exists at all (default recommendation: no — the denylist is host-specific, so a user with
a Thunderforest plan or their own server is unaffected). Done when: a build against either
host fails with a message a non-lawyer understands. **Small.**

**A3 — Correct the Athens pack's provenance note.** Its README calls the build "a one-off
~690-tile fetch, not a bulk pipeline." The OSM policy defines bulk downloading as *any*
pre-emptive fetching and names archive-building explicitly, so that characterisation does
not survive the text. The pack stays as evidence; it must not read as a sanctioned recipe.
Logged spin-off **S1**.
→ `una-sdk`, `docs/athens-pack-compliance-note`. **Small.**

### Group B — Identity, before any renderer exists (blocks Group C)

**B0 — Redo the spec 0.7 adequacy fixes.** Both remotes hold rawtiles at **v0.6**. The branch
that implemented `M1`–`M8`, the extension registry, the widened corpus and the first canonical
RLE encoder is gone — not on GitHub, not on the Gitea, not on this machine. Full account and
probable cause in `Docs/External/rawtiles/README.md`.

The recipe survives intact: `RAWTILES_SPEC_ADEQUACY.md` § 11 is a change list written as
instructions, and the v0.6 corpus survives in this repo on
`origin/tmp/rawtiles-container-pr-description` (`d2f26542`, under `Tests/Host/rawtiles/corpus/`),
so a rebuild starts from a working base rather than from nothing. The expensive part to
recreate is the RLE encoder, whose value came from being cross-validated against an independent
decoder and from the bug that cross-validation found — so rebuild it the same way, not from
memory of the result.
→ `rawtiles`, `spec-0.7-adequacy-fixes` (again). **Push to the Gitea and verify with
`ls-remote`.** Done when the corpus is green, the reference reader implements the new rules, and
the branch is confirmed present on the authoritative remote. **Medium-large, and it blocks
`B1`.**

**B1 — Put the renderer in the canonical descriptor.** *Depends on `B0`.* `PackDescriptor` already carries
`quantiser_version` and `style_hash`, and `Source::Style` hashes the style JSON — but
**nothing captures the renderer**: MapLibre Native's version, and the glyphs and sprites a
style references *by URL* rather than by content. Bump the renderer or repoint a font
stack and you get **the same `pack_uuid` over different bytes** — exactly the `E1` defect
that Appendix A's `M1` fix just closed for compression, re-entering through the renderer
door. And `M1` is itself unlanded again, so that hole is currently open on both sides.

The timing argument is the whole point of this card: descriptor keys are **additive today
and UUID-invalidating once rendered packs exist.** Land it before, or in the same pass as,
any renderer — and fold it into `B0`'s pass if that work is being redone anyway, since both
edit Appendix A.
→ `rawtiles` (Appendix A + a conformance rule) then `slippypack` (`identity.rs`), stacked.
Done when: same style, different renderer version ⇒ different `pack_uuid`, with a test
that fails if the field is dropped; A.5's worked example recomputed; corpus refreshed.
**Medium.**

**B2 — Sweep for the rest of the hole.** `B1` is one instance of a class: *anything that
changes pixels must be in the descriptor or explicitly declared out of it.* Enumerate the
render inputs — font stack availability and fallback, sprite pixel ratio, label language
selection, palette table version, quantiser dither policy — and for each, either commit it
to the descriptor or record why it cannot change output.
→ same branch stack as `B1`, or its own follow-up. Done when: the list is exhaustive and
each entry has a verdict. **Small-medium.**

### Group C — The renderer, re-scoped (depends on B1)

**C1 — Re-point Phase 2 from PBF to vector tiles.** `PLAN.md`'s Phase 2 targets
`pbf:///path/to/europe-latest.osm.pbf`. That predates the audit, and it is now backwards
on both axes: compliance makes **Protomaps/OpenFreeMap vector tiles** the primary permitted
sources, and rendering from MVT is *strictly less work* than from PBF — Planetiler already
did the schema and import work, upstream and maintained, and MapLibre renders MVT natively.
Make `--source pmtiles://… --style watch.json` the headline path; demote PBF to an optional
pre-step (shell out to planetiler or tilemaker). The offline-laptop story improves: a
regional PMTiles extract is a better pre-trip artifact than a country PBF.
→ `slippypack`, `docs/phase2-mvt-first`. Done when: the phase's stated input, risks and
schedule match the sources the project may actually use. **Small (docs), and it re-prices
the phase.**

**C2 — The per-tile API spike.** Unchanged by `C1` and still the long pole's real risk:
`render_static`-per-tile pays setup per tile and risks label placement and line continuity
at tile boundaries. Three exit conditions are already written in `PLAN.md` — keep them
(proceed with `render_static`; extend the binding to expose tile-mode; shell out to
`tilemaker`). Week-one spike, then choose.
→ `slippypack`, `spike/maplibre-tile-mode`. Done when: one exit condition is chosen with a
measurement behind it, and boundary correctness is shown on a rendered seam, not asserted.
**Medium-large.**

**C3 — Palette-first rendering and the snap-to-slots quantiser.** The cartography spec's
`E5` finding: snapping to declared palette slots recovers the palette-first result from an
ordinary anti-aliasing renderer at negligible byte cost, so no custom aliased rasteriser is
needed. Small in code, and only meaningful once `C2` lands — palette-first is a property of
the render, and `E6` showed a blit-time LUT cannot rescue an anti-aliased pack.
→ `slippypack`, `feat/palette-snap-quantiser`, stacked on `C2`. **Small, high leverage.**

### Group D — CLI ergonomics (independent, small, each its own branch)

Ordered by leverage per line changed. `D1` alone materially speeds up the local workflow.

**D1 — Don't rate-limit loopback.** Builds against a renderer on your own machine are
throttled to the default non-OSM 4 req/s. Special-case `127.0.0.1`, `::1` and `localhost`
to unlimited. **Trivial.**

**D2 — `--tile-dim`.** `tile_dim_px` is hardcoded to 256 for URL-template builds. The
cartography spec prescribes **128** (a 240×240 viewport can straddle four 256 px tiles =
43 % of the GUI RAM budget on cache alone; nine 128 px tiles is 144 KiB). Expose the flag;
**do not change the default** until the app's tile constants move with it, and say so in
the flag's help. **Small — but note the app-side coupling.**

**D3 — `pmtiles` and `mbtiles` source kinds.** Already Phase 1.x. Include remote PMTiles
via HTTP range reads, which removes a separate extract step from every local build.
**Medium.**

**D4 — `slippypack verify <pack>`.** `format/reader.rs` exists and `spec-validator-cpp` is
already the independent second opinion; wiring them to a subcommand removes "go clone
rawtiles and build the C reader" from every validation. **Small.**

**D5 — `slippypack estimate`.** Pure math, no network: tiles, bytes at the measured
per-zoom compression ratios, and whether the result fits the device's budgets. Reframed
from the PWA's API-quota guard to a flash-budget guard. **Small.**

**D6 — Local-source freshness.** A local renderer sends no `Last-Modified`, so every local
build warns and writes the zero sentinel. Accept `--timestamp now`, or derive from source
mtime. **Trivial.**

**D7 — Canonical bbox.** The writer emits the *requested* bbox verbatim rather than
§ 4.9's canonical tile-coverage bbox. Harmless to readers, wrong for a writer claiming
reproducibility. Logged spin-off **S4**. **Small.**

### Group E — The tools that are not slippypack

**E1 — A panel preview.** Render a pack region to a 240×240 PNG through the real ABGR2222
palette, optionally through the four activity LUTs. The panel has **only three of 64 codes
below L\* 40 and no neutral dark grey between L\* 23.7 and L\* 66.5** — style iteration
against that without a preview is guesswork, and the watch round-trip is minutes per
attempt. Highest-value new surface for cartography, and it needs no hardware.
→ decide the seam: a `slippypack preview` subcommand (it already owns the quantiser) that
must **not** pull in the renderer dependency, or a separate tool. Done when a style change
is judgeable on a laptop. **Medium.**

**E2 — A deploy command.** One invocation that finds the mounted watch volume, **refuses
while BLE sync is active**, copies to the app-sandbox-relative path, byte-verifies, and
tells you to eject. This encodes two traps as code instead of tribal knowledge: USB-MSC
writes and the watch's own BLE sync collide on the same exFAT partition and will corrupt
files, and no absolute `N:/`-style volume path resolves from an app.
→ `una-sdk`, `feat/pack-deploy-tool`. Done when the corruption mode is unreachable through
the tool. **Medium.**

**E3 — Validate the power-user workflow end to end, and write it down.** The fastest way
to find the real friction. Compliant by construction, because the renderer is yours:
`pmtiles extract` a region from the Protomaps planet → serve raster locally (tileserver-gl
is BSD-3 open source and unrelated to MapTiler's data terms) → `slippypack make --source
'http://localhost:.../{z}/{x}/{y}.png'` (**`http://` is accepted**) → validate → deploy →
view in the PoC app. Record what broke, and land the result as a runbook.
→ `slippypack` or `una-sdk` `Docs/Investigations/`. Done when someone else can follow it
without improvising. **Medium — and it will reorder Group D.**

### Group F — Product decisions (not code)

**F1 — Adopt or reject the end-user tiering.** The proposal: **the default for an end user
is a pack they download, not a source they configure.** T1 (most owners) configures
nothing — a catalog pack over USB, one region-sized pack loaded on day one rather than one
per run. T2 (unusual destination) draws a box against the archive and so inherits the
project's compliance posture instead of acquiring their own. T3 (power users, developers,
other devices) is the only tier needing a source recommendation, ranked **permission
first**:

1. **Protomaps basemap** — planet PMTiles, `pmtiles extract` per region, copy to your own
   storage rather than hotlink. `© OpenStreetMap contributors`.
2. **OpenFreeMap** — weekly planet downloads; use that channel, not the public instance
   ("no limits on requests" is written about serving a live map).
3. **Geofabrik / BBBike raw OSM** — fallback; you own a schema and a pipeline.
4. **Stadia** — client-side only, user's own device, under their per-device ceiling,
   **blocked pending `G2`**.
5. **Thunderforest, Small Business or above** — the documented priced route to bulk
   permission.

Never, and enforced by `A2`: the OSM tile CDN, MapTiler Cloud, OpenMapTiles' pre-generated
tilesets (the schema is fine), and the API-metered majors.
→ decision, recorded. Done when the picker's ordering and the docs both derive from it.

**F2 — Catalog design.** Which regions, what sizes, how named, how indexed — and whether
packs are city-sized or region-sized, which `G4` decides (a region-sized pack over BLE is
several minutes; a city-sized one is under two). This is the cheapest path to a working
product for most owners and needs no slicer.

### Group G — Open questions that can still change the architecture

Each is a question to *answer*, not a task to build. `G1` is the one that could still
overturn a conclusion.

**G1 — National mapping agencies.** The compliance appendix lists national open data
(Ordnance Survey, NRCan, …) only under the terrain estate as "optional local enrichment."
Several agencies — swisstopo, IGN France, Kartverket, OS — publish **pre-rendered raster
or WMTS under open licences with bulk download as the intended channel.** If a compliant
ready-to-quantise raster source exists anywhere, it is here, and it would be the
counter-example the audit looked for and did not find. **Their terms have not been read.**
Give them the appendix's treatment: verbatim quotes, retrieval dates, committed HTML,
PERMITS/PROHIBITS verdicts. Note up front that even a clean result cannot be the global
default — coverage is per country and cartography varies by nation — so the realistic
outcome is regional enrichment, not a change of primary source.

**G2 — Stadia (legal item L3).** Is a `.rawtiles` file on a watch "cached … per device in
a mobile application"? Is the per-device ceiling at-a-time (implying eviction) or
cumulative? Until answered, Stadia is *listed*, not recommended.

**G3 — Copernicus (legal item L4).** The licence page did not render to static HTML and is
therefore unquoted. Needed before any terrain layer ships; not on the critical path.

**G4 — Can UNA's mobile app carry arbitrary files, and at what throughput (risk R2)?**
Decides phone-first vs desktop-first, and sizes the catalog. Does not change the
recommendation — USB works today — but it is the largest open variable in reach.

**G5 — Browser render throughput (experiment X4, cut).** Only needed if the runner-up
delivery shape becomes the path; it was cut because compliance demoted that shape before
throughput mattered. **Must be run before committing to it.**

**G6 — Blit-time LUT cost on device (experiment X7, half-run).** Mechanism proven in
simulation; per-frame cost unmeasured. Decides whether activity variants are free.

**G7 — A verifiable prefix (`blkh`, change C2 of the adequacy report).** The format has no
per-region integrity, so a chunked or resumable transfer has no integrity story. Blocks
BLE transfer of anything large.

### Group H — Logged debt, cheap to clear

**H1 (S3)** — Pin one spec version. Commits, `DECISIONS.md`, and the SDK cite v0.5, a
nonexistent `rawtiles-v1.0-rc1.md`, v0.6 and v0.7 between them.
**H2 (S5)** — The "256 KB app RAM" figure in the SDK's architecture doc is a stale diagram
label; the real budgets are the linker's 500 K service / 600 K GUI.
**H3 (S6)** — The panel is a Sharp LS012B7DD06A: transflective memory-in-pixel, 64 colours
native, 25:1, holds its image at 11 µW. None of this is in the SDK docs, and all of it
constrains UI design well beyond maps.

---

## 4. If you are picking one thing

**A1 + A2 together.** They are hours, they stop the tool recommending a prohibited source,
and every other card's documentation quotes them.

**Then `B0` + `B1` as one pass**, because they edit the same appendix and because `B1` is the
card whose cost grows with delay: additive now, invalidating every issued `pack_uuid` later.
`B0` also has a second clock on it — the analysis that specifies it is a year of context away
from being hard to act on, and it has already been lost once.

**Then `E3`**, because a validated end-to-end run will reorder Group D more reliably than
this prompt's guesses about which friction matters.

---

## 5. Known premise traps

Verified; do not re-derive, and do not trust older documents that contradict these.

| claim you will encounter | what is actually true |
|---|---|
| "RLE measured 32.4 %, so Athens ⇒ ~14.6 MiB" | **7.7 % ⇒ 3.3 MiB** over all 687 real tiles. The old figure came from a handful of dense low-zoom tiles |
| "Each zoom level costs 4×" | **≈2.2×** — tile count quadruples but bytes per tile fall with zoom |
| "The CLI needs HTTPS" | `http://` is accepted, which is what makes a localhost renderer work today |
| "`--tile-dim` exists" | It does not; URL-template builds hardcode 256 |
| "Apps read absolute volume paths like `N:/maps/…`" | No absolute volume path resolves from an app. Sandbox-relative only |
| "256 KB of app RAM" | A stale diagram label. Real: 500 K service / 600 K GUI, and the PoC app fits exactly **one** 256 px tile slot over Running's GUI statics |
| "The pack can use RLE" | The vendored reader fails closed on RLE until its decoder lands, so build `--compression none` for now. This is a capacity feature, not a latency one — a 64 KiB tile read measured 7–9 ms on hardware |
| "`pack_uuid` identifies the bytes" | **It does not.** `M1` fixed this on a branch that no longer exists, so both remotes still hold the defect: same UUID over 6.9× different bytes. Anything written after 2026-08-06 claiming otherwise describes lost work — see `B0` |
| "The rawtiles spec is at 0.7" | Both remotes are at **v0.6** (`38d4d26`). Treat every 0.7 claim as a specification of intended work, not of shipped work |

---

## 6. Not in scope

Re-opening `.rawtiles` as the on-device format. Vector rendering on the watch. Hosting,
payments, or accounts. Competing with GIS tools. The upstream SDK map-interface proposal,
which is on hold in favour of the fork PoC. Any marketing claim about being class-leading —
that needs three watches photographed side by side in the same light, and it has not been
done.
