/**
 ******************************************************************************
 * @file    SensorRrIntervalReplay.hpp
 * @brief   Simulator mock: replays recorded beat-to-beat R-R intervals as
 *          RR_INTERVAL sensor frames.
 ******************************************************************************
 *
 * Reads a Polar Sensor Logger "*_RR.txt" export
 * (header line, then rows "Phone timestamp;RR-interval [ms]") and streams the
 * intervals to any app subscribed to SDK::Sensor::Type::RR_INTERVAL, paced in
 * real time by the RR values themselves. This lets the whole HRV / DFA alpha1
 * pipeline be developed and validated against real strap data with no hardware
 * and no firmware -- the SDK-side half of the RR co-design.
 *
 * Header-only (all methods in-class => implicitly inline) so it needs no entry
 * in any app's simulator Makefile source list; mirrors the header-only
 * generator style used elsewhere in the simulator (e.g. HeatRateSimulator.hpp).
 *
 * Emits one interval PER FRAME (three fields: rr_ms, source=EXTERNAL, flags),
 * which is the only shape the Data model honestly supports for a stream of
 * intervals -- field count is derived from the per-driver stride, so it is
 * fixed, and there is no in-band length field. Because the driver is
 * EVENT_BASED the per-listener queue capacity is 1, so each interval arrives as
 * its own delivery; a consumer iterates DataBatch::size() (== 1 here) to stay
 * correct if a future producer batches.
 *
 * EVENT_BASED also pins the driver period to the min-period floor, so the
 * per-listener sample-rate adapter's emit threshold sits far below any real
 * inter-beat gap and normal beats are never decimated (a corrupt sub-~20 ms
 * interval would still be dropped by the adapter -- see the min-period note).
 *
 * DISCONTINUITY (Flag bit 0) is derived from the phone-timestamp column: when a
 * beat's arrival is later than the previous arrival by more than its own RR
 * (plus slack), the strap dropped out and this beat is not contiguous with the
 * last -- the flag lets a consumer avoid feeding a fabricated interval into
 * RMSSD / alpha1. The first beat of the stream is always flagged discontinuous.
 *
 * Intervals are replayed VERBATIM (no physiological filtering -- artefact
 * correction is the consumer's job). The stream plays once and stops; it does
 * not loop, because a wrap would inject a non-physiological successive-difference
 * that corrupts RMSSD (re-subscribe to replay). Each frame is timestamped with
 * the beat instant the interval ends on, reconstructed from the replay start.
 *
 ******************************************************************************
 */

#ifndef __SENSOR_RR_INTERVAL_REPLAY_HPP
#define __SENSOR_RR_INTERVAL_REPLAY_HPP

#include "SDK/Simulator/Components/SensorDriver.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/Simulator/OS/SwTimer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserRrInterval.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using namespace Interface;

namespace Sensor
{
    class RrIntervalReplay : public ISensor,
                             public Sensor::ISensorDriverCtrl
    {
        using Parser = SDK::SensorDataParser::RrInterval;

    public:
        RrIntervalReplay()
            : mDriver(*this,
                      SDK::Sensor::Type::RR_INTERVAL,
                      kFieldCount, // rr_ms + source + flags, one interval per frame
                      *this,
                      Sensor::Driver::Mode::EVENT_BASED) // beats are events, not a fixed rate
            , mTimer()
            , mCursor(0)
            , mStartMs(0)
            , mStarted(false)
            , mTruncated(false)
        {
        }

        Sensor::Driver& getDriver() { return mDriver; }

        /**
         * @brief Whether the last loadFile() hit the interval cap and dropped rows.
         */
        bool wasTruncated() const { return mTruncated; }

        /**
         * @brief Load a Polar Sensor Logger "*_RR.txt" file.
         * @param path Path to the export (relative to the simulator working dir).
         * @return true if at least one interval was parsed.
         */
        bool loadFile(const char* path)
        {
            mRr.clear();
            mCumMs.clear();
            mFlags.clear();
            mCursor    = 0;
            mStartMs   = 0;
            mStarted   = false;
            mTruncated = false;

            std::ifstream in(path);
            if (!in.is_open()) {
                return false;
            }

            std::string line;
            std::getline(in, line); // discard the header row

            double   cum      = 0.0;
            uint32_t prevArr  = 0;
            bool     havePrev = false;
            while (std::getline(in, line)) {
                if (mRr.size() >= kMaxIntervals) {
                    mTruncated = true;
                    break;
                }
                const std::size_t sep = line.find(';');
                if (sep == std::string::npos) {
                    continue;
                }
                std::string rrStr = line.substr(sep + 1);
                const std::size_t sep2 = rrStr.find(';');
                if (sep2 != std::string::npos) {
                    rrStr = rrStr.substr(0, sep2);
                }
                while (!rrStr.empty() &&
                       (rrStr.back() == '\r' || rrStr.back() == '\n' || rrStr.back() == ' ')) {
                    rrStr.pop_back();
                }

                // Parse the RR value with the "C" locale so a '.' decimal is read
                // regardless of the host locale (Polar exports integer ms).
                std::istringstream iss(rrStr);
                iss.imbue(std::locale::classic());
                float rr = 0.0f;
                iss >> rr;
                if (iss.fail() || rr <= 0.0f) { // skip unparseable / non-positive only
                    continue;
                }

                // Discontinuity: derive from the phone-timestamp column, not the RR
                // value (a dropout leaves no RR notification, so only arrival time
                // reveals the gap). First beat is always discontinuous.
                uint32_t flags = 0;
                uint32_t arr   = 0;
                const bool haveArr = parseMsOfDay(line.substr(0, sep), arr);
                if (!havePrev) {
                    flags |= Parser::Flag::DISCONTINUITY;
                } else if (haveArr) {
                    int64_t gap = static_cast<int64_t>(arr) - static_cast<int64_t>(prevArr);
                    if (gap < 0) {
                        gap += 24LL * 60 * 60 * 1000; // midnight wrap
                    }
                    if (gap - static_cast<int64_t>(rr) > kGapSlackMs) {
                        flags |= Parser::Flag::DISCONTINUITY;
                    }
                }
                if (haveArr) {
                    prevArr  = arr;
                    havePrev = true;
                }

                // Replay verbatim; artefact correction belongs to the consumer.
                cum += rr;
                mRr.push_back(rr);
                mCumMs.push_back(static_cast<uint32_t>(cum));
                mFlags.push_back(flags);
            }
            return !mRr.empty();
        }

        //// ISensorDriverCtrl
        float sdcStart(Sensor::Driver* driver, float period) override
        {
            (void)driver;
            mCursor  = 0;
            mStarted = false;
            mTimer.start(static_cast<uint32_t>(period));
            return period;
        }

        void sdcStop(Sensor::Driver* driver) override
        {
            (void)driver;
            mTimer.stop();
        }

        float sdcUpdatePeriod(Sensor::Driver* driver, float period) override
        {
            (void)driver;
            mTimer.start(static_cast<uint32_t>(period));
            return period;
        }

        float sdcGetMinPeriod(Sensor::Driver* driver) override
        {
            (void)driver;
            return mMinPeriod;
        }

        const char* sdcGetDescription(Sensor::Driver* driver) override
        {
            (void)driver;
            return "RR interval replay (mock)";
        }

        //// ISensor
        void sensorRefresh() override
        {
            if (!mTimer.check() || mRr.empty()) {
                return;
            }

            const uint32_t now = ::Driver::SwTimer::getTicks();
            if (!mStarted) {
                mStartMs = now;
                mStarted = true;
            }
            const uint32_t elapsed = now - mStartMs;

            auto& sample = mDriver.getDataSample();
            while (mCursor < mRr.size() && mCumMs[mCursor] <= elapsed) {
                sample.setTimestamp(mStartMs + mCumMs[mCursor]);
                sample.f[Parser::Field::RR_MS]  = mRr[mCursor];
                sample.f[Parser::Field::SOURCE] = static_cast<float>(Parser::Source::EXTERNAL);
                sample.f[Parser::Field::FLAGS]  = static_cast<float>(mFlags[mCursor]);
                mDriver.pushDataSample();
                ++mCursor;
            }
            // Plays once and then stops. It deliberately does not loop: a wrap
            // would inject a non-physiological successive-difference that corrupts
            // RMSSD. Re-subscribe (sdcStart) to replay from the top.
        }

    private:
        // Parse "YYYY-MM-DDThh:mm:ss.mmm" -> milliseconds-of-day. false if no time part.
        static bool parseMsOfDay(const std::string& ts, uint32_t& outMs)
        {
            const std::size_t t = ts.find('T');
            if (t == std::string::npos) {
                return false;
            }
            int hh = 0, mm = 0, ss = 0, ms = 0;
            if (std::sscanf(ts.c_str() + t + 1, "%d:%d:%d.%d", &hh, &mm, &ss, &ms) < 3) {
                return false;
            }
            outMs = ((static_cast<uint32_t>(hh) * 60 + mm) * 60 + ss) * 1000u
                    + static_cast<uint32_t>(ms);
            return true;
        }

        static constexpr uint16_t   kFieldCount   = 3;       // rr_ms + source + flags
        static constexpr float      mMinPeriod    = 40.0f;   // ms
        static constexpr int64_t    kGapSlackMs   = 250;     // arrival-minus-RR slack before "gap"
        static constexpr std::size_t kMaxIntervals = 500000; // ~16x a full overnight file; bounds memory

        Sensor::Driver        mDriver;
        ::Driver::SwTimer     mTimer;
        std::vector<float>    mRr;    // RR intervals, milliseconds
        std::vector<uint32_t> mCumMs; // cumulative ms from replay start (pacing)
        std::vector<uint32_t> mFlags; // per-interval Flag bits (DISCONTINUITY)
        std::size_t           mCursor;
        uint32_t              mStartMs;
        bool                  mStarted;
        bool                  mTruncated;
    }; /* class RrIntervalReplay */

} /* namespace Sensor */

#endif /* __SENSOR_RR_INTERVAL_REPLAY_HPP */
