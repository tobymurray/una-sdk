/**
 ******************************************************************************
 * @file    UnitText.hpp
 * @brief   Draws an SDK::Units::Reading into TouchGFX text areas.
 *
 ******************************************************************************
 *
 * SDK/Units/Units.hpp decides *what* to show -- the converted value, its
 * precision, its unit and whether there is data at all. This header is the
 * other half: it puts that decision on screen, so the eight lines of
 * snprintf-and-invalidate that every activity widget used to carry exist once.
 *
 * TouchGFX generates text ids per app, so the SDK cannot name "km" or "mi".
 * The app supplies a single mapping function -- the only place its
 * TypedTextIds appear -- and passes it in:
 *
 *     // App-owned, one per app.
 *     touchgfx::TypedTextId App::unitTextId(SDK::Units::Label label)
 *     {
 *         switch (label) {
 *             case SDK::Units::Label::Kilometre: return T_TEXT_KM;
 *             case SDK::Units::Label::Mile:      return T_TEXT_MI;
 *             default:                           return T_TEXT_KM;
 *         }
 *     }
 *
 *     // Widget.
 *     void TrackFaceTotal::setDistance(const SDK::Units::Reading& distance)
 *     {
 *         SDK::Gui::setReading(distance,
 *                              {&distanceValue, distanceValueBuffer, DISTANCEVALUE_SIZE},
 *                              {&distanceUnits, distanceUnitsBuffer, DISTANCEUNITS_SIZE},
 *                              App::unitTextId);
 *     }
 *
 * This mirrors SDK/GUI/SensorStatusRow.hpp, which takes the host app's
 * BitmapIds for the same reason.
 *
 * The unit area is optional: pass an empty TextField for screens whose unit is
 * baked into the design.
 *
 ******************************************************************************
 */

#ifndef __SDK_GUI_UNIT_TEXT_HPP
#define __SDK_GUI_UNIT_TEXT_HPP

#include <touchgfx/TypedText.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/widgets/TextArea.hpp>

#include "SDK/Units/Units.hpp"

#include <cstdint>

namespace SDK
{
namespace Gui
{

/**
 * @brief Keep a shared body out of every call site.
 *
 * These functions are called from every measurement widget in an app. Left to
 * inline, the compiler duplicates each body at every site, which measures
 * larger than the hand-written if-ladders they replace -- more than double the
 * flash cost on the first app to adopt this. Docs/units-and-display.md carries
 * the current figures; do not drop the attribute without re-measuring.
 *
 * They stay in the header -- the SDK ships no GUI translation unit and the
 * simulator build compiles none -- so each user emits a weak definition and
 * the linker folds them to one under -ffunction-sections --gc-sections.
 */
#if defined(_MSC_VER)
#define SDK_GUI_NO_INLINE __declspec(noinline)
#else
#define SDK_GUI_NO_INLINE __attribute__((noinline))
#endif

/** @brief App-provided mapping from an abstract unit to the app's text id. */
using UnitTextLookup = touchgfx::TypedTextId (*)(SDK::Units::Label);

/**
 * @brief A generated TouchGFX wildcard text area and the buffer behind it.
 *
 * Default-constructed (`{}`) means "no such field", which is how an optional
 * unit area is expressed.
 */
struct TextField
{
    touchgfx::TextArea*             area   = nullptr;
    touchgfx::Unicode::UnicodeChar* buffer = nullptr;
    uint16_t                        size   = 0;

    bool isBound() const { return area != nullptr && buffer != nullptr && size > 0; }
};

/** @brief Drawn in place of the value when a Reading carries no data. */
inline constexpr const char* kPlaceholder = "---";

namespace detail
{

/**
 * @brief Format string per decimal count.
 *
 * Indexed rather than built, because TouchGFX's snprintfFloat takes a plain
 * format string and the watch has no business assembling one at 30 Hz.
 */
inline const char* decimalFormat(uint8_t decimals)
{
    switch (decimals) {
        case 0:  return "%.0f";
        case 1:  return "%.01f";
        case 2:  return "%.02f";
        default: return "%.03f";
    }
}

/** @brief Whether two strings match within @p maxChars, terminators included. */
SDK_GUI_NO_INLINE inline bool sameText(const touchgfx::Unicode::UnicodeChar* a,
                                       const touchgfx::Unicode::UnicodeChar* b,
                                       uint16_t                              maxChars)
{
    for (uint16_t i = 0; i < maxChars; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == 0) {
            return true;
        }
    }
    return true;
}

/**
 * @brief Write @p text into @p field, repainting only if it actually changed.
 *
 * A unit label changes when the user changes their preference and at no other
 * time, but the value beside it is rewritten on every tick. Invalidating the
 * label along with it enlarges the dirty region for no visible reason.
 */
SDK_GUI_NO_INLINE inline void setUnitText(const TextField&                      field,
                                          const touchgfx::Unicode::UnicodeChar* text)
{
    if (!field.isBound() || text == nullptr) {
        return;
    }

    if (sameText(field.buffer, text, field.size)) {
        return;
    }

    touchgfx::Unicode::strncpy(field.buffer, text, field.size);
    field.area->invalidate();
}

} // namespace detail

// =============================================================================
// Buffer-only formatting
//
// For widgets that take a finished string rather than owning a text area --
// ScrollList rows, for instance. Same precision and placeholder rules as the
// text-area calls below, so a lap row and the track face never disagree.
// =============================================================================

/** @brief Write @p reading (or the placeholder) into @p buffer. */
SDK_GUI_NO_INLINE inline void formatValue(const SDK::Units::Reading&      reading,
                                          touchgfx::Unicode::UnicodeChar* buffer,
                                          uint16_t                        size)
{
    if (reading.valid) {
        touchgfx::Unicode::snprintfFloat(buffer, size,
                                         detail::decimalFormat(reading.decimals),
                                         reading.value);
    } else {
        touchgfx::Unicode::snprintf(buffer, size, "%s", kPlaceholder);
    }
}

/** @brief Write @p pace as "h:mm" past the hour, "m:ss" below it. */
SDK_GUI_NO_INLINE inline void formatPaceHoursMinutes(const SDK::Units::PaceReading&  pace,
                                                     touchgfx::Unicode::UnicodeChar* buffer,
                                                     uint16_t                        size)
{
    if (!pace.valid) {
        touchgfx::Unicode::snprintf(buffer, size, "%s", kPlaceholder);
    } else if (pace.hms.h > 0) {
        touchgfx::Unicode::snprintf(buffer, size, "%u:%02u",
                                    static_cast<unsigned>(pace.hms.h),
                                    static_cast<unsigned>(pace.hms.m));
    } else {
        touchgfx::Unicode::snprintf(buffer, size, "%u:%02u",
                                    static_cast<unsigned>(pace.hms.m),
                                    static_cast<unsigned>(pace.hms.s));
    }
}

/**
 * @brief Draw a Reading into a value area, and its unit into an optional unit area.
 *
 * @param reading  What to draw, from SDK::Units::Formatter.
 * @param value    Where the number (or the placeholder) goes.
 * @param unit     Where the unit label goes; pass `{}` to skip.
 * @param lookup   App mapping from Units::Label to a TypedTextId; may be
 *                 nullptr when @p unit is unbound.
 */
SDK_GUI_NO_INLINE inline void setReading(const SDK::Units::Reading& reading,
                                         const TextField&           value,
                                         const TextField&           unit   = {},
                                         UnitTextLookup             lookup = nullptr)
{
    if (value.isBound()) {
        formatValue(reading, value.buffer, value.size);
        value.area->invalidate();
    }

    // The label is drawn even for an invalid reading: the unit a screen is
    // configured for does not stop being true because the GPS dropped out.
    if (unit.isBound() && lookup != nullptr) {
        detail::setUnitText(unit, touchgfx::TypedText(lookup(reading.label)).getText());
    }
}

/**
 * @brief Draw a PaceReading as "h:mm" past the hour and "m:ss" below it.
 *
 * Keeps a pace field to four characters on screens whose text area cannot grow.
 * PaceReading also exposes totalMinutes() for designs that prefer "66:40".
 */
SDK_GUI_NO_INLINE inline void setPaceHoursMinutes(const SDK::Units::PaceReading& pace,
                                                  const TextField&               value,
                                                  const TextField&               unit   = {},
                                                  UnitTextLookup                 lookup = nullptr)
{
    if (value.isBound()) {
        formatPaceHoursMinutes(pace, value.buffer, value.size);
        value.area->invalidate();
    }

    if (unit.isBound() && lookup != nullptr) {
        detail::setUnitText(unit, touchgfx::TypedText(lookup(pace.label)).getText());
    }
}

} // namespace Gui
} // namespace SDK

#endif // __SDK_GUI_UNIT_TEXT_HPP
