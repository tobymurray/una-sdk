# Units and Display

Every measurement inside a UNA app is SI: metres, metres per second, seconds
per metre. The user's metric/imperial preference is a **presentation** concern,
applied exactly once, at the moment a measurement becomes text.

`SDK::Units` owns that moment.

## Why this exists

The natural way to write a watch face is to convert early and pass a `bool`
along with the number:

```cpp
// Don't.
void TrackFaceTotal::setDistance(float dist, bool isImperial);
```

Three things go wrong, and all three had gone wrong in the example apps:

1. **The value and its label are separate decisions.** The caller converts; the
   widget picks the "km"/"mi" string. Nothing makes them agree.
2. **`float dist` has no unit.** Two setters called `setDistance(float)` in the
   same app took metres and kilometres respectively.
3. **Precision is written out longhand at each call site, and the ladders
   drift.** The same 15 km rendered as `15.00` on one face and `15.0` on
   another.

A `Reading` fixes all three by construction: one call produces the converted
number, its precision and its unit together.

## The two layers

| Header | Depends on | Role |
| --- | --- | --- |
| `SDK/Units/Units.hpp` | nothing | Decides *what* to show. Pure, `constexpr`, host-testable. |
| `SDK/GUI/UnitText.hpp` | TouchGFX | Puts that decision on screen. |

## Using it

Hold one `Formatter` per screen and set it from the user's preference:

```cpp
class TrackView : public TrackViewBase
{
    ...
    SDK::Units::Formatter mUnits;   // one byte; the screen's only unit state
};

void TrackView::setConfig(bool isImperial, ...)
{
    mUnits.setImperial(isImperial);
}
```

Feed it SI and hand the result to your widgets:

```cpp
void TrackView::setTrackData(const Track::Data& data)
{
    // data.distance is metres; data.pace is seconds per metre.
    trackFaceTotal.setDistance(mUnits.distance(data.distance, App::Display::kDistance));
    trackFaceTotal.setPace(mUnits.pace(data.pace));
}
```

Widgets take `Reading`s and hold no unit state at all:

```cpp
void TrackFaceTotal::setDistance(const SDK::Units::Reading& distance)
{
    SDK::Gui::setReading(distance,
                         {&distanceValue, distanceValueBuffer, DISTANCEVALUE_SIZE},
                         {&distanceUnits, distanceUnitsBuffer, DISTANCEUNITS_SIZE},
                         App::unitTextId);
}
```

Because the widget signature takes a `Reading`, passing raw metres is a
compile error rather than a wrong number on a watch.

## Unit labels are per-app

TouchGFX generates `TypedTextId`s per app, so the SDK cannot name `T_TEXT_KM`.
A `Reading` carries an abstract `Units::Label` and each app maps it once —
the same arrangement `SDK/GUI/SensorStatusRow.hpp` uses for BitmapIds:

```cpp
// gui/include/gui/UnitLabels.hpp -- the only place this app names its units.
namespace App
{
inline touchgfx::TypedTextId unitTextId(SDK::Units::Label label)
{
    switch (label) {
        case SDK::Units::Label::Mile:  return T_TEXT_MI;
        case SDK::Units::Label::Metre: return T_TEXT_M;
        default:                       return T_TEXT_KM;
    }
}
}
```

## Precision policy

`Precision` is data, not code, so every face agrees:

```cpp
// SDK::Units::Policy::kFourChar -- 9.99 / 99.9 / 999
inline constexpr Precision kFourChar{
    false, 0.0f, {{10.0f, 2}, {100.0f, 1}, {}}, 2, 0,
};
```

Four significant characters at every magnitude, which is what fixed-width
TouchGFX text areas need. Apps supply only their own data floor:

```cpp
namespace App::Display
{
constexpr SDK::Units::Precision kDistance =
    SDK::Units::withPlaceholderBelow(SDK::Units::Policy::kDistance, 0.0f);
}
```

A value under the floor comes back with `valid == false` and the widget draws
`---` instead of a number. The label is still drawn: the unit a screen is
configured for does not stop being true because the GPS dropped out.

## Pace

Pace renders as `m:ss`, not as a decimal, so it has its own type:

```cpp
const SDK::Units::PaceReading p = mUnits.pace(secondsPerMetre);
```

`SDK::Gui::setPaceHoursMinutes()` renders `1:06` past the hour and `m:ss`
below it. `PaceReading` also exposes `hms` and `totalMinutes()` for a design
that would rather show `66:40`. Rounding is to the nearest whole second before
decomposition, so a 59.6 s remainder carries into the next minute instead of
being truncated away.

## Buffer-only formatting

`ScrollList` rows take a finished string rather than owning a text area.
`SDK::Gui::formatValue()` and `formatPaceHoursMinutes()` write into a plain
buffer under exactly the same rules, so a lap row and the track face cannot
disagree.

## Cost

`Units.hpp` is header-only, `constexpr` throughout, and allocation-free.
`Formatter` is one byte and trivially copyable, so a screen that used to hold
`bool mIsImperial` is no larger.

Measured on `RunningGUI.elf` (arm-none-eabi-g++ 12.2, `-Os`), adopting this
across Running's activity display path costs:

```
            text     data      bss
before    391988    18144    82300
after     392588    18152    82300
             +600       +8        0   (+0.15% flash)
```

Per widget the setters got much smaller -- `TrackFaceTotal::setDistance` went
from 240 bytes to 72, because the if-ladder it used to carry now lives in one
shared place. The net is still positive because the module adds things that did
not exist before: the `Precision` tables and the unified pace rounding.

The drawing functions are marked `SDK_GUI_NO_INLINE` deliberately. Left to
inline they are duplicated into every measurement widget, which measured
**+1.4 KB** -- worse than the hand-written ladders they replace. They stay in
the header rather than moving to a `.cpp` because the SDK ships no GUI
translation unit and the simulator build compiles none; as weak definitions the
linker folds them to a single copy under `-ffunction-sections --gc-sections`.

`Units.hpp` itself stays header-only and fully `constexpr` -- it wants to fold
at compile time, and does.

## Reference

- `Libs/Header/SDK/Units/Units.hpp`
- `Libs/Header/SDK/GUI/UnitText.hpp`
- `Tests/Host/utils/Units_test.cpp`
- Worked example: `Examples/Apps/Running`
