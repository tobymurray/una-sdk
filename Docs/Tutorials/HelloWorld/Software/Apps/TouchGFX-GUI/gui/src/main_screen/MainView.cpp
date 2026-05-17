#include <gui/main_screen/MainView.hpp>

namespace
{
// ABGR2222 colours. One byte per pixel, bits MSB→LSB are AABBGGRR. The
// alpha channel must be 3 (opaque) per the format spec.
constexpr uint8_t kABGR_Sky    = 0xFE; // pale sky-blue
constexpr uint8_t kABGR_Yellow = 0xCF; // saturated yellow
constexpr uint8_t kABGR_Black  = 0xC0; // pure black

constexpr uint16_t kReproDim = 256;
uint8_t  sReproPattern[kReproDim * kReproDim];
uint16_t sReproPool[2048]; // 4 KB cache pool — only one bitmap registered

// Render a tiny smiley face into @p buf. Centred at bitmap (160, 128) so
// that, when the widget anchors the bitmap at screen x = -50, the face
// lands near widget x = 110 — close to the 240-px display's horizontal
// centre. The pale-sky background fills the rest of the bitmap so the
// visible region of the widget is unambiguously the bitmap's content.
void drawSmiley(uint8_t* buf, uint16_t dim)
{
    constexpr int faceX = 160;
    constexpr int faceY = 128;

    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            uint8_t c = kABGR_Sky;

            const int fdx = x - faceX;
            const int fdy = y - faceY;
            const int faceR2 = fdx * fdx + fdy * fdy;

            // Face: yellow disc with a black outline ring.
            if (faceR2 <= 99 * 99) {
                c = (faceR2 >= 95 * 95) ? kABGR_Black : kABGR_Yellow;
            }

            // Eyes: small black discs above the centre.
            const int leDx = x - (faceX - 28);
            const int leDy = y - (faceY - 25);
            if (leDx * leDx + leDy * leDy <= 10 * 10) c = kABGR_Black;
            const int reDx = x - (faceX + 28);
            const int reDy = y - (faceY - 25);
            if (reDx * reDx + reDy * reDy <= 10 * 10) c = kABGR_Black;

            // Mouth: the lower half of a thick ring — a smile arc.
            const int mdx = x - faceX;
            const int mdy = y - (faceY + 20);
            const int mr2 = mdx * mdx + mdy * mdy;
            if (mr2 <= 30 * 30 && mr2 >= 22 * 22 && y >= faceY + 20) {
                c = kABGR_Black;
            }

            buf[y * dim + x] = c;
        }
    }
}
} // namespace

MainView::MainView()
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    drawSmiley(sReproPattern, kReproDim);
    touchgfx::Bitmap::setCache(sReproPool, sizeof(sReproPool), 1);
    const touchgfx::BitmapId id = touchgfx::Bitmap::dynamicBitmapCreateExternal(
        kReproDim, kReproDim, sReproPattern, touchgfx::Bitmap::ABGR2222);

    reproWidget.setPosition(0, 0, 240, 240);
    reproWidget.setBitmap(id);
    add(reproWidget);

    buttons.setL1(ButtonsSet::NONE);
    buttons.setL2(ButtonsSet::NONE);
    buttons.setR1(ButtonsSet::NONE);
    buttons.setR2(ButtonsSet::WHITE);
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}
