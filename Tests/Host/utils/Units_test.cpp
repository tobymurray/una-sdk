/**
 ******************************************************************************
 * @file    Units_test.cpp
 * @brief   Tests for SDK::Units -- the display-unit conversion and precision
 *          policy shared by every activity app.
 ******************************************************************************
 */

#include "SDK/Units/Units.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using SDK::Units::Formatter;
using SDK::Units::Label;
using SDK::Units::PaceReading;
using SDK::Units::Precision;
using SDK::Units::Reading;
using SDK::Units::System;

namespace
{

constexpr Formatter kMetric{System::Metric};
constexpr Formatter kImperial{System::Imperial};

constexpr float kMetresPerMile = 1609.344f;

} // namespace

// =============================================================================
// Distance
// =============================================================================

TEST(Units, DistanceMetricIsKilometres)
{
    const Reading r = kMetric.distance(5000.0f);

    EXPECT_TRUE(r.valid);
    EXPECT_FLOAT_EQ(r.value, 5.0f);
    EXPECT_EQ(r.label, Label::Kilometre);
}

TEST(Units, DistanceImperialIsMiles)
{
    const Reading r = kImperial.distance(kMetresPerMile);

    EXPECT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 1.0f, 0.001f);
    EXPECT_EQ(r.label, Label::Mile);
}

TEST(Units, DistanceLabelAlwaysMatchesTheValueItCameWith)
{
    // The point of returning one Reading: there is no way to obtain a mile
    // value carrying a "km" label, which is what a separate `bool isImperial`
    // parameter permits.
    for (const System s : {System::Metric, System::Imperial}) {
        const Formatter f{s};
        const Reading   r = f.distance(12345.0f);
        const bool      imperial = (s == System::Imperial);

        EXPECT_EQ(r.label, imperial ? Label::Mile : Label::Kilometre);
        EXPECT_NEAR(r.value, imperial ? 7.6708f : 12.345f, 0.001f);
    }
}

// =============================================================================
// Precision tiers
// =============================================================================

TEST(Units, PrecisionShrinksAsTheNumberGrows)
{
    // Four significant characters at every magnitude: 9.99 / 99.9 / 999.
    EXPECT_EQ(kMetric.distance(0.0f).decimals, 2u);
    EXPECT_EQ(kMetric.distance(9994.0f).decimals, 2u);   // 9.994 km
    EXPECT_EQ(kMetric.distance(10000.0f).decimals, 1u);  // 10.0 km -- tier edge
    EXPECT_EQ(kMetric.distance(99900.0f).decimals, 1u);  // 99.9 km
    EXPECT_EQ(kMetric.distance(100000.0f).decimals, 0u); // 100 km -- tier edge
    EXPECT_EQ(kMetric.distance(250000.0f).decimals, 0u);
}

TEST(Units, PrecisionTiersAreDrivenByDisplayUnitsNotBySi)
{
    // 15 km is 9.32 mi, so the same SI distance lands in different tiers.
    EXPECT_EQ(kMetric.distance(15000.0f).decimals, 1u);
    EXPECT_EQ(kImperial.distance(15000.0f).decimals, 2u);
}

TEST(Units, TheSameDistanceGetsTheSamePrecisionEverywhere)
{
    // Regression guard for the divergence this module replaced: the track face
    // rendered 15 km as "15.00" where the lap screen, reading the same
    // Track::Data, rendered "15.0". One policy, so one answer.
    for (const float metres : {900.0f, 9994.0f, 15000.0f, 150000.0f}) {
        EXPECT_EQ(kMetric.distance(metres).decimals,
                  kMetric.distance(metres, SDK::Units::Policy::kDistance).decimals);
        EXPECT_EQ(kMetric.distance(metres).decimals,
                  kMetric.distance(metres,
                      SDK::Units::withPlaceholderBelow(SDK::Units::Policy::kDistance, 0.0f)).decimals);
    }
}

// =============================================================================
// Placeholder handling
// =============================================================================

TEST(Units, DefaultDistancePolicyHasNoPlaceholder)
{
    EXPECT_TRUE(kMetric.distance(0.0f).valid);
    EXPECT_TRUE(kMetric.distance(-1.0f).valid);
}

TEST(Units, PlaceholderBelowMarksLowValuesInvalid)
{
    constexpr Precision p = SDK::Units::withPlaceholderBelow(SDK::Units::Policy::kDistance, 0.0f);

    EXPECT_FALSE(kMetric.distance(-1.0f, p).valid);
    EXPECT_TRUE(kMetric.distance(0.0f, p).valid);
    EXPECT_TRUE(kMetric.distance(1.0f, p).valid);
}

TEST(Units, InvalidReadingStillCarriesItsLabel)
{
    // The unit text stays correct while the value shows a placeholder, so a
    // screen never flips between "km" and "mi" as data comes and goes.
    constexpr Precision p = SDK::Units::withPlaceholderBelow(SDK::Units::Policy::kDistance, 0.0f);
    const Reading       r = kImperial.distance(-1.0f, p);

    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.label, Label::Mile);
}

TEST(Units, WithoutPlaceholderClearsAFloor)
{
    constexpr Precision floored = SDK::Units::withPlaceholderBelow(SDK::Units::Policy::kDistance, 100.0f);
    constexpr Precision open    = SDK::Units::withoutPlaceholder(floored);

    EXPECT_FALSE(kMetric.distance(1000.0f, floored).valid);
    EXPECT_TRUE(kMetric.distance(1000.0f, open).valid);
}

// =============================================================================
// Elevation
// =============================================================================

TEST(Units, ElevationIsWholeUnits)
{
    const Reading metric = kMetric.elevation(123.7f);
    EXPECT_TRUE(metric.valid);
    EXPECT_FLOAT_EQ(metric.value, 123.7f);
    EXPECT_EQ(metric.decimals, 0u);
    EXPECT_EQ(metric.label, Label::Metre);

    const Reading imperial = kImperial.elevation(100.0f);
    EXPECT_NEAR(imperial.value, 328.084f, 0.01f);
    EXPECT_EQ(imperial.label, Label::Foot);
}

TEST(Units, ElevationAcceptsNegativeValues)
{
    // Descent below the start altitude is real data, not a missing reading.
    const Reading r = kMetric.elevation(-12.0f);

    EXPECT_TRUE(r.valid);
    EXPECT_FLOAT_EQ(r.value, -12.0f);
}

// =============================================================================
// Pace
// =============================================================================

TEST(Units, PaceMetricIsPerKilometre)
{
    // 0.3 s/m == 300 s/km == 5:00 /km
    const PaceReading p = kMetric.pace(0.3f);

    EXPECT_TRUE(p.valid);
    EXPECT_EQ(p.totalMinutes(), 5u);
    EXPECT_EQ(p.hms.s, 0u);
    EXPECT_EQ(p.label, Label::MinPerKm);
}

TEST(Units, PaceImperialIsPerMile)
{
    // 5:00 /km == 8:03 /mi
    const PaceReading p = kImperial.pace(0.3f);

    EXPECT_TRUE(p.valid);
    EXPECT_EQ(p.totalMinutes(), 8u);
    EXPECT_EQ(p.hms.s, 3u);
    EXPECT_EQ(p.label, Label::MinPerMile);
}

TEST(Units, PaceRoundsToTheNearestSecondAndCarries)
{
    // 299.6 s/km must render 5:00, not 4:59 -- truncating loses a second and
    // makes a lap's pace disagree with its displayed duration.
    const PaceReading p = kMetric.pace(0.2996f);

    EXPECT_TRUE(p.valid);
    EXPECT_EQ(p.totalMinutes(), 5u);
    EXPECT_EQ(p.hms.s, 0u);
}

TEST(Units, PaceBelowTheFloorIsInvalid)
{
    EXPECT_FALSE(kMetric.pace(0.02f).valid);   // 20 s/km
    EXPECT_TRUE(kMetric.pace(0.031f).valid);   // 31 s/km
}

TEST(Units, PaceFloorAppliesInDisplayUnits)
{
    // 25 s/km is under the 30 s floor on a metric watch but is 40 s/mi on an
    // imperial one, so the imperial reading is presentable.
    EXPECT_FALSE(kMetric.pace(0.025f).valid);
    EXPECT_TRUE(kImperial.pace(0.025f).valid);
}

TEST(Units, PaceRejectsNonPositiveInput)
{
    EXPECT_FALSE(kMetric.pace(0.0f).valid);
    EXPECT_FALSE(kMetric.pace(-1.0f).valid);
}

TEST(Units, PaceInvalidStillCarriesItsLabel)
{
    EXPECT_EQ(kImperial.pace(0.0f).label, Label::MinPerMile);
}

TEST(Units, PaceBeyondAnHourKeepsHoursSeparate)
{
    // 4 s/m == 4000 s/km == 1:06:40. The caller decides between "66:40" and
    // "1:06", so both forms stay reachable.
    const PaceReading p = kMetric.pace(4.0f);

    EXPECT_TRUE(p.valid);
    EXPECT_EQ(p.hms.h, 1u);
    EXPECT_EQ(p.hms.m, 6u);
    EXPECT_EQ(p.hms.s, 40u);
    EXPECT_EQ(p.totalMinutes(), 66u);
}

// =============================================================================
// Formatter state
// =============================================================================

TEST(Units, SetImperialBridgesFromISettings)
{
    Formatter f;
    EXPECT_EQ(f.system(), System::Metric);
    EXPECT_FALSE(f.isImperial());

    f.setImperial(true);
    EXPECT_EQ(f.system(), System::Imperial);
    EXPECT_TRUE(f.isImperial());

    f.setImperial(false);
    EXPECT_EQ(f.system(), System::Metric);
}

TEST(Units, PolicyIsUsableInConstantExpressions)
{
    // The whole policy folds at compile time, so it costs no flash on the
    // watch beyond the values it produces.
    static_assert(Formatter{System::Metric}.distance(5000.0f).value == 5.0f, "");
    static_assert(Formatter{System::Metric}.distance(5000.0f).decimals == 2, "");
    static_assert(Formatter{System::Imperial}.distance(1000.0f).label == Label::Mile, "");
    static_assert(Formatter{System::Metric}.pace(0.3f).valid, "");
    SUCCEED();
}
