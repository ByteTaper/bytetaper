// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/cache_metrics.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::metrics {

TEST(CacheMetricsTest, L1HitIncrementsCounter) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::L1Hit);
    record_cache_event(&m, CacheMetricEvent::L1Hit);
    EXPECT_EQ(m.l1_hit.load(), 2u);
    EXPECT_EQ(m.l1_miss.load(), 0u);
}

TEST(CacheMetricsTest, L2HitIncrementsCounter) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::L2Hit);
    EXPECT_EQ(m.l2_hit.load(), 1u);
    EXPECT_EQ(m.l2_miss.load(), 0u);
}

TEST(CacheMetricsTest, ExpiredIncrementsCounter) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::Expired);
    EXPECT_EQ(m.expired.load(), 1u);
}

TEST(CacheMetricsTest, BypassIncrementsCounter) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::Bypass);
    EXPECT_EQ(m.bypass.load(), 1u);
}

TEST(CacheMetricsTest, L1StoreMetricsIncremented) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::L1StoreAttempt);
    record_cache_event(&m, CacheMetricEvent::L1StoreSuccess);
    record_cache_event(&m, CacheMetricEvent::L1StoreSkipped);
    record_cache_event(&m, CacheMetricEvent::L1StoreSkippedBodyTooLarge);
    record_cache_event(&m, CacheMetricEvent::L1StoreAdmitted);
    record_cache_event(&m, CacheMetricEvent::L1StoreRejectedBodyTooLarge);
    record_cache_event(&m, CacheMetricEvent::L1StoreRejectedInvalidBody);
    EXPECT_EQ(m.l1_store_attempt.load(), 1u);
    EXPECT_EQ(m.l1_store_success.load(), 1u);
    EXPECT_EQ(m.l1_store_skipped.load(), 1u);
    EXPECT_EQ(m.l1_store_skipped_body_too_large.load(), 1u);
    EXPECT_EQ(m.l1_store_admitted_total.load(), 1u);
    EXPECT_EQ(m.l1_store_rejected_body_too_large_total.load(), 1u);
    EXPECT_EQ(m.l1_store_rejected_invalid_body_total.load(), 1u);
}

TEST(CacheMetricsTest, InvalidationPrepareMetricsIncremented) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::InvalidationPrepareAttempt);
    record_cache_event(&m, CacheMetricEvent::InvalidationPrepared);
    record_cache_event(&m, CacheMetricEvent::InvalidationPrepareSkipped);
    record_cache_event(&m, CacheMetricEvent::InvalidationNoPolicy);
    record_cache_event(&m, CacheMetricEvent::InvalidationMethodNotEnabled);
    record_cache_event(&m, CacheMetricEvent::InvalidationTargetResolveFailed);
    EXPECT_EQ(m.invalidation_prepare_attempt_total.load(), 1u);
    EXPECT_EQ(m.invalidation_prepared_total.load(), 1u);
    EXPECT_EQ(m.invalidation_prepare_skipped_total.load(), 1u);
    EXPECT_EQ(m.invalidation_no_policy_total.load(), 1u);
    EXPECT_EQ(m.invalidation_method_not_enabled_total.load(), 1u);
    EXPECT_EQ(m.invalidation_target_resolve_failed_total.load(), 1u);
}

TEST(CacheMetricsTest, InvalidationApplyMetricsIncremented) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::InvalidationApplyAttempt);
    record_cache_event(&m, CacheMetricEvent::InvalidationApplied);
    record_cache_event(&m, CacheMetricEvent::InvalidationSkipped);
    record_cache_event(&m, CacheMetricEvent::InvalidationFailed);
    record_cache_event(&m, CacheMetricEvent::InvalidationStatusNotSuccessful);
    EXPECT_EQ(m.invalidation_apply_attempt_total.load(), 1u);
    EXPECT_EQ(m.invalidation_applied_total.load(), 1u);
    EXPECT_EQ(m.invalidation_skipped_total.load(), 1u);
    EXPECT_EQ(m.invalidation_failed_total.load(), 1u);
    EXPECT_EQ(m.invalidation_status_not_successful_total.load(), 1u);
}

TEST(CacheMetricsTest, RouteEpochBumpMetricsIncremented) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::RouteEpochBumpAttempt);
    record_cache_event(&m, CacheMetricEvent::RouteEpochBumpSuccess);
    record_cache_event(&m, CacheMetricEvent::RouteEpochBumpFailed);
    EXPECT_EQ(m.route_epoch_bump_attempt_total.load(), 1u);
    EXPECT_EQ(m.route_epoch_bump_success_total.load(), 1u);
    EXPECT_EQ(m.route_epoch_bump_failed_total.load(), 1u);
}

TEST(CacheMetricsTest, L1RemoveMetricsIncremented) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::L1RemoveHit);
    record_cache_event(&m, CacheMetricEvent::L1RemoveMiss);
    record_cache_event(&m, CacheMetricEvent::L1RemoveFailed);
    EXPECT_EQ(m.l1_remove_hit_total.load(), 1u);
    EXPECT_EQ(m.l1_remove_miss_total.load(), 1u);
    EXPECT_EQ(m.l1_remove_failed_total.load(), 1u);
}

TEST(CacheMetricsTest, L2RemoveMetricsIncremented) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::L2RemoveEnqueued);
    record_cache_event(&m, CacheMetricEvent::L2RemoveSuccess);
    record_cache_event(&m, CacheMetricEvent::L2RemoveMiss);
    record_cache_event(&m, CacheMetricEvent::L2RemoveFailed);
    EXPECT_EQ(m.l2_remove_enqueued_total.load(), 1u);
    EXPECT_EQ(m.l2_remove_success_total.load(), 1u);
    EXPECT_EQ(m.l2_remove_miss_total.load(), 1u);
    EXPECT_EQ(m.l2_remove_failed_total.load(), 1u);
}

TEST(CacheMetricsTest, VariantInvalidationMetricsIncremented) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::VariantInvalidatedByRouteEpoch);
    record_cache_event(&m, CacheMetricEvent::VariantExactRemoveAttempt);
    record_cache_event(&m, CacheMetricEvent::VariantExactRemoveSuccess);
    record_cache_event(&m, CacheMetricEvent::VariantExactRemoveMiss);
    EXPECT_EQ(m.variant_invalidated_by_route_epoch_total.load(), 1u);
    EXPECT_EQ(m.variant_exact_remove_attempt_total.load(), 1u);
    EXPECT_EQ(m.variant_exact_remove_success_total.load(), 1u);
    EXPECT_EQ(m.variant_exact_remove_miss_total.load(), 1u);
}

TEST(CacheMetricsTest, PrometheusRenderContainsAllCounters) {
    CacheMetrics m{};
    record_cache_event(&m, CacheMetricEvent::L1Hit);
    record_cache_event(&m, CacheMetricEvent::L2Miss);
    record_cache_event(&m, CacheMetricEvent::Bypass);
    record_cache_event(&m, CacheMetricEvent::L1StoreAttempt);
    record_cache_event(&m, CacheMetricEvent::L1StoreSuccess);
    record_cache_event(&m, CacheMetricEvent::L1StoreSkipped);
    record_cache_event(&m, CacheMetricEvent::L1StoreSkippedBodyTooLarge);
    record_cache_event(&m, CacheMetricEvent::L1StoreAdmitted);
    record_cache_event(&m, CacheMetricEvent::L1StoreRejectedBodyTooLarge);
    record_cache_event(&m, CacheMetricEvent::L1StoreRejectedInvalidBody);

    // Invalidation
    record_cache_event(&m, CacheMetricEvent::InvalidationPrepareAttempt);
    record_cache_event(&m, CacheMetricEvent::InvalidationPrepared);
    record_cache_event(&m, CacheMetricEvent::InvalidationPrepareSkipped);
    record_cache_event(&m, CacheMetricEvent::InvalidationNoPolicy);
    record_cache_event(&m, CacheMetricEvent::InvalidationMethodNotEnabled);
    record_cache_event(&m, CacheMetricEvent::InvalidationTargetResolveFailed);

    record_cache_event(&m, CacheMetricEvent::InvalidationApplyAttempt);
    record_cache_event(&m, CacheMetricEvent::InvalidationApplied);
    record_cache_event(&m, CacheMetricEvent::InvalidationSkipped);
    record_cache_event(&m, CacheMetricEvent::InvalidationFailed);
    record_cache_event(&m, CacheMetricEvent::InvalidationStatusNotSuccessful);

    record_cache_event(&m, CacheMetricEvent::RouteEpochBumpAttempt);
    record_cache_event(&m, CacheMetricEvent::RouteEpochBumpSuccess);
    record_cache_event(&m, CacheMetricEvent::RouteEpochBumpFailed);

    record_cache_event(&m, CacheMetricEvent::L1RemoveHit);
    record_cache_event(&m, CacheMetricEvent::L1RemoveMiss);
    record_cache_event(&m, CacheMetricEvent::L1RemoveFailed);

    record_cache_event(&m, CacheMetricEvent::L2RemoveEnqueued);
    record_cache_event(&m, CacheMetricEvent::L2RemoveSuccess);
    record_cache_event(&m, CacheMetricEvent::L2RemoveMiss);
    record_cache_event(&m, CacheMetricEvent::L2RemoveFailed);

    record_cache_event(&m, CacheMetricEvent::VariantInvalidatedByRouteEpoch);
    record_cache_event(&m, CacheMetricEvent::VariantExactRemoveAttempt);
    record_cache_event(&m, CacheMetricEvent::VariantExactRemoveSuccess);
    record_cache_event(&m, CacheMetricEvent::VariantExactRemoveMiss);

    char buf[8192]{};
    std::size_t n = render_cache_metrics_prometheus(m, buf, sizeof(buf));
    ASSERT_GT(n, 0u);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_hit_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l2_miss_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_bypass_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_store_attempt_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_store_success_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_store_skipped_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_store_skipped_body_too_large_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_l1_store_admitted_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_l1_store_rejected_body_too_large_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_l1_store_rejected_invalid_body_total 1"), nullptr);

    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_prepare_attempt_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_prepared_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_prepare_skipped_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_no_policy_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_method_not_enabled_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_target_resolve_failed_total 1"),
              nullptr);

    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_apply_attempt_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_applied_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_skipped_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_failed_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_invalidation_status_not_successful_total 1"),
              nullptr);

    EXPECT_NE(std::strstr(buf, "bytetaper_cache_route_epoch_bump_attempt_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_route_epoch_bump_success_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_route_epoch_bump_failed_total 1"), nullptr);

    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_remove_hit_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_remove_miss_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l1_remove_failed_total 1"), nullptr);

    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l2_remove_enqueued_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l2_remove_success_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l2_remove_miss_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_l2_remove_failed_total 1"), nullptr);

    EXPECT_NE(std::strstr(buf, "bytetaper_cache_variant_invalidated_by_route_epoch_total 1"),
              nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_variant_exact_remove_attempt_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_variant_exact_remove_success_total 1"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_cache_variant_exact_remove_miss_total 1"), nullptr);
}

} // namespace bytetaper::metrics
