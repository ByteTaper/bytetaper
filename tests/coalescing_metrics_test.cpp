// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/coalescing_metrics.h"

#include <cstring>
#include <gtest/gtest.h>
#include <string>

namespace bytetaper::metrics {

class CoalescingMetricsTest : public ::testing::Test {
protected:
    CoalescingMetrics metrics{};
};

TEST_F(CoalescingMetricsTest, RecordEvents) {
    record_coalescing_event(&metrics, CoalescingMetricEvent::Leader);
    record_coalescing_event(&metrics, CoalescingMetricEvent::Follower);
    record_coalescing_event(&metrics, CoalescingMetricEvent::FollowerCacheHit);
    record_coalescing_event(&metrics, CoalescingMetricEvent::Fallback);
    record_coalescing_event(&metrics, CoalescingMetricEvent::Bypass);
    record_coalescing_event(&metrics, CoalescingMetricEvent::TooManyWaiters);
    record_coalescing_event(&metrics, CoalescingMetricEvent::Leader); // twice

    EXPECT_EQ(metrics.leader_total.load(), 2u);
    EXPECT_EQ(metrics.follower_total.load(), 1u);
    EXPECT_EQ(metrics.follower_cache_hit_total.load(), 1u);
    EXPECT_EQ(metrics.fallback_total.load(), 1u);
    EXPECT_EQ(metrics.bypass_total.load(), 1u);
    EXPECT_EQ(metrics.too_many_waiters_total.load(), 1u);
}

TEST_F(CoalescingMetricsTest, RenderPrometheus) {
    record_coalescing_event(&metrics, CoalescingMetricEvent::Leader);
    record_coalescing_event(&metrics, CoalescingMetricEvent::Follower);

    char buf[4096];
    std::size_t written = render_coalescing_metrics_prometheus(metrics, buf, sizeof(buf));
    ASSERT_GT(written, 0u);

    std::string out(buf);
    EXPECT_NE(out.find("bytetaper_coalescing_leader_total 1"), std::string::npos);
    EXPECT_NE(out.find("bytetaper_coalescing_follower_total 1"), std::string::npos);
    EXPECT_NE(out.find("bytetaper_coalescing_follower_cache_hit_total 0"), std::string::npos);
}

TEST_F(CoalescingMetricsTest, BufferOverflow) {
    char small_buf[10];
    std::size_t written =
        render_coalescing_metrics_prometheus(metrics, small_buf, sizeof(small_buf));
    EXPECT_EQ(written, 0u);
}

} // namespace bytetaper::metrics
