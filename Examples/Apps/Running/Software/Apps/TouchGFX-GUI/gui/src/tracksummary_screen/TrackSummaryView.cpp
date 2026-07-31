#include <gui/tracksummary_screen/TrackSummaryView.hpp>
#include <SDK/GUI/Button.hpp>
#include <SDK/Utils/Utils.hpp>

TrackSummaryView::TrackSummaryView()
{
}

void TrackSummaryView::setupScreen()
{
    TrackSummaryViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::NONE);

    scrollIndicator.setConfig(ScrollIndicator::kSmall);
    scrollIndicator.setCount(3);
    scrollIndicator.setActiveId(FACE_MAP);

    mCurrentFace = FACE_MAP;
    updateFace();
}

void TrackSummaryView::tearDownScreen()
{
    TrackSummaryViewBase::tearDownScreen();
}

void TrackSummaryView::setSummary(const ActivitySummary& s, bool isImperial, bool isPaused)
{
    mUnits.setImperial(isImperial);
    mTrackIsPaused = isPaused;

    // One Reading, shared by both faces: they cannot disagree about the number
    // or about the unit beside it.
    const SDK::Units::Reading distance = mUnits.distance(s.distance, App::Display::kDistance);

    summaryFaceMap.setDistance(distance);
    summaryFaceOverview.setDistance(distance);
    summaryFaceOverview.setAvgPace(mUnits.pace(s.paceAvg));
    summaryFaceOverview.setTimer(s.time);
    summaryFaceHeartRate.setMaxHR(s.hrMax);
    summaryFaceHeartRate.setAvgHR(s.hrAvg);
    summaryFaceMap.setMap(s.map);

    summaryFaceLaps.setLaps(s.laps, mUnits);
    mLapsPageCount = summaryFaceLaps.getPageCount();
    scrollIndicator.setCount(3 + mLapsPageCount);

    if (isPaused) {
        buttons.setR1(Buttons::NONE);
        buttons.setR2(Buttons::AMBER);
    } else {
        buttons.setR1(Buttons::AMBER);
        buttons.setR2(Buttons::NONE);
    }
}

void TrackSummaryView::updateFace()
{
    summaryFaceMap.setVisible(mCurrentFace == FACE_MAP);
    summaryFaceOverview.setVisible(mCurrentFace == FACE_OVERVIEW);
    summaryFaceHeartRate.setVisible(mCurrentFace == FACE_HEARTRATE);
    summaryFaceLaps.setVisible(mCurrentFace == FACE_LAPS);

    summaryFaceMap.invalidate();
    summaryFaceOverview.invalidate();
    summaryFaceHeartRate.invalidate();
    summaryFaceLaps.invalidate();

    updateScrollIndicator();
}

void TrackSummaryView::updateScrollIndicator()
{
    const uint8_t id = (mCurrentFace < FACE_LAPS)
        ? mCurrentFace
        : static_cast<uint8_t>(FACE_LAPS + summaryFaceLaps.getScrollPage());
    scrollIndicator.animateToId(id);
}

void TrackSummaryView::handleKeyEvent(uint8_t key)
{
    if (key == SDK::GUI::Button::L2) {
        if (mCurrentFace == FACE_LAPS) {
            if (summaryFaceLaps.canScrollDown()) {
                summaryFaceLaps.scrollDown();
                updateScrollIndicator();
            }
        } else if (mCurrentFace < FACE_HEARTRATE || mLapsPageCount > 0) {
            mCurrentFace++;
            updateFace();
        }
        return;
    }

    if (key == SDK::GUI::Button::L1) {
        if (mCurrentFace == FACE_LAPS) {
            if (summaryFaceLaps.canScrollUp()) {
                summaryFaceLaps.scrollUp();
                updateScrollIndicator();
            } else {
                mCurrentFace--;
                updateFace();
            }
        } else if (mCurrentFace > FACE_MAP) {
            mCurrentFace--;
            updateFace();
        }
        return;
    }

    if (key == SDK::GUI::Button::R1) {
        if (!mTrackIsPaused) presenter->backToTrack();
        return;
    }

    if (key == SDK::GUI::Button::R2) {
        if (mTrackIsPaused) presenter->backToTrack();
        return;
    }
}
