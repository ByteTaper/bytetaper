// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "hash/hash.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::cache {

class L1VariantInvalidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache = std::make_unique<L1Cache>();
        l1_init(cache.get());
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
    }

    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    std::unique_ptr<L1Cache> cache;
    char key_buf[1024];

    CacheKeyInput make_input(const char* route_id, std::uint64_t epoch, bool variant,
                             char (*fields_buf)[policy::kMaxFieldNameLen] = nullptr) {
        CacheKeyInput ki{};
        ki.method = policy::HttpMethod::Get;
        ki.route_id = route_id;
        ki.path = "/api/v1/resource";
        ki.policy_version = "v1";
        ki.route_cache_epoch = epoch;
        ki.route_cache_epoch_ready = true;
        ki.variant = variant;
        if (variant && fields_buf) {
            std::strncpy(fields_buf[0], "id", policy::kMaxFieldNameLen - 1);
            std::strncpy(fields_buf[1], "name", policy::kMaxFieldNameLen - 1);
            ki.selected_fields = fields_buf;
            ki.selected_field_count = 2;
        }
        return ki;
    }
};

TEST_F(L1VariantInvalidationTest, OldVariantDoesNotHitAfterEpochBump) {
    // 1. Store variant under epoch 7
    char fields7[2][policy::kMaxFieldNameLen] = {};
    auto ki7 = make_input("route-1", 7, true, fields7);
    ASSERT_TRUE(build_cache_key(ki7, key_buf, sizeof(key_buf)));
    std::string vkey7(key_buf);

    CacheEntry e7{};
    ::strncpy(e7.key, vkey7.c_str(), sizeof(e7.key) - 1);
    e7.body = "body-epoch-7";
    e7.body_len = 12;
    l1_put(cache.get(), e7);

    // Verify it hits with epoch 7
    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_TRUE(l1_get(cache.get(), vkey7.c_str(), 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_EQ(out.body_len, 12);
    body_buf[out.body_len] = '\0';
    EXPECT_STREQ(body_buf, "body-epoch-7");

    // 2. Bump epoch to 8
    char fields8[2][policy::kMaxFieldNameLen] = {};
    auto ki8 = make_input("route-1", 8, true, fields8);
    ASSERT_TRUE(build_cache_key(ki8, key_buf, sizeof(key_buf)));
    std::string vkey8(key_buf);

    EXPECT_STRNE(vkey7.c_str(), vkey8.c_str());

    // 3. Lookup with epoch 8 key -> MISS
    EXPECT_FALSE(l1_get(cache.get(), vkey8.c_str(), 0, &out, body_buf, sizeof(body_buf)));

    // 4. Old variant still physically present but unreachable by new epoch
    EXPECT_TRUE(l1_get(cache.get(), vkey7.c_str(), 0, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L1VariantInvalidationTest, NewVariantCanBeStoredAfterEpochBump) {
    // 1. Bump epoch to 8
    char fields8[2][policy::kMaxFieldNameLen] = {};
    auto ki8 = make_input("route-1", 8, true, fields8);
    ASSERT_TRUE(build_cache_key(ki8, key_buf, sizeof(key_buf)));
    std::string vkey8(key_buf);

    // 2. Store new variant under epoch 8
    CacheEntry e8{};
    ::strncpy(e8.key, vkey8.c_str(), sizeof(e8.key) - 1);
    e8.body = "body-epoch-8";
    e8.body_len = 12;
    l1_put(cache.get(), e8);

    // 3. Lookup epoch 8 -> HIT
    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_TRUE(l1_get(cache.get(), vkey8.c_str(), 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_EQ(out.body_len, 12);
    body_buf[out.body_len] = '\0';
    EXPECT_STREQ(body_buf, "body-epoch-8");
}

TEST_F(L1VariantInvalidationTest, RawAndVariantBothMissAfterEpochBump) {
    // 1. Store both raw + variant under epoch 7
    auto ki_raw7 = make_input("route-1", 7, false);
    build_cache_key(ki_raw7, key_buf, sizeof(key_buf));
    std::string rkey7(key_buf);
    CacheEntry er{};
    ::strncpy(er.key, rkey7.c_str(), sizeof(er.key) - 1);
    er.body = "raw-7";
    er.body_len = 5;
    l1_put(cache.get(), er);

    char fields7[2][policy::kMaxFieldNameLen] = {};
    auto ki_var7 = make_input("route-1", 7, true, fields7);
    build_cache_key(ki_var7, key_buf, sizeof(key_buf));
    std::string vkey7(key_buf);
    CacheEntry ev{};
    ::strncpy(ev.key, vkey7.c_str(), sizeof(ev.key) - 1);
    ev.body = "var-7";
    ev.body_len = 5;
    l1_put(cache.get(), ev);

    // 2. Bump to epoch 8
    auto ki_raw8 = make_input("route-1", 8, false);
    build_cache_key(ki_raw8, key_buf, sizeof(key_buf));
    std::string rkey8(key_buf);

    char fields8[2][policy::kMaxFieldNameLen] = {};
    auto ki_var8 = make_input("route-1", 8, true, fields8);
    build_cache_key(ki_var8, key_buf, sizeof(key_buf));
    std::string vkey8(key_buf);

    // 3. Both misses
    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_FALSE(l1_get(cache.get(), rkey8.c_str(), 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_FALSE(l1_get(cache.get(), vkey8.c_str(), 0, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L1VariantInvalidationTest, ExactVariantRemoveRemovesKnownVariantKey) {
    char fields7[2][policy::kMaxFieldNameLen] = {};
    auto ki = make_input("route-1", 7, true, fields7);
    build_cache_key(ki, key_buf, sizeof(key_buf));
    std::string vkey(key_buf);

    CacheEntry ev{};
    ::strncpy(ev.key, vkey.c_str(), sizeof(ev.key) - 1);
    ev.body = "var-7";
    ev.body_len = 5;
    l1_put(cache.get(), ev);

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    ASSERT_TRUE(l1_get(cache.get(), vkey.c_str(), 0, &out, body_buf, sizeof(body_buf)));

    // Exact remove
    l1_remove(cache.get(), vkey.c_str());

    EXPECT_FALSE(l1_get(cache.get(), vkey.c_str(), 0, &out, body_buf, sizeof(body_buf)));
}

} // namespace bytetaper::cache
