# Typeface metrics on a four-level panel

2026-09-07. Sixteen OFL/Apache text faces measured through the watch's own glyph
pipeline, at the cap heights a 240 px round panel can actually show, to price the
published legibility guidance in
[`../../Research/2026-08-13-watch-cartography-prior-art.md`](../../Research/2026-08-13-watch-cartography-prior-art.md)
§ 1.8–1.9 against real typefaces rather than against a nominal em.

**No watch was involved.** Every number here is FreeType rasterizing a font file on
a host. That is enough to settle what a typeface *is* — its cap-to-em ratio, its
advance widths, how its stems land on a pixel grid, whether a counter survives
quantisation — and it settles nothing about what the glass does with the result.
The prior-art document's § 1.10 is the standing warning, and it applies to
everything below.

## Why this branch and not the app repo

The typeface *decision* for the apps is not here: it belongs to `tobymurray/watch-apps`,
whose `Docs/TEXT.md` is the design record and whose `TextKit/README.md` is the crate's.
Its byte totals, its 376-glyph parity result against the TouchGFX converter, and its
per-app footprints are owned there and are deliberately not restated here.

What earns a place here is the part that outlives any one app: the panel is coarse,
round, four levels a channel and bright-on-dark, and those constraints price typefaces
in ways that are not obvious and that no SDK checkout can tell you. A second app
choosing a face should be able to start from these tables.

## The harness is the shipped pipeline

`tools/measure.py` reproduces the rasterization the apps' generator performs —
FreeType's light autohinter, coverage rounded to the nearest third at 43 / 128 / 213,
blank edge rows and columns cropped. `tools/verify.py` is the check that it really is
the same pipeline: it rebuilds four faces the watch ships and compares the glyph-byte
totals against the figures `watch-apps`' `TextKit/README.md` records. All four match.

Falsified by a different FreeType. This ran on **2.13.2** via `freetype-py 2.5.1`,
which is the version that tree pins; `tools/hintpath.py` and `tools/verify.py` are
what would notice a change.

## 1. Cap height is the axis, and the prior-art table's x-height constant is low

§ 1.9 converts this project's device observations to angles by taking **capital height
as 0.7 em and x-height as 0.5 em**. Measured across the sixteen faces
(`tools/ratio.py`):

| | measured range | the assumption | worst error |
|---|---|---|---|
| cap / em | 0.660 – 0.728 | 0.700 | −5.7 % (Source Sans 3), +3.9 % (Inter) |
| x / em | 0.486 – 0.548 | 0.500 | **+9.6 %** (Poppins) |

The cap constant is sound — 0.7 is within 4 % of every face measured, and within
0.3 % of Poppins, which is the face those device observations were made in. The
x-height constant is not: fifteen of sixteen faces sit **above** 0.5, so § 1.9's
x-height arcminute column understates the angle by up to a tenth. The fix is one
number per face, and the ratio is in the `OS/2` table of any font, so this is
cheap to keep true.

The practical consequence is larger than the arithmetic. Because cap/em varies by
10 % across faces and x/cap by more, **a pixel em size is not a comparable
quantity between typefaces** — the same 18 px em is a 13 px capital in one face and
a 12 px capital in another. Every table below is normalised on rendered cap height,
which is also what the FAA matrix rules in § 1.8 are stated in.

## 2. The FAA height/width rule does not discriminate between text faces

§ 1.8 quotes FAA HFDS: *a 13 px-high capital wants 6–12 px of width, preferred 9.*
Mean ink width of `A`–`Z` at a rendered 13 px capital (`tools/faa.py`):

| face | cap ink width | face | cap ink width |
|---|---|---|---|
| Barlow SemiCondensed | **8.9** | Manrope | 10.4 |
| Roboto Condensed | 9.2 | Inter Tight | 10.5 |
| Fira Sans | 9.3 | IBM Plex Sans | 10.7 |
| Barlow | 9.9 | Roboto | 10.7 |
| Source Sans 3 | 10.0 | Inter | 10.7 |
| Poppins | 10.2 | Public Sans | 11.0 |
| Noto Sans | 10.3 | Montserrat | 11.6 |
| Open Sans | 10.3 | Archivo | 11.7 |

**All sixteen sit inside the 6–12 band, and exactly one reaches the preferred 9.**
So the rule is not a filter on typeface choice: it is satisfied by any normal-width
sans, and the "preferred 9" is only reachable by going condensed. Anyone reading
§ 1.8 as a constraint that narrows the candidate list should stop — it does not.
It does mark the two outliers, Archivo and Montserrat, as the faces that will run
out of chord first.

Width still matters on a round panel, but as a **layout** budget rather than a
legibility floor, and the axis it varies on is not the capitals. See § 4.

## 3. The FAA stroke-width rule is the one that binds, and it says use the bold weight

§ 1.8 also quotes: *stroke width 1–2 px for a 9–12 px cap height, 2 px for 13–14.*
Effective stem width, taken as the summed column levels of `I` divided by 3 through
the stem's middle band, so a level-2 and a level-3 column count as 1.67 px of ink
rather than 2 px of geometry (`tools/faa.py`):

| face | Reg cap 9 | Reg cap 11 | Reg cap 13 | SemiBold cap 12 | SemiBold cap 14 |
|---|---|---|---|---|---|
| Poppins | 1.33 | 1.33 | 1.67 | 2.33 | 3.00 |
| Inter Tight | 1.00 | 1.33 | 1.67 | 2.00 | 2.67 |
| Roboto | 1.33 | 1.33 | 1.67 | 2.33 | 2.67 |
| Fira Sans | 1.33 | 1.67 | 1.67 | 2.67 | 3.00 |
| IBM Plex Sans | 1.00 | 1.33 | 1.67 | 2.33 | 2.67 |
| Source Sans 3 | 1.00 | 1.33 | 1.67 | 2.33 | 2.67 |
| Open Sans | 1.00 | 1.33 | 1.33 | 2.00 | 2.33 |
| Barlow SemiCondensed | 1.00 | 1.00 | 1.33 | 2.33 | 2.33 |
| Manrope | 1.00 | 1.00 | 1.33 | **1.67** | 2.00 |
| Montserrat | 1.00 | 1.00 | 1.33 | 2.00 | 2.67 |

Every Regular weight measured lands at **1.33 to 2.00 px at a 13 px capital, where
the rule asks for 2** — fifteen of the sixteen are under it and the sixteenth, Public
Sans, is exactly at it. Every SemiBold at cap 12–14 lands in or above the 2–3 px band,
the one exception being Manrope at cap 12.

This is an independent corroboration of a conclusion `watch-apps`' `Docs/TEXT.md`
reached from its own stem measurements and stated as *prefer SemiBold for anything
small that must read as crisp*. Two unrelated routes — a US aviation display standard
and a count of quantised stem columns — put the crossover in the same place. It is
also the reason the choice of **weight** matters more here than the choice of family:
switching family moves a stem by a third of a pixel, switching weight moves it by a
whole one.

FreeType's light autohinter snaps vertically only, so it does not help: a stem lands
where the outline puts it, hinted or not.

## 4. Digits are a separate axis from capitals, and geometric faces are expensive there

Advance width per unit of cap height, from font units, so no rasterizer is involved
(`tools/width.py`):

| face | `0`–`9` | `A`–`Z` | face | `0`–`9` | `A`–`Z` |
|---|---|---|---|---|---|
| Barlow SemiCondensed | 0.642 | 0.745 | Roboto | 0.791 | 0.892 |
| Roboto Condensed | 0.694 | 0.778 | Noto Sans | 0.801 | 0.889 |
| Barlow | 0.723 | 0.862 | Open Sans | 0.801 | 0.880 |
| Fira Sans | 0.736 | 0.846 | Inter | 0.815 | 0.937 |
| Inter Tight | 0.749 | 0.860 | Public Sans | 0.831 | 0.924 |
| Source Sans 3 | 0.753 | 0.862 | Montserrat | 0.832 | 1.023 |
| Manrope | 0.780 | 0.873 | IBM Plex Sans | 0.860 | 0.908 |
| | | | Poppins | **0.910** | 0.914 |

Fifteen of the sixteen draw digits narrower than their own capitals, by 5 % to 19 %.
**Poppins is the exception: its digits are within half a percent of its capitals**, and
in absolute terms the widest of the sixteen. IBM Plex Sans is next closest at 5 %.

This is not a property of geometric faces in general — Montserrat is as geometric as
Poppins and narrows its digits by 19 %. It is specific to how Poppins draws a `0`:
the same near-circle as its `O`, where a text face draws a narrower oval.

For a watch this is the axis that matters, because a numeric readout is the largest
text on the panel and the chord is narrowest where large text goes. Rasterized at a
42 px capital, `08:42` costs 176 px in Poppins and 136 px in Roboto Condensed — 40 px,
a sixth of the panel, at identical cap height (`tools/clock.py`,
`sheets/clock-cap42-16-faces.png`). A face chosen on the strength of its capitals can
be a poor choice for a clock.

## 5. Confusable separation is a trade, not a ranking

Normalised disagreement between the quantised bitmaps of a pair, aligned on baseline
and left bearing: 0 is an identical bitmap, higher is more distinguishable. At a 14 px
capital in SemiBold (`tools/idface.py`):

| face | `I`/`1` | `l`/`1` | `S`/`5` | `Z`/`2` | `G`/`6` | `O`/`0` | `I`/`l` | `a`/`o` |
|---|---|---|---|---|---|---|---|---|
| Poppins | **1.82** | **1.84** | **0.63** | **0.77** | 0.57 | 0.68 | 0.02 | 0.22 |
| Inter | 1.53 | 1.53 | 0.48 | 0.51 | 0.56 | 0.57 | 0.00 | 0.57 |
| Inter Tight | 1.53 | 1.53 | 0.49 | 0.50 | 0.56 | 0.54 | 0.00 | 0.52 |
| Manrope | 1.50 | 1.50 | 0.60 | 0.62 | 0.73 | 0.69 | 0.00 | 0.58 |
| Open Sans | 1.55 | 1.64 | 0.31 | 0.50 | 0.72 | **0.94** | 0.33 | **0.66** |
| Noto Sans | 1.36 | 1.61 | 0.29 | 0.54 | 0.68 | 0.89 | 0.65 | 0.57 |
| Archivo | 1.39 | 1.49 | 0.47 | 0.48 | **0.75** | 0.83 | 0.30 | 0.62 |
| Fira Sans | 1.36 | 1.49 | 0.34 | 0.36 | 0.44 | 0.58 | 0.27 | 0.58 |
| Public Sans | 1.28 | 1.23 | 0.35 | 0.54 | 0.46 | 0.71 | 0.31 | 0.45 |
| Source Sans 3 | 1.23 | 1.32 | 0.39 | 0.42 | 0.62 | 0.88 | 0.22 | 0.59 |
| IBM Plex Sans | 1.07 | 1.15 | 0.31 | 0.51 | 0.61 | 0.60 | **0.77** | 0.54 |
| Montserrat | 0.86 | 1.09 | 0.33 | 0.39 | 0.66 | 0.72 | 0.30 | 0.65 |

Twelve of the sixteen; Roboto Condensed and Barlow SemiCondensed step from a 13 px
capital to a 15 px one and have no integer size that renders a 14 px one.

The finding worth carrying forward is that these do not co-vary. A face that
separates `I` from `l` does it with a tail on the `l`, and the same humanist
construction that gives it that tail closes the apertures of `S` and `Z`, which is
where it loses `S`/`5` and `Z`/`2`. A geometric face makes the opposite trade: open
`S` and `Z`, but `I` and `l` collapse to the same bar and single-storey `a` collapses
toward `o`.

So there is no face that is simply better at being read a character at a time, and
**which trade is right depends on the alphabet the string is drawn from.** For an
uppercase-and-digits string — a serial, a clock, a units label — the geometric trade
is the favourable one, and `I`/`l` is a pair that cannot occur. For arbitrary
mixed-case text the humanist trade is. `sheets/confusables-cap9-16-faces.png` is all
sixteen at a 9 px capital, magnified twelve times, which is the frame to look at
before trusting any of these numbers.

## 6. Four levels close some counters, and which ones is not guessable

At a 9 px capital in Regular, thirteen of `aeobdgpq469`'s counters survive
quantisation in twelve of the sixteen faces. Four faces close exactly one, and it is
the same one in all four — the `4`:

    Barlow, Barlow SemiCondensed, Manrope, Montserrat

For a watch whose text is largely numeric this is a hard failure mode rather than a
soft one, and it is invisible in a specimen at any reasonable size. `tools/sweep.py`
reports it per face and size.

## 7. `FT_LOAD_TARGET_LIGHT` uses the autohinter whatever the font ships

Relevant to any generator on this platform. Six of the sixteen faces carry TrueType
hinting bytecode in `fpgm`; Poppins does not. Rendering each face with and without
`FT_LOAD_NO_AUTOHINT` changes the bitmap for **all sixteen**, hinted and unhinted
alike (`tools/hintpath.py`), which means the light target selects FreeType's
autohinter and the font's own instructions are not consulted.

Consequence: the rasterization semantics are a property of the pipeline, not of the
font, so a generator's thresholds and its tests transfer to a new typeface unchanged.
A hinted face does not get a different quality regime here, and picking an unhinted
one buys no consistency it did not already have.

## 8. Correction: the panel does render intermediate levels, and text uses them heavily

The prior-art document's § 1.6 says *"this panel has no antialiasing and 25:1
contrast"* while discounting a study whose stimuli were antialiased. The contrast
figure is not in question; the antialiasing half is wrong. The framebuffer format is
readable in the SDK and gives more than two levels a channel, and the text pipeline
the watch ships uses them: measured over `A`–`Z`, `a`–`z`, `0`–`9`, the fraction of a
glyph's ink pixels sitting at an **intermediate** level rather than full is

| rendered cap | Regular | SemiBold |
|---|---|---|
| 9 px | 0.60 | 0.50 |
| 11 px | 0.65 | 0.39 |
| 13 px | 0.55 | — |
| 14 px | 0.54 | 0.39 |

Each cell is the smallest integer em that renders that capital: cap 9 is 13 px em,
cap 11 is 16, cap 13 is 18, cap 14 is 20.

So between a third and two thirds of the ink in every glyph on the watch today is a
grey, not white. § 1.6's argument — that the study's floor is one this hardware will
not reach — may well survive; the reason given for it does not.

What is **not** settled is whether those greys help. At two intermediate levels a
stem is as likely to be softened into illegibility as to be smoothed, and no
photograph exists either way. `watch-apps`' `Docs/TEXT.md` owes exactly that
photograph, with frames already rendered for it, and it is the same experiment that
would settle this. Nothing here should be read as evidence that four-level
antialiasing is worth its bytes.

## 9. Poppins' coverage, as a file

A stable property of a versioned font file, so this branch can keep it. Poppins 4.004,
the face every app in `watch-apps` currently draws with: ASCII 95/95, Latin-1
Supplement 96/96, Latin Extended-A 107/128, **Greek 1/144, Cyrillic 0/256**, and of
the symbols a watch reaches for, `°` `µ` `…` `–` `—` `‘` `’` `“` `”` `€` `•` are
present while **`′` (U+2032) and `♥` (U+2665) are not**.

Any localisation past Latin needs a second family whatever the mechanism. Among the
sixteen, Inter Tight carries Greek 121/144 and Cyrillic 254/256 plus both `′` and
`♥`; Noto Sans carries 121 and 256/256 but no `♥`; Source Sans 3 carries both symbols
with Greek 88 and Cyrillic 156. `tools/coverage.py` prints the matrix.

## Reproducing

```sh
python3 -m venv .venv && .venv/bin/pip install freetype-py==2.5.1 fonttools==4.60.2 numpy pillow
cd tools && sh download.sh          # fetches fifteen families, cuts static instances
POPPINS_DIR=~/git/watch-apps/TextKit/Fonts .venv/bin/python faa.py
```

Poppins is not vendored here. `cands.py` points `POPPINS_DIR` at a `watch-apps`
checkout, because the copy that matters is the one that tree's generator rasterizes,
and a second copy on this branch would be a restatement waiting to diverge.
`RESULTS.txt` is every table above as the tools printed it.

| file | what it answers |
|---|---|
| `tools/verify.py` | is the harness the shipped pipeline |
| `tools/faa.py` | the § 1.8 height/width, stroke-width and matrix rules, priced |
| `tools/ratio.py`, `tools/width.py` | cap/em, x/em, and advance per cap, from font units |
| `tools/sweep.py`, `tools/caps.py` | sixteen faces at equal x-height and at equal cap height |
| `tools/idface.py` | confusable pairs at a serial-number face |
| `tools/clock.py` | a large numeric readout at equal cap height |
| `tools/coverage.py` | code points, `fpgm`, and the symbols a watch reaches for |
| `tools/hintpath.py` | autohinter or the font's own bytecode |
| `tools/allsheets.py`, `tools/sheet.py` | the two contact sheets in `sheets/`: sixteen faces at a 9 px capital magnified 12×, and at a 42 px capital |
| `tools/download.sh`, `tools/instance.py` | fetch the fifteen families and cut static weight instances |
| `tools/check_readme.py` | the § 5 and § 8 tables above, which no other tool prints verbatim |
| `tools/apps.py`, `tools/poppins_profile.py` | the `watch-apps`-specific rows, kept because they are what `verify.py` checks against |

## What would settle the rest

One frame, in the photograph `watch-apps`' `Docs/TEXT.md` already owes: the
sixteen-face confusable sheet on the glass at a 9 px capital, in daylight and under
the frontlight. It answers § 8's open half and § 5's trade at the same time, and it
costs one shot alongside the three-condition frames that are already rendered.
