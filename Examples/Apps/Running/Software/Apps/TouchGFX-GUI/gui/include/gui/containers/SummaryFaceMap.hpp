#ifndef SUMMARYFACEMAP_HPP
#define SUMMARYFACEMAP_HPP

#include <gui_generated/containers/SummaryFaceMapBase.hpp>
#include <SDK/TrackMap/TrackMapScreen.hpp>
#include <SDK/Units/Units.hpp>

/**
 * @brief Summary face showing the GPS track map and total distance.
 *
 * One of four swipeable faces on the TrackSummary screen.
 * Measurements arrive as Readings, already converted and labelled by the view.
 */
class SummaryFaceMap : public SummaryFaceMapBase
{
public:
    SummaryFaceMap();
    virtual ~SummaryFaceMap() {}

    virtual void initialize();

    /** @brief Display total distance and its unit, from SDK::Units::Formatter::distance(). */
    void setDistance(const SDK::Units::Reading& distance);

    /**
     * @brief Render the recorded GPS route on the embedded Map container.
     * @param map Pre-scaled map screen data produced by SDK::TrackMapBuilder::build().
     *            Passing empty map data is a no-op.
     */
    void setMap(const SDK::TrackMapScreen& map);

protected:
};

#endif // SUMMARYFACEMAP_HPP
