// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/policy_activation_result.h"

namespace bytetaper::operational {

const char* to_string(PolicyActivationStage stage) {
    switch (stage) {
    case PolicyActivationStage::Committed:
        return "committed";
    case PolicyActivationStage::OperationalDiffComputed:
        return "operational_diff_computed";
    case PolicyActivationStage::CacheNamespacePrepared:
        return "cache_namespace_prepared";
    case PolicyActivationStage::RouteEpochsBumped:
        return "route_epochs_bumped";
    case PolicyActivationStage::CleanupEnqueued:
        return "cleanup_enqueued";
    case PolicyActivationStage::MaterializedVariantsInvalidated:
        return "materialized_variants_invalidated";
    case PolicyActivationStage::SnapshotBuilt:
        return "snapshot_built";
    case PolicyActivationStage::SnapshotSwapped:
        return "snapshot_swapped";
    case PolicyActivationStage::ActivationPublished:
        return "activation_published";
    case PolicyActivationStage::Activated:
        return "activated";
    }
    return "unknown";
}

const char* to_string(PolicyActivationStatus status) {
    switch (status) {
    case PolicyActivationStatus::PolicyCommittedNotActivated:
        return "policy_committed_not_activated";
    case PolicyActivationStatus::PolicyActivationInProgress:
        return "policy_activation_in_progress";
    case PolicyActivationStatus::PolicyActivationFailed:
        return "policy_activation_failed";
    case PolicyActivationStatus::PolicyActiveCleanupPending:
        return "policy_active_cleanup_pending";
    case PolicyActivationStatus::PolicyActive:
        return "policy_active";
    }
    return "unknown";
}

PolicyActivationStage parse_policy_activation_stage(const std::string& stage) {
    if (stage == "committed") {
        return PolicyActivationStage::Committed;
    }
    if (stage == "operational_diff_computed") {
        return PolicyActivationStage::OperationalDiffComputed;
    }
    if (stage == "cache_namespace_prepared") {
        return PolicyActivationStage::CacheNamespacePrepared;
    }
    if (stage == "route_epochs_bumped") {
        return PolicyActivationStage::RouteEpochsBumped;
    }
    if (stage == "cleanup_enqueued") {
        return PolicyActivationStage::CleanupEnqueued;
    }
    if (stage == "materialized_variants_invalidated") {
        return PolicyActivationStage::MaterializedVariantsInvalidated;
    }
    if (stage == "snapshot_built") {
        return PolicyActivationStage::SnapshotBuilt;
    }
    if (stage == "snapshot_swapped") {
        return PolicyActivationStage::SnapshotSwapped;
    }
    if (stage == "activation_published") {
        return PolicyActivationStage::ActivationPublished;
    }
    if (stage == "activated") {
        return PolicyActivationStage::Activated;
    }
    return PolicyActivationStage::Committed;
}

} // namespace bytetaper::operational
