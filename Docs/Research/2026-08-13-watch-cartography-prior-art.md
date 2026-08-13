# Prior art for watch cartography

**Scope.** What is already settled in the cartographic, human-factors and standards
literature about map design for very small, colour-limited, round, reflective
displays — so that effort on this hardware goes to what genuinely still needs
measuring. The hardware facts, the computation and delivery limits, and the ten
empirical findings this project has already established are taken as given and are
not re-derived here.

**Read-depth discipline.** Every source in § 7 is marked *full text*, *part*,
*abstract only*, *snippet only*, or *not retrieved*. Several documents that would
have been useful (ISO 15008's requirement tables, RTCA DO-257A, SAE ARP5289,
MIL-STD-2525, Apple's watchOS HIG) could not be fetched; where a number from one of
those appears below it is explicitly flagged as unverified against its primary
source. Nothing here is cited from memory.

**Unit convention used throughout.** Angular sizes are given in arcminutes (′) at a
30 cm viewing distance, because pixel counts are meaningless across pitches. On this
panel, one pixel is 0.126 mm, which subtends **1.44′ at 30 cm**; the panel is
**202 ppi**. Conversions between the literature's millimetres and this panel's pixels
are my own arithmetic, not quotations.

---

## 1. What the literature already settles

### 1.1 Finding 1 is textbook cartographic generalization, and it has a vocabulary

The U-shaped legibility curve — everything drawn at every zoom, the extremes
readable, the middle collapsing into texture, fixed by withholding minor roads,
paths and building fills below a threshold — is the standard problem of
**multi-scale generalization**, and specifically of the operator called
**selection** (also *elimination* or *content* generalization). The canonical modern
reference is Roth, Brewer & Stryker (2011), *A typology of operators for maintaining
legible map designs at multiple scales*, which organises the operators into four
categories — **content, geometry, symbol, label** — attached to decision points on a
**ScaleMaster** diagram: an explicit chart of which feature classes are drawn, in
which representation, at which scales. That is precisely the artifact this project
built by hand after the failure. The historical anchor for *how many* features to
keep is Töpfer & Pillewizer's **Radical Law** (1966), `Nd = Ns·√(Ms/Md)`, with the
exponent varying by dimensionality of the feature (point / line / area) — a
selection budget, not a styling rule.

The distinction the vocabulary buys you is worth adopting verbatim: *selection*
(drop the class), *simplification* (fewer vertices), *aggregation* (merge into one
areal feature), *typification* (replace many with a representative pattern),
*symbol* operators (change the ink), *label* operators (change the text). This
project has so far used selection and symbol operators only. Aggregation and
typification are the two it has not tried, and both are strong candidates for the
sidewalk mesh problem (§ 4.2).

The industry version of the same finding is public and dated. OpenStreetMap Carto
pull request #1682, *Start rendering of minor roads later* (merged 2015-07-27), moved
`highway=residential` and `living_street` from **z10 to z13**, `highway=unclassified`
from **z10 to z11**, and buildings from **z12 to z13**, with the stated reason that
"Currently both `highway=residential` and `highway=unclassified` is rendered too
early, leading to clutter on the map and poor display of available data." So: the
failure mode, the diagnosis and the fix were all public a decade ago, in a style
sheet, with specific numbers.

### 1.2 Finding 3 is a misuse of a non-ordered visual variable

Hue-coding the more numerous class and watching the hierarchy invert is not a
surprise about density; it is the textbook consequence of using a **non-ordered
visual variable to carry ordered information**. Axis Maps' *Visual Variables* guide
states it plainly: "Color hue is an example of a non-ordered variable: there is no
clear ordering of, say, red, green, and blue." Ordered information — road class is
ordered — is carried by **size** and **colour value (lightness)**. A road hierarchy
must therefore be expressed as width and lightness, with hue reserved for
*qualitative* distinctions (a railway is a different *kind* of thing; a minor road is
a *lesser* road).

The navigation-standards literature says the same thing operationally. IHO S-52
§2.2.4 requires that "important features ... should be more conspicuous than minor
features", and §2.1.1 lists, as a design guideline, "Use redundant coding for
important features" — the planned route is both a distinctive colour *and* "the only
heavy dotted line on the display". Conspicuity is engineered onto the *important*
class, never onto the numerous one.

### 1.3 Finding 4's mechanism is why standards specify colour as tokens, background-first

That a stock phone theme collapses road fill and ground fill onto one slot under
64-colour quantisation, losing the network entirely, is a specific instance of a
general rule that ECDIS encodes as process. S-52 §2.1.1: "When planning colours,
begin with the background colours, the area fills for depth zones and land. Then
work on the foreground lines and symbols, making sure they have good contrast with
all their backgrounds." And S-52 does not express colours as RGB at all — it defines
named **colour tokens** (`DEPDW`, `DEPVS`, `NINFO`, `UINFD`, …) resolved through
per-condition **colour tables** for day, dusk and night, specified in CIE xy
chromaticity and luminance "because any other colour specification, such as RGB, is
specific to a particular monitor".

That is exactly "palette-first styling", arrived at independently and thirty years
earlier, and it is the reason ECDIS themes do not degrade when ported: the style
sheet names roles, and the device resolves roles to the colours it can actually
produce. This project's finding 10 (palette-first styling reduced the quantiser's
exposure to jitter roughly tenfold) is a rediscovery of the *architecture*, not just
of a tuning trick.

### 1.4 Finding 9 has a name in a formal standard: colour fringes

The FAA *Human Factors Design Standard* (HFDS 2003, HF-STD-001) §5.2.1.5 is titled
"Ensure color fringes do not affect performance" and requires that "Color fringes on
images ... if perceptible, shall not have an adverse effect on a user's perception or
performance", with the definition "Color fringes are the pixels along the border of
an object that contain a combination of the selection and background colors."

That is finding 9, as a *shall*. The project's inference — a code that antialiasing
commonly produces is a bad choice for a feature class — is the correct reading of
that requirement in a palette-constrained setting. The 3–5 % of viewport occupied by
undeclared `0xD5` is the fringe population, and the standard's position is that its
existence is tolerable only so long as it is not confusable with meaning.

### 1.5 Finding 6 and 7 are both solved-in-principle by standards practice

**Round clipping (finding 6).** Pebble's own round-display guidance
(*Design in the Round*, for the Chalk platform) treats the bezel as a hard design
constraint: reserve the outer margin — it specifies **two-pixel borders on each
edge** on a 180 × 180 panel — either fill it with background or guarantee that
"readable information cannot be displayed in this margin". It also warns that thin
concentric rings are a bad idea because "manufacturing variations may cause them to
be visibly off-center", and recommends *pagination* over smooth per-pixel scrolling
because line width varies with vertical position on a circle. Wear OS's current
guidance is the same idea expressed as a rule rather than a number: define all outer
margins "as percentages rather than absolute values, so that margins can scale
proportionally on round screens and avoid clipping any UI elements", and "In cases
where items sit on the top or bottom of the screen, apply additional inner padding to
minimize content clipping from the screen's curved edge."

Neither vendor publishes a safe-area radius, so the number has to come from geometry
here. What is settled is the *architecture*: label placement and collision must run
against the circular aperture, not the square framebuffer, and the reserved annulus
is a first-class layout parameter.

**Chrome colliding with map labels (finding 7).** This is not an open problem in
marine practice; it is a prohibition. S-52 §3.4.3: "The text panel should be
**outside** the 270 by 270 mm minimum area designated for the route monitoring chart
display", with the reasoning that "Sea experience has shown that the text panel on
the route monitoring display may have a prominence out of proportion to its
significance to safety of navigation." Overlaid windows are permitted only if
"temporary", drawn in dedicated **User Interface** colours, and relocatable by the
user "in a less important part of the display, such as on land, or behind the ship".

So the settled answer is: the chart area is exclusive, the chrome lives outside it,
and if chrome must intrude it is transient, drawn from a separate reserved palette
range, and movable. Two independent text systems sharing a framebuffer with nothing
arbitrating is the thing the standard exists to forbid.

### 1.6 Legibility limits on small displays: the direct paper, and the numbers

There is one direct experimental paper and it is worth reading in full: Ledermann
(2021), *Small differences: Limits of Legibility of Cartographic Symbols on High- and
Ultra-High-Resolution Mobile Displays* (LBS 2021), expanded into Ledermann's 2022 TU
Wien dissertation with a full guideline table. Design: four phones at **228 / 342 /
522 / 801 ppi**, masked to an identical 48 × 48 mm aperture, rail-fixed at
**≈30 cm**, 28 participants, staircase procedure, six stimulus classes (tumbling E's,
parallel lines, dash patterns, Auckland Optotypes, "vanishing" symbols, pseudo-toponym
words).

Headline results:

- Point symbols reliably identified at **0.6 mm** on the two highest-resolution
  displays; dash patterns discriminable at a line width of **0.12 mm** — both
  "approaching the minimum dimensions conventionally recommended for printed maps
  (0.6 mm and 0.1 mm, respectively)".
- The received wisdom that screen maps need coarser symbology than print is
  **wrong for modern high-density screens**.
- **No significant difference between any pair of devices for the text legibility
  task.** Text was insensitive to pixel density across 228–801 ppi.
- The 522 ppi display performed *worse* than 342 ppi on tumbling E's, traced to
  aliasing: the stimulus's "intensity gradients being aligned with the pixel grid"
  distorted it on some densities. The dissertation generalises this: fine detail
  "separated by a gap width close to one physical pixel can potentially result in
  aliasing artefacts detrimental to legibility, depending on the precise alignment
  with the pixel grid".
- Manual **grid-fitting of icons had essentially no effect** on legibility; and the
  "one physical pixel" model of smallest usable detail "overestimates the required
  size of graphical detail for lower-, and underestimates the size for
  higher-resolution displays".

The dissertation's Table 8.1 gives minimum dimensions for a 30 cm smartphone
scenario at ≥260 ppi and good acuity; the relevant rows, converted to this panel:

| Element (Ledermann 2022, Table 8.1) | Minimum, good acuity | = ′ at 30 cm | = px at 0.126 mm |
|---|---|---|---|
| Well-differentiated point icon | 0.7 mm | 8.0′ | 5.6 px |
| Poorly-differentiated point icon (NPS set) | 1.5 mm | 17.2′ | 11.9 px |
| Line width, to discriminate dash pattern | 0.15 mm | 1.7′ | 1.2 px |
| Line width, to discriminate two line *widths* | 0.2 mm | 2.3′ | 1.6 px |
| Separation of parallel lines | 0.15 mm | 1.7′ | 1.2 px |
| Line with internal detail (arrows / hachures) | 0.5 mm | 5.7′ | 4.0 px |
| Text label, **capital** height | 1.1 mm | 12.6′ | 8.7 px |

Two cautions before using that table here. First, **this panel is coarser than
anything Ledermann tested**: 202 ppi versus a 228 ppi floor, and the 228 ppi device
was significantly outperformed by the next step up in five of six tasks. Second, the
stimuli were near-maximum contrast, isolated, and **antialiased** — this panel has no
antialiasing and 25:1 contrast. Treat the table as a floor that this hardware will not
reach, not as a target.

### 1.7 Angular specification is the established vocabulary (open question 8, answered)

Yes — multiple formal standards specify legibility in visual angle, and this project
should adopt it.

| Source | Rule | = ′ | = px here |
|---|---|---|---|
| Robinson et al., *Elements of Cartography* (via Ledermann 2022 §3.3.1) | 1′ = "perfect vision and perfect conditions"; **2′** proposed as the prudent working minimum for graphical detail | 2′ | 1.4 px |
| IHO S-52 §3.1.5 | "Human factors experts quote a minimum requirement that symbols and characters subtend **20 arc minutes** at the observers eye" | 20′ | 13.9 px |
| IEC 60945, quoted in S-52 §3.1.5 | "character size in mm be not less than **3.5 × the viewing distance in metres**" — a constant angular rule | 12.0′ | 8.3 px |
| ISO 15008:2017 (character height) | 20′ recommended / 16′ acceptable / 12′ minimum — **not verified against the standard itself**, see § 7 | 20/16/12′ | 13.9/11.1/8.3 px |
| Ledermann 2022 (empirical, ≥260 ppi) | 1.0–1.1 mm capital height at 30 cm | 11.5–12.6′ | 8–8.7 px |

The convergence is striking and it is not coincidence: IEC 60945's millimetre rule
*is* a 12′ rule, ISO 15008's stated floor is 12′, and Ledermann's empirical floor is
11.5–12.6′. Three independent traditions put the absolute floor for character height
at **≈12′** and the comfortable target at **≈20′**.

Adopt arcminutes. State every symbology dimension as an angle at a declared glance
distance, and derive pixels last.

### 1.8 On a coarse panel the *pixel-matrix* floor binds harder than the angular floor

This is the part the angular literature does not cover and the display-engineering
standards do. FAA HFDS §5.3.1–5.3.4, for flat panels:

- Characters in **vertical orientation**: "should be formed from a matrix of at least
  **9 by 13 pixels**".
- Characters in **nonvertical orientation**: "at least **8 by 11 pixels**, preferably
  **15 by 21 pixels**".
- Stroke width for pixel-generated characters: 1 px for 7–8 px cap height, 1–2 px for
  9–12, 2 px for 13–14, 2–3 px for 15–20.
- Height/width: a 13 px-high capital wants 6–12 px of width, preferred 9.
- HFDS §5.2.4.3: "Alphanumeric characters should have at least **10 resolution
  elements per character height**."

At 1.44′ per pixel, a 13-pixel-high capital subtends 18.7′ — i.e. **the FAA's pixel
matrix and the 20′ angular target land in the same place on this panel**, which is
why both traditions can be satisfied at once here. Below about 20′ the angular rule
still permits text that the pixel grid can no longer form. That is the mechanism
behind this project's measurement that 11–12 px is detectable but unreadable: at
12 px em (≈8.4 px capital) you are under the 9 × 13 matrix, so the glyph is not
merely small, it is *no longer being drawn*.

### 1.9 Reconciling § 1.6–1.8 with this project's measured text numbers

Converting this project's figures (its arcminute values correspond to **x-height**;
capital height is taken as 0.7 em, x-height as 0.5 em):

| Project observation | em (px) | x-height ′ | Capital ′ | Standards verdict |
|---|---|---|---|---|
| "detection, not reading — confirmed unreadable" | 11–12 | 8.0–8.7′ | 11.1–12.1′ | at/below the 12′ absolute floor; below the 9 × 13 matrix |
| 20 px | 20 | 14.4′ | 20.2′ | at ISO 15008's *recommended* 20′; matrix 14 px, satisfies FAA vertical minimum |
| **22–26 px, confirmed readable on device** | 22–26 | 15.9–18.8′ | 22.2–26.3′ | above every published floor, 1.1–1.3× the 20′ recommendation |
| 28 px "comfortable" | 28 | 20.2′ | 28.3′ | 1.4× the recommendation |

The project's device measurements are **consistent with, and slightly more
conservative than, the published minimums** — which is what you would expect from a
panel below the tested density range, with no antialiasing and 25:1 contrast. There
is no contradiction to resolve here; there is a vocabulary to adopt and a modest
sanity check passed.

### 1.10 The photograph/desktop-render trap is corroborated from an unexpected direction

The project's observation that "1:1" desktop renders overstate legibility by roughly
2× is arithmetic, and it is independently confirmed in the field on the closest
commercial hardware. In the Garmin Connect IQ developer forum thread on the MIP
palette, a developer reports that "while FR955's device snapshot looks the same as
the simulator, it doesn't look the same to the human eye." Same class of display
(memory-in-pixel, 64 fixed colours), same conclusion: only the panel is evidence.

---

## 2. Where the literature contradicts this project's findings

This is the section that earns the exercise. Four contradictions, one caveat, and one
finding that turns out to be *stronger* than the project states.

### 2.1 "Angled strokes rasterise worst … with no antialiasing to soften them" is being used to rule out along-line labels. The literature does not support that inference.

Finding 5 treats rotation as the culprit. The evidence separates two effects that
this project has merged:

- **Perceptual cost of rotation is small at moderate angles.** Wigdor &
  Balakrishnan (2005), 
  *Empirical Investigation into the Effect of Orientation on Text Readability*: reading
  times at −45°, 0° and +45° "were not significantly different from one another",
  and even at 90° the penalties were "only 26 %, 54 %, and 17 % for short words,
  phrases, and numbers respectively" — against prior work that had reported 52 % at
  45° and 205 % at 90°. For serial search the penalty was a 3 % increase between zero
  and some-rotation. Street names are short words: the class with the *smallest*
  penalty.
- **Rasterisation cost of rotation is real and quantified — and it is a size
  requirement, not a prohibition.** FAA HFDS §5.3.2 asks for a larger matrix for
  nonvertical characters, "preferably 15 by 21 pixels" against 9 × 13 for vertical.
  Kurokawa (1988) studied exactly this — rotated dot-matrix characters "as might be
  the case in a moving map display" — and concluded that rotation "affects legibility
  mainly through the distortion of dot-matrix patterns" rather than through the angle
  itself. Distortion of the pattern is a resolution problem, and the standard's answer
  is more pixels.

**Contradiction:** along-line labelling is not ruled out at this pitch; it is
*priced*. The price is roughly **1.5× the glyph matrix** — about a 21 px cap height
instead of 13 px — plus a rotation limit. Combined with Wigdor's angle data, the
defensible rule is: allow along-line labels only where the local bearing is within
±45° of horizontal, and set them at ~1.5× the point-label size. Whether that is
affordable on a 240 px circle is a real question; but it is a budget question, not a
"rotation rasterises badly" question, and it should be tested that way.

### 2.2 The competitor's perpendicular offset is a *documented standard requirement*, not a trick a style sheet cannot express

Finding 5 notes that Garmin "appears to offset perpendicular to the line and stay
adjacent, which a static style sheet cannot express per-feature", implying an
undocumented proprietary capability.

IHO S-52 §3.1.6 is blunt: "Symbols and text should always be drawn **screen-up**, no
matter what the orientation of the screen may be." That is a mandated
upright-text-on-a-rotating-map rule in a certified navigation standard — the same
choice Garmin appears to make — and the algorithmic problem it creates is a studied
one. Gemsa, Nöllenburg & Rutter (2014), *Evaluation of Labeling Strategies for
Rotating Maps*, formalises exactly this: labels that "remain horizontally aligned
during the map rotation", attached by a corner to their feature, with the objective
of maximising label activity integrated over a full 2π rotation, plus **consistency
models** whose whole purpose is to stop labels flickering on and off as the view
turns. It is NP-hard, it has 1/4- and 1/8-approximation algorithms and practical
greedy heuristics, and their result is that "low flickering can be achieved at the
expense of only a small reduction in the objective value".

**Contradiction:** this is not an inexpressible per-feature style attribute. It is a
*placement algorithm* that belongs in the offline renderer, driven by geometry, with
published models and heuristics. The style sheet's job is to declare the label class;
the renderer's job is to solve the placement. Building it as a style-sheet feature is
the wrong layer, and that is probably why it looks impossible.

### 2.3 Finding 2's diagnosis is right but its scope is too narrow — the zoom ladder is a *task* decision, not only a legibility one

The ladder was specified from ground-resolution arithmetic without a legibility trial;
true, and the fix is a trial. But the mobile-cartography literature says the ladder
is also wrong if it is a single ladder. Utebaliyeva (2019), *The use of maps on
smartwatches* (MSc, ITC/Twente; eye-tracking, think-aloud, interviews) recommends
"Use a more **generalized** map in the navigation mode and provide more information
when the user is **browsing** the map", and distinguishes the geographic questions by
task: for objects in the user's proximity "a map is not necessary, and information
could be provided in textual form"; maps are needed for spatial distribution,
relations and overview; and "For wayfinding and navigation, the map is needed for
**route confirmation**."

**Contradiction of emphasis:** the failing middle zooms may be failing partly because
one ladder is serving two different tasks — *confirmation while moving* (needs a
skeleton) and *browsing while stopped* (needs detail). A single legibility trial on
one ladder will find a compromise; two ladders may dominate the compromise.

### 2.4 Finding 5's framing of "unsolved" is too pessimistic: external/boundary labelling has now been evaluated on circular displays

See § 3.2. Briefly: a 2026 controlled experiment with 54 participants on *orbital
boundary labelling* — labels in an annulus outside a circular view, connected by
leaders — reports accuracy above 0.95 and explicitly targets "smartwatches,
automotive dashboards, cockpit instruments". This is no longer a research gap; it is
an evaluated design with a published cost model.

### 2.5 Where the literature *disagrees with itself*, and this project should not be misled

Ledermann's empirical result that **manual grid-fitting has essentially no effect**,
and that the physical-pixel model of minimum detail is the wrong model, sits against
a strong intuition in embedded graphics work (and against the project's own
palette-alignment reasoning). Two reasons not to over-read it here: all four of his
displays had antialiasing enabled and none was as coarse as this panel; and his own
aliasing finding on tumbling E's shows grid alignment *does* matter when detail
approaches one pixel. On a 202 ppi panel with no antialiasing, one-pixel detail is
exactly where map symbology lives. **This is the one place where the literature's
conclusion should be treated as not transferring**, and it is a fair thing to measure
here.

### 2.6 Finding 10 is understated

"Rendering is not byte-reproducible … differing by one or two pixels, from
antialiasing coverage ties" is treated as an annoyance to be mitigated. FAA HFDS
§5.2.1.6 treats one-pixel positional instability as a *defect class* with a numeric
limit: "Avoid jitter. Deviations in the location of a displayed element shall be
equal to or less than .0002 mm per mm of viewing distance over the period of a
second." That limit is about temporal jitter on a live display rather than build
non-determinism, so it does not apply directly — but it establishes that one-pixel
placement instability is the kind of thing standards regulate, not tolerate. Given
that tiles here are pre-rendered offline, byte-reproducibility is achievable in
principle (fix the rasteriser's tie-breaking), and "reduced the quantiser's exposure
tenfold" is a mitigation of a bug, not a resolution of it.

---

## 3. Reframings worth taking seriously

### 3.1 Route linearisation (StripeMaps): the evidence survives, but it does not say what it is often quoted as saying

**What it is.** Wenig, Schöning, Hecht & Malaka, *StripeMaps: Improving Map-based
Pedestrian Navigation for Smartwatches*, MobileHCI '15, DOI 10.1145/2785830.2785862.
A 2-D route map is cut along the angle bisector at each turn and the pieces rotated
and reassembled into a single 1-D vertical strip, browsed by scrolling in one
direction. Cuts are made only when not cutting would push the path outside the
strip's width, which avoids shattering zig-zags. The visible cut angle itself
communicates the turn: "The cuts not only show the coarse direction but also the
exact turning angle." Lineage is the AAA TripTik strip booklet.

**The evaluation, precisely.** Study 1: 16 participants, mean age 19.4, none had used
a smartwatch, **indoors** in a university building they were unfamiliar with, four
routes of similar length and turn count, within-subject, counterbalanced, four
conditions — turn-by-turn text (TBT), pannable 2-D map at the same scale with zoom
disabled (2D), StripeMaps (SM), StripeMaps plus compass orientation indicator (SM+O).
Measures: completion time, navigation errors (a wrong turn not noticed within five
metres), SUS, NASA-TLX.

| Condition | Mean time/route | Mean errors/route | SUS | NASA-TLX overall |
|---|---|---|---|---|
| SM+O | 180 s | 0.13 | 75.3 | 21.7 |
| SM | 187 s | 0.19 | 73.7 | 29.4 |
| TBT | 197 s | 0.75 | 67.9 | 36.4 |
| 2D | 235 s | 0.44 | 59.1 | 65.6 |

Significance: time, repeated-measures ANOVA F = 21.1, p < .001, with SM > 2D
(p < .01) and SM+O > TBT (p < .05). Errors, Friedman χ² = 10, p = .02, with **only
SM+O vs TBT surviving Bonferroni correction (p = .004)**. SUS significant between all
pairs except SM vs SM+O. Preference: all 16 ranked a StripeMaps variant first (13 of
16 chose SM+O); 15 of 16 ranked the 2-D map **last**. A behavioural detail is the most
persuasive number in the paper: in the 2-D condition "12 out of 16 participants
stopped at every decision point to interact with the map, whereas in the SM and SM+O
condition just 2 out of 16 participants stopped".

Study 2: 8 tourists in Aachen, mean age 38.5, 20 minutes each, outdoors,
qualitative, SUS 79.

**How strong is this?** Moderate, and narrower than the summary suggests.

- N = 16, young, smartwatch-naive, single site, **indoors**, single session.
- **No positioning in any condition.** No blue dot, no GPS, no automatic scrolling.
  The authors flag this as future work; a Study-2 participant said "If you would add
  the blue GPS dot, then it would be perfect".
- The 2-D baseline was **crippled on purpose**: zoom disabled, so panning was the
  only interaction. Defensible (Apple's own guidance discouraged pan-and-zoom on the
  watch, and Kerber et al. found the same) but it means "beats 2-D maps" means "beats
  a pannable un-zoomable raster map with no self-location".
- On **errors**, StripeMaps did *not* significantly beat the 2-D map. The robust
  error result is that turn-by-turn text is worst.
- Errors were corrected by the shadowing experimenter after five metres, which the
  authors note likely *flatters* TBT's time.
- The paper is explicit that StripeMaps addresses **navigation, not orientation**:
  "StripeMaps is explicitly dedicated to map navigation rather than orientation (i.e.
  'getting one's bearings')", and new techniques "will have to be developed to aid in
  smartwatch map orientation".
- Follow-ups exist but are by the same group and are not replications
  (*ScrollingHome*, MobileHCI '16; *The aesthetics of StripeMaps*, MobileHCI '16
  adjunct). I searched for independent replication or published criticism and
  **found none**.

**What it costs to adopt here, concretely.** The transform is offline and cheap —
it is a raster cut-and-rotate, which is well within a desktop toolchain. The real
costs are structural:

1. **It requires a route.** A runner following a planned course gets a stripe; a
   runner improvising, or wanting to know what is around them, gets nothing. The
   paper's own scope statement concedes orientation is out of scope. The Utebaliyeva
   thesis independently converges: the navigation map's job is *route confirmation*.
2. **Rotated labels get worse, not better.** The paper's stated limitation: the
   implementation "turns rasterized images into stripes", and "a serious drawback are
   rotated labels (e.g. street names or symbols of landmarks), which can become
   unreadable or unrecognizable". The cut-and-rotate step rotates map content by
   arbitrary angles. On a panel with no antialiasing and a 9 × 13 glyph matrix floor,
   that is worse than the problem it replaces. A stripe here would have to be
   generated from vector source with upright labels applied *after* rotation — i.e. it
   needs the same upright-label placement machinery as § 2.2, so it is not a way to
   avoid that work.
3. **Scrolling interaction.** The evaluated design is scroll-driven with no GPS. A
   running watch has GPS and poor interaction affordances mid-run; the untested
   variant (auto-scroll by position) is the one this project would actually build.
4. **It abandons trace-over-map.** This project's activity draws its own GPS trace
   over the map. On a stripe, "am I on the line" is trivially true by construction and
   deviation is hard to depict.

**Verdict.** The reframing survives scrutiny as *a real, evaluated, better artifact
for the specific task of following a known route on foot without positioning*. It
does **not** establish that a 2-D map is the wrong artifact for a running watch. The
honest reading is that it splits the problem: linearise when there is a route and the
task is confirmation; keep a 2-D map when the task is orientation or improvisation.
That split is worth a prototype, and the cheapest experiment — because it needs no
device work — is to render a stripe for a known route offline and compare it against
the current map pack on the panel, upright labels in both.

### 3.2 External / boundary labelling on a circular display: the strongest new find

Bekos, Niedermann & Nöllenburg (2019), *External Labeling Techniques: A Taxonomy and
Survey* (Computer Graphics Forum 38(3):833–860; arXiv:1902.01454) is the entry point:
twenty years of algorithms for placing labels "in the empty space surrounding the
image", connected by "thin leader lines", with a unified taxonomy.

The directly applicable work is newer and is about round screens. Wallinger,
Bonerath, Terziadis, Wulms & Nöllenburg (2026), *Clarity and Computational Efficiency
of Orbital Boundary Labeling* (arXiv:2603.08657): labels placed in an annulus around a
circular view, connected either by straight-line (SL) leaders or orbital-radial (OR)
leaders. Controlled experiment, **54 participants**, two tasks, 72 trials each,
instances of 10/15/20 features. Results: no significant accuracy difference between
leader styles, mean accuracy **>0.95** (uniform labels) and **>0.98**
(non-uniform); SL leaders significantly faster (p < 0.01, 0.05–0.42 s, largest gap for
off-centre clustered features) and rated more confidence-inspiring, while OR leaders
were judged more aesthetically pleasing. And the sizing claim, aimed squarely at this
hardware class: "Typical smartwatch screens (e.g., 440-480 px in diameter) provide an
outer circumference of roughly 1.4-1.5 kpx, which comfortably accommodates 10–16
labels with short names in a readable font size (8–10 pt)."

**Why this matters for findings 5, 6 and 7 at once.** Boundary labelling puts every
label upright, in a reserved ring, outside the map body:

- rotation problem: gone (all labels horizontal);
- round-clipping problem: gone (the ring *is* the aperture, so placement is polar by
  construction);
- association problem: carried by the leader, and measured at >0.95 accuracy;
- chrome collision: the ring is a natural home for a single arbitrated text system.

**The cost, and it is not small.** Their geometry: a 440 px inner circle plus 20 px
labels top and bottom giving 480 px total — the ring is ~8 % of the diameter *added
outside*. This panel cannot add; it must subtract. Taking a 20 px label band out of a
240 px circle leaves a 200 px map body: the visible map area falls from ~45 200 px² to
~31 400 px², i.e. **~30 % of the visible map is spent on the label ring**. And their
own limitation section is explicit that smartwatch generalisation is untested: "We
expect that some of our findings may generalize to smartwatches; however, this remains
to be validated in a dedicated study." Their label count (10–16) assumes a 440–480 px
circumference; this panel's 240 px circle has roughly half the circumference, so
budget **5–8 labels**.

**Verdict.** This is the highest-value candidate in the brief after linearisation, and
unlike linearisation it is compatible with everything already built: the map body
stays a 2-D blit, and the label system moves out of the tiles into a ring drawn by
the app. It also collapses three open problems into one. Worth prototyping, with the
30 %-area cost measured on the panel rather than argued about.

### 3.3 Non-map spatial displays: real, but thin for this use case

- **Haptic / vibrotactile wayfinding** has a substantial literature (waist belts,
  wristbands, direction cueing on the wrist) and reported direction-recognition
  accuracy of the order of **96 % for four cardinal directions and 69 % once
  inter-cardinals are added** — a figure I have only from search snippets over
  abstracts, not from a full text, so treat as indicative. The honest reading: haptics
  are good for *turn now / wrong way* and poor for *where am I*. Utebaliyeva's
  recommendation list independently asks for "vibration commands" and "sound and
  vibration" alongside the map, not instead of it. As a complement to a map this is
  cheap and well-supported; as a replacement it is not.
- **Landmark-based instructions** are the established best practice for turn-by-turn
  text (StripeMaps built its TBT baseline that way deliberately, "referencing
  landmarks rather than distances between turns"), and turn-by-turn text was
  nonetheless the **worst** condition for errors in the one study that measured it on
  a watch. That is a useful negative result: better text is still text.
- **Schematic / compass guidance.** Utebaliyeva recommends "Include compass-type
  graphics, so that users know which direction they are facing", and in StripeMaps the
  compass-augmented variant (SM+O) was the best condition on every measure. A heading
  indicator is a small, cheap, evidence-supported addition regardless of which map
  artifact wins.

### 3.4 Fields with decades of formal minimums: what actually transfers

The marine standard transfers best, because ECDIS is a moving map with a fixed
palette, an ambient-light problem, a mandated exclusive chart area and an
own-position symbol — structurally the same artifact.

Transferable from **IHO S-52 Edition 6.1.1**:

- **Three-tier declutter with a floor.** IMO defines **Display Base** (can never be
  removed), **Standard Display** (the default), and **Other Information** (opt-in).
  The mariner may add or remove anything "except Display Base". Note also the warning
  that the base tier is deliberately *not* sufficient on its own: "it should not be a
  display option to 'Show Display Base' without any additions". A per-zoom detail
  threshold should be organised this way — a never-dropped tier, a default tier, and
  an opt-in tier — rather than as a flat list of zoom cutoffs.
- **Design for the worst viewing case first**: "Design for the worst case; fit the
  rest in afterwards. For the ECDIS this means setting up the display for bright
  sunlight, when all but the starkest contrast will disappear".
- **Redundant coding for the most important classes** (colour *and* line style *and*
  width), §2.1.1 and §2.2.4.
- **Keep the renderer dumb**: "Keep the software simple. For example, lineweights are
  not changed for different colour tables as this would require a conditional
  symbology procedure." A device with no rasteriser and pre-baked tiles should read
  that as permission, not limitation.
- **Symbol/text scale is anchored, not relative**: "When the display scale is enlarged
  by zooming in, it should be possible to hold symbol size constant. ... Symbol and
  text size should never be decreased when zooming out."

Transferable from **FAA HFDS 2003 (HF-STD-001) ch. 5**: the pixel-matrix and
stroke-width tables in § 1.8; the colour-fringe requirement in § 1.4; and one polarity
rule that matters for a reflective panel — §5.3.8: "If character stroke width,
modulation, and luminance values are approximately equal for both polarities, the
**positive polarity** (dark characters on a light background) should be used", with
the reason "Reflections are less visible on a bright background than on a dark
background." A light-background map is the right default on a transflective panel, and
it is also the direction this project's palette is already going.

**Not retrieved:** RTCA DO-257A (aviation electronic map MOPS), SAE ARP5289 (electronic
map symbology), MIL-STD-2525 and the Navy display-symbology test report — all
paywalled or 403. Aviation-specific symbology minimums are therefore **not covered
here**; the FAA HFDS covers the general display-ergonomics layer only. If the aviation
angle matters later, DO-257A §2.3 (Aerodrome Moving Map Display) is the section to
buy.

---

## 4. The open questions, answered or left open

### 4.1 Is a 2-D map the right artifact for a running watch at all?

**Partly settled, and the answer is "it depends on the task, and the task splits."**
StripeMaps is real evidence that for *following a known route on foot*, a linearised
strip beats both a pannable 2-D map and turn-by-turn text on time and usability
(§ 3.1) — with the caveats that it was indoors, N = 16, no GPS in any condition, and
that on *errors* it beat only turn-by-turn. It is explicitly not an orientation aid.
Utebaliyeva's independent conclusion is complementary: for proximity questions text is
enough; for distribution/relations/overview a map is needed; for navigation the map's
role is **route confirmation**.

For a *running* watch specifically: no study found. The nearest thing in the
literature is a passing mention of RunNav (McGookin et al.), which gives runners a
high-level overview of good and bad places to run rather than explicit routes — i.e.
the running use case in the literature is treated as *exploration*, which is exactly
the case linearisation cannot serve. **Left open for running.** The cheap experiment
is § 3.1's offline stripe versus the current pack.

### 4.2 Which path/footway classes deserve ink, at which zooms?

**Partly settled by practice, not by research.** OSM Carto's merged thresholds
(§ 1.1) are the best published numbers: residential and living_street at **z13**,
unclassified at **z11**, buildings at **z13**. Those are calibrated for a ~0.26 mm CSS
pixel; this panel is 0.126 mm, so every feature is about **half the physical size at
the same tile pixel dimensions**, which is one full zoom level of linear scale. My
derivation, not a citation: **shift OSM Carto's thresholds one zoom later** as the
starting hypothesis, then trial.

Sidewalks and crossings specifically are an **acknowledged unsolved problem in the
reference style**, not something this project got wrong. OSM Carto issue #5068,
*Assessing options for improved rendering of footways*, is still open and labelled
"input needed"; it records that "path/footway rendering bad to read at z=13/14" and
that footway styles are "difficult to distinguish and in general much weaker than
cycleway". So: no published guidance settles which footway classes deserve ink. What
the generalization literature does offer is the two operators this project has not
used — **aggregation** and **typification** (§ 1.1): a sidewalk mesh is a candidate for
being *typified* into the street it parallels (draw the street, not its two
sidewalks), which is a data-preparation decision, not a styling one. **Left open, with
a named approach to try.**

### 4.3 What is the right treatment for railways in a palette with no spare dark neutral?

**Settled in principle: do not spend a colour on it.** S-52 §2.1.1 states that
contrast "may be colour contrast or luminance contrast, or **contrast from differing
linestyles or symbol shapes**", and §2.2.4 requires redundant coding for important
features, with the planned route distinguished partly by being "the only heavy dotted
line on the display". Railways are conventionally distinguished by **pattern**, not
hue, in exactly this way.

The constraint here is the pixel budget for the pattern. Ledermann's Table 8.1 puts
the minimum width of a line carrying internal detail at **0.5 mm = 4 px** on this
panel, and the minimum separation of parallel lines at **0.15 mm = 1.2 px**. A
railway ladder (dark casing plus alternating light ticks) therefore needs about
**4 px of width** and a tick period comfortably above 2 px. That is affordable at the
closer zooms and not at the wider ones — which is itself a generalization decision
(railway becomes a plain dark line, then disappears).

One further option the palette makes available: the L\* gap between 23.7 and 66.5 can
be *filled by dithering* two neighbouring legal codes, which is the only way to obtain
a mid-lightness neutral at all on this cube (see § 4.5). A dithered casing is a
legitimate way to buy the missing dark-neutral without a palette change.

### 4.4 Aliased or antialiased text at 126 µm with 64 colours — is there evidence either way?

**Left open, but the literature narrows it usefully.** No study was found on text
rendering at ~200 ppi with a 2-bit-per-channel palette; that combination is outside
the tested space of everything reviewed here (Ledermann's floor was 228 ppi with full
colour and antialiasing on).

What the literature does say:

- Antialiasing was "generally recommended" for text in earlier screen-cartography
  guidance (Jenny et al., as summarised in Ledermann 2022 §3.2.4) — but that
  recommendation assumes the intermediate tones are *available*. Here they are not:
  antialiasing between two legal colours lands off-palette, and §5.2.1.5 of the FAA
  standard requires that the resulting fringes not degrade performance.
- Ledermann found **no significant density effect for text** across 228–801 ppi, and
  found that fine detail near one-pixel scale can be *degraded* by grid-aligned
  aliasing.
- The pixel-matrix floor (§ 1.8) says the first-order variable for text legibility
  here is glyph *matrix size*, not edge treatment.

The practical implication: this is worth measuring, and the measurement is cheap
because text is baked into tiles offline. Test three conditions on the panel at 22 px
and 26 px em — hard-aliased, antialiased-then-quantised-to-palette, and
antialiased-with-a-declared-two-code-fringe-ramp (i.e. pick the fringe codes
deliberately, in the spirit of § 1.4, so the fringe is a declared colour rather than
an accident).

### 4.5 Is dithering worth it at 2 bits per channel for map fills?

**Not settled by any study; there is industry practice on both sides, and the
arithmetic is favourable enough to justify a trial.**

- **For:** Pebble's community *Simple Dithering Library* documents the standard
  practice on the closest hardware — "By using 50 % dithering, developers are able to
  combine every color available in Basalt, resulting in a new color palette of about
  343 different colors", justified by "50 % dithering is so small that the human eye
  perceives this as a single color". Pebble Basalt's pitch (144 × 168 on a 1.25″
  panel, ≈0.144 mm) is close to this panel's 0.126 mm, so the perceptual claim
  transfers about as well as such a claim can.
- **Against:** E Ink's own patent on the subject — US10353266B2, *Color sets for low
  resolution dithering in reflective color displays* (Bouchard & Telfer, granted
  2019-07-16) — states that "dithered images when viewed closely have a characteristic
  graininess", and offers only the qualitative rule that "if the resolution of the
  display is sufficiently high and the viewing distance sufficiently long, it may be
  adequate to dither between only the eight device primaries". No pixel densities, no
  viewing distances, no dither-matrix guidance. The company with the largest
  commercial interest in reflective-display dithering publishes no numbers.

My arithmetic, to size the risk: a 2 × 2 ordered dither cell is 0.252 mm, which at
30 cm subtends 2.9′ per cycle, i.e. **≈21 cycles/degree** — just under the ~30
cycles/degree acuity limit. So the *pattern* is near-invisible in principle, but
contrast sensitivity at 21 c/deg is not zero, and it scales with the contrast between
the two dithered codes. That predicts the useful rule: **dither between adjacent
palette levels (an 85-unit step) and it will fuse; dither between distant codes and it
will read as noise.** The single most valuable application is filling the
L\* 23.7 ↔ 66.5 neutral gap, where the two candidates are far apart in lightness —
which is exactly the risky case, and therefore exactly the thing to put on the panel
before believing.

Cost of trying is near zero: tiles are pre-rendered, so dithering is a toolchain
option with no device-side cost and no runtime penalty.

### 4.6 How should a moving-map display reserve space for application chrome?

**Settled.** See § 1.5. IHO S-52 §3.4.2–3.4.3: the minimum chart area is reserved for
chart and navigation information alone; the text panel goes **outside** it; any
superimposed window must be temporary, drawn only in dedicated User Interface
colours, and relocatable by the user to "a less important part of the display". The
generalisable rules for this project are: (a) declare a chart rectangle/disc and
forbid chrome inside it; (b) give chrome its own reserved colour-token range so it can
never be confused with map ink; (c) if chrome must overlap, make it transient and
movable. Combined with § 3.2, a boundary-label ring is a natural place to put chrome
and labels under one arbiter.

### 4.7 What tile edge length minimises cache pressure without wrecking the zoom ladder?

**No published guidance found for RAM-constrained blit clients — searched and came
up empty.** What exists is network- and label-driven guidance for web maps, arguing
the opposite direction: Mapzen's *Get more for less with 512 pixel tiles* and
MapTiler's documentation both recommend 512 px tiles because "a single 512 request is
equivalent to four 256 requests at all zooms", compression is better, and label
placement across tile seams improves. Those arguments do not apply here: there is no
HTTP, tiles are uncompressed `tile_dim²` bytes, and the binding constraint is the
resident cache, where the project's own arithmetic already dominates any published
guidance (64 KiB per 256 px tile, four-tile straddle = 256 KiB ≈ 43 % of the GUI
budget; 16 KiB per 128 px tile, nine-tile worst case = 144 KiB).

The one relevant published consideration in favour of larger tiles is
**cross-tile label placement** — labels near a tile edge need context from the
neighbour, which is a real problem for a pre-rendered pipeline and gets worse as tiles
shrink. If boundary labelling (§ 3.2) is adopted, that argument disappears entirely,
because labels leave the tiles. **Left open, but the decision is coupled to the
labelling architecture, not to a published tile-size rule.**

### 4.8 Does anyone specify legibility in angular terms?

**Settled: yes, and this project should adopt arcminutes.** See § 1.7. IHO S-52
quotes 20′ for symbols and characters; IEC 60945's "3.5 mm per metre of viewing
distance" is a 12′ rule in disguise; Robinson's textbook proposes 2′ as the prudent
minimum for graphical detail; Ledermann's empirical capital-height floor is 11.5–12.6′;
and ISO 15008's reported 20′/16′/12′ ladder (unverified against the standard) fits the
same frame. The project's existing arcminute figures are already in the right
vocabulary — they should be labelled as **x-height** angles, since that is what they
are, and a capital-height figure should be published alongside, because that is what
every standard quotes.

### 4.9 Reflective-display palette design — is there guidance for ambient-dependent contrast?

**Partly settled, and the answer to the specific question asked is: computing WCAG
contrast ratios against a nominal white is defensible as a screening heuristic and
unsound as a criterion.**

The primary text is unusually helpful here. W3C's *Understanding SC 1.4.3* states the
thresholds (4.5:1 normal text, 3:1 for large-scale text, where large is "18 point or
14 point bold" ≈ 24 px and 18.5 px), and is explicit about the provenance and the
assumptions: "A contrast ratio of 3:1 is the minimum level recommended by ISO-9241-3
and ANSI-HFES-100-1988 for standard text and vision"; 4.5:1 comes from multiplying
that by the ~1.5 contrast-sensitivity loss associated with 20/40 acuity; and crucially
"The ANSI/HFS 100-1988 standard calls for the contribution from ambient light to be
included in the calculation of L1 and L2. The .05 value used is based on Typical
Viewing Flare." So the WCAG formula does model ambient light — as a **single fixed
5 % flare constant**, on sRGB-encoded colours, for a display that emits. WCAG itself
notes the criterion is framed as contrast ratio because "web content does not emit
light itself".

On a transflective memory-in-pixel panel *all* the luminance is reflected ambient,
the black level moves with illuminance, and the effective contrast ratio is a property
of the room, not of the two colour codes. ISO 9241-303:2011 §5.2 makes exactly this
distinction in normative language: "In most cases, there is a luminous environment to
the screen that contributes to its luminance and colour ... (for reflective displays
such as paper, contrast on the display screen is even **caused** by the luminous
environment). Since the environment's luminosity generally cannot be controlled by the
user, it is necessary to provide means of adjusting display luminance to obtain a
proper luminance balance over a range of work environments" — and §5.2.5's adjustability
requirement is scoped to "**emissive** displays", which this panel is not (it has a
backlight, but the normal viewing mode is reflective with the backlight off).

What to do instead, from S-52's practice: **design for the worst ambient case and
verify by measurement, not by formula.** "Design for the worst case; fit the rest in
afterwards ... setting up the display for bright sunlight, when all but the starkest
contrast will disappear." And use redundant coding so that a class remains
identifiable when its colour separation collapses.

There is also a directly relevant recent standard that I could **not retrieve**:
**ISO/TR 9241-313:2025, *Optical measurement methods for reflective displays*** — it
exists (listed on the ISO OBP), it is exactly the missing methodology, and the ISO
site returned 403. Related peer-reviewed work by Hertel (2018) on applying IEC/ISO
optical measurement methods to reflective e-paper, and on predicting reflective
colours under ambient illumination, appears in SID and *Color Research & Application*
— abstracts only, both paywalled.

Finally, one piece of vendor practice worth copying: Pebble shipped **two** palette
files for the same 64 colours — `pebble_colors_uncorrected.aseprite` and
`pebble_colors_sunlight.aseprite` — and its colour-picker tool offered "Uncorrected"
and "Sunlight" viewing modes. A palette document for a reflective panel should ship
the ambient-corrected appearance alongside the nominal codes. That is the closest
thing to published reflective-palette design guidance that this review found.

---

## 5. Established guidance to conform to

Concrete, with the conversion to this panel (0.126 mm pitch, 1.44′/px at 30 cm) shown
where it is my arithmetic.

**Text**

| Rule | Source | Value | This panel |
|---|---|---|---|
| Character height, comfortable | S-52 §3.1.5; ISO 15008 (unverified) | 20′ | 14 px capital |
| Character height, absolute floor | IEC 60945 via S-52; ISO 15008 (unverified) | 12′ | 8.3 px capital |
| Capital height, empirical floor (≥260 ppi, AA on) | Ledermann 2022 Table 8.1 | 1.0–1.1 mm | 8–8.7 px |
| Glyph matrix, upright | FAA HFDS §5.3.1 | ≥ 9 × 13 px | binding constraint |
| Glyph matrix, rotated | FAA HFDS §5.3.2 | ≥ 8 × 11, **preferably 15 × 21 px** | ~1.5× cost for along-line labels |
| Stroke width at 13–14 px cap height | FAA HFDS Exhibit 5.3.3 | 2 px | — |
| Resolution elements per character height | FAA HFDS §5.2.4.3 | ≥ 10 | — |
| Rotation limit before measurable reading cost | Wigdor & Balakrishnan 2005 | ±45° free; 90° costs 17–54 % | cap along-line labels at ±45° |
| Text orientation on a rotating map | S-52 §3.1.6 | "always ... screen-up" | upright only |

**Symbols and lines** (Ledermann 2022 Table 8.1, good acuity, ≥260 ppi, AA on — a
floor this panel will not reach)

| Element | Value | This panel |
|---|---|---|
| Well-differentiated point icon | 0.7 mm | 5.6 px |
| Poorly-differentiated point icon | 1.5 mm | 11.9 px |
| Line width to discriminate dash patterns | 0.15 mm | 1.2 px |
| Line width to discriminate widths | 0.2 mm | 1.6 px |
| Parallel-line separation | 0.15 mm | 1.2 px |
| Line with internal detail (railway ladder) | 0.5 mm | 4.0 px |
| Simple chart symbol (S-52 §3.1.5) | "about 12 pixels" | 12 px, ≈17′ |

**Layout and structure**

- Reserve the chart area exclusively; put chrome outside it; overlays only if
  temporary, in a dedicated UI colour range, and relocatable. (S-52 §3.4.2–3.4.3)
- Round displays: reserve an outer margin and put no readable information in it;
  avoid thin concentric rings (off-centre manufacturing tolerance); paginate rather
  than pixel-scroll. (Pebble *Design in the Round*)
- Express all margins as percentages of screen size, and add extra inner padding at
  the top and bottom of a circular screen. (Wear OS)
- Three declutter tiers: never-removable base, default, opt-in — and the base tier is
  deliberately not sufficient alone. (S-52 §3.2 / IMO Display Base)
- Hold symbol and text size constant across zoom; never shrink them when zooming out.
  (S-52 §3.1.5)
- Design for the worst ambient case first; use redundant coding for the classes that
  matter; plan background fills before foreground ink; keep the renderer free of
  conditional symbology. (S-52 §2.1.1, §2.2.4)
- Prefer positive polarity — dark ink on a light ground — on a reflective panel.
  (FAA HFDS §5.3.8)
- Fringe pixels must not degrade identification. (FAA HFDS §5.2.1.5)

**Contrast**

- WCAG 2.x: 4.5:1 normal text, 3:1 large text (≥24 px, or ≥18.5 px bold), with a fixed
  5 % viewing-flare term and an emissive-display assumption. Use as a screening
  heuristic on the nominal palette; do not treat as a pass criterion for this panel.
- ISO 9241-303 §5.2: for reflective displays, contrast is *caused* by the ambient
  environment; the luminance-adjustment requirement is scoped to emissive displays.
- Panel contrast ratio is 25:1 nominal, so the achievable palette contrast is bounded
  well below what an sRGB calculation suggests.

**Not conformed to, because not retrieved:** ISO 15008's own tables, ISO/TR 9241-313,
RTCA DO-257A, SAE ARP5289, MIL-STD-2525, Apple's watchOS HIG.

---

## 6. What is genuinely novel to this hardware, and still has to be measured here

Everything below was searched for and not found in the literature. These are the
places where device measurement is the only source of truth, and where the project's
empirical habit is the right one.

1. **Legibility below 228 ppi with no antialiasing.** Every experimental result found
   sits at 228 ppi or above with antialiasing enabled. This panel is 202 ppi with
   hard pixels. The interaction between the angular floor (~12′) and the pixel-matrix
   floor (9 × 13) is the specific unknown, and § 1.9 suggests the project has already
   measured it more precisely than the literature has.
2. **Whether grid-fitting matters at this pitch.** Ledermann's null result on
   grid-fitting is the one literature conclusion I would expect *not* to transfer
   (§ 2.5). Directly testable.
3. **Palette-cube pathologies of `ABGR2222`.** No published work was found on
   near-neutral colours emerging saturated because their channels straddle a rounding
   boundary — the (215, 213, 211) → (255, 255, 170) case. The general principle
   (choose colours from the device gamut, do not quantise into it) is S-52's colour
   tokens; the specific failure mode of *this* cube is this project's own. Pebble's
   published 64-colour palette files are the nearest artifact and they document a
   palette, not its traps.
4. **Which palette codes antialiasing actually produces on this pipeline, and at what
   viewport coverage.** The standard requires fringes not to be confusable (§ 1.4); it
   does not tell you which codes your rasteriser emits. The 3–5 % `0xD5` measurement
   is exactly the right kind of evidence and has no analogue in the literature.
5. **Dither fusion versus graininess at 0.126 mm.** The two available data points
   (Pebble's practice, E Ink's patent) point in opposite directions and neither gives
   numbers (§ 4.5). The 2 × 2-cell arithmetic predicts fusion for adjacent levels and
   noise for distant ones; that prediction needs the panel to settle it.
6. **Whether a 20 px boundary-label ring is worth 30 % of the visible map on a 240 px
   circle.** The orbital-labelling study's own limitations section says smartwatch
   generalisation is unvalidated, and its label budget assumes twice this
   circumference (§ 3.2).
7. **Backlight blue shift as a palette constraint.** No source found addresses a
   transflective panel whose backlight pushes white to cyan, i.e. a display with two
   materially different colour appearances depending on illumination mode. Pebble's
   uncorrected/sunlight pair is the closest published practice and it covers ambient
   variation, not a colour-shifting backlight. A two-appearance palette document is
   novel work here.
8. **Tile edge length under a RAM-resident cache with 7–9 ms flash reads.** No
   published guidance (§ 4.7). The trade is between cache footprint and cross-tile
   label context, and it is decided by the labelling architecture.
9. **Whether linearisation helps a *running* watch with GPS and no interaction.**
   StripeMaps was indoors, without positioning, scroll-driven, N = 16 (§ 3.1, § 4.1).
   Every one of those differs here.
10. **Byte-reproducible rasterisation with palette-first styling.** The literature
    regards one-pixel instability as a defect to be eliminated, not managed
    (§ 2.6); how to eliminate it in *this* pipeline is engineering specific to it.

---

## 7. Sources

All retrieved **2026-08-13** unless noted. "Full text" means the complete document was
read (PDFs were fetched and converted locally with `pdftotext`); "part" names what was
read; "abstract only" and "snippet only" mean exactly that.

**Cartographic legibility and generalization**

1. Ledermann, F. (2021). *Small differences: Limits of Legibility of Cartographic
   Symbols on High- and Ultra-High-Resolution Mobile Displays.* Proc. 16th Int. Conf.
   on Location Based Services (LBS 2021), pp. 177–182. DOI 10.34726/1785.
   https://repositum.tuwien.at/bitstream/20.500.12708/18855/3/Ledermann-2021-Small%20differences%20Limits%20of%20Legibility%20of%20Cartographic%20Sym...-vor.pdf
   — **full text.**
2. Ledermann, F. (2022). *Empirical assessment of minimum dimensions for cartographic
   symbology on modern digital displays* (doctoral dissertation, TU Wien).
   https://repositum.tuwien.at/bitstream/20.500.12708/139892/1/Ledermann%20Florian%20-%202022%20-%20Empirical%20assessment%20of%20minimum%20dimensions%20for...pdf
   — **part**: §3.2.4 (Table 3.2, pixel size / logMAR / ppi), §3.3–3.3.1 (history of
   minimum dimensions, Chlupac 1982, Swiss Cartographic Society, Robinson's 2′),
   §8.1 (general guidelines) and §8.2 with Table 8.1 in full. Chapters 4–7 (the three
   studies' methods and per-task analyses) not read.
3. Ledermann, F. (2022). *The Effect of Display Pixel Density on the Minimum Legible
   Size of Fundamental Cartographic Symbols.* The Cartographic Journal.
   https://www.tandfonline.com/doi/full/10.1080/00087041.2022.2055938 — **not
   retrieved** (publisher 403). Content is covered by (2).
4. Ledermann, F. (2023). *Minimum dimensions for cartographic symbology – history,
   rationale and relevance in the digital age.* International Journal of Cartography.
   https://www.tandfonline.com/doi/full/10.1080/23729333.2023.2165218 — **not
   retrieved** (403). Overlaps §3.3 of (2).
5. Roth, R. E., Brewer, C. A., & Stryker, M. S. (2011). *A typology of operators for
   maintaining legible map designs at multiple scales.* Cartographic Perspectives 68,
   29–64. DOI 10.14714/CP68.7.
   https://cartographicperspectives.org/index.php/journal/article/view/cp68-roth-et-al
   — **abstract only**, plus the four operator category names (content, geometry,
   symbol, label) from the landing page. The full PDF was not read.
6. Töpfer, F., & Pillewizer, W. (1966). *The Principles of Selection.* The
   Cartographic Journal 3(1), 10–16.
   https://www.tandfonline.com/doi/abs/10.1179/caj.1966.3.1.10 — **snippet only**
   (paywalled). The Radical Law formula and its per-dimension exponents are quoted
   from search snippets of secondary sources, not from Töpfer.
7. Roth, R. E., Çöltekin, A., Delazari, L., Denney, B., Mendonça, A., Shen, J.,
   Stachoň, Z., & Wu, M. (2023). *Designing maps & visualizations for mobile devices:
   A collaborative research agenda.* Abstracts of the ICA 6, 213.
   https://ica-abs.copernicus.org/articles/6/213/2023/ica-abs-6-213-2023.pdf —
   **full text** (2-page abstract). The five design dimensions are: scale and
   generalization; projections; symbolization and visual hierarchy; toponymy and
   typography; user interaction. The full journal article (J. Location Based Services,
   DOI 10.1080/17489725.2023.2251423, and the 2024 ICA collective agenda) was **not
   retrieved** (403), so the "20 research challenges" enumeration is **snippet only**
   and I have not reproduced individual challenge statements.
8. Axis Maps. *Visual Variables* (cartography guide).
   https://www.axismaps.com/guide/visual-variables — **read**; practitioner guide, not
   a primary research source. Bertin (1967/1983) itself was not consulted.

**Small-screen and smartwatch cartography**

9. Wenig, D., Schöning, J., Hecht, B., & Malaka, R. (2015). *StripeMaps: Improving
   Map-based Pedestrian Navigation for Smartwatches.* MobileHCI '15, DOI
   10.1145/2785830.2785862.
   https://brenthecht.com/publications/MobileHCI_2015_StripeMaps.pdf — **full text**
   (author's copy; all numbers in § 3.1 are from this text).
10. Utebaliyeva, M. (2019). *The use of maps on smartwatches* (MSc thesis, ITC /
    University of Twente, joint MSc Cartography; supervisor C. P. J. M. van Elzakker).
    https://cartographymaster.eu/wp-content/theses/2019_Utebaliyeva_Thesis.pdf —
    **part**: abstract, §6.5 recommendations, Tables 7-1 and 7-2, §7.3 limitations.
    Method chapters read only in outline; the study is qualitative (eye-tracking,
    think-aloud, interviews) and the author explicitly notes small N, selection bias
    toward geographers, and participants' familiarity with the test area.
11. Searched for independent replication or published criticism of StripeMaps
    (multiple query formulations, 2016–2026): **none found**. Follow-up work located
    is by the same group — *ScrollingHome* (MobileHCI '16, DOI 10.1145/2935334.2935373)
    and *The aesthetics of StripeMaps* (MobileHCI '16 adjunct, DOI
    10.1145/2957265.2965021) — **titles/metadata only**, not read.

**Labelling**

12. Bekos, M. A., Niedermann, B., & Nöllenburg, M. (2019). *External Labeling
    Techniques: A Taxonomy and Survey.* Computer Graphics Forum 38(3), 833–860;
    arXiv:1902.01454. https://arxiv.org/abs/1902.01454 — **abstract only.**
13. Wallinger, M., Bonerath, A., Terziadis, S., Wulms, J., & Nöllenburg, M. (2026).
    *Clarity and Computational Efficiency of Orbital Boundary Labeling.*
    arXiv:2603.08657. https://arxiv.org/html/2603.08657 — **part**: abstract, study
    design and results, the smartwatch sizing passage, the stimulus geometry, and the
    limitations section. Algorithmic sections not read.
14. Gemsa, A., Nöllenburg, M., & Rutter, I. (2014). *Evaluation of Labeling Strategies
    for Rotating Maps.* arXiv:1404.1849. https://arxiv.org/pdf/1404.1849 — **part**:
    abstract, introduction and the consistency-model discussion. Note this is an
    *algorithmic* evaluation on real-world instances; it contains **no user study**.
15. Wigdor, D., & Balakrishnan, R. (2005). *Empirical Investigation into the Effect of
    Orientation on Text Readability in Tabletop Displays.* ECSCW 2005.
    https://www.dgp.toronto.edu/~ravin/papers/ecscw2005_textorientation.pdf —
    **part**: abstract, related-work numbers, and the results/discussion passages
    quoted in § 2.1. Caveat: a large high-resolution tabletop display, so this
    isolates the *perceptual* cost of rotation and says nothing about rasterisation.
16. Kurokawa, K. (1988). *The effect of rotation on legibility of dot-matrix
    characters* (MSc thesis, Virginia Tech).
    https://vtechworks.lib.vt.edu/items/28a91140-994c-4730-b78f-eb34b3a7cd39 —
    **abstract and library metadata only** (full text available at that record but not
    read). The relevant conclusion — rotation harms legibility "mainly through the
    distortion of dot-matrix patterns" — is from the record's abstract.
17. Čmolík, L., et al. (2020). *Mixed Labeling: Integrating Internal and External
    Labels.* https://www.ac.tuwien.ac.at/files/pub/Cmolik2020.pdf — **not retrieved**
    (exceeded fetch size limit). Mentioned only because a mixed internal/external
    labelling policy is an obvious middle path for § 3.2; I have not verified its
    contents or its user study.

**Standards: marine, automotive, aviation, ergonomics, accessibility**

18. International Hydrographic Organization (2015). *S-52: Specifications for Chart
    Content and Display Aspects of ECDIS*, Edition 6.1.1, June 2015.
    https://iho.int/uploads/user/pubs/standards/s-52/S-52%20Edition%206.1.1%20-%20June%202015.pdf
    — **part**, read in full for: §2.1.1 (design guidelines), §2.2.4 (redundant
    coding), §2.2.5, §3.1.5 (sizes and fonts), §3.1.6 (orientation), §3.2 (Display
    Base / Standard Display / Other Information), §3.4.2–3.4.4 (text panels), §4.1–4.2
    (colour specification and assignment). Presentation Library Part I (the actual
    colour tables and symbol dimensions) **not retrieved**.
19. FAA (2003). *Human Factors Design Standard* (HF-STD-001), Chapter 5, *Displays and
    printers*.
    https://hf.tc.faa.gov/hfds/download-hfds/hfds_pdfs/Ch5_Displays_and_printers.pdf
    — **part**: §5.2.1 (flicker, colour fringes, jitter), §5.2.2 (luminance and
    contrast), §5.2.3 (glare), §5.2.4 (resolution), §5.3 (flat panels, including
    Exhibits 5.3.3, 5.3.4 and §5.3.8 polarity). Later sections (LCD, plasma,
    large-screen, colour usage at 5-21) skimmed by keyword only.
20. ISO 15008:2017, *Road vehicles — Ergonomic aspects of transport information and
    control systems — Specifications and test procedures for in-vehicle visual
    presentation*. https://www.iso.org/standard/62784.html — **preview only**: the
    iTeh sample PDF
    (https://cdn.standards.iteh.ai/samples/62784/ac1daa21f49545c69dc9a5b33259a059/ISO-15008-2017.pdf)
    was read in full and contains front matter, terms, and clauses 4.1–4.2 (including
    that contrast measurement follows SAE J1757/1 and the direct-sunlight critical
    specular line). **The requirement tables are not in the sample.** The widely-quoted
    character-height ladder of **20′ recommended / 16′ acceptable / 12′ minimum**, and
    the sunlight contrast figure of 2:1, are from **search snippets only** and are
    marked unverified throughout this report. The ISO Online Browsing Platform
    returned 403.
21. IEC 60945, as quoted in S-52 §3.1.5: "character size in mm be not less than 3.5 x
    the viewing distance in metres." — **quoted via (18)**; IEC 60945 itself not
    retrieved.
22. W3C. *Understanding SC 1.4.3: Contrast (Minimum)*, WCAG 2.1.
    https://www.w3.org/WAI/WCAG21/Understanding/contrast-minimum.html — **full text.**
23. ISO 9241-303:2011, *Ergonomics of human–system interaction — Part 303:
    Requirements for electronic visual displays*. Sample:
    https://cdn.standards.iteh.ai/samples/57992/bddfd91165b444f6b9815a6993feadc5/ISO-9241-303-2011.pdf
    — **part**: the sample includes clauses 5.1–5.3 in normative text (including §5.2
    on the luminous environment and reflective displays, and §5.2.5 scoped to emissive
    displays). Clauses 5.4–5.7 (luminance contrast, character height, luminance
    coding) appear in the table of contents only.
24. ISO/TR 9241-313:2025, *Optical measurement methods for reflective displays*.
    https://www.iso.org/obp/ui#!iso:std:iso:tr:9241:-313:ed-1:v1:en — **not
    retrieved** (403). Listed because it is the standard that most directly addresses
    § 4.9 and should be obtained.
25. Hertel, D. (2018). Two papers on applying IEC/ISO optical measurement methods to
    reflective e-paper and predicting reflective colours under ambient illumination
    (SID Symposium Digest, DOI 10.1002/sdtp.12509; Color Research & Application, DOI
    10.1002/col.22279) — **snippet only**, both paywalled.
26. Voelz, et al. (2025). *Optimized Workflow for Fast and Precise ISO 15008 Contrast
    Evaluation Under Ambient Light.* Information Display.
    https://sid.onlinelibrary.wiley.com/doi/full/10.1002/msid.1549 — **not retrieved**
    (403).
27. RTCA DO-257A, SAE ARP5289, MIL-STD-2525, FAA AC 23.1311-1C, and the Navy
    warfighting display symbology report (DTIC ADA484484) — **not retrieved**:
    paywalled or 403. Aviation and military symbology minimums are therefore not
    covered in this report.

**Nearest hardware precedent: Pebble**

28. Pebble Developers (via Rebble archive). *Design in the Round.*
    https://developer.rebble.io/guides/design-and-interaction/in-the-round/ —
    **read in full** (2-px edge border, avoid thin rings, centre-focused menus,
    paginate rather than scroll, ContentIndicator, be willing to design a separate
    round UI).
29. Pebble Developers. *App Resources: Images.*
    https://developer.rebble.io/guides/app-resources/images/ — **read in full**:
    per-platform colour counts (Aplite/Diorite/Flint 2 colours; Basalt/Chalk/Emery
    64), bitmap formats (1Bit, 8Bit, 1/2/4-BitPalette, Smallest,
    SmallestPalette), and downloadable 64-colour palette files for Photoshop, GIMP,
    ImageMagick, Illustrator and Aseprite — including separate **uncorrected** and
    **sunlight colour-corrected** Aseprite palettes. The page does **not** discuss
    2-bits-per-channel encoding or give dithering advice.
30. Pebble Developers. *Color Picker Tool.*
    https://developer.rebble.io/guides/tools-and-resources/color-picker/ — **part**
    (JavaScript tool; the page confirms "Uncorrected" and "Sunlight" viewing modes and
    per-colour SDK constants, but the palette values themselves did not render to
    text).
31. Pebble Developers (community). *Simple Dithering Library.*
    https://developer.rebble.io/community/libraries/simple-dithering-library/ —
    **read in full**: ordered and random dithering helpers; "By using 50 % dithering,
    developers are able to combine every color available in Basalt, resulting in a new
    color palette of about 343 different colors", justified as perceptually fusing.
    Community library documentation, not vendor guidance or research.
32. E Ink Corporation. Bouchard, A., & Telfer, S. J. *Color sets for low resolution
    dithering in reflective color displays*, US 10,353,266 B2, granted 2019-07-16.
    https://patents.google.com/patent/US10353266 — **read** (via fetch): the
    graininess statement and the resolution/viewing-distance caveat quoted in § 4.5;
    no quantitative guidance in the document.

**Vendor practice (§ 4.6 of the brief)**

33. Garmin Connect IQ developer forum, thread *Garmin MIP / AMOLED Palette*.
    https://forums.garmin.com/developer/connect-iq/f/discussion/419763/garmin-mip-amoled-palette/1966943
    — **read**: 64 fixed colour values, community-built grid apps to enumerate them,
    **no official Garmin documentation and no Garmin staff response in the thread**,
    and the observation that a device screenshot matches the simulator while the panel
    does not match to the eye.
34. Garmin Connect IQ FAQ, *How do I use a MapView?*
    https://developer.garmin.com/connect-iq/connect-iq-faq/how-do-i-use-a-mapview/ —
    **not retrieved** (the FAQ index rendered, the article body did not).
35. mkgmap, *The mkgmap TYP file compiler.* https://www.mkgmap.org.uk/doc/typ-compiler
    — **snippet only.** Reverse-engineered documentation of Garmin's on-device
    symbology format is the most substantive public material on Garmin map rendering
    that this review found: lines and polygons limited to two colours (one if
    transparency is used), separate Day/Night XPM bitmaps constrained to identical
    dimensions, and explicit draw-order layers. This is community reverse engineering,
    **not** Garmin publication.
36. Searches for published material from **Suunto, Coros and Wahoo** on map rendering,
    palette or label strategy returned nothing substantive. **Confirmed: the
    expectation in the brief holds.** The only structured public knowledge about
    consumer wearable map rendering is (a) Pebble's archived developer documentation,
    which is genuinely detailed about colour and round layout but says nothing about
    maps, and (b) the OSM community's reverse engineering of Garmin's formats. Effort
    spent looking for vendor guidance is effort wasted; effort spent reading S-52 and
    Pebble's round/colour guides is not.

**Style-sheet practice**

37. OpenStreetMap Carto, pull request #1682, *Start rendering of minor roads later*
    (merged 2015-07-27).
    https://github.com/openstreetmap-carto/openstreetmap-carto/pull/1682 — **read**
    (diff summary, rationale and review discussion via fetch).
38. OpenStreetMap Carto, issue #5068, *Assessing options for improved rendering of
    footways* — **read**; still open, labelled "input needed".
    https://github.com/openstreetmap-carto/openstreetmap-carto/issues/5068
39. Mapzen, *Get more for less with 512 pixel tiles*
    (https://www.mapzen.com/blog/512-pixel-tiles-and-basemaps/) and MapTiler
    documentation on 256 vs 512 tiles — **snippet only**; both argue from network
    request count and label placement, not from client RAM.

**Wear OS**

40. Android Developers, *Adaptive design* / *Prevent text truncation and content
    clipping* / *Screen sizes* (Wear OS design guidance).
    https://developer.android.com/design/ui/wear/guides/foundations/adaptive-design ,
    https://developer.android.com/design/ui/wear/guides/m2-5/behaviors-and-patterns/clipping ,
    https://developer.android.com/design/ui/wear/guides/m2-5/foundations/screen-sizes
    — **all three read in full.** They give breakpoints (192–224 dp small, 225 dp+
    large) and the percentage-margin rule, and they **do not** publish safe-area
    percentages, minimum text sizes, or a round-vs-square area figure. A widely
    repeated "round screens have 22 % less UI space" line appeared in search results
    attributed to Wear OS material but **could not be verified on any official page I
    read** — treat it as unverified; the geometric figure (1 − π/4 ≈ 21.5 %) is
    arithmetic, not a citation.
41. Apple, *Designing for watchOS* (Human Interface Guidelines).
    https://developer.apple.com/design/human-interface-guidelines/designing-for-watchos
    — **not retrieved**: the page is JavaScript-rendered and returned no article body.
    watchOS guidance on glance duration, minimum text sizes and safe areas is
    therefore **not covered** in this report.

**Haptic and landmark-based guidance**

42. Vibrotactile wayfinding literature (tactile belts, wrist cueing, waypoint
    navigation) — **snippet only**, across abstracts on ACM/ResearchGate/PMC. The
    direction-recognition figures quoted in § 3.3 (96 % cardinal, 69 % with
    inter-cardinals) are from search snippets and are **not verified against a full
    text**. Treated as indicative only.
</content>
</invoke>
