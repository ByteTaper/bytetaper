// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/committed_policy_mirror_sync.h"

#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_persistence.h"

#include <chrono>
#include <filesystem>

namespace bytetaper::runtime_policy {

namespace {

std::int64_t now_unix_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

CommittedPolicyMirrorSyncResult sync_local_mirror_from_committed(
    ControlPlanePolicyClient* client, const control_plane::PolicyResourceKey& resource_key,
    const taperquery::LocalPolicyPersistenceConfig& mirror_config, std::uint64_t generation) {
    CommittedPolicyMirrorSyncResult result{};

    if (client == nullptr) {
        result.error = "control plane client is not configured";
        result.error_code = "CONTROL_PLANE_UNAVAILABLE";
        return result;
    }
    if (!mirror_config.enabled || mirror_config.state_dir.empty()) {
        result.error = "local mirror persistence is not enabled";
        result.error_code = "LOCAL_MIRROR_DISABLED";
        return result;
    }

    {
        std::error_code ec;
        const std::filesystem::path versions_dir =
            std::filesystem::path(mirror_config.state_dir) / "versions";
        std::filesystem::remove_all(versions_dir, ec);
    }

    const ActivePolicyPointerResult active_res = client->get_active_policy(resource_key);
    if (!active_res.ok) {
        result.error =
            active_res.error.empty() ? "failed to load active policy pointer" : active_res.error;
        result.error_code = active_res.error_code;
        return result;
    }

    const std::uint64_t target_generation =
        generation != 0 ? generation : active_res.active.generation;
    const CanonicalPolicyResult version_res =
        client->get_policy_version(resource_key, target_generation);
    if (!version_res.ok) {
        result.error = version_res.error.empty() ? "failed to load committed policy version"
                                                 : version_res.error;
        result.error_code = version_res.error_code;
        return result;
    }

    const taperquery::PolicyIrLoadResult load_res = taperquery::load_policy_ir_from_yaml_string(
        version_res.canonical_yaml.data(), version_res.canonical_yaml.size());
    if (!load_res.ok) {
        result.error =
            load_res.error.empty() ? "failed to parse committed policy yaml" : load_res.error;
        result.error_code = "POLICY_PARSE_FAILED";
        return result;
    }

    taperquery::PersistedPolicyMetadata metadata{};
    metadata.policy_identity = version_res.record.policy_id;
    metadata.generation = version_res.record.generation;
    metadata.canonical_hash = version_res.record.canonical_hash;
    if (metadata.canonical_hash.rfind("sha256:", 0) == 0) {
        metadata.canonical_yaml_sha256 = metadata.canonical_hash.substr(7);
    } else {
        metadata.canonical_yaml_sha256 = metadata.canonical_hash;
    }
    metadata.canonical_hash_algorithm = "sha256";
    metadata.resource_key = resource_key.to_string();
    metadata.metadata_schema_version = 1;
    metadata.schema_version = version_res.record.schema_version;
    metadata.api_version = version_res.record.api_version;
    metadata.kind = version_res.record.kind;
    metadata.source_type =
        version_res.record.source_type.empty() ? "manual-repair" : version_res.record.source_type;
    metadata.active_policy_file = mirror_config.active_policy_filename.empty()
                                      ? "active-policy.yaml"
                                      : mirror_config.active_policy_filename;
    metadata.committed_at_unix_epoch_ms = version_res.record.created_at_unix_epoch_ms;
    metadata.written_at_unix_epoch_ms = static_cast<std::uint64_t>(now_unix_epoch_ms());

    const taperquery::PolicyPersistenceWriteResult persist_res =
        taperquery::persist_active_policy_canonical_yaml(mirror_config, load_res.policy, metadata);
    if (!persist_res.ok) {
        result.error = persist_res.error;
        result.error_code = "LOCAL_MIRROR_WRITE_FAILED";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace bytetaper::runtime_policy
