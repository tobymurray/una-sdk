# Prompt: find the prior art for watch cartography before we re-derive any more of it

You are researching **the state of the art in map design for very small, very colour-limited,
round, reflective displays**, and writing a report. You are not writing code and not building
anything.

The context: a project has been deriving cartographic lessons empirically — building map packs,
putting them on a real watch, photographing the panel — and several findings turned out to be
re-derivations of published work. The purpose of this report is to stop that. **Find what is
already settled in the literature and in industry practice, so effort goes to what genuinely
needs measuring on this hardware.**

Treat everything in § 1–§ 3 as given: it was measured on the device, and you do not need to
re-establish it. Your job is § 4 onward.

---

## 0. Ground rules

- **Never fabricate a citation.** If you cannot fetch a source, say so and mark the claim
  unverified. A report with six real sources beats one with twenty plausible-looking ones.
- **Distinguish what you read from what you skimmed.** For every source, say whether you read the
  full text, an abstract, or only a search snippet. This matters more than coverage.
- Cite **URL + retrieval date** for everything. Quote sparingly and exactly.
- Prefer **primary sources** (papers, official design guidance, vendor documentation) over blog
  summaries of them. Where only a secondary source exists, say so.
- **Say when the literature disagrees with this project's findings.** That is the most valuable
  output. Do not smooth it over.
- **Say when you find nothing.** "Searched X, Y, Z; no published guidance found" is a real result
  and prevents the next person repeating the search.
- Never state counts of sources, pages or searches as a measure of effort. Name what is covered.

---

## 1. The hardware, as measured

**Display — Sharp LS012B7DD06A**
- 240 × 240 across 30.24 mm, so a **126 µm pixel pitch**.
- **Transflective memory-in-pixel LCD.** Holds its image at ~11 µW, so the normal viewing mode is
  **reflective, backlight off**. Contrast ratio 25:1.
- **The backlight pushes the whole panel strongly blue** — nominal white reads as light cyan. Any
  colour judgement must be made with it off.
- **64 colours native, `ABGR2222`** — two bits per channel, so each channel is one of
  {0, 85, 170, 255}. One byte per pixel.
- **The visible area is a circle inscribed in the 240 × 240 square.** The aperture is round, so the
  corners sit behind the bezel: about **21 %** of every rendered frame is never seen (only π/4 of a
  square inscribes).

**Measured palette limitations**
- Only **three of the 64 codes sit below L\* 40**, and there is **no neutral grey between L\* 23.7
  and L\* 66.5**.
- There is **no neutral light grey between channel levels 170 and 255**, so a *near-neutral* colour
  whose channels straddle a rounding boundary emerges **saturated** — a grey at (215, 213, 211)
  quantises to (255, 255, 170), a visible yellow. Nearest-in-3D picks the same slot; this is
  intrinsic to the cube, not a quantiser bug.
- Antialiasing between two legal colours lands **off-palette**. The mid-grey `0xD5` (85, 85, 85)
  occupies **3–5 % of a typical viewport** while being declared by nothing.

**Measured text legibility (this is the most useful number in the document)**
- At 126 µm pitch, text whose x-height is half the em subtends, at a 30 cm glance distance:
  **11–12 px ≈ 8 arcminutes** (detection, *not* reading — confirmed unreadable on the panel);
  20 px ≈ 14 arcmin; 24 px ≈ 17 arcmin; **28 px ≈ 20 arcmin** (comfortable, but only eight such
  lines fit across the panel). **22–26 px was confirmed readable on the device.**
- **Photographs and "1:1" desktop renders both overstate legibility.** A monitor's pixel pitch is
  roughly 0.25 mm against this panel's 0.126 mm, so a 1:1 render is a **2× magnification**; a
  close-up photograph is worse. Several confident judgements were wrong for exactly this reason.

## 2. The computation and delivery limits

- **App memory budgets: ~500 K service / ~600 K GUI** (from the linker). A widely-repeated
  "256 KB" figure is a stale diagram label.
- **Tile cache is the dominant memory cost.** A 256 px `ABGR2222` tile is 64 KiB, and a 240 × 240
  viewport can straddle four of them — 256 KiB, about 43 % of the GUI budget. At 128 px a tile is
  16 KiB and the nine-tile worst case is 144 KiB.
- A 64 KiB tile read from flash measured **7–9 ms** on hardware.
- **There is no rasteriser, no font engine and no styling budget on the device.** Map tiles are
  pre-rendered, blit-ready raster. Nothing about the map is computed on the watch except which
  tiles to blit and the activity's own GPS trace drawn over the top.
- **Delivery is USB mass storage.** BLE was measured at about **2.2 kB/s sustained**, so a 17 MB
  pack over BLE is hours, not minutes. Pack sizes seen: 17 MB for ~5.6 km² of dense city at
  z12–16; 45 MB for a 10 × 8 km rural area.
- Uncompressed tiles are exactly `tile_dim²` bytes. An RLE mode exists in the format but the
  device-side reader currently fails closed on it.

## 3. What has already been established empirically — do not re-derive

Each of these was measured or observed on the device. Your report should say, for each, **whether
the literature already knew it, and whether the literature disagrees.**

1. **Legibility is U-shaped in zoom.** With every feature class drawn at every zoom, the widest
   (~27 m/px) and closest (~1.7 m/px) zooms read well and **the middle collapses into undifferentiated
   texture**. Withholding minor roads, paths and building fills below a detail threshold turned the
   failing middle zooms into a readable arterial skeleton. (Suspected to be textbook *cartographic
   generalization* — confirm and cite properly.)
2. **The designated "default" zoom was the one that did not work.** The ladder was specified from
   ground-resolution arithmetic without a legibility trial.
3. **Hue-coding the more numerous feature class makes it dominant.** Minor roads in a warm
   saturated ink against major roads in neutral black: at urban density the minors are far more
   numerous, so the *subordinate* class reads as louder and the hierarchy inverts.
4. **Stock phone-oriented map themes are unusable here.** Quantised to 64 colours, a typical light
   theme collapses road fill and ground fill onto the same slot, so **the road network disappears
   entirely** — about 94 % of the viewport ends up above L\* 93.
5. **Label placement is an unsolved trade in this project.** Text placed *along* the line keeps
   association but rotates the glyphs, and angled strokes rasterise worst at this pitch with no
   antialiasing to soften them. Text placed as an offset *point* rasterises cleanly but reads as
   floating near a road rather than naming it. A competitor (Garmin) appears to offset
   **perpendicular** to the line and stay adjacent, which a static style sheet cannot express
   per-feature.
6. **The round aperture clips labels** the renderer happily placed, because the renderer's
   placement and collision logic works on the square framebuffer.
7. **The application's own status overlay collides with map labels**, because two independent text
   systems share a framebuffer with nothing arbitrating between them.
8. **The palette has no slot for railways**, and a city has railways.
9. **Slot selection needs a criterion beyond colour separability**: a code that antialiasing
   commonly produces is a bad choice for a feature class, because the feature becomes confusable
   with fringe artefacts.
10. **Rendering is not byte-reproducible.** Identical inputs through the same renderer produced
    several distinct outputs across repeated builds, differing by one or two pixels, from
    antialiasing coverage ties. Palette-first styling reduced the *quantiser's exposure* to that
    jitter roughly tenfold without removing the jitter.

## 4. What to research

Weight your effort toward **(a) well-respected UX and cartographic design guidance** and
**(b) academic work that reframes the problem rather than tuning it.** The project has plenty of
tuning ideas already; what it lacks is knowledge of which approaches are known-good and which
known-dead.

### 4.1 Cartography for small and low-fidelity displays
- **Cartographic generalization**: per-zoom feature selection, road-class thresholds, when to drop
  versus simplify versus aggregate. Establish the standard vocabulary for what finding 1 above
  rediscovered, and find the canonical references.
- **Legibility limits of cartographic symbols on small displays** — minimum discriminable sizes,
  line widths, and separations. There is at least one direct paper on this; find it and extract the
  actual numbers, then compare them with § 1's arcminute figures.
- **Mobile-first and responsive cartographic design** — there is a recent research agenda paper in
  this area; summarise what it considers open.

### 4.2 The labelling problem specifically
- **External / boundary labelling with leader lines** — labels placed at the map edge and connected
  to features. This plausibly solves association, rotation and round-edge clipping together. Find
  the algorithmic literature and any evaluation of whether users prefer it.
- Label placement under **rotation constraints** (upright-only text), and evidence about rotated
  versus horizontal text legibility at low resolution.
- How label *density* should scale with screen size and viewing time, for a user who is moving.

### 4.3 Reframings — the highest-value category
- **Route linearisation**: at least one system turns a route into a 1-D strip for smartwatches and
  reportedly beats both 2-D maps and turn-by-turn for pedestrian navigation. Find it, find its
  evaluation, find any replications or criticisms. **If this holds, it challenges the premise that
  a 2-D map is the right artifact at all**, and that conclusion is worth more than any amount of
  style tuning.
- Other non-map spatial displays for wearables: schematic/beak/compass-style guidance, haptic
  wayfinding, landmark-based instructions.
- Anything from **outside consumer wearables** that solves glanceable spatial display under hard
  constraints: aviation moving maps and their certification-driven symbology minimums, marine
  chart display standards, automotive HUD guidance, tactical/military symbology at low resolution.
  These fields have decades of formal minimums; extract any that translate.

### 4.4 The nearest hardware precedent
- **Pebble** used the same 2-bits-per-channel, 64-colour constraint, and one of its models was
  **round**. Find its published palette material, design guidance, dithering libraries and
  device-accurate preview tooling, and its round-display guidance. Note what it recommends about
  dithering versus flat fills — this project currently assumes flat.
- Any other low-bit-depth map rendering practice: e-ink navigation devices, e-ink readers,
  low-colour embedded displays.

### 4.5 Established UX guidance to check against
- Platform guidance for **round watch displays** and **glanceability** (Wear OS, watchOS and
  similar) — specifically what they say about safe areas on circular screens, minimum text sizes,
  and glance duration.
- Contrast standards (WCAG and successors) and **whether they apply meaningfully to a reflective
  display** whose contrast depends on ambient light. This project's palette work computes contrast
  ratios against a nominal white; say whether that is sound practice for transflective panels.

### 4.6 Vendor practice
- Whether **Garmin, Suunto, Coros, Wahoo** or similar have published anything substantive about
  their map rendering, palette or label strategy. Expectation: very little, and their knowledge is
  proprietary. **Confirm or refute that**, because if it is true it changes where effort should go —
  and if some of it is documented, that is a large win.

## 5. Open questions to answer if the literature can

List each with what you found, or state plainly that the literature does not settle it.

1. **Is a 2-D map the right artifact for a running watch at all**, or does a linearised or
   schematic display serve the actual task better?
2. **Which path/footway classes deserve ink**, at which zooms? Dense cities produce a sidewalk and
   crossing mesh that overwhelms the road network.
3. **What is the right treatment for railways** in a palette with no spare dark neutral?
4. **Aliased or antialiased text** at 126 µm with 64 colours — is there evidence either way?
5. **Is dithering worth it** at 2 bits per channel for map fills, or does it read as noise at this
   pitch?
6. **How should a moving-map display reserve space for application chrome** so two text systems do
   not collide?
7. **What tile edge length** minimises cache pressure without wrecking the zoom ladder — is there
   published guidance on tile size for constrained clients?
8. **Does anyone specify legibility in angular terms** (arcminutes at a stated distance) rather than
   pixels? If so, that is the vocabulary this project should adopt, since pixel sizes are
   meaningless across pitches.
9. **Reflective-display palette design** — is there guidance on designing for ambient-dependent
   contrast, as opposed to emissive screens?

## 6. The report

Write **one markdown file** to `Docs/Research/2026-08-13-watch-cartography-prior-art.md` in the
repository, structured as:

1. **What the literature already settles** — the things this project should stop deriving, each with
   its citation and the standard terminology for it.
2. **Where the literature contradicts this project's findings** — the most important section.
3. **Reframings worth taking seriously**, with an honest assessment of the evidence behind each and
   what adopting it would cost.
4. **The open questions, answered or explicitly left open**, following § 5's numbering.
5. **Established guidance to conform to** — concrete numbers where they exist (minimum sizes, safe
   areas, contrast floors).
6. **What is genuinely novel to this hardware** and therefore still has to be measured here.
7. **Sources**, each with URL, retrieval date, and whether you read it in full, in abstract, or as a
   snippet.

Aim for something a person can act on in an afternoon. Prose over bullet soup where the reasoning
matters; tables where the numbers matter.

## 7. Committing

- Work on the **`research`** branch of `https://github.com/tobymurray/una-sdk`. Check it out, add
  only your new file under `Docs/Research/`, and commit.
- **Author and committer must be `Toby Murray <toby.murray@protonmail.com>`** — use
  `git -c user.email=toby.murray@protonmail.com -c user.name="Toby Murray" commit`.
- **No AI or assistant attribution anywhere** — no `Co-Authored-By`, no "generated with", nothing.
  This is a hard project rule.
- Commit message: terse subject, body mostly **why** rather than what.
- Push the branch so the work survives — a cloud session's disk does not. Do **not** open a pull
  request, do not comment on issues, and do not `@`-mention anyone.
- If you cannot access the repository, still produce the full report as your final message so the
  content is not lost, and say clearly that the commit failed.
