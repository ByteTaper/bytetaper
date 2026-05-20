// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_log_events.h"
#include "control_plane/policy_lifecycle_event.h"
#include "runtime_policy/runtime_policy_log_events.h"

#include <gtest/gtest.h>

using namespace bytetaper::control_plane;
using namespace bytetaper::runtime_policy;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST(PolicyLifecycleLoggingTest, ControlPlaneJsonContainsRequiredFields) {
    PolicyLifecycleEvent event{};
    event.event_type = PolicyLifecycleEventType::PolicyApplyFailed;
    event.resource_key = "policy/default/runtime";
    event.stage = "compile";
    event.status = "failure";
    event.error_code = "POLICY_APPLY_COMPILE_FAILED";
    event.message = "compile failed";

    const std::string json = format_control_plane_lifecycle_log_json(event);
    EXPECT_TRUE(contains(json, "\"component\":\"control_plane\""));
    EXPECT_TRUE(contains(json, "\"event\":\"policy_apply_failed\""));
    EXPECT_TRUE(contains(json, "\"resourceKey\":\"policy/default/runtime\""));
    EXPECT_TRUE(contains(json, "\"stage\":\"compile\""));
    EXPECT_TRUE(contains(json, "\"status\":\"failure\""));
    EXPECT_TRUE(contains(json, "\"errorCode\":\"POLICY_APPLY_COMPILE_FAILED\""));
}

TEST(PolicyLifecycleLoggingTest, RuntimePolicyJsonContainsActivationFailureFields) {
    PolicyLifecycleEvent event{};
    event.event_type = PolicyLifecycleEventType::PolicyActivationFailed;
    event.resource_key = "policy/default/runtime";
    event.stage = "snapshot_swapped";
    event.status = "failure";
    event.error_code = "POLICY_ACTIVATION_SNAPSHOT_SWAP_FAILED";
    event.message = "swap failed";
    event.manual_resolution_required = true;
    event.old_snapshot_still_active = true;

    const std::string json = format_runtime_policy_lifecycle_log_json(event);
    EXPECT_TRUE(contains(json, "\"component\":\"runtime_policy\""));
    EXPECT_TRUE(contains(json, "\"event\":\"policy_activation_failed\""));
    EXPECT_TRUE(contains(json, "\"failedStage\":\"snapshot_swapped\""));
    EXPECT_TRUE(contains(json, "\"manualResolutionRequired\":true"));
    EXPECT_TRUE(contains(json, "\"oldSnapshotStillActive\":true"));
}
