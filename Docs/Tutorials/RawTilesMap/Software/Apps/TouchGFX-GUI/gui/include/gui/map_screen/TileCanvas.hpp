#ifndef TILECANVAS_HPP
#define TILECANVAS_HPP

#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/Bitmap.hpp>
#include <touchgfx/hal/Types.hpp>

class TileCanvas : public touchgfx::Widget
{
public:
    TileCanvas();

    /**
     * @brief Binds the dynamic bitmap that this canvas should draw. Pass
     *        @c touchgfx::BITMAP_INVALID to fall back to the solid-colour
     *        placeholder.
     */
    void setBitmap(touchgfx::BitmapId id);

    virtual void draw(const touchgfx::Rect& area) const override;
    virtual touchgfx::Rect getSolidRect() const override { return getRect(); }

private:
    touchgfx::BitmapId mBitmapId = touchgfx::BITMAP_INVALID;
};

#endif // TILECANVAS_HPP
