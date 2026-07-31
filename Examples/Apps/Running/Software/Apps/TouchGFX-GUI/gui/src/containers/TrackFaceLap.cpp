#include <gui/containers/TrackFaceLap.hpp>

#include "SDK/GUI/UnitText.hpp"

TrackFaceLap::TrackFaceLap()
{
}

void TrackFaceLap::initialize()
{
    TrackFaceLapBase::initialize();
}

void TrackFaceLap::setHR(float hr, const uint8_t* thresholds, uint8_t thresholdCount)
{
    if (hr < App::Display::kMinHR) {
        Unicode::snprintf(hrValueBuffer, HRVALUE_SIZE, "---");
        hrValue.invalidate();
        hrZone.setHR(0, thresholds, thresholdCount);
        return;
    }

    Unicode::snprintfFloat(hrValueBuffer, HRVALUE_SIZE, "%.0f", hr);
    hrValue.invalidate();

    hrZone.setHR(static_cast<uint8_t>(hr), thresholds, thresholdCount);
}

void TrackFaceLap::setPace(const SDK::Units::PaceReading& pace)
{
    SDK::Gui::setPaceHoursMinutes(pace, {&lapPaceValue, lapPaceValueBuffer, LAPPACEVALUE_SIZE});
}

void TrackFaceLap::setDistance(const SDK::Units::Reading& distance)
{
    SDK::Gui::setReading(distance, {&lapDistValue, lapDistValueBuffer, LAPDISTVALUE_SIZE});
}

void TrackFaceLap::setTimer(std::time_t sec)
{
    auto hms = SDK::Utils::toHMS(sec);
    Unicode::snprintf(lapTimerValueBuffer, LAPTIMERVALUE_SIZE,
        "%u:%02u:%02u", hms.h, hms.m, hms.s);
    lapTimerValue.invalidate();
}
