// Generate the UNA watch basemap style for the Protomaps schema.
//
// Every colour is derived from its ABGR2222 byte, per MAP_CARTOGRAPHY_SPEC.md § 3 --
// the byte is authoritative and the spec's `preview` sRGB column is explicitly NOT
// what belongs in a style file. The script asserts at the end that no colour outside
// the declared slots appears anywhere, so "palette-locked" is mechanical, not
// aspirational.
//
// Line weights are § 4's, in panel pixels. They are only meaningful at a stated m/px:
// these are correct for tile_dim 256, where § 7's published ladder (z12-16) and its
// m/px column agree. At tile_dim 128 the ladder shifts to z12-z17 -- see finding 18 in
// Docs/Investigations/2026-08-12-map-e2e-run/.
const fs = require('fs');

const SOURCE = 'protomaps';

// slot -> ABGR2222 byte (spec § 3)
const BYTE = {
  paper: 0xFF, landuse: 0xEE, wood_lt: 0xDD, building: 0xEA, wood: 0xD8,
  water: 0xF4, contour: 0xC5, water_dk: 0xF0, trace: 0xC3,
  road_minor: 0xC1, path: 0xD0, road_major: 0xC0, ink: 0xC0, halo: 0xFF,
};

/** ABGR2222: two bits per channel, expanded by x85. Alpha must be 3 (opaque). */
function hex(byte) {
  const a = (byte >> 6) & 3, b = (byte >> 4) & 3, g = (byte >> 2) & 3, r = byte & 3;
  if (a !== 3) throw new Error(`slot byte 0x${byte.toString(16)} is not opaque`);
  return '#' + [r, g, b].map((c) => (c * 85).toString(16).padStart(2, '0').toUpperCase()).join('');
}
const C = Object.fromEntries(Object.entries(BYTE).map(([k, v]) => [k, hex(v)]));
const LEGAL = new Set(Object.values(C));

// Kind sets, taken from what the Athens extract actually contains plus the schema's
// wider vocabulary so the style is not Athens-specific.
const BUILT_UP = ['residential', 'commercial', 'industrial', 'school', 'farmland',
                  'playground', 'pedestrian', 'aerodrome'];
const GREEN_REC = ['park', 'golf_course', 'cemetery', 'nature_reserve', 'national_park'];
const GREEN_NAT = ['wood', 'forest', 'wetland', 'scrub', 'grass'];
const MAJOR = ['highway', 'major_road'];
const MINOR = ['medium_road', 'minor_road', 'other'];
const FOOT = ['path'];

const fill = (id, source_layer, kinds, color) => ({
  id, type: 'fill', source: SOURCE, 'source-layer': source_layer,
  filter: ['in', ['get', 'kind'], ['literal', kinds]],
  paint: { 'fill-color': color },
});

const line = (id, source_layer, kinds, color, width, extra = {}) => ({
  id, type: 'line', source: SOURCE, 'source-layer': source_layer,
  filter: ['in', ['get', 'kind'], ['literal', kinds]],
  layout: { 'line-cap': 'butt', 'line-join': 'miter', ...(extra.layout || {}) },
  paint: { 'line-color': color, 'line-width': width, ...(extra.paint || {}) },
});

const layers = [
  { id: 'background', type: 'background', paint: { 'background-color': C.paper } },

  // Ground and area fills. R2: green fills are ambient context, never a must-notice cue.
  fill('earth', 'earth', ['earth'], C.paper),
  fill('landuse_builtup', 'landuse', BUILT_UP, C.landuse),
  fill('landuse_green_rec', 'landuse', GREEN_REC, C.wood_lt),
  fill('landuse_green_nat', 'landuse', GREEN_NAT, C.wood),

  // Water. Fill plus a 1 px darker edge, per § 4.
  fill('water_fill', 'water', ['water', 'ocean', 'lake', 'river', 'playa'], C.water),
  line('water_edge', 'water', ['water', 'ocean', 'lake', 'river', 'playa'], C.water_dk, 1),
  line('waterway', 'water', ['river', 'stream', 'canal', 'ditch', 'drain'], C.water_dk, 2),

  // Buildings: context only (§ 3), so no outline competing with roads.
  fill('buildings', 'buildings', ['building'], C.building),

  // ---- Road casings, ALL of them, before ANY road fill ----------------------
  // § 4 inverts the usual convention: the road is the dark thing and the casing is a
  // `paper` halo, so it reads as a gap rather than an outline. Casings must be a
  // separate pass or a casing overdraws the adjacent road it should be separating.
  line('road_major_casing', 'roads', MAJOR, C.halo, 7),
  line('road_minor_casing', 'roads', MINOR, C.halo, 5),

  // ---- Road fills ----------------------------------------------------------
  // R1: the three dark codes are a budget. 0xC0 is the road you are on and label text;
  // 0xC1 warm = vehicular, 0xD0 cool = foot. They differ by hue, not weight.
  line('road_major', 'roads', MAJOR, C.road_major, 4),
  line('road_minor', 'roads', MINOR, C.road_minor, 2),
  line('road_path', 'roads', FOOT, C.path, 2, {
    paint: { 'line-dasharray': [3, 3] },
  }),

  // ---- Labels --------------------------------------------------------------
  // § 4: 11-12 px, `ink`, 1 px `halo` ring. Only `Noto Sans Regular` exists in the
  // renderer image, so the spec's "bold" is not currently achievable -- recorded as a
  // gap rather than silently substituted.
  {
    id: 'label_place', type: 'symbol', source: SOURCE, 'source-layer': 'places',
    filter: ['==', ['geometry-type'], 'Point'],
    layout: {
      'text-field': ['get', 'name'], 'text-font': ['Noto Sans Regular'],
      'text-size': 12, 'text-max-width': 8, 'text-padding': 4,
    },
    paint: { 'text-color': C.ink, 'text-halo-color': C.halo, 'text-halo-width': 1 },
  },
  {
    id: 'label_road', type: 'symbol', source: SOURCE, 'source-layer': 'roads',
    filter: ['in', ['get', 'kind'], ['literal', [...MAJOR, ...MINOR]]],
    layout: {
      'text-field': ['get', 'name'], 'text-font': ['Noto Sans Regular'],
      'text-size': 11, 'symbol-placement': 'line', 'text-padding': 4,
    },
    paint: { 'text-color': C.ink, 'text-halo-color': C.halo, 'text-halo-width': 1 },
  },
];

const style = {
  version: 8,
  name: 'una-watch',
  metadata: {
    'una:spec': 'MAP_CARTOGRAPHY_SPEC.md § 3 palette, § 4 weights',
    'una:tile_dim': 256,
    'una:ladder': 'z12-16 at tile_dim 256; z12-z17 at 128 (finding 18)',
    'una:slots_used': Object.keys(BYTE).length,
  },
  sources: { [SOURCE]: { type: 'vector', url: 'pmtiles://{pmtiles}' } },
  glyphs: '{fontstack}/{range}.pbf',
  layers,
};

// Palette lock: every colour literal in the emitted style must be a declared slot.
const used = new Set();
JSON.stringify(style).replace(/"#[0-9A-Fa-f]{6}"/g, (m) => used.add(m.slice(1, -1)));
const illegal = [...used].filter((c) => !LEGAL.has(c));
if (illegal.length) throw new Error(`off-palette colours emitted: ${illegal.join(', ')}`);

fs.writeFileSync(process.argv[2], JSON.stringify(style, null, 2));
console.error(`layers=${layers.length} colours used=${used.size} all legal slots=yes`);
console.error('slots: ' + Object.entries(C).map(([k, v]) => `${k}=${v}`).join(' '));
