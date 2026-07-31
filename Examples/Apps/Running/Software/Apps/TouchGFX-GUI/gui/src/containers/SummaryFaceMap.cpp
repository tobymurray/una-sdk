#include <gui/containers/SummaryFaceMap.hpp>
#include <gui/UnitLabels.hpp>
#include <texts/TextKeysAndLanguages.hpp>

#include "SDK/GUI/UnitText.hpp"

SummaryFaceMap::SummaryFaceMap()
{
}

void SummaryFaceMap::initialize()
{
    SummaryFaceMapBase::initialize();
    title.set(T_TEXT_MAP_UC);
}

void SummaryFaceMap::setDistance(const SDK::Units::Reading& distance)
{
    SDK::Gui::setReading(distance,
                         {&distanceValue, distanceValueBuffer, DISTANCEVALUE_SIZE},
                         {&distanceUnits, distanceUnitsBuffer, DISTANCEUNITS_SIZE},
                         App::unitTextId);
}

void SummaryFaceMap::setMap(const SDK::TrackMapScreen& mapData)
{
    map.setMap(mapData);
}
