# 2026-08-06 — Athens, Ontario map pack (PoC basemap for AthensRun)

The basemap for the AthensRun PoC (`RAWTILES_POC_APP_PROMPT.md`, repo root): ~10 × 8 km
around Athens, Ontario (village center 44.6259 N, −75.9523 W, geocoded via Nominatim,
OSM relation 7434060), zooms 12–16, built with slippypack from OSM raster tiles.

## The pack

| Field | Value |
|---|---|
| File | `athens.rawtiles`, **45,037,308 B** (~43 MiB) |
| SHA-256 | `5ea9b75f822e1c7c98c7b97e2fa923e21fd3d17122ab8b81e9e81a5617b861de` |
| `pack_uuid` | `feeafe01-34bf-5422-9549-bee1a0f1f0bd` (slippypack pre-0.7 descriptor) |
| Pixel format / compression | ABGR2222, `None` (deliberate: the vendored Container fails closed on RLE until PoC experiment E3 lands the decoder) |
| `tile_dim` | 256 px (64 KiB/tile) |
| Tiles | **687** — z12: 6, z13: 16, z14: 42, z15: 143, z16: 480 |
| bbox | lon [−76.015, −75.889], lat [44.590, 44.662] |
| Attribution | `ATTR` = "© OpenStreetMap contributors" (ODbL) |
| `build_timestamp` | 0 — OSM's CDN returns no `Last-Modified`; the writer warned and § 4.10's no-freshness sentinel applies |

## Verification (done at build time, 2026-08-06)

Opened by the **C reference reader** (`rawtiles` repo @ `spec-0.7-adequacy-fixes`) with
full eager validation — every § 11 rule plus the footer CRC — and a sample tile decoded
to the expected 65,536 bytes. Fetch was rate-limited to OSM's policy cap (2 req/s,
built into slippypack) — a one-off ~690-tile fetch, not a bulk pipeline.

## Regeneration

From the slippypack repo, branch `rle-pixel-level` (any branch ≥ it works;
`--compression none` also builds on `main`):

```
cargo run -p slippypack-cli -- make \
  --source 'https://tile.openstreetmap.org/{z}/{x}/{y}.png' \
  --bbox='-76.015,44.590,-75.889,44.662' \
  --zoom 12-16 \
  --attribution '© OpenStreetMap contributors' \
  --out athens.rawtiles
```

Byte-identical regeneration is NOT guaranteed across time — the source is a live tile
server, so tile content (and therefore every hash) drifts with OSM edits and style
changes. The command is the recipe; the SHA-256 above pins this build.

## Notes / spin-offs observed while building

- **slippypack emits the *requested* bbox verbatim** in the header rather than § 4.9's
  canonical tile-coverage bbox (tile edges at z16 extend slightly past the request).
  Harmless to readers (range/order checks pass) but a § 4.9 canonical-derivation gap
  for a reproducibility-claiming writer → slippypack fix, alongside the spec-0.7
  identity update.
- Size arithmetic for E3 (RLE A/B): 45 MB at `None`; the audit's measured 32.4% RLE
  ratio predicts a ~14.6 MB RLE twin.
- Deploying to the watch: copy into the app sandbox as `maps/athens.rawtiles`;
  **BLE sync off during the USB copy; byte-verify after copying, before unmount.**
