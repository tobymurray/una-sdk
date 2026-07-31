#include <gui/containers/SummaryFaceOverview.hpp>
#include <gui/UnitLabels.hpp>
#include <texts/TextKeysAndLanguages.hpp>

#include "SDK/GUI/UnitText.hpp"

SummaryFaceOverview::SummaryFaceOverview()
{
}

void SummaryFaceOverview::initialize()
{
    SummaryFaceOverviewBase::initialize();
    title.set(T_TEXT_SUMMARY_UC);
}

void SummaryFaceOverview::setDistance(const SDK::Units::Reading& distance)
{
    SDK::Gui::setReading(distance,
                         {&distanceValue, distanceValueBuffer, DISTANCEVALUE_SIZE},
                         {&distanceUnits, distanceUnitsBuffer, DISTANCEUNITS_SIZE},
                         App::unitTextId);
}

void SummaryFaceOverview::setAvgPace(const SDK::Units::PaceReading& pace)
{
    SDK::Gui::setPaceHoursMinutes(pace, {&avgPaceValue, avgPaceValueBuffer, AVGPACEVALUE_SIZE});
}

void SummaryFaceOverview::setTimer(std::time_t sec)
{
    auto hms = SDK::Utils::toHMS(sec);
    Unicode::snprintf(timerValueBuffer, TIMERVALUE_SIZE,
        "%u:%02u:%02u", hms.h, hms.m, hms.s);
    timerValue.invalidate();
}
