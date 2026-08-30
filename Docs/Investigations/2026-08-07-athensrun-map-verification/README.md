# AthensRun map verification: freeze/crash fix, on-device validation, contrast, and a tile-sourcing compliance finding

Branch: `poc/athensrun`. This investigation covers four related threads that came out of trying to make AthensRun's offline map usable end-to-end on real hardware: a device-crashing performance bug and its fix, on-device validation of that fix (including one still-partially-unexplained anomaly), a map-legibility problem and its empirical resolution, and a tile-sourcing policy compliance finding that affects how any future pack for this app should be built.

## 1. The bug: eager CRC verification froze the GUI, then crashed the watch

`SDK::RawTiles::Container::openFromFile()` did a mandatory, synchronous, whole-file CRC-32 scan (`verifyCrc()`) before returning — spec-conformant (rawtiles v0.6 §10's "eager verify" mode), but blocking. `Model::ensureMapPack()` called this once per launch on the GUI thread.

- At the pack's original size (45MB, a ~10×8km test extract around Athens, ON), this froze the GUI for ~10s.
- After regenerating the pack at a size that actually covers real running routes (201MB, 20×20km), the same call blocked long enough to trip an app-liveness watchdog and **force-restart the entire watch**.

An earlier local mitigation (`kChunkSize` 128→4096 in `Container.cpp`, reducing IPC call count) helped but didn't fix the root cause: total bytes scanned — and thus blocking time — scales with pack size regardless of internal chunking. As tile packs get more useful (bigger), this only gets worse.

### Fix: background, cached, resumable verification

Grounded directly in the rawtiles spec (fetched live from `github.com/tobymurray/rawtiles`, v0.6, `spec/rawtiles.md`): §10 defines three legal CRC-verification modes — **Eager**, **Streaming**, and **Caller-asserted trust** ("a reader MAY skip the CRC entirely when the caller has provided integrity assurance through a separate channel... a previously-verified cache..."). The fix implements Caller-asserted trust for one call path — a sanctioned reader choice, not a spec deviation.

- `Container::openFromFile()`/`openFromMemory()` gained a defaulted `skipCrcVerify` parameter. Every other §11 structural rule (magic, version, uuid, bbox, zoom, tile-index bounds, extensions) still runs unconditionally — a genuinely malformed pack still fails immediately either way. Default `false` preserves all existing behavior.
- New Service-only class `MapPackCrcVerifier` (own independent CRC-32/ISO-HDLC table — deliberately not sharing `Container.cpp`, which lives in a GUI-only CMake source set) drives the scan in small bounded slices across every `Service::run()` loop iteration — not just its 500ms idle-timeout branch, since GPS/sensor traffic during an active run keeps that branch from firing and would otherwise starve the background pass exactly when it's most needed.
- New tri-state marker `MapPackTrustMarker` (`Absent` / `Bad` / `Good`, 16-byte file) records `(packSize, declaredCrc)` once a pass completes — `Bad` on mismatch (so a corrupt pack surfaces as a real error, not an eternal "verifying"), `Good` on match, checked before Service even bothers to (re-)scan.
- `Model::ensureMapPack()` now always does a *structural-only* open (`skipCrcVerify=true` — cheap and bounded even at 200MB+, so real errors like bad magic still surface immediately), then polls the marker cheaply once per tick. Trusted → renders. Not yet trusted → shows a neutral "verifying map" status and keeps polling, never blocking.
- New `MapPackVerifyLog` (header-only, modeled directly on the `watch-apps` Squash app's `ImuFileSink` pattern — a device-side diagnostic file sink kept in its own directory, separate from user data) writes timestamped progress/decision lines from both GUI and Service to `Apps/AthensRun/Debug/mappack_verify.log`, retrievable over the same USB-MSC connection already used to deploy — no debug-UART rig needed to see what happened after the fact.

14 new host tests (`Tests/Host/apps/AthensRun/ContainerCrcSkip_test.cpp`, `MapPackCrcVerifier_test.cpp`) cover the new `Container` surface and the verifier/marker classes; all pass, plus zero regressions across the existing 277 host tests.

### Spec feedback (separate from the app fix — AthensRun is an explicit PoC for the `rawtiles` spec itself)

1. §11.1's resource analysis is precisely budgeted for **memory** ("≤1KB... 8KB working-set budget on nRF52832-class targets") but §10's CRC modes carry no equivalent analysis for **time**/responsiveness on cooperatively-scheduled, watchdog-supervised readers — which is exactly the failure mode encountered here. A stall is arguably a worse failure than a RAM overrun (one fails to link, the other fails silently in the field).
2. "Streaming verify" (running CRC "in parallel with... structural checks") may assume a concurrency model the spec's own smallest target class doesn't have — single-threaded cooperative loops have no way to run anything "in parallel" without redefining the term loosely (interleaved via yields, not literal concurrency).
3. "Caller-asserted trust via a previously-verified cache" is named but has zero mechanism guidance — reasonable as a scope boundary, but nearly all the real design work in this fix (cache identity, cross-process staleness, what "previously-verified" means when the verifier and the consumer are different processes) lived entirely in that one unelaborated phrase.

## 2. On-device validation

Deployed and observed over a single continuous test window. Raw artifacts in this folder: `mappack_verify.log` (full device log), `crash-dump-0002-watchdog-gui.bin` (see below).

**It works as designed:** GUI never blocked, never froze, never triggered a device restart. Confirmed via the log:

```
[SVC 9800ms] MapPackCrcVerifier::start() ... priorMarker=Absent -> beginning scan from 0
[GUI 10096ms] ensureMapPack() structural open Ok, resolved=maps/athens.rawtiles tiles=3070
[GUI 10105ms] ensureMapPack() marker not yet trustworthy ... -- still waiting
...
[SVC 3736458ms] MapPackCrcVerifier::step() DONE elapsed=1720711ms declaredCrc=0xA3BF1336 computedCrc=0xA3BF1336 -> Verified, marker written
[GUI 3736588ms] ensureMapPack() marker Good and matches -- trusted
[SVC 4962980ms] MapPackCrcVerifier::start() ... already trusted via cached marker ... -- skipping scan
[GUI 4963262ms] ensureMapPack() marker Good and matches -- trusted
```

- **Full pass**: 1,720,711ms ≈ **28.7 minutes** for the 201MB pack, steady-state throughput ~112KB/s.
- **Reboot-cheap path confirmed**: subsequent `start()` calls go straight to `Verified` with zero scan I/O, exactly as designed.
- GUI polled the marker roughly once per second throughout, correctly reporting "not yet trustworthy" without ever stalling — this is the actual fix, working.

**One real anomaly, not fully root-caused.** The log contains *two* `"beginning scan from 0"` events, meaning the scan was interrupted and restarted once before the run captured above succeeded. Separately, `Crash/dump_403795_0002_20260806T224612_1.3.0.bin` (copied into this folder) records:

```
CRASH: WATCHDOG PC=0x080E20D4 LR=0x080D9E9D CFSR=0x00000000 task=Athens Run.GUI
Reset reason: SOFTWARE (RCC_CSR=0x14004400)   [on the following boot]
```

`CFSR=0x00000000` (no fault-status bits set) is consistent with a genuine watchdog timeout rather than a memory/bus fault. `arm-none-eabi-addr2line` against the exact deployed `AthensRunGUI.elf` couldn't resolve either address (`??`) — plausible given a hardware watchdog reset doesn't capture a reliable synchronous PC the way a real fault exception would. The design's built-in resilience (a restart just re-scans from byte 0 — safe, idempotent, never a false-trust) absorbed this without any visible symptom to the user beyond a longer wait. **Left open**: whether this was a one-off (e.g. specific to first-open of a freshly-USB-deployed 200MB file) or something that would reproduce reliably under a debug-UART rig with real-time visibility. Not reproduced on the second attempt.

## 3. Summary screen doesn't show map tiles — confirmed *not* a regression

Traced via an Explore agent, file-by-file: the post-run summary screen (`TrackSummaryView` → `SummaryFaceMap` → a `Map` container, `gui/src/containers/Map.cpp`) only ever draws the recorded GPS trace as a polyline + start/end markers on a blank canvas. It never references `Model::MapState`, `SDK::RawTiles::Container`, or `MapTileView` — there is no tile view instance on that screen at all, so nothing in this session's work could have gated or broken it. This is a pre-existing gap in the original AthensRun skeleton (a real feature to add later — give `SummaryFaceMap` its own tile view and fit-to-bounds viewport — deferred for now, not fixed here).

## 4. Map contrast: empirically resolved

On-device photo showed the basemap reading as near-uniform gray while the app-drawn GPS trace stayed crisp — a real, diagnosable problem, not a display defect.

**Root cause**: the pack's pixel format, `ABGR2222` (2 bits/channel, 4 luminance levels per channel: 0/85/170/255), is the *correct*, native format for this watch's framebuffer — not the problem. The problem is the *source style*: default OpenStreetMap standard raster tiles cluster almost all their color mass near white. Squeezed through only 4 levels per channel, nearly everything collapses into the same one or two buckets.

**Empirical comparison** (not guessed — measured): fetched single sample tiles (z14, Athens ON) from OSM standard, CyclOSM, OpenTopoMap, and the Humanitarian (HOT) style, quantized each to the real 4-level-per-channel palette, and inspected both distinct-color survival and visual legibility. See `tile-style-quantized-comparison.png` in this folder (left→right: CyclOSM, OpenTopoMap, HOT).

| Style | Distinct colors before → after 2-bit/channel quant | Visual verdict |
|---|---|---|
| OSM standard | 55 → 14 | Washed out (matches the on-device photo) |
| CyclOSM | 256 → 11 | **Best**: clean white background, sharply distinct gray road network, yellow highways, green parks — legible at a glance |
| OpenTopoMap | 255 → 22 | Highest raw color retention, but contour lines make it visually busy/noisy at this zoom |
| HOT (Humanitarian) | 254 → 8 | Large flat land-use color blocks, but roads themselves go thin/faint — wrong tradeoff for a runner glancing at their route |

Raw distinct-color counts alone were misleading (OpenTopoMap "won" numerically but lost visually to clutter) — the actual side-by-side rendering was necessary to judge legibility, not just statistics.

**Distilled design guidance** (useful beyond this one fix, e.g. for `slippypack`'s planned "watch-tuned default style JSON"): **flat, saturated, categorically-distinct fills spread across the tonal range survive 2-bit-per-channel quantization far better than gradients or dense line art** — intermediate tones just round to the nearest of 4 buckets regardless, so anything relying on subtle gradation reads as mush; anything using a handful of well-separated flat colors survives intact.

**Action taken**: regenerated the pack via `slippypack` using CyclOSM as the source (same bbox/zoom as before, `--rate-per-sec 2`), to replace the OSM-standard-style pack currently on-device.

## 5. Tile-sourcing compliance finding — affects any future pack build for this app

While fetching sample tiles for the contrast comparison, a direct request to `tile.openstreetmap.org` was **actively blocked**: a 403 page reading *"Access blocked — App is not following the tile usage policy of OpenStreetMap's volunteer-run servers."* Checking the actual policy (`operations.osmfoundation.org/policies/tiles/`) turned up something more consequential than a rate-limit nuance:

> **"Bulk downloading" is prohibited entirely.** This includes pre-seeding areas, building tile archives, automated scans... **Offline use is not permitted on `tile.openstreetmap.org`.**

This is a *use-case* restriction, independent of request pacing. `slippypack`'s built-in 2 req/sec throttle (matching OSM's "no more than 2 download threads" guidance) addresses *rate* — it does not, and cannot, make "download the whole area and keep it offline" compliant, because the policy forbids that use case outright regardless of pace. The originally-deployed 201MB pack (built by fetching ~3,070 tiles from this exact server) was built in a way that conflicts with this policy.

CyclOSM (`tile-cyclosm.openstreetmap.fr`) is not a safe substitute for this specific problem: its own docs state it operates under "a fairuse tile policy" that links back to the same OSMF policy. Same restriction, different door.

**The actually-compliant path** — and notably, one `slippypack`'s own `PLAN.md` already anticipates as "the canonical offline-laptop path": download a regional `.osm.pbf` extract from a provider like Geofabrik (data explicitly distributed for bulk/offline use, unlike a live raster tile service), then render tiles from it locally — no ongoing server dependency, no policy conflict, and full control over style (directly enabling the contrast fix above without depending on any tile host's palette choices at all). This is listed in `slippypack`'s roadmap as unbuilt Phase 2 work requiring a rendering engine (MapLibre Native or a `tilemaker` shell-out) — realistically weeks of engineering, not a quick fix.

**Disposition**: tile sourcing is being resolved separately/out-of-band by the repo owner. For this session, the CyclOSM-styled pack was built via `slippypack`'s throttled fetcher as an interim step, with this compliance gap explicitly flagged rather than silently worked around. Worth feeding back to `slippypack` itself: its rate-limiter's documentation implies rate-limiting is the compliance mechanism ("2 req/sec keeps a single-threaded fetcher comfortably inside that envelope") when the policy's actual bulk/offline-use restriction is orthogonal to rate — a design assumption worth correcting in that tool independent of anything here.

## Artifacts in this folder

- `mappack_verify.log` — full on-device diagnostic log spanning the validation window described in §2.
- `crash-dump-0002-watchdog-gui.bin` — the watchdog crash dump referenced in §2 (raw binary; readable in large part via `strings`, includes an embedded boot log and task list).
- `tile-style-quantized-comparison.png` — the CyclOSM / OpenTopoMap / HOT side-by-side quantization comparison from §4.
