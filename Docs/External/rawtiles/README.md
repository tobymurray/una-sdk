# rawtiles — a loss record, not a proxy

There is nothing to proxy here, and that is the finding.

## What was checked, 2026-08-12

| location | refs found |
|---|---|
| `github.com/tobymurray/rawtiles` (mirror-cloned, all refs) | `main` @ `38d4d269e4c66f14a839933cf94e259f93529d51` — nothing else, no tags |
| `nas:3000/toby/rawtiles` (authoritative Gitea, **reachable**) | `main` @ `38d4d26`, repository last updated **2026-05-17** |
| this machine | no clone, and no trace of the work products (`generators/_rle.py`, `spec/extensions.md`, any corpus directory) |

`main` @ `38d4d26` is **spec v0.6** — the version the adequacy audit graded and found wanting.

## What is missing

The branch recorded as **`spec-0.7-adequacy-fixes`** — roughly fourteen commits of spec work
done 2026-08-06 — is **not on either remote and not on this machine.** It carried:

- **`M1`** — the Appendix A fix that made the canonical descriptor determine the pack's bytes
  (`compressions`, uppercase-only `extensions_hash`, `supersedes`), with A.5's worked example
  recomputed. This is the fix for the defect where two packs with different compression
  derived the *same* `pack_uuid` over entirely different bytes.
- `M2`–`M8` — validation rule #40 and its negative fixture, widened BCP-47 language matching,
  the reserved `ABGR2222_A` format with frozen alpha semantics, bits-aware width and
  per-compression decoder bounds, the minimum-length rule for compressed tiles, detached
  signatures, and the reserved deflate window.
- The **first canonical RLE encoder** (`generators/_rle.py`), whose conformance vectors caught
  a real remainder-run bug, cross-validated three ways against the C reference decoder.
- The extension registry (`spec/extensions.md`: `blkh` and `sups` specified; `tmet`, `covr`,
  `lics`, `genr`, `coll` reserved), a `golden-blkh` fixture, and the corpus widened well
  beyond its v0.6 shape.

**Probable cause.** The branch was pushed to GitHub, which is a **mirror** of the Gitea. The
Gitea never received it — its repository has not been touched since May. A sync from the
authoritative side would remove a branch the authoritative side does not have. This is the
exact failure mode that was written down as a caveat at the time, and then not acted on.

Note the contrast, because it tells you the direction of trust: **`slippypack` is current on
the Gitea** (updated 2026-08-08, matching its GitHub copy commit-for-commit), so that repo was
never at risk. Only rawtiles fell through.

## What survives, and what it costs to rebuild

**The recipe survives.** `RAWTILES_SPEC_ADEQUACY.md` (this repo root, now tracked — see the
commit that added this file) contains the full grading, items `M1`–`M8` with their rationale,
and **§ 11, a change list written as instructions to the rawtiles repo.** The lost branch was
an implementation of that section. Redoing it is work, but it is not rediscovery: the analysis,
the experiments, and the decisions are intact, including which of the author's own conclusions
were overturned and why.

**The v0.6 corpus survives** in this repository on `origin/tmp/rawtiles-container-pr-description`
(`d2f26542`) under `Tests/Host/rawtiles/corpus/` — goldens, negatives, hashes, and a
conformance runner. That is the base the 0.7 additions were built on, so a rebuild starts from
a working corpus rather than from nothing.

**What is genuinely gone** is the spec prose as edited, the new and widened fixtures, and the
canonical RLE encoder — the last being the most expensive, since its value came from being
cross-validated against an independent decoder and from the bug that cross-validation found.

## Consequences to carry forward

1. **Do not treat `M1` as landed.** Anything written after 2026-08-06 that says the descriptor
   determines the bytes is describing a branch that no longer exists. The defect is live in
   v0.6, which is what both remotes hold.
2. `MAP_TOOLCHAIN_PROMPT.md` card **`B1`** (renderer identity in the descriptor) assumed it was
   extending `M1`. It now sits on top of card `B0` — redo the 0.7 fixes first.
3. **Push rawtiles work to the Gitea**, not to GitHub, and verify with `git ls-remote` against
   `nas:3000` afterwards. The Gitea is authoritative; GitHub is downstream of it.
