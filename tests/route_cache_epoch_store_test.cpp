// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/route_cache_epoch_store.h"

#include <gtest/gtest.h>

namespace bytetaper::runtime {

class RouteCacheEpochStoreTest : public ::testing::Test {
protected:
    RouteCacheEpochStore store;

    void SetUp() override {
        store.count = 0;
    }
};

TEST_F(RouteCacheEpochStoreTest, RegisterRouteStartsAtInitialEpoch) {
    EXPECT_EQ(route_cache_epoch_register(&store, "route-1"), RouteCacheEpochResult::Ok);

    std::uint64_t epoch = 0;
    EXPECT_EQ(route_cache_epoch_get(&store, "route-1", &epoch), RouteCacheEpochResult::Ok);
    EXPECT_EQ(epoch, kInitialRouteCacheEpoch);
}

TEST_F(RouteCacheEpochStoreTest, GetUnknownRouteReturnsNotFound) {
    std::uint64_t epoch = 0;
    EXPECT_EQ(route_cache_epoch_get(&store, "unknown", &epoch), RouteCacheEpochResult::NotFound);
}

TEST_F(RouteCacheEpochStoreTest, BumpRouteIncrementsEpoch) {
    route_cache_epoch_register(&store, "route-1");

    std::uint64_t new_epoch = 0;
    EXPECT_EQ(route_cache_epoch_bump(&store, "route-1", &new_epoch), RouteCacheEpochResult::Ok);
    EXPECT_EQ(new_epoch, kInitialRouteCacheEpoch + 1);

    std::uint64_t current = 0;
    route_cache_epoch_get(&store, "route-1", &current);
    EXPECT_EQ(current, kInitialRouteCacheEpoch + 1);
}

TEST_F(RouteCacheEpochStoreTest, BumpUnknownRouteReturnsNotFound) {
    EXPECT_EQ(route_cache_epoch_bump(&store, "unknown", nullptr), RouteCacheEpochResult::NotFound);
}

TEST_F(RouteCacheEpochStoreTest, RegisterDuplicateRouteIsIdempotent) {
    route_cache_epoch_register(&store, "route-1");
    route_cache_epoch_bump(&store, "route-1", nullptr);

    // Registering again should NOT reset the epoch
    EXPECT_EQ(route_cache_epoch_register(&store, "route-1"), RouteCacheEpochResult::Ok);

    std::uint64_t current = 0;
    route_cache_epoch_get(&store, "route-1", &current);
    EXPECT_EQ(current, kInitialRouteCacheEpoch + 1);
}

TEST_F(RouteCacheEpochStoreTest, NullArgumentsReturnInvalidArgument) {
    EXPECT_EQ(route_cache_epoch_register(nullptr, "id"), RouteCacheEpochResult::InvalidArgument);
    EXPECT_EQ(route_cache_epoch_register(&store, nullptr), RouteCacheEpochResult::InvalidArgument);
    EXPECT_EQ(route_cache_epoch_get(nullptr, "id", nullptr),
              RouteCacheEpochResult::InvalidArgument);
}

TEST_F(RouteCacheEpochStoreTest, CapacityExceededReturnsCapacityExceeded) {
    for (std::size_t i = 0; i < kMaxRouteCacheEpochEntries; ++i) {
        std::string id = "route-" + std::to_string(i);
        EXPECT_EQ(route_cache_epoch_register(&store, id.c_str()), RouteCacheEpochResult::Ok);
    }

    EXPECT_EQ(route_cache_epoch_register(&store, "one-too-many"),
              RouteCacheEpochResult::CapacityExceeded);
}

TEST_F(RouteCacheEpochStoreTest, ResetForTestsSetsExplicitValue) {
    route_cache_epoch_register(&store, "route-1");
    EXPECT_EQ(route_cache_epoch_reset_for_tests(&store, "route-1", 12345),
              RouteCacheEpochResult::Ok);

    std::uint64_t current = 0;
    route_cache_epoch_get(&store, "route-1", &current);
    EXPECT_EQ(current, 12345);
}

} // namespace bytetaper::runtime
