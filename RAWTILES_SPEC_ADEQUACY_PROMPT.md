# Prompt: Grade the rawtiles spec against the full needs-space, before the wire format freezes

You are evaluating whether the **rawtiles spec** (v0.6, wire format `(1, 0)`, status
Provisional) is **complete, flexible, and extensible enough** to carry everything a
*user* might ever want to do with maps on this class of device, and everything *UNA*
(the platform) might want to build — or whether it has gaps that must be fixed **now**,
while breaking the wire format is still free.

This is not the earlier evaluation re-run. `RAWTILES_MAP_EVALUATION.md` already settled
"is rawtiles the right altitude vs. PMTiles/MBTiles/vector" (verdict: yes, for
blit-ready raster on this hardware) and the device spike settled feasibility. **Do not
re-litigate the format choice.** Your question is narrower and later-binding: *given*
this direction, does the spec's shape survive contact with the whole space of needs, or
only with the two demos that exist today (a 12-tile park pack and a synthetic fixture)?

Why now: the spec's own gate says v1.0 freezes the wire format. The SDK reader
(`feat/rawtiles-container`) is about to go up for upstream review, and `slippypack`
writes packs people can start hoarding. Every wire-format defect found after that is a
migration; every one found now is an edit. The economics of this evaluation are entirely
"cost of late discovery" — rank everything you find by that.

---

## 0. Ground rules (hard constraints)

- **Never post anything to GitHub** — no PRs, comments, or issues, on any repo. `gh` is
  read-only. Your deliverable is a local markdown report.
- **Verify, don't trust.** Every fact below was true on 2026-08-06; re-check anything
  load-bearing. Cite spec `§` for every claim about the spec, `file:line` for every
  claim about code. Label findings `CONFIRMED` (traced to text/experiment) or
  `PLAUSIBLE` (reasoned), and say what would settle a `PLAUSIBLE`.
- **Grade the spec, not the reader.** SDK-layer gaps (caching, discovery, multi-pack
  composition, widget) are only findings here if the *spec* makes them impossible or
  needlessly hard. Route them to the right layer explicitly.
- **Known premise trap:** older docs claim "256 KB app RAM"
  (`Docs/architecture-deep-dive.md`, a Mermaid label). The real budgets are linker
  `RAM_LENGTH` 500 K (service) / 600 K (GUI) — `cmake/una-app.cmake:217,:281`. Use the
  real numbers.
- Settle questions by experiment where an experiment is cheaper than an argument;
  evidence goes in `Docs/Investigations/<date>-<slug>/` (hypothesis / method / log /
  verdict, failures included).

## 1. Where the truth lives (read before judging)

- **The spec:** clone `https://github.com/tobymurray/rawtiles` (v0.6 HEAD was
  `38d4d26`). `spec/rawtiles.md` is normative (RFC-2119, ~1070 lines): 292-byte header,
  20-byte/entry tile index, aligned tile blob, TLV extensions, CRC-32 footer; ABGR2222 +
  RGB565; None + pixel-RLE; Quadtree/WebMercator + SingleImage/LocalLinear (48-byte
  AFFN); XYZ/TMS; 24-entry zoom directory; canonical `pack_uuid` (Appendix A);
  `supersedes_uuid`; eager/lazy reader validation profiles sized for MCU readers.
  `spec/conformance/` has 17 golden + 79 negative fixtures and their generators.
- **The writer:** `https://github.com/tobymurray/slippypack` (mirror of
  `http://nas:3000/toby/slippypack`) — Rust CLI + core crate (writer, ABGR2222
  quantiser, projection, UUIDv5 identity). URL-template + synthetic sources work today;
  `dir`/`mbtiles`/`pmtiles` sources and a browser PWA are planned (`PLAN.md` there is
  the design record — read it; DECISIONS.md too).
- **The reader:** `origin/feat/rawtiles-container` (this repo) —
  `SDK::RawTiles::Container`, v0.6-conformant (96/96 corpus; RLE recognised but
  fail-closed). The PR body on `origin/tmp/rawtiles-container-pr-description`
  (`tmp/rawtiles-container-pr-description.md`) states four open design questions —
  your findings should answer or sharpen them, especially cross-app pack sharing.
- **Ground truth about the device** (all measured, `origin/spike/rawtiles-device-proof`
  + the container evidence bundle): 64 KiB tile read via `IFileSystem` = 6–9 ms;
  first filesystem touch after app start ≈ 113 ms; apps see a **sandbox-relative**
  filesystem (no absolute volume path resolves); no mmap — tiles are copied into
  caller-owned buffers; render path is `LCD::blitCopy` over ABGR2222 rows on a 240×240
  8bpp panel; storage is 2 MB NOR + multi-GB eMMC behind exFAT (USB-MSC writes
  concurrent with BLE sync corrupt it); `IFile` is absolute-seek + read,
  `skMaxPathLen = 256`.
- **Adjacent platform efforts that shape UNA-side needs:** the Gadgetbridge/companion
  sync work (`GADGETBRIDGE_SYNC_PROMPT.md` on `rawtiles-next-step`) — BLE transfer of
  files to the watch is a live direction, which makes "how does a multi-MB pack get to
  the device, resumably, with integrity" a platform question, not a hypothetical.

## 2. Build the needs inventory (method matters)

Do this in two passes so the seed lists below don't anchor you:

**Pass 1 — independent.** Generate the inventory yourself, from personas, before
reading § 3. User personas at minimum: urban walker, hiker/backcountry, trail runner
with a planned route, cyclist, traveler (many cities, storage-constrained), sailor
(charts), skier (piste maps), orienteer, geocacher, someone mapping a private site
(campus, mine, farm — imagery that is *not* on any tile server), someone who wants
satellite/topo/custom-styled basemaps, a non-English-locale user. Platform personas:
the SDK maintainer (API freeze, multiple future watch models — displays that differ in
resolution, bit depth, maybe RGB888), the companion-app author (transfer, storage
management, pack updates), a future pack-store operator (distribution, dedup,
licensing), the firmware owner (shared storage across app sandboxes, quota), the
security reviewer (malicious packs, validation cost bounds, DoS via crafted
extensions), the lawyer (ODbL attribution obligations travelling with derived tiles).

**Pass 2 — merge and extend** with the seeds in § 3, prior-art mining (§ 4), and the
PR's open questions. Every need becomes a row: *who wants it, what they'd do, which
spec mechanism serves it, grade.*

## 3. Seed needs (verify each against spec text — some may already be served)

User-side, roughly in order of likelihood: make a pack for an arbitrary bbox × zoom
range; carry several packs; know what a pack covers *before* opening it (name, bbox,
zooms, source, build date); update a pack when the map changes (whole-pack supersedence
via `supersedes_uuid` — but is *partial* update, "just the tiles that changed",
representable at all?); very large areas (country-scale — packs hit the u32 4 GiB file
cap where?); overlays vs. basemaps (route highlights, POI layers — is layering a spec
concern or strictly reader-side composition of multiple packs?); day/night themes (two
packs? a palette extension? nothing?); non-WebMercator sources (nautical charts, floor
plans — how far does SingleImage/LocalLinear + AFFN actually stretch? polar regions?);
localized names (NAME's BCP-47 machinery — complete?); attribution display (ATTR —
sufficient for ODbL's "reasonably calculated" standard?); getting packs onto the watch
(USB-MSC today, BLE/companion tomorrow — does a footer-CRC-only integrity model serve
resumable chunked transfer, or does the transfer layer need its own?); storage
management (which pack to evict — is there enough metadata to decide?).

UNA-side: new display hardware (pixel format beyond ABGR2222/RGB565 — RGB888 is 3 B/px:
does every size field, alignment rule, and RLE definition survive odd byte widths? new
`tile_dim` — u16, but is 512 px viable end-to-end?); new compression later (the enum
exists — but does the spec's bounded-decoder-state promise (§ 11) generalise beyond
RLE, or is it RLE-shaped?); zoom > 23 (hard cap — defensible forever?); per-tile
metadata (timestamps, source revision — extension tag or index change?); signing /
tamper-evidence beyond CRC (a pack-store needs it; extension or wire-major?);
cross-app pack sharing (the PR's open question 4 — if firmware ever exposes a shared
read-only mount, does the spec need anything, e.g. stable identity for dedup? Appendix
A's canonical UUID may already be the answer — check); multiple packs covering the same
area (priority/ordering metadata, or reader policy?); diagnostics (enough in the header
to identify a misbehaving pack in a bug report?).

Deliberate limits to audit as *tradeoffs* (right call / wrong call / needs an escape
hatch before v1.0): u32 offsets everywhere (4 GiB cap); one pixel format and one
`tile_dim` per pack; two projections; RLE as the only compression; CRC-32 only at the
footer (no per-tile integrity in the wire — § 14.5 hashes are conformance sidecars);
attribution as free text; extensions as flat TLV with uppercase=critical /
lowercase=ancillary semantics (verify the exact rule) — is that mechanism actually
strong enough to carry most of the "SATISFIABLE-VIA-EXTENSION" grades you hand out?

## 4. Prior-art mining (for forgotten needs, not for relitigation)

Walk the capability surface of PMTiles, MBTiles, Garmin (IMG/JNX/KMZ custom maps),
OruxMaps/Locus offline formats, and mapsforge. For each capability they have and
rawtiles lacks, either add an inventory row or record *why the need doesn't transfer*
to this niche. (Examples to check, not conclusions: MBTiles metadata table's
arbitrary key-value store; PMTiles' internal compression + directories designed for
partial fetch; Garmin JNX's multi-product subscription fields; mapsforge's
multi-language POI model.)

## 5. Grading rubric (apply to every inventory row)

- **SATISFIED** — mechanism exists; cite `§`.
- **VIA-EXTENSION** — representable today as an uppercase/lowercase TLV tag without
  touching the wire format. *Prove it*: sketch the tag (name, payload layout, critical
  or ancillary, and why) — a grade of VIA-EXTENSION without a sketch is a guess.
- **WIRE-MINOR** — needs an additive, backward-compatible bump (old readers must
  still open new packs per the spec's minor-version rules — verify what those actually
  promise).
- **WIRE-MAJOR** — breaking. These are the only findings that are *expensive later*;
  each one needs: the need, the break, a concrete fix, and your judgement on whether
  the need is real enough to pay for now.
- **OUT-OF-SCOPE** — defensibly not this format's job (say whose job it is).
- **SDK-LAYER** — spec is fine; the reader/platform needs to build it.

## 6. Extensibility stress tests (do these on paper, in the report)

Drill each of these as if implementing, and record where the spec bends vs. breaks:
(a) add RGB888; (b) add an LZ-family compression; (c) add per-tile timestamps for
delta updates; (d) add a signature block; (e) ship a 6 GiB country pack; (f) define a
"pack collection" (ordered layering of N packs); (g) a writer that must stream output
without knowing tile count up front (footer CRC + header fields — what must be known
when?); (h) split one pack into two regions and later re-merge them byte-identically
(Appendix A determinism — does it hold under re-partition?).

## 7. Deliverables

1. `RAWTILES_SPEC_ADEQUACY.md` (repo root, untracked): the needs matrix (every row
   graded + cited), the stress-test results, the tradeoff audit, and a **ranked gap
   list** — ordered by cost-of-late-discovery, split into: *must fix before v1.0
   freeze* (wire-major/minor), *should define as extension tags now* (so early writers
   agree), *SDK-layer backlog*, *won't-fix with rationale*. End with the verdict, one
   line per adjective: complete? flexible? extensible? — each yes/no/conditional with
   the single strongest supporting finding.
2. A concrete change list for the `rawtiles` repo (spec edits, new fixtures, new
   extension-tag registry entries), mergeable into the existing "PR 0 spec debt" stack
   (RLE encoder + missing RGB565/RLE goldens + § 9.11 exact-consumption wording).
3. Answers (or sharpened framings) for the container PR's open questions 3 and 4,
   grounded in what the matrix showed.
4. Spin-off list for anything real but out of scope, with its target repo/branch.
5. Investigation bundle for anything you settled by experiment.

**What "done" looks like:** a reviewer who has never seen rawtiles can read the report
top to bottom and know exactly what the format can serve today, what it can absorb
without breaking, what it can never do, and what must change *this month* — every claim
carrying a spec citation, a sketch, or an experiment, and none carrying only vibes.
