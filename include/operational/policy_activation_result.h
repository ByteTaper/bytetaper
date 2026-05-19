// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OPERATIONAL_POLICY_ACTIVATION_RESULT_H
#define BYTETAPER_OPERATIONAL_POLICY_ACTIVATION_RESULT_H

#include <cstdint>
#include <string>

namespace bytetaper::operational {

enum class PolicyActivationStage : std::uint8_t {
    Committed,
    OperationalDiffComputed,
    CacheNamespacePrepared,
    RouteEpochsBumped,
    CleanupEnqueued,
    MaterializedVariantsInvalidated,
    SnapshotBuilt,
    SnapshotSwapped,
    ActivationPublished,
    Activated,
};

enum class PolicyActivationStatus : std::uint8_t {
    PolicyCommittedNotActivated,
    PolicyActivationInProgress,
    PolicyActivationFailed,
    PolicyActiveCleanupPending,
    PolicyActive,
};

struct PolicyActivationResult {
    bool ok = false;
    PolicyActivationStatus status = PolicyActivationStatus::PolicyActivationFailed;
    PolicyActivationStage stage = PolicyActivationStage::Committed;
    PolicyActivationStage failed_stage = PolicyActivationStage::Committed;

    std::string resource_key;
    std::uint64_t generation = 0;
    std::string policy_id;

    std::uint32_t affected_routes = 0;
    std::uint32_t bumped_route_epochs = 0;
    std::uint32_t l1_cleanup_jobs = 0;
    std::uint32_t l2_cleanup_jobs = 0;
    bool cleanup_pending = false;
    bool old_snapshot_still_active = true;

    std::string message;
    std::string error_code;
};

const char* to_string(PolicyActivationStage stage);
const char* to_string(PolicyActivationStatus status);

PolicyActivationStage parse_policy_activation_stage(const std::string& stage);

} // namespace bytetaper::operational

#endif // BYTETAPER_OPERATIONAL_POLICY_ACTIVATION_RESULT_H
