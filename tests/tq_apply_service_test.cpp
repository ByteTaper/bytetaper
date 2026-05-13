// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/policy_snapshot.h"
#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/tq_apply_service.h"

#include <gtest/gtest.h>
#include <thread>
namespace bytetaper::taperquery {

namespace {

TqPolicyDocument create_valid_document(const std::string& doc_id, const std::string& base_sha,
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

} // namespace

class TqApplyServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        uint64_t initial_gen = store.next_generation();

        // Build initial snapshot from IR
        TqPolicyDocument init_doc =
            create_valid_document("my-policy", "", "initial_route", "/initial");

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

TEST_F(TqApplyServiceTest, InvalidRequests) {
    TqApplyService service(&store);

    // 1. Empty source
    {
        TqApplyRequest req;
        req.source = "";
        req.expected_base_identity = initial_identity;
        req.mode = TqApplyMode::Apply;

        auto res = service.execute(req);
        EXPECT_FALSE(res.ok);
        EXPECT_EQ(res.status, TqApplyStatus::RejectedInvalidRequest);
        EXPECT_EQ(res.message, "TaperQuery source is empty");
        EXPECT_EQ(store.load()->policy_identity, initial_identity);
        EXPECT_EQ(store.load()->generation, 1u);
    }

    // 2. Strict production mode missing expected base identity
    {
        TqApplyRequest req;
        req.source = "policy \"pid\" { route \"r1\" when path prefix \"/\" {} }";
        req.expected_base_identity = "";
        req.mode = TqApplyMode::Apply;
        req.strict_production = true;

        auto res = service.execute(req);
        EXPECT_FALSE(res.ok);
        EXPECT_EQ(res.status, TqApplyStatus::RejectedInvalidRequest);
        EXPECT_EQ(store.load()->policy_identity, initial_identity);
        EXPECT_EQ(store.load()->generation, 1u);
    }
}

TEST_F(TqApplyServiceTest, ParseAndCompileError) {
    TqApplyService service(&store);

    // Parse Error (missing brackets)
    {
        TqApplyRequest req;
        req.source = "policy \"pid\" { route \"r1\" ";
        req.expected_base_identity = initial_identity;
        req.mode = TqApplyMode::Apply;
        req.strict_production = false;

        auto res = service.execute(req);
        EXPECT_FALSE(res.ok);
        EXPECT_EQ(res.status, TqApplyStatus::RejectedParseError);
        EXPECT_FALSE(res.diagnostics.empty());
        EXPECT_EQ(res.diagnostics[0].code, "TQ_PARSE_ERROR");
        EXPECT_EQ(store.load()->policy_identity, initial_identity);
        EXPECT_EQ(store.load()->generation, 1u);
    }

    // Compile Error (duplicate routes)
    {
        TqApplyRequest req;
        req.source = "policy \"pid\" { route \"r1\" when path prefix \"/\" {} route \"r1\" when "
                     "path prefix \"/\" {} }";
        req.expected_base_identity = initial_identity;
        req.mode = TqApplyMode::Apply;
        req.strict_production = false;

        auto res = service.execute(req);
        EXPECT_FALSE(res.ok);
        EXPECT_EQ(res.status, TqApplyStatus::RejectedCompileError);
        EXPECT_FALSE(res.diagnostics.empty());
        EXPECT_EQ(res.diagnostics[0].code, "TQ_COMPILE_ERROR");
        EXPECT_EQ(store.load()->policy_identity, initial_identity);
        EXPECT_EQ(store.load()->generation, 1u);
    }
}

TEST_F(TqApplyServiceTest, CasMismatch) {
    TqApplyService service(&store);

    TqApplyRequest req;
    req.source = "policy \"pid\" { route \"r1\" when path prefix \"/new-route\" {} }";
    req.expected_base_identity = "outdated_hash_12345";
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCasMismatch);
    ASSERT_FALSE(res.diagnostics.empty());
    EXPECT_EQ(res.diagnostics[0].code, "CAS_MISMATCH");
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

TEST_F(TqApplyServiceTest, SemanticValidationError) {
    TqApplyService service(&store);

    // Route cache store behavior with zero L1 capacity is a semantic validation error
    TqApplyRequest req;
    req.source = "policy \"pid\" { route \"r1\" when path prefix \"/api\" { cache store ttl 60s { "
                 "l1 enabled capacity 0 entries } } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedValidation);
    ASSERT_FALSE(res.diagnostics.empty());
    EXPECT_EQ(res.diagnostics[0].code, "TQ_VALIDATION_ERROR");
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

TEST_F(TqApplyServiceTest, RoutePrecedenceError) {
    TqApplyService service(&store);

    // Conflict: exact duplicate route matches trigger a hard precedence/routing error
    TqApplyRequest req;
    req.source = "policy \"pid\" { route \"r1\" when path exact \"/api\" {} route \"r2\" when "
                 "path exact \"/api\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = true;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedRouteAnalysis);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

class FailingTqSnapshotBuilder : public TqSnapshotBuilder {
public:
    runtime::RuntimePolicySnapshotBuildResult build_snapshot(const TqPolicyDocument&,
                                                             std::uint64_t) override {
        runtime::RuntimePolicySnapshotBuildResult res;
        res.ok = false;
        res.error = "simulated snapshot build failure";
        return res;
    }
};

TEST_F(TqApplyServiceTest, SnapshotBuildFailure) {
    FailingTqSnapshotBuilder failing_builder;
    TqApplyService service(&store, &failing_builder);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/api\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);

    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedSnapshotBuildFailed);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

class DummySucceedsFinalFailsBuilder : public TqSnapshotBuilder {
public:
    runtime::RuntimePolicySnapshotBuildResult build_snapshot(const TqPolicyDocument& policy_ir,
                                                             std::uint64_t generation) override {
        call_count_++;
        if (call_count_ == 1) {
            return runtime::build_runtime_policy_snapshot_from_ir(policy_ir, generation);
        } else {
            runtime::RuntimePolicySnapshotBuildResult res;
            res.ok = false;
            res.error = "simulated final snapshot build failure";
            return res;
        }
    }

private:
    int call_count_ = 0;
};

TEST_F(TqApplyServiceTest, TransactionalRollbackOnFinalBuildFailure) {
    DummySucceedsFinalFailsBuilder custom_builder;
    TqApplyService service(&store, &custom_builder);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/api\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);

    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedSnapshotBuildFailed);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);

    // Apply normally and verify sequential generation increment without gaps
    TqApplyService normal_service(&store);
    auto res2 = normal_service.execute(req);
    EXPECT_TRUE(res2.ok);
    EXPECT_EQ(res2.status, TqApplyStatus::Applied);
    EXPECT_EQ(store.load()->generation, 2u);
}

TEST_F(TqApplyServiceTest, MultiServiceSerialization) {
    TqApplyService service1(&store);
    TqApplyService service2(&store);

    TqApplyRequest req1;
    req1.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/api1\" {} }";
    req1.expected_base_identity = initial_identity;
    req1.mode = TqApplyMode::Apply;
    req1.strict_production = false;

    TqApplyRequest req2;
    req2.source = "policy \"my-policy\" { route \"r2\" when path prefix \"/api2\" {} }";
    req2.expected_base_identity = initial_identity;
    req2.mode = TqApplyMode::Apply;
    req2.strict_production = false;

    TqApplyResult res1, res2;
    std::thread t1([&]() { res1 = service1.execute(req1); });
    std::thread t2([&]() { res2 = service2.execute(req2); });

    t1.join();
    t2.join();

    // Exactly one must succeed, and exactly one must fail with RejectedCasMismatch
    if (res1.ok) {
        EXPECT_EQ(res1.status, TqApplyStatus::Applied);
        EXPECT_FALSE(res2.ok);
        EXPECT_EQ(res2.status, TqApplyStatus::RejectedCasMismatch);
        EXPECT_EQ(store.load()->generation, 2u);
    } else {
        EXPECT_EQ(res1.status, TqApplyStatus::RejectedCasMismatch);
        EXPECT_TRUE(res2.ok);
        EXPECT_EQ(res2.status, TqApplyStatus::Applied);
        EXPECT_EQ(store.load()->generation, 2u);
    }
}

class DelayingTqSnapshotBuilder : public TqSnapshotBuilder {
public:
    runtime::RuntimePolicySnapshotBuildResult build_snapshot(const TqPolicyDocument& policy_ir,
                                                             std::uint64_t generation) override {
        int current = ++inflight_;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (current > max_inflight_) {
                max_inflight_ = current;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto res = runtime::build_runtime_policy_snapshot_from_ir(policy_ir, generation);
        --inflight_;
        return res;
    }

    int max_inflight() {
        std::lock_guard<std::mutex> lock(mu_);
        return max_inflight_;
    }

private:
    std::atomic<int> inflight_ = 0;
    std::mutex mu_;
    int max_inflight_ = 0;
};

TEST_F(TqApplyServiceTest, ApplyBoundaryExclusivity) {
    DelayingTqSnapshotBuilder shared_builder;
    TqApplyService service1(&store, &shared_builder);
    TqApplyService service2(&store, &shared_builder);

    TqApplyRequest req1;
    req1.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/api1\" {} }";
    req1.expected_base_identity = "";
    req1.mode = TqApplyMode::DryRun;
    req1.strict_production = false;

    TqApplyRequest req2;
    req2.source = "policy \"my-policy\" { route \"r2\" when path prefix \"/api2\" {} }";
    req2.expected_base_identity = "";
    req2.mode = TqApplyMode::DryRun;
    req2.strict_production = false;

    std::thread t1([&]() { service1.execute(req1); });
    std::thread t2([&]() { service2.execute(req2); });

    t1.join();
    t2.join();

    EXPECT_EQ(shared_builder.max_inflight(), 1);
}

TEST_F(TqApplyServiceTest, RejectedNoChanges) {
    TqApplyService service(&store);

    // Source that yields exactly the same policy (initial_route on /initial)
    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
                 "\"/initial\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedNoChanges);
    EXPECT_EQ(res.candidate_policy_identity, initial_identity);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

TEST_F(TqApplyServiceTest, DryRunNoChanges) {
    TqApplyService service(&store);

    // DryRun with same/no-op policy
    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
                 "\"/initial\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::DryRun;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::DryRunReady);
    EXPECT_EQ(res.candidate_policy_identity, initial_identity);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

TEST_F(TqApplyServiceTest, DryRunSuccess) {
    TqApplyService service(&store);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"new_route\" when path prefix "
                 "\"/new-path\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::DryRun;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok) << res.message;
    EXPECT_EQ(res.status, TqApplyStatus::DryRunReady);
    EXPECT_FALSE(res.candidate_policy_identity.empty());
    EXPECT_NE(res.candidate_policy_identity, initial_identity);

    // DryRun shouldn't mutate store's active snapshot nor increment generation!
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

TEST_F(TqApplyServiceTest, ApplySuccess) {
    TqApplyService service(&store);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"new_route\" when path prefix "
                 "\"/new-path\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok) << res.message;
    EXPECT_EQ(res.status, TqApplyStatus::Applied);
    EXPECT_FALSE(res.applied_policy_identity.empty());
    EXPECT_NE(res.applied_policy_identity, initial_identity);
    EXPECT_EQ(res.after_generation, 2u);

    // Verify store has indeed swapped
    auto current = store.load();
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->policy_identity, res.applied_policy_identity);
    EXPECT_EQ(current->generation, 2u);
}

TEST_F(TqApplyServiceTest, ConcurrencySequentialFifo) {
    TqApplyService service(&store);

    constexpr int kThreadCount = 10;
    std::vector<std::thread> threads;
    std::vector<TqApplyResult> results(kThreadCount);

    // Since each successful apply changes the active policy_identity (and the generation),
    // and each apply expects the previous state's policy_identity, if we run them completely
    // concurrently, only one will win the CAS, and others will fail CAS. This is the desired CAS
    // safety behavior! Let's verify that concurrency is thread-safe and the store's state is
    // consistent.
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&service, &results, i, this]() {
            TqApplyRequest req;
            req.source = "policy \"my-policy\" against sha \"\" { route \"route_" +
                         std::to_string(i) + "\" when path prefix \"/path_" + std::to_string(i) +
                         "\" {} }";
            // We'll pass the initial identity. Exactly one should succeed, and others should fail
            // CAS.
            req.expected_base_identity = initial_identity;
            req.mode = TqApplyMode::Apply;
            req.strict_production = false;

            results[i] = service.execute(req);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    int success_count = 0;
    int cas_mismatch_count = 0;

    for (const auto& res : results) {
        if (res.ok && res.status == TqApplyStatus::Applied) {
            success_count++;
        } else if (!res.ok && res.status == TqApplyStatus::RejectedCasMismatch) {
            cas_mismatch_count++;
        }
    }

    EXPECT_EQ(success_count, 1);
    EXPECT_EQ(cas_mismatch_count, kThreadCount - 1);
}

} // namespace bytetaper::taperquery
