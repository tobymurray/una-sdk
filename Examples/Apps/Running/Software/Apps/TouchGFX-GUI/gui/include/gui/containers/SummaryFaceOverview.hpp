#ifndef SUMMARYFACEOVERVIEW_HPP
#define SUMMARYFACEOVERVIEW_HPP

#include <gui_generated/containers/SummaryFaceOverviewBase.hpp>
#include <ctime>

#include "SDK/Units/Units.hpp"

/**
 * @brief Summary face showing overall session metrics: distance, average pace and elapsed time.
 *
 * One of four swipeable faces on the TrackSummary screen.
 * Measurements arrive as Readings, already converted and labelled by the view.
 */
class SummaryFaceOverview : public SummaryFaceOverviewBase
{
public:
    SummaryFaceOverview();
    virtual ~SummaryFaceOverview() {}

    virtual void initialize();

    /** @brief Display total distance and its unit, from SDK::Units::Formatter::distance(). */
    void setDistance(const SDK::Units::Reading& distance);

    /** @brief Display average pace, from SDK::Units::Formatter::pace(). */
    void setAvgPace(const SDK::Units::PaceReading& pace);

    /** @brief Display total elapsed active time as "H:MM:SS". */
    void setTimer(std::time_t sec);

protected:
};

#endif // SUMMARYFACEOVERVIEW_HPP
