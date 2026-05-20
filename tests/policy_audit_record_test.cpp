// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/manual_resolution_audit.h"
#include "control_plane/policy_lifecycle_event.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"

#include <gtest/gtest.h>

using namespace bytetaper::control_plane;

TEST(PolicyAuditRecordTest, V3RoundTripPreservesLifecycleFields) {
    PolicyAuditRecord original{};
    original.record_version = 3;
    original.record_type = "PolicyAuditRecord";
    original.apply_id = "req-1";
    original.event_id = "evt-1";
    original.resource_key = "policy/default/runtime";
    original.generation = 4;
    original.policy_id = "policy-after";
    original.event_type = "policy_apply_committed";
    original.job_id = "policy-job-req-1";
    original.before_policy_id = "policy-before";
    original.after_policy_id = "policy-after";
    original.lifecycle_status = "success";
    original.failure_code = "";
    original.failure_stage = "compare_and_promote";
    original.message = "committed";
    original.canonical_hash = "sha256:11112222";
    original.operation = "policy_apply_committed";
    original.before_generation = 3;
    original.after_generation = 4;
    original.result = "success";

    const std::string json = serialize_audit_record(original);
    PolicyAuditRecord parsed{};
    ASSERT_TRUE(deserialize_audit_record(json, &parsed));
    EXPECT_EQ(parsed.record_version, 3u);
    EXPECT_EQ(parsed.event_type, "policy_apply_committed");
    EXPECT_EQ(parsed.job_id, "policy-job-req-1");
    EXPECT_EQ(parsed.before_policy_id, "policy-before");
    EXPECT_EQ(parsed.after_policy_id, "policy-after");
    EXPECT_EQ(parsed.lifecycle_status, "success");
    EXPECT_EQ(parsed.failure_stage, "compare_and_promote");
    EXPECT_EQ(parsed.canonical_hash, "sha256:11112222");
}

TEST(PolicyAuditRecordTest, LifecycleEventMapsToV3AuditRecord) {
    PolicyLifecycleEvent event{};
    event.event_type = PolicyLifecycleEventType::PolicyApplyFailed;
    event.resource_key = "policy/default/runtime";
    event.job_id = "job-1";
    event.request_id = "req-1";
    event.before_generation = 2;
    event.after_generation = 3;
    event.before_policy_id = "before";
    event.after_policy_id = "after";
    event.stage = "store_version";
    event.status = "failure";
    event.error_code = "POLICY_APPLY_STORE_VERSION_FAILED";
    event.message = "version conflict";
    event.canonical_hash = "sha256:deadbeef";

    const PolicyAuditRecord audit = lifecycle_event_to_audit_record(event);
    EXPECT_EQ(audit.canonical_hash, "sha256:deadbeef");
    EXPECT_EQ(audit.record_version, 3u);
    EXPECT_EQ(audit.event_type, "policy_apply_failed");
    EXPECT_EQ(audit.job_id, "job-1");
    EXPECT_EQ(audit.failure_code, "POLICY_APPLY_STORE_VERSION_FAILED");
    EXPECT_EQ(audit.failure_stage, "store_version");
    EXPECT_EQ(audit.result, "failure");
}

TEST(PolicyAuditRecordTest, ManualResolutionAuditFactoryEmitsV3LifecycleFields) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const PolicyAuditRecord audit = make_manual_resolution_audit_record(
        PolicyLifecycleEventType::ManualAdoptRequested, key, "adopt-local", "manual-adopt",
        "operator-1", "req-adopt", "success", "", "", "", 1, 0, 2);
    EXPECT_EQ(audit.record_version, 3u);
    EXPECT_EQ(audit.event_id, "req-adopt");
    EXPECT_EQ(audit.event_type, "manual_adopt_requested");
    EXPECT_EQ(audit.lifecycle_status, "success");
    EXPECT_EQ(audit.target_generation, 2u);
}
