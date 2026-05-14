// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "admin/taperquery_admin_http_server.h"
#include "runtime/policy_snapshot.h"
#include "taperquery/tq_apply_audit.h"
#include "taperquery/tq_apply_service.h"
#include "taperquery_test_helpers.h"

#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace bytetaper::taperquery {

class TaperqueryHotReloadIntegrationTest : public ::testing::Test {
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

        service = std::make_unique<TqApplyService>(&store, nullptr, &audit_store);

        admin::TaperQueryAdminHttpServerConfig config;
        config.listen_address = "127.0.0.1";
        config.port = 0;
        config.policy_store = &store;
        config.apply_service = service.get();
        config.audit_store = &audit_store;
        config.enable_taperquery_apply = true;

        ASSERT_TRUE(admin::start_taperquery_admin_http_server(config, &handle));
        ASSERT_NE(handle.bound_port, 0);
        bound_port = handle.bound_port;
    }

    void TearDown() override {
        admin::stop_taperquery_admin_http_server(&handle);
    }

    runtime::RuntimePolicyStore store;
    TqApplyAuditStore audit_store;
    std::unique_ptr<TqApplyService> service;
    admin::TaperQueryAdminHttpServerHandle handle;
    std::uint16_t bound_port = 0;
    std::string initial_identity;
};

TEST_F(TaperqueryHotReloadIntegrationTest, ApplyThenRouteMatchUsesNewSnapshot) {
    // 1. Initial snapshot does not match /new
    auto init_snap = store.load();
    const policy::RoutePolicy* matched =
        policy::match_route_compiled(init_snap->route_matcher, "/new/path", nullptr);
    EXPECT_EQ(matched, nullptr);

    // 2. Apply B with /new
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"new_r\" when path prefix \"/new\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service->execute(req);
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::Applied);

    // 3. New snapshot matches /new perfectly, and /initial/foo no longer matches
    auto new_snap = store.load();
    matched = policy::match_route_compiled(new_snap->route_matcher, "/new/path", nullptr);
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "new_r");

    const policy::RoutePolicy* old_matched =
        policy::match_route_compiled(new_snap->route_matcher, "/initial/foo", nullptr);
    EXPECT_EQ(old_matched, nullptr);
}

TEST_F(TaperqueryHotReloadIntegrationTest, InFlightStreamUsesOldSnapshotAfterSwap) {
    auto in_flight_snap = store.load();
    const policy::RoutePolicy* matched =
        policy::match_route_compiled(in_flight_snap->route_matcher, "/initial/foo", nullptr);
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "initial_r");

    // Hot swap via apply
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"replaced_r\" when path prefix \"/replaced\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service->execute(req);
    ASSERT_TRUE(res.ok);

    // Verify the in-flight stream's reference is still perfectly valid and points to initial_r
    EXPECT_STREQ(matched->route_id, "initial_r");
    EXPECT_STREQ(in_flight_snap->routes[0].route_id, "initial_r");

    // Load active snapshot and assert /replaced/foo matches replaced_r
    auto active_snap = store.load();
    const policy::RoutePolicy* active_matched =
        policy::match_route_compiled(active_snap->route_matcher, "/replaced/foo", nullptr);
    ASSERT_NE(active_matched, nullptr);
    EXPECT_STREQ(active_matched->route_id, "replaced_r");
}

TEST_F(TaperqueryHotReloadIntegrationTest, CurrentEndpointReflectsAppliedGeneration) {
    // 1. Check initial generation
    auto res1 = testing::admin_http_request(bound_port, "GET", "/admin/taperquery/policy/current");
    EXPECT_EQ(res1.status_code, 200);
    EXPECT_NE(res1.body.find("\"generation\":1"), std::string::npos);

    // 2. Apply new policy
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"new_r\" when path prefix \"/new\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    auto res = service->execute(req);
    ASSERT_TRUE(res.ok);

    // 3. Check generation is incremented to 2 and policy_identity matches applied
    auto res2 = testing::admin_http_request(bound_port, "GET", "/admin/taperquery/policy/current");
    EXPECT_EQ(res2.status_code, 200);
    EXPECT_NE(res2.body.find("\"generation\":2"), std::string::npos);
    EXPECT_NE(res2.body.find("\"policy_identity\":\"" + res.applied_policy_identity + "\""),
              std::string::npos);
}

TEST_F(TaperqueryHotReloadIntegrationTest, CurrentPolicyIncludesLatestApplySummary) {
    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"new_r\" when path prefix \"/new\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;
    auto res = service->execute(req);
    ASSERT_TRUE(res.ok);

    auto http_res =
        testing::admin_http_request(bound_port, "GET", "/admin/taperquery/policy/current");
    EXPECT_EQ(http_res.status_code, 200);
    EXPECT_NE(http_res.body.find("\"latest_apply\":{"), std::string::npos);
    EXPECT_NE(http_res.body.find("\"outcome\":\"Applied\""), std::string::npos);
}

} // namespace bytetaper::taperquery
