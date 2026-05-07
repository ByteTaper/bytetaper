// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/coalescing_policy.h"

namespace bytetaper::policy {

const char* validate_coalescing_policy(const CoalescingPolicy& policy) {
    if (!policy.enabled) {
        return nullptr;
    }

    if (policy.backend_timeout_ms == 0) {
        return "backend_timeout_ms must be > 0";
    }
    if (policy.backend_timeout_ms > 30000) {
        return "backend_timeout_ms must be <= 30000";
    }

    if (policy.handoff_buffer_ms == 0) {
        return "handoff_buffer_ms must be > 0";
    }
    if (policy.handoff_buffer_ms > 5000) {
        return "handoff_buffer_ms must be <= 5000";
    }

    if (policy.result_ready_retention_ms < 10) {
        return "result_ready_retention_ms must be >= 10";
    }
    if (policy.result_ready_retention_ms > 1000) {
        return "result_ready_retention_ms must be <= 1000";
    }

    if (policy.result_ready_retention_ms >= policy.backend_timeout_ms) {
        return "result_ready_retention_ms must be < backend_timeout_ms";
    }

    if (policy.max_waiters_per_key == 0) {
        return "coalescing.max_waiters_per_key must be > 0";
    }

    return nullptr;
}

} // namespace bytetaper::policy
