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

// v4 responds to the 2026-08-12 hardware trial. Legibility on the panel is
// U-shaped in zoom: z12 and z16 read, z13 and z14 are texture. So the wide zooms
// become a skeleton -- majors, water, area fills, place names only -- and detail
// arrives at z15 where there is room for it. DETAIL_Z is that boundary and is the
// single number this version is really testing.
const DETAIL_Z = 15;
// Text sizes chosen from the arcminute arithmetic at 126 um pitch, not from a 1:1
// render on a monitor (which is a 2x magnification -- see finding 29). 11-12 px is
// ~8 arcmin, detection not reading; these are the marginal band. Still not
// "comfortable" (~28 px), because that costs a fifth of the panel per label.
const ROAD_LABEL_PX = 22;
const PLACE_LABEL_PX = 26;

// slot -> ABGR2222 byte (spec § 3)
const BYTE = {
  paper: 0xFF, landuse: 0xEE, wood_lt: 0xDD, building: 0xEA, wood: 0xD8,
  water: 0xF4, contour: 0xC5, water_dk: 0xF0, trace: 0xC3,
  road_minor: 0xC1, path: 0xD0, road_major: 0xC0, ink: 0xC0, halo: 0xFF,
  // NO rail slot. 0xD5 was tried and withdrawn: measured at 3.19 % of a rural
  // viewport and 5.28 % of an urban one with nothing declaring it, it is where
  // antialiasing already lands, so rail drawn there is confusable with the fringe
  // around every label and road edge. Rail still needs a code -- see the
  // investigation's findings -- but it must be one antialiasing does not produce.
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
// Toronto: every street has a sidewalk each side and a crossing at every corner,
// all arriving as kind `path` with these kind_details. Drawn, they form a mesh
// denser than the road network they sit under. Excluded by kind_detail, so real
// footpaths and cycleways survive.
const FOOT_FURNITURE = ['sidewalk', 'crossing', 'corridor', 'alley'];

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
  { ...fill('buildings', 'buildings', ['building'], C.building), minzoom: DETAIL_Z },

  // ---- Road casings, ALL of them, before ANY road fill ----------------------
  // § 4 inverts the usual convention: the road is the dark thing and the casing is a
  // `paper` halo, so it reads as a gap rather than an outline. Casings must be a
  // separate pass or a casing overdraws the adjacent road it should be separating.
  line('road_major_casing', 'roads', MAJOR, C.halo, 7),
  { ...line('road_minor_casing', 'roads', MINOR, C.halo, 5), minzoom: DETAIL_Z },

  // ---- Road fills ----------------------------------------------------------
  // R1: the three dark codes are a budget. 0xC0 is the road you are on and label text;
  // 0xC1 warm = vehicular, 0xD0 cool = foot. They differ by hue, not weight.
  line('road_major', 'roads', MAJOR, C.road_major, 4),
  { ...line('road_minor', 'roads', MINOR, C.road_minor, 2), minzoom: DETAIL_Z },
  {
    id: 'road_path', type: 'line', source: SOURCE, 'source-layer': 'roads',
    minzoom: DETAIL_Z,
    filter: ['all',
      ['in', ['get', 'kind'], ['literal', FOOT]],
      ['!', ['in', ['get', 'kind_detail'], ['literal', FOOT_FURNITURE]]],
    ],
    layout: { 'line-cap': 'butt', 'line-join': 'miter' },
    paint: { 'line-color': C.path, 'line-width': 2, 'line-dasharray': [3, 3] },
  },

  // ---- Labels --------------------------------------------------------------
  // § 4: 11-12 px, `ink`, 1 px `halo` ring. Only `Noto Sans Regular` exists in the
  // renderer image, so the spec's "bold" is not currently achievable -- recorded as a
  // gap rather than silently substituted.
  {
    id: 'label_place', type: 'symbol', source: SOURCE, 'source-layer': 'places',
    filter: ['==', ['geometry-type'], 'Point'],
    layout: {
      'text-field': ['get', 'name'], 'text-font': ['Noto Sans Regular'],
      'text-size': PLACE_LABEL_PX, 'text-max-width': 7, 'text-padding': 24,
    },
    paint: { 'text-color': C.ink, 'text-halo-color': C.halo, 'text-halo-width': 1 },
  },
  {
    // Named roads only from DETAIL_Z, majors only, and placed as points rather
    // than along the line: `symbol-placement: line` rotates text to the street
    // angle, and angled strokes rasterise worst at 126 um with no antialiasing to
    // soften them. Offset above the anchor so the road stroke does not run through
    // the glyphs -- the one thing the Garmin comparison shows unambiguously.
    id: 'label_road', type: 'symbol', source: SOURCE, 'source-layer': 'roads',
    minzoom: DETAIL_Z,
    filter: ['in', ['get', 'kind'], ['literal', MAJOR]],
    layout: {
      'text-field': ['get', 'name'], 'text-font': ['Noto Sans Regular'],
      'text-size': ROAD_LABEL_PX, 'symbol-placement': 'point',
      'text-anchor': 'bottom', 'text-offset': [0, -0.35],
      'text-padding': 28, 'text-max-width': 9, 'symbol-avoid-edges': true,
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
    'una:detail_z': DETAIL_Z,
    'una:deviations': 'path furniture excluded; minors/paths/buildings withheld below DETAIL_Z (§ 7 silent on per-zoom feature selection); labels horizontal+offset+22-26px against § 4\'s 11-12px along-line; minor-road names dropped; rail unstyled, § 3 has no slot',
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
