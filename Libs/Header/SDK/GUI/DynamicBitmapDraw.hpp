/**
 ******************************************************************************
 * @file    DynamicBitmapDraw.hpp
 * @date    16-05-2026
 * @brief   Workaround helper for a TouchGFX rendering defect that drops
 *          ABGR2222 dynamic bitmaps when drawPartialBitmap is called with a
 *          negative target position or a sub-rectangle source.
 *
 * == Defect ==
 *
 * The Linux simulator prebuilt at @c ThirdParty/touchgfx/lib/linux/libtouchgfx.a
 * mis-renders ABGR2222 dynamic bitmaps registered via
 * @c Bitmap::dynamicBitmapCreateExternal when @c LCD::drawPartialBitmap is
 * invoked with either:
 *
 *   1. a negative target @c x argument, or
 *   2. a @c rect argument that is a strict sub-rectangle of the bitmap
 *      (i.e. @c rect.width != bitmap.getWidth() or @c rect.height !=
 *      bitmap.getHeight()).
 *
 * Calls meeting either condition are dropped silently — the pixels never
 * reach the framebuffer and the affected widget area shows whatever was
 * underneath (slate background or stale content). Calls with non-negative
 * @c x and a full-bitmap @c rect render, but @c x is ignored and the bitmap
 * is stretched across the widget's full width.
 *
 * Both call shapes are valid uses of the documented API. The TouchGFX docs
 * (https://support.touchgfx.com/docs/api/classes/classtouchgfx_1_1_l_c_d#function-drawpartialbitmap)
 * describe @c x as "the absolute x coordinate to place (0, 0) of the bitmap
 * on the screen" (negative values are therefore meaningful — they place the
 * bitmap origin off-screen and the visible portion is rendered) and @c rect
 * as "a rectangle describing what region of the bitmap is to be drawn"
 * (any sub-rectangle is permitted). The defect is divergence from the
 * documented contract, not a misuse on the caller's part.
 *
 * The defect is localized to the @c drawPartialBitmap wrapper. The
 * underlying @c LCD::blitCopy primitive, invoked with the equivalent
 * bitmap-position-in-screen-coords @c source rect and a correctly
 * source-relative @c blitRect (the intersection of @c source with the dirty
 * rect, translated to bitmap-local coords), renders the same pixel data
 * correctly. The dynamic-bitmap registry, @c dynamicBitmapGetAddress, and
 * @c Bitmap::getData all return the registered pointer correctly at all
 * stages.
 *
 * == Workaround ==
 *
 * @c SDK::GUI::drawDynamicBitmap bypasses @c drawPartialBitmap and calls
 * @c LCD::blitCopy directly, computing the intersection of the bitmap's
 * screen-absolute rect with the dirty rect inline and translating the
 * result to a source-relative @c blitRect. This matches the call shape used
 * by TouchGFX's own @c PixelDataWidget for partial repaints.
 *
 * == Platform gating ==
 *
 * The workaround is only active when @c SIMULATOR @b and @c __linux__ are
 * both defined. Other platforms — Windows simulator, embedded ARM, etc. —
 * use the unmodified @c drawPartialBitmap path because the defect has not
 * been verified there. The validation procedure to determine whether your
 * platform needs the workaround is in
 * @c Docs/Investigations/2026-05-16-touchgfx-drawpartialbitmap-negative-x/README.md;
 * extend the @c \#if guard below if your platform's outcome is conclusive.
 *
 * == Format scope ==
 *
 * The workaround dispatches @c blitCopy with @c Bitmap::ABGR2222 hard-coded
 * because that is the only format for which the defect was verified.
 * Sibling 8bpp formats (ARGB2222, BGRA2222, RGBA2222) appear to share the
 * implementation in @c libtouchgfx.a (identical code size in @c nm output)
 * but have not been empirically tested. Generalize this helper to accept a
 * @c BitmapFormat argument when adding coverage for those formats.
 ******************************************************************************
 */

#ifndef __SDK_GUI_DYNAMIC_BITMAP_DRAW_HPP
#define __SDK_GUI_DYNAMIC_BITMAP_DRAW_HPP

#include <algorithm>
#include <cstdint>

#include "touchgfx/Bitmap.hpp"
#include "touchgfx/hal/HAL.hpp"
#include "touchgfx/hal/Types.hpp"
#include "touchgfx/lcd/LCD.hpp"

namespace SDK
{
namespace GUI
{

/**
 * @brief Draw an ABGR2222 dynamic bitmap at an absolute screen position,
 *        clipped against a dirty rect. See file-level comment for rationale.
 *
 * The supplied @p screenX, @p screenY are where bitmap pixel (0, 0) should
 * land on the framebuffer. They may be negative; the helper clips to the
 * dirty rect before issuing the blit.
 *
 * @param bitmap         Bitmap whose id refers to a dynamic external bitmap
 *                       registered via @c dynamicBitmapCreateExternal.
 * @param screenX        Absolute-screen x for bitmap (0, 0). May be negative.
 * @param screenY        Absolute-screen y for bitmap (0, 0). May be negative.
 * @param dirtyRectAbs   Dirty rect in absolute-screen coordinates (e.g. the
 *                       @c area argument to @c Widget::draw after
 *                       @c translateRectToAbsolute).
 */
inline void drawDynamicBitmap(const touchgfx::Bitmap& bitmap,
                              int16_t screenX, int16_t screenY,
                              const touchgfx::Rect& dirtyRectAbs)
{
#if defined(SIMULATOR) && defined(__linux__)
    const uint8_t* pixels =
        touchgfx::Bitmap::dynamicBitmapGetAddress(bitmap.getId());
    if (pixels == nullptr) {
        return;
    }

    const touchgfx::Rect source(
        screenX, screenY,
        static_cast<int16_t>(bitmap.getWidth()),
        static_cast<int16_t>(bitmap.getHeight()));

    const int16_t ix0 = std::max<int16_t>(source.x, dirtyRectAbs.x);
    const int16_t iy0 = std::max<int16_t>(source.y, dirtyRectAbs.y);
    const int16_t ix1 = std::min<int16_t>(
        static_cast<int16_t>(source.x + source.width),
        static_cast<int16_t>(dirtyRectAbs.x + dirtyRectAbs.width));
    const int16_t iy1 = std::min<int16_t>(
        static_cast<int16_t>(source.y + source.height),
        static_cast<int16_t>(dirtyRectAbs.y + dirtyRectAbs.height));
    if (ix1 <= ix0 || iy1 <= iy0) {
        return;
    }

    const touchgfx::Rect blitRect(
        static_cast<int16_t>(ix0 - source.x),
        static_cast<int16_t>(iy0 - source.y),
        static_cast<int16_t>(ix1 - ix0),
        static_cast<int16_t>(iy1 - iy0));

    touchgfx::HAL::lcd().blitCopy(
        pixels, touchgfx::Bitmap::ABGR2222, source, blitRect, 255, false);
#else
    const touchgfx::Rect src(0, 0,
        static_cast<int16_t>(bitmap.getWidth()),
        static_cast<int16_t>(bitmap.getHeight()));
    touchgfx::HAL::lcd().drawPartialBitmap(
        bitmap, screenX, screenY, src, 255, true);
#endif
}

} // namespace GUI
} // namespace SDK

#endif // __SDK_GUI_DYNAMIC_BITMAP_DRAW_HPP
