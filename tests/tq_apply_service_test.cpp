// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "runtime/policy_snapshot.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/l1_cache_lookup_stage.h"
#include "stages/l1_variant_lookup_stage.h"
#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_roundtrip.h"
#include "taperquery/tq_apply_audit.h"
#include "taperquery/tq_apply_service.h"

#include <chrono>
#include <filesystem>
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

TEST_F(TqApplyServiceTest, DryRunDoesNotMutateActivePolicy) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"new_r\" when path prefix \"/new\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::DryRun;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::DryRunReady);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
}

TEST_F(TqApplyServiceTest, ApplySuccessMutatesActivePolicy) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"new_r\" when path prefix \"/new\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::Applied);
    EXPECT_NE(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 2u);
}

TEST_F(TqApplyServiceTest, CasMismatchRejectsStaleApply) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"new_r\" when path prefix \"/new\" {} }";
    req.expected_base_identity = "bad_base_identity";
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCasMismatch);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
}

TEST_F(TqApplyServiceTest, ParseFailureDoesNotMutate) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { invalid_syntax ";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedParseError);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
}

TEST_F(TqApplyServiceTest, CompileFailureDoesNotMutate) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/\" {} route \"r1\" when "
                 "path prefix \"/\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCompileError);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
}

TEST_F(TqApplyServiceTest, SemanticValidationFailureDoesNotMutate) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/api\" { cache store ttl "
                 "60s { l1 enabled capacity 0 entries } } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedValidation);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
}

TEST_F(TqApplyServiceTest, NoOpApplyRejected) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
                 "\"/initial\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedNoChanges);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
}

TEST_F(TqApplyServiceTest, DryRunNoOpAllowed) {
    TqApplyService service(&store);
    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
                 "\"/initial\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::DryRun;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::DryRunReady);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
}

TEST_F(TqApplyServiceTest, ApplyUsesRoundTripIrForSnapshot) {
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

    auto current = store.load();
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->policy_identity, res.applied_policy_identity);
}

TEST_F(TqApplyServiceTest, DryRunExercisesRoundTrip) {
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
    EXPECT_EQ(store.load()->policy_identity, initial_identity); // No swap
}

TEST_F(TqApplyServiceTest, MetadataUsesPersistedIdentityAndCandidateIdentity) {
    std::string state_dir = "/tmp/tq_apply_persistence_test_dir";
    std::filesystem::remove_all(state_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = state_dir;

    TqApplyService service(&store, nullptr, nullptr, config);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"new_route\" when path prefix "
                 "\"/new-path\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok) << res.message;

    // Load persisted meta
    auto recovery = load_persisted_active_policy(config);
    ASSERT_TRUE(recovery.ok) << recovery.error;
    EXPECT_EQ(recovery.metadata.policy_identity, res.applied_policy_identity);
    EXPECT_EQ(recovery.metadata.persisted_policy_identity, res.applied_policy_identity);
    EXPECT_FALSE(recovery.metadata.candidate_policy_identity.empty());

    // Clean up
    std::filesystem::remove_all(state_dir);
}

TEST_F(TqApplyServiceTest, RejectedCanonicalYamlMismatch) {
    std::string state_dir = "/tmp/tq_apply_mismatch_test_dir";
    std::filesystem::remove_all(state_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = state_dir;

    TqApplyService service(&store, nullptr, nullptr, config);

    std::function<PolicyIrYamlRoundTripResult(const TqPolicyDocument&)> mock_hook =
        [](const TqPolicyDocument&) {
            PolicyIrYamlRoundTripResult r;
            r.ok = true;
            r.candidate_policy_identity = "candidate_123";
            r.persisted_policy_identity = "persisted_456"; // Mismatch!
            return r;
        };
    set_roundtrip_override_for_testing(&mock_hook);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"new_route\" when path prefix "
                 "\"/new-path\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCanonicalYamlMismatch);

    // Assert active snapshot remains unchanged
    EXPECT_EQ(store.load()->policy_identity, initial_identity);

    // Assert no files written
    EXPECT_FALSE(std::filesystem::exists(state_dir + "/active-policy.yaml"));
    EXPECT_FALSE(std::filesystem::exists(state_dir + "/active-policy.meta.json"));

    // Reset hook
    set_roundtrip_override_for_testing(nullptr);
    std::filesystem::remove_all(state_dir);
}

TEST_F(TqApplyServiceTest, RejectedCanonicalYamlRoundTripFailed) {
    std::string state_dir = "/tmp/tq_apply_roundtrip_fail_test_dir";
    std::filesystem::remove_all(state_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = state_dir;

    TqApplyService service(&store, nullptr, nullptr, config);

    std::function<PolicyIrYamlRoundTripResult(const TqPolicyDocument&)> mock_hook =
        [](const TqPolicyDocument&) {
            PolicyIrYamlRoundTripResult r;
            r.ok = false;
            r.error = "Mock roundtrip emission/parsing failure";
            return r;
        };
    set_roundtrip_override_for_testing(&mock_hook);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"new_route\" when path prefix "
                 "\"/new-path\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCanonicalYamlRoundTripFailed);

    // Assert active snapshot remains unchanged
    EXPECT_EQ(store.load()->policy_identity, initial_identity);

    // Assert no files written
    EXPECT_FALSE(std::filesystem::exists(state_dir + "/active-policy.yaml"));
    EXPECT_FALSE(std::filesystem::exists(state_dir + "/active-policy.meta.json"));

    // Reset hook
    set_roundtrip_override_for_testing(nullptr);
    std::filesystem::remove_all(state_dir);
}

TEST_F(TqApplyServiceTest, RestartLoadedIdentityParity) {
    std::string state_dir = "/tmp/tq_apply_restart_parity_test_dir";
    std::filesystem::remove_all(state_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = state_dir;

    TqApplyService service(&store, nullptr, nullptr, config);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"new_route\" when path prefix "
                 "\"/new-path\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok) << res.message;
    EXPECT_EQ(res.status, TqApplyStatus::Applied);

    // Use loader directly matching recovery startup config
    StartupPolicyLoadConfig load_config;
    load_config.policy_state_dir = state_dir;
    load_config.policy_persistence_enabled = true;
    load_config.fallback_to_bootstrap_on_persisted_policy_error = false;

    auto startup_res = load_startup_policy_with_persistence(load_config);
    ASSERT_TRUE(startup_res.ok) << startup_res.error;
    EXPECT_EQ(startup_res.policy_identity, res.applied_policy_identity);
    EXPECT_EQ(startup_res.generation, res.after_generation);

    std::filesystem::remove_all(state_dir);
}

TEST_F(TqApplyServiceTest, ApplyBumpsEpochForFieldFilteringChange) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");

    TqApplyService service(&store, nullptr, nullptr, {}, &epoch_store);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
                 "\"/initial\" { fields allow [\"id\", \"email\"]; } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    ASSERT_TRUE(res.ok) << res.message;
    EXPECT_EQ(res.status, TqApplyStatus::Applied);

    std::uint64_t current = 0;
    auto get_res = runtime::route_cache_epoch_get(&epoch_store, "initial_route", &current);
    ASSERT_EQ(get_res, runtime::RouteCacheEpochResult::Ok);
    EXPECT_EQ(current, 2u);
}

TEST_F(TqApplyServiceTest, DryRunDoesNotBumpEpoch) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");

    TqApplyService service(&store, nullptr, nullptr, {}, &epoch_store);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
                 "\"/initial\" { fields allow [\"id\", \"email\"]; } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::DryRun;
    req.strict_production = false;

    auto res = service.execute(req);
    ASSERT_TRUE(res.ok) << res.message;
    EXPECT_EQ(res.status, TqApplyStatus::DryRunReady);

    std::uint64_t current = 0;
    auto get_res = runtime::route_cache_epoch_get(&epoch_store, "initial_route", &current);
    ASSERT_EQ(get_res, runtime::RouteCacheEpochResult::Ok);
    EXPECT_EQ(current, 1u);

    ASSERT_EQ(res.cache_namespace_versioning.changed_routes.size(), 1u);
    EXPECT_TRUE(res.cache_namespace_versioning.changed_routes[0].epoch_bump_required);
}

TEST_F(TqApplyServiceTest, FailedApplyDoesNotBumpEpoch) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");

    TqApplyService service(&store, nullptr, nullptr, {}, &epoch_store);

    TqApplyRequest req;
    req.source =
        "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
        "\"/initial\" { fields allow []; } }"; // invalid empty allow list fails semantic validation
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    ASSERT_FALSE(res.ok);

    std::uint64_t current = 0;
    auto get_res = runtime::route_cache_epoch_get(&epoch_store, "initial_route", &current);
    ASSERT_EQ(get_res, runtime::RouteCacheEpochResult::Ok);
    EXPECT_EQ(current, 1u);
}

TEST_F(TqApplyServiceTest, SuccessfulApplyBumpsOnlyAffectedRoute) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");
    runtime::route_cache_epoch_register(&epoch_store, "other_route");

    TqApplyService service(&store, nullptr, nullptr, {}, &epoch_store);

    TqApplyRequest req;
    req.source =
        "policy \"my-policy\" against sha \"\" { route \"initial_route\" when path prefix "
        "\"/initial\" { fields allow [\"id\", \"email\"]; } route \"other_route\" when path prefix "
        "\"/other\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    ASSERT_TRUE(res.ok) << res.message;

    std::uint64_t current_initial = 0;
    runtime::route_cache_epoch_get(&epoch_store, "initial_route", &current_initial);
    EXPECT_EQ(current_initial, 2u);

    std::uint64_t current_other = 0;
    runtime::route_cache_epoch_get(&epoch_store, "other_route", &current_other);
    EXPECT_EQ(current_other, 1u);
}

TEST_F(TqApplyServiceTest, E2eCacheUnreachableAfterApply) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");

    RouteCacheCleanupQueueImpl cleanup_queue;
    cleanup_queue.start_worker();

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    TqApplyService service(&store, nullptr, &audit_store, persistence_config, &epoch_store,
                           &cleanup_queue);

    // Initial policy with cache enabled and allowed fields (id, email)
    TqApplyRequest req_init{};
    req_init.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when method "
                      "get path prefix "
                      "\"/initial\" { mutate full; cache store ttl 60s { l1 enabled capacity 1000 "
                      "entries } fields allow [\"id\", \"email\"]; } }";
    req_init.expected_base_identity = initial_identity;
    req_init.mode = TqApplyMode::Apply;
    req_init.strict_production = false;

    auto res_init = service.execute(req_init);
    ASSERT_TRUE(res_init.ok) << res_init.message;
    std::string base_sha = res_init.applied_policy_identity;

    // Get epoch before apply
    std::uint64_t epoch_before = 0;
    runtime::route_cache_epoch_get(&epoch_store, "initial_route", &epoch_before);
    EXPECT_EQ(epoch_before, 2u);

    // Initialize mock L1 Cache
    auto l1_cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1_cache.get());

    auto active_snap_init = store.load();
    const policy::RoutePolicy* matched_policy_init = nullptr;
    for (const auto& r : active_snap_init->routes) {
        if (r.route_id != nullptr && std::strcmp(r.route_id, "initial_route") == 0) {
            matched_policy_init = &r;
            break;
        }
    }
    ASSERT_NE(matched_policy_init, nullptr);

    // Generate cache key for epoch 2
    cache::CacheKeyInput input{};
    input.method = policy::HttpMethod::Get;
    input.route_id = "initial_route";
    input.path = "/initial";
    input.policy_version = matched_policy_init->policy_identity;
    input.route_cache_epoch = epoch_before;
    input.route_cache_epoch_ready = true;

    char key_before[cache::kCacheKeyMaxLen];
    ASSERT_TRUE(cache::build_cache_key(input, key_before, sizeof(key_before)));

    // Store a cached entry containing the 'email' field under epoch 2
    cache::CacheEntry entry{};
    std::strncpy(entry.key, key_before, cache::kCacheKeyMaxLen - 1);
    entry.status_code = 200;
    entry.body = "{\"id\":123,\"email\":\"old@example.com\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = 1000;
    entry.expires_at_epoch_ms = 9999999999LL;
    cache::l1_put(l1_cache.get(), entry);

    // Simulate pipeline lookup before update: it should result in a CACHE HIT
    {
        auto active_snap = store.load();
        const policy::RoutePolicy* matched_policy = nullptr;
        for (const auto& r : active_snap->routes) {
            if (r.route_id != nullptr && std::strcmp(r.route_id, "initial_route") == 0) {
                matched_policy = &r;
                break;
            }
        }
        ASSERT_NE(matched_policy, nullptr);

        apg::ApgTransformContext ctx{};
        ctx.matched_policy = matched_policy;
        ctx.l1_cache = l1_cache.get();
        ctx.route_cache_epoch_store = &epoch_store;
        ctx.request_method = policy::HttpMethod::Get;
        ctx.request_epoch_ms = 5000;
        std::strncpy(ctx.raw_path, "/initial", sizeof(ctx.raw_path) - 1);

        auto prep_out = stages::cache_key_prepare_stage(ctx);
        EXPECT_EQ(prep_out.result, apg::StageResult::Continue);
        EXPECT_TRUE(ctx.cache_key_ready);

        auto lookup_out = stages::l1_cache_lookup_stage(ctx);
        EXPECT_EQ(lookup_out.result, apg::StageResult::SkipRemaining);
        EXPECT_STREQ(lookup_out.note, "l1-hit");
        EXPECT_TRUE(ctx.cache_hit);
        EXPECT_STREQ(ctx.cached_response.body, "{\"id\":123,\"email\":\"old@example.com\"}");
    }

    // Now apply a policy change that modifies the allowed fields: restricting allowed fields to
    // just 'id' (triggers FieldFilteringBehavior, bumping epoch to 3)
    TqApplyRequest req_update{};
    req_update.source = "policy \"my-policy\" against sha \"\" { route \"initial_route\" when "
                        "method get path prefix "
                        "\"/initial\" { mutate full; cache store ttl 60s { l1 enabled capacity "
                        "1000 entries } fields allow [\"id\"]; } }";
    req_update.expected_base_identity = base_sha;
    req_update.mode = TqApplyMode::Apply;
    req_update.strict_production = false;

    auto res_update = service.execute(req_update);
    ASSERT_TRUE(res_update.ok) << res_update.message;

    // Verify epoch has been bumped dynamically by TqApplyService
    std::uint64_t epoch_after = 0;
    runtime::route_cache_epoch_get(&epoch_store, "initial_route", &epoch_after);
    EXPECT_EQ(epoch_after, 3u);

    // Simulate pipeline lookup after update: it must result in a CACHE MISS (email won't be
    // served!)
    {
        auto active_snap = store.load();
        const policy::RoutePolicy* matched_policy = nullptr;
        for (const auto& r : active_snap->routes) {
            if (r.route_id != nullptr && std::strcmp(r.route_id, "initial_route") == 0) {
                matched_policy = &r;
                break;
            }
        }
        ASSERT_NE(matched_policy, nullptr);

        apg::ApgTransformContext ctx{};
        ctx.matched_policy = matched_policy;
        ctx.l1_cache = l1_cache.get();
        ctx.route_cache_epoch_store = &epoch_store;
        ctx.request_method = policy::HttpMethod::Get;
        ctx.request_epoch_ms = 5000;
        std::strncpy(ctx.raw_path, "/initial", sizeof(ctx.raw_path) - 1);

        auto prep_out = stages::cache_key_prepare_stage(ctx);
        EXPECT_EQ(prep_out.result, apg::StageResult::Continue);
        EXPECT_TRUE(ctx.cache_key_ready);

        auto lookup_out = stages::l1_cache_lookup_stage(ctx);
        EXPECT_EQ(lookup_out.result, apg::StageResult::Continue);
        EXPECT_STREQ(lookup_out.note, "l1-miss");
        EXPECT_FALSE(ctx.cache_hit);
    }

    // Prove Gap 3: Assert the durable cleanup queue enqueued and processed both epoch updates
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let async thread run
    auto completed = cleanup_queue.get_completed_jobs();
    ASSERT_EQ(completed.size(), 2);
    bool found_target_job = false;
    for (const auto& job : completed) {
        if (job.old_epoch == 2 && job.new_epoch == 3) {
            found_target_job = true;
            EXPECT_EQ(job.route_id, "initial_route");
        }
    }
    EXPECT_TRUE(found_target_job);
}

TEST_F(TqApplyServiceTest, TaperQueryApplyBumpsEpochForVariantCacheChange) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");

    RouteCacheCleanupQueueImpl cleanup_queue;
    cleanup_queue.start_worker();

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    TqApplyService service(&store, nullptr, &audit_store, persistence_config, &epoch_store,
                           &cleanup_queue);

    // Initial policy with cache enabled, allowed fields (id, email), and field_variant enabled!
    TqApplyRequest req_init{};
    req_init.source =
        "policy \"my-policy\" against sha \"\" { route \"initial_route\" when method get path "
        "prefix "
        "\"/initial\" { mutate full; cache store ttl 60s { l1 enabled capacity 1000 entries "
        "field_variant { enabled true max_variants_per_route 16 min_field_count 1 max_field_count "
        "16 admission_threshold 3 ttl_max 500s } } fields allow [\"id\", \"email\"]; } }";
    req_init.expected_base_identity = initial_identity;
    req_init.mode = TqApplyMode::Apply;
    req_init.strict_production = false;

    auto res_init = service.execute(req_init);
    ASSERT_TRUE(res_init.ok) << res_init.message;
    std::string base_sha = res_init.applied_policy_identity;

    std::uint64_t epoch_before = 0;
    runtime::route_cache_epoch_get(&epoch_store, "initial_route", &epoch_before);
    EXPECT_EQ(epoch_before, 2u);

    // Initialize mock L1 Cache
    auto l1_cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1_cache.get());

    auto active_snap_init = store.load();
    const policy::RoutePolicy* matched_policy_init = nullptr;
    for (const auto& r : active_snap_init->routes) {
        if (r.route_id != nullptr && std::strcmp(r.route_id, "initial_route") == 0) {
            matched_policy_init = &r;
            break;
        }
    }
    ASSERT_NE(matched_policy_init, nullptr);

    // Prepare a variant cache key input under epoch 2
    char fields[2][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields[1], "email", policy::kMaxFieldNameLen - 1);

    cache::CacheKeyInput input{};
    input.method = policy::HttpMethod::Get;
    input.route_id = "initial_route";
    input.path = "/initial";
    input.policy_version = matched_policy_init->policy_identity;
    input.route_cache_epoch = epoch_before;
    input.route_cache_epoch_ready = true;
    input.variant = true;
    input.selected_fields = fields;
    input.selected_field_count = 2;

    char key_before[cache::kCacheKeyMaxLen];
    ASSERT_TRUE(cache::build_cache_key(input, key_before, sizeof(key_before)));

    // Store variant entry in L1 Cache
    cache::CacheEntry entry{};
    std::strncpy(entry.key, key_before, cache::kCacheKeyMaxLen - 1);
    entry.status_code = 200;
    entry.body = "{\"id\":123,\"email\":\"variant@example.com\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = 1000;
    entry.expires_at_epoch_ms = 9999999999LL;
    cache::l1_put(l1_cache.get(), entry);

    // Pipeline lookup before update: it must result in a VARIANT CACHE HIT!
    {
        auto active_snap = store.load();
        const policy::RoutePolicy* matched_policy = nullptr;
        for (const auto& r : active_snap->routes) {
            if (r.route_id != nullptr && std::strcmp(r.route_id, "initial_route") == 0) {
                matched_policy = &r;
                break;
            }
        }
        ASSERT_NE(matched_policy, nullptr);

        apg::ApgTransformContext ctx{};
        ctx.matched_policy = matched_policy;
        ctx.l1_cache = l1_cache.get();
        ctx.route_cache_epoch_store = &epoch_store;
        ctx.request_method = policy::HttpMethod::Get;
        ctx.request_epoch_ms = 5000;
        std::strncpy(ctx.raw_path, "/initial", sizeof(ctx.raw_path) - 1);
        std::strncpy(ctx.selected_fields[0], "id", policy::kMaxFieldNameLen - 1);
        std::strncpy(ctx.selected_fields[1], "email", policy::kMaxFieldNameLen - 1);
        ctx.selected_field_count = 2;

        auto prep_out = stages::cache_key_prepare_stage(ctx);
        EXPECT_EQ(prep_out.result, apg::StageResult::Continue);
        EXPECT_TRUE(ctx.variant_cache_key_ready);

        auto lookup_out = stages::l1_variant_lookup_stage(ctx);
        EXPECT_EQ(lookup_out.result, apg::StageResult::SkipRemaining);
        EXPECT_STREQ(lookup_out.note, "l1-variant-hit");
        EXPECT_TRUE(ctx.cache_hit);
        EXPECT_STREQ(ctx.cached_response.body, "{\"id\":123,\"email\":\"variant@example.com\"}");
    }

    // Now, apply the update that restricts allowed fields to just 'id' (removing 'email')
    TqApplyRequest req_update{};
    req_update.source =
        "policy \"my-policy\" against sha \"\" { route \"initial_route\" when method get path "
        "prefix "
        "\"/initial\" { mutate full; cache store ttl 60s { l1 enabled capacity 1000 entries "
        "field_variant { enabled true max_variants_per_route 16 min_field_count 1 max_field_count "
        "16 admission_threshold 3 ttl_max 500s } } fields allow [\"id\"]; } }";
    req_update.expected_base_identity = base_sha;
    req_update.mode = TqApplyMode::Apply;
    req_update.strict_production = false;

    auto res_update = service.execute(req_update);
    ASSERT_TRUE(res_update.ok) << res_update.message;

    // Verify epoch was bumped dynamically
    std::uint64_t epoch_after = 0;
    runtime::route_cache_epoch_get(&epoch_store, "initial_route", &epoch_after);
    EXPECT_EQ(epoch_after, 3u);

    // Pipeline lookup after update: it must result in a CACHE MISS (old variant response is
    // unreachable!)
    {
        auto active_snap = store.load();
        const policy::RoutePolicy* matched_policy = nullptr;
        for (const auto& r : active_snap->routes) {
            if (r.route_id != nullptr && std::strcmp(r.route_id, "initial_route") == 0) {
                matched_policy = &r;
                break;
            }
        }
        ASSERT_NE(matched_policy, nullptr);

        apg::ApgTransformContext ctx{};
        ctx.matched_policy = matched_policy;
        ctx.l1_cache = l1_cache.get();
        ctx.route_cache_epoch_store = &epoch_store;
        ctx.request_method = policy::HttpMethod::Get;
        ctx.request_epoch_ms = 5000;
        std::strncpy(ctx.raw_path, "/initial", sizeof(ctx.raw_path) - 1);
        std::strncpy(ctx.selected_fields[0], "id", policy::kMaxFieldNameLen - 1);
        std::strncpy(ctx.selected_fields[1], "email", policy::kMaxFieldNameLen - 1);
        ctx.selected_field_count = 2;

        auto prep_out = stages::cache_key_prepare_stage(ctx);
        EXPECT_EQ(prep_out.result, apg::StageResult::Continue);
        EXPECT_TRUE(ctx.variant_cache_key_ready);

        auto lookup_out = stages::l1_variant_lookup_stage(ctx);
        EXPECT_EQ(lookup_out.result, apg::StageResult::Continue);
        EXPECT_STREQ(lookup_out.note, "l1-miss");
        EXPECT_FALSE(ctx.cache_hit);
    }
}

TEST_F(TqApplyServiceTest, TaperQueryApplyEnqueuesCleanupForRemovedRoute) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    TqApplyService service(&store, nullptr, &audit_store, persistence_config, &epoch_store);

    // Initial policy with two routes
    TqApplyRequest req_init{};
    req_init.source = "policy \"my-policy\" against sha \"\" { "
                      "route \"initial_route\" when method get path prefix \"/initial\" { mutate "
                      "full; cache store ttl 60s { l1 enabled capacity 1000 entries } } "
                      "route \"other_route\" when method get path prefix \"/other\" { mutate full; "
                      "cache store ttl 60s { l1 enabled capacity 1000 entries } } }";
    req_init.expected_base_identity = initial_identity;
    req_init.mode = TqApplyMode::Apply;
    req_init.strict_production = false;

    auto res_init = service.execute(req_init);
    ASSERT_TRUE(res_init.ok) << res_init.message;
    std::string base_sha = res_init.applied_policy_identity;

    // Apply update that removes other_route
    TqApplyRequest req_update{};
    req_update.source = "policy \"my-policy\" against sha \"\" { "
                        "route \"initial_route\" when method get path prefix \"/initial\" { mutate "
                        "full; cache store ttl 60s { l1 enabled capacity 1000 entries } } }";
    req_update.expected_base_identity = base_sha;
    req_update.mode = TqApplyMode::Apply;
    req_update.strict_production = false;

    auto res_update = service.execute(req_update);
    ASSERT_TRUE(res_update.ok) << res_update.message;

    // Assert that the cleanup job for other_route was enqueued
    bool found_cleanup = false;
    for (const auto& clean : res_update.enqueued_cleanups) {
        if (clean == "route:other_route:epoch:2" || clean == "route:other_route:epoch:1") {
            found_cleanup = true;
        }
    }
    EXPECT_TRUE(found_cleanup);
}

TEST_F(TqApplyServiceTest, DryRunDoesNotMutateEpochOrClean) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");
    runtime::route_cache_epoch_reset_for_tests(&epoch_store, "initial_route", 5);

    auto l1_cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1_cache.get());

    cache::CacheEntry e{};
    std::strncpy(e.key, "GET|initial_route|epoch:5|/initial|||policy_v1", sizeof(e.key) - 1);
    e.body = "body";
    e.body_len = 4;
    cache::l1_put(l1_cache.get(), e);

    RouteCacheCleanupQueueImpl cleanup_queue;
    cleanup_queue.start_worker();

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    TqApplyService service(&store, nullptr, &audit_store, persistence_config, &epoch_store,
                           &cleanup_queue, l1_cache.get());

    // Execute in DryRun mode
    TqApplyRequest req{};
    req.source = "policy \"my-policy\" against sha \"\" { "
                 "route \"initial_route\" when method get path prefix \"/initial\" { mutate "
                 "full; cache store ttl 60s { l1 enabled capacity 1000 entries } } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::DryRun;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::DryRunReady);

    // Verify epoch was NOT bumped (remains 5)
    std::uint64_t current_epoch = 0;
    EXPECT_EQ(runtime::route_cache_epoch_get(&epoch_store, "initial_route", &current_epoch),
              runtime::RouteCacheEpochResult::Ok);
    EXPECT_EQ(current_epoch, 5);

    // Verify L1 cache entry was NOT cleaned up
    cache::CacheEntry out{};
    char body_buf[128];
    EXPECT_TRUE(cache::l1_get(l1_cache.get(), "GET|initial_route|epoch:5|/initial|||policy_v1", 0,
                              &out, body_buf, sizeof(body_buf)));

    // Verify L2 queue has 0 completed jobs
    cleanup_queue.shutdown();
    EXPECT_EQ(cleanup_queue.get_completed_jobs().size(), 0);
}

TEST_F(TqApplyServiceTest, FailedApplyDoesNotMutateEpochOrClean) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");
    runtime::route_cache_epoch_reset_for_tests(&epoch_store, "initial_route", 5);

    auto l1_cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1_cache.get());

    cache::CacheEntry e{};
    std::strncpy(e.key, "GET|initial_route|epoch:5|/initial|||policy_v1", sizeof(e.key) - 1);
    e.body = "body";
    e.body_len = 4;
    cache::l1_put(l1_cache.get(), e);

    RouteCacheCleanupQueueImpl cleanup_queue;
    cleanup_queue.start_worker();

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    TqApplyService service(&store, nullptr, &audit_store, persistence_config, &epoch_store,
                           &cleanup_queue, l1_cache.get());

    // Execute with wrong expected_base_identity (should fail CAS mismatch)
    TqApplyRequest req{};
    req.source = "policy \"my-policy\" against sha \"\" { "
                 "route \"initial_route\" when method get path prefix \"/initial\" { mutate "
                 "full; cache store ttl 60s { l1 enabled capacity 1000 entries } } }";
    req.expected_base_identity = "wrong_sha";
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCasMismatch);

    // Verify epoch was NOT bumped (remains 5)
    std::uint64_t current_epoch = 0;
    EXPECT_EQ(runtime::route_cache_epoch_get(&epoch_store, "initial_route", &current_epoch),
              runtime::RouteCacheEpochResult::Ok);
    EXPECT_EQ(current_epoch, 5);

    // Verify L1 cache entry was NOT cleaned up
    cache::CacheEntry out{};
    char body_buf[128];
    EXPECT_TRUE(cache::l1_get(l1_cache.get(), "GET|initial_route|epoch:5|/initial|||policy_v1", 0,
                              &out, body_buf, sizeof(body_buf)));

    // Verify L2 queue has 0 completed jobs
    cleanup_queue.shutdown();
    EXPECT_EQ(cleanup_queue.get_completed_jobs().size(), 0);
}

TEST_F(TqApplyServiceTest, EpochBumpFailureAbortsSnapshotSwap) {
    runtime::RouteCacheEpochStore epoch_store{};

    // Fill the epoch store completely to trigger CapacityExceeded on any new registration
    for (std::size_t i = 0; i < runtime::kMaxRouteCacheEpochEntries; ++i) {
        std::string route_id = "route_" + std::to_string(i);
        ASSERT_EQ(runtime::route_cache_epoch_register(&epoch_store, route_id.c_str()),
                  runtime::RouteCacheEpochResult::Ok);
    }

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    TqApplyService service(&store, nullptr, &audit_store, persistence_config, &epoch_store);

    std::string initial_identity_before = store.load()->policy_identity;

    // Apply a policy that adds a new route ("new_route"), which will fail registration
    TqApplyRequest req{};
    req.source = "policy \"my-policy\" against sha \"" + initial_identity_before +
                 "\" { "
                 "route \"new_route\" when method get path prefix \"/new\" { mutate disabled } }";
    req.expected_base_identity = initial_identity_before;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::InternalError);
    EXPECT_NE(res.message.find("Failed to register added route: new_route"), std::string::npos);

    // Verify the snapshot in the store was NOT swapped
    EXPECT_EQ(store.load()->policy_identity, initial_identity_before);
}

class CasMismatchTriggerBuilder : public TqSnapshotBuilder {
public:
    CasMismatchTriggerBuilder(runtime::RuntimePolicyStore* store,
                              const std::string& initial_identity)
        : store_(store), initial_identity_(initial_identity) {}

    runtime::RuntimePolicySnapshotBuildResult build_snapshot(const TqPolicyDocument& policy_ir,
                                                             std::uint64_t generation) override {
        // Trigger the concurrent snapshot update during the final build
        if (generation > 1 && !triggered_) {
            triggered_ = true;
            TqPolicyDocument dummy_doc;
            dummy_doc.document_id = "dummy";
            dummy_doc.version.source_schema_version = "tq/v1";
            auto dummy_res = runtime::build_runtime_policy_snapshot_from_ir(dummy_doc, generation);
            if (dummy_res.ok) {
                std::string err;
                // Swapping snapshot concurrently so the apply service's CAS swap fails
                store_->swap_if_current(initial_identity_, dummy_res.snapshot, &err);
            }
        }
        return runtime::build_runtime_policy_snapshot_from_ir(policy_ir, generation);
    }

private:
    runtime::RuntimePolicyStore* store_;
    std::string initial_identity_;
    bool triggered_ = false;
};

TEST_F(TqApplyServiceTest, SwapFailureRollsBackEpochStore) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");
    runtime::route_cache_epoch_reset_for_tests(&epoch_store, "initial_route", 5);

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    // Use our custom builder that triggers a CAS mismatch failure
    CasMismatchTriggerBuilder builder(&store, initial_identity);
    TqApplyService service(&store, &builder, &audit_store, persistence_config, &epoch_store);

    TqApplyRequest req{};
    req.source = "policy \"my-policy\" against sha \"" + initial_identity +
                 "\" { "
                 "route \"initial_route\" when method get path prefix \"/initial\" { mutate "
                 "full; cache store ttl 60s { l1 enabled capacity 1000 entries } } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCasMismatch);

    // Verify epoch was rolled back to 5 due to CAS swap failure
    std::uint64_t current_epoch = 0;
    EXPECT_EQ(runtime::route_cache_epoch_get(&epoch_store, "initial_route", &current_epoch),
              runtime::RouteCacheEpochResult::Ok);
    EXPECT_EQ(current_epoch, 5);
}

TEST_F(TqApplyServiceTest, DryRunReportsWouldCleanup) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");
    runtime::route_cache_epoch_reset_for_tests(&epoch_store, "initial_route", 5);

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    TqApplyService service(&store, nullptr, &audit_store, persistence_config, &epoch_store);

    TqApplyRequest req{};
    req.source = "policy \"my-policy\" against sha \"" + initial_identity +
                 "\" { "
                 "route \"initial_route\" when method get path prefix \"/initial\" { mutate "
                 "full; cache store ttl 60s { l1 enabled capacity 1000 entries } } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::DryRun;
    req.strict_production = false;

    auto res = service.execute(req);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::DryRunReady);

    // Verify dry-run reports would_cleanup_l1 and would_cleanup_l2
    bool found = false;
    for (const auto& ch : res.cache_namespace_versioning.changed_routes) {
        if (ch.route_id == "initial_route") {
            found = true;
            EXPECT_TRUE(ch.epoch_bump_required);
            EXPECT_TRUE(ch.would_cleanup_l1);
            EXPECT_TRUE(ch.would_cleanup_l2);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(TqApplyServiceTest, SwapFailureRollsBackAddedRoutes) {
    runtime::RouteCacheEpochStore epoch_store{};
    runtime::route_cache_epoch_register(&epoch_store, "initial_route");
    runtime::route_cache_epoch_reset_for_tests(&epoch_store, "initial_route", 5);

    TqApplyAuditStore audit_store{};
    LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;

    // Use our custom builder that triggers a CAS mismatch failure
    CasMismatchTriggerBuilder builder(&store, initial_identity);
    TqApplyService service(&store, &builder, &audit_store, persistence_config, &epoch_store);

    TqApplyRequest req{};
    req.source =
        "policy \"my-policy\" against sha \"" + initial_identity +
        "\" { "
        "route \"initial_route\" when method get path prefix \"/initial\" { mutate disabled } "
        "route \"added_route\" when method get path prefix \"/added\" { mutate disabled } }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    req.strict_production = false;

    // Initially, there's only 1 registered route in epoch_store
    EXPECT_EQ(epoch_store.count, 1u);

    auto res = service.execute(req);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedCasMismatch);

    // Verify "added_route" was completely unregistered and epoch_store reverted back to count 1
    EXPECT_EQ(epoch_store.count, 1u);
    std::uint64_t current_epoch = 0;
    EXPECT_EQ(runtime::route_cache_epoch_get(&epoch_store, "added_route", &current_epoch),
              runtime::RouteCacheEpochResult::NotFound);
}

} // namespace bytetaper::taperquery
