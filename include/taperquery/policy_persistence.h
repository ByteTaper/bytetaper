// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_PERSISTENCE_H
#define BYTETAPER_TAPERQUERY_POLICY_PERSISTENCE_H

#include "taperquery/policy_ir.h"

#include <cstdint>
#include <string>

namespace bytetaper::taperquery {

struct LocalPolicyPersistenceConfig {
    bool enabled = true;
    std::string state_dir;
    std::string active_policy_filename = "active-policy.yaml";
    std::string metadata_filename = "active-policy.meta.json";
};

struct BootstrapMetadata {
    std::string file;
    std::string role; // "initial-default-only" | "explicit-import-only"
    bool overwrite_protection = true;
};

struct CompatibilityMetadata {
    std::string policy_ir_version;
    std::string identity_version;
    std::string emitter_version;
    std::string runtime_min_version;
    std::string runtime_capability_profile;
};

enum class MetadataValidationError {
    Ok,
    Missing,
    Unparseable,
    SchemaUnsupported,
    RequiredFieldMissing,
    PathInvalid,
    CanonicalHashMismatch,
    PolicyIdMismatch,
    GenerationMismatch,
    KindUnsupported,
    ApiVersionUnsupported,
};

enum class VersionedPolicyError {
    Ok,
    Missing,
    HashMismatch,
    Conflict,
    WriteFailed,
    RenameFailed,
    DirectoryCreateFailed,
    ActiveMirrorHashMismatch,
};

struct PersistedPolicyMetadata {
    // === Existing fields (unchanged) ===
    std::string policy_identity;
    std::string candidate_policy_identity;
    std::string persisted_policy_identity;
    std::string previous_policy_identity;
    std::string expected_base_identity;
    std::uint64_t generation = 0;
    std::string source_type;
    std::uint64_t written_at_unix_epoch_ms = 0;
    std::string operator_id;
    std::string request_id;
    std::string canonical_yaml_sha256;

    // === New fields ===
    std::uint32_t metadata_schema_version = 0; // 0 = legacy/unset; 1 = extended
    std::string resource_key;
    std::string apply_id;
    std::uint64_t previous_generation = 0;
    std::string previous_policy_id;
    std::uint32_t schema_version = 0;
    std::string api_version;
    std::string kind;
    std::string active_policy_file;
    std::string versioned_policy_file;
    std::uint64_t committed_at_unix_epoch_ms = 0;
    std::string canonical_hash;
    std::string canonical_hash_algorithm;

    BootstrapMetadata bootstrap{};
    CompatibilityMetadata compatibility{};
};

struct PolicyPersistenceWriteResult {
    bool ok = false;
    std::string active_policy_path;
    std::string metadata_path;
    std::string error;
};

struct PolicyPersistenceLoadResult {
    bool ok = false;
    TqPolicyDocument document;
    PersistedPolicyMetadata metadata;
    std::string error;
    bool files_missing = false;
};

PolicyPersistenceWriteResult
persist_active_policy_canonical_yaml(const LocalPolicyPersistenceConfig& config,
                                     const TqPolicyDocument& document,
                                     const PersistedPolicyMetadata& metadata);

PolicyPersistenceLoadResult
load_persisted_active_policy(const LocalPolicyPersistenceConfig& config);

struct StartupPolicyLoadConfig {
    std::string bootstrap_policy_file;
    std::string policy_state_dir;
    std::string active_policy_filename = "active-policy.yaml";
    std::string metadata_filename = "active-policy.meta.json";
    bool policy_persistence_enabled = true;
    bool fallback_to_bootstrap_on_persisted_policy_error = false;
};

struct StartupPolicyLoadResult {
    bool ok = false;
    std::string loaded_source; // "persisted" | "bootstrap" | ""
    std::string policy_identity;
    std::uint64_t generation = 0;
    TqPolicyDocument policy_ir;
    std::string error;
};

StartupPolicyLoadResult load_startup_policy_with_persistence(const StartupPolicyLoadConfig& config);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_PERSISTENCE_H
