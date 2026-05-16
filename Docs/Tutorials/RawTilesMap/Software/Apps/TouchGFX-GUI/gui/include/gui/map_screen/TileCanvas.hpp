#ifndef TILECANVAS_HPP
#define TILECANVAS_HPP

#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/Bitmap.hpp>
#include <touchgfx/hal/Types.hpp>

/**
 * @brief Custom Widget that mosaics a 3×3 grid of pre-loaded tile bitmaps.
 *
 * Cells are laid out so the seam between the four centre tiles falls at the
 * widget centre; with 256-px tiles on a 240-px screen that exposes the
 * top-left, top-right, bottom-left, and bottom-right quadrants of cells
 * @c (1,1), @c (2,1), @c (1,2), @c (2,2) respectively. The outer five cells
 * are loaded but off-screen until panning (future step).
 *
 * Cells whose @c BitmapId is @c BITMAP_INVALID render as the slate sentinel,
 * which makes a pack's bbox tangible on screen.
 */
class TileCanvas : public touchgfx::Widget
{
public:
    static constexpr int kGrid  = 3;
    static constexpr int kCells = kGrid * kGrid;

    TileCanvas();

    /**
     * @brief Binds the 3×3 grid of pre-loaded dynamic-bitmap IDs and the
     *        pixel dimension of one square tile. Pass @c tileDimPx = 0 to
     *        revert to the solid-colour placeholder.
     */
    void setViewport(const touchgfx::BitmapId* ids, uint16_t tileDimPx);

    virtual void draw(const touchgfx::Rect& area) const override;
    virtual touchgfx::Rect getSolidRect() const override { return getRect(); }

private:
    touchgfx::BitmapId mIds[kCells];
    uint16_t           mTileDimPx = 0;
};

#endif // TILECANVAS_HPP
