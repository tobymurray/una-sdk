#include <gui/containers/SummaryFaceLaps.hpp>
#include <gui/UnitLabels.hpp>
#include <texts/TextKeysAndLanguages.hpp>

#include "SDK/GUI/UnitText.hpp"

static constexpr uint32_t skListAnimationSteps = App::Config::kMenuAnimationSteps / 2;

SummaryFaceLaps::SummaryFaceLaps()
{
}

void SummaryFaceLaps::initialize()
{
    SummaryFaceLapsBase::initialize();
    title.set(T_TEXT_LAPS_UC);
}

void SummaryFaceLaps::setLaps(const std::vector<LapSummary>& laps, SDK::Units::Formatter units)
{
    mLaps  = &laps;
    mUnits = units;

    Unicode::snprintf(lapsTextBuffer, LAPSTEXT_SIZE, "%u %s",
        static_cast<uint32_t>(laps.size()),
        touchgfx::TypedText(T_TEXT_LAPS_UC).getText());
    lapsText.invalidate();

    time_t total = 0;
    for (const auto& lap : laps) {
        total += lap.duration;
    }
    auto hms = SDK::Utils::toHMS(total);
    Unicode::snprintf(timerTextBuffer, TIMERTEXT_SIZE, "%s: %u:%02u:%02u",
        touchgfx::TypedText(T_TEXT_TOTAL).getText(), hms.h, hms.m, hms.s);
    timerText.invalidate();

    mCurrentPage = 0;
    mNumPages = (laps.empty())
        ? 0u
        : static_cast<uint8_t>((laps.size() + kPageSize - 1) / kPageSize);

    // Pad to (numPages-1)*kPageSize + kVisible so every page transition
    // selects an item outside the current window, forcing a real scroll.
    const int16_t paddedSize = (mNumPages > 0)
        ? static_cast<int16_t>((mNumPages - 1) * kPageSize + kVisible)
        : 0;
    scrollList.setNumberOfItems(paddedSize);
    scrollList.animateToItem(0, 0);
}

bool SummaryFaceLaps::canScrollDown() const
{
    return mCurrentPage + 1 < mNumPages;
}

bool SummaryFaceLaps::canScrollUp() const
{
    return mCurrentPage > 0;
}

void SummaryFaceLaps::scrollDown()
{
    if (canScrollDown()) {
        ++mCurrentPage;
        // Select last item of new window -- always outside previous window.
        scrollList.animateToItem(
            static_cast<int16_t>(mCurrentPage * kPageSize + kVisible - 1), skListAnimationSteps);
    }
}

void SummaryFaceLaps::scrollUp()
{
    if (canScrollUp()) {
        --mCurrentPage;
        // Select first item of new window -- always outside previous window.
        scrollList.animateToItem(
            static_cast<int16_t>(mCurrentPage * kPageSize), skListAnimationSteps);
    }
}

void SummaryFaceLaps::scrollListUpdateItem(LapListItem& item, int16_t itemIndex)
{
    static const touchgfx::Unicode::UnicodeChar kEmpty[1] = {0};

    if (mLaps == nullptr || itemIndex < 0 || itemIndex >= static_cast<int16_t>(mLaps->size())) {
        item.setIndex(kEmpty);
        item.setDistance(kEmpty);
        item.setUnits(kEmpty);
        item.setTime(kEmpty);
        item.setPace(kEmpty);
        return;
    }

    const LapSummary& lap = (*mLaps)[itemIndex];

    // Rows are converted on demand rather than up front, so the formatter is
    // consulted here; the row still gets exactly the precision and rounding the
    // track and summary faces use.
    const SDK::Units::Reading     distance = mUnits.distance(lap.distance, App::Display::kDistance);
    const SDK::Units::PaceReading pace     = mUnits.pace(lap.paceAvg);

    touchgfx::Unicode::UnicodeChar buf[32];

    Unicode::snprintf(buf, 32, "%u.", static_cast<uint32_t>(itemIndex + 1));
    item.setIndex(buf);

    SDK::Gui::formatValue(distance, buf, 32);
    item.setDistance(buf);

    item.setUnits(touchgfx::TypedText(App::unitTextId(distance.label)).getText());

    auto dur = SDK::Utils::toHMS(lap.duration);
    if (dur.h > 0) {
        Unicode::snprintf(buf, 32, "%u:%02u", dur.h, dur.m);
    } else {
        Unicode::snprintf(buf, 32, "%u:%02u", dur.m, dur.s);
    }
    item.setTime(buf);

    SDK::Gui::formatPaceHoursMinutes(pace, buf, 32);
    item.setPace(buf);
}
