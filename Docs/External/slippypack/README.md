# slippypack's map work, proxied here because its own origin is not durable

This directory is **a copy, not a home.** `slippypack` develops in its own repository; these
documents are duplicated into `una-sdk` because `slippypack`'s `origin` is a **mirror** of a
Gitea instance at `nas:3000` that is not always reachable. A branch pushed to the mirror can
be clobbered by a sync from the authoritative side, so content that lives only there is not
safely stored. `una-sdk`'s `origin` is a normal fork and is durable, so it acts as the proxy.

**Copied 2026-08-12** from the local clone — which is at `~/git/rust/slippypack`, not
`~/git/slippypack` — whose `main` matches its origin. Note that clone is **behind**: it sits
on `main` @ `1f9132d` and did not have `map-delivery-workflow` fetched at all. The refs below
were read from the remotes, and re-verified against both the GitHub mirror and the Gitea on
2026-08-12:

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

## `MAP_END_USER_PATH.md` exists only here — confirmed

At copy time that file was untracked in the `slippypack` worktree, so it existed in no
repository at all. **Re-checked 2026-08-12, and it is worse than "uncommitted":** the file is
not in `origin/map-delivery-workflow`'s tree, not in the Gitea's copy of that branch, and not
in the local worktree — which is clean, on old `main`. Whatever worktree held it is gone.

**This copy is the original, not a copy.** It is not recoverable from `slippypack` at all, so
treat it as first-class content of *this* repository. Committing it back to `slippypack` is
now a publish, not a sync.

The proxy is what saved it. This is the same failure that took `rawtiles`'
`spec-0.7-adequacy-fixes` — work that existed in exactly one place, which turned out not to
be a place that keeps things — caught here only because copying it out happened first.

## Not covered by this proxy

The **`rawtiles`** repository had the same exposure and the exposure already cost something:
its `spec-0.7-adequacy-fixes` branch — the Appendix A identity fix, the first canonical RLE
encoder, the widened corpus — **is gone from every remote and from this machine.** The loss
record and the rebuild recipe are in `Docs/External/rawtiles/README.md`; the board tracks the
redo as card `B0`.

A correction to what this section used to say: the Gitea is **not** unreachable. Verified
2026-08-12 over HTTP — `git ls-remote http://nas:3000/toby/rawtiles.git` answers, and reports
`main` @ `38d4d26` and nothing else. It listens for HTTP on 3000, not SSH, which is probably
why it read as down. The branch was never pushed there; it went to the mirror only.

## Keeping this current

This copy goes stale the moment `slippypack`'s documents move. Re-copy when they change
materially and update the SHAs above. Do not edit files in this directory — edit them in
`slippypack` and re-copy, or the proxy becomes a second source of truth, which is worse than
no proxy.
