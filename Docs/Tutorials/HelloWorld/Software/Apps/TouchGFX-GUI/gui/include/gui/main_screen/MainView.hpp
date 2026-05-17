#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/Bitmap.hpp>
#include <touchgfx/hal/HAL.hpp>

#include "SDK/GUI/DynamicBitmapDraw.hpp"

/**
 * @class NegativeAnchorRepro
 * @brief Visual confirmation that ABGR2222 dynamic bitmaps render correctly
 *        when anchored at a negative screen position.
 *
 * Calls @c SDK::GUI::drawDynamicBitmap with a 256x256 bitmap whose origin
 * lands 50 px to the left of the widget's top-left. The bitmap's content is
 * a yellow smiley face on a pale-sky background (drawn by @c drawSmiley in
 * MainView.cpp). When the helper renders correctly the smiley appears in
 * the widget area; when the underlying @c drawPartialBitmap path is broken
 * the draw is dropped and the standard HelloWorld content shows through.
 *
 * To check whether your platform suffers from the underlying defect, swap
 * the call in @c draw for the direct API call:
 *
 *     touchgfx::Bitmap bmp(mId);
 *     touchgfx::Rect src(0, 0, bmp.getWidth(), bmp.getHeight());
 *     touchgfx::HAL::lcd().drawPartialBitmap(bmp, absArea.x - 50, absArea.y,
 *                                            src, 255, true);
 *
 * Procedure and conclusive outcomes are in
 * @c Docs/Investigations/2026-05-16-touchgfx-drawpartialbitmap-negative-x/.
 */
class NegativeAnchorRepro : public touchgfx::Widget
{
public:
    void setBitmap(touchgfx::BitmapId id) { mId = id; invalidate(); }

    virtual void draw(const touchgfx::Rect& area) const override
    {
        if (mId == touchgfx::BITMAP_INVALID) {
            return;
        }
        touchgfx::Rect absArea = area;
        translateRectToAbsolute(absArea);
        const int16_t widgetAbsX = static_cast<int16_t>(absArea.x - area.x);
        const int16_t widgetAbsY = static_cast<int16_t>(absArea.y - area.y);
        SDK::GUI::drawDynamicBitmap(touchgfx::Bitmap(mId),
                                    static_cast<int16_t>(widgetAbsX - 50),
                                    widgetAbsY,
                                    absArea);
    }

    virtual touchgfx::Rect getSolidRect() const override { return touchgfx::Rect(); }

private:
    touchgfx::BitmapId mId = touchgfx::BITMAP_INVALID;
};

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    NegativeAnchorRepro reproWidget;
};

#endif // MAINVIEW_HPP
