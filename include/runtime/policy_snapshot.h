// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_SNAPSHOT_H
#define BYTETAPER_RUNTIME_POLICY_SNAPSHOT_H

#include "extproc/default_pipelines.h"
#include "policy/route_matcher.h"
#include "policy/route_policy.h"
#include "taperquery/policy_ir.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bytetaper::runtime {

struct RuntimePolicySnapshot {
    std::string policy_identity;
    std::string source_name;
    std::uint64_t generation = 0;

    taperquery::TqPolicyDocument policy_ir;

    std::vector<policy::RoutePolicy> routes;
    policy::CompiledRouteMatcher route_matcher{};
    bool route_matcher_ready = false;

    extproc::CompiledRouteRuntimeTable route_runtimes{};
};

struct RuntimePolicySnapshotBuildResult {
    bool ok = false;
    std::shared_ptr<const RuntimePolicySnapshot> snapshot{};
    std::string error;
};

RuntimePolicySnapshotBuildResult
build_runtime_policy_snapshot_from_routes(const policy::RoutePolicy* routes,
                                          std::size_t route_count, const char* source_name,
                                          std::uint64_t generation);

RuntimePolicySnapshotBuildResult
build_runtime_policy_snapshot_from_ir(const taperquery::TqPolicyDocument& policy_ir,
                                      std::uint64_t generation);

class RuntimePolicyStore {
public:
    RuntimePolicyStore() = default;

    std::shared_ptr<const RuntimePolicySnapshot> load() const;

    bool install_initial(std::shared_ptr<const RuntimePolicySnapshot> snapshot,
                         std::string* error_out);

    bool swap(std::shared_ptr<const RuntimePolicySnapshot> snapshot, std::string* error_out);
    bool swap_if_current(const std::string& expected_identity,
                         std::shared_ptr<const RuntimePolicySnapshot> snapshot,
                         std::string* error_out);

    std::uint64_t next_generation();
    std::unique_lock<std::mutex> acquire_apply_lock();

private:
    mutable std::mutex mu_;
    std::mutex apply_mu_;
    std::shared_ptr<const RuntimePolicySnapshot> active_;
    std::uint64_t generation_ = 0;
};

} // namespace bytetaper::runtime

#endif // BYTETAPER_RUNTIME_POLICY_SNAPSHOT_H
