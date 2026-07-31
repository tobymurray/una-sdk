#ifndef TRACKFACETOTAL_HPP
#define TRACKFACETOTAL_HPP

#include <gui_generated/containers/TrackFaceTotalBase.hpp>

#include "SDK/Units/Units.hpp"

/**
 * @brief Track face showing overall session metrics: pace, total distance and elapsed time.
 *
 * The face has no unit preference of its own. It draws the Readings the view
 * hands it, each of which already carries its converted value, its precision
 * and the unit label that belongs to that value.
 */
class TrackFaceTotal : public TrackFaceTotalBase
{
public:
    TrackFaceTotal();
    virtual ~TrackFaceTotal() {}

    virtual void initialize();

    /** @brief Display current pace, from SDK::Units::Formatter::pace(). */
    void setPace(const SDK::Units::PaceReading& pace);

    /** @brief Display total distance and its unit, from SDK::Units::Formatter::distance(). */
    void setDistance(const SDK::Units::Reading& distance);

    /** @brief Display total elapsed time as "H:MM:SS". */
    void setTimer(std::time_t sec);

protected:
};

#endif // TRACKFACETOTAL_HPP
