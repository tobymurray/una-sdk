// EXPERIMENT: does the simulator sensor path deliver every beat of an
// EVENT_BASED RR_INTERVAL producer?
//
// A: k intervals from one 0x2A37 notification, all stamped with the single
//    arrival instant (what a naive strap producer emits).
// B: the same k intervals back-dated so each frame carries the beat instant
//    the interval ends on.
// C: back-dated, but at an implausibly fast rate (sub-minPeriod/2 spacing).
// D: does the delivered payload preserve beat order and the u32 metadata?

#include <gtest/gtest.h>
#include <cstdio>
#include <vector>

#include "SDK/Simulator/Components/SensorManager.hpp"
#include "SDK/Simulator/Components/SensorDriver.hpp"
#include "SDK/Simulator/Components/Sensors/ISensor.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserRrInterval.hpp"
#include "support/SimSystemDouble.hpp"

using Parser = SDK::SensorDataParser::RrInterval;

namespace {

struct Recorder : public SDK::Interface::ISensorDataListener {
    void onSdlNewData(uint16_t, const SDK::Sensor::Data* d, uint16_t count, uint16_t stride) override
    {
        SDK::Sensor::DataBatch b(d, count, stride);
        for (uint16_t i = 0; i < b.size(); ++i) {
            Parser p(b[i]);
            rr.push_back(p.getRrMs());
            srcs.push_back(static_cast<int>(p.getSource()));
            disc.push_back(p.hasDiscontinuity());
            ts.push_back(p.getTimestamp());
        }
        callbacks++;
    }
    std::vector<float>    rr;
    std::vector<int>      srcs;
    std::vector<bool>     disc;
    std::vector<uint32_t> ts;
    int                   callbacks = 0;
};

struct Ctrl : public Sensor::ISensorDriverCtrl {
    explicit Ctrl(float minP) : minPeriod(minP) {}
    float       sdcStart(Sensor::Driver*, float p) override { return p; }
    void        sdcStop(Sensor::Driver*) override {}
    float       sdcUpdatePeriod(Sensor::Driver*, float p) override { return p; }
    float       sdcGetMinPeriod(Sensor::Driver*) override { return minPeriod; }
    const char* sdcGetDescription(Sensor::Driver*) override { return "rr-exp"; }
    float       minPeriod;
};

struct NullSensor : public Interface::ISensor {
    void sensorRefresh() override {}
};

// Push k beats. If backdate, each frame gets the beat instant the interval ends
// on; otherwise every frame carries the same arrival instant.
Recorder run(const std::vector<float>& intervals, bool backdate, float minPeriod, uint32_t arrivalMs)
{
    NullSensor s;
    Ctrl       c(minPeriod);
    Sensor::Driver d(s, SDK::Sensor::Type::RR_INTERVAL, Parser::getFieldsNumber(),
                     c, Sensor::Driver::Mode::EVENT_BASED);
    Recorder r;
    d.connect(&r, 0, 0);

    // Back-dated instant of interval i: arrival - sum of the intervals after it.
    float tailAfter = 0.f;
    std::vector<uint32_t> stamps(intervals.size());
    for (size_t i = intervals.size(); i-- > 0;) {
        stamps[i] = static_cast<uint32_t>(arrivalMs - tailAfter);
        tailAfter += intervals[i];
    }

    auto& sample = d.getDataSample();
    for (size_t i = 0; i < intervals.size(); ++i) {
        sample.setTimestamp(backdate ? stamps[i] : arrivalMs);
        sample.f[Parser::Field::RR_MS]  = intervals[i];
        sample.u[Parser::Field::SOURCE] = static_cast<uint32_t>(Parser::Source::EXTERNAL);
        sample.u[Parser::Field::FLAGS]  = (i == 0) ? Parser::Flag::DISCONTINUITY : 0u;
        d.pushDataSample();
    }
    d.disconnect(&r);
    return r;
}

const std::vector<float> kBeats = {812.f, 986.f, 1170.f, 903.f, 855.f, 971.f, 1004.f, 890.f, 940.f, 1010.f};

} // namespace

TEST(RrDelivery, A_SharedArrivalTimestampDropsBeats)
{
    Recorder r = run(kBeats, /*backdate=*/false, 40.f, 100000);
    std::printf("[A] pushed=%zu delivered=%zu callbacks=%d\n", kBeats.size(), r.rr.size(), r.callbacks);
    EXPECT_EQ(r.rr.size(), kBeats.size()) << "shared-arrival burst lost beats";
}

TEST(RrDelivery, B_BackdatedPerBeatTimestampsDeliverAll)
{
    Recorder r = run(kBeats, /*backdate=*/true, 40.f, 100000);
    std::printf("[B] pushed=%zu delivered=%zu callbacks=%d\n", kBeats.size(), r.rr.size(), r.callbacks);
    for (size_t i = 0; i < r.rr.size() && i < 3; ++i) {
        std::printf("[B]   beat %zu rr=%.0f src=%d disc=%d ts=%u\n",
                    i, r.rr[i], r.srcs[i], (int)r.disc[i], r.ts[i]);
    }
    EXPECT_EQ(r.rr.size(), kBeats.size());
}

TEST(RrDelivery, C_BackdatedButFasterThanHalfMinPeriod)
{
    // 15 ms intervals: below minPeriod/2 == 20 ms. Physiologically impossible,
    // but it is exactly the artefact an HRV consumer is supposed to see and
    // reject on its own terms.
    std::vector<float> fast(10, 15.f);
    Recorder r = run(fast, /*backdate=*/true, 40.f, 100000);
    std::printf("[C] pushed=%zu delivered=%zu callbacks=%d\n", fast.size(), r.rr.size(), r.callbacks);
    EXPECT_EQ(r.rr.size(), fast.size()) << "sub-minPeriod/2 intervals silently dropped";
}

TEST(RrDelivery, D_OrderAndMetadataSurviveTheWire)
{
    Recorder r = run(kBeats, /*backdate=*/true, 40.f, 100000);
    ASSERT_GE(r.rr.size(), 2u);
    for (size_t i = 0; i < r.rr.size(); ++i) {
        EXPECT_FLOAT_EQ(r.rr[i], kBeats[i]) << "beat order broken at " << i;
        EXPECT_EQ(r.srcs[i], static_cast<int>(Parser::Source::EXTERNAL));
    }
    EXPECT_TRUE(r.disc[0]);
    if (r.disc.size() > 1) EXPECT_FALSE(r.disc[1]);
    // Loss is detectable ONLY if timestamps are per-beat: t[n]-t[n-1] == rr[n].
    for (size_t i = 1; i < r.ts.size(); ++i) {
        EXPECT_NEAR(static_cast<double>(r.ts[i]) - r.ts[i-1], kBeats[i], 1.5)
            << "timestamp/interval identity broken at " << i;
    }
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int rc = RUN_ALL_TESTS();
    simtest::setManagerRunning(false);
    return rc;
}
