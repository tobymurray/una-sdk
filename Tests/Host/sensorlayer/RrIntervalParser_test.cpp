/**
 * Host unit tests for SensorDataParser::RrInterval — the opt-in beat-to-beat
 * R-R interval frame (one interval per frame; optional appended source + flags
 * fields, lenient field-count validation like HeartRateEx).
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "SDK/SensorLayer/SensorData.hpp"
#include "SDK/SensorLayer/SensorDataView.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserRrInterval.hpp"

using RrInterval = SDK::SensorDataParser::RrInterval;

namespace {

// A SDK::Sensor::Data with room for rr_ms + source + flags.
struct RrData {
    alignas(SDK::Sensor::Data) uint8_t buf[sizeof(SDK::Sensor::Data) +
            3 * sizeof(SDK::Sensor::Data::Field)] {};
    SDK::Sensor::Data* operator->() { return data(); }
    SDK::Sensor::Data* data() { return reinterpret_cast<SDK::Sensor::Data*>(buf); }
};

int src(RrInterval::Source s) { return static_cast<int>(s); }

} // namespace

TEST(RrIntervalParser, OneFieldMinimalFrame)
{
    RrData m;
    m->mTimeStamp = 1000;
    m->mValue[RrInterval::RR_MS].f = 855.f;

    // The minimal producer (and the sim's minimum) — rr only.
    RrInterval p(SDK::Sensor::DataView(*m.data(), 1));

    EXPECT_TRUE(p.isDataValid());
    EXPECT_EQ(RrInterval::getFieldsNumber(), 1);
    EXPECT_FLOAT_EQ(p.getRrMs(), 855.f);
    EXPECT_FLOAT_EQ(p.getBpm(), 60000.f / 855.f);
    EXPECT_EQ(p.getTimestamp(), 1000u);
    // Metadata absent -> safe defaults, never a false gap.
    EXPECT_EQ(src(p.getSource()), src(RrInterval::Source::UNKNOWN));
    EXPECT_FALSE(p.hasDiscontinuity());
    EXPECT_FALSE(p.isArtifactSuspect());
}

TEST(RrIntervalParser, ThreeFieldExternalWithGap)
{
    RrData m;
    m->mValue[RrInterval::RR_MS].f  = 412.f;
    m->mValue[RrInterval::SOURCE].f = static_cast<float>(RrInterval::Source::EXTERNAL);
    m->mValue[RrInterval::FLAGS].f  = static_cast<float>(RrInterval::Flag::DISCONTINUITY);

    RrInterval p(SDK::Sensor::DataView(*m.data(), 3));

    EXPECT_FLOAT_EQ(p.getRrMs(), 412.f);
    EXPECT_EQ(src(p.getSource()), src(RrInterval::Source::EXTERNAL));
    EXPECT_TRUE(p.hasDiscontinuity());
    EXPECT_FALSE(p.isArtifactSuspect());
}

TEST(RrIntervalParser, ArtifactFlagIndependentOfGap)
{
    RrData m;
    m->mValue[RrInterval::RR_MS].f  = 800.f;
    m->mValue[RrInterval::SOURCE].f = static_cast<float>(RrInterval::Source::ECG);
    m->mValue[RrInterval::FLAGS].f  = static_cast<float>(RrInterval::Flag::ARTIFACT_SUSPECT);

    RrInterval p(SDK::Sensor::DataView(*m.data(), 3));

    EXPECT_EQ(src(p.getSource()), src(RrInterval::Source::ECG));
    EXPECT_TRUE(p.isArtifactSuspect());
    EXPECT_FALSE(p.hasDiscontinuity());
}

TEST(RrIntervalParser, SkinContactLostFlag)
{
    RrData m;
    m->mValue[RrInterval::RR_MS].f  = 900.f;
    m->mValue[RrInterval::SOURCE].f = static_cast<float>(RrInterval::Source::EXTERNAL);
    m->mValue[RrInterval::FLAGS].f  = static_cast<float>(RrInterval::Flag::NO_SKIN_CONTACT);

    RrInterval p(SDK::Sensor::DataView(*m.data(), 3));

    EXPECT_TRUE(p.isSkinContactLost());
    EXPECT_FALSE(p.hasDiscontinuity());
    EXPECT_FALSE(p.isArtifactSuspect());
}

TEST(RrIntervalParser, UnknownSourceForOutOfRangeValue)
{
    RrData m;
    m->mValue[RrInterval::RR_MS].f  = 700.f;
    m->mValue[RrInterval::SOURCE].f = 9.f; // not a known Source

    RrInterval p(SDK::Sensor::DataView(*m.data(), 2));

    EXPECT_EQ(src(p.getSource()), src(RrInterval::Source::UNKNOWN));
}

TEST(RrIntervalParser, ZeroFieldFrameIsInvalid)
{
    RrData m;
    m->mValue[RrInterval::RR_MS].f = 800.f;

    // A malformed / empty frame: getters read safe zeros, never a false gap.
    RrInterval p(SDK::Sensor::DataView(*m.data(), 0));

    EXPECT_FALSE(p.isDataValid());
    EXPECT_FLOAT_EQ(p.getRrMs(), 0.f);
    EXPECT_FLOAT_EQ(p.getBpm(), 0.f);
    EXPECT_EQ(src(p.getSource()), src(RrInterval::Source::UNKNOWN));
    EXPECT_FALSE(p.hasDiscontinuity());
}
