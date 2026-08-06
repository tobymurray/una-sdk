# 2026-08-05 — rawtiles Container port, conformance, and fuzz verification

Branch: `feat/rawtiles-container` (off `upstream/main` @ `59210889`). This is the
"conformance first" step of `RAWTILES_MAP_EVALUATION.md` § 5, step 3 — the Container is the
one thing this PR ships; this bundle is its self-verification evidence, not a PR
description (see `tmp/rawtiles-container-pr-description.md` for that).

## What was ported and why

`Libs/Header|Source/RawTiles/Container.{hpp,cpp}` are ported from
`feat/rawtilesmap-tutorial` (`Container.hpp` 249 lines / `Container.cpp` 629 lines) with
the I/O layer rebuilt around `SDK::Interface::IFileSystem` per the device spike's findings
(`RAWTILES_MAP_EVALUATION.md` addendum, 2026-08-05): open at app start, single ~64 KiB
seek+reads, no whole-pack RAM residency, caller-owned tile buffers (no mmap on FatFs/eMMC).

Two backends share one validation/tile-serving implementation (`Container::readAt()`
dispatches on which is active): `openFromMemory` (borrows a caller buffer — host tests)
and `openFromFile` (owns a `unique_ptr<IFile>`, streams via seek+read). A single shared
code path was a deliberate choice over "validate once for memory, again for file" —
divergent validation logic between two backends is exactly the kind of silent-break risk
this PR's reviewer (rryles) is stated to block on.

## Hypotheses and verdicts

| # | Hypothesis | Method | Verdict |
|---|---|---|---|
| H1 | The ported+fixed Container round-trips the real sample pack byte-for-byte | Open `stanley.rawtiles` (787,008 B) via both backends, look up the documented z14 tile (x2587, y5604), read it, CRC-32 the decoded bytes | **CONFIRMED** — offset 393748, length 65536, both backends produce identical bytes, CRC `0x26E5979B` exactly matching the device-proof bundle's independently-measured value |
| H2 | Fixing the 22 extension-payload wrong-accepts (AFFN/NAME/SRCD/ATTR/duplicate-tag) makes this reader pass the full negative corpus, unlike the tutorial-branch Container (57/79) and the reference C reader (66/79) | Run all 96 vendored fixtures through the new reader; compare against a fresh build of the reference reader from the same corpus commit | **CONFIRMED, with one correction to the prompt's framing** — 95/96 negatives rejected; the 96th (`neg-33-padding-nonzero`) is *correctly accepted*, not a miss: spec § 11.2/§ 14.6 explicitly exempts a reader that never reads tile-blob alignment padding (this one doesn't — row-streaming access pattern) from having to reject it. 17/17 golden fixtures pass with all 70 § 14.5 tile hashes matching. |
| H3 | The racy CRC-table init is fixable without changing observable behaviour | Replace the `static bool tableReady` guard with a function-local `static const` (magic static); re-run the full corpus + fuzz suite | **CONFIRMED** — identical CRC results, and the construct is now the C++11-guaranteed-thread-safe form instead of a hand-rolled double-checked guard with no memory barrier |
| H4 | The new file-backend I/O layer (seek+read bookkeeping, bounded-scratch validation) doesn't introduce new memory-safety bugs, including on the truncation/short-read paths a whole-pack-resident reader could never exercise | ASan+UBSan fuzz run: 96 corpus fixtures as seeds, targeted (truncation at every byte boundary, per-bit flips across the first 300 bytes, boundary-value corruption of key u32 fields) + random mutations, driven through *both* backends including a `TruncatingFileSystem` double that shrinks the readable window between `findTile()` and `readTile()`/`readTileRows()` | **CONFIRMED** — 472,995 total mutations (272,995 targeted + 200,000 random), zero ASan/UBSan reports, zero crashes. See `fuzz/run.log`. |
| H5 | The module builds under `arm-none-eabi` with no exceptions/RTTI/heap surprises in the read path | Add `UNA_SDK_SOURCES_RAWTILES` to a shipping Example app's `GUI_SOURCES` (Stopwatch, in a throwaway `git archive` copy — this edit is NOT part of this PR's diff) and build in the `xanderhendriks/stm32cubeide:16.0` docker image used by `apps-ci.yml`, per its documented recipe | **CONFIRMED** — `Container.cpp.obj` compiles with zero warnings and links cleanly into `StopwatchGUI.elf` (265,468 B) → `Stopwatch_0.0.0-dev.uapp` (160,712 B); `arm-none-eabi-nm` on the object shows no undefined reference to `malloc`/`operator new` (no heap in the compiled Container code). See `arm-build.log`. |
| H6 | The real device numbers from `spike/rawtiles-device-proof` (a hand-rolled parser, built before this Container existed) still hold when the actual `Container` API is substituted in, on both the simulator and the physical watch | Rebased the spike's `RawTilesProbe` onto a throwaway branch (`spike/rawtiles-device-proof-container-api`, off `origin/spike/rawtiles-device-proof`, not pushed): replaced its hand-rolled header/index parsing with `openFromFile`/`tileCountAtZoom`+`tileAtIndex`/`readTile` calls, keeping the `Result` struct and `TileBlitProbe` render widget untouched. Ran it in the Linux simulator, then on the same physical watch the original spike used | **CONFIRMED, on hardware.** Sim: `open ok 787008 bytes 8ms`, `z12..14 12 tiles`, tile `x2587 y5604 at 393748`, `crc 0x26E5979B (stanley OK)`. Device (photo, `device/01-container-api-stanley-run1.png`): `scan 113ms` (exactly the addendum's documented first-touch cost), `787008` bytes, `z12-14 n=12`, tile `x2587 y5604`, `64K cold 6ms warm 7ms` (same order of magnitude as the original hand-rolled probe's 7-9 ms cold / 6-9 ms warm), **`crc 26E5979B OK`** — byte-exact tile data through the new Container's `readTile()`, on the real watch, confirmed by the same CRC the simulator and both host backends produce — and confirmed visually too (`device/02-container-api-stanley-render.png`): a correct, legible Stanley Park / Lions Gate Bridge render via the unmodified `blitCopy` path. `openMs`/`scan` numbers aren't apples-to-apples with the original probe's separate "open" stage, since `Container::openFromFile()` now does full eager validation (header + index + extensions + footer CRC) in that one call where the original only opened a file handle — a real, expected cost increase from doing strictly more work, not a regression. |

## Evidence layout

- `conformance/corpus_runner.cpp` — standalone driver (same accept/reject + § 14.5 hash
  logic as the gtest suite, built independently for this bundle) run against both readers.
- `conformance/una_sdk_reader_matrix.txt` — this reader's full 96-fixture matrix.
- `conformance/reference_reader_matrix.txt` — the upstream C reference reader's matrix,
  built fresh from the same corpus commit (`clang -std=c99 -D_POSIX_C_SOURCE=200809L -O2
  -Iinclude src/rawtiles.c tests/conformance.c -lm`), for side-by-side comparison.
- `fuzz/fuzz.cpp`, `fuzz/run.log` — the ASan/UBSan mutation fuzz driver and its output.
- `arm-build.log` (if present) — the docker ARM cross-compile log.
- `device/01-container-api-stanley-run1.png` — photo of the ported probe's on-screen
  stats readout on the physical watch (H6). `device/02-container-api-stanley-render.png` —
  the actual `blitCopy` render (stats overlay hidden) from the same run. `device/README.md`
  has the full run notes.

## Device verification (H6, post-push follow-up)

Confirmed on hardware in a follow-up session, on a throwaway branch not merged into this
one (`spike/rawtiles-device-proof-container-api`, off `origin/spike/rawtiles-device-proof`,
not pushed anywhere — its only purpose was to prove the API before this PR merges, not to
add another branch to review). See `device/README.md` for the full method, sim log, and
photo. Short version: the spike's original probe hand-parsed the wire format directly
(there was no `Container` yet — its numbers are what shaped this one); porting it to call
the real `Container` API instead reproduced the same tile, the same CRC, and the same
order-of-magnitude timings, on both the Linux simulator and the same physical watch the
original spike used. `openFromFile()`'s cost is not directly comparable to the original
probe's separate "open the handle" stage, since it now does full eager validation (header
+ index + extensions + footer CRC) in one call — see the H6 row above and `device/README.md`
for the reasoning.

## What this bundle does NOT cover

RGB565 golden fixtures and any RLE fixture (golden or negative) do not exist anywhere —
not in the vendored corpus, not self-authored here. `Tests/Host/rawtiles/RawTilesContainer_test.cpp`
covers RGB565 (trivial: `compression = None` decode is a byte-copy regardless of pixel
format) and RLE's fail-closed behaviour with hand-built local packs instead of corpus
fixtures. Upstreaming real RLE/RGB565 conformance fixtures to the rawtiles repo is a
spin-off, not this bundle's job.
