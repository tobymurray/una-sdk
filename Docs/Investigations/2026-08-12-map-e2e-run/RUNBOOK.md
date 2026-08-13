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

**Use `scripts/gen-watch-style-v4.js`. It is the current style and the only one that produces a
readable map.** It needs no `npm install` and no theme package — it emits the palette directly.

```sh
mkdir -p ~/maps/styles/watch
node gen-watch-style-v4.js ~/maps/styles/watch/style.json
# expect: layers=16 colours used=10 all legal slots=yes
```

That `all legal slots=yes` is the point: every colour it emits is one of the panel's 64, derived
from its `ABGR2222` byte rather than typed, and the script throws if an off-palette literal survives
into the output. Quantisation is then close to a no-op instead of a lottery.

What v4 does, and why each part is not optional:

- **Only the 14 slots** of `MAP_CARTOGRAPHY_SPEC.md` § 3. A phone-oriented theme quantised to 64
  colours puts road fill and ground fill on the *same slot* and the road network vanishes entirely.
- **Detail is withheld below z15** — no minor roads, paths or building fills at the wide zooms. Map
  legibility is U-shaped in zoom, and drawing everything everywhere makes the middle zooms
  unreadable texture. This is cartographic *generalization*; OSM Carto shipped the same fix in 2015.
- **Labels at 22–26 px, horizontal, offset off the line, majors only.** At this pixel pitch 11–12 px
  text is detectable but not readable.
- **Every `text-font` rewritten to `Noto Sans Regular`** — the only stack in the renderer image. Any
  other stack means those labels silently do not draw.

### The other generators beside this runbook, and why you do not want them

`gen-protomaps-style.js` renders the **stock Protomaps light theme** and exists to document what
goes wrong: quantised to the panel it is a white screen with faint marks, roads invisible. If you
run it, note that it passes the theme **object** (`namedTheme('light')`) rather than the string
`'light'` — passing the string silently emits `null` colours and tileserver-gl then rejects the
whole style with `layers[2].paint.fill-color[2]: Expected color but found null instead`, an error
pointing nowhere near the cause. `gen-watch-style.js` and `-v3.js` are earlier palette-locked
versions kept for A/B comparison. **v4 is the one to use.**

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

The map should look like a map: dark arterial roads on a pale ground, water in blue, one or two
large place names. If it is a pale wash with barely visible roads, you are running the stock theme
from step 6's warning rather than v4.

---

## Step 9 — Build slippypack

The work you want is spread across **three unmerged branches**, so build a worktree that has all of
them. Nothing is merged into `main` yet, and `main` itself is well behind.

```sh
cd ~/git/rust/slippypack
git fetch origin
git worktree add /tmp/wt-slippy origin/map-delivery-workflow
cd /tmp/wt-slippy
git switch -c local/mvp
git merge --no-edit origin/fix/no-loopback-rate-limit origin/feat/tile-dim
cargo build --release -p slippypack-cli     # ~30 s; toolchain 1.95.0 is pinned
```

What each branch gives you, and what you lose without it:

| branch | what it is for |
|---|---|
| `map-delivery-workflow` | the base — the audit documents and the CLI as it stood |
| `fix/no-loopback-rate-limit` | stops the 4 req/s limiter applying to your own machine. **Without it every build takes 172 s instead of 3 s** |
| `feat/tile-dim` | `--tile-dim`, plus the check that the header's tile size matches the tiles. Only needed if you build at anything other than 256 px |

The binary lands in `target/release/slippypack` (or wherever `CARGO_TARGET_DIR` points).

**If the merge conflicts**, the branches are independent — take both sides. If you would rather not
merge, build from `origin/fix/no-loopback-rate-limit` alone and pass `--rate-per-sec 10000` on every
`make`; you then simply have no `--tile-dim`.

## Step 10 — Build the pack

```sh
SP=/tmp/wt-slippy/target/release/slippypack
cd ~/maps
$SP make \
  --source 'http://localhost:8081/styles/watch/{z}/{x}/{y}.png' \
  --out athens.rawtiles \
  --bbox=-76.015,44.590,-75.889,44.662 \
  --zoom 12-16 \
  --compression none \
  --attribution "© OpenStreetMap contributors"
```

Four things about that command:

- **No `--rate-per-sec` is needed** if you merged `fix/no-loopback-rate-limit` in step 9 — verified
  at **3 s** for this bbox. If you skipped that branch, add `--rate-per-sec 10000` or the same build
  takes **172 s**, because the 4 req/s limiter meant for other people's servers gets applied to your
  own machine.
- **`--compression none` is required.** The reader vendored into the watch app fails closed on
  RLE, so a compressed pack will not open on the device.
- **`--attribution` is required and must say this.** The data is OSM-derived under ODbL.
- **It prints nothing while it works.** A three-minute silent run looks like a hang. It isn't.
- **Do not touch the renderer while it runs.** Restarting the container mid-build fails the
  build with `HTTP transport error: io: Connection refused`. It aborts cleanly and leaves no
  partial file, so just start over.

Check what you got:

```sh
$SP inspect athens.rawtiles
```

Expect 687 tiles for this bbox — z12: 6, z13: 16, z14: 42, z15: 143, z16: 480 — and a size of
45,037,308 bytes. That size is fixed by tile count alone: uncompressed ABGR2222 is exactly
65,536 bytes per tile no matter what the map looks like.

**Two things in that output are misleading, and neither is your fault:**

- `build_timestamp` will show roughly when you started the *renderer*, not when the map data
  was current. tileserver-gl sends its process start time as `Last-Modified` and the writer
  believes it. If you care, pass `--timestamp` with the epoch seconds of the
  `planetiler:osm:osmosisreplicationtime` value from step 5.
- `pack_uuid` does **not** identify these bytes. Rebuild after editing your style and you get
  the same UUID over a different map. Do not use it as a cache key.

Also expect that **two builds from identical inputs are not byte-identical.** Renders differ by
a pixel or two on antialiasing ties. If you are diffing packs, a handful of differing bytes plus
a changed footer CRC is normal here; it is not corruption.

---

## Step 11 — Validate the pack with something that isn't slippypack

You do **not** need to clone the `rawtiles` repo for this. The independent validator ships inside
slippypack:

```sh
cd /tmp/wt-slippy/spec-validator-cpp
make                     # needs only a C++17 compiler, no libraries
./build/rawtiles_validate ~/maps/athens.rawtiles
```

Expect:

```
OK  <uuid>
    version 1.0   tile_dim_px 256   zoom_range 12..16
    tile_count 687   file_size 45037308 bytes   crc32 0x...
```

Exit 0 means well-formed. It re-derives the byte layout from the spec and calls no slippypack
code, so it catches writer bugs that the Rust writer and reader would agree on and therefore miss
between themselves. If it rejects your pack, believe it and do not deploy — a malformed pack on
the watch gives you nothing to debug with.

**Do not over-read a green result.** Specifically, "OK" does *not* mean:

- **that the pixels are right.** Change a pixel, repair the footer CRC, and the pack still
  validates. Nothing in the file says what the map should look like.
- **that `tile_dim_px` is right.** It is only checked for being greater than zero. A pack
  claiming 128 px tiles while containing 256 px ones validates clean, and `inspect` will repeat
  the wrong number back to you. If you ever build with a non-default tile size, verify by hand
  that bytes-per-tile equals `tile_dim_px²`.
- **that `pack_uuid` is genuine.** Any non-zero value in that header field is accepted and
  echoed as the pack's identity. It is not derived from or checked against the bytes.

What it *does* prove is real and worth having: magic, offsets, the extension walk, per-zoom index
counts against entries actually present, and the whole-file CRC.

---

## Before you connect the watch — read this once

**Do not write to the watch's filesystem while its BLE sync is running.** USB mass-storage
writes from the host and the watch's own BLE sync collide on the same exFAT partition and
**will corrupt files** — not the one you are writing, potentially the partition. Nothing in the
steps below can detect that state for you, which is exactly why card `E2` exists: a deploy tool
that refuses while sync is active. Until it exists, this is a rule you follow by hand.

The other thing to know before you start: **the watch cannot read its own filesystem while your
host has the volume mounted.** So testing on-watch always means unmounting first, and a pack that
"does not load" is very often a pack on a still-mounted volume.

## Step 12 — Mount the watch

Connect the watch's **own data cable** (not the Dev Tool cable — that is debug UART only and
cannot carry files). It enumerates as USB mass storage: `/dev/sda1`, exFAT, label `UNA WATCH`,
3.3 GB.

It arrives **unmounted**, so mount it explicitly:

```sh
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT /dev/sda
udisksctl mount -b /dev/sda1
# → Mounted /dev/sda1 at /run/media/toby/UNA WATCH
WATCH="/run/media/toby/UNA WATCH"
```

If `lsblk` shows a different device, use that — do not assume `sda1`.

## Step 13 — Put the pack where the app actually looks

The app reads a **sandbox-relative** path. On branch `poc/athensrun`,
`Examples/Apps/AthensRun/Software/Libs/Header/MapPackPaths.hpp` declares exactly one candidate:

```
../SharedData/maps/athens.rawtiles
```

That is the **shared MapManager directory**, deliberately not a copy private to AthensRun, so
every app reads the same already-verified location. Two consequences for you:

- The filename is fixed. The app does not scan for packs; it opens that name or nothing.
- The volume-absolute location is wherever `../SharedData/maps/` resolves to from the app's own
  directory on the volume. Find it rather than guessing:

```sh
find "$WATCH" -maxdepth 3 -type d -name SharedData
mkdir -p "<that>/maps"
MAPS="<that>/maps"
```

Then copy, and let the copy finish before you do anything else:

```sh
cp ~/maps/athens.rawtiles "$MAPS/athens.rawtiles"
sync
```

**If a pack is already there, move it aside rather than deleting it** — the old one is the only
thing you can fall back to if the new one is bad. A pre-effort pack from the OSM-CDN era was set
aside as `athens.rawtiles.old-preeffort` on this run. Watch the free space if you do that: those
older packs run to hundreds of MB on a 3.3 GB volume.

## Step 14 — Publish the trust marker, or the app will ignore the pack

This is the step nobody guesses. The app does not trust a pack because it parses; it trusts a
pack because a **trust marker** beside it says a full CRC-32 scan passed. The marker is
`<pack>.trust`, 16 bytes, fixed layout, little-endian — see
`Examples/Apps/AthensRun/Software/Libs/Header/MapPackTrustMarker.hpp` on `poc/athensrun`:

| bytes | field |
|---|---|
| `[0..3]` | magic — `MPT1` = Good, `MPTX` = known bad |
| `[4..11]` | pack size, u64 |
| `[12..15]` | the pack's declared footer CRC-32, u32 |

Normally the separate MapManager app writes this in the background after scanning. A
hand-installed pack has nobody to write it, so **you write it or the pack is dead weight** —
present, well-formed, and ignored, with nothing in the filesystem to suggest why.

The CRC is the one `rawtiles_validate` printed in step 11 for *this* pack. Take it from there;
do not reuse a value from another build.

```sh
SIZE=$(stat -c%s "$MAPS/athens.rawtiles")
CRC=0x9c01a4d4     # ← the crc32 rawtiles_validate printed for THIS pack

python3 - "$MAPS/athens.rawtiles" "$SIZE" "$CRC" <<'EOF'
import struct, sys
pack, size, crc = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 16)
with open(pack + '.trust', 'wb') as f:
    f.write(b'MPT1' + struct.pack('<QI', size, crc))
EOF
sync
ls -l "$MAPS/athens.rawtiles.trust"    # expect exactly 16 bytes
```

**The marker binds to `(size, CRC)`.** So the failure mode that costs an afternoon is:
**replace the pack and leave the old marker in place.** The app then sees a size/CRC mismatch and
declines the pack it was just given. Every time the pack changes, rewrite the marker.

## Step 15 — Verify the bytes on the watch, not the bytes you sent

Three checks, and they are independent of each other. Do all three:

```sh
sha256sum ~/maps/athens.rawtiles "$MAPS/athens.rawtiles"     # expect identical digests

/tmp/wt-slippy/spec-validator-cpp/build/rawtiles_validate "$MAPS/athens.rawtiles"
# expect: OK  <uuid> ... — run against the file ON THE WATCH, not your local copy
```

and confirm the marker you just wrote agrees with the pack:

```sh
python3 - "$MAPS/athens.rawtiles" <<'EOF'
import struct, sys, os
p = sys.argv[1]
m = open(p + '.trust','rb').read()
magic, size, crc = m[:4], *struct.unpack('<QI', m[4:16])
print(magic, size, hex(crc), 'size matches:', size == os.path.getsize(p))
EOF
```

Validating off the watch rather than off a copy is the point: it exercises the write, the
filesystem and the read path in one go. A digest match alone would not tell you the file is
readable in place.

## Step 16 — Unmount before you test. This is required, not tidy.

```sh
sync
udisksctl unmount -b /dev/sda1
```

While the host holds the partition, the watch **cannot read its own filesystem**, so the app
will find no pack. Unmount, disconnect, then launch the app.

## Step 17 — Swapping packs by rename

The app opens exactly one filename, so keep several packs on the volume and make one live by
renaming. Rename the marker with it — the marker's name is derived from the pack's:

```sh
mv "$MAPS/athens.rawtiles"        "$MAPS/athens-rural.rawtiles"
mv "$MAPS/athens.rawtiles.trust"  "$MAPS/athens-rural.rawtiles.trust"
mv "$MAPS/athens-trails.rawtiles"       "$MAPS/athens.rawtiles"
mv "$MAPS/athens-trails.rawtiles.trust" "$MAPS/athens.rawtiles.trust"
sync && udisksctl unmount -b /dev/sda1
```

Because each pack keeps its own marker, a swap is two renames and no re-scan. Skip the marker
rename and you get the size/CRC mismatch from step 14.

**A pack of anywhere works on the bench.** The app centres the view on the *pack's* bbox centre
when it opens, so you can evaluate a Toronto pack in a field in Ontario. The corollary bites in
the field: once a real GPS fix arrives from outside the pack's bbox, the view pans off-pack and
goes **blank**. That is expected behaviour for a foreign pack, not a corrupt file.

---

## Step 18 — Look at it on the watch

**Unplug the watch.** It cannot read its own filesystem while a host holds it as mass storage, so
an unmount alone is not enough to be sure.

**Stay somewhere with no GPS fix** — indoors, away from a window. The map centres on the pack until
a fix arrives; the moment one does, the view jumps to where you actually are, and if that is outside
the pack you get an off-coverage screen instead of a map. This is the single thing that most often
makes people think the pack is broken.

1. Launch **AthensRun** from the app launcher.
2. Select **Start**. With no fix this routes to a start-confirmation screen — confirm through it.
   (With a fix it goes straight in, which is what you do *not* want for bench evaluation.)
3. You land on the track screen. **Swipe through the faces to the last one**: Total → Lap → Status →
   **Map**. The scroll indicator shows where you are.
4. **R2 — the amber button — cycles the zoom** through z12–z16. It is the only zoom control; all
   other buttons were already spoken for.

You should see the map immediately, centred on the pack. If instead the face shows an error or a
"validating" state, the trust marker from step 14 is wrong — recheck its size and CRC against the
pack, since nothing else in the pipeline reports that mismatch.

Discard the activity rather than saving it when you are done, unless you want the FIT file.

### Judging what you see

Two traps make this harder than it looks, and both have caught this project:

- **Judge on the panel, not on a photograph.** A close-up photo resolves detail your eye at glance
  distance does not, and a "1:1" render on a monitor is physically about **twice** the size, because
  a monitor's pixel pitch is roughly 0.25 mm against this panel's 0.126 mm. Text that is perfectly
  legible in both can be unreadable on the wrist.
- **Judge with the backlight off.** Backlit, the whole panel reads strongly blue and nominal white
  looks like light cyan. Reflective is the panel's normal mode and the one the palette is designed
  against.

---

## Step 19 onward — **not yet verified. Stop here.**

| next | what happens |
|---|---|
| daylight | the reflective panel in direct sun. Untested — and it is the case that stresses the palette's contrast floors hardest |
| a bare watch | this runbook assumes AthensRun is **already installed**. Building and installing the app from `poc/athensrun` is not written up anywhere |

Update this file as each link lands, and keep the "verified on" date at the top honest.
