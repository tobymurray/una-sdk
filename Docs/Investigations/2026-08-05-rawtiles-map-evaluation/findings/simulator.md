# RawTilesMap tutorial — simulator build & render verification

Branch: `feat/rawtilesmap-tutorial`, HEAD `7fc2602c` ("feat(tutorial): add RawTilesMap, a tile-based map renderer built on GpsTrack").
Built from a `git archive` copy of the worktree (worktree itself untouched; `git status` clean before and after).
Toolchain: `docker run --platform linux/amd64 gcc:13` + libsdl2/libjpeg/ruby-nokogiri, `UNA_SDK=/src make -f simulator/gcc/Makefile -j4` from the app's TouchGFX-GUI dir.

## 1. Build: YES — but only after fixing the app's simulator Makefile (findings below)

The committed tree does NOT build as-is. Two rounds of failure:

1. **`make: No rule to make target 'build/Linux/Libs/Source/FitHelper/FitHelper.o'`**
   `Docs/Tutorials/RawTilesMap/Software/Apps/TouchGFX-GUI/simulator/gcc/Makefile` still lists
   `Libs/Source/FitHelper/FitHelper.cpp` and the four Garmin `ThirdParty/FitSDKRelease_21.171.00/c/*.c`
   sources plus the `$(SDK)/ThirdParty/FitSDKRelease_21.171.00/c` include path — all removed from the
   tree by ancestor commit `659a7a5f` ("build(fit): rewire builds to SDK::Fit, port tutorials, remove
   FitHelper"). That commit ported the other tutorials' Makefiles but RawTilesMap (added later on this
   branch) was never ported. Notably the tutorial's own `Software/Libs/Sources/ActivityWriter.cpp` IS
   already written against SDK::Fit — only the Makefile is stale.
   **Fix applied (build copy only):** drop the 5 stale entries + include path, add
   `Libs/Source/Fit/{FitCrc,FitWriter,FitRecordCadence,RecordingMarker}.cpp` (mirrors
   `Examples/Apps/Hiking/.../simulator/gcc/Makefile`).

2. **Link failure: `undefined reference to Sensor::ImuRunningCadence::ImuRunningCadence()`**
   (referenced from `Libs/Source/Simulator/Components/InstanceSensorLayer.cpp:80`). The Makefile's
   source list predates `ImuRunningCadence`; Hiking's Makefile has it, RawTilesMap's doesn't.
   **Fix applied:** add `Libs/Source/Simulator/Components/Sensors/Imu/ImuRunningCadence.cpp`.

3. **Missing `config/gcc/app.mk`** (expected on recent lineages; app has only `config/msvs/`).
   **Fix applied:** created with `touchgfx_path := ../../../../../../ThirdParty/touchgfx`
   (same content/depth as the Examples apps).

After these three changes: clean build, `build/bin/simulator.out` (5.2 MB), `MAKE_EXIT=0`.
Logs: `findings/build.log` (failure 1), `build2.log` (failure 2), `build3.log` (success).

**None of these fixes were committed anywhere; the worktree at `wt-rawtiles` is untouched.** The
Makefile port (items 1–2) and the app.mk (item 3) need to land on the branch for the tutorial to
build out of the box.

## 2. Boot: YES

Headless run under Xvfb (`:99`), SDL window `RawTilesMap`, geometry 240x240. Boot marker
`GUI is now running` present (`findings/evidence/sim.log:52`). Service starts, GPS track recording
starts, simulated sensor feed flows (HR, GPS, pressure, steps...). No crashes; clean run until killed.

## 3. Map render: YES — real tiles, full coverage, no seams

Evidence: `findings/evidence/01-boot-map-screen.png` (map screen at boot, pre-keypress),
`02-main-screen-after-L1.png` and `03-main-screen-later.png` (Main sensor screen after pressing '1'),
`sim.log` (full app log).

- **01-boot-map-screen.png**: a coherent 240x240 rendered OSM map of the Stanley Park / Lions Gate
  Bridge area of Vancouver — matches the pack bbox (lon [-123.155, -123.120], lat [49.290, 49.315]).
  Visible: "Lions Gate Bridge" label and the bridge road crossing Burrard Inlet, "Vancouver Harbour"
  water labels, Stanley Park landmass with paths, road casings, an orange linear feature in the water
  (part of the tile art). All four visible quadrants line up; **no seams, no slate sentinel patches,
  no blank cells** in the visible area.
- **No position marker is drawn** — the app renders tiles only. The viewport is pack-driven, not
  GPS-driven: Model picks the first index entry at zoom_max as the seam centre (z=14 x=2587 y=5604).
  The simulator's canned GPS fix (49.23N 28.47E — nowhere near Vancouver) has no effect on the map;
  the map renders identically with or without a fix. So "no-fix behaviour" = same map.
- Log confirms the geometry: pack header tile_dim 256 px, zoom [12,14], 12 tiles; at z14 the pack is
  2x3 tiles (x 2587–2588, y 5604–5606), so the 3x3 viewport around the centre finds
  **4/9 tiles ("viewport filled 4/9 cells")** — exactly the four centre cells whose quadrants tile
  the whole 240x240 visible area. The five missing cells are off-screen; hence full visual coverage.
- The app **boots directly into the Map screen**: the hand-written
  `gui/include/gui/common/FrontendHeap.hpp:70` overrides `gotoStartScreen` with
  `app.gotoMapScreenNoTransition()`, overriding the designer file's `StartupScreenName: "Main"`
  (`RawTilesMap.touchgfx:242`). Key '1' (L1) toggles Map <-> Main (verified: screenshots 02/03 show
  the Main sensor screen — Battery/Time/HR/Steps/GPS/CPU stats — after one keypress).

## 4. Pack discovery mechanism (Model.cpp)

`Docs/Tutorials/RawTilesMap/Software/Apps/TouchGFX-GUI/gui/src/model/Model.cpp`:
- **`RAWTILES_PATH` env var wins if set** (line 64: `std::getenv("RAWTILES_PATH")` → `mTiles.openFromFile(envPath)`).
- Otherwise it walks `kPackCandidates[]` (lines 25–32): six relative paths covering launches from
  `build/bin/`, `simulator/gcc/`, the TouchGFX-GUI dir, and the repo root
  (e.g. `../../../../../Resources/stanley.rawtiles`, `Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles`).
- Simulator-only: the whole block is `#if defined(SIMULATOR)` (line 44).
- This run used `RAWTILES_PATH=/src/Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles`;
  log: `rawtiles: opening ... -> ok` (sim.log:12).

## 5. Method notes

- Screenshot method: the in-repo recipe from commit `36084ac3` (`Docs/Simulator-Linux.md` — xdotool
  window lookup + ImageMagick `import -window`), run under Xvfb inside the container. Keypress
  delivered via xdotool XTEST. **No code edits/hooks were needed in the app**; the only tree changes
  were the three build fixes above, made in the throwaway build copy (`scratchpad/simtree`), never in
  the worktree.
- Runner script: `scratchpad/simtree/run_sim.sh`; full trace in `findings/run.log`.
