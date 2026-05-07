// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bytetaper::metrics {

enum class CoalescingMetricEvent : std::uint8_t {
    Leader,
    Follower,
    FollowerCacheHit,
    Fallback,
    Bypass,
    TooManyWaiters
};

struct CoalescingMetrics {
    std::atomic<std::uint64_t> leader_total{ 0 };
    std::atomic<std::uint64_t> follower_total{ 0 };
    std::atomic<std::uint64_t> follower_cache_hit_total{ 0 };
    std::atomic<std::uint64_t> fallback_total{ 0 };
    std::atomic<std::uint64_t> bypass_total{ 0 };
    std::atomic<std::uint64_t> too_many_waiters_total{ 0 };
};

void record_coalescing_event(CoalescingMetrics* metrics, CoalescingMetricEvent event);

// Renders all counters as Prometheus text into buf.
// Returns bytes written (excluding null terminator), or 0 on overflow / null buf.
std::size_t render_coalescing_metrics_prometheus(const CoalescingMetrics& metrics, char* buf,
                                                 std::size_t buf_size);

} // namespace bytetaper::metrics
