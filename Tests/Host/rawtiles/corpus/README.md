# Vendored rawtiles conformance corpus

Vendored from `github.com/tobymurray/rawtiles` at commit `38d4d269e4c66f14a839933cf94e259f93529d51`
(spec v0.6, wire format `(1, 0)`, `spec/conformance/` in that repo), 2026-08-05.

Contents: `golden/` (17 `.rawtiles` fixtures + their `.hashes` sibling files, spec § 14.5),
`negative/` (79 `.rawtiles` fixtures, spec § 11 negative corpus), `manifest.json` /
`manifest.schema.json` (the upstream corpus manifest, not currently consumed by the
gtest suite — the tests enumerate the directories directly, matching the reference
reader's own `tests/conformance.c` approach).

## Why vendored, not fetched

A network fetch of test fixtures in CI is a flake source (transient network failure
turns a green build red for reasons unrelated to the code under test) and the corpus
is small (115 files, ~628 KiB total). Vendoring trades a small amount of repo weight
for reproducibility: `Tests/Host/rawtiles/*_test.cpp` runs against exactly these bytes,
every time, offline.

## Refreshing

```
git clone https://github.com/tobymurray/rawtiles /tmp/rawtiles-spec
cd /tmp/rawtiles-spec && git log -1 --format='%H %cd' --date=short   # record the new commit
rm -rf <this dir>/golden <this dir>/negative
cp -r spec/conformance/golden spec/conformance/negative <this dir>/
cp spec/conformance/manifest.json spec/conformance/manifest.schema.json <this dir>/
```

Then update the commit hash at the top of this file and re-run
`Tests/Host/rawtiles/*_test.cpp` (`RawTilesCorpus.HasExpectedFixtureCounts` will fail
loudly if the fixture count changed — update its expected counts along with the refresh).

## Known corpus gaps (as of the vendored commit)

- **Zero RGB565 or RLE fixtures.** § 14.3 and § 14.5 claim coverage for every legal
  `pixel_format × compression` combination; no golden fixture actually exercises
  `pixel_format = RGB565` or `compression = RLE`, and no negative fixture targets an
  RLE-specific malformation (truncated encoded stream, a run whose payload overruns the
  section, etc.). `Tests/Host/rawtiles/RawTilesContainer_test.cpp`
  covers RGB565 (a straight byte-copy for `compression = None`, no corpus needed to prove
  it) and RLE's fail-closed behavior (`ReadResult::UnsupportedCompression`) with hand-built
  local fixtures instead. Real RLE/RGB565 golden fixtures belong upstream in the rawtiles
  repo; refresh this corpus once they exist there.
- **`neg-33-padding-nonzero` is access-pattern-conditional** (spec § 11.2, § 14.6): a
  reader that never reads tile-blob alignment padding — this one doesn't — is not
  obligated to reject it. `RawTilesConformance_test.cpp` asserts `Ok` for this one fixture
  and rejection for the other 78, rather than treating it as a 79/79 pass.
