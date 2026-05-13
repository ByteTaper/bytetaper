// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_TQ_APPLY_AUDIT_H
#define BYTETAPER_TAPERQUERY_TQ_APPLY_AUDIT_H

#include "taperquery/tq_apply_service.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

enum class TqApplyAuditOutcome : std::uint8_t {
    Applied,
    DryRun,
    Rejected,
    Failed,
};

struct TqApplyAuditRouteSummary {
    std::string route_id;
    std::string change_kind; // "Added" | "Removed" | "Modified" | "Unchanged"
    std::string before_identity;
    std::string after_identity;
    std::uint32_t field_change_count = 0;
};

struct TqApplyAuditIssueSummary {
    std::string severity;
    std::string code;
    std::string route_id;
    std::string reason;
};

struct TqApplyAuditRecord {
    std::uint64_t sequence = 0;
    std::uint64_t unix_epoch_ms = 0;
    TqApplyAuditOutcome outcome = TqApplyAuditOutcome::Failed;

    std::string request_id;
    std::string operator_id;
    std::string mode;
    std::string status;
    std::string message;

    std::string previous_policy_identity;
    std::string expected_base_identity;
    std::string candidate_policy_identity;
    std::string applied_policy_identity;

    std::uint64_t before_generation = 0;
    std::uint64_t after_generation = 0;

    std::uint32_t added_routes = 0;
    std::uint32_t removed_routes = 0;
    std::uint32_t modified_routes = 0;
    std::uint32_t unchanged_routes = 0;
    std::uint32_t issue_count = 0;

    std::vector<TqApplyAuditRouteSummary> route_changes;
    std::vector<TqApplyAuditIssueSummary> issues;
};

struct TqApplyAuditStoreOptions {
    std::size_t capacity = 128;
    bool record_rejected_attempts = true;
    bool record_dry_run_attempts = false; // off by default: dry-run can flood ring
};

class TqApplyAuditStore {
public:
    explicit TqApplyAuditStore(TqApplyAuditStoreOptions options = {});

    std::uint64_t append(TqApplyAuditRecord record); // assigns sequence, returns it
    bool latest(TqApplyAuditRecord* out) const;      // false if empty
    std::vector<TqApplyAuditRecord> recent(std::size_t limit) const;
    std::size_t size() const;
    std::size_t capacity() const;

    const TqApplyAuditStoreOptions& options() const {
        return options_;
    }

private:
    TqApplyAuditStoreOptions options_;
    mutable std::mutex mu_;
    std::deque<TqApplyAuditRecord> deque_;
    std::uint64_t sequence_ = 0;
};

struct TqCurrentPolicySummary {
    bool ok = false;
    std::string error_code;
    std::string message;

    std::string policy_identity;
    std::uint64_t generation = 0;
    std::uint32_t route_count = 0;
    std::string source_name;
    std::string source_schema_version;
    std::string policy_ir_version;
    std::string identity_version;
    std::string emitter_version;
    std::string runtime_min_version;
    std::string runtime_capability_profile;

    bool has_latest_apply = false;
    TqApplyAuditRecord latest_apply;
};

TqApplyAuditRecord build_apply_audit_record(const TqApplyRequest& request,
                                            const TqApplyResult& result);

std::string outcome_to_string(TqApplyAuditOutcome outcome);
std::string to_json(const TqApplyAuditRouteSummary& summary);
std::string to_json(const TqApplyAuditIssueSummary& summary);
std::string to_json(const TqApplyAuditRecord& record);
std::string to_json_compact(const TqApplyAuditRecord& record);
std::string to_json(const TqCurrentPolicySummary& summary);

bool should_record(const TqApplyResult& result, const TqApplyAuditStoreOptions& options);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_TQ_APPLY_AUDIT_H
