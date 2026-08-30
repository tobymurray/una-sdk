// Generate a tileserver-gl-compatible MapLibre style for the Protomaps basemap
// schema. Two adjustments are needed beyond calling the theme package:
//   - source url uses tileserver-gl's local reference form, not an http URL
//   - every font stack is remapped to "Noto Sans Regular", the only stack the
//     tileserver-gl image ships. Unresolvable stacks render without labels.
const fs = require('fs');
const base = require('protomaps-themes-base');

const SOURCE = 'protomaps';
const themeName = process.argv[2] || 'light';
// Must pass the theme OBJECT, not its name. layers(src, 'light', …) is accepted
// and silently emits null colours where it fails to resolve a key; MapLibre GL
// JS tolerates that, tileserver-gl's style validator rejects the style outright.
const theme = base.namedTheme(themeName);
const layers = base.layers(SOURCE, theme, { lang: 'en' });

let remapped = 0;
for (const layer of layers) {
  if (layer.layout && layer.layout['text-font']) {
    layer.layout['text-font'] = ['Noto Sans Regular'];
    remapped++;
  }
}

const style = {
  version: 8,
  name: `protomaps-${themeName}`,
  metadata: {
    'protomaps:theme': themeName,
    'protomaps-themes-base:version': require('protomaps-themes-base/package.json').version,
  },
  sources: { [SOURCE]: { type: 'vector', url: 'pmtiles://{pmtiles}' } },
  glyphs: '{fontstack}/{range}.pbf',
  layers,
};

fs.writeFileSync(process.argv[3], JSON.stringify(style, null, 2));
console.error(`theme=${themeName} layers=${layers.length} font-stacks-remapped=${remapped}`);
console.error('distinct text-font values now: ' + JSON.stringify([...new Set(layers.filter(l => l.layout && l.layout['text-font']).map(l => JSON.stringify(l.layout['text-font'])))]));
