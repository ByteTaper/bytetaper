// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_ROCKSDB_POLICY_STATE_STORE_H
#define BYTETAPER_CONTROL_PLANE_ROCKSDB_POLICY_STATE_STORE_H

#include "control_plane/policy_state_store.h"

#include <memory>
#include <string>

namespace bytetaper::control_plane {

struct RocksDBPolicyStateStoreImpl;

class RocksDBPolicyStateStore : public PolicyStateStore {
public:
    explicit RocksDBPolicyStateStore(const char* db_path);
    ~RocksDBPolicyStateStore() override;

    bool is_open() const;
    const std::string& open_error() const;

    StorePolicyVersionResult store_policy_version(const PolicyResourceKey& key,
                                                  const PolicyVersionRecord& version,
                                                  const std::string& canonical_yaml) override;

    LoadPolicyVersionResult load_policy_version(const PolicyResourceKey& key,
                                                std::uint64_t generation) override;

    LoadActivePointerResult load_active_pointer(const PolicyResourceKey& key) override;

    PromoteActiveResult compare_and_promote_active(const PolicyResourceKey& key,
                                                   const ExpectedActivePolicy& expected,
                                                   const ActivePolicyPointer& next) override;

    AppendAuditResult append_audit_record(const PolicyResourceKey& key,
                                          const PolicyAuditRecord& record) override;

    StorePolicyUpdateJobResult store_policy_update_job(const PolicyResourceKey& key,
                                                       const PolicyUpdateJobRecord& job) override;

    LoadPolicyUpdateJobResult load_policy_update_job(const PolicyResourceKey& key,
                                                     const std::string& job_id) override;

private:
    std::unique_ptr<RocksDBPolicyStateStoreImpl> impl_;
    std::string open_error_;
    bool open_ = false;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_ROCKSDB_POLICY_STATE_STORE_H
