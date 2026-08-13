# Runbook — from nothing to a map pack, step by step

Follow this top to bottom. Every command here was **run and verified on 2026-08-12**; the
expected output is shown so you can tell whether your run matches. Where a step has not been
verified yet, it says so in bold and you should stop rather than improvise.

**Do not point any of this at a public tile server.** Not `tile.openstreetmap.org`, not
MapTiler Cloud, not "just a few tiles to test". Both prohibit building offline archives, and
the prohibition is on the purpose, not the volume — see `MAP_COMPLIANCE_APPENDIX.md` §§ 2.1–2.2
(proxied in `Docs/External/slippypack/docs/`). This runbook downloads *vector data* through a
bulk channel published for it, then renders raster tiles on your own machine. That difference
is the entire reason the workflow looks like this.

## What you need before starting

- Linux x86_64 (this was run on Arch, kernel 7.1.4).
- `docker`, working without `sudo`. Check: `docker info` prints a server version.
- `~/.local/bin` on your `PATH`. Check: `echo $PATH | tr ':' '\n' | grep local/bin`.
- About 2 GB free disk: the tileserver-gl image is 1.17 GB, the region extract is a few MB.
  You do **not** need room for the planet — nothing here downloads it.
- `curl`, `tar`, `sha256sum`.

Nothing here needs an account, an API key, or a signup.

---

## Step 1 — Install the `pmtiles` CLI

This is the tool that downloads map data. It reads *ranges* out of a remote archive, so you
get one region instead of a 120 GB planet file.

```sh
cd /tmp
curl -sSL -o pmtiles.tar.gz \
  https://github.com/protomaps/go-pmtiles/releases/download/v1.31.2/go-pmtiles_1.31.2_Linux_x86_64.tar.gz

# Verify you got the same bytes this runbook was written against:
sha256sum pmtiles.tar.gz
# expect: 3ed7dbf4ec2e6dfe5e25b6f70d1ffc932729f93c86db353bf514dd71010a312f

tar xzf pmtiles.tar.gz pmtiles
install -m755 pmtiles ~/.local/bin/pmtiles
```

Confirm it runs:

```sh
pmtiles version
# expect: pmtiles 1.31.2, commit a3e4951ea6a0477b784c27c1dcbfd9c130878c5a, built at 2026-07-22T18:59:03Z
```

The installed binary should be
`a7e9ae10184d109c83f456ccdf6df4f3e2a64ba6cf69d9ed0f9f1840305055c1` (`sha256sum ~/.local/bin/pmtiles`).

A newer release than v1.31.2 is probably fine, but then your output will not match the numbers
below, so note the version you used.

## Step 2 — Install the renderer

`tileserver-gl` turns vector data into raster PNG tiles on your own machine. It is BSD-3
licensed open source. The docker image happens to be published by MapTiler the company; this
is **not** MapTiler Cloud and none of MapTiler's data terms apply to running it against your
own data.

```sh
docker pull maptiler/tileserver-gl:latest
```

Verified against digest
`sha256:3a9ccdb24820b6814c8119bcc8a4376c39867cb0ffe69d62919ef898b90c2427` (1.17 GB, image built
about four months before this run). Pin that digest if you want a reproducible render — the
renderer version affects output pixels, which matters for pack identity.

```sh
docker images maptiler/tileserver-gl
```

## Step 3 — Pick a planet build date, and check it is still there

Protomaps publishes a daily planet build at `https://build.protomaps.com/YYYYMMDD.pmtiles`.
**Retention is short — roughly a week.** Verified on 2026-08-12: that day and the two before it
answered; a week earlier was already gone.

Check a date before relying on it. A range request returning **206** means it exists and
supports the ranged reads step 4 needs; **404** means pick a newer date.

```sh
DATE=20260812   # ← change to a recent date
curl -sS -o /dev/null -w '%{http_code}\n' -r 0-0 "https://build.protomaps.com/$DATE.pmtiles"
# expect: 206
```

Write down the date you used. A runbook that says "use the latest" is useless six weeks later,
and the data in each build is different.

## Step 4 — Extract your region

The bbox is `minLon,minLat,maxLon,maxLat` in decimal degrees. The values below are Athens,
Ontario — about 10 × 8 km around the village centre (44.6259 N, −75.9523 W) — matching the
existing PoC pack so results are comparable.

```sh
mkdir -p ~/maps
cd ~/maps
pmtiles extract "https://build.protomaps.com/$DATE.pmtiles" athens.pmtiles \
  --bbox=-76.015,44.590,-75.889,44.662
```

**Keep this outside any git repository.** `~/maps/` is the convention here; the una-sdk repo
ignores `*.pmtiles` precisely so a stray `git add` cannot swallow one.

Expected output, and roughly what you should see:

```
extract.go:441: Region tiles 230, result tile entries 230
extract.go:606: Completed in 5.980881628s with 4 download threads
extract.go:612: Extract transferred 2.3 MB (overfetch 0.05) for an archive size of 2.3 MB
```

Six seconds, 2.3 MB transferred. If you see hundreds of megabytes moving, your bbox is far
larger than you think — stop and check the sign of your longitudes.

To pick your own area: get a bbox from <https://boundingbox.klokantech.com/> (choose the
"CSV" format, which is already `minLon,minLat,maxLon,maxLat`). Expect cost to scale with area,
not with how many zooms you ask for.

## Step 5 — Check what you got

```sh
ls -la ~/maps/athens.pmtiles
# expect: 2,255,635 bytes

sha256sum ~/maps/athens.pmtiles
# expect: 23975d648ace2c5360e67f50cba088429fac794ff141f5215eec2ed088312733
#         (only if you used DATE=20260812 — a different build gives different bytes)

pmtiles show ~/maps/athens.pmtiles
```

In that output, four lines matter:

| line | what it tells you |
|---|---|
| `tile type: mvt` | you have **vector** tiles. Raster does not exist yet; step 6 makes it |
| `max zoom: 15` | the data stops at z15. Raster z16 is possible but it is z15 geometry drawn bigger, not extra detail |
| `planetiler:osm:osmosisreplicationtime` | when the underlying OSM data was current — `2026-08-12T04:00:00Z` here. This is the real freshness of your map |
| `attribution` | `© OpenStreetMap` as **HTML**. The pack needs plain text `© OpenStreetMap contributors`; do not copy the markup |

If `tile type` says anything other than `mvt`, you are holding a different kind of archive and
the rest of this workflow does not apply.

---

## Step 6 — Build a style for your renderer

**Do not skip this and just point tileserver-gl at the file.** It will start, log
`WARN: PMTiles not in "openmaptiles" format. Serving raw data only...`, serve vector tiles
happily, and return **404 for every raster tile**. It looks like it is working. It is not
rendering.

The reason: tileserver-gl ships a style for the *OpenMapTiles* schema, and the Protomaps
basemap uses a different one. Rendering needs a style written for Protomaps.

```sh
cd /tmp
mkdir -p styleboot && cd styleboot
npm install protomaps-themes-base@^4      # 4.5.0 was used here
```

Copy `scripts/gen-protomaps-style.js` (beside this runbook) into that directory, then:

```sh
mkdir -p ~/maps/styles/watch
node gen-protomaps-style.js light ~/maps/styles/watch/style.json
# expect: theme=light layers=68 font-stacks-remapped=11
```

Two things that script does which you must not drop:

- It passes the theme **object** (`namedTheme('light')`), not the string `'light'`. Passing the
  string silently produces `null` colours, and tileserver-gl then rejects the style with
  `layers[2].paint.fill-color[2]: Expected color but found null instead` — an error that points
  nowhere near the cause.
- It rewrites every `text-font` to `Noto Sans Regular`. That is the **only** font stack in the
  image; any other stack means those labels silently do not draw.

## Step 7 — Give the renderer its fonts and config

The fonts live inside the image, so copy them out into the directory you mount:

```sh
docker rm -f tsgl 2>/dev/null
docker run -d -p 8081:8080 -v ~/maps:/data --name tsgl \
  maptiler/tileserver-gl:latest --file /data/athens.pmtiles
sleep 10
mkdir -p ~/maps/fonts
docker cp 'tsgl:/usr/src/app/node_modules/tileserver-gl-styles/fonts/Noto Sans Regular' ~/maps/fonts/
docker rm -f tsgl
```

Copy `scripts/tileserver-config.json` (beside this runbook) to `~/maps/config.json`. Your
`~/maps` should now look like:

```
~/maps/
├── athens.pmtiles
├── config.json
├── fonts/Noto Sans Regular/*.pbf
└── styles/watch/style.json
```

## Step 8 — Start the renderer and fetch a raster tile

Port 8080 was taken on the machine this was written on, so everything here uses **8081** on the
host. Change it if you like; keep the container side at 8080.

```sh
docker run -d -p 8081:8080 -v ~/maps:/data --name tsgl \
  maptiler/tileserver-gl:latest -c /data/config.json
sleep 15
docker logs tsgl | grep -i "not a valid"   # expect: no output
curl -sS http://localhost:8081/styles.json
# expect: one style, "name": "protomaps-light", "id": "watch"
```

If `docker logs` prints `is not a valid style file`, fix the style before going further — the
server keeps running with **no styles loaded**, which fails as a 404 later and looks unrelated.

Now the tile that proves it:

```sh
curl -o ~/maps/athens_z14_render.png \
  http://localhost:8081/styles/watch/14/4735/5917.png
file ~/maps/athens_z14_render.png
# expect: PNG image data, 256 x 256, 8-bit/color RGBA
```

Open it. You should see the village of Athens with roads, buildings, green space and the label
"Athens" — compare against `images/athens_z14_render.png` beside this runbook. If your PNG is a
uniform colour, the style loaded but matched no data: check that the style's source name matches
the `data` key in `config.json`.

To find the tile numbers for your own area at a given zoom:

```sh
python3 -c "
import math; lat,lon,z = 44.6259,-75.9523,14
n=2**z; lr=math.radians(lat)
print(int((lon+180)/360*n), int((1-math.log(math.tan(lr)+1/math.cos(lr))/math.pi)/2*n))"
```

Note the map you get is the stock Protomaps *light* theme: pale, thin, and designed for a phone
screen. It is deliberately not the watch cartography — that is a separate piece of work
(`MAP_CARTOGRAPHY_SPEC.md`). It is good enough to prove the pipeline.

---

## Step 9 onward — **not yet verified. Stop here.**

Everything above was run end to end. What follows has not been, so it is not written as
instructions yet — see the investigation README's L3–L6 sections for the hypotheses and the
known traps.

In outline, so you know where you are:

| next | what happens |
|---|---|
| L3 | `slippypack make --source 'http://localhost:8081/styles/watch/{z}/{x}/{y}.png'` → a `.rawtiles` pack. Needs `--compression none` |
| L4 | validate the pack with an independent reader (`spec-validator-cpp`) |
| L5 | copy to the watch over USB mass storage — **never while BLE sync is running**, or you will corrupt the partition |
| L6 | open it in the AthensRun app on the watch |

Update this file as each link lands, and keep the "verified on" date at the top honest.
