# slippypack's map work, proxied here because its own origin is not durable

This directory is **a copy, not a home.** `slippypack` develops in its own repository; these
documents are duplicated into `una-sdk` because `slippypack`'s `origin` is a **mirror** of a
Gitea instance at `nas:3000` that is not always reachable. A branch pushed to the mirror can
be clobbered by a sync from the authoritative side, so content that lives only there is not
safely stored. `una-sdk`'s `origin` is a normal fork and is durable, so it acts as the proxy.

**Copied 2026-08-12** from `~/git/slippypack`, which at that moment matched its own origin
exactly on both refs:

| ref | commit |
|---|---|
| `map-delivery-workflow` | `b8d546421c29397251fa00a98e0c59e44a105974` (2026-08-08) |
| `main` | `1f9132ddb68330513b56b6f9543791e98758439a` |

**No history is duplicated here, deliberately.** Nothing was local-only, the code is
reproducible, and the crates are not what would be painful to lose. What is proxied is the
part that cannot be reconstructed: written analysis, and licence text as it was actually
served on the day it was quoted. Recover the code itself by cloning `slippypack` from
whichever of the mirror or the Gitea is reachable, and check out the SHAs above.

## What is here

**`docs/`** — the map investigation's reports, plus the two planning documents its findings
contradict, because several cards in `MAP_TOOLCHAIN_PROMPT.md` (repo root) cite them directly:

| file | what it is |
|---|---|
| `MAP_DELIVERY_WORKFLOW.md` | the recommendation: candidate scoring, sequencing, risks, spin-offs, which charter experiments were cut |
| `MAP_COMPLIANCE_APPENDIX.md` | every tile source considered, verbatim clauses with retrieval dates, PERMITS/PROHIBITS verdicts, ODbL obligations that travel with a pack |
| `MAP_CARTOGRAPHY_SPEC.md` | palette, line weight, labels, zoom ladder, render/quantise pipeline, activity LUTs |
| `MAP_END_USER_PATH.md` | what to recommend to end users, and the build order to get there |
| `MAP_DELIVERY_PROMPT.md` | the brief the first three were written against |
| `RAWTILES_SPEC_UPDATE_PROMPT.md` | the spec-side companion brief |
| `PLAN.md` | slippypack's phasing — **partly pre-audit**; Phase 2's PBF-first input and the first-run source ordering are both superseded |
| `DECISIONS.md` | slippypack's decision log — carries the spec-version drift noted as card `H1` |

**`investigations/2026-08-07-watch-cartography/`** — the evidence behind the reports:
experiments E1–E8, the panel datasheet extract, rendered comparison images, the scripts, and
`terms/` — the **retrieved HTML of every licence and policy quoted**, committed so each
verbatim quote can be checked against what was served on 2026-08-07 rather than against what
a page says today. This is the least reproducible thing in the directory: terms change
silently and without versioning.

## `MAP_END_USER_PATH.md` was uncommitted upstream

At copy time that file was untracked in the `slippypack` worktree — it existed in no
repository at all. **This copy is currently its only stored version.** If `slippypack`
becomes authoritative again, commit it there.

## Not covered by this proxy

The **`rawtiles`** repository has the same exposure and is *not* proxied here: it is not
cloned on this machine, and its `spec-0.7-adequacy-fixes` branch — the Appendix A identity
fix, the first canonical RLE encoder, and the widened corpus — exists only on the same mirror
plus the unreachable Gitea. That is the largest unprotected body of work in this effort, and
unlike the crates it is *not* easily reproducible. Worth handling separately.

## Keeping this current

This copy goes stale the moment `slippypack`'s documents move. Re-copy when they change
materially and update the SHAs above. Do not edit files in this directory — edit them in
`slippypack` and re-copy, or the proxy becomes a second source of truth, which is worse than
no proxy.
