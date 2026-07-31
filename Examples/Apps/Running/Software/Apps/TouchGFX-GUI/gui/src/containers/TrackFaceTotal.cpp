#include <gui/containers/TrackFaceTotal.hpp>
#include <gui/UnitLabels.hpp>

#include "SDK/GUI/UnitText.hpp"

TrackFaceTotal::TrackFaceTotal()
{
}

void TrackFaceTotal::initialize()
{
    TrackFaceTotalBase::initialize();
}

void TrackFaceTotal::setPace(const SDK::Units::PaceReading& pace)
{
    SDK::Gui::setPaceHoursMinutes(pace, {&paceValue, paceValueBuffer, PACEVALUE_SIZE});
}

void TrackFaceTotal::setDistance(const SDK::Units::Reading& distance)
{
    SDK::Gui::setReading(distance,
                         {&distanceValue, distanceValueBuffer, DISTANCEVALUE_SIZE},
                         {&distanceUnits, distanceUnitsBuffer, DISTANCEUNITS_SIZE},
                         App::unitTextId);
}

void TrackFaceTotal::setTimer(std::time_t sec)
{
    auto hms = SDK::Utils::toHMS(sec);
    Unicode::snprintf(timerValueBuffer, TIMERVALUE_SIZE,
        "%u:%02u:%02u", hms.h, hms.m, hms.s);
    timerValue.invalidate();
}
