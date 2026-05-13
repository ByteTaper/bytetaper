// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_apply_audit.h"

#include <chrono>
#include <cstdio>

namespace bytetaper::taperquery {

namespace {

static std::string escape_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else if (static_cast<unsigned char>(c) < 32) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<int>(c));
            out += hex;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static std::string status_to_string(TqApplyStatus s) {
    switch (s) {
    case TqApplyStatus::Applied:
        return "Applied";
    case TqApplyStatus::DryRunReady:
        return "DryRunReady";
    case TqApplyStatus::RejectedInvalidRequest:
        return "RejectedInvalidRequest";
    case TqApplyStatus::RejectedParseError:
        return "RejectedParseError";
    case TqApplyStatus::RejectedCompileError:
        return "RejectedCompileError";
    case TqApplyStatus::RejectedValidation:
        return "RejectedValidation";
    case TqApplyStatus::RejectedRouteAnalysis:
        return "RejectedRouteAnalysis";
    case TqApplyStatus::RejectedCasMismatch:
        return "RejectedCasMismatch";
    case TqApplyStatus::RejectedNoChanges:
        return "RejectedNoChanges";
    case TqApplyStatus::RejectedSnapshotBuildFailed:
        return "RejectedSnapshotBuildFailed";
    case TqApplyStatus::InternalError:
        return "InternalError";
    }
    return "Unknown";
}

} // namespace

TqApplyAuditStore::TqApplyAuditStore(TqApplyAuditStoreOptions options) : options_(options) {}

std::uint64_t TqApplyAuditStore::append(TqApplyAuditRecord record) {
    std::lock_guard<std::mutex> lock(mu_);
    record.sequence = ++sequence_;
    if (deque_.size() >= options_.capacity) {
        deque_.pop_front();
    }
    deque_.push_back(std::move(record));
    return sequence_;
}

bool TqApplyAuditStore::latest(TqApplyAuditRecord* out) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (deque_.empty()) {
        return false;
    }
    if (out != nullptr) {
        *out = deque_.back();
    }
    return true;
}

std::vector<TqApplyAuditRecord> TqApplyAuditStore::recent(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<TqApplyAuditRecord> res;
    std::size_t n = std::min(limit, deque_.size());
    if (n == 0) {
        return res;
    }
    res.reserve(n);
    std::size_t start_idx = deque_.size() - n;
    for (std::size_t i = start_idx; i < deque_.size(); ++i) {
        res.push_back(deque_[i]);
    }
    return res;
}

std::size_t TqApplyAuditStore::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return deque_.size();
}

std::size_t TqApplyAuditStore::capacity() const {
    return options_.capacity;
}

TqApplyAuditRecord build_apply_audit_record(const TqApplyRequest& request,
                                            const TqApplyResult& result) {
    TqApplyAuditRecord rec;

    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    rec.unix_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    if (result.status == TqApplyStatus::Applied) {
        rec.outcome = TqApplyAuditOutcome::Applied;
    } else if (result.status == TqApplyStatus::DryRunReady) {
        rec.outcome = TqApplyAuditOutcome::DryRun;
    } else if (result.status == TqApplyStatus::RejectedInvalidRequest ||
               result.status == TqApplyStatus::RejectedParseError ||
               result.status == TqApplyStatus::RejectedCompileError ||
               result.status == TqApplyStatus::RejectedValidation ||
               result.status == TqApplyStatus::RejectedRouteAnalysis ||
               result.status == TqApplyStatus::RejectedCasMismatch ||
               result.status == TqApplyStatus::RejectedNoChanges ||
               result.status == TqApplyStatus::RejectedSnapshotBuildFailed) {
        rec.outcome = TqApplyAuditOutcome::Rejected;
    } else {
        rec.outcome = TqApplyAuditOutcome::Failed;
    }

    rec.request_id = request.request_id;
    rec.operator_id = request.operator_id;
    rec.mode = (request.mode == TqApplyMode::Apply) ? "apply" : "dry_run";
    rec.status = status_to_string(result.status);
    rec.message = result.message;

    rec.previous_policy_identity = result.current_policy_identity;
    rec.expected_base_identity = result.expected_base_identity;
    rec.candidate_policy_identity = result.candidate_policy_identity;
    rec.applied_policy_identity = result.applied_policy_identity;

    rec.before_generation = result.before_generation;
    rec.after_generation = result.after_generation;

    rec.added_routes = 0;
    rec.removed_routes = 0;
    rec.modified_routes = 0;
    rec.unchanged_routes = 0;

    for (const auto& rc : result.plan.route_changes) {
        TqApplyAuditRouteSummary rs;
        rs.route_id = rc.route_id;
        rs.before_identity = rc.before_identity;
        rs.after_identity = rc.after_identity;
        rs.field_change_count = rc.field_changes.size();

        if (rc.kind == TqRouteChangeKind::Added) {
            rs.change_kind = "Added";
            rec.added_routes++;
        } else if (rc.kind == TqRouteChangeKind::Removed) {
            rs.change_kind = "Removed";
            rec.removed_routes++;
        } else if (rc.kind == TqRouteChangeKind::Modified) {
            rs.change_kind = "Modified";
            rec.modified_routes++;
        } else {
            rs.change_kind = "Unchanged";
            rec.unchanged_routes++;
        }
        rec.route_changes.push_back(std::move(rs));
    }

    rec.issue_count = 0;
    for (const auto& issue : result.plan.issues) {
        TqApplyAuditIssueSummary is;
        if (issue.severity == TqPlanSeverity::Blocker) {
            is.severity = "error";
        } else if (issue.severity == TqPlanSeverity::Warning) {
            is.severity = "warning";
        } else {
            is.severity = "info";
        }
        is.code = issue.code;
        is.route_id = issue.route_id;
        is.reason = issue.reason;
        rec.issues.push_back(std::move(is));
        rec.issue_count++;
    }

    if (rec.issues.empty()) {
        for (const auto& diag : result.diagnostics) {
            TqApplyAuditIssueSummary is;
            is.severity = diag.severity;
            is.code = diag.code;
            is.route_id = diag.route_id;
            is.reason = diag.reason;
            rec.issues.push_back(std::move(is));
            rec.issue_count++;
        }
    }

    return rec;
}

std::string outcome_to_string(TqApplyAuditOutcome outcome) {
    switch (outcome) {
    case TqApplyAuditOutcome::Applied:
        return "Applied";
    case TqApplyAuditOutcome::DryRun:
        return "DryRun";
    case TqApplyAuditOutcome::Rejected:
        return "Rejected";
    case TqApplyAuditOutcome::Failed:
        return "Failed";
    }
    return "Unknown";
}

std::string to_json(const TqApplyAuditRouteSummary& summary) {
    std::string out = "{";
    out += "\"route_id\":\"" + escape_json_string(summary.route_id) + "\",";
    out += "\"change_kind\":\"" + escape_json_string(summary.change_kind) + "\",";
    out += "\"before_identity\":\"" + escape_json_string(summary.before_identity) + "\",";
    out += "\"after_identity\":\"" + escape_json_string(summary.after_identity) + "\",";
    out += "\"field_change_count\":" + std::to_string(summary.field_change_count);
    out += "}";
    return out;
}

std::string to_json(const TqApplyAuditIssueSummary& summary) {
    std::string out = "{";
    out += "\"severity\":\"" + escape_json_string(summary.severity) + "\",";
    out += "\"code\":\"" + escape_json_string(summary.code) + "\",";
    out += "\"route_id\":\"" + escape_json_string(summary.route_id) + "\",";
    out += "\"reason\":\"" + escape_json_string(summary.reason) + "\"";
    out += "}";
    return out;
}

std::string to_json(const TqApplyAuditRecord& record) {
    std::string out = "{";
    out += "\"sequence\":" + std::to_string(record.sequence) + ",";
    out += "\"unix_epoch_ms\":" + std::to_string(record.unix_epoch_ms) + ",";
    out += "\"outcome\":\"" + escape_json_string(outcome_to_string(record.outcome)) + "\",";
    out += "\"request_id\":\"" + escape_json_string(record.request_id) + "\",";
    out += "\"operator_id\":\"" + escape_json_string(record.operator_id) + "\",";
    out += "\"mode\":\"" + escape_json_string(record.mode) + "\",";
    out += "\"status\":\"" + escape_json_string(record.status) + "\",";
    out += "\"message\":\"" + escape_json_string(record.message) + "\",";
    out += "\"previous_policy_identity\":\"" + escape_json_string(record.previous_policy_identity) +
           "\",";
    out +=
        "\"expected_base_identity\":\"" + escape_json_string(record.expected_base_identity) + "\",";
    out += "\"candidate_policy_identity\":\"" +
           escape_json_string(record.candidate_policy_identity) + "\",";
    out += "\"applied_policy_identity\":\"" + escape_json_string(record.applied_policy_identity) +
           "\",";
    out += "\"before_generation\":" + std::to_string(record.before_generation) + ",";
    out += "\"after_generation\":" + std::to_string(record.after_generation) + ",";
    out += "\"added_routes\":" + std::to_string(record.added_routes) + ",";
    out += "\"removed_routes\":" + std::to_string(record.removed_routes) + ",";
    out += "\"modified_routes\":" + std::to_string(record.modified_routes) + ",";
    out += "\"unchanged_routes\":" + std::to_string(record.unchanged_routes) + ",";
    out += "\"issue_count\":" + std::to_string(record.issue_count) + ",";

    out += "\"route_changes\":[";
    for (std::size_t i = 0; i < record.route_changes.size(); ++i) {
        if (i > 0)
            out += ",";
        out += to_json(record.route_changes[i]);
    }
    out += "],";

    out += "\"issues\":[";
    for (std::size_t i = 0; i < record.issues.size(); ++i) {
        if (i > 0)
            out += ",";
        out += to_json(record.issues[i]);
    }
    out += "]";

    out += "}";
    return out;
}

std::string to_json_compact(const TqApplyAuditRecord& record) {
    std::string out = "{";
    out += "\"sequence\":" + std::to_string(record.sequence) + ",";
    out += "\"unix_epoch_ms\":" + std::to_string(record.unix_epoch_ms) + ",";
    out += "\"outcome\":\"" + escape_json_string(outcome_to_string(record.outcome)) + "\",";
    out += "\"request_id\":\"" + escape_json_string(record.request_id) + "\",";
    out += "\"operator_id\":\"" + escape_json_string(record.operator_id) + "\",";
    out += "\"mode\":\"" + escape_json_string(record.mode) + "\",";
    out += "\"status\":\"" + escape_json_string(record.status) + "\",";
    out += "\"message\":\"" + escape_json_string(record.message) + "\",";
    out += "\"previous_policy_identity\":\"" + escape_json_string(record.previous_policy_identity) +
           "\",";
    out +=
        "\"expected_base_identity\":\"" + escape_json_string(record.expected_base_identity) + "\",";
    out += "\"candidate_policy_identity\":\"" +
           escape_json_string(record.candidate_policy_identity) + "\",";
    out += "\"applied_policy_identity\":\"" + escape_json_string(record.applied_policy_identity) +
           "\",";
    out += "\"before_generation\":" + std::to_string(record.before_generation) + ",";
    out += "\"after_generation\":" + std::to_string(record.after_generation) + ",";
    out += "\"added_routes\":" + std::to_string(record.added_routes) + ",";
    out += "\"removed_routes\":" + std::to_string(record.removed_routes) + ",";
    out += "\"modified_routes\":" + std::to_string(record.modified_routes) + ",";
    out += "\"unchanged_routes\":" + std::to_string(record.unchanged_routes) + ",";
    out += "\"issue_count\":" + std::to_string(record.issue_count);
    out += "}";
    return out;
}

std::string to_json(const TqCurrentPolicySummary& summary) {
    std::string out = "{";
    out += "\"ok\":" + std::string(summary.ok ? "true" : "false") + ",";
    if (!summary.ok) {
        if (!summary.error_code.empty()) {
            out += "\"error_code\":\"" + escape_json_string(summary.error_code) + "\",";
        }
        if (!summary.message.empty()) {
            out += "\"message\":\"" + escape_json_string(summary.message) + "\"";
        }
        if (out.back() == ',') {
            out.pop_back();
        }
        out += "}";
        return out;
    }

    out += "\"policy_identity\":\"" + escape_json_string(summary.policy_identity) + "\",";
    out += "\"generation\":" + std::to_string(summary.generation) + ",";
    out += "\"route_count\":" + std::to_string(summary.route_count) + ",";
    out += "\"source_name\":\"" + escape_json_string(summary.source_name) + "\",";
    out += "\"version\":{";
    out +=
        "\"source_schema_version\":\"" + escape_json_string(summary.source_schema_version) + "\",";
    out += "\"policy_ir_version\":\"" + escape_json_string(summary.policy_ir_version) + "\",";
    out += "\"identity_version\":\"" + escape_json_string(summary.identity_version) + "\",";
    out += "\"emitter_version\":\"" + escape_json_string(summary.emitter_version) + "\",";
    out += "\"runtime_min_version\":\"" + escape_json_string(summary.runtime_min_version) + "\",";
    out += "\"runtime_capability_profile\":\"" +
           escape_json_string(summary.runtime_capability_profile) + "\"";
    out += "},";

    out += "\"has_latest_apply\":" + std::string(summary.has_latest_apply ? "true" : "false") + ",";
    if (summary.has_latest_apply) {
        out += "\"latest_apply\":" + to_json_compact(summary.latest_apply);
    } else {
        out += "\"latest_apply\":null";
    }

    out += "}";
    return out;
}

bool should_record(const TqApplyResult& result, const TqApplyAuditStoreOptions& options) {
    if (result.status == TqApplyStatus::Applied) {
        return true;
    }
    if (result.status == TqApplyStatus::DryRunReady) {
        return options.record_dry_run_attempts;
    }
    if (result.status == TqApplyStatus::RejectedInvalidRequest ||
        result.status == TqApplyStatus::RejectedParseError ||
        result.status == TqApplyStatus::RejectedCompileError ||
        result.status == TqApplyStatus::RejectedValidation ||
        result.status == TqApplyStatus::RejectedRouteAnalysis ||
        result.status == TqApplyStatus::RejectedCasMismatch ||
        result.status == TqApplyStatus::RejectedNoChanges ||
        result.status == TqApplyStatus::RejectedSnapshotBuildFailed) {
        return options.record_rejected_attempts;
    }
    return true; // record Failed and other unhandled scenarios
}

} // namespace bytetaper::taperquery
