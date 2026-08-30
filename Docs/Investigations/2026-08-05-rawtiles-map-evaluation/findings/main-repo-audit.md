# Main-repo docs/API audit (deployment, constraints, IFileSystem, TrackMapBuilder, GPS floats)

Source: background research agent, 2026-08-05. All paths on main @ 59210889.

## Premise corrections
1. `Utilities/Scripts/app_packer.py` is actually `Utilities/Scripts/app_packer/app_packer.py`.
2. Header dir is `Libs/Header/SDK/Interfaces/` (plural); namespace `SDK::Interface` (singular).
3. **No prose documents a "256KB app RAM limit"** — only a Mermaid label at `Docs/architecture-deep-dive.md:1628`. The build system links per-process RAM: 500K service (`cmake/una-app.cmake:217`) / 600K GUI (`:281`) via `-Wl,--defsym=RAM_LENGTH=` (`:250`,`:314`) into `Libs/Source/AppSystem/linker/Main/Sections.ld:7-9`; overridable. Do not cite 256KB as documented.
4. `architecture-deep-dive.md:2044-2045` hardware diagram (320x300 RGB, GT911 touch) contradicts the port doc and code — trust `TouchGFX-Port-Architecture.md`.

## Packaging/deployment (openness gap) — CONFIRMED, stricter than claimed
- `app_packer.py` (374 lines) packs ELF only — **not even icons** (docstring :8,:15; PIL import kept "if icon support returns" :42). CLI has no manifest/resource argument (:316-321). Closed hardcoded section list (:200-203). No slot for any data file.
- Icons enter at `app_merging.py:130-131` (60x60 + 30x30, hard-validated :158-161; sizes pinned in `pack_variants.py:124-126`). Fixed 5-part container: header + 2 icons + service + gui (+CRC) — `app_merging.py:238`.
- `RESOURCES_PATH` (cmake/una-app.cmake:9-11) consumed once, two hardcoded icon filenames (:358-362). Every `Resources/` dir repo-wide contains only PNG icons.
- Deploy: USB mass-storage hand-copy only (`Docs/deploy.md:5-9`; watch scans `<Drive>:\Apps\<App>\` on boot per `Update-Watch-Apps.ps1:21`; script warns of silent bit-level corruption via scripted copy :25-26). BLE OTA exists but is a system-update channel (`development-workflow.md:553-556`, `2:/Update/`).
- **No rawtiles discovery convention anywhere on main** — `git ls-files | grep -i rawtiles` empty; zero hard-coded `"[0123]:/"` volume-path literals in Examples/Libs/Docs.

## Device constraints (verified)
- STM32U595: one occurrence (Mermaid, architecture-deep-dive.md:2041); corroborated by `-mcpu=cortex-m33` + `-mfpu=fpv5-sp-d16` (una-app.cmake:95-96,:127) → **single-precision FPU; double is soft-float**.
- Display: 240×240 8bpp ABGR2222, software render, single static framebuffer (`TouchGFX-Port-Architecture.md:24,:34,:395-399`; `TouchGFXHAL.cpp:58` `static uint8_t sFrameBuffer[skBufferSize]`, 240*240 computed :53-55; no double buffering :82-83).
- Storage: 2MB NOR + eMMC + SPI flash + USB MSC (`architecture-deep-dive.md:1130-1135`). Volumes are FOUR (:1137-1142): `0:/` NOR/system, `1:/` eMMC/user data (where a map pack belongs), `2:/` USB/apps/OTA staging, `3:/` SPI flash backup+logs (Mermaid only; prose lists three).

## IFileSystem / IFile (Libs/Header/SDK/Interfaces/IFileSystem.hpp, 325 lines)
- `skMaxPathLen = 256` incl. NUL (:36-37).
- `read(char* buff, size_t btr, size_t& br)` (:213); `seek(size_t offset)` **absolute-only, no whence** (:229; sole impl `Mock/FileSystem.cpp:257` does lseek SEEK_SET). No pread — seek+read is two calls, non-atomic, mutable cursor. `size()` :184, `open(wMode,override)` :192, `getPosition()` :248.
- Enumeration: cursor-style `readNext(ObjectInfo&, bool reset=false)` (:313, semantics :288-312) — discovery convention would build on this.

## TrackMapBuilder (main)
- Breadcrumb-trail renderer: equirectangular projection of recorded own-track into a small bitmap; **no file I/O, no tiles** (`TrackMapBuilder.hpp:6,:23-24`). GpsPoint float lat/lon (:34-36). kMetersPerDegree=111320.0f (:194).
- `TrackMapScreen::Point` CONFIRMED `uint8_t x,y` under `#pragma pack(push,1)` (`TrackMapScreen.hpp:27,:32-35`); 255 is a sentinel (usable 0-254). → composes with a tile basemap as an overlay source, never a basemap engine.

## GPS floats — CONFIRMED
- `SensorDataParserGpsLocation.hpp`: class, zero-copy view; lat/lon/alt all float (:99-111,:88); structurally float — `SensorDataView.hpp:26-35` union has no double lane. ~7 sig digits ≈ 1.5 m. (Enum comments :42-43 wrongly say metres; method docs correct.)

## Bottom line
No packaging path, no deploy path, no discovery convention for arbitrary resource files on main. The pack-as-user-contract design must ADD a convention (eMMC `1:/maps/` + readNext enumeration) rather than fight an existing one — nothing is leaked yet because nothing exists.
