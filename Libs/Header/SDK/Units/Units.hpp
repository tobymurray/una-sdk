/**
 ******************************************************************************
 * @file    Units.hpp
 * @brief   Display-unit policy: convert an SI measurement into the value,
 *          precision and unit label that should be rendered for it.
 *
 ******************************************************************************
 *
 * Everything inside an app is SI: metres, metres per second, seconds per metre.
 * The user's metric/imperial preference is a *presentation* concern, so it is
 * applied exactly once, at the moment a measurement becomes text.
 *
 * This header owns that moment. A Formatter carries the unit system and turns
 * an SI value into a Reading -- the converted number, how many decimals it
 * should be printed with, the unit label that belongs to it, and whether there
 * is anything worth showing at all:
 *
 *     SDK::Units::Formatter units{SDK::Units::System::Imperial};
 *     SDK::Units::Reading   d = units.distance(metresFromGps);
 *     // d.value == miles, d.label == Label::Mile, d.decimals == 2
 *
 * Because one call produces the number *and* its label, the two cannot drift
 * apart -- which is the failure mode of passing a converted float and a
 * separate `bool isImperial` down to a widget.
 *
 * The header is free of TouchGFX and of any app dependency so that the policy
 * is unit-testable on the host; SDK/GUI/UnitText.hpp binds it to TouchGFX text
 * areas, and the app supplies the mapping from Label to its own generated
 * TypedTextIds.
 *
 ******************************************************************************
 */

#ifndef SDK_UNITS_UNITS_HPP
#define SDK_UNITS_UNITS_HPP

#include "SDK/Utils/Utils.hpp"

#include <cstdint>

namespace SDK::Units
{

// =============================================================================
// Vocabulary
// =============================================================================

/** @brief The user's measurement-system preference. */
enum class System : uint8_t
{
    Metric   = 0,
    Imperial = 1,
};

/**
 * @brief Abstract unit label.
 *
 * The SDK cannot name an app's unit strings: TouchGFX generates TypedTextIds
 * per app. So a Reading carries the unit it is expressed in, and the app maps
 * that to its own text id in one place (see SDK/GUI/UnitText.hpp).
 */
enum class Label : uint8_t
{
    None = 0,     ///< Dimensionless, or the unit is baked into the screen design.
    Kilometre,    ///< "km"
    Mile,         ///< "mi"
    Metre,        ///< "m"
    Foot,         ///< "ft"
    MinPerKm,     ///< "/km"
    MinPerMile,   ///< "/mi"
};

/** @brief Number of distinct Label values, for app-side lookup tables. */
inline constexpr uint8_t kLabelCount = static_cast<uint8_t>(Label::MinPerMile) + 1u;

/**
 * @brief A measurement that is ready to be drawn.
 *
 * @p value is already in the unit named by @p label -- never SI, unless the
 * unit system happens to be metric. @p valid is false when the underlying
 * measurement is missing or nonsensical, in which case the caller should draw
 * a placeholder rather than @p value.
 */
struct Reading
{
    float   value    = 0.0f;
    Label   label    = Label::None;
    uint8_t decimals = 0;
    bool    valid    = false;
};

/**
 * @brief A pace, ready to be drawn.
 *
 * Pace is a duration per unit distance, so it renders as m:ss rather than as a
 * decimal. @p hms holds the whole-second duration; use totalMinutes() for the
 * usual "m:ss" form, or hms.h / hms.m for an "h:mm" form when the pace is slow
 * enough to run past an hour.
 */
struct PaceReading
{
    SDK::Utils::HMS hms;
    Label           label = Label::None;
    bool            valid = false;

    /** @brief Minutes including any whole hours, for "m:ss" rendering. */
    constexpr uint16_t totalMinutes() const
    {
        return static_cast<uint16_t>(hms.h * 60u + hms.m);
    }
};

// =============================================================================
// Precision policy
// =============================================================================

/**
 * @brief How many decimals a value gets, as a function of its magnitude.
 *
 * Watch faces have fixed-width text areas, so precision has to shrink as a
 * number grows or it overflows its box. Tiers are tried in order and the first
 * one the value falls under wins; anything at or above every tier gets
 * @p fallbackDecimals.
 *
 * Keeping this as data rather than as an if-ladder at each call site is the
 * point: the ladders drift, a shared constant cannot.
 */
struct Precision
{
    struct Tier
    {
        float   below    = 0.0f;
        uint8_t decimals = 0;
    };

    static constexpr uint8_t kMaxTiers = 3;

    bool    usePlaceholder   = false;  ///< Whether @p placeholderBelow applies at all.
    float   placeholderBelow = 0.0f;   ///< Values under this are reported as !valid.
    Tier    tiers[kMaxTiers] = {};
    uint8_t tierCount        = 0;
    uint8_t fallbackDecimals = 0;

    constexpr uint8_t decimalsFor(float value) const
    {
        for (uint8_t i = 0; i < tierCount && i < kMaxTiers; ++i) {
            if (value < tiers[i].below) {
                return tiers[i].decimals;
            }
        }
        return fallbackDecimals;
    }

    constexpr bool isPresentable(float value) const
    {
        return !usePlaceholder || !(value < placeholderBelow);
    }
};

/**
 * @brief Copy of @p p that reports values below @p floorValue as !valid.
 *
 * @p floorValue is in *display* units, because it is a display sanity check
 * ("no GPS lock yet"), not a physical bound.
 */
constexpr Precision withPlaceholderBelow(Precision p, float floorValue)
{
    p.usePlaceholder   = true;
    p.placeholderBelow = floorValue;
    return p;
}

/** @brief Copy of @p p that never reports a value as !valid. */
constexpr Precision withoutPlaceholder(Precision p)
{
    p.usePlaceholder   = false;
    p.placeholderBelow = 0.0f;
    return p;
}

namespace Policy
{

/**
 * @brief Four significant characters: 9.99 / 99.9 / 999.
 *
 * Used for distance, which routinely crosses both 10 and 100 during an
 * activity while its text area does not grow.
 */
inline constexpr Precision kFourChar{
    /* usePlaceholder   */ false,
    /* placeholderBelow */ 0.0f,
    /* tiers            */ {{10.0f, 2}, {100.0f, 1}, {}},
    /* tierCount        */ 2,
    /* fallbackDecimals */ 0,
};

/** @brief Whole units only -- elevation and ascent are never shown fractionally. */
inline constexpr Precision kWhole{
    /* usePlaceholder   */ false,
    /* placeholderBelow */ 0.0f,
    /* tiers            */ {},
    /* tierCount        */ 0,
    /* fallbackDecimals */ 0,
};

inline constexpr Precision kDistance  = kFourChar;
inline constexpr Precision kElevation = kWhole;

/**
 * @brief Default pace floor, in seconds per displayed unit.
 *
 * Below this the source is noise rather than a pace, so it is drawn as a
 * placeholder. Applied after conversion, so it means "30 s/km" on a metric
 * watch and "30 s/mi" on an imperial one -- a display guard, not a speed limit.
 */
inline constexpr float kMinPaceSeconds = 30.0f;

} // namespace Policy

// =============================================================================
// Formatter
// =============================================================================

/**
 * @brief Converts SI measurements into drawable Readings.
 *
 * Hold one per screen, set it once from the user's preference, and pass the
 * Readings it produces to widgets. Widgets then need no unit state of their
 * own, which is what makes it impossible for a value and its label to
 * disagree.
 *
 * Trivially copyable and one byte wide; pass it by value freely.
 */
class Formatter
{
public:

    constexpr Formatter() = default;

    constexpr explicit Formatter(System system) : mSystem(system) {}

    constexpr void setSystem(System system) { mSystem = system; }

    /** @brief Bridge from ISettings::isUnitsImperial(), the one place a bool belongs. */
    constexpr void setImperial(bool imperial)
    {
        mSystem = imperial ? System::Imperial : System::Metric;
    }

    constexpr System system() const { return mSystem; }

    constexpr bool isImperial() const { return mSystem == System::Imperial; }

    /**
     * @brief Distance travelled.
     * @param metres  Distance in metres. Always metres -- never km, never miles.
     * @param p       Precision policy; defaults to the shared distance policy.
     */
    constexpr Reading distance(float metres, const Precision& p = Policy::kDistance) const
    {
        const float km    = metres / 1000.0f;
        const float value = isImperial() ? SDK::Utils::kmToMiles(km) : km;
        return make(value, isImperial() ? Label::Mile : Label::Kilometre, p);
    }

    /**
     * @brief Elevation, ascent or descent.
     * @param metres  Height in metres.
     */
    constexpr Reading elevation(float metres, const Precision& p = Policy::kElevation) const
    {
        const float value = isImperial() ? SDK::Utils::metersToFeet(metres) : metres;
        return make(value, isImperial() ? Label::Foot : Label::Metre, p);
    }

    /**
     * @brief Pace, as a duration per kilometre or per mile.
     *
     * @param secondsPerMetre  Pace in s/m. Always s/m -- never s/km, never s/mi.
     * @param floorSeconds     Below this many seconds per displayed unit the
     *                         reading is reported as !valid.
     */
    constexpr PaceReading pace(float secondsPerMetre,
                               float floorSeconds = Policy::kMinPaceSeconds) const
    {
        PaceReading out;
        out.label = isImperial() ? Label::MinPerMile : Label::MinPerKm;

        if (!(secondsPerMetre > 0.0f)) {
            return out;  // zero, negative or NaN: no pace yet
        }

        const float secPerKm = secondsPerMetre * 1000.0f;
        const float seconds  = isImperial() ? secPerKm / SDK::Utils::kmToMiles(1.0f)
                                            : secPerKm;

        if (seconds < floorSeconds) {
            return out;
        }

        // Round to whole seconds before decomposing, so a 59.6 s remainder
        // carries into the next minute instead of being truncated away.
        out.hms   = SDK::Utils::toHMS(static_cast<std::time_t>(seconds + 0.5f));
        out.valid = true;
        return out;
    }

private:

    static constexpr Reading make(float value, Label label, const Precision& p)
    {
        Reading out;
        out.label = label;

        if (!p.isPresentable(value)) {
            return out;
        }

        out.value    = value;
        out.decimals = p.decimalsFor(value);
        out.valid    = true;
        return out;
    }

    System mSystem = System::Metric;
};

} // namespace SDK::Units

#endif // SDK_UNITS_UNITS_HPP
