// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/control_plane_policy_client.h"

#include "control_plane/control_plane_service.h"
#include "runtime_policy/runtime_policy_pull_status.h"

namespace bytetaper::runtime_policy {

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
    out.ok = true;
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
