#ifndef TRACKFACELAP_HPP
#define TRACKFACELAP_HPP

#include <gui_generated/containers/TrackFaceLapBase.hpp>

#include "SDK/Units/Units.hpp"

/**
 * @brief Track face showing current lap metrics and heart rate zone.
 *
 * Displays lap pace, lap distance, lap elapsed time and an HR zone bar.
 * Measurements arrive as Readings, already converted and labelled by the view.
 */
class TrackFaceLap : public TrackFaceLapBase
{
public:
    TrackFaceLap();
    virtual ~TrackFaceLap() {}

    virtual void initialize();

    /**
     * @brief Display heart rate value and update the HR zone bar.
     * @param hr             Current heart rate in bpm.
     *                       Pass a value < App::Display::kMinHR to show "---" (no sensor data).
     * @param thresholds     Pointer to the HR zone threshold array (bpm boundaries).
     * @param thresholdCount Number of thresholds in the array.
     */
    void setHR(float hr, const uint8_t* thresholds, uint8_t thresholdCount);

    /** @brief Display current lap pace, from SDK::Units::Formatter::pace(). */
    void setPace(const SDK::Units::PaceReading& pace);

    /**
     * @brief Display current lap distance, from SDK::Units::Formatter::distance().
     * @note  This face has no unit text area; the km/mi label is part of the design.
     */
    void setDistance(const SDK::Units::Reading& distance);

    /** @brief Display current lap elapsed time as "M:SS" (or "H:MM" when >= 1 h). */
    void setTimer(std::time_t sec);

protected:
};

#endif // TRACKFACELAP_HPP
