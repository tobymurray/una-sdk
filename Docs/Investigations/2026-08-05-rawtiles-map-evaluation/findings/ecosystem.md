# Competitive landscape: offline raster tile packs for MCU-class smartwatches

Subject: **rawtiles** (https://tobymurray.github.io/rawtiles/) — 292-byte header, flat 20-byte/entry
index, raw ABGR2222/RGB565 tiles, optional pixel-level RLE, CRC-32 footer, ~1 KB lazy-reader RAM,
pread-friendly. Target class: 240×240 screen, 256 KB app RAM, 2 MB internal flash + eMMC, no GPU,
Cortex-M33. Researched 2026-08-05. Confidence flags: VERIFIED (direct source), PLAUSIBLE (inference
from adjacent evidence).

---

## 1. PMTiles v3

**Format facts (VERIFIED from spec, https://github.com/protomaps/PMTiles/blob/main/spec/v3/spec.md):**
- Fixed **127-byte header**; root directory must fit with the header in the first **16,384 bytes**
  (max compressed root ≈ 16,257 B).
- Directories are arrays of entries `{TileID, Offset, Length, RunLength}` where TileID is a
  **cumulative Hilbert-curve ID** (z,x,y → position on Hilbert curve, summed across zooms),
  **delta-encoded**, all fields **protobuf-style varints**. RunLength=0 means the entry points at a
  **leaf directory** (second lookup hop).
- Directories are compressed per the header's `internal_compression` field: none/gzip/brotli/zstd.
  "None" is legal, but the ecosystem's writers (go-pmtiles, tippecanoe) emit **gzip'd directories by
  default**, so a reader that wants to consume arbitrary real-world PMTiles MUST carry an inflate
  implementation. (Spec: compression enum VERIFIED; "writers default to gzip" PLAUSIBLE — consistent
  with protomaps tooling docs and the v3 blog, https://protomaps.com/blog/pmtiles-v3-whats-new/.)

**What a minimal MCU reader must implement:** header parse; inflate (gzip) for directories; varint
decode; delta-decode of TileIDs; (z,x,y)→Hilbert-ID conversion; run-length entry semantics; leaf-
directory recursion; then per-tile decompression (tiles themselves are PNG/JPEG/WebP/MVT — PMTiles
says nothing about pixels, so a raster consumer still needs an image decoder on top).

**RAM for a city-scale pack (arithmetic, PLAUSIBLE):** serialized entries run ~6–10 bytes; a
10 km × 10 km z15+z16 pack (~720 tiles, §9) is a single root directory of roughly 5–8 KB compressed
— no leaves needed. Decoding it the way every reference reader does (full entry array in RAM at
~16–24 B/entry in C) costs **~12–18 KB RAM**, plus inflate working memory (uzlib ≈ 1–2.5 KB code +
~1 KB RAM; miniz `tinfl` ~32 KB dictionary window RAM if streaming gzip). A streaming varint scan
could get directory RAM to O(1) but no published reader does this. Versus rawtiles' flat 20-byte
stride: **binary search directly against the file with pread, zero decode state, O(1) RAM** — the
delta encoding in PMTiles is precisely what forfeits random access into the index.

**MCU usage:** no PMTiles-on-MCU ecosystem exists; searches for embedded/ESP32 readers come up
empty except one notable hobby project, **yuiseki's M5Stack Cardputer "Offgrid Tiny Map"**
(https://www.hackster.io/yuiseki/m5stack-cardputer-offgrid-tiny-map-fa44d8,
https://github.com/yuiseki/m5-cardputer-offgrid-tiny-map): ESP32-S3, **no PSRAM**, renders planet
z0–14 vector PMTiles (~78 GiB on a 128 GB SD card). It works by streaming, and — the telling detail —
**first render of a tile is slow, so it caches the rendered result to SD as raw RGB565** and serves
the cache thereafter. I.e., the one known MCU PMTiles consumer converges on rawtiles' storage model
(raw RGB565 framebuffer tiles) as its fast path. There is no PMTiles C reference reader for
embedded; the official ecosystem is JS/Go/Python/Java (e.g.
https://github.com/simonpoole/pmtiles-reader).

**Cost verdict:** an MCU PMTiles reader is feasible (~2–4 KB of format logic + inflate lib +
10–20 KB directory RAM) but every element — inflate, varints, Hilbert IDs, leaf recursion, plus an
image decoder for the payload — is pure overhead relative to a fixed-stride index over
raw-pixel tiles, and none of it buys anything on-device (Hilbert clustering optimizes HTTP range
request coalescing, not local pread).

## 2. MBTiles

- MBTiles **is** SQLite: the spec is "a SQLite database with a `tiles` table". A conforming reader
  therefore embeds SQLite.
- SQLite footprint (VERIFIED, https://sqlite.org/footprint.html, https://sqlite.org/about.html):
  **~590 KB** compiled `-Os` on ARM64 gcc 10.2, ~650 KB x64; "less than 900 KiB with all features".
  With aggressive `SQLITE_OMIT_*` builds this shrinks, but Anthropic-side estimate for a
  thumb2 `-Os` read-only build is still **on the order of 300–500 KB flash** (PLAUSIBLE — sqlite.org
  no longer publishes an omit-everything number), plus SQLite wants tens-to-hundreds of KB of heap
  for page cache to perform acceptably. On the subject platform that is **15–25 % of the entire
  2 MB internal flash and a large bite of 256 KB app RAM** — for what a ~600-line bespoke reader
  (a few KB of thumb2) does for a flat format.
- Embedded/MCU precedent: **none found**. Even projects that had every reason to use MBTiles
  (Meshtastic device UI, §7) chose a bare z/x/y PNG directory tree on FAT32 instead. MBTiles' home
  turf is phones/servers where SQLite is already resident
  (https://corvusintell.com/blog/field-apps/mbtiles-pmtiles-offline-maps/).

## 3. Garmin custom maps (KMZ / JNX / IMG)

**KMZ Custom Maps (VERIFIED):**
- Format: a zip containing JPEG tiles + `doc.kml` georeference. Per-image limit **1 megapixel**
  (e.g. 1024×1024) and ≤ 3 MB; devices index at most **100 Custom-Map JPEGs total** (500 on Montana/
  Oregon 6x0/GPSMAP 64-class units).
  Sources: https://github.com/NINAnor/GarminCustomMaps, https://topofusion.com/garmin-custom-maps.php,
  https://www.gpsrchive.com/Shared/Custom%20Maps%20Tutorial/Custom%20Maps%20Tutorial.html
- **Watches do support it**: DC Rainmaker documents installing KMZ satellite imagery on
  **fenix 5 Plus/6/7 and Epix** (https://www.dcrainmaker.com/2022/03/install-satellite-imagery.html);
  Garmin forums confirm working (sometimes finicky) KMZ on Epix Gen 2 / fenix 7 / fenix 8
  (https://forums.garmin.com/outdoor-recreation/outdoor-recreation/f/fenix-7-series/391011/,
  https://forums.garmin.com/outdoor-recreation/outdoor-recreation/f/epix-2/355117/). So raster-on-
  wearable is shipped commercial reality — not hypothetical.
- **Lesson:** the 100-tile ceiling ≈ 100 MP ≈ one ~10×10 km area at ~1 m/px, single zoom level.
  Garmin evidently caps the *index* and leans on JPEG for size; users constantly fight the limits.
  A format with a flat scalable index and cheap per-tile access removes exactly the pain Garmin
  users hit.

**JNX (BirdsEye):** proprietary, reverse-engineered by hobbyists; internally **JPEG tiles**, up to
5 zoom levels, ~5000 tiles/level, and **device-locked via an encrypted unit ID** at fixed offsets
(https://www.gpspower.net/creating-maps/168766-garmin-birdseye-jnx-format-hacking.html,
http://gwprojects.org/forum/viewtopic.php?f=17&t=1933). Structurally it's close to what rawtiles is
— header + tile index + raster blobs — but undocumented, DRM'd, and JPEG-bound. It proves the shape
of the design; it is not usable prior art for an open ecosystem.

**IMG:** Garmin's vector format, proprietary, reverse-engineered (mkgmap ecosystem). Garmin renders
vector maps on watch-class hardware — but with a vertically integrated proprietary renderer, years
of tuning, and (on map-capable watches) 16–32 GB storage. It's the existence proof that vector on a
watch is possible *and* the demonstration of how much machinery it takes.

## 4. Bangle.js OpenStMap

Sources: https://github.com/espruino/BangleApps/tree/master/apps/openstmap,
https://banglejs.com/apps/?id=openstmap, https://www.espruino.com/Bangle.js2+Technical

- **Delivery:** the App Loader web page has a per-app "disk" uploader: pick an area on a Leaflet
  map, choose a size preset, `Get Map`; the page rasterizes OSM tiles in the browser, **pre-dithers
  to 3 bpp** (matching the Bangle.js 2's 3-bit 176×176 LCD) and uploads over BLE. Bangle.js 1 may
  optionally use 8-bit images ("2.6× more space"). Bangle.js 2 is 3 bpp only.
- **Storage/reader:** maps land as Espruino Storage files (`openstmap.<n>.json` metadata + image
  files used as sprite sheets; the JS lib computes tile frame = `ttx + tty*map.w` and blits via
  `Bangle.project()`). Reader is ~a few hundred lines of JS on top of Espruino's native image blit.
- **Hardware limits (VERIFIED):** nRF52840, **256 KB RAM, 1 MB internal + 8 MB external flash**
  shared with all apps — so maps are a few hundred KB to low MB: a small town at a couple of zoom
  levels. BLE upload of a "medium" map takes minutes.
- **UX verdict:** the closest living analogue to rawtiles' pipeline (browser-side pre-rasterization
  → dithered low-bpp raw-ish images → dumb blit on watch), and it validates the approach — but the
  format is ad-hoc, Espruino-specific, undocumented at byte level, single-projection, no index
  structure, no integrity checking.

## 5. InfiniTime / PineTime

- **No offline map effort found.** InfiniTime's only navigation feature is the **InfiniNav /
  Navigation app**: it displays turn-by-turn *instructions/icons pushed from the phone* (OsmAnd,
  PureMaps, Google Maps via Gadgetbridge) — no tiles, no map raster
  (https://pine64.org/documentation/PineTime/Software/InfiniTime/,
  https://codeberg.org/Freeyourgadget/Gadgetbridge/issues/2387,
  https://github.com/organicmaps/organicmaps/issues/6564 — open request, unimplemented).
- Telling constraint: the Navigation app's *fonts* had to be moved to external SPI-flash resources
  "to free 19 KB" (InfiniTime release notes) — on a 512 KB-flash nRF52832 with 64 KB RAM, even icon
  assets are a fight. PineTime's 4 MB SPI flash could hold a small RLE'd raw-tile pack; nobody has
  tried. Status: **vacant niche**, not a competing format.

## 6. wasp-os and other open watch platforms

- **wasp-os** (MicroPython on PineTime): app list is clocks, HR, stopwatch, calculator, games —
  **no map or navigation app found** (https://github.com/wasp-os/wasp-os,
  https://wasp-os.readthedocs.io/en/latest/README.html).
- Nothing map-shaped surfaced for other MCU-class open platforms (Sensor Watch, ZSWatch) in these
  searches (PLAUSIBLE absence — not exhaustively swept). The only open-watch platform with working
  offline maps is Bangle.js (§4).

## 7. Vector-on-MCU reality check

- **Meshtastic device UI (MUI, T-Deck / T-LoRa Pager)** — the highest-profile "map on an MCU"
  today — is **raster, not vector**: 256×256 **PNG** tiles in `/maps/{style}/z/x/y.png` on a FAT32
  SD card, 8-bit palette recommended, zooms 1–20, tiles fetched by third-party downloader tools
  (https://github.com/meshtastic/device-ui/blob/master/maps/README.md,
  https://meshtastic.org/docs/configuration/device-uis/meshtasticui/,
  https://www.jeffgeerling.com/blog/2025/adding-gps-and-grid-maps-my-meshtastic-t-deck/). They
  pay PNG decode (inflate + line buffers) per pan on an ESP32-S3 with PSRAM.
- **The one true MVT-on-MCU renderer found:** yuiseki's Cardputer project (§1) — ESP32-S3,
  no PSRAM, streaming inflate + simplified styling, planet z0–14. Performance is poor enough that
  it **rasterizes to RGB565 on SD as a cache**. Footprint not published (PLAUSIBLE: tens of KB of
  code; it is a demo, not a library). Its siblings (M5AtomS3R "Ultra Tiny Map", M5Stamp "Flying
  Tiny Map", https://www.hackster.io/yuiseki/m5atoms3r-ultra-tiny-map-4a4a65) share the approach.
- No embedded mapsforge port, no LVGL vector-map widget (LVGL map components are raster tile
  loaders, e.g. `0015__map_tiles`, §8), no shipped MVT product on MCU-class hardware found —
  except Garmin's proprietary IMG stack (§3). **Conclusion: vector on MCU is demonstrated-possible
  and consistently not chosen**; every practical system pre-rasterizes somewhere.

## 8. Prior art for "pre-rasterized raw-pixel tile pack with a published byte-level spec"

Closest neighbors, none of which occupy the exact class:
- **`0015__map_tiles` (That Project, ESP-IDF/LVGL 9 component)** — loads **raw 256×256 RGB565
  tiles from SD**; the nearest thing to rawtiles' payload model in the wild. But it's a code
  artifact: loose files, no pack container, no documented header/index, no integrity checking, no
  spec document, no conformance corpus (https://github.com/0015/map_tiles_projects,
  https://osrtos.com/projects/map-tiles-projects-for-esp32/).
- **yuiseki's RGB565 SD cache** (§1/§7) — raw framebuffer tiles as an *internal cache format*,
  again unspecified.
- **Garmin JNX** — header + multi-level tile index + raster blobs, but proprietary, DRM-locked,
  JPEG payload (§3).
- **Garmin KMZ** — zip + JPEG + KML; needs zip walk + JPEG decode. On decode cost, be honest:
  **TJpgDec** does baseline JPEG in **3.5–8.5 KB ROM + ~3.1–3.5 KB RAM**
  (https://elm-chan.org/fsw/tjpgd/), so "JPEG needs 20–30 KB flash + 40 KB RAM" overstates it —
  the real raster-decode costs are CPU per pan, artifact smearing on 1-bit-ish map linework, and
  quality collapse at 2-bit color depth after dithering. PNG is heavier where it matters:
  **PNGdec "runs on any MCU with at least 48K of free RAM"**
  (https://github.com/bitbank2/PNGdec) — nearly 20 % of the subject's 256 KB app RAM.
- **Bangle.js openstmap** — pre-dithered low-bpp raw-ish images, but ad-hoc and Espruino-bound (§4).
- **QOI** (https://qoiformat.org/) — proves the market's appetite for one-page byte-level specs
  with reference corpora, and QOI-tiled maps are an obvious idea, but **no QOI map-tile pack format
  was found**; QOI is also RGB(A)8-only, so it can't express ABGR2222/RGB565 natively.

**Judgment:** raw-pixel map tiles on MCUs are a recurring *pattern* (0015, yuiseki's cache,
openstmap), but **no one has published a specified container for them** — header, flat index,
declared pixel formats, canonical quantizers, RLE with defined canonical encoding, CRC, golden
fixtures + negative corpus. In this exact class, a published spec with a conformance corpus appears
to be **genuinely novel**, not merely unusual. The competition is either unspecified code (0015),
proprietary+DRM (JNX), or a general format that still needs a decoder (PNG/JPEG/PMTiles payloads).

## 9. Size arithmetic: 10 km × 10 km at z15+z16, 256 px tiles, lat ≈ 45°

Tile ground size = 40 075 km · cos(45°)/2^z: **z15 ≈ 0.865 km, z16 ≈ 0.432 km**.
- z15: ceil(10/0.865)=12 → 12×12 = **144 tiles**; z16: 24×24 = **576 tiles**; total **≈ 720 tiles**.

Per 256×256 tile:
| encoding | bytes/tile | 720 tiles |
|---|---|---|
| raw RGB565 (16 bpp) | 131,072 | **94 MB** |
| raw ABGR2222 (8 bpp) | 65,536 | **47 MB** |
| PNG, OSM-carto-style city (avg ≈ 15–20 KB; OSM wiki cites 17.9 KB for osm-bright+hillshade, ~2 KB fleet-wide avg dominated by empty tiles — https://wiki.openstreetmap.org/wiki/Tile_disk_usage) | ~15,000–20,000 | **~11–14 MB** |
| RLE'd 8 bpp (carto-style flat-area maps; 3–8× typical — PLAUSIBLE, not measured) | ~8,000–22,000 | **~6–16 MB** |

Budget fit:
- **2 MB internal NOR:** raw 8 bpp fits only ~31 tiles (a ~2.5 km z16 patch). RLE'd 8 bpp at ~5×
  fits **one zoom level**: 144 z15 tiles × ~13 KB ≈ 1.9 MB — 10×10 km at z15 *just* fits. So NOR is
  a "single-neighborhood / single-zoom" tier, and RLE is what makes it usable at all.
- **Few-hundred-MB eMMC:** 720-tile z15+z16 pack costs 47 MB raw 8 bpp — trivial. A 50×50 km metro
  at z12–z16 (~18 k tiles) is ~1.2 GB raw 8 bpp but **~200–300 MB RLE'd** — fits the budget; raw
  RGB565 at metro scale (~2.3 GB) does not. RLE (or accepting 8 bpp) is load-bearing at metro scale.
- Comparison honesty: PNG is ~3–4× denser than RLE'd 8 bpp, but costs inflate + ≥48 KB RAM +
  CPU per tile; rawtiles trades storage (abundant on eMMC) for zero-decode blits (scarce CPU/RAM).
  That trade is exactly wrong for 2 MB-NOR-only devices and exactly right once eMMC exists.

---

## Sources (primary)

- rawtiles spec: https://tobymurray.github.io/rawtiles/
- PMTiles v3 spec: https://github.com/protomaps/PMTiles/blob/main/spec/v3/spec.md ; v3 blog: https://protomaps.com/blog/pmtiles-v3-whats-new/
- yuiseki Cardputer PMTiles map: https://www.hackster.io/yuiseki/m5stack-cardputer-offgrid-tiny-map-fa44d8 ; https://github.com/yuiseki/m5-cardputer-offgrid-tiny-map
- SQLite size: https://sqlite.org/footprint.html ; https://sqlite.org/about.html
- MBTiles vs PMTiles offline: https://corvusintell.com/blog/field-apps/mbtiles-pmtiles-offline-maps/
- Garmin Custom Maps: https://github.com/NINAnor/GarminCustomMaps ; https://topofusion.com/garmin-custom-maps.php ; https://www.gpsrchive.com/Shared/Custom%20Maps%20Tutorial/Custom%20Maps%20Tutorial.html ; https://www.dcrainmaker.com/2022/03/install-satellite-imagery.html ; https://forums.garmin.com/outdoor-recreation/outdoor-recreation/f/fenix-7-series/391011/
- JNX reverse engineering: https://www.gpspower.net/creating-maps/168766-garmin-birdseye-jnx-format-hacking.html ; http://gwprojects.org/forum/viewtopic.php?f=17&t=1933
- Bangle.js openstmap: https://github.com/espruino/BangleApps/tree/master/apps/openstmap ; https://banglejs.com/apps/?id=openstmap ; https://www.espruino.com/Bangle.js2+Technical
- InfiniTime navigation: https://pine64.org/documentation/PineTime/Software/InfiniTime/ ; https://codeberg.org/Freeyourgadget/Gadgetbridge/issues/2387 ; https://github.com/organicmaps/organicmaps/issues/6564
- wasp-os: https://github.com/wasp-os/wasp-os ; https://wasp-os.readthedocs.io/en/latest/README.html
- Meshtastic MUI maps: https://github.com/meshtastic/device-ui/blob/master/maps/README.md ; https://meshtastic.org/docs/configuration/device-uis/meshtasticui/ ; https://www.jeffgeerling.com/blog/2025/adding-gps-and-grid-maps-my-meshtastic-t-deck/
- 0015 raw-RGB565 LVGL tiles: https://github.com/0015/map_tiles_projects ; https://osrtos.com/projects/map-tiles-projects-for-esp32/
- Decoder footprints: https://elm-chan.org/fsw/tjpgd/ (TJpgDec) ; https://github.com/bitbank2/PNGdec
- QOI: https://qoiformat.org/ ; https://github.com/phoboslab/qoi
- OSM tile sizes: https://wiki.openstreetmap.org/wiki/Tile_disk_usage ; https://wiki.openstreetmap.org/wiki/Zoom_levels
