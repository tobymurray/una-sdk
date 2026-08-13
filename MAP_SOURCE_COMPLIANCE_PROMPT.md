# Prompt: stop slippypack recommending — and accepting — sources whose terms forbid its only use case

You are landing cards **`A1`** and **`A2`** from `MAP_TOOLCHAIN_PROMPT.md`: the board's
designated first step. Read that file's § 0 (ground rules), § 2 (settled), and § 5 (premise
traps) before starting; they are not repeated here except where this prompt sharpens them.

The problem in one sentence: **slippypack's own documentation recommends MapTiler Cloud as
its headline first-run source, and its URL-template input accepts the OSM tile CDN, and both
of those parties' terms prohibit building offline tile archives — which is the only thing
slippypack does.** `A1` is the prose half, `A2` is the code half. A tool that prints a
warning and then does the prohibited thing anyway has not fixed this, which is why `A2`
exists as a separate card.

**Two branches, not one** (§ 0's one-reason-to-merge rule). `A1` is documentation and help
text; `A2` is a refusal in code with tests. They are independent — do either first.

## Where the work lands

`slippypack`, local clone `~/git/rust/slippypack`. **Fetch first:** the clone sits on
`main` @ `1f9132d` (spec-0.5-era) and everything below is on `origin/map-delivery-workflow`
@ `b8d5464`. Branch off that.

Its only remote is `origin` (GitHub), which is a **mirror of the authoritative Gitea**. Add
the Gitea and push there too, verifying with
`git ls-remote http://nas:3000/toby/slippypack.git` — HTTP on 3000, not SSH. A branch that
exists only on GitHub is not safely stored; see `Docs/External/rawtiles/README.md` for the
branch already lost that way.

## The evidence, verified 2026-08-12

Citations below are to this repo's durable proxy copies under
`Docs/External/slippypack/docs/`; the same files are at the repo root on `slippypack`'s
branch.

**MapTiler Cloud PROHIBITS** — `MAP_COMPLIANCE_APPENDIX.md` § 2.2 (`:78`). Verbatim: export
of map content for use outside the Service is prohibited; batch or bulk download of tiles is
prohibited; storing or redistributing map content from a cache or static image instead of
calling the APIs is prohibited. The one carve-out — "**End-user Device Caching**… a temporary
personal cache (browser cache, mobile app cache, etc.) for use by a single end-user only" —
does not reach a `.rawtiles` pack copied to a watch, because the pack is neither temporary
nor inside the Service. Note § 2.3: **MapTiler Server / Data is a different product and a
licensed offline route that PERMITS.** The prohibition is on the Cloud API hosts, not on the
company. `A2`'s denylist must respect that distinction or it will refuse a user who has
legitimately paid for the offline license.

**The OSM tile CDN PROHIBITS** — § 2.1 (`:30`). The policy defines bulk downloading as *any*
pre-emptive fetching beyond what a user is actively viewing, and names "Building tile
archives (e.g. `.zip`, `.mbtiles`) for later distribution" and "Download city/country for
offline use" explicitly. This is not a rate-limit question: the prohibition is on purpose,
so the existing 2 req/s special-case for OSM hosts limits *load* and cures nothing.

**Stadia is client-side only and blocked pending `G2`** — § 2.4 (`:138`). It cannot appear
at the top of a source list a CLI user reads.

## `A1` — the defaults in prose

Branch `fix/source-compliance-defaults`. Logged spin-off **S2**.

`PLAN.md`'s first-run flow (`:452`) offers sources "in order of expected friction" with
**MapTiler first** — carrying the free-tier pitch "100K tile requests/month — comfortably
covers a small country at default zooms" — and **Stadia second**. Replace that ordering with
`MAP_TOOLCHAIN_PROMPT.md` card `F1`'s, which ranks **permission first**: Protomaps basemap,
then OpenFreeMap's planet downloads, then Geofabrik/BBBike raw OSM, then Stadia (listed, not
recommended, pending `G2`), then Thunderforest at Small Business or above as the documented
priced route.

**The card understates this one, and you should scope it before writing code.** MapTiler is
not confined to that list; `PLAN.md` leans on it structurally:

| `PLAN.md` | what names MapTiler Cloud |
|---|---|
| `:86`, `:249` | the PWA's tile-source row and the architecture diagram |
| `:395` | the worked `slippypack.toml` example — `url = "https://api.maptiler.com/…"`, `auth_query = "key=YOUR_MAPTILER_KEY"` |
| `:422` | built-in attribution defaults, keyed by source kind |
| `:434` | the rate-limit prose, which names MapTiler and Stadia as the paid-quota case for `--rate-per-sec` |
| `:460` | the quota-warning threshold, whose default *is* the MapTiler free tier |
| `:472` | the README callout: "to try the real flow, get a MapTiler key" |
| `:512` | the offline error message, verbatim, names MapTiler |
| `:704`, `:714` | the PWA source picker and its `fetch()` path |
| `:777` | the business model — infrastructure cost is the user's, "in the form of a MapTiler/Stadia account" |

Decide and record how far this branch goes. The defensible minimum is: no permitted-source
claim anywhere in the repo rests on a source the appendix does not permit, the worked config
example uses a permitted source, and the cost model no longer assumes a prohibited account.
If you split the deeper edits (`:422` attribution defaults, `:460` quota threshold) onto a
follow-up, say so in the commit and name the follow-up.

Also add the compliance note to `make --source`'s help — see `A2`, since the two texts
should agree.

**Done when:** no source recommendation in the repo contradicts the appendix, and
`slippypack make --source --help` says which hosts are refused and why.

## `A2` — the refusal in code

Branch `fix/refuse-prohibited-hosts`.

**The choke point is `UrlTemplate::parse`**, `crates/slippypack-cli/src/sources/url_template.rs`.
It validates the scheme (`http://` or `https://`) and the presence of `{z}`, `{x}`, `{y}`,
and nothing else — there is no host policy anywhere in the crate. Both entry points that
accept a user source string (`run_make` and `debug uuid`, in `main.rs`) construct through it,
so one check covers the CLI's URL-template surface.

**Reuse what is already there rather than writing new URL handling.**
`crates/slippypack-cli/src/sources/rate_limit.rs` has `extract_host()` (strips `user:pass@`
and `:port`, lowercases; explicitly does *not* handle IPv6 literals) and `is_osm_host()`
(exact match plus a `.tile.openstreetmap.org` suffix arm, with a test asserting
`tile.openstreetmap.org.evil.example` is **not** a match). Build the denylist the same way —
suffix-anchored, never `contains()` — and route it through `extract_host()` so userinfo and
port tricks do not walk past it.

Errors: `UrlTemplateError` is `#[non_exhaustive]` with a hand-written `Display`. Add a
variant carrying the host and the policy URL, and make the message something a non-lawyer
acts on — what was refused, whose policy, and which permitted source to use instead. Point at
`F1`'s ranking, not at a bare "see the docs".

### Decisions that are yours

1. **The MapTiler surface.** `api.maptiler.com` is the Cloud API. Blanket-refusing
   `*.maptiler.com` also refuses a self-hosted MapTiler Server, which § 2.3 says **PERMITS**.
   Enumerate the Cloud hostnames and refuse those, or refuse the wildcard and document the
   escape — but do not refuse the licensed product by accident.
2. **Whether an override exists at all.** The card's default recommendation is **no**: the
   denylist is host-specific, so a Thunderforest subscriber or someone running their own
   renderer is unaffected, and there is no legitimate case left to serve.
   `--rate-per-sec` is *not* a precedent — it exists because rate is a quota question and the
   user knows their quota; a prohibition is not. Record the decision either way.
3. **What happens to the OSM rate-limit entry.** Once the host is refused, `RatePerSec::OSM`
   and `is_osm_host`'s rate role are unreachable for fetching. Delete, or keep with a comment
   saying why it survives. Either is fine; leaving it undiscussed is not.
4. **Whether the check belongs in `slippypack-core` instead.** The PWA fetches through the
   browser (`PLAN.md:714`) and its picker offers MapTiler (`:704`), so a check living in
   `slippypack-cli` leaves that surface open. Decide whether to place it once in core now or
   record the PWA gap as explicitly deferred.

**Done when:** a build against either prohibited host fails — before any request is issued —
with a message a non-lawyer understands, and the failure is asserted at the CLI level
(`crates/slippypack-cli/tests/`), not only in the unit tests beside the parser.

**Then mutate it** (§ 0: a green suite is not evidence). Swap the suffix anchor for
`contains()`, drop the subdomain arm, and lowercase-normalise one input less — each should
break something. If the suite stays green, the coverage is decorative.

## Traps

- **`http://` is accepted on purpose.** It is what makes a localhost renderer work today
  (`parse_accepts_http` pins it). Do not tighten the scheme check while you are in this file,
  and do not implement the refusal as a scheme rule.
- **Do not refuse loopback.** `127.0.0.1`, `::1` and `localhost` are the compliant path —
  your own renderer. Card `D1` wants them *less* restricted, not more.
- **`extract_host()` returns `None` for IPv6 literals.** Know whether your refusal fails open
  on `http://[::1]:8080/…`-shaped input, and say which way you chose.
- **Do not fetch anything to test this.** A refusal is testable without a network, and § 0
  prohibits the "just a few tiles to check" experiment. `--source synthetic` and the committed
  fixtures are the legitimate inputs.
- **Do not touch the Athens pack or its provenance note** — that is card `A3`, in `una-sdk`,
  on its own branch.
- The appendix quotes are the authority for the error text, but **you are not a lawyer**
  (§ 0). Quote what the terms say. Do not write a conclusion about what would be fine.
