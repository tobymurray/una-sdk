# Prompt: Build AthensRun — a fully-fledged PoC running app with a Garmin-style live map

You are building the proof-of-concept that the whole rawtiles effort now hangs on: a
**running-activity app for the Una watch whose map data screen works the way Garmin's
does** — a real basemap of the area around **Athens, Ontario**, auto-centered on the
runner, with the **GPS trace of the current activity drawn overtop**, recording a normal
FIT activity the whole time. This is a product, not a demo: the deliverable is an app a
runner in Athens could actually use for a 10 km loop.

Strategic context (do not re-litigate): the spec adequacy audit demoted rawtiles from
"proposed SDK interface" to "prove it as an app first." So this PoC deliberately does
**not** touch `Libs/SDK`, `cmake/una-sdk.cmake`, or anything upstream-shaped — the
reader is **vendored into the app**, freezing nothing, so wire-format and API mistakes
stay cheap while the spec is still v0.x. What the PoC must produce, besides a working
app, is the **app-layer evidence no audit could**: viewport math, cache policy, redraw
strategy, trace rendering — the parts of the map story that have never been attempted.

---

## 0. Ground rules (hard constraints)

- **Never post anything to GitHub** — no PRs, comments, or issues, on any repo. `gh` is
  read-only. Push branches to `origin` (the fork) only; plain push, never force (other
  sessions share branches).
- Branch: `poc/athensrun` off `upstream/main` (== `origin/main`). One branch, one
  reason. Real findings outside scope go in a spin-off list with their target branch.
- Commits terse, mostly *why*; author `toby.murray@protonmail.com`.
- House evidence convention: anything settled by experiment goes in
  `Docs/Investigations/<date>-<slug>/` (hypothesis / method / log / screenshots /
  verdict, failures included). Cite `file:line` for claims about existing code.
- **Verify, don't trust this prompt.** Every fact below was checked on 2026-08-06;
  re-verify anything load-bearing.

## 1. What already exists (use it, don't rebuild it)

| Thing | Where | Notes |
|---|---|---|
| `SDK::RawTiles::Container` — the v0.6-conformant streaming reader | `origin/feat/rawtiles-container` → `Libs/Header/SDK/RawTiles/Container.hpp` + `Libs/Source/RawTiles/Container.cpp` | **Vendor both files into the app's own `Libs/`** (rename the namespace if you like). Do NOT add `UNA_SDK_SOURCES_RAWTILES` to any shared cmake — app-private is the point. 96/96 conformance, fuzz-clean, device-proven (6–9 ms per 64 KiB tile read) |
| The Athens map pack | `Docs/Investigations/2026-08-06-athens-pack/athens.rawtiles` (untracked; README beside it has the exact regeneration command + stats) | ~10 × 8 km around 44.6259 N −75.9523 W, z12–16, 256 px ABGR2222 tiles, `compression = None`, ATTR = "© OpenStreetMap contributors". Built by slippypack from OSM tiles |
| slippypack (pack writer, Rust) | `github.com/tobymurray/slippypack`, branch `rle-pixel-level` (canonical pixel-level RLE landed there) | For regenerating packs or building variants (RLE, different bbox/zooms) |
| rawtiles spec 0.7 + conformance corpus | `github.com/tobymurray/rawtiles`, branch `spec-0.7-adequacy-fixes` | The corpus now HAS RLE golden fixtures (`golden-rle-abgr`, `golden-rle-rgb565`) — a C++ RLE decoder is finally testable against neutral fixtures (experiment E3 below) |
| A complete running app to copy | `Examples/Apps/Running` | Service + GUI + GPS + FIT recording + settings. AthensRun = a copy of it plus a map screen. Check how it moves GPS fixes service→GUI before designing anything (its `Commands.hpp`; note the known double-width-IPC-field spin-off — don't fix it here) |
| Device measurements (do not re-derive) | evaluation + spike bundles | 64 KiB tile read 6–9 ms; first FS touch ≈ 113 ms; open+validate a 787 KB pack ≈ 27 ms (27.8 MiB/s ⇒ a ~40 MB pack CRCs in ~1.4 s — decide eager vs. § 10 caller-asserted trust and document); `LCD::blitCopy(ptr, Bitmap::ABGR2222, …)` renders correctly at negative-X/-Y overhang on hardware |
| GUI RAM budget | `cmake/una-app.cmake:281` | `RAM_LENGTH` 600 K for the GUI process. Older docs say "256 KB app RAM" — that's a stale Mermaid label, ignore it |

Device gotchas (each cost real time once; all in memory/investigation notes):
apps see a **sandbox-relative** filesystem — no `N:/` path resolves, so the pack lives
at e.g. `maps/athens.rawtiles` relative to the app's own dir; **disable BLE sync before
any USB-MSC copy** (two writers corrupt the exFAT) and **byte-verify the pack after
copying, before unmount**; apps built from current `main` fault instantly on v2-firmware
watches — if the test watch is still v2, pin `KERNEL_INTERFACE_VERSION (2)` on a
throwaway branch, never on `poc/athensrun`.

## 2. The product bar ("the way Garmin does")

A Garmin map data screen, translated to this hardware:

1. **It's a data screen inside a real activity.** Start/pause/resume/stop and FIT
   recording work exactly as Running does; the map is one of the swipeable screens, not
   a separate mode. Pace/distance/time screens stay.
2. **Basemap, north-up, auto-follow.** Map centered on the current GPS position,
   north-up (blitCopy can't rotate; track-up is out of scope — note it, don't fake it).
   Viewport = 240 × 240 window into the z-level's world-pixel plane; with 256 px tiles
   that's at most a 2 × 2 tile mosaic per frame, each tile blitted with its partial
   rect and negative offsets (the proven path).
3. **GPS trace overtop.** The current activity's track drawn as a polyline over the
   basemap — Garmin's breadcrumb. Decimate: append a point only when ≥ N metres from
   the last kept point (pick N per zoom, ~10 m at z16); a fixed ring buffer
   (e.g. 1024 × two i32 world-pixel coords = 8 KB) bounds RAM for arbitrarily long
   runs; accept trace thinning on overflow (drop every 2nd point, double N — Garmin
   does the equivalent). Draw with the LCD line primitives / TouchGFX painter over the
   blitted tiles, current position as a distinct marker (dot; direction arrow only if a
   heading source proves trivially available).
4. **Zoom.** Buttons cycle z12–16. Trace re-projects (store points as lat/lon µdeg or
   z16 world pixels and shift, your call — justify it).
5. **Honest states.** No GPS fix yet (say so, show map at last/default center — the
   pack's bbox center is the sane default); runner leaves pack coverage (blank tiles +
   "off map" hint, absent-tile lookups are cheap); pack missing/corrupt (screen says
   so; app still records the activity — the map must never take down the run).
6. **Pan override (stretch, do only if the rest lands).** Swipe to pan, auto-recenter
   after a timeout or button. If cut, say so in the report.

**Coordinate math** (nothing like this exists anywhere in the repo — this is the PoC's
genuinely new ground): lat/lon → z-level world pixels via WebMercator
(`x = (lon+180)/360 · 2^z · 256`, `y = (1 − asinh(tan φ)/π)/2 · 2^z · 256`). Do it in
`double` or i64 fixed-point — **float resolves only ~0.36 px at z16**, the evaluation
measured it; a float pipeline makes the trace wobble. Tile = world-pixel ≫ 8; sub-tile
offset = low 8 bits. Write host tests for the math (corner cases: tile boundaries,
negative sub-tile offsets, the pack's own bbox corners) — it's pure code, test it pure.

## 3. Architecture guidance (starting point, not a straitjacket)

- App: `Examples/Apps/AthensRun`, copied from Running (its `app.mk` is committed —
  follow the same layout; msvs project files updated per the CI check script).
- GUI process owns the map stack: `Container` (vendored) → `TileCache` → `MapView`
  widget → `TraceOverlay`. Service process stays Running's: sensors, FIT, GPS fan-out.
- **TileCache:** open the pack at app start (that's when the ~113 ms first-touch cost
  is invisible). Decide slots against the 600 K budget *after* measuring the app's
  existing RAM use, not before; 3 × 64 KiB slots is a defensible starting point
  (covers a 2 × 2 mosaic with one eviction per pan-crossing at 6–9 ms per fill — the
  spike says that's imperceptible). Simple LRU; no heap in steady state (fixed arrays).
- **Redraw:** re-blit on new fix / zoom change / pan, not on a timer. A fix arrives
  ~1 Hz; blit of 4 partial tiles + polyline is well under a frame.
- **Trace over blit:** blitCopy writes the framebuffer, then the polyline draws over
  it in the same invalidation pass. Verify draw order in the TouchGFX pipeline early —
  if the widget z-order fights you, one custom widget that blits then draws lines in
  its own `draw()` is the escape hatch (the spike's probe widget is the precedent).
- Pack discovery: enumerate the app sandbox's `maps/` dir, open the first
  `.rawtiles` (multi-pack composition is explicitly out of scope).

## 4. Experiments charter (the "various attempts, side by side" mandate)

Each is a `Docs/Investigations/` bundle; each changes a real decision:

- **E1 — cache/redraw sweep (sim, then device):** 2 vs 3 vs 6 tile slots; re-blit-all
  vs dirty-rect. Metric: worst-case pan-crossing latency + RAM. Decides the shipped
  config.
- **E2 — open policy on a ~40 MB pack (device):** eager CRC (~1.4 s predicted at
  27.8 MiB/s) vs § 10 caller-asserted trust after a one-time post-copy verify. Measure
  actual open time; decides the trust story and validates E3's extrapolation curve.
- **E3 — RLE decode in the vendored Container (host + device):** implement
  `readTileRows` RLE decode (spec § 9.11 v0.7, exact-consumption; test against the
  corpus's `golden-rle-*` fixtures and § 14.8 vectors), produce the RLE twin of the
  Athens pack (slippypack `rle-pixel-level` branch — either refetch or repack), then
  A/B: flash footprint (expect ~3×), tile-fill latency (read shrinks ~3×, decode adds
  CPU — the audit *predicted* a wash; measure it), pan feel. This is the first
  RLE-on-device data anywhere.
- **E4 — trace density limits (sim):** how many polyline segments before frame time
  suffers; decides the decimation constants.

Report what ran and what didn't; a cut experiment with a reason beats a faked one.

## 5. Verification (in order; do not skip to hardware)

1. **Host tests** for the coordinate math and (if E3 runs) the RLE decoder — in the
   app's own test tree, docker recipe as usual (`git archive` + coreJSON copy +
   ubuntu:24.04 cmake/ctest).
2. **Simulator:** the Linux sim build recipe is in memory/notes (docker amd64 +
   SDL2/ruby-nokogiri; `app.mk` committed per app). Feed a **simulated run**: a GPX/CSV
   route through Athens (down Main St, around Centennial Park, out Wiltse Creek Rd —
   any plausible ~5 km loop inside the pack bbox) replayed through however Running's
   sim feeds GPS (find it: the simulator sensor mocks; `feat/gps-quality-logging` may
   carry a feed). Evidence: screenshots + a GIF of the map following the trace.
3. **Device:** kernel-version pin decision first (above); BLE off; copy
   `athens.rawtiles` + `.uapp`; byte-verify both; then an actual walk/run — or, if the
   watch stays on a desk in a different city, the sim route replayed on-device if a
   feed exists, else GPS-static behavior verified and the moving-map claim scoped to
   the simulator, stated honestly. Photos; latency numbers via `ISystem::getTimeMs`
   logging as the spike did.

**What "done" looks like:** a pushed `poc/athensrun` branch; the app recording a real
FIT activity while its map screen follows a moving position through Athens with the
trace overtop, on the simulator with photo/GIF evidence and on hardware to whatever
depth the watch's location allows; E1–E4 bundles (or honest cuts); an addendum note in
`RAWTILES_MAP_EVALUATION.md`'s ordering (this is step 5's tutorial slot, executed as a
PoC instead); and a spin-off list. The next conversation after this one should be able
to decide "productize / iterate / re-scope" from your evidence alone.
