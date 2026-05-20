// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OPERATIONAL_POLICY_ACTIVATION_BARRIER_H
#define BYTETAPER_OPERATIONAL_POLICY_ACTIVATION_BARRIER_H

#include "control_plane/policy_lifecycle_emitter.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"
#include "operational/policy_activation_result.h"
#include "runtime/policy_snapshot.h"
#include "runtime/route_cache_epoch_store.h"
#include "taperquery/tq_cache_namespace_versioning.h"

namespace bytetaper::cache {
struct L1Cache;
}

namespace bytetaper::runtime_policy {
struct RuntimePolicyMetrics;
}

namespace bytetaper::operational {

using PolicySnapshotBuildFn = runtime::RuntimePolicySnapshotBuildResult (*)(
    const taperquery::TqPolicyDocument& policy_ir, std::uint64_t generation);

struct PolicyActivationBarrierConfig {
    control_plane::PolicyStateStore* policy_state_store = nullptr;
    runtime::RuntimePolicyStore* runtime_policy_store = nullptr;
    runtime::RouteCacheEpochStore* route_cache_epoch_store = nullptr;
    cache::L1Cache* l1_cache = nullptr;
    taperquery::RouteCacheCleanupQueue* l2_cleanup_queue = nullptr;
    control_plane::PolicyResourceKey resource_key =
        control_plane::PolicyResourceKey::default_runtime();
    control_plane::PolicyLifecycleEmitter* lifecycle_emitter = nullptr;
    runtime_policy::RuntimePolicyMetrics* runtime_policy_metrics = nullptr;
    // When non-null, used instead of build_runtime_policy_snapshot_from_ir (tests).
    PolicySnapshotBuildFn snapshot_build_fn = nullptr;
};

struct PolicyActivationRequest {
    std::uint64_t generation = 0;
    std::string policy_id;
    std::uint64_t previous_generation = 0;
    // When set, used as committed (after) policy instead of PolicyStateStore reload.
    const taperquery::TqPolicyDocument* committed_policy_ir = nullptr;
    // When set, used for swap instead of rebuilding from after_ir.
    std::shared_ptr<const runtime::RuntimePolicySnapshot> committed_snapshot;
};

class PolicyActivationBarrier {
public:
    explicit PolicyActivationBarrier(PolicyActivationBarrierConfig config);

    PolicyActivationResult activate(const PolicyActivationRequest& request);

private:
    PolicyActivationBarrierConfig config_;
};

} // namespace bytetaper::operational

#endif // BYTETAPER_OPERATIONAL_POLICY_ACTIVATION_BARRIER_H
