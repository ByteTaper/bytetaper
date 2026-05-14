// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/policy_snapshot.h"
#include "taperquery/tq_apply_service.h"
#include "taperquery_test_helpers.h"

#include <atomic>
#include <gtest/gtest.h>
#include <latch>
#include <thread>
#include <vector>

namespace bytetaper::taperquery {

class TaperqueryApplyConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        uint64_t initial_gen = store.next_generation();
        TqPolicyDocument init_doc;
        init_doc.document_id = "initial-doc";
        TqRoutePolicy r;
        r.route_id = "initial_r";
        r.match_prefix = "/initial";
        r.match_kind = TqRouteMatchKind::Prefix;
        init_doc.routes.push_back(r);

        auto res = runtime::build_runtime_policy_snapshot_from_ir(init_doc, initial_gen);
        ASSERT_TRUE(res.ok);
        initial_identity = res.snapshot->policy_identity;
        std::string err;
        ASSERT_TRUE(store.install_initial(res.snapshot, &err));
    }

    runtime::RuntimePolicyStore store;
    std::string initial_identity;
};

TEST_F(TaperqueryApplyConcurrencyTest, SameBaseConcurrentApply) {
    TqApplyService service(&store);
    std::latch sync_latch(2);
    TqApplyResult res1, res2;

    auto task = [&](int id, TqApplyResult* out_res) {
        TqApplyRequest req;
        req.source = "policy \"my-policy\" { route \"r_" + std::to_string(id) +
                     "\" when path prefix \"/path_" + std::to_string(id) + "\" {} }";
        req.expected_base_identity = initial_identity;
        req.mode = TqApplyMode::Apply;

        sync_latch.arrive_and_wait();

        *out_res = service.execute(req);
    };

    std::thread t1(task, 1, &res1);
    std::thread t2(task, 2, &res2);

    t1.join();
    t2.join();

    int applied_count = 0;
    int mismatch_count = 0;
    std::string successful_identity;

    if (res1.ok && res1.status == TqApplyStatus::Applied) {
        applied_count++;
        successful_identity = res1.applied_policy_identity;
    } else if (!res1.ok && res1.status == TqApplyStatus::RejectedCasMismatch) {
        mismatch_count++;
    }

    if (res2.ok && res2.status == TqApplyStatus::Applied) {
        applied_count++;
        successful_identity = res2.applied_policy_identity;
    } else if (!res2.ok && res2.status == TqApplyStatus::RejectedCasMismatch) {
        mismatch_count++;
    }

    EXPECT_EQ(applied_count, 1);
    EXPECT_EQ(mismatch_count, 1);
    EXPECT_EQ(store.load()->policy_identity, successful_identity);
}

TEST_F(TaperqueryApplyConcurrencyTest, SequentialApplyWithUpdatedBase) {
    TqApplyService service(&store);

    // Apply B
    TqApplyRequest req1;
    req1.source = "policy \"my-policy\" { route \"r_B\" when path prefix \"/path_B\" {} }";
    req1.expected_base_identity = initial_identity;
    req1.mode = TqApplyMode::Apply;

    auto res1 = service.execute(req1);
    ASSERT_TRUE(res1.ok);
    EXPECT_EQ(res1.status, TqApplyStatus::Applied);

    // Apply C expected B
    TqApplyRequest req2;
    req2.source = "policy \"my-policy\" { route \"r_C\" when path prefix \"/path_C\" {} }";
    req2.expected_base_identity = res1.applied_policy_identity;
    req2.mode = TqApplyMode::Apply;

    auto res2 = service.execute(req2);
    ASSERT_TRUE(res2.ok);
    EXPECT_EQ(res2.status, TqApplyStatus::Applied);

    auto final_snap = store.load();
    ASSERT_NE(final_snap, nullptr);
    EXPECT_EQ(final_snap->policy_identity, res2.applied_policy_identity);
    EXPECT_EQ(final_snap->generation, 3u);
}

TEST_F(TaperqueryApplyConcurrencyTest, StaleRequestAfterTwoApplies) {
    TqApplyService service(&store);

    // Apply B
    TqApplyRequest req1;
    req1.source = "policy \"my-policy\" { route \"r_B\" when path prefix \"/path_B\" {} }";
    req1.expected_base_identity = initial_identity;
    req1.mode = TqApplyMode::Apply;
    auto res1 = service.execute(req1);
    ASSERT_TRUE(res1.ok);

    // Apply C
    TqApplyRequest req2;
    req2.source = "policy \"my-policy\" { route \"r_C\" when path prefix \"/path_C\" {} }";
    req2.expected_base_identity = res1.applied_policy_identity;
    req2.mode = TqApplyMode::Apply;
    auto res2 = service.execute(req2);
    ASSERT_TRUE(res2.ok);

    // Stale D expected initial_identity
    TqApplyRequest stale_req;
    stale_req.source = "policy \"my-policy\" { route \"r_D\" when path prefix \"/path_D\" {} }";
    stale_req.expected_base_identity = initial_identity;
    stale_req.mode = TqApplyMode::Apply;

    auto stale_res = service.execute(stale_req);
    EXPECT_FALSE(stale_res.ok);
    EXPECT_EQ(stale_res.status, TqApplyStatus::RejectedCasMismatch);

    auto current_snap = store.load();
    ASSERT_NE(current_snap, nullptr);
    EXPECT_EQ(current_snap->policy_identity, res2.applied_policy_identity);
}

TEST_F(TaperqueryApplyConcurrencyTest, ApplyLockProtectsCasCheck) {
    TqApplyService service(&store);
    constexpr int kThreads = 10;
    std::latch start_latch(kThreads);
    std::atomic<int> success_count{ 0 };

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            TqApplyRequest req;
            req.source = "policy \"my-policy\" { route \"r_" + std::to_string(i) +
                         "\" when path prefix \"/path_" + std::to_string(i) + "\" {} }";
            req.expected_base_identity = initial_identity;
            req.mode = TqApplyMode::Apply;

            start_latch.arrive_and_wait();

            auto res = service.execute(req);
            if (res.ok && res.status == TqApplyStatus::Applied) {
                success_count++;
            }
        });
    }

    for (auto& t : threads)
        t.join();

    EXPECT_LE(success_count.load(), 1);
}

} // namespace bytetaper::taperquery
