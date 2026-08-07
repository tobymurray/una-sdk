/**
 * @file    TraceBuffer_test.cpp
 * @brief   Host tests for the AthensRun breadcrumb ring.
 */
#include <gui/map/TraceBuffer.hpp>

#include <gtest/gtest.h>

using AthensRun::TraceBuffer;

namespace
{

TEST(TraceBuffer, FirstPointAlwaysKept)
{
    TraceBuffer t;
    EXPECT_TRUE(t.append(100, 100));
    EXPECT_EQ(t.count(), 1u);
}

TEST(TraceBuffer, DecimatesBelowThreshold)
{
    TraceBuffer t;
    t.append(100, 100);
    // 5 px in both axes < INITIAL_THRESHOLD_PX (6): dropped.
    EXPECT_FALSE(t.append(105, 105));
    EXPECT_EQ(t.count(), 1u);
    // 6 px in one axis: kept.
    EXPECT_TRUE(t.append(106, 100));
    EXPECT_EQ(t.count(), 2u);
}

TEST(TraceBuffer, ThinsAtCapacityAndKeepsEndpoints)
{
    TraceBuffer t;
    // March east 6 px per kept point until the buffer is about to thin.
    const int32_t step = TraceBuffer::INITIAL_THRESHOLD_PX;
    for (size_t i = 0; i < TraceBuffer::CAPACITY; ++i) {
        ASSERT_TRUE(t.append(static_cast<int32_t>(i) * step, 0));
    }
    ASSERT_EQ(t.count(), TraceBuffer::CAPACITY);
    const auto firstBefore = t.at(0);

    // The next kept append triggers a thin.
    ASSERT_TRUE(t.append(static_cast<int32_t>(TraceBuffer::CAPACITY) * step, 0));
    EXPECT_LE(t.count(), TraceBuffer::CAPACITY / 2 + 2);
    EXPECT_EQ(t.at(0).x, firstBefore.x);                       // origin kept
    EXPECT_EQ(t.at(t.count() - 1).x,
              static_cast<int32_t>(TraceBuffer::CAPACITY) * step); // newest kept
    EXPECT_EQ(t.thresholdPx(), TraceBuffer::INITIAL_THRESHOLD_PX * 2);
}

TEST(TraceBuffer, SurvivesManyThinsWithoutOverflow)
{
    TraceBuffer t;
    // Simulate a very long run: 20k fixes far enough apart to always keep.
    int32_t x = 0;
    for (int i = 0; i < 20000; ++i) {
        x += t.thresholdPx();          // always exactly at threshold
        t.append(x, 0);
        ASSERT_LE(t.count(), TraceBuffer::CAPACITY);
    }
    EXPECT_GT(t.count(), 2u);
    EXPECT_GT(t.thresholdPx(), TraceBuffer::INITIAL_THRESHOLD_PX);
}

TEST(TraceBuffer, ClearResets)
{
    TraceBuffer t;
    t.append(1, 1);
    t.append(100, 100);
    t.clear();
    EXPECT_EQ(t.count(), 0u);
    EXPECT_EQ(t.thresholdPx(), TraceBuffer::INITIAL_THRESHOLD_PX);
}

} // namespace
