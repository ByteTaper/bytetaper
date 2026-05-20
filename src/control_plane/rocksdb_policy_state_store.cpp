// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/rocksdb_policy_state_store.h"

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/status.h>
#include <rocksdb/write_batch.h>

namespace bytetaper::control_plane {

struct RocksDBPolicyStateStoreImpl {
    std::unique_ptr<rocksdb::DB> db;
    rocksdb::WriteOptions sync_write{};

    RocksDBPolicyStateStoreImpl() {
        sync_write.sync = true;
    }
};

namespace {

StorePolicyVersionResult make_store_error(PolicyStateErrorCode code, const std::string& error) {
    StorePolicyVersionResult res;
    res.ok = false;
    res.code = code;
    res.error = error;
    return res;
}

LoadPolicyVersionResult make_load_version_error(PolicyStateErrorCode code,
                                                const std::string& error) {
    LoadPolicyVersionResult res;
    res.ok = false;
    res.code = code;
    res.error = error;
    return res;
}

LoadActivePointerResult make_load_active_error(PolicyStateErrorCode code, const std::string& error,
                                               bool not_found = false) {
    LoadActivePointerResult res;
    res.ok = false;
    res.not_found = not_found;
    res.code = code;
    res.error = error;
    return res;
}

PromoteActiveResult make_promote_error(PolicyStateErrorCode code, const std::string& error) {
    PromoteActiveResult res;
    res.ok = false;
    res.code = code;
    res.error = error;
    return res;
}

AppendAuditResult make_audit_error(PolicyStateErrorCode code, const std::string& error) {
    AppendAuditResult res;
    res.ok = false;
    res.code = code;
    res.error = error;
    return res;
}

StorePolicyUpdateJobResult make_store_job_error(PolicyStateErrorCode code,
                                                const std::string& error) {
    StorePolicyUpdateJobResult res;
    res.ok = false;
    res.code = code;
    res.error = error;
    return res;
}

LoadPolicyUpdateJobResult make_load_job_error(PolicyStateErrorCode code, const std::string& error,
                                              bool not_found = false) {
    LoadPolicyUpdateJobResult res;
    res.ok = false;
    res.not_found = not_found;
    res.code = code;
    res.error = error;
    return res;
}

} // namespace

RocksDBPolicyStateStore::RocksDBPolicyStateStore(const char* db_path)
    : impl_(std::make_unique<RocksDBPolicyStateStoreImpl>()) {
    if (db_path == nullptr || db_path[0] == '\0') {
        open_error_ = "policy state db path is empty";
        return;
    }

    rocksdb::Options opts;
    opts.create_if_missing = true;

    std::unique_ptr<rocksdb::DB> raw_db;
    const rocksdb::Status status = rocksdb::DB::Open(opts, db_path, &raw_db);
    if (!status.ok()) {
        open_error_ = status.ToString();
        return;
    }

    impl_->db = std::move(raw_db);
    open_ = true;
}

RocksDBPolicyStateStore::~RocksDBPolicyStateStore() = default;

bool RocksDBPolicyStateStore::is_open() const {
    return open_;
}

const std::string& RocksDBPolicyStateStore::open_error() const {
    return open_error_;
}

StorePolicyVersionResult
RocksDBPolicyStateStore::store_policy_version(const PolicyResourceKey& key,
                                              const PolicyVersionRecord& version,
                                              const std::string& canonical_yaml) {
    if (!open_ || impl_->db == nullptr) {
        return make_store_error(PolicyStateErrorCode::DbOpenFailed, "policy state db is not open");
    }

    const std::string version_key = make_version_key(key, version.generation);
    const std::string yaml_key = make_yaml_key(key, version.generation);

    std::string existing_json;
    const rocksdb::Status get_status =
        impl_->db->Get(rocksdb::ReadOptions{}, version_key, &existing_json);
    if (get_status.ok()) {
        PolicyVersionRecord existing;
        if (!deserialize_version_record(existing_json, &existing)) {
            return make_store_error(PolicyStateErrorCode::DbReadFailed,
                                    "existing policy version record is invalid");
        }
        if (existing.canonical_hash != version.canonical_hash) {
            return make_store_error(PolicyStateErrorCode::VersionConflict,
                                    "policy version conflict (VERSION_CONFLICT)");
        }

        std::string existing_yaml;
        const rocksdb::Status yaml_status =
            impl_->db->Get(rocksdb::ReadOptions{}, yaml_key, &existing_yaml);
        if (yaml_status.IsNotFound()) {
            return make_store_error(PolicyStateErrorCode::VersionConflict,
                                    "policy version yaml missing for existing generation");
        }
        if (!yaml_status.ok()) {
            return make_store_error(PolicyStateErrorCode::DbReadFailed, yaml_status.ToString());
        }
        if (existing_yaml != canonical_yaml) {
            return make_store_error(PolicyStateErrorCode::VersionConflict,
                                    "policy version yaml content conflict (VERSION_CONFLICT)");
        }

        StorePolicyVersionResult res;
        res.ok = true;
        res.code = PolicyStateErrorCode::Ok;
        return res;
    }
    if (!get_status.IsNotFound()) {
        return make_store_error(PolicyStateErrorCode::DbReadFailed, get_status.ToString());
    }

    PolicyVersionRecord to_store = version;
    to_store.resource_key = key.to_string();
    to_store.yaml_key = yaml_key;

    rocksdb::WriteBatch batch;
    batch.Put(version_key, serialize_version_record(to_store));
    batch.Put(yaml_key, canonical_yaml);

    const rocksdb::Status write_status = impl_->db->Write(impl_->sync_write, &batch);
    if (!write_status.ok()) {
        return make_store_error(PolicyStateErrorCode::DbWriteFailed, write_status.ToString());
    }

    StorePolicyVersionResult res;
    res.ok = true;
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

LoadPolicyVersionResult RocksDBPolicyStateStore::load_policy_version(const PolicyResourceKey& key,
                                                                     std::uint64_t generation) {
    if (!open_ || impl_->db == nullptr) {
        return make_load_version_error(PolicyStateErrorCode::DbOpenFailed,
                                       "policy state db is not open");
    }

    const std::string version_key = make_version_key(key, generation);
    const std::string yaml_key = make_yaml_key(key, generation);

    std::string version_json;
    const rocksdb::Status version_status =
        impl_->db->Get(rocksdb::ReadOptions{}, version_key, &version_json);
    if (version_status.IsNotFound()) {
        return make_load_version_error(PolicyStateErrorCode::VersionNotFound,
                                       "policy version not found (VERSION_NOT_FOUND)");
    }
    if (!version_status.ok()) {
        return make_load_version_error(PolicyStateErrorCode::DbReadFailed,
                                       version_status.ToString());
    }

    PolicyVersionRecord record;
    if (!deserialize_version_record(version_json, &record)) {
        return make_load_version_error(PolicyStateErrorCode::DbReadFailed,
                                       "policy version record is invalid");
    }

    std::string canonical_yaml;
    const rocksdb::Status yaml_status =
        impl_->db->Get(rocksdb::ReadOptions{}, yaml_key, &canonical_yaml);
    if (yaml_status.IsNotFound()) {
        return make_load_version_error(PolicyStateErrorCode::VersionNotFound,
                                       "policy version yaml not found (VERSION_NOT_FOUND)");
    }
    if (!yaml_status.ok()) {
        return make_load_version_error(PolicyStateErrorCode::DbReadFailed, yaml_status.ToString());
    }

    LoadPolicyVersionResult res;
    res.ok = true;
    res.record = std::move(record);
    res.canonical_yaml = std::move(canonical_yaml);
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

LoadActivePointerResult RocksDBPolicyStateStore::load_active_pointer(const PolicyResourceKey& key) {
    if (!open_ || impl_->db == nullptr) {
        return make_load_active_error(PolicyStateErrorCode::DbOpenFailed,
                                      "policy state db is not open");
    }

    const std::string active_key = make_active_key(key);
    std::string active_json;
    const rocksdb::Status status = impl_->db->Get(rocksdb::ReadOptions{}, active_key, &active_json);
    if (status.IsNotFound()) {
        return make_load_active_error(PolicyStateErrorCode::ActivePointerNotFound,
                                      "active policy pointer not found (ACTIVE_POINTER_NOT_FOUND)",
                                      true);
    }
    if (!status.ok()) {
        return make_load_active_error(PolicyStateErrorCode::DbReadFailed, status.ToString());
    }

    ActivePolicyPointer pointer;
    if (!deserialize_active_pointer(active_json, &pointer)) {
        return make_load_active_error(PolicyStateErrorCode::ActivePointerInvalid,
                                      "active policy pointer is invalid (ACTIVE_POINTER_INVALID)");
    }

    LoadActivePointerResult res;
    res.ok = true;
    res.pointer = std::move(pointer);
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

PromoteActiveResult
RocksDBPolicyStateStore::compare_and_promote_active(const PolicyResourceKey& key,
                                                    const ExpectedActivePolicy& expected,
                                                    const ActivePolicyPointer& next) {
    if (!open_ || impl_->db == nullptr) {
        return make_promote_error(PolicyStateErrorCode::DbOpenFailed,
                                  "policy state db is not open");
    }

    const std::string active_key = make_active_key(key);
    std::string current_json;
    const rocksdb::Status get_status =
        impl_->db->Get(rocksdb::ReadOptions{}, active_key, &current_json);

    ActivePolicyPointer current;
    const bool has_current = get_status.ok();
    if (has_current) {
        if (!deserialize_active_pointer(current_json, &current)) {
            return make_promote_error(PolicyStateErrorCode::ActivePointerInvalid,
                                      "active policy pointer is invalid (ACTIVE_POINTER_INVALID)");
        }
        if (current.generation == next.generation && current.policy_id == next.policy_id) {
            PromoteActiveResult res;
            res.ok = true;
            res.idempotent = true;
            res.code = PolicyStateErrorCode::Ok;
            return res;
        }
        if (current.generation != expected.generation || current.policy_id != expected.policy_id) {
            return make_promote_error(PolicyStateErrorCode::ActivePointerConflict,
                                      "active policy pointer conflict (ACTIVE_POINTER_CONFLICT)");
        }
    } else if (!get_status.IsNotFound()) {
        return make_promote_error(PolicyStateErrorCode::DbReadFailed, get_status.ToString());
    } else if (expected.generation != 0) {
        return make_promote_error(PolicyStateErrorCode::ActivePointerConflict,
                                  "active policy pointer conflict (ACTIVE_POINTER_CONFLICT)");
    }

    if (next.version_key.empty()) {
        return make_promote_error(PolicyStateErrorCode::ActivePointerTargetMissing,
                                  "active pointer version key is empty");
    }

    std::string target_json;
    const rocksdb::Status target_status =
        impl_->db->Get(rocksdb::ReadOptions{}, next.version_key, &target_json);
    if (target_status.IsNotFound()) {
        return make_promote_error(
            PolicyStateErrorCode::ActivePointerTargetMissing,
            "active pointer target version missing (ACTIVE_POINTER_TARGET_MISSING)");
    }
    if (!target_status.ok()) {
        return make_promote_error(PolicyStateErrorCode::DbReadFailed, target_status.ToString());
    }

    ActivePolicyPointer to_store = next;
    to_store.resource_key = key.to_string();

    rocksdb::WriteBatch batch;
    batch.Put(active_key, serialize_active_pointer(to_store));

    const rocksdb::Status write_status = impl_->db->Write(impl_->sync_write, &batch);
    if (!write_status.ok()) {
        return make_promote_error(PolicyStateErrorCode::DbWriteFailed, write_status.ToString());
    }

    PromoteActiveResult res;
    res.ok = true;
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

CommitPolicyGenerationWithAuditResult RocksDBPolicyStateStore::commit_policy_generation_with_audit(
    const CommitPolicyGenerationWithAuditParams& params) {
    if (!open_ || impl_->db == nullptr) {
        CommitPolicyGenerationWithAuditResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::DbOpenFailed;
        res.error = "policy state db is not open";
        return res;
    }

    if (params.audit.apply_id.empty()) {
        CommitPolicyGenerationWithAuditResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::AuditWriteFailed;
        res.error = "audit apply_id is empty";
        return res;
    }

    const PolicyResourceKey& key = params.resource_key;
    const std::string version_key = make_version_key(key, params.version.generation);
    const std::string yaml_key = make_yaml_key(key, params.version.generation);
    const std::string active_key = make_active_key(key);

    std::string existing_version_json;
    const rocksdb::Status version_get_status =
        impl_->db->Get(rocksdb::ReadOptions{}, version_key, &existing_version_json);
    bool write_version = false;
    if (version_get_status.ok()) {
        PolicyVersionRecord existing;
        if (!deserialize_version_record(existing_version_json, &existing)) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::DbReadFailed;
            res.error = "existing policy version record is invalid";
            return res;
        }
        if (existing.canonical_hash != params.version.canonical_hash) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::VersionConflict;
            res.error = "policy version conflict (VERSION_CONFLICT)";
            return res;
        }

        std::string existing_yaml;
        const rocksdb::Status yaml_status =
            impl_->db->Get(rocksdb::ReadOptions{}, yaml_key, &existing_yaml);
        if (yaml_status.IsNotFound()) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::VersionConflict;
            res.error = "policy version yaml missing for existing generation";
            return res;
        }
        if (!yaml_status.ok()) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::DbReadFailed;
            res.error = yaml_status.ToString();
            return res;
        }
        if (existing_yaml != params.canonical_yaml) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::VersionConflict;
            res.error = "policy version yaml content conflict (VERSION_CONFLICT)";
            return res;
        }
    } else if (!version_get_status.IsNotFound()) {
        CommitPolicyGenerationWithAuditResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::DbReadFailed;
        res.error = version_get_status.ToString();
        return res;
    } else {
        write_version = true;
    }

    std::string current_json;
    const rocksdb::Status active_get_status =
        impl_->db->Get(rocksdb::ReadOptions{}, active_key, &current_json);

    ActivePolicyPointer current;
    const bool has_current = active_get_status.ok();
    if (has_current) {
        if (!deserialize_active_pointer(current_json, &current)) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::ActivePointerInvalid;
            res.error = "active policy pointer is invalid (ACTIVE_POINTER_INVALID)";
            return res;
        }
        if (current.generation == params.next_pointer.generation &&
            current.policy_id == params.next_pointer.policy_id) {
            PolicyAuditRecord audit_to_store = params.audit;
            audit_to_store.resource_key = key.to_string();
            const std::string audit_key = make_audit_key(key, params.audit.apply_id);

            rocksdb::WriteBatch batch;
            batch.Put(audit_key, serialize_audit_record(audit_to_store));
            const rocksdb::Status write_status = impl_->db->Write(impl_->sync_write, &batch);
            if (!write_status.ok()) {
                CommitPolicyGenerationWithAuditResult res;
                res.ok = false;
                res.code = PolicyStateErrorCode::DbWriteFailed;
                res.error = write_status.ToString();
                return res;
            }

            CommitPolicyGenerationWithAuditResult res;
            res.ok = true;
            res.code = PolicyStateErrorCode::Ok;
            return res;
        }
        if (current.generation != params.expected_active.generation ||
            current.policy_id != params.expected_active.policy_id) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::ActivePointerConflict;
            res.error = "active policy pointer conflict (ACTIVE_POINTER_CONFLICT)";
            return res;
        }
    } else if (!active_get_status.IsNotFound()) {
        CommitPolicyGenerationWithAuditResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::DbReadFailed;
        res.error = active_get_status.ToString();
        return res;
    } else if (params.expected_active.generation != 0) {
        CommitPolicyGenerationWithAuditResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::ActivePointerConflict;
        res.error = "active policy pointer conflict (ACTIVE_POINTER_CONFLICT)";
        return res;
    }

    if (params.next_pointer.version_key.empty()) {
        CommitPolicyGenerationWithAuditResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::ActivePointerTargetMissing;
        res.error = "active pointer version key is empty";
        return res;
    }

    if (!write_version) {
        std::string target_json;
        const rocksdb::Status target_status =
            impl_->db->Get(rocksdb::ReadOptions{}, params.next_pointer.version_key, &target_json);
        if (target_status.IsNotFound()) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::ActivePointerTargetMissing;
            res.error = "active pointer target version missing (ACTIVE_POINTER_TARGET_MISSING)";
            return res;
        }
        if (!target_status.ok()) {
            CommitPolicyGenerationWithAuditResult res;
            res.ok = false;
            res.code = PolicyStateErrorCode::DbReadFailed;
            res.error = target_status.ToString();
            return res;
        }
    }

    PolicyVersionRecord version_to_store = params.version;
    version_to_store.resource_key = key.to_string();
    version_to_store.yaml_key = yaml_key;

    ActivePolicyPointer active_to_store = params.next_pointer;
    active_to_store.resource_key = key.to_string();

    PolicyAuditRecord audit_to_store = params.audit;
    audit_to_store.resource_key = key.to_string();
    const std::string audit_key = make_audit_key(key, params.audit.apply_id);

    rocksdb::WriteBatch batch;
    if (write_version) {
        batch.Put(version_key, serialize_version_record(version_to_store));
        batch.Put(yaml_key, params.canonical_yaml);
    }
    batch.Put(active_key, serialize_active_pointer(active_to_store));
    batch.Put(audit_key, serialize_audit_record(audit_to_store));

    const rocksdb::Status write_status = impl_->db->Write(impl_->sync_write, &batch);
    if (!write_status.ok()) {
        CommitPolicyGenerationWithAuditResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::DbWriteFailed;
        res.error = write_status.ToString();
        return res;
    }

    CommitPolicyGenerationWithAuditResult res;
    res.ok = true;
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

AppendAuditResult RocksDBPolicyStateStore::append_audit_record(const PolicyResourceKey& key,
                                                               const PolicyAuditRecord& record) {
    if (!open_ || impl_->db == nullptr) {
        return make_audit_error(PolicyStateErrorCode::DbOpenFailed, "policy state db is not open");
    }

    if (record.apply_id.empty()) {
        return make_audit_error(PolicyStateErrorCode::AuditWriteFailed, "audit apply_id is empty");
    }

    PolicyAuditRecord to_store = record;
    to_store.resource_key = key.to_string();

    const std::string audit_key = make_audit_key(key, record.apply_id);
    const rocksdb::Status status =
        impl_->db->Put(impl_->sync_write, audit_key, serialize_audit_record(to_store));
    if (!status.ok()) {
        return make_audit_error(PolicyStateErrorCode::AuditWriteFailed, status.ToString());
    }

    AppendAuditResult res;
    res.ok = true;
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

StorePolicyUpdateJobResult
RocksDBPolicyStateStore::store_policy_update_job(const PolicyResourceKey& key,
                                                 const PolicyUpdateJobRecord& job) {
    if (!open_ || impl_->db == nullptr) {
        return make_store_job_error(PolicyStateErrorCode::DbOpenFailed,
                                    "policy state db is not open");
    }

    if (job.job_id.empty()) {
        return make_store_job_error(PolicyStateErrorCode::JobWriteFailed, "job id is empty");
    }

    PolicyUpdateJobRecord to_store = job;
    to_store.resource_key = key.to_string();

    const std::string job_key = make_job_key(key, job.job_id);
    const rocksdb::Status status =
        impl_->db->Put(impl_->sync_write, job_key, serialize_policy_update_job_record(to_store));
    if (!status.ok()) {
        return make_store_job_error(PolicyStateErrorCode::JobWriteFailed, status.ToString());
    }

    StorePolicyUpdateJobResult res;
    res.ok = true;
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

LoadPolicyUpdateJobResult
RocksDBPolicyStateStore::load_policy_update_job(const PolicyResourceKey& key,
                                                const std::string& job_id) {
    if (!open_ || impl_->db == nullptr) {
        return make_load_job_error(PolicyStateErrorCode::DbOpenFailed,
                                   "policy state db is not open");
    }

    if (job_id.empty()) {
        return make_load_job_error(PolicyStateErrorCode::JobNotFound, "job id is empty", true);
    }

    const std::string job_key = make_job_key(key, job_id);
    std::string json;
    const rocksdb::Status status = impl_->db->Get(rocksdb::ReadOptions{}, job_key, &json);
    if (status.IsNotFound()) {
        return make_load_job_error(PolicyStateErrorCode::JobNotFound, "policy update job not found",
                                   true);
    }
    if (!status.ok()) {
        return make_load_job_error(PolicyStateErrorCode::DbReadFailed, status.ToString());
    }

    LoadPolicyUpdateJobResult res;
    if (!deserialize_policy_update_job_record(json, &res.record)) {
        return make_load_job_error(PolicyStateErrorCode::DbReadFailed,
                                   "policy update job record is invalid");
    }

    res.ok = true;
    res.code = PolicyStateErrorCode::Ok;
    return res;
}

} // namespace bytetaper::control_plane
