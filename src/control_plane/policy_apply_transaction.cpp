// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_apply_transaction.h"

#include "control_plane/policy_state_key.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_normalize.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_ir_yaml_roundtrip.h"
#include "taperquery/policy_persistence.h"
#include "taperquery/route_analysis.h"
#include "taperquery/tq_plan.h"

#include <chrono>

namespace bytetaper::control_plane {

namespace {

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

const char* to_string(PolicyApplyStage stage) {
    switch (stage) {
    case PolicyApplyStage::LoadActive:
        return "load_active";
    case PolicyApplyStage::ValidateBase:
        return "validate_base";
    case PolicyApplyStage::LoadBasePolicy:
        return "load_base_policy";
    case PolicyApplyStage::BuildCandidate:
        return "compile";
    case PolicyApplyStage::Canonicalize:
        return "canonicalize";
    case PolicyApplyStage::StoreVersion:
        return "store_version";
    case PolicyApplyStage::CompareAndPromote:
        return "compare_and_promote";
    case PolicyApplyStage::RecordResult:
        return "record_result";
    }
    return "unknown";
}

PolicyApplyTransaction::PolicyApplyTransaction(PolicyApplyTransactionConfig config)
    : config_(std::move(config)) {}

void PolicyApplyTransaction::notify_state_change(const PolicyUpdateJob& job) const {
    if (config_.on_state_change) {
        config_.on_state_change(job);
    }
}

PolicyApplyTransactionResult PolicyApplyTransaction::make_failure(PolicyUpdateJob& job,
                                                                  PolicyApplyStage stage,
                                                                  const std::string& code,
                                                                  const std::string& message,
                                                                  std::uint64_t expected_generation,
                                                                  std::uint64_t actual_generation) {
    job.state = PolicyUpdateJobState::Failed;
    job.updated_at_unix_epoch_ms = now_ms();
    job.failure.stage = to_string(stage);
    job.failure.code = code;
    job.failure.message = message;
    job.failure.expected_generation = expected_generation;
    job.failure.actual_generation = actual_generation;
    notify_state_change(job);

    PolicyApplyTransactionResult result{};
    result.ok = false;
    result.final_state = PolicyUpdateJobState::Failed;
    result.failure_stage = stage;
    result.error_code = code;
    result.error_message = message;
    return result;
}

PolicyApplyTransactionResult PolicyApplyTransaction::execute(PolicyUpdateJob& job) {
    if (config_.policy_state_store == nullptr) {
        return make_failure(job, PolicyApplyStage::LoadActive, "POLICY_APPLY_STORE_UNAVAILABLE",
                            "policy state store is not configured");
    }

    job.state = PolicyUpdateJobState::Processing;
    job.updated_at_unix_epoch_ms = now_ms();
    notify_state_change(job);

    const LoadActivePointerResult active_res =
        config_.policy_state_store->load_active_pointer(config_.resource_key);
    if (!active_res.ok) {
        if (active_res.not_found) {
            return make_failure(job, PolicyApplyStage::LoadActive,
                                "POLICY_APPLY_LOAD_ACTIVE_FAILED",
                                "active policy pointer not found");
        }
        return make_failure(job, PolicyApplyStage::LoadActive, "POLICY_APPLY_LOAD_ACTIVE_FAILED",
                            active_res.error);
    }

    const ActivePolicyPointer& active = active_res.pointer;

    if (active.generation != job.expected_base_generation ||
        active.policy_id != job.expected_base_policy_id) {
        return make_failure(job, PolicyApplyStage::ValidateBase, "POLICY_APPLY_BASE_MISMATCH",
                            "expected base policy does not match active committed policy",
                            job.expected_base_generation, active.generation);
    }

    const LoadPolicyVersionResult base_version =
        config_.policy_state_store->load_policy_version(config_.resource_key, active.generation);
    if (!base_version.ok) {
        return make_failure(job, PolicyApplyStage::LoadBasePolicy, "POLICY_APPLY_LOAD_BASE_FAILED",
                            base_version.error);
    }

    const taperquery::PolicyIrLoadResult base_load = taperquery::load_policy_ir_from_yaml_string(
        base_version.canonical_yaml.data(), base_version.canonical_yaml.size());
    if (!base_load.ok) {
        return make_failure(job, PolicyApplyStage::LoadBasePolicy, "POLICY_APPLY_LOAD_BASE_FAILED",
                            "failed to parse committed base policy: " + base_load.error);
    }

    runtime::RuntimePolicyStore scratch_store;
    auto base_build =
        runtime::build_runtime_policy_snapshot_from_ir(base_load.policy, active.generation);
    if (!base_build.ok || base_build.snapshot == nullptr) {
        return make_failure(job, PolicyApplyStage::LoadBasePolicy, "POLICY_APPLY_LOAD_BASE_FAILED",
                            "failed to build base runtime snapshot: " + base_build.error);
    }

    std::string install_err;
    if (!scratch_store.install_initial(base_build.snapshot, &install_err)) {
        return make_failure(job, PolicyApplyStage::LoadBasePolicy, "POLICY_APPLY_LOAD_BASE_FAILED",
                            install_err);
    }

    taperquery::TqPolicyDocument candidate_ir{};
    const bool yaml_source = job.source_type == "yaml";

    if (yaml_source) {
        const taperquery::PolicyIrLoadResult candidate_load =
            taperquery::load_policy_ir_from_yaml_string(job.apply_request.source.data(),
                                                        job.apply_request.source.size());
        if (!candidate_load.ok) {
            return make_failure(job, PolicyApplyStage::BuildCandidate,
                                "POLICY_APPLY_COMPILE_FAILED", candidate_load.error);
        }
        candidate_ir = taperquery::normalize_policy_ir(candidate_load.policy);
        if (!job.expected_base_policy_id.empty()) {
            candidate_ir.expected_base_sha = job.expected_base_policy_id;
        }

        const auto route_analysis = taperquery::analyze_taperquery_route_precedence(candidate_ir);
        if (!route_analysis.ok) {
            return make_failure(job, PolicyApplyStage::BuildCandidate,
                                "POLICY_APPLY_COMPILE_FAILED", "route precedence analysis failed");
        }

        taperquery::TqApplyPlanOptions plan_opts{};
        plan_opts.strict_production = job.apply_request.strict_production;
        const taperquery::TqApplyPlan plan =
            taperquery::build_taperquery_apply_plan(base_load.policy, candidate_ir, plan_opts);
        if (!plan.ok) {
            return make_failure(job, PolicyApplyStage::BuildCandidate,
                                "POLICY_APPLY_COMPILE_FAILED",
                                "candidate policy plan validation failed");
        }
        for (const auto& issue : plan.issues) {
            if (issue.severity == taperquery::TqPlanSeverity::Blocker) {
                return make_failure(job, PolicyApplyStage::BuildCandidate,
                                    "POLICY_APPLY_COMPILE_FAILED", issue.reason);
            }
        }
    } else {
        taperquery::LocalPolicyPersistenceConfig no_persist{};
        no_persist.enabled = false;
        taperquery::TqApplyService apply_service(&scratch_store, nullptr, nullptr, no_persist);

        taperquery::TqApplyRequest apply_req = job.apply_request;
        apply_req.mode = taperquery::TqApplyMode::Apply;
        apply_req.expected_base_identity = active.policy_id;

        const taperquery::TqApplyResult apply_result = apply_service.execute(apply_req);
        if (apply_result.status != taperquery::TqApplyStatus::Applied) {
            std::string msg =
                apply_result.message.empty() ? "policy apply failed" : apply_result.message;
            if (apply_result.status == taperquery::TqApplyStatus::RejectedCasMismatch) {
                return make_failure(job, PolicyApplyStage::BuildCandidate,
                                    "POLICY_APPLY_BASE_MISMATCH", msg);
            }
            return make_failure(job, PolicyApplyStage::BuildCandidate,
                                "POLICY_APPLY_COMPILE_FAILED", msg);
        }

        const auto active_snapshot = scratch_store.load();
        if (active_snapshot == nullptr) {
            return make_failure(job, PolicyApplyStage::Canonicalize,
                                "POLICY_APPLY_CANONICALIZE_FAILED",
                                "applied snapshot is not available");
        }
        candidate_ir = active_snapshot->policy_ir;
    }

    job.state = PolicyUpdateJobState::CandidateBuilt;
    job.updated_at_unix_epoch_ms = now_ms();
    notify_state_change(job);

    const auto roundtrip = taperquery::emit_and_reparse_canonical_policy_yaml(candidate_ir);
    if (!roundtrip.ok) {
        return make_failure(job, PolicyApplyStage::Canonicalize, "POLICY_APPLY_CANONICALIZE_FAILED",
                            roundtrip.error);
    }

    const std::string candidate_policy_id =
        taperquery::compute_policy_document_identity(roundtrip.parsed_policy_ir);
    const std::string canonical_hash =
        "sha256:" + taperquery::compute_canonical_yaml_sha256_hex(roundtrip.canonical_yaml);
    const std::uint64_t candidate_generation = active.generation + 1;

    job.candidate_generation = candidate_generation;
    job.candidate_policy_id = candidate_policy_id;
    job.candidate_canonical_hash = canonical_hash;

    PolicyVersionRecord version_record;
    version_record.generation = candidate_generation;
    version_record.policy_id = candidate_policy_id;
    version_record.canonical_hash = canonical_hash;
    version_record.schema_version = roundtrip.parsed_policy_ir.schema_version_num;
    version_record.api_version = roundtrip.parsed_policy_ir.api_version;
    version_record.kind = roundtrip.parsed_policy_ir.kind;
    version_record.source_type = job.source_type.empty() ? "taperql-apply" : job.source_type;
    version_record.apply_id = job.job_id;
    version_record.previous_generation = active.generation;
    version_record.previous_policy_id = active.policy_id;
    version_record.created_at_unix_epoch_ms = now_ms();

    const StorePolicyVersionResult store_res = config_.policy_state_store->store_policy_version(
        config_.resource_key, version_record, roundtrip.canonical_yaml);
    if (!store_res.ok) {
        if (store_res.code == PolicyStateErrorCode::VersionConflict) {
            return make_failure(job, PolicyApplyStage::StoreVersion,
                                "POLICY_APPLY_STORE_VERSION_FAILED", store_res.error);
        }
        return make_failure(job, PolicyApplyStage::StoreVersion,
                            "POLICY_APPLY_STORE_VERSION_FAILED", store_res.error);
    }

    job.state = PolicyUpdateJobState::VersionStored;
    job.updated_at_unix_epoch_ms = now_ms();
    notify_state_change(job);

    ActivePolicyPointer next_pointer;
    next_pointer.generation = candidate_generation;
    next_pointer.policy_id = candidate_policy_id;
    next_pointer.canonical_hash = canonical_hash;
    next_pointer.version_key = make_version_key(config_.resource_key, candidate_generation);
    next_pointer.yaml_key = make_yaml_key(config_.resource_key, candidate_generation);
    next_pointer.schema_version = version_record.schema_version;
    next_pointer.api_version = version_record.api_version;
    next_pointer.kind = version_record.kind;
    next_pointer.source_type = version_record.source_type;
    next_pointer.apply_id = job.job_id;
    next_pointer.previous_generation = active.generation;
    next_pointer.previous_policy_id = active.policy_id;
    next_pointer.committed_at_unix_epoch_ms = now_ms();

    ExpectedActivePolicy expected;
    expected.generation = active.generation;
    expected.policy_id = active.policy_id;

    const PromoteActiveResult promote_res = config_.policy_state_store->compare_and_promote_active(
        config_.resource_key, expected, next_pointer);
    if (!promote_res.ok) {
        if (promote_res.code == PolicyStateErrorCode::ActivePointerConflict) {
            return make_failure(job, PolicyApplyStage::CompareAndPromote,
                                "POLICY_APPLY_PROMOTE_CONFLICT", promote_res.error,
                                expected.generation, active.generation);
        }
        return make_failure(job, PolicyApplyStage::CompareAndPromote,
                            "POLICY_APPLY_PROMOTE_CONFLICT", promote_res.error);
    }

    job.state = PolicyUpdateJobState::ActivePromoted;
    job.updated_at_unix_epoch_ms = now_ms();
    notify_state_change(job);

    job.state = PolicyUpdateJobState::Committed;
    job.updated_at_unix_epoch_ms = now_ms();
    notify_state_change(job);

    PolicyApplyTransactionResult result{};
    result.ok = true;
    result.final_state = PolicyUpdateJobState::Committed;
    result.candidate_generation = candidate_generation;
    result.candidate_policy_id = candidate_policy_id;
    result.candidate_canonical_hash = canonical_hash;
    result.idempotent = promote_res.idempotent;
    return result;
}

} // namespace bytetaper::control_plane
