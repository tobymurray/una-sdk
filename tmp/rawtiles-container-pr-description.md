# feat(rawtiles): streaming, v0.6-conformant Container + host tests

One new module (`SDK::RawTiles::Container`), one unit-test file whose test packs are
built in code (every byte reviewable), and ~10 lines of cmake. Five files, ~1,850 lines,
no binaries. Nothing links it by default; no shipping app changes.

This is the first piece of an offline-maps effort, sent smallest-first to get the
contract right before anything builds on it: the reader's public API and the `.rawtiles`
wire format are the two things that freeze once an app ships against them. Feedback on
the *direction* is as welcome as feedback on the code — everything below is written so
you can judge the direction without trusting me.

## The problem

An offline map app on this hardware has to render map tiles under these constraints
(each measured or read from the tree, not assumed):

- GUI process RAM budget is 600 K (`cmake/una-app.cmake:281`) — a whole city's tiles, or
  even one zoom level, cannot be resident. A single 256×256 tile is 64 KiB.
- There is no mmap; files are reached through `SDK::Interface::IFile`, which is
  absolute-`seek` + `read` into caller memory (`Libs/Header/SDK/Interfaces/IFileSystem.hpp:213,:229`).
- The proven render path is `LCD::blitCopy` consuming contiguous rows of the display's
  native `ABGR2222` format — handing it blit-ready bytes means zero decode work and zero
  decode RAM.
- Measured on the watch ([device spike](https://github.com/tobymurray/una-sdk/tree/spike/rawtiles-device-proof)):
  a 64 KiB tile read through `IFileSystem` costs 7–9 ms cold, 6–9 ms warm, byte-exact.
  That number is what makes the whole design work: tiles can be read *on pan*, so
  nothing needs to be cached beyond the visible viewport.

So the storage format this hardware wants is: pre-rasterized tiles, already in display
format, in one seekable file with an index — because then a map frame is
`findTile → seek+read → blitCopy`, bounded by one tile buffer.

## Alternatives considered (and why they don't fit)

| Format | Why not on this device |
|---|---|
| Directory of PNG/JPEG tiles (classic slippy-map cache) | Needs an image decoder in the GUI process (code + decode buffers + CPU on every pan); thousands of small files on FatFs; no pack identity, integrity check, or metadata |
| MBTiles | It's a SQLite database — a SQLite port on the watch is wildly out of proportion for read-only tile lookup |
| PMTiles | Single-file and well designed, but for HTTP range requests: compressed directories, Hilbert-clustered entries, and the tiles inside are still PNG/WebP/MVT — the decoder problem again, plus directory decompression |
| Vector tiles (MVT & friends) | Needs a runtime rasterizer, styling, fonts — a different order of magnitude of scope and RAM |
| Ad-hoc raw dumps (e.g. one raw file per tile) | This is what rawtiles *is*, formalized: an index for O(log n) lookup, CRC-32 integrity, pack identity/supersedence, and attribution metadata (OSM's license requires attribution to travel with the data) |

The common thread: every existing container assumes a consumer that can afford to
decode. This render path doesn't want to decode — it wants blit-ready bytes. That's the
niche `.rawtiles` fills, and it's also the format's honest cost: raw pixels are larger
on flash than PNG (64 KiB vs typically 10–40 KiB per tile). The mitigations are bounded
packs (bbox × zoom range — e.g. 10×10 km at z15 ≈ 2.9 MiB, fine for eMMC) and the
spec's pixel-level RLE compression as a later flash-capacity feature (deliberately not
decoded yet — see open question 2).

## How a user makes a pack

[`slippypack`](https://github.com/tobymurray/slippypack) — a Rust CLI (with the format
writer, ABGR2222 quantiser, and projection math in a reusable core crate):

```
slippypack make --source <tile-server URL template> --bbox ... --zooms ... --out map.rawtiles
```

It fetches slippy tiles (rate-limited), quantises to ABGR2222, and writes a
spec-conformant pack atomically. Honest status: in flight — URL-template and synthetic
sources work today (~280 tests); directory/MBTiles/PMTiles *inputs* and a browser PWA
front-end are planned. Note where MBTiles/PMTiles land in this design: as build-time
*sources* on the desktop, not on-device formats.

Deployment today is copying the pack into the app's sandbox over USB-MSC. Discovery
conventions are deliberately not in this PR (open question 4).

## What's in the PR

- **`Libs/Header|Source/RawTiles/Container.{hpp,cpp}`** — two open paths, one validation
  and tile-serving implementation behind them:
  - `openFromMemory(data, size)` borrows a caller buffer (host tests, fixtures).
  - `openFromFile(IFileSystem&, path)` owns a `unique_ptr<IFile>` and streams via
    seek+read. Validation runs eagerly at open in bounded scratch memory; the file
    backend never holds the pack resident.
  - `findTile(z, x, y)` is index-lookup only (binary search over the zoom directory, no
    I/O); `readTile(info, dst, dstSize)` / `readTileRows(...)` do one seek+read into a
    caller-owned buffer. No heap in the read path.
  - Every enforced spec § 11 rule is cited by the `OpenResult` code that fires it
    (`describeResult()` gives the human-readable string). Extension payloads are
    validated in streamed 128-byte chunks, not "read the payload into a buffer sized by
    what the file claims."
  - Spec currency is v0.6, scoped deliberately: `pixel_format = RGB565` decodes today
    (decode of an uncompressed tile is a byte-copy regardless of pixel width);
    `compression = RLE` is *recognised* as legal — a structurally valid RLE entry opens
    fine — but fails closed at read time with `ReadResult::UnsupportedCompression`.
- **`Tests/Host/rawtiles/RawTilesContainer_test.cpp`** — unit tests whose packs are
  hand-built in code: both backends agreeing byte-for-byte, truncation/short-read on the
  file backend, RGB565, RLE fail-closed, CRC computed independently of the code under
  test.
- **cmake** — `UNA_SDK_SOURCES_RAWTILES`, the same opt-in pattern as
  `UNA_SDK_SOURCES_TRACKMAP`; two lines in `Tests/Host/CMakeLists.txt`.

## What's deliberately NOT in it

- **The spec's conformance corpus** (96 binary fixtures, ~628 KiB). The reader is
  verified against all of it — results below — but vendoring binary blobs into this diff
  would have tripled the file count while adding nothing reviewable. Whether it belongs
  in-tree is open question 1; the suite exists and is a small follow-up PR if wanted.
- No writer/encoder (that's [`slippypack`](https://github.com/tobymurray/slippypack)'s
  job), no tile widget, no tutorial, no path/discovery convention, no RLE decoder, no
  `Docs/` page yet (happy to add one; at this stage the module doc-comment carries it).

## Verification

Evidence bundle (conformance matrices, fuzz driver + log, ARM build log, device photos)
lives on the fork so this PR stays lean:
[`Docs/Investigations/2026-08-05-rawtiles-container/` on `tmp/rawtiles-container-pr-description`](https://github.com/tobymurray/una-sdk/tree/tmp/rawtiles-container-pr-description/Docs/Investigations/2026-08-05-rawtiles-container).
That branch is this PR's head plus one commit holding the evidence *and* the out-of-tree
conformance suite, so everything below is re-runnable against exactly the code under
review.

- **Conformance: 96/96 fixtures handled correctly** (spec corpus @
  [`tobymurray/rawtiles@38d4d26`](https://github.com/tobymurray/rawtiles)) — 17/17
  golden with all 70 § 14.5 tile hashes matching; 78/79 negatives rejected, and the 79th
  (`neg-33-padding-nonzero`) *correctly accepted*: § 11.2/§ 14.6 exempts a reader that
  never reads tile-blob alignment padding from rejecting non-zero padding, and this
  reader never reads it. The C reference reader built from the same corpus commit
  wrongly accepts 13 fixtures (payload rules its README discloses as unimplemented).
- **Fuzz: 472,995 ASan+UBSan mutations, zero reports, zero crashes** — 272,995 targeted
  (truncation at every byte boundary, per-bit flips across the first 300 bytes, boundary
  values on the u32 fields) plus 200,000 random, through *both* backends, including a
  `TruncatingFileSystem` double that shrinks the file between `findTile()` and
  `readTile()` — the short-read surface the streaming backend introduces.
- **Host tests: 286/286 green on the exact PR head** (9 new) in a clean-room container
  (`git archive` → `ubuntu:24.04` → cmake/ctest), suites confirmed present via
  `--gtest_list_tests`. The evidence branch, which adds the corpus suite back, runs
  383/383 the same way.
- **ARM**: `Container.cpp` compiles warning-free under `arm-none-eabi` in the `apps-ci`
  image and links into a real example-app `.elf`/`.uapp`; `nm` on the object shows no
  reference to `malloc`/`operator new`.
- **On the watch**: the device spike's probe, re-pointed at this Container's actual API
  (`openFromFile` → `findTile` → `readTile`), reproduces the spike's numbers on the same
  hardware: a 787,008-byte, 12-tile pack opens and fully validates; 64 KiB tile read
  6 ms cold / 7 ms warm; CRC byte-exact (`0x26E5979B`); the tile renders correctly
  through the unmodified `blitCopy` path. Photos + method in the bundle's `device/`.

## Trade-offs made, both directions

- **Eager, full validation at open** (vs. the spec's lazy profile): `open() == Ok` means
  every rule has fired — nothing fails later except buffer-size mismatch and the
  deliberate `UnsupportedCompression`. The cost is an O(index + extensions) streamed
  pass at open; on hardware that's low-hundreds-of-ms *including* the ~113 ms
  first-filesystem-touch cost every app pays once. A pack with a very large tile count
  pays proportionally more; lazy validation is an additive change if that ever matters.
- **One shared validation path behind two backends**: every read goes through a backend
  dispatch — negligible against real I/O, and it makes it impossible for the memory- and
  file-backed paths to drift in what they accept (divergence there is the
  "ships-then-silently-breaks" failure mode).
- **`readTileRows()` exists before any consumer needs it**: it's the row-range shape
  that RLE decode (which can suspend mid-run per § 9.11) and partial redraw both want,
  added now so neither needs an API break later. Honest risk: API surface designed
  slightly ahead of its use.
- **RGB565 decode ships without a corpus golden fixture** — none exist upstream (§ 14.3
  promises them; that's a spec defect, flagged in the spin-offs). Covered by in-code
  fixtures here; the decode is a byte-copy, so the residual risk is small but non-zero.
- **The unit tests alone don't re-prove conformance** — that currently rides the
  out-of-tree corpus run. If you want the regression net in-tree, that's open
  question 1 and I'll send the follow-up immediately.

## The Provisional-spec question, head-on

The wire format is v0.6 `(1, 0)`, status Provisional, and the spec, the corpus, the
reference reader, and the pack writer all live in my personal repos — same author as
this PR. The spec's own v1.0 gate ("a second independent consumer") is unsatisfiable in
spirit while every consumer shares an author. I don't think that blocks this PR, for a
narrower reason than the spec's maturity: what freezes here is the *API*, and the API
doesn't encode wire-format details a spec-minor bump would change — new
`pixel_format`/`compression` enum values slot in without a signature change, exactly as
RLE already demonstrates (legal to open today, decodable later, no break). A
wire-format-*major* bump invalidates old packs by the spec's own rules; no API survives
that regardless. So the claim is "freeze the API, not the wire format" — but if you'd
rather see the spec in a neutral home (or mirrored in-tree) before an `SDK::` module
references it, I'd rather hear that now than after more pieces land.

## Open questions (genuinely open — each with my recommendation)

1. **Should the conformance corpus + suite be vendored in-tree?** It's 96 binary
   fixtures (~628 KiB) + a corpus-driven gtest suite + a test-local SHA-256, all sitting
   ready on the evidence branch. In-tree buys a continuous conformance regression net in
   CI; out-of-tree keeps the repo free of binary blobs that can't be reviewed, only
   regenerated (the corpus generators live in the spec repo). Recommend: in-tree as an
   immediate follow-up PR if you're taking the module — kept out of *this* diff so the
   reviewable part stays visible.
2. **RLE decoder: follow-up, or fold in?** Recommend follow-up. Zero RLE fixtures exist
   anywhere to test a decoder against, so writing one now means also writing the first
   encoder and grading my own homework in one PR. The measured read speed demoted RLE
   from "RAM enabler" to "fits-more-packs-in-flash", so nothing needs it yet, and the
   API already has its landing slot (`TileInfo.compression`, `readTileRows`,
   `UnsupportedCompression`).
3. **Is commit-pinning against a personal external spec repo acceptable?** Recommend
   yes for now (this PR names the exact spec commit it implements). If UNA wants a
   stronger arrangement — spec text mirrored in-tree, or the spec repo moved to a
   neutral org — that decision shapes the writer/tutorial follow-ups more than this
   one, and I'm happy to do either.
4. **Cross-app pack sharing** (surfacing, not answering): on hardware each app sees only
   its own sandbox — no absolute volume path resolves — so two map apps today would
   each carry their own copy of a pack. Is per-app duplication acceptable, or would UNA
   ever expose a shared read-only location into app sandboxes? This PR doesn't depend
   on the answer (`openFromFile` takes whatever the caller can reach), but the answer
   decides the eventual discovery convention, and kernel-side work has the longest lead
   time.

## Spin-offs (noted during this work, none folded in)

| Finding | Where it belongs |
|---|---|
| § 14.3/§ 14.5 claim RGB565/RLE golden fixtures that don't exist; no RLE encoder exists anywhere | `rawtiles` spec repo |
| § 9.11 doesn't say whether the encoded RLE stream must be consumed exactly; the reference reader rejects trailing bytes, so two conforming readers can disagree | `rawtiles` spec repo |
| Apps built from current `main` fault instantly and unlogged on v2-firmware watches (`KERNEL_INTERFACE_VERSION` 2→3) — cost several hours during the device spike | separate branch (device-wide, not rawtiles-specific) |
| `Docs/deploy.md` should warn that USB-MSC writes concurrent with BLE sync corrupt files (two uncoordinated writers on one exFAT) | separate `docs/` branch |
