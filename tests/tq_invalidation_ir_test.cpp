// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_validator.h"
#include "taperquery/policy_ir_version.h"

#include <gtest/gtest.h>

namespace bytetaper::taperquery {

TEST(TaperQueryInvalidationIrTest, ValidConfiguration) {
    TqPolicyValidationOptions options;
    options.collect_all = true;
    options.strict_production = false;
    options.include_warnings = false;

    TqPolicyDocument doc;
    doc.document_id = "valid_doc";
    doc.version.source_schema_version = "tq/v1";
    doc.version.policy_ir_version = std::string(kCurrentPolicyIrVersion);
    doc.version.identity_version = std::string(kCurrentPolicyIdentityVersion);

    TqRoutePolicy r1;
    r1.route_id = "target_route";
    r1.match_prefix = "/api/cacheable";
    r1.allowed_method = TqHttpMethod::Get;
    r1.mutation = TqMutationMode::Disabled;
    r1.cache.enabled = true;
    r1.cache.behavior = TqCacheBehavior::Store;
    r1.cache.ttl_ms = 30000;
    r1.cache.l1.enabled = true;
    r1.cache.l1.capacity_entries = 1000;

    TqRoutePolicy r2;
    r2.route_id = "mutation_route";
    r2.match_prefix = "/api/mutate";
    r2.allowed_method = TqHttpMethod::Delete;
    r2.mutation = TqMutationMode::Disabled;
    r2.cache.invalidation.enabled = true;
    r2.cache.invalidation.on_methods = { "DELETE" };
    r2.cache.invalidation.success_status_min = 200;
    r2.cache.invalidation.success_status_max = 299;

    TqCacheInvalidationTarget target{};
    target.route_id = "target_route";
    target.strategy = TqCacheInvalidationStrategy::RouteEpoch;
    r2.cache.invalidation.targets.push_back(target);

    doc.routes = { r1, r2 };

    auto result = validate_taperquery_policy_ir(doc, options);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.issues.empty());
}

TEST(TaperQueryInvalidationIrTest, MissingOnMethodsInvalid) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqRoutePolicy route;
    route.route_id = "mutate_route";
    route.match_prefix = "/api/mutate";
    route.allowed_method = TqHttpMethod::Delete;
    route.cache.invalidation.enabled = true;
    route.cache.invalidation.on_methods.clear(); // empty

    auto result = validate_taperquery_route_policy_ir(route, options);
    EXPECT_FALSE(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.code == "INVALIDATION_MISSING_METHODS")
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryInvalidationIrTest, InvalidOnMethodInvalid) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqRoutePolicy route;
    route.route_id = "mutate_route";
    route.match_prefix = "/api/mutate";
    route.allowed_method = TqHttpMethod::Delete;
    route.cache.invalidation.enabled = true;
    route.cache.invalidation.on_methods = { "GET" };

    auto result = validate_taperquery_route_policy_ir(route, options);
    EXPECT_FALSE(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.code == "INVALIDATION_INVALID_METHOD")
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryInvalidationIrTest, DefinedOnNonMutationRouteInvalid) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqRoutePolicy route;
    route.route_id = "get_route";
    route.match_prefix = "/api/get";
    route.allowed_method = TqHttpMethod::Get;
    route.cache.invalidation.enabled = true;
    route.cache.invalidation.on_methods = { "DELETE" };
    route.cache.invalidation.targets.push_back(
        { "some_target", TqCacheInvalidationStrategy::RouteEpoch });

    auto result = validate_taperquery_route_policy_ir(route, options);
    EXPECT_FALSE(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.code == "INVALIDATION_ON_NON_MUTATION_ROUTE")
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryInvalidationIrTest, DefinedOnAnyMethodRouteInvalid) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqRoutePolicy route;
    route.route_id = "any_route";
    route.match_prefix = "/api/any";
    route.allowed_method = TqHttpMethod::Any;
    route.cache.invalidation.enabled = true;
    route.cache.invalidation.on_methods = { "DELETE" };
    route.cache.invalidation.targets.push_back(
        { "some_target", TqCacheInvalidationStrategy::RouteEpoch });

    auto result = validate_taperquery_route_policy_ir(route, options);
    EXPECT_FALSE(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.code == "INVALIDATION_ON_NON_MUTATION_ROUTE")
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryInvalidationIrTest, TargetNotFoundInvalid) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqPolicyDocument doc;
    doc.version.source_schema_version = "tq/v1";
    doc.version.policy_ir_version = std::string(kCurrentPolicyIrVersion);
    doc.version.identity_version = std::string(kCurrentPolicyIdentityVersion);

    TqRoutePolicy r;
    r.route_id = "mutate_route";
    r.match_prefix = "/api/mutate";
    r.allowed_method = TqHttpMethod::Delete;
    r.cache.invalidation.enabled = true;
    r.cache.invalidation.on_methods = { "DELETE" };
    r.cache.invalidation.targets.push_back(
        { "nonexistent_target", TqCacheInvalidationStrategy::RouteEpoch });

    doc.routes = { r };

    auto result = validate_taperquery_policy_ir(doc, options);
    EXPECT_FALSE(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.code == "INVALIDATION_TARGET_NOT_FOUND")
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryInvalidationIrTest, TargetNotCacheableInvalid) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqPolicyDocument doc;
    doc.version.source_schema_version = "tq/v1";
    doc.version.policy_ir_version = std::string(kCurrentPolicyIrVersion);
    doc.version.identity_version = std::string(kCurrentPolicyIdentityVersion);

    TqRoutePolicy r1;
    r1.route_id = "target_route";
    r1.match_prefix = "/api/nocache";
    r1.allowed_method = TqHttpMethod::Get;
    r1.cache.enabled = false;

    TqRoutePolicy r2;
    r2.route_id = "mutate_route";
    r2.match_prefix = "/api/mutate";
    r2.allowed_method = TqHttpMethod::Delete;
    r2.cache.invalidation.enabled = true;
    r2.cache.invalidation.on_methods = { "DELETE" };
    r2.cache.invalidation.targets.push_back(
        { "target_route", TqCacheInvalidationStrategy::RouteEpoch });

    doc.routes = { r1, r2 };

    auto result = validate_taperquery_policy_ir(doc, options);
    EXPECT_FALSE(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.code == "INVALIDATION_TARGET_NOT_CACHEABLE")
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryInvalidationIrTest, TargetNotGetInvalid) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqPolicyDocument doc;
    doc.version.source_schema_version = "tq/v1";
    doc.version.policy_ir_version = std::string(kCurrentPolicyIrVersion);
    doc.version.identity_version = std::string(kCurrentPolicyIdentityVersion);

    TqRoutePolicy r1;
    r1.route_id = "target_any";
    r1.match_prefix = "/api/target_any";
    r1.allowed_method = TqHttpMethod::Any;
    r1.cache.enabled = true;
    r1.cache.behavior = TqCacheBehavior::Store;
    r1.cache.ttl_ms = 30000;
    r1.cache.l1.enabled = true;
    r1.cache.l1.capacity_entries = 1000;

    TqRoutePolicy r2;
    r2.route_id = "mutate_route";
    r2.match_prefix = "/api/mutate";
    r2.allowed_method = TqHttpMethod::Delete;
    r2.cache.invalidation.enabled = true;
    r2.cache.invalidation.on_methods = { "DELETE" };
    r2.cache.invalidation.targets.push_back(
        { "target_any", TqCacheInvalidationStrategy::RouteEpoch });

    doc.routes = { r1, r2 };

    auto result = validate_taperquery_policy_ir(doc, options);
    EXPECT_FALSE(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.code == "INVALIDATION_TARGET_NOT_GET")
            found = true;
    }
    EXPECT_TRUE(found);
}

} // namespace bytetaper::taperquery
