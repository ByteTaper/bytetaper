// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/policy_snapshot.h"
#include "taperquery/policy_ir.h"
#include "taperquery/tq_apply_audit.h"
#include "taperquery/tq_apply_service.h"

#include <gtest/gtest.h>
#include <thread>

namespace bytetaper::taperquery {

namespace {

TqPolicyDocument create_valid_doc(const std::string& doc_id, const std::string& base_sha,
                                  const std::string& route_id, const std::string& prefix) {
    TqPolicyDocument doc;
    doc.document_id = doc_id;
    doc.version.source_schema_version = "tq/v1";
    doc.expected_base_sha = base_sha;

    TqRoutePolicy r;
    r.route_id = route_id;
    r.match_prefix = prefix;
    r.match_kind = TqRouteMatchKind::Prefix;
    r.mutation = TqMutationMode::Disabled;
    r.allowed_method = TqHttpMethod::Any;
    doc.routes.push_back(r);

    return doc;
}

class TqApplyAuditTest : public ::testing::Test {
protected:
    void SetUp() override {
        uint64_t initial_gen = store.next_generation();
        TqPolicyDocument init_doc = create_valid_doc("my-policy", "", "initial_route", "/initial");

        auto build_res = runtime::build_runtime_policy_snapshot_from_ir(init_doc, initial_gen);
        ASSERT_TRUE(build_res.ok) << build_res.error;
        ASSERT_NE(build_res.snapshot, nullptr);

        initial_identity = build_res.snapshot->policy_identity;

        std::string err;
        ASSERT_TRUE(store.install_initial(build_res.snapshot, &err)) << err;
    }

    runtime::RuntimePolicyStore store;
    std::string initial_identity;
};

} // namespace

// 1. AuditStoreAppendAndLatest
TEST(TqApplyAuditStoreTest, AuditStoreAppendAndLatest) {
    TqApplyAuditStore store;
    EXPECT_EQ(store.size(), 0u);

    TqApplyAuditRecord r1;
    r1.request_id = "req-1";
    r1.operator_id = "op-1";
    r1.outcome = TqApplyAuditOutcome::Applied;

    std::uint64_t seq = store.append(r1);
    EXPECT_EQ(seq, 1u);
    EXPECT_EQ(store.size(), 1u);

    TqApplyAuditRecord latest_rec;
    ASSERT_TRUE(store.latest(&latest_rec));
    EXPECT_EQ(latest_rec.sequence, 1u);
    EXPECT_EQ(latest_rec.request_id, "req-1");
    EXPECT_EQ(latest_rec.operator_id, "op-1");
}

// 2. AuditStoreRingEviction
TEST(TqApplyAuditStoreTest, AuditStoreRingEviction) {
    TqApplyAuditStoreOptions opts;
    opts.capacity = 2;
    TqApplyAuditStore store(opts);

    TqApplyAuditRecord r1, r2, r3;
    r1.request_id = "req-1";
    r2.request_id = "req-2";
    r3.request_id = "req-3";

    store.append(r1);
    store.append(r2);
    EXPECT_EQ(store.size(), 2u);

    store.append(r3);
    EXPECT_EQ(store.size(), 2u);

    auto recent = store.recent(2);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].request_id, "req-2");
    EXPECT_EQ(recent[1].request_id, "req-3");
}

// 3. AuditStoreSequenceIncrements
TEST(TqApplyAuditStoreTest, AuditStoreSequenceIncrements) {
    TqApplyAuditStore store;
    TqApplyAuditRecord r1, r2;
    EXPECT_EQ(store.append(r1), 1u);
    EXPECT_EQ(store.append(r2), 2u);

    TqApplyAuditRecord out;
    ASSERT_TRUE(store.latest(&out));
    EXPECT_EQ(out.sequence, 2u);
}

// 4. BuildAuditFromSuccessfulApply
TEST_F(TqApplyAuditTest, BuildAuditFromSuccessfulApply) {
    TqApplyRequest req;
    req.request_id = "req-123";
    req.operator_id = "operator-abc";
    req.mode = TqApplyMode::Apply;
    req.expected_base_identity = initial_identity;
    req.source = "policy \"my-policy\" against sha \"" + initial_identity +
                 "\" {\n"
                 "  route \"new_route\" when path prefix \"/new\" {}\n"
                 "}";

    TqApplyService service(&store);
    auto res = service.execute(req);
    ASSERT_TRUE(res.ok) << res.message;

    TqApplyAuditRecord rec = build_apply_audit_record(req, res);
    EXPECT_EQ(rec.outcome, TqApplyAuditOutcome::Applied);
    EXPECT_EQ(rec.request_id, "req-123");
    EXPECT_EQ(rec.operator_id, "operator-abc");
    EXPECT_EQ(rec.mode, "apply");
    EXPECT_EQ(rec.status, "Applied");
    EXPECT_EQ(rec.previous_policy_identity, initial_identity);
    EXPECT_EQ(rec.applied_policy_identity, res.applied_policy_identity);
    EXPECT_EQ(rec.before_generation, 1u);
    EXPECT_EQ(rec.after_generation, 2u);
    EXPECT_GT(rec.added_routes, 0u);
}

// 5. BuildAuditFromCasMismatch
TEST_F(TqApplyAuditTest, BuildAuditFromCasMismatch) {
    TqApplyRequest req;
    req.request_id = "req-cas";
    req.operator_id = "operator-cas";
    req.mode = TqApplyMode::Apply;
    req.expected_base_identity = "incorrect-base-sha";
    req.source = "policy \"my-policy\" {\n"
                 "  route \"new_route\" when path prefix \"/new\" {}\n"
                 "}";

    TqApplyService service(&store);
    auto res = service.execute(req);
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCasMismatch);

    TqApplyAuditRecord rec = build_apply_audit_record(req, res);
    EXPECT_EQ(rec.outcome, TqApplyAuditOutcome::Rejected);
    EXPECT_EQ(rec.request_id, "req-cas");
    EXPECT_EQ(rec.operator_id, "operator-cas");
    EXPECT_EQ(rec.mode, "apply");
    EXPECT_EQ(rec.status, "RejectedCasMismatch");
    EXPECT_EQ(rec.previous_policy_identity, initial_identity);
    EXPECT_EQ(rec.applied_policy_identity, "");
}

// 6. DryRunNotRecordedByDefault
TEST_F(TqApplyAuditTest, DryRunNotRecordedByDefault) {
    TqApplyAuditStore audit_store;
    TqApplyRequest req;
    req.request_id = "req-dry";
    req.operator_id = "op-dry";
    req.mode = TqApplyMode::DryRun;
    req.expected_base_identity = initial_identity;
    req.source = "policy \"my-policy\" against sha \"" + initial_identity +
                 "\" {\n"
                 "  route \"new_route\" when path prefix \"/new\" {}\n"
                 "}";

    TqApplyService service(&store, nullptr, &audit_store);
    auto res = service.execute(req);
    ASSERT_TRUE(res.ok) << res.message;

    EXPECT_EQ(audit_store.size(), 0u);
}

// 7. SuccessfulApplyWritesAudit
TEST_F(TqApplyAuditTest, SuccessfulApplyWritesAudit) {
    TqApplyAuditStore audit_store;
    TqApplyRequest req;
    req.request_id = "req-apply";
    req.operator_id = "op-apply";
    req.mode = TqApplyMode::Apply;
    req.expected_base_identity = initial_identity;
    req.source = "policy \"my-policy\" against sha \"" + initial_identity +
                 "\" {\n"
                 "  route \"new_route\" when path prefix \"/new\" {}\n"
                 "}";

    TqApplyService service(&store, nullptr, &audit_store);
    auto res = service.execute(req);
    ASSERT_TRUE(res.ok) << res.message;

    EXPECT_EQ(audit_store.size(), 1u);
    TqApplyAuditRecord rec;
    ASSERT_TRUE(audit_store.latest(&rec));
    EXPECT_EQ(rec.outcome, TqApplyAuditOutcome::Applied);
    EXPECT_EQ(rec.request_id, "req-apply");
    EXPECT_EQ(rec.applied_policy_identity, res.applied_policy_identity);
}

// 8. RejectedCasWritesAuditWhenEnabled
TEST_F(TqApplyAuditTest, RejectedCasWritesAuditWhenEnabled) {
    TqApplyAuditStoreOptions opts;
    opts.record_rejected_attempts = true;
    TqApplyAuditStore audit_store(opts);

    TqApplyRequest req;
    req.request_id = "req-cas-reject";
    req.operator_id = "op-cas-reject";
    req.mode = TqApplyMode::Apply;
    req.expected_base_identity = "bad-base-sha";
    req.source = "policy \"my-policy\" {\n"
                 "  route \"new_route\" when path prefix \"/new\" {}\n"
                 "}";

    TqApplyService service(&store, nullptr, &audit_store);
    auto res = service.execute(req);
    ASSERT_FALSE(res.ok);

    EXPECT_EQ(audit_store.size(), 1u);
    TqApplyAuditRecord rec;
    ASSERT_TRUE(audit_store.latest(&rec));
    EXPECT_EQ(rec.outcome, TqApplyAuditOutcome::Rejected);
    EXPECT_EQ(rec.status, "RejectedCasMismatch");
}

// 9. CurrentPolicySummaryIncludesLatestApply
TEST_F(TqApplyAuditTest, CurrentPolicySummaryIncludesLatestApply) {
    TqApplyAuditStore audit_store;
    TqApplyRequest req;
    req.request_id = "req-current-summary";
    req.operator_id = "op-current-summary";
    req.mode = TqApplyMode::Apply;
    req.expected_base_identity = initial_identity;
    req.source = "policy \"my-policy\" against sha \"" + initial_identity +
                 "\" {\n"
                 "  route \"new_route\" when path prefix \"/new\" {}\n"
                 "}";

    TqApplyService service(&store, nullptr, &audit_store);
    auto res = service.execute(req);
    ASSERT_TRUE(res.ok) << res.message;

    TqCurrentPolicySummary summary;
    auto snapshot = store.load();
    ASSERT_NE(snapshot, nullptr);

    summary.ok = true;
    summary.policy_identity = snapshot->policy_identity;
    summary.generation = snapshot->generation;
    summary.route_count = snapshot->routes.size();
    summary.source_name = snapshot->source_name;
    summary.source_schema_version = snapshot->policy_ir.version.source_schema_version;
    summary.policy_ir_version = snapshot->policy_ir.version.policy_ir_version;
    summary.identity_version = snapshot->policy_ir.version.identity_version;
    summary.emitter_version = snapshot->policy_ir.version.emitter_version;
    summary.runtime_min_version = snapshot->policy_ir.version.runtime_min_version;
    summary.runtime_capability_profile = snapshot->policy_ir.version.runtime_capability_profile;

    summary.has_latest_apply = audit_store.latest(&summary.latest_apply);

    EXPECT_TRUE(summary.has_latest_apply);
    EXPECT_EQ(summary.latest_apply.request_id, "req-current-summary");
    EXPECT_EQ(summary.latest_apply.outcome, TqApplyAuditOutcome::Applied);

    std::string json = to_json(summary);
    EXPECT_NE(json.find("\"has_latest_apply\":true"), std::string::npos);
    EXPECT_NE(json.find("\"request_id\":\"req-current-summary\""), std::string::npos);
    EXPECT_EQ(json.find("\"route_changes\":"), std::string::npos);
    EXPECT_EQ(json.find("\"issues\":"), std::string::npos);
}

} // namespace bytetaper::taperquery
