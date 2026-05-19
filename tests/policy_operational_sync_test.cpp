// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/policy_operational_diff.h"
#include "operational/route_epoch_sync.h"
#include "runtime/route_cache_epoch_store.h"
#include "taperquery/policy_ir_identity.h"

#include <gtest/gtest.h>

using namespace bytetaper::operational;
using namespace bytetaper::runtime;
using namespace bytetaper::taperquery;

namespace {

TqPolicyDocument make_route_doc(const std::string& route_id) {
    TqPolicyDocument doc;
    doc.document_id = "sync-test";
    doc.source_name = "test";
    doc.generation = 1;
    doc.schema_version_num = 1;
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";

    TqRoutePolicy route;
    route.route_id = route_id;
    route.match_prefix = "/api";
    route.match_kind = TqRouteMatchKind::Prefix;
    route.mutation = TqMutationMode::Disabled;
    route.allowed_method = TqHttpMethod::Get;
    doc.routes.push_back(route);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

} // namespace

TEST(PolicyOperationalSyncTest, NoPolicyDiffHasNoAffectedRoutes) {
    const TqPolicyDocument doc = make_route_doc("route-a");
    const auto diff = compute_policy_operational_diff(doc, doc);
    EXPECT_EQ(diff.affected_routes, 0u);
}

TEST(PolicyOperationalSyncTest, FieldAllowlistChangeRequiresEpochBump) {
    TqPolicyDocument before = make_route_doc("route-a");
    before.routes[0].field_filter.mode = TqFieldFilterMode::Allowlist;
    before.routes[0].field_filter.fields = { "id" };
    before.policy_id = compute_policy_document_identity(before);

    TqPolicyDocument after = before;
    after.routes[0].field_filter.fields = { "id", "secret_token" };
    after.policy_id = compute_policy_document_identity(after);

    RouteCacheEpochStore epoch_store{};
    route_cache_epoch_register(&epoch_store, "route-a");

    TqApplyPlanOptions opts{};
    opts.include_field_level_changes = true;
    const auto diff = compute_policy_operational_diff(before, after, opts);
    ASSERT_GT(diff.affected_routes, 0u);

    const auto epoch_res = sync_route_epochs_for_apply(diff.plan, &epoch_store);
    ASSERT_TRUE(epoch_res.ok);
    ASSERT_EQ(epoch_res.routes.size(), 1u);
    EXPECT_GT(epoch_res.routes[0].new_epoch, epoch_res.routes[0].old_epoch);
}

TEST(PolicyOperationalSyncTest, CacheTtlChangeRequiresEpochBump) {
    TqPolicyDocument before = make_route_doc("route-a");
    before.routes[0].cache.enabled = true;
    before.routes[0].cache.behavior = TqCacheBehavior::Store;
    before.routes[0].cache.ttl_ms = duration_seconds(60);
    before.policy_id = compute_policy_document_identity(before);

    TqPolicyDocument after = before;
    after.routes[0].cache.ttl_ms = duration_seconds(120);
    after.policy_id = compute_policy_document_identity(after);

    RouteCacheEpochStore epoch_store{};
    route_cache_epoch_register(&epoch_store, "route-a");

    TqApplyPlanOptions opts{};
    opts.include_field_level_changes = true;
    const auto diff = compute_policy_operational_diff(before, after, opts);
    ASSERT_GT(diff.affected_routes, 0u);

    const auto epoch_res = sync_route_epochs_for_apply(diff.plan, &epoch_store);
    ASSERT_TRUE(epoch_res.ok);
    ASSERT_EQ(epoch_res.routes.size(), 1u);
    EXPECT_GT(epoch_res.routes[0].new_epoch, epoch_res.routes[0].old_epoch);
}
