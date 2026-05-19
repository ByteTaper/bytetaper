// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/control_plane_policy_client.h"

#include "control_plane/control_plane_service.h"
#include "control_plane/runtime_status_report.h"
#include "runtime_policy/runtime_policy_pull_status.h"

#include <chrono>

namespace bytetaper::runtime_policy {

namespace {

std::int64_t now_unix_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

control_plane::RuntimeStatusReport
to_control_plane_report(const RuntimePolicyStatusReport& report) {
    control_plane::RuntimeStatusReport out{};
    out.runtime_id = report.runtime_id;
    out.resource_key = report.resource_key;
    out.gateway_adapter = report.gateway_adapter;
    out.active_generation = report.active_generation;
    out.active_policy_id = report.active_policy_id;
    out.active_canonical_hash = report.active_canonical_hash;
    out.activation_status = report.activation_status;
    out.control_plane_reachable = report.control_plane_reachable;
    out.data_path_mode = report.data_path_mode;
    out.last_pull_at_unix_epoch_ms = report.last_pull_at_unix_epoch_ms;
    out.last_activated_at_unix_epoch_ms = report.last_activated_at_unix_epoch_ms;
    out.last_error_code = report.last_error_code;
    out.last_error_message = report.last_error_message;
    out.received_at_unix_epoch_ms = now_unix_epoch_ms();
    return out;
}

} // namespace

InProcessControlPlanePolicyClient::InProcessControlPlanePolicyClient(
    control_plane::ControlPlaneService* service)
    : service_(service) {}

ActivePolicyPointerResult InProcessControlPlanePolicyClient::get_active_policy(
    const control_plane::PolicyResourceKey& resource_key) {
    ActivePolicyPointerResult out{};
    if (service_ == nullptr) {
        out.error = "control plane service is not configured";
        out.error_code = kErrControlPlaneUnavailable;
        return out;
    }

    const control_plane::ActivePolicyResult res = service_->get_active_policy(resource_key);
    if (!res.ok) {
        out.error = res.error;
        out.error_code = kErrActivePolicyQueryFailed;
        return out;
    }

    out.ok = true;
    out.active = res.active;
    return out;
}

CanonicalPolicyResult InProcessControlPlanePolicyClient::get_policy_version(
    const control_plane::PolicyResourceKey& resource_key, std::uint64_t generation) {
    CanonicalPolicyResult out{};
    if (service_ == nullptr) {
        out.error = "control plane service is not configured";
        out.error_code = kErrControlPlaneUnavailable;
        return out;
    }

    const control_plane::PolicyVersionFetchResult res =
        service_->get_policy_version(resource_key, generation);
    if (!res.ok) {
        out.error = res.error;
        out.error_code = kErrPolicyVersionFetchFailed;
        return out;
    }

    out.ok = true;
    out.record = res.record;
    out.canonical_yaml = res.canonical_yaml;
    return out;
}

RuntimeStatusReportResult
InProcessControlPlanePolicyClient::report_runtime_status(const RuntimePolicyStatusReport& report) {
    last_report_ = report;
    RuntimeStatusReportResult out{};
    if (service_ == nullptr) {
        out.error = "control plane service is not configured";
        out.error_code = kErrControlPlaneUnavailable;
        return out;
    }

    const control_plane::RuntimeStatusReportResult cp_result =
        service_->report_runtime_status(to_control_plane_report(report));
    out.ok = cp_result.ok;
    out.error = cp_result.error;
    out.error_code = cp_result.error_code;
    return out;
}

const RuntimePolicyStatusReport& InProcessControlPlanePolicyClient::last_report() const {
    return last_report_;
}

HttpControlPlanePolicyClient::HttpControlPlanePolicyClient(std::string endpoint)
    : endpoint_(std::move(endpoint)) {}

ActivePolicyPointerResult
HttpControlPlanePolicyClient::get_active_policy(const control_plane::PolicyResourceKey&) {
    ActivePolicyPointerResult out{};
    out.error = "HTTP control plane client is not implemented (endpoint: " + endpoint_ + ")";
    out.error_code = kErrControlPlaneUnavailable;
    return out;
}

CanonicalPolicyResult
HttpControlPlanePolicyClient::get_policy_version(const control_plane::PolicyResourceKey&,
                                                 std::uint64_t) {
    CanonicalPolicyResult out{};
    out.error = "HTTP control plane client is not implemented (endpoint: " + endpoint_ + ")";
    out.error_code = kErrControlPlaneUnavailable;
    return out;
}

RuntimeStatusReportResult
HttpControlPlanePolicyClient::report_runtime_status(const RuntimePolicyStatusReport&) {
    RuntimeStatusReportResult out{};
    out.error = "HTTP control plane client is not implemented (endpoint: " + endpoint_ + ")";
    out.error_code = kErrRuntimeStatusReportFailed;
    return out;
}

} // namespace bytetaper::runtime_policy
