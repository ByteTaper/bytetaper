// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "operational/policy_activation_barrier.h"
#include "policy/route_policy.h"
#include "runtime/policy_snapshot.h"
#include "runtime/route_cache_epoch_store.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"
#include "taperquery/tq_cache_namespace_versioning.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace fs = std::filesystem;
using namespace bytetaper::cache;
using namespace bytetaper::control_plane;
using namespace bytetaper::policy;
using namespace bytetaper::operational;
using namespace bytetaper::runtime;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_activation_test_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqRoutePolicy make_route(const std::string& route_id,
                         const std::vector<std::string>& filter_fields = {}) {
    TqRoutePolicy route;
    route.route_id = route_id;
    route.match_prefix = "/api/" + route_id;
    route.match_kind = TqRouteMatchKind::Prefix;
    route.mutation = TqMutationMode::Disabled;
    route.allowed_method = TqHttpMethod::Get;
    if (!filter_fields.empty()) {
        route.field_filter.mode = TqFieldFilterMode::Allowlist;
        route.field_filter.fields = filter_fields;
    }
    return route;
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation,
                                 const std::vector<std::string>& filter_fields = {}) {
    TqPolicyDocument doc;
    doc.document_id = "activation-test";
    doc.source_name = "test";
    doc.generation = generation;
    doc.schema_version_num = 1;
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.routes.push_back(make_route(route_id, filter_fields));
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

TqPolicyDocument make_policy_doc_with_routes(std::uint64_t generation,
                                             std::vector<TqRoutePolicy> routes) {
    TqPolicyDocument doc;
    doc.document_id = "activation-test";
    doc.source_name = "test";
    doc.generation = generation;
    doc.schema_version_num = 1;
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.routes = std::move(routes);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

RuntimePolicySnapshotBuildResult failing_snapshot_build(const TqPolicyDocument&, std::uint64_t) {
    RuntimePolicySnapshotBuildResult result{};
    result.ok = false;
    result.error = "injected snapshot build failure";
    return result;
}

std::string canonical_hash_for_yaml(const std::string& yaml) {
    return "sha256:" + compute_canonical_yaml_sha256_hex(yaml);
}

void store_version(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                   const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    PolicyVersionRecord version;
    version.generation = generation;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    version.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);
}

void promote_active(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                    const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    ActivePolicyPointer pointer;
    pointer.generation = generation;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    pointer.version_key = make_version_key(key, generation);
    pointer.yaml_key = make_yaml_key(key, generation);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = generation > 1 ? generation - 1 : 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

class RecordingCleanupQueue : public RouteCacheCleanupQueue {
public:
    void enqueue(const RouteCacheCleanupJob& job) override {
        jobs_.push_back(job);
    }

    std::vector<RouteCacheCleanupJob> jobs_;
};

class PolicyActivationBarrierTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db(db_path_);
        store_.emplace(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        key_ = PolicyResourceKey::default_runtime();

        RuntimePolicySnapshotBuildResult initial_build =
            build_runtime_policy_snapshot_from_ir(make_policy_doc("route-a", 1, { "id" }), 1);
        ASSERT_TRUE(initial_build.ok);
        ASSERT_TRUE(runtime_store_.install_initial(initial_build.snapshot, nullptr));

        l1_init(&l1_cache_);
        route_cache_epoch_register(&epoch_store_, "route-a");

        config_.policy_state_store = &*store_;
        config_.runtime_policy_store = &runtime_store_;
        config_.route_cache_epoch_store = &epoch_store_;
        config_.l1_cache = &l1_cache_;
        config_.l2_cleanup_queue = &cleanup_queue_;
        config_.resource_key = key_;
    }

    void TearDown() override {
        store_.reset();
        destroy_db(db_path_);
    }

    std::string db_path_;
    PolicyResourceKey key_;
    std::optional<RocksDBPolicyStateStore> store_;
    RuntimePolicyStore runtime_store_;
    RouteCacheEpochStore epoch_store_{};
    L1Cache l1_cache_{};
    RecordingCleanupQueue cleanup_queue_;
    PolicyActivationBarrierConfig config_;
};

TEST_F(PolicyActivationBarrierTest, ActivateUsesCommittedPolicyIrWithoutStoreVersion) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id" });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    const RuntimePolicySnapshotBuildResult gen2_build =
        build_runtime_policy_snapshot_from_ir(gen2, 2);
    ASSERT_TRUE(gen2_build.ok);
    ASSERT_NE(gen2_build.snapshot, nullptr);

    PolicyActivationBarrier barrier(config_);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;
    request.committed_policy_ir = &gen2;
    request.committed_snapshot = gen2_build.snapshot;

    const PolicyActivationResult result = barrier.activate(request);
    ASSERT_TRUE(result.ok) << result.message;

    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 2u);
    EXPECT_EQ(active->policy_identity, gen2.policy_id);
}

TEST_F(PolicyActivationBarrierTest, SuccessfulActivationSwapsSnapshot) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id" });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);

    PolicyActivationBarrier barrier(config_);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.status == PolicyActivationStatus::PolicyActive ||
                result.status == PolicyActivationStatus::PolicyActiveCleanupPending);
    EXPECT_EQ(result.stage, PolicyActivationStage::Activated);

    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 2u);
}

TEST_F(PolicyActivationBarrierTest, FieldReductionBumpsEpochBeforeSwap) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id", "secret_token" });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id" });
    store_version(*store_, key_, gen2, 2);

    std::uint64_t epoch_before = 0;
    ASSERT_EQ(route_cache_epoch_get(&epoch_store_, "route-a", &epoch_before),
              RouteCacheEpochResult::Ok);

    PolicyActivationBarrier barrier(config_);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_GT(result.bumped_route_epochs, 0u);

    std::uint64_t epoch_after = 0;
    ASSERT_EQ(route_cache_epoch_get(&epoch_store_, "route-a", &epoch_after),
              RouteCacheEpochResult::Ok);
    EXPECT_GT(epoch_after, epoch_before);
}

TEST_F(PolicyActivationBarrierTest, EpochBumpFailurePreservesOldSnapshot) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id" });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);

    RouteCacheEpochStore broken_epoch_store{};
    PolicyActivationBarrierConfig broken_config = config_;
    broken_config.route_cache_epoch_store = &broken_epoch_store;

    PolicyActivationBarrier barrier(broken_config);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyActivationStatus::PolicyActivationFailed);
    EXPECT_TRUE(result.old_snapshot_still_active);

    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 1u);
}

TEST_F(PolicyActivationBarrierTest, EnqueuesL2CleanupBeforeSwap) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id", "secret_token" });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id" });
    store_version(*store_, key_, gen2, 2);

    cleanup_queue_.jobs_.clear();

    PolicyActivationBarrier barrier(config_);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_GT(result.l2_cleanup_jobs, 0u);
    EXPECT_FALSE(cleanup_queue_.jobs_.empty());
}

TEST_F(PolicyActivationBarrierTest, ServingSafeAfterFieldReductionInvalidatesStaleL1) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id", "email" });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    std::uint64_t epoch_before = 0;
    ASSERT_EQ(route_cache_epoch_get(&epoch_store_, "route-a", &epoch_before),
              RouteCacheEpochResult::Ok);

    const auto snap_before = runtime_store_.load();
    ASSERT_NE(snap_before, nullptr);
    const RoutePolicy* route_policy = nullptr;
    for (const auto& route : snap_before->routes) {
        if (route.route_id != nullptr && std::strcmp(route.route_id, "route-a") == 0) {
            route_policy = &route;
            break;
        }
    }
    ASSERT_NE(route_policy, nullptr);

    CacheKeyInput key_input{};
    key_input.method = HttpMethod::Get;
    key_input.route_id = "route-a";
    key_input.path = "/api";
    key_input.policy_version = route_policy->policy_identity;
    key_input.route_cache_epoch = epoch_before;
    key_input.route_cache_epoch_ready = true;

    char cache_key[kCacheKeyMaxLen];
    ASSERT_TRUE(build_cache_key(key_input, cache_key, sizeof(cache_key)));

    CacheEntry entry{};
    std::strncpy(entry.key, cache_key, kCacheKeyMaxLen - 1);
    entry.status_code = 200;
    entry.body = "{\"id\":1,\"email\":\"stale@example.com\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = 1000;
    entry.expires_at_epoch_ms = 9999999999LL;
    l1_put(&l1_cache_, entry);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id" });
    store_version(*store_, key_, gen2, 2);

    PolicyActivationBarrier barrier(config_);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    ASSERT_TRUE(result.ok) << result.message;

    std::uint64_t epoch_after = 0;
    ASSERT_EQ(route_cache_epoch_get(&epoch_store_, "route-a", &epoch_after),
              RouteCacheEpochResult::Ok);
    EXPECT_GT(epoch_after, epoch_before);

    const auto snap_after = runtime_store_.load();
    ASSERT_NE(snap_after, nullptr);
    const RoutePolicy* route_policy_after = nullptr;
    for (const auto& route : snap_after->routes) {
        if (route.route_id != nullptr && std::strcmp(route.route_id, "route-a") == 0) {
            route_policy_after = &route;
            break;
        }
    }
    ASSERT_NE(route_policy_after, nullptr);

    CacheKeyInput key_after{};
    key_after.method = HttpMethod::Get;
    key_after.route_id = "route-a";
    key_after.path = "/api";
    key_after.policy_version = route_policy_after->policy_identity;
    key_after.route_cache_epoch = epoch_after;
    key_after.route_cache_epoch_ready = true;

    char new_cache_key[kCacheKeyMaxLen];
    ASSERT_TRUE(build_cache_key(key_after, new_cache_key, sizeof(new_cache_key)));

    CacheEntry lookup_entry{};
    char lookup_body[256];
    const bool hit =
        l1_get(&l1_cache_, new_cache_key, 5000, &lookup_entry, lookup_body, sizeof(lookup_body));
    EXPECT_FALSE(hit);
}

TEST_F(PolicyActivationBarrierTest, RouteRemovedEnqueuesCleanup) {
    route_cache_epoch_register(&epoch_store_, "route-b");

    TqPolicyDocument gen1 = make_policy_doc_with_routes(
        1, { make_route("route-a", { "id" }), make_route("route-b", { "id" }) });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    TqPolicyDocument gen2 = make_policy_doc_with_routes(2, { make_route("route-a", { "id" }) });
    store_version(*store_, key_, gen2, 2);

    cleanup_queue_.jobs_.clear();

    PolicyActivationBarrier barrier(config_);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_GT(result.l2_cleanup_jobs, 0u);

    bool removed_route_cleanup = false;
    for (const auto& job : cleanup_queue_.jobs_) {
        if (job.route_id == "route-b") {
            removed_route_cleanup = true;
            break;
        }
    }
    EXPECT_TRUE(removed_route_cleanup);
}

TEST_F(PolicyActivationBarrierTest, MaterializedVariantChangeTriggersVariantInvalidation) {
    TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id" });
    gen1.routes[0].cache.enabled = true;
    gen1.routes[0].cache.behavior = TqCacheBehavior::Store;
    gen1.routes[0].cache.ttl_ms = duration_seconds(60);
    gen1.routes[0].cache.field_variant.enabled = true;
    gen1.routes[0].cache.field_variant.max_variants_per_route = 8;
    gen1.policy_id = compute_policy_document_identity(gen1);
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    TqPolicyDocument gen2 = gen1;
    gen2.generation = 2;
    gen2.routes[0].cache.field_variant.max_variants_per_route = 4;
    gen2.policy_id = compute_policy_document_identity(gen2);
    store_version(*store_, key_, gen2, 2);

    PolicyActivationBarrier barrier(config_);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_GT(result.bumped_route_epochs, 0u);
    EXPECT_GT(result.l1_cleanup_jobs, 0u);
}

TEST_F(PolicyActivationBarrierTest, SnapshotBuildFailurePreservesOldSnapshot) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1, { "id" });
    store_version(*store_, key_, gen1, 1);
    promote_active(*store_, key_, gen1, 1);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);

    PolicyActivationBarrierConfig failing_config = config_;
    failing_config.snapshot_build_fn = failing_snapshot_build;

    PolicyActivationBarrier barrier(failing_config);
    PolicyActivationRequest request{};
    request.generation = 2;
    request.policy_id = gen2.policy_id;
    request.previous_generation = 1;

    const PolicyActivationResult result = barrier.activate(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyActivationStatus::PolicyActivationFailed);
    EXPECT_EQ(result.stage, PolicyActivationStage::SnapshotBuilt);
    EXPECT_TRUE(result.old_snapshot_still_active);

    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 1u);
}

} // namespace
