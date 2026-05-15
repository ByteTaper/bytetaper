// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::cache {

class CacheKeyRouteEpochTest : public ::testing::Test {
protected:
    char buf[1024];

    CacheKeyInput make_input() {
        CacheKeyInput ki{};
        ki.method = policy::HttpMethod::Get;
        ki.route_id = "route-1";
        ki.path = "/api/v1/resource";
        ki.policy_version = "v1";
        return ki;
    }
};

TEST_F(CacheKeyRouteEpochTest, IncludesEpochWhenReady) {
    auto ki = make_input();
    ki.route_cache_epoch = 7;
    ki.route_cache_epoch_ready = true;

    EXPECT_TRUE(build_cache_key(ki, buf, sizeof(buf)));
    // Format: GET|{route_id}|epoch:{N}|{path}|...
    EXPECT_TRUE(std::strstr(buf, "route-1|epoch:7|/api/v1/resource") != nullptr);
}

TEST_F(CacheKeyRouteEpochTest, DifferentEpochProducesDifferentKey) {
    auto ki7 = make_input();
    ki7.route_cache_epoch = 7;
    ki7.route_cache_epoch_ready = true;
    char buf7[1024];
    build_cache_key(ki7, buf7, sizeof(buf7));

    auto ki8 = make_input();
    ki8.route_cache_epoch = 8;
    ki8.route_cache_epoch_ready = true;
    char buf8[1024];
    build_cache_key(ki8, buf8, sizeof(buf8));

    EXPECT_STRNE(buf7, buf8);
}

TEST_F(CacheKeyRouteEpochTest, VariantKeyIncludesEpochWhenReady) {
    auto ki = make_input();
    ki.variant = true;
    ki.route_cache_epoch = 42;
    ki.route_cache_epoch_ready = true;

    EXPECT_TRUE(build_cache_key(ki, buf, sizeof(buf)));
    EXPECT_TRUE(std::strstr(buf, "var:GET|route-1|epoch:42|") != nullptr);
}

TEST_F(CacheKeyRouteEpochTest, DifferentEpochProducesDifferentVariantKey) {
    auto ki = make_input();
    ki.variant = true;
    ki.route_cache_epoch_ready = true;

    char buf7[1024];
    ki.route_cache_epoch = 7;
    build_cache_key(ki, buf7, sizeof(buf7));

    char buf8[1024];
    ki.route_cache_epoch = 8;
    build_cache_key(ki, buf8, sizeof(buf8));

    EXPECT_STRNE(buf7, buf8);
    EXPECT_TRUE(std::strstr(buf7, "epoch:7") != nullptr);
    EXPECT_TRUE(std::strstr(buf8, "epoch:8") != nullptr);
}

TEST_F(CacheKeyRouteEpochTest, RawAndVariantUseSameEpoch) {
    auto ki = make_input();
    ki.route_cache_epoch = 42;
    ki.route_cache_epoch_ready = true;

    char raw_buf[1024];
    ki.variant = false;
    build_cache_key(ki, raw_buf, sizeof(raw_buf));

    char var_buf[1024];
    ki.variant = true;
    build_cache_key(ki, var_buf, sizeof(var_buf));

    // Both should have "epoch:42"
    EXPECT_TRUE(std::strstr(raw_buf, "epoch:42") != nullptr);
    EXPECT_TRUE(std::strstr(var_buf, "epoch:42") != nullptr);

    // Variant has var: prefix but the rest of the epoch segment should be identical
    // raw: GET|route-1|epoch:42|...
    // var: var:GET|route-1|epoch:42|...
    EXPECT_TRUE(std::strstr(var_buf, "GET|route-1|epoch:42|") != nullptr);
}

TEST_F(CacheKeyRouteEpochTest, VariantKeyStillSortsAndDeduplicatesFields) {
    auto ki = make_input();
    ki.variant = true;
    ki.route_cache_epoch = 7;
    ki.route_cache_epoch_ready = true;

    char fields[3][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields[0], "b", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields[1], "a", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields[2], "b", policy::kMaxFieldNameLen - 1);
    ki.selected_fields = fields;
    ki.selected_field_count = 3;

    EXPECT_TRUE(build_cache_key(ki, buf, sizeof(buf)));
    // Should contain epoch and sorted/deduped fields "a,b"
    EXPECT_TRUE(std::strstr(buf, "epoch:7") != nullptr);
    EXPECT_TRUE(std::strstr(buf, "|a,b|") != nullptr);
}

TEST_F(CacheKeyRouteEpochTest, PreservesOldBehaviorWhenEpochNotReady) {
    auto ki = make_input();
    ki.route_cache_epoch = 7; // ignored because ready=false
    ki.route_cache_epoch_ready = false;

    EXPECT_TRUE(build_cache_key(ki, buf, sizeof(buf)));
    EXPECT_TRUE(std::strstr(buf, "epoch:") == nullptr);
    EXPECT_STREQ(buf, "GET|route-1|/api/v1/resource|||v1");
}

TEST_F(CacheKeyRouteEpochTest, EpochDoesNotReplacePolicyVersion) {
    auto ki = make_input();
    ki.route_cache_epoch = 99;
    ki.route_cache_epoch_ready = true;
    ki.policy_version = "custom-ver";

    EXPECT_TRUE(build_cache_key(ki, buf, sizeof(buf)));
    EXPECT_TRUE(std::strstr(buf, "epoch:99") != nullptr);
    EXPECT_TRUE(std::strstr(buf, "|custom-ver") != nullptr);
}

} // namespace bytetaper::cache
