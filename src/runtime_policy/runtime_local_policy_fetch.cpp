// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_local_policy_fetch.h"

#include "control_plane/manual_resolution_api.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

namespace bytetaper::runtime_policy {

namespace {

std::string normalize_hash(const std::string& hash) {
    if (hash.rfind("sha256:", 0) == 0) {
        return hash;
    }
    if (hash.empty()) {
        return hash;
    }
    return "sha256:" + hash;
}

} // namespace

control_plane::AdoptLocalFetchedPolicy
fetch_local_policy_from_mirror(const taperquery::LocalPolicyPersistenceConfig& mirror_config,
                               const control_plane::RuntimeStatusReport& runtime_report) {
    control_plane::AdoptLocalFetchedPolicy result{};

    if (!mirror_config.enabled || mirror_config.state_dir.empty()) {
        result.error = "runtime local mirror persistence is not enabled";
        result.error_code = control_plane::kErrAdoptLocalPolicyUnavailable;
        return result;
    }

    const taperquery::PolicyPersistenceLoadResult load_res =
        taperquery::load_persisted_active_policy(mirror_config);
    if (!load_res.ok) {
        result.error =
            load_res.error.empty() ? "runtime local policy is not available" : load_res.error;
        result.error_code = control_plane::kErrAdoptLocalPolicyUnavailable;
        return result;
    }

    const taperquery::PolicyIrYamlEmitResult emit =
        taperquery::emit_policy_ir_canonical_yaml(load_res.document);
    if (!emit.ok) {
        result.error =
            emit.error.empty() ? "failed to canonicalize runtime local policy" : emit.error;
        result.error_code = control_plane::kErrAdoptLocalPolicyInvalid;
        return result;
    }

    result.canonical_yaml = emit.yaml;
    result.local_generation = load_res.metadata.generation != 0 ? load_res.metadata.generation
                                                                : runtime_report.active_generation;
    result.local_policy_id = load_res.metadata.policy_identity;
    result.local_canonical_hash = normalize_hash(load_res.metadata.canonical_hash);
    if (result.local_canonical_hash.empty()) {
        result.local_canonical_hash =
            "sha256:" + taperquery::compute_canonical_yaml_sha256_hex(emit.yaml);
    }

    if (!runtime_report.active_policy_id.empty() &&
        runtime_report.active_policy_id != result.local_policy_id) {
        result.error = "runtime status activePolicyId does not match local mirror policy";
        result.error_code = control_plane::kErrAdoptLocalPolicyInvalid;
        return result;
    }

    if (!runtime_report.active_canonical_hash.empty()) {
        const std::string normalized_report_hash =
            normalize_hash(runtime_report.active_canonical_hash);
        if (normalized_report_hash != result.local_canonical_hash) {
            result.error = "runtime status activeCanonicalHash does not match local mirror policy";
            result.error_code = control_plane::kErrAdoptLocalPolicyInvalid;
            return result;
        }
    }

    result.ok = true;
    return result;
}

} // namespace bytetaper::runtime_policy
