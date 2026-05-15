// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/route_analysis.h"

#include <gtest/gtest.h>

using namespace bytetaper::taperquery;

TEST(TaperQueryRouteAnalysisTest, SpecificityScorePreservesRules) {
    TqRoutePolicy exact_route;
    exact_route.match_kind = TqRouteMatchKind::Exact;
    exact_route.match_prefix = "/api/v1/users";
    exact_route.allowed_method = TqHttpMethod::Get;

    TqRoutePolicy prefix_route;
    prefix_route.match_kind = TqRouteMatchKind::Prefix;
    prefix_route.match_prefix = "/api/v1/users";
    prefix_route.allowed_method = TqHttpMethod::Get;

    EXPECT_GT(compute_route_specificity_score(exact_route),
              compute_route_specificity_score(prefix_route));
}

TEST(TaperQueryRouteAnalysisTest, DuplicateRouteIdDetected) {
    TqRoutePolicy r1;
    r1.route_id = "duplicate-id";
    r1.match_prefix = "/a";
    TqRoutePolicy r2;
    r2.route_id = "duplicate-id";
    r2.match_prefix = "/b";

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_FALSE(report.ok);

    bool found = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::DuplicateRouteId) {
            found = true;
            EXPECT_EQ(f.route_id, "duplicate-id");
            EXPECT_EQ(f.severity, TqRouteAnalysisSeverity::Error);
        }
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryRouteAnalysisTest, ExactDuplicateMatchDetected) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api";
    r1.match_kind = TqRouteMatchKind::Exact;
    r1.allowed_method = TqHttpMethod::Get;
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api";
    r2.match_kind = TqRouteMatchKind::Exact;
    r2.allowed_method = TqHttpMethod::Get;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_FALSE(report.ok);

    bool found_duplicate = false;
    bool found_wins = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::DuplicateExactMatch) {
            found_duplicate = true;
            EXPECT_EQ(f.route_id, "r2");
            EXPECT_EQ(f.related_route_id, "r1");
        }
        if (f.kind == TqRouteAnalysisKind::EarlierRouteWins) {
            found_wins = true;
            EXPECT_EQ(f.route_id, "r2");
            EXPECT_EQ(f.related_route_id, "r1");
        }
    }
    EXPECT_TRUE(found_duplicate);
    EXPECT_TRUE(found_wins);
}

TEST(TaperQueryRouteAnalysisTest, PrefixShadowsPrefixDetected) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api";
    r1.match_kind = TqRouteMatchKind::Prefix;
    r1.allowed_method = TqHttpMethod::Get;
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/orders";
    r2.match_kind = TqRouteMatchKind::Prefix;
    r2.allowed_method = TqHttpMethod::Get;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_TRUE(report.ok); // Warnings don't cause report.ok to be false.

    bool found_shadow = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::PrefixShadowsPrefix) {
            found_shadow = true;
            EXPECT_EQ(f.route_id, "r2");
            EXPECT_EQ(f.related_route_id, "r1");
            EXPECT_EQ(f.severity, TqRouteAnalysisSeverity::Warning);
        }
    }
    EXPECT_TRUE(found_shadow);
    EXPECT_TRUE(report.precedence[0].may_shadow_later_routes);
    EXPECT_TRUE(report.precedence[1].may_be_shadowed);
}

TEST(TaperQueryRouteAnalysisTest, PrefixShadowsExactDetected) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api";
    r1.match_kind = TqRouteMatchKind::Prefix;
    r1.allowed_method = TqHttpMethod::Get;
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/orders/123";
    r2.match_kind = TqRouteMatchKind::Exact;
    r2.allowed_method = TqHttpMethod::Get;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_TRUE(report.ok);

    bool found_shadow = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::PrefixShadowsExact) {
            found_shadow = true;
            EXPECT_EQ(f.route_id, "r2");
            EXPECT_EQ(f.related_route_id, "r1");
            EXPECT_EQ(f.severity, TqRouteAnalysisSeverity::Warning);
        }
    }
    EXPECT_TRUE(found_shadow);
}

TEST(TaperQueryRouteAnalysisTest, SharedL2CachePathWarning) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.cache.enabled = true;
    r1.cache.l2.enabled = true;
    r1.cache.l2.path = "/var/db/cache";
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.cache.enabled = true;
    r2.cache.l2.enabled = true;
    r2.cache.l2.path = "/var/db/cache";

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_TRUE(report.ok);

    bool found_shared = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::SharedL2CachePath) {
            found_shared = true;
            EXPECT_EQ(f.route_id, "r2");
            EXPECT_EQ(f.related_route_id, "r1");
            EXPECT_EQ(f.severity, TqRouteAnalysisSeverity::Warning);
        }
    }
    EXPECT_TRUE(found_shared);
}

TEST(TaperQueryRouteAnalysisTest, NoFindingsForNonOverlappingRoutes) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api/v1/users";
    r1.match_kind = TqRouteMatchKind::Prefix;
    r1.allowed_method = TqHttpMethod::Get;

    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/v1/orders";
    r2.match_kind = TqRouteMatchKind::Prefix;
    r2.allowed_method = TqHttpMethod::Get;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.findings.empty());
}

TEST(TaperQueryRouteAnalysisTest, ExactGetAndExactPostDoNotConflict) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api";
    r1.match_kind = TqRouteMatchKind::Exact;
    r1.allowed_method = TqHttpMethod::Get;

    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api";
    r2.match_kind = TqRouteMatchKind::Exact;
    r2.allowed_method = TqHttpMethod::Post;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.findings.empty());
}

TEST(TaperQueryRouteAnalysisTest, ExactGetAndExactAnyConflict) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api";
    r1.match_kind = TqRouteMatchKind::Exact;
    r1.allowed_method = TqHttpMethod::Get;

    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api";
    r2.match_kind = TqRouteMatchKind::Exact;
    r2.allowed_method = TqHttpMethod::Any;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_FALSE(report.ok);

    bool found = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::DuplicateExactMatch ||
            f.kind == TqRouteAnalysisKind::EarlierRouteWins) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryRouteAnalysisTest, PrefixDoesNotShadowDifferentPrefixWords) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api";
    r1.match_kind = TqRouteMatchKind::Prefix;
    r1.allowed_method = TqHttpMethod::Get;

    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/apiary";
    r2.match_kind = TqRouteMatchKind::Prefix;
    r2.allowed_method = TqHttpMethod::Get;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.findings.empty());
}

TEST(TaperQueryRouteAnalysisTest, MoreSpecificRouteBeforeBroadRouteDoesNotWarn) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api/v1/users";
    r1.match_kind = TqRouteMatchKind::Prefix;
    r1.allowed_method = TqHttpMethod::Get;

    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api";
    r2.match_kind = TqRouteMatchKind::Prefix;
    r2.allowed_method = TqHttpMethod::Get;

    TqPolicyDocument doc;
    doc.routes = { r1, r2 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.findings.empty());
}

TEST(TaperQueryRouteAnalysisTest, DeclarationOrderIsPreserved) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    TqRoutePolicy r2;
    r2.route_id = "r2";
    TqRoutePolicy r3;
    r3.route_id = "r3";

    TqPolicyDocument doc;
    doc.routes = { r1, r2, r3 };

    auto report = analyze_taperquery_route_precedence(doc);
    EXPECT_EQ(report.precedence.size(), 3u);
    EXPECT_EQ(report.precedence[0].route_id, "r1");
    EXPECT_EQ(report.precedence[1].route_id, "r2");
    EXPECT_EQ(report.precedence[2].route_id, "r3");
}

TEST(TaperQueryRouteAnalysisTest, DetectsUnknownInvalidationTarget) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.cache.invalidation.enabled = true;
    r1.cache.invalidation.targets = { { "ghost", TqCacheInvalidationStrategy::RouteEpoch } };

    TqPolicyDocument doc;
    doc.routes = { r1 };

    auto report = analyze_taperquery_route_precedence(doc);
    bool found = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::UnknownInvalidationTarget) {
            found = true;
            EXPECT_EQ(f.route_id, "r1");
        }
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryRouteAnalysisTest, DetectsInvalidInvalidationTargetMethod) {
    TqRoutePolicy target;
    target.route_id = "target";
    target.allowed_method = TqHttpMethod::Post;

    TqRoutePolicy mutator;
    mutator.route_id = "mutator";
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.targets = { { "target", TqCacheInvalidationStrategy::RouteEpoch } };

    TqPolicyDocument doc;
    doc.routes = { target, mutator };

    auto report = analyze_taperquery_route_precedence(doc);
    bool found = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::InvalidInvalidationTargetMethod) {
            found = true;
            EXPECT_EQ(f.route_id, "mutator");
            EXPECT_EQ(f.related_route_id, "target");
        }
    }
    EXPECT_TRUE(found);
}

TEST(TaperQueryRouteAnalysisTest, DetectsInvalidInvalidationTargetCachePolicy) {
    TqRoutePolicy target;
    target.route_id = "target";
    target.allowed_method = TqHttpMethod::Get;
    target.cache.enabled = false;

    TqRoutePolicy mutator;
    mutator.route_id = "mutator";
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.targets = { { "target", TqCacheInvalidationStrategy::RouteEpoch } };

    {
        TqPolicyDocument doc;
        doc.routes = { target, mutator };

        auto report = analyze_taperquery_route_precedence(doc);
        bool found = false;
        for (const auto& f : report.findings) {
            if (f.kind == TqRouteAnalysisKind::InvalidInvalidationTargetCachePolicy) {
                found = true;
                EXPECT_EQ(f.route_id, "mutator");
                EXPECT_EQ(f.related_route_id, "target");
            }
        }
        EXPECT_TRUE(found);
    }

    // Test behavior = Default
    target.cache.enabled = true;
    target.cache.behavior = TqCacheBehavior::Default;
    {
        TqPolicyDocument doc;
        doc.routes = { target, mutator };

        auto report = analyze_taperquery_route_precedence(doc);
        bool found = false;
        for (const auto& f : report.findings) {
            if (f.kind == TqRouteAnalysisKind::InvalidInvalidationTargetCachePolicy) {
                found = true;
                EXPECT_EQ(f.route_id, "mutator");
                EXPECT_EQ(f.related_route_id, "target");
            }
        }
        EXPECT_TRUE(found);
    }
}

TEST(TaperQueryRouteAnalysisTest, DetectsUnsupportedInvalidationStrategy) {
    TqRoutePolicy target;
    target.route_id = "target";
    target.allowed_method = TqHttpMethod::Get;
    target.cache.enabled = true;

    TqRoutePolicy mutator;
    mutator.route_id = "mutator";
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.targets = { { "target", TqCacheInvalidationStrategy::ExactKey } };

    TqPolicyDocument doc;
    doc.routes = { target, mutator };

    auto report = analyze_taperquery_route_precedence(doc);
    bool found = false;
    for (const auto& f : report.findings) {
        if (f.kind == TqRouteAnalysisKind::UnsupportedInvalidationStrategy) {
            found = true;
            EXPECT_EQ(f.route_id, "mutator");
        }
    }
    EXPECT_TRUE(found);
}
