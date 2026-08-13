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
| `una-sdk` | the watch SDK, the PoC app, deploy | `~/git/cpp/una-sdk` |
| `slippypack` | the pack writer (Rust CLI + core, PWA later) | `~/git/rust/slippypack` |
| `rawtiles` | the byte format spec + conformance corpus | no clone on this machine; `git clone http://nas:3000/toby/rawtiles.git` |

**The local `slippypack` clone is behind.** It sits on `main` @ `1f9132d` ("docs: drop
Tier-1/Tier-2 vocabulary"), which is spec-0.5-era, and it did not have
`map-delivery-workflow` fetched at all until 2026-08-12. `main` matches both remotes, so
nothing is lost — but `git fetch` before reading anything, and do not assume the worktree
reflects the branch the audit was written on.

**The Gitea at `nas:3000` is authoritative; the GitHub copies of `slippypack` and `rawtiles`
are downstream mirrors of it.** Both re-verified 2026-08-12: `slippypack` matches GitHub
commit-for-commit on `main` and `map-delivery-workflow` (`b8d5464`); `rawtiles` is `main` @
`38d4d26` and nothing else. **The Gitea speaks HTTP on port 3000, not SSH** —
`git ls-remote http://nas:3000/toby/<repo>.git` works, `ssh://nas:3000/…` is refused by the
listener. Push to the Gitea and confirm with `ls-remote` afterwards — a branch pushed only to
GitHub can be removed by a sync from the authoritative side, and **that has already happened
once**: see `Docs/External/rawtiles/README.md`. Never force-push blind on a shared branch
either; other sessions push to these.

---

## 1. Where the truth lives — read before deciding anything

On `slippypack`, branch **`map-delivery-workflow`** (head `b8d5464`, unmerged — `git fetch
origin map-delivery-workflow` first; see the clone caveat above). The proxied copies under
`Docs/External/slippypack/docs/` in this repo are the same content and are the durable ones:

- `MAP_DELIVERY_WORKFLOW.md` — candidate scoring W-A…W-H, the recommendation, sequencing,
  risks R1–R*, spin-offs S1–S6, charter experiments X1–X7 with what was cut.
- `MAP_COMPLIANCE_APPENDIX.md` — every source considered, verbatim clauses, verdicts, the
  ODbL obligations that travel with a pack, and § 5's list of legal-review items.
- `MAP_CARTOGRAPHY_SPEC.md` — palette, line weight, labels, zoom ladder, the render and
  quantise pipeline, activity LUTs.
- `Docs/Investigations/2026-08-07-watch-cartography/` — E1–E7 plus the retrieved terms as
  committed HTML, so every quote is checkable.
- `MAP_END_USER_PATH.md` — **it survives only as the proxy in this repo.** Checked
  2026-08-12: not in `origin/map-delivery-workflow`'s tree, not in the local worktree
  (which is clean, on old `main`). Whatever worktree held it uncommitted is gone. The copy
  at `Docs/External/slippypack/docs/MAP_END_USER_PATH.md` is now the original; commit it to
  `slippypack` from there if that repo should carry it. § 3's ranking is also reproduced in
  card `F1` below, which is why the board did not depend on it.
- `PLAN.md` — phasing. Parts of it predate the audit; see `A1` and `C1`.

On `una-sdk` (repo root): `RAWTILES_SPEC_ADEQUACY.md` — the v0.6 grading, needs matrix,
must-fix items M1–M8, E1's identity defect, and § 11's change list. **Read
`Docs/External/rawtiles/README.md` alongside it:** the branch that implemented that change
list no longer exists on any remote, so § 11 is a set of instructions again rather than a
description of something landed. Card `B0` exists because of this.

**Prior art: `Docs/Research/2026-08-13-watch-cartography-prior-art.md` on the `research` branch**
(commit `74d8327a`). Read it before any further cartography work. Several cards below were derived
by photographing a watch and turn out to have published names, numbers and standards — OSM Carto
shipped this board's zoom-selection fix in 2015 with thresholds, and IHO S-52 settles two things
treated here as open questions. It also flags where *not* to trust the literature.

**And `Docs/Investigations/2026-08-12-map-e2e-run/` — the only document here written from
running the thing rather than reading about it.** Card `E3`'s links L1–L4 are done: data
downloaded, tiles rendered, a validated 45 MiB Athens pack built, all by a compliant route.
`RUNBOOK.md` beside it is reproducible instructions with expected output, and stops hard at the
first unverified step. **Several cards below were rewritten by what it found, and where this
board and that investigation disagree, the investigation measured it.**

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

**`A1` and `A2` have a worked prompt: `MAP_SOURCE_COMPLIANCE_PROMPT.md`** (this repo root).
It carries the appendix citations, the `PLAN.md` line references, the choke point in the CLI
source, and the four decisions the cards leave open. Start there rather than from the two
paragraphs below.

**A1 — Strip the prohibited default from `PLAN.md`.** `PLAN.md`'s first-run flow lists
sources "in order of expected friction" with **MapTiler first, Stadia second**; MapTiler
Cloud's terms prohibit this product outright and Stadia reaches only a client-side
workflow. Replace the ordering with `F1`'s. Add a compliance note to `slippypack make
--source`'s help. This is logged spin-off **S2**.
→ `slippypack`, `fix/source-compliance-defaults`. Done when: no permitted-source claim in
the repo rests on a source the appendix does not permit, and `--source --help` says which
hosts are refused and why. **Small–medium** — "replace the ordering" undersells it; the
worked config example, the attribution defaults, the quota threshold and the stated cost
model all name MapTiler Cloud as well.

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

**A second reason to exist, from the 2026-08-12 run:** the lost branch also carried **detached
signatures**, and that run showed `pack_uuid` is unattested by the pack — any non-zero value is
accepted and echoed back as identity by both readers. Signatures are the mechanism that closes
that, so `B0` is not only about `M1` any more.

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

**Two additions from the 2026-08-12 run, one of which the list did not reach:**

- **The data producer's version, upstream of the renderer.** The PMTiles extract carries
  `planetiler:version`, its githash, and the basemap `version` — all of which change pixels and
  none of which the list mentions, because the list starts at the renderer.
- **Font stack availability is not hypothetical.** The renderer image ships exactly one stack
  (`Noto Sans Regular`). Any style asking for Medium or Italic silently drops those labels. It
  is the first item on this card and it fired on the first render anyone attempted.

→ same branch stack as `B1`, or its own follow-up. Done when: the list is exhaustive and
each entry has a verdict. **Small-medium.**

**B3 — Separate "same recipe" from "same bytes". *New; forced by the 2026-08-12 run.***
That run found something no descriptor key can fix: **the renderer is not deterministic.** One
tile in 480 returns more than one distinct PNG; forty fetches of it returned three. The
difference is a single pixel off by one in a channel, which the ABGR2222 quantiser then amplifies
into a whole palette level whenever the pre-quantisation value sits beside a decision boundary —
about 4 % of pixels do. Five builds from identical inputs produced three distinct packs.

So `pack_uuid` is stable while the bytes are not, and the break runs both ways: **the bytes do
not attest the UUID either.** Overwrite the header UUID with anything non-zero, repair the CRC,
and both the independent validator and `slippypack inspect` accept it and report it as identity.

The fix is not a new key, because nothing is *missing* from the descriptor — the render simply is
not a function of it. § A.4 conflates two questions that need two answers:

| question | answered by | must be computable |
|---|---|---|
| "did I ask for the same thing?" | `pack_uuid`, derived from the descriptor | **before** the pack exists — that is what `debug uuid` is for |
| "do I have the same bytes?" | a published content digest | only **from** the bytes |

Replacing `pack_uuid` with a content hash is therefore the wrong fix: it answers the second
question by destroying the ability to answer the first. Do both. The writer already streams a
whole-file CRC-32 as it writes, so hashing during write is mechanically in place; what is missing
is a strong digest exposed where a recipient can compare it **before** transferring tens of MB,
and § A.4 saying what is true. Design this with `G7` — the verifiable prefix wants per-region
integrity for the same reason — and note the detached signatures from the lost 0.7 branch are
what close the attestation half.
→ `rawtiles` (Appendix A + § A.4 wording) then `slippypack`. Stack on `B0`/`B1`; all three edit
Appendix A. Done when: a recipient can check both directions, and neither promise overstates.
**Medium, and cheap only while v0.x lets identity change.**

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

**Re-scoped 2026-08-12 after measurement. `E5`'s benefit was an artifact of the style it was
measured against, and this card's real justification turns out to be `R4` and the activity LUTs.**

Snapping recovers the palette-first *appearance*, never palette-first *determinism* — those were
treated as one property and are not. Worse, its effect **reverses** once the style is palette-first,
which the cartography spec mandates. Perturbing every pixel by one unit, measured:

| style | exposed @ 64 slots | exposed @ declared |
|---|---:|---:|
| stock off-palette theme | 4.16 % | 0.99 % — snapping helps |
| palette-locked watch style | **0.42 %** | 0.77 % — **snapping hurts** |

With only ten declared slots an antialiasing blend lands near the midpoint *between* two of them
more often than near a slot centre in the dense 64-code lattice. And neither number buys
determinism: five builds of the palette-locked pack still produce three distinct outputs (see
`B3`, `G8`).

What survives is `R4`: § 3 says "one code per feature class, no exceptions — this is what makes
§ 5's LUT work", and an antialiasing fringe introduces codes no feature class owns, which a
per-activity LUT cannot map predictably. **So do `C3` for the activity variants (`G6`), not for
determinism and not on the assumption that appearance needs it** — the fringe pixels are legal,
displayable panel codes, and whether a softened road edge is a defect has never been checked on
hardware.
→ `slippypack`, `feat/palette-snap-quantiser`, stacked on `C2`. **Small — and now gated on a
hardware look, not on `C2` alone.**

### Group D — CLI ergonomics (independent, small, each its own branch)

Ordered by leverage per line changed. **`D1` is measured at 43× and belongs first.**

**D1 — Don't rate-limit loopback.** Builds against a renderer on your own machine are
throttled to the default non-OSM 4 req/s. Special-case `127.0.0.1`, `::1` and `localhost`
to unlimited. **Measured 2026-08-12: the Athens pack takes 172 s at the default rate and 4 s
with the limit raised** — 687 tiles against a 4 req/s floor of 171.5 s, so **the limiter is
essentially the entire build time and rendering is free beside it.** Every style iteration
currently pays three minutes to look at an idea. **Trivial, and it is the highest
leverage-per-line card on this board.**

**D2 — `--tile-dim`.** `tile_dim_px` is hardcoded to 256 for URL-template builds. The
cartography spec prescribes **128** (a 240×240 viewport can straddle four 256 px tiles =
43 % of the GUI RAM budget on cache alone; nine 128 px tiles is 144 KiB). Expose the flag;
**do not change the default** until the app's tile constants move with it, and say so in
the flag's help.

**Promoted 2026-08-12: this is a prerequisite for cartography, not an ergonomic nicety.**
`MAP_CARTOGRAPHY_SPEC.md` § 4 specifies line weights in panel pixels and § 7 prescribes
`tile_dim = 128`, but § 7's m/px table is arithmetically 256 px-tile pixels — so `z@128` is one
level coarser than the ladder intends, and judging a 4 px stroke at 256 px then shipping at 128 px
invalidates every weight in § 4. Style iteration therefore needs this flag before it can produce a
falsifiable judgement. **Settle the ladder in m/px first** — restatement and arithmetic in
`Docs/Investigations/2026-08-12-map-e2e-run/`, finding 18; at 128 px the ladder is z12–z17.

**Add a cross-check while you are in here, because nothing verifies this field.** Measured
2026-08-12: `tile_dim_px` is checked only for being greater than zero. Set it to 128 on a pack
whose tile blobs are 65,536 bytes and the independent validator passes it and `inspect` repeats
the false value back. For ABGR2222 the relation is exact — bytes per tile = `tile_dim_px²` at one
byte per pixel — so this is a line of code. **The field this card is about to start changing is
the one with no verification behind it, and it fails as garbage on the panel with drifting tile
offsets, not as an error on a laptop.**
→ **Small — but note the app-side coupling, and pair it with `D4`.**

**D3 — `pmtiles` and `mbtiles` source kinds.** Already Phase 1.x. Include remote PMTiles
via HTTP range reads, which removes a separate extract step from every local build.
**Medium.**

**D4 — `slippypack verify <pack>`.** `format/reader.rs` exists and `spec-validator-cpp` is
already the independent second opinion; wiring them to a subcommand removes "go clone
rawtiles and build the C reader" from every validation. **Small.**

Two corrections from 2026-08-12. **`spec-validator-cpp` is vendored in `slippypack` already** —
`make`, a C++17 compiler, no libraries — so nobody ever needed to clone `rawtiles` for this; the
cost is a `make` and a path, not a repo. And the validator has the blind spots `D2` names:
`tile_dim_px` unchecked against tile size, and any non-zero `pack_uuid` accepted. A `verify`
subcommand should close both rather than inherit them.

**D5 — `slippypack estimate`.** Pure math, no network: tiles, bytes at the measured
per-zoom compression ratios, and whether the result fits the device's budgets. Reframed
from the PWA's API-quota guard to a flash-budget guard. **Small.**

**D6 — Local-source freshness. *This card's premise was wrong, and the truth is worse.***
It said: a local renderer sends no `Last-Modified`, so every local build warns and writes the
zero sentinel; accept `--timestamp now` or derive from source mtime.

Measured 2026-08-12: **tileserver-gl does send `Last-Modified`, and the value is the renderer
process's start time.** So there is no warning, no sentinel, and the pack records that as its
freshness — observed `build_timestamp` 2026-08-13T00:41:57Z on data whose actual OSM replication
time was 2026-08-12T04:00:00Z. Restarting the container changes the claim. A pack now asserts
freshness it does not have, silently, which is worse than the zero sentinel honestly saying
"unknown".

Both proposed fixes are also wrong: `now` records when someone ran a build and mtime records when
a file was written, and § 4.10's field means *source-data* freshness. **The right value is
already in the PMTiles metadata** — `planetiler:osm:osmosisreplicationtime` — and `--timestamp`
already exists to carry it, documented as a "CI override". So this is derive-and-pass plus a
warning when a source's `Last-Modified` cannot be trusted, not a new flag. **Trivial, and
`build_timestamp` is not in the descriptor, so none of this moves `pack_uuid`.**

**D7 — Canonical bbox.** The writer emits the *requested* bbox verbatim rather than
§ 4.9's canonical tile-coverage bbox. Harmless to readers, wrong for a writer claiming
reproducibility. Logged spin-off **S4**. **Small.**

### Group E — The tools that are not slippypack

**E1 — A panel preview. *A working minimal one exists as of 2026-08-12; re-scope this card to
productionising it.*** Render a pack region to a 240×240 PNG through the real ABGR2222
palette, optionally through the four activity LUTs. The panel has **only three of 64 codes
below L\* 40 and no neutral dark grey between L\* 23.7 and L\* 66.5** — style iteration
against that without a preview is guesswork, and the watch round-trip is minutes per
attempt. Highest-value new surface for cartography, and it needs no hardware.

**The seam question this card spends its length on dissolves: a preview needs no renderer
dependency at all.** `Docs/Investigations/2026-08-12-map-e2e-run/scripts/panel_preview.py` decodes
tiles straight out of a finished pack, composes the viewport and writes it — a short script, no
slippypack code, no renderer. So `slippypack preview` is viable without the dependency the card
feared. What remains is productionising: the activity LUTs, a `--zoom`/`--centre` interface, and
RLE once a decoder exists.

**And it paid for itself immediately.** L1–L4 all passed, the pack validates, and the preview
showed the map is a **white screen with faint marks** — roads gone, buildings yellow, ~94 % of the
viewport above L\* 93. Nothing upstream of a rendered preview would have revealed that. This is
the strongest argument on the board for doing `E1` before more of Group C or D.
→ Done when a style change is judgeable on a laptop — **which it now is.** **Small, from here.**

**E4 — Carry labels as data, not pixels. *New; the highest-leverage architectural change on this
board.*** Today a label is baked into the tile. Three separate problems all resolve to the same
change, and none of them resolves without it:

- **Course-up orientation.** The app is north-up today (`TrackFaceMap::update` takes centre, zoom and
  fix — no heading), but a running watch plausibly wants direction-of-travel up. **A label baked into
  a tile rotates with the tile**, so IHO S-52's "symbols and text should always be drawn screen-up"
  becomes unsatisfiable offline at any price. The upright-label algorithms do not rescue a
  pre-rendered pack: they assume a live renderer that knows the current heading.
- **Boundary/orbital labelling**, which the prior-art report recommends as the strongest available
  reframing, anchors labels to the *screen* while the map pans beneath — so they cannot be baked
  either, whatever the orientation.
- **S-52 § 3.4.3 chrome placement** (see `E5`), which needs the app to own text placement.

→ A label section in the `.rawtiles` extension mechanism carrying position, text and class, drawn
on-device at draw time. Note the frequent claim that the device has no font engine is wrong as
stated — there is none for *map* text, but the app already renders UI text and draws the GPS trace
over the blit. Done when a pack's labels stay upright while the map rotates. **Medium-large, and it
gates the cartography work that follows it.**

**E5 — Move the status overlay off the chart. *New; a settled requirement, not a design question.***
`Acquiring GPS …` sits across the lower third of the live map for the whole fix-acquisition window —
the first minute of every run. IHO S-52 § 3.4.3 requires the text panel **outside** the reserved
chart area, permitting overlays only when temporary, drawn in dedicated UI colours, and
user-relocatable. This one is none of those, and it also collides with map labels because two text
systems share a framebuffer with nothing arbitrating.
→ `una-sdk`, app-side. **Small, and it is currently defeating the map face it sits on.**

**E2 — A deploy command.** One invocation that finds the mounted watch volume, **refuses
while BLE sync is active**, copies to the app-sandbox-relative path, byte-verifies, and
tells you to eject. This encodes two traps as code instead of tribal knowledge: USB-MSC
writes and the watch's own BLE sync collide on the same exFAT partition and will corrupt
files, and no absolute `N:/`-style volume path resolves from an app.
→ `una-sdk`, `feat/pack-deploy-tool`. Done when the corruption mode is unreachable through
the tool. **Medium.**

**E3 — Validate the power-user workflow end to end, and write it down. *Started 2026-08-12;
L1–L4 done, L5–L6 open.*** Live in `Docs/Investigations/2026-08-12-map-e2e-run/`, with
`RUNBOOK.md` as the reproducible instructions. Data extracted, tiles rendered, a 45 MiB Athens
pack built and independently validated, all compliant by construction because the renderer is
ours. It did reorder Group D, and it rewrote `C3`, `D1`, `D2`, `D4`, `D6`, `F1` and this card,
and produced `B3`.

**The one-line recipe below hid a prerequisite, so read it as corrected.** "Serve raster locally
(tileserver-gl)" is not a step you can follow: pointed at a Protomaps archive the server starts,
warns that the data is not `openmaptiles` format, serves **vector** happily and **404s every
raster request**. It reads as working until you ask for a PNG. Rendering needs a style written for
the Protomaps schema — its layers are `earth`, `landuse_park`, `roads_minor` — while the bundled
style targets OpenMapTiles. **Cartography therefore stops being a later phase at exactly this
point:** you cannot render without choosing a style, whether or not you meant to.

Corrected chain: `pmtiles extract` a region from the Protomaps planet (2.3 MB and six seconds for
Athens, against ~120 GB for the planet) → **build a schema-matched style and give the renderer its
fonts** → serve raster locally (tileserver-gl is BSD-3 and unrelated to MapTiler's data terms) →
`slippypack make --source 'http://localhost:.../{z}/{x}/{y}.png'` (**`http://` is accepted**;
raise `--rate-per-sec` or pay `D1`'s 43×) → validate with the vendored `spec-validator-cpp` → deploy →
view in the PoC app.
→ `una-sdk` `Docs/Investigations/`, because the run terminates in deploy and the PoC app, which
exist only there. Done when someone else can follow it without improvising — and **L5 and L6 are
what remain**, L6 gated on the unresolved `MapPackTrustMarker.hpp` conflict in AthensRun.
**Medium.**

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

**An axis this ranking could not see, found 2026-08-12: the choice carries a style bill.** The
Protomaps basemap uses the Protomaps schema, so nothing off the shelf renders it — a
schema-matched style has to be generated and its fonts supplied before a single raster tile
exists. **OpenFreeMap, ranked second here, is OpenMapTiles-schema and would have rendered with
tileserver-gl's bundled style and no style work at all.** Permission does not distinguish the two;
tooling cost does, against the one ranked first. This does not overturn permission-first — it is a
real cost that belongs in the decision, and it partly evaporates once the watch has its own
cartography, since then the style is ours either way.
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
BLE transfer of anything large. **Design with `B3`** — both are about identifying bytes rather
than recipes, and `B3` found the second half of why this matters: a resumed transfer against a
*rebuilt* pack will not stitch, because two builds of the same inputs are not byte-identical.

**G8 — Is byte-reproducibility required at all? *New; forced by `B3`.*** Not a task — a decision,
and it now blocks pricing the renderer question rather than the other way round. The renderer is
nondeterministic by about one pixel per few hundred tiles, and `C3` was measured and does **not**
fix it. Two honest positions:

- **No.** While delivery is a whole-file USB copy, one pixel in 45 MiB costs nothing. `B3`'s
  content digest plus § A.4 saying what is true make that safe and honest, and it is cheap.
  **But the prior-art report argues the "yes" branch is cheaper than assumed:** because tiles are
  pre-rendered offline rather than drawn live, byte-reproducibility is *achievable* rather than only
  mitigable, and one-pixel instability is a regulated defect class in the display standards this
  board is now borrowing from. Re-price before answering.
  **This branch got cheaper 2026-08-12:** a palette-first style cuts the *exposure surface*
  tenfold (4.16 % → 0.42 % of pixels beside a decision boundary) for free, as a side effect of
  cartography work that has to happen anyway.
- **Yes,** in which case **aliased rendering is the requirement**, not snapping — and the order to
  price it in is: a deterministic single-threaded CPU rasteriser (mapnik/AGG, or a
  `tiny-skia`-class Rust stack, antialiasing off) **before** writing one ourselves. A rewrite buys
  determinism *and* palette-first output natively *and* collapses most of `B1`/`B2`, but the bill
  is label placement — collision, priority, along-line, continuity across seams — which is the
  majority of the work and of what separates a professional map from a hobby one.

The answer tracks the transfer story, not the map's appearance: harmless under USB, load-bearing
the moment `G4`/`G7`/BLE become real. Decide it before anyone starts `C2`.

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

**A1 + A2 together**, via `MAP_SOURCE_COMPLIANCE_PROMPT.md`. They stop the tool recommending
a prohibited source, and every other card's documentation quotes them. `A2` is hours; `A1`
is larger than this board first carded it — `PLAN.md` leans on MapTiler structurally, not
just in its first-run list, and the prompt scopes that.

**Then `B0` + `B1` + `B3` as one pass**, because all three edit Appendix A and because their cost
grows with delay: additive now, invalidating every issued `pack_uuid` later. `B0` also has a second
clock on it — the analysis that specifies it is a year of context away from being hard to act on,
and it has already been lost once.

**`D1` before any of that if you are about to iterate on cartography.** It is trivial and measured
at 43× on a local build; every style attempt currently pays three minutes to look at an idea.

~~**Then `E3`**~~ — **`E3` is underway.** L1–L4 are done in
`Docs/Investigations/2026-08-12-map-e2e-run/`; it has already reordered Group D and rewritten seven
cards. What remains is L5 and L6, and L6 needs the AthensRun conflict resolved first.

**And answer `G8` before anyone starts `C2`,** because whether the renderer must be deterministic
decides what `C2` is even choosing between.

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
| "`tileserver-gl` renders a Protomaps archive" | Not out of the box. It warns, serves **vector**, and 404s raster until you supply a Protomaps-schema style and fonts — see `E3` |
| "A local renderer sends no `Last-Modified`" | It sends one, and it is the **renderer's process start time**, recorded as the pack's freshness with no warning — see `D6` |
| "Two builds of the same pack are byte-identical" | **They are not.** The renderer is nondeterministic by a pixel; the same inputs gave three distinct packs across five builds — see `B3` |
| "Snap-to-slots will make the render deterministic" | It will not, and its effect on exposure **reverses** once the style is palette-first (0.42 % → 0.77 %, i.e. worse) — see `C3` |
| "Rotated map labels are unreadable, so along-line placement is out" | Rotation costs little *perceptually* (±45° is not significantly slower than upright). The cost is the **glyph matrix** — ~1.5× the upright one — so along-line labels are priced, not prohibited. See the prior-art report |
| "Per-feature perpendicular label offset can't be expressed" | It is a mandated standard (S-52 § 3.1.6, screen-up always) with a studied algorithm. It belongs in a renderer, not a style sheet — and under course-up, in the *app*, not the pack. See `E4` |
| "The map is north-up, so baked labels are fine" | True **today** — there is no heading in the map face. The moment course-up is wanted, every baked label rotates with its tile and no offline fix exists. See `E4` |
| "Per-zoom feature thresholds have to be found by experiment" | OSM Carto published theirs in 2015: residential z10→z13, unclassified z10→z11. Start there and shift for this pitch |
| "Low quantiser exposure means reproducible builds" | Exposure counts pixels that *could* flip. A tenfold cut in it changed the build-to-build divergence **not at all** — the jitter is localised to the few antialiased pixels that remain exposed. See `G8` |
| "Pack size tells you something about content" | Uncompressed ABGR2222 is `tile_dim_px²` bytes per tile regardless of content. Two completely different maps over one bbox are the **same size to the byte** |
| "A validated pack is a correct pack" | Validation proves well-formedness only. Change a pixel, repair the CRC, it passes — as it must |
| "`tile_dim_px` is verified" | Checked for `> 0` and never against tile size. A pack claiming 128 px tiles full of 256 px ones validates clean — see `D2` |
| "`pack_uuid` in the header is trustworthy" | Any non-zero value is accepted and echoed as identity by both readers. The bytes do not attest the UUID — see `B3` |
| "Validating needs the `rawtiles` repo cloned" | `spec-validator-cpp` is vendored in `slippypack`: `make`, a C++17 compiler, no libraries |

---

## 6. Not in scope

Re-opening `.rawtiles` as the on-device format. Vector rendering on the watch. Hosting,
payments, or accounts. Competing with GIS tools. The upstream SDK map-interface proposal,
which is on hold in favour of the fork PoC. Any marketing claim about being class-leading —
that needs three watches photographed side by side in the same light, and it has not been
done.
