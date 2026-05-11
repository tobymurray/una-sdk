/**
 ******************************************************************************
 * @file    GpsStepCounterSimulator.hpp
 * @date    28-March-2026
 * @author  Vlad Andriyash
 * @brief   Simulator GPS and Step Counter
 *
 ******************************************************************************
 */
#include "SDK/Simulator/Components/Simulator/GpsStepCounterSimulator.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp"
#include <cmath>
#include <chrono>
#include <random>
#include <SDK/Simulator/Kernel/Mock/System.hpp>

#define DATA_SAMPLE_COUNT  5

namespace Simulator {

    GpsStepCounterSimulator::GpsStepCounterSimulator()
        : mGen(std::random_device{}()),
        mTimerGpsFix(),
        mTimer(),
        mGpsNoise(0.0f, 1.5f),
        mAltNoise(0.0f, 0.5f),
        mSpeedNoise(0.0f, 0.05f),
        mPrecisionNoise(0.0f, 1.0f),
        mGpsLoss(0.0f, 1.0f),
        mCurrentSpeed(mBaseSpeed),
        mTotalDistanceMeters(0.0f),
        mDriftX(0), mDriftY(0), mBaseAlt(250.0f), mAltDrift(0),
        mDataSample(DATA_SAMPLE_COUNT),
        mRunning(false),
        mPeriodSendGpsData(990),
        mTrackPosition(0.0),
        mLapLength(0.0f),
        mLoc{}
    {
        mStart = std::chrono::steady_clock::now();
        mLastTime = mStart;
        buildHeartTable();
        mTimerGpsFix.start(5000);
        mTimer.start(mPeriodSendGpsData);
        mThread = std::thread(&GpsStepCounterSimulator::task, this);
    }

    GpsStepCounterSimulator::~GpsStepCounterSimulator()
    {
        mTimerGpsFix.stop();
        mTimer.stop();
        if (mThread.joinable())
            mThread.join();
    }

    bool GpsStepCounterSimulator::enable()
    {
        mRunning = true;
        return true;
    }

    bool GpsStepCounterSimulator::disable()
    {
        mRunning = false;

        return true;
    }

    void GpsStepCounterSimulator::setPeriod(uint32_t seconds)
    {
        mPeriodSendGpsData = seconds;
        mTimer.start(mPeriodSendGpsData);
    }

    void GpsStepCounterSimulator::task()
    {
        static bool flagGpsFix = false;

        while (1)
        {
            if (!SDK::Simulator::Mock::SystemGUI::isAppRunning()) {
                return;
            }

            if (mTimerGpsFix.check()) {
                mTimerGpsFix.stop();
                flagGpsFix = true;
            }

            if (flagGpsFix == false) {
                continue;
            }

            if (!mTimer.check()) {
                continue;
            }

            updateLocation();
            IGps::LocationInfo loc = getLocation();
        }
    }
    
    IGps::LocationInfo GpsStepCounterSimulator::getLocation()
    {
        return mLoc;
    }

    void GpsStepCounterSimulator::updateLocation()
    {
        // Calculate time delta since last update
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - mLastTime).count();
        mLastTime = now;

        // Determine current segment of the track
        float distanceAlongLap = mTrackPosition;

        // Determine speed factor based on track segment (straight / curve)
        float speedFactor = getSpeedFactor(distanceAlongLap);

        // Update current speed smoothly with random jitter
        updateSpeed(speedFactor);

        // Compute step distance based on speed and elapsed time
        float step = mCurrentSpeed * dt;

        // Update total distance traveled
        mTotalDistanceMeters = step;

        // Move position along track
        mTrackPosition += step;

        updateStepCounter(step);

        // Wrap around lap if necessary
        if (mTrackPosition > mLapLength)
            mTrackPosition = std::fmod(mTrackPosition, mLapLength);

        // Look up (x, y) in the pre-built heart arc-length table
        float pos = std::fmod(mTrackPosition, mLapLength);

        int lo = 0, hi = kHeartSamples - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (mHeartTable[mid].arcLen <= pos) lo = mid;
            else hi = mid - 1;
        }
        float a0    = mHeartTable[lo].arcLen;
        float a1    = mHeartTable[lo + 1].arcLen;
        float alpha = (a1 > a0) ? (pos - a0) / (a1 - a0) : 0.0f;
        float x = mHeartTable[lo].x + alpha * (mHeartTable[lo + 1].x - mHeartTable[lo].x);
        float y = mHeartTable[lo].y + alpha * (mHeartTable[lo + 1].y - mHeartTable[lo].y);

        // Add GPS drift and noise
        mDriftX += mGpsNoise(mGen) * 0.05f;
        mDriftY += mGpsNoise(mGen) * 0.05f;

        x += mGpsNoise(mGen) + mDriftX;
        y += mGpsNoise(mGen) + mDriftY;

        // Convert meters to GPS degrees
        const float metersPerDegLat = 111320.0f;
        const float metersPerDegLon = static_cast<float>(metersPerDegLat * std::cos(mCenterLat * M_PI / 180.0f));

        LocationInfo loc;

        // Simulate occasional GPS signal loss
        if (mGpsLoss(mGen) < 0.02f)
        {
            loc.valid = false;
            mLoc = loc;
        }

        loc.valid = true;

        loc.lat = mCenterLat + y / metersPerDegLat;
        loc.lon = mCenterLon + x / metersPerDegLon;

        // Simulate altitude with small random drift
        float altitudeVariation = static_cast<float>(0.5f * std::sin(mTrackPosition / mLapLength * 2.0f * M_PI));
        mAltDrift += mAltNoise(mGen) * 0.02f;
        loc.alt = mBaseAlt + altitudeVariation + mAltDrift + mAltNoise(mGen);

        // Simulate measurement precision (accuracy)
        float p = 3.0f + std::fabs(mGpsNoise(mGen));
        if (mPrecisionNoise(mGen) < 0.05f)
            p += 10.0f;
        loc.precision = p;

        mLoc = loc;
  
    }

    void GpsStepCounterSimulator::setParamSimulation(float speedMin, float speedMidle, float speedMax, uint32_t seachSatteliteMs)
    {
        mMinSpeed = speedMin / 3.6f;
        mBaseSpeed  = speedMidle / 3.6f;
        mMaxSpeed = speedMax / 3.6f;

        mTimerGpsFix.stop();
        mTimerGpsFix.start(seachSatteliteMs*1000);
    }

    bool GpsStepCounterSimulator::init()
    {
        return true;
    }

    bool GpsStepCounterSimulator::deinit()
    {
        return true;
    }

    time_t GpsStepCounterSimulator::getTime()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        return t;
    }

    bool GpsStepCounterSimulator::hasFix()
    {
        return mLoc.valid;
    }

    bool GpsStepCounterSimulator::isEnabled()
    {
        return mRunning;
    }

    float GpsStepCounterSimulator::getSpeed()
    {
        return mCurrentSpeed;
    }

    float GpsStepCounterSimulator::getDistance()
    {
        return mTotalDistanceMeters;
    }

    float GpsStepCounterSimulator::getAltitude()
    {
        return mLoc.alt;
    }

    void GpsStepCounterSimulator::startStepCounter()
    {
    }

    void GpsStepCounterSimulator::stopStepCounter()
    {
    }

    void GpsStepCounterSimulator::setParamStepCounter(float strideLength)
    {
        mStrideLength = strideLength;
    }

    uint32_t GpsStepCounterSimulator::getStepCounter()
    {
        return mTotalSteps;
    }

    void GpsStepCounterSimulator::buildHeartTable()
    {
        float arc = 0.0f;
        float prevX = 0.0f, prevY = 0.0f;

        for (int i = 0; i <= kHeartSamples; i++) {
            float t  = static_cast<float>(i) / kHeartSamples * 2.0f * static_cast<float>(M_PI);
            float st = std::sin(t);
            float x  = kHeartScale * 16.0f * st * st * st;
            float y  = kHeartScale * (13.0f * std::cos(t)
                                     - 5.0f * std::cos(2.0f * t)
                                     - 2.0f * std::cos(3.0f * t)
                                     -        std::cos(4.0f * t));
            if (i > 0) {
                float dx = x - prevX;
                float dy = y - prevY;
                arc += std::sqrt(dx * dx + dy * dy);
            }
            mHeartTable[i] = {x, y, arc};
            prevX = x;
            prevY = y;
        }
        mLapLength = arc;
    }

    float GpsStepCounterSimulator::getSpeedFactor(float /*distanceAlongLap*/)
    {
        return 1.0f;
    }

    void GpsStepCounterSimulator::updateSpeed(float factor)
    {
        float targetSpeed = mBaseSpeed * factor;
        float delta = targetSpeed - mCurrentSpeed;

        mCurrentSpeed += delta * 0.1f;
        mCurrentSpeed += mSpeedNoise(mGen);

        if (mCurrentSpeed < mMinSpeed) mCurrentSpeed = mMinSpeed;
        if (mCurrentSpeed > mMaxSpeed) mCurrentSpeed = mMaxSpeed;
    }

    uint16_t GpsStepCounterSimulator::calcStride(uint16_t fieldCount)
    {
        assert(fieldCount > 0);

        const std::size_t headerSize = sizeof(SDK::Sensor::Data); // with mValue[1] inside
        const std::size_t extraFields = (fieldCount - 1) * sizeof(SDK::Sensor::Data::Field);

        return static_cast<uint16_t>(headerSize + extraFields);
    }

    void GpsStepCounterSimulator::updateStepCounter(float distance)
    {
        mStepAccumulator += distance;

        while (mStepAccumulator >= mStrideLength)
        {
            mTotalSteps++;
            mStepAccumulator -= mStrideLength;
        }

    }
}