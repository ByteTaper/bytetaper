// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_CONTROL_PLANE_POLICY_CLIENT_H
#define BYTETAPER_RUNTIME_POLICY_CONTROL_PLANE_POLICY_CLIENT_H

#include "control_plane/policy_apply_api.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/policy_version_query.h"
#include "runtime_policy/runtime_status_report.h"

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {
class ControlPlaneService;
}

namespace bytetaper::runtime_policy {

struct ActivePolicyPointerResult {
    bool ok = false;
    control_plane::ActivePolicyPointer active{};
    std::string error;
    std::string error_code;
};

struct CanonicalPolicyResult {
    bool ok = false;
    control_plane::PolicyVersionRecord record{};
    std::string canonical_yaml;
    std::string error;
    std::string error_code;
};

struct RuntimeStatusReportResult {
    bool ok = false;
    std::string error;
    std::string error_code;
};

class ControlPlanePolicyClient {
public:
    virtual ~ControlPlanePolicyClient() = default;

    virtual ActivePolicyPointerResult
    get_active_policy(const control_plane::PolicyResourceKey& resource_key) = 0;

    virtual CanonicalPolicyResult
    get_policy_version(const control_plane::PolicyResourceKey& resource_key,
                       std::uint64_t generation) = 0;

    virtual RuntimeStatusReportResult
    report_runtime_status(const RuntimePolicyStatusReport& report) = 0;
};

class InProcessControlPlanePolicyClient : public ControlPlanePolicyClient {
public:
    explicit InProcessControlPlanePolicyClient(control_plane::ControlPlaneService* service);

    ActivePolicyPointerResult
    get_active_policy(const control_plane::PolicyResourceKey& resource_key) override;

    CanonicalPolicyResult get_policy_version(const control_plane::PolicyResourceKey& resource_key,
                                             std::uint64_t generation) override;

    RuntimeStatusReportResult
    report_runtime_status(const RuntimePolicyStatusReport& report) override;

    const RuntimePolicyStatusReport& last_report() const;

private:
    control_plane::ControlPlaneService* service_;
    RuntimePolicyStatusReport last_report_{};
};

class HttpControlPlanePolicyClient : public ControlPlanePolicyClient {
public:
    explicit HttpControlPlanePolicyClient(std::string endpoint);

    ActivePolicyPointerResult
    get_active_policy(const control_plane::PolicyResourceKey& resource_key) override;

    CanonicalPolicyResult get_policy_version(const control_plane::PolicyResourceKey& resource_key,
                                             std::uint64_t generation) override;

    RuntimeStatusReportResult
    report_runtime_status(const RuntimePolicyStatusReport& report) override;

private:
    std::string endpoint_;
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_CONTROL_PLANE_POLICY_CLIENT_H
