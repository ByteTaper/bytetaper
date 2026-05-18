// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "runtime/policy_snapshot.h"
#include "taperquery/policy_ir.h"
#include "taperquery/policy_persistence.h"
#include "taperquery/route_analysis.h"
#include "taperquery/tq_cache_namespace_versioning.h"
#include "taperquery/tq_diagnostic.h"
#include "taperquery/tq_plan.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

enum class TqApplyMode : std::uint8_t {
    DryRun,
    Apply,
};

enum class TqApplySourceType : std::uint8_t {
    TaperQuery,
};

enum class TqApplyStatus : std::uint8_t {
    Applied,
    DryRunReady,
    RejectedInvalidRequest,
    RejectedParseError,
    RejectedCompileError,
    RejectedValidation,
    RejectedRouteAnalysis,
    RejectedCasMismatch,
    RejectedNoChanges,
    RejectedSnapshotBuildFailed,
    RejectedPersistenceFailed,
    RejectedCanonicalYamlRoundTripFailed,
    RejectedCanonicalYamlMismatch,
    InternalError,
};

struct TqApplyRequest {
    TqApplySourceType source_type = TqApplySourceType::TaperQuery;
    TqApplyMode mode = TqApplyMode::DryRun;

    std::string source;
    std::string expected_base_identity;

    // Optional metadata to be consumed by audit in a later task.
    std::string operator_id;
    std::string request_id;

    bool include_unchanged_routes = false;
    bool include_field_level_changes = true;
    bool strict_production = true;
};

struct TqApplyDiagnostic {
    std::string severity;
    std::string code;
    std::string route_id;
    std::string field_path;
    std::string reason;
    std::string hint;
};

struct TqApplyResult {
    bool ok = false;
    TqApplyStatus status = TqApplyStatus::InternalError;

    std::string current_policy_identity;
    std::string candidate_policy_identity;
    std::string expected_base_identity;
    std::string applied_policy_identity;

    std::uint64_t before_generation = 0;
    std::uint64_t after_generation = 0;

    TqApplyPlan plan;
    TqRouteAnalysisReport route_analysis;

    std::vector<TqApplyDiagnostic> diagnostics;

    TqCacheNamespaceVersioningResult cache_namespace_versioning;

    std::vector<std::string> enqueued_cleanups;

    std::string message;
};

class TqSnapshotBuilder {
public:
    virtual ~TqSnapshotBuilder() = default;
    virtual runtime::RuntimePolicySnapshotBuildResult
    build_snapshot(const TqPolicyDocument& policy_ir, std::uint64_t generation) = 0;
};

class TqApplyAuditStore;

class TqApplyService {
public:
    explicit TqApplyService(runtime::RuntimePolicyStore* policy_store,
                            TqSnapshotBuilder* builder = nullptr,
                            TqApplyAuditStore* audit_store = nullptr,
                            LocalPolicyPersistenceConfig persistence_config = {},
                            runtime::RouteCacheEpochStore* epoch_store = nullptr,
                            RouteCacheCleanupQueue* cleanup_queue = nullptr);

    TqApplyResult execute(const TqApplyRequest& request);

private:
    TqApplyResult execute_impl(const TqApplyRequest& request);

    runtime::RuntimePolicyStore* policy_store_ = nullptr;
    TqSnapshotBuilder* builder_ = nullptr;
    TqApplyAuditStore* audit_store_ = nullptr;
    LocalPolicyPersistenceConfig persistence_config_;
    runtime::RouteCacheEpochStore* epoch_store_ = nullptr;
    RouteCacheCleanupQueue* cleanup_queue_ = nullptr;
};

} // namespace bytetaper::taperquery
