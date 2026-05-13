// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/policy_snapshot.h"

#include "policy/policy_identity.h"
#include "taperquery/policy_ir_identity.h"

#include <algorithm>
#include <cstring>

namespace bytetaper::runtime {

namespace {

taperquery::TqRoutePolicy convert_runtime_route_policy_to_tq(const policy::RoutePolicy& route) {
    taperquery::TqRoutePolicy result;
    if (route.route_id) {
        result.route_id = route.route_id;
    }
    if (route.match_prefix) {
        result.match_prefix = route.match_prefix;
    }
    result.match_kind = static_cast<taperquery::TqRouteMatchKind>(route.match_kind);
    result.mutation = static_cast<taperquery::TqMutationMode>(route.mutation);
    result.allowed_method = static_cast<taperquery::TqHttpMethod>(route.allowed_method);

    // field_filter
    result.field_filter.mode = static_cast<taperquery::TqFieldFilterMode>(route.field_filter.mode);
    result.field_filter.fields.clear();
    for (std::size_t i = 0; i < route.field_filter.field_count; ++i) {
        result.field_filter.fields.push_back(route.field_filter.fields[i]);
    }

    result.max_response_bytes = route.max_response_bytes;

    // cache
    result.cache.behavior = static_cast<taperquery::TqCacheBehavior>(route.cache.behavior);
    result.cache.ttl_ms = static_cast<taperquery::TqDurationMs>(route.cache.ttl_seconds) * 1000u;
    result.cache.enabled = route.cache.enabled;
    result.cache.l1.enabled = route.cache.l1.enabled;
    result.cache.l1.capacity_entries = route.cache.l1.capacity_entries;
    result.cache.l2.enabled = route.cache.l2.enabled;
    result.cache.l2.path = route.cache.l2.path;
    result.cache.private_cache.enabled = route.cache.private_cache;
    result.cache.private_cache.auth_scope_header = route.cache.auth_scope_header;

    result.cache.field_variant.enabled = route.cache.field_variant.enabled;
    result.cache.field_variant.max_variants_per_route =
        route.cache.field_variant.max_variants_per_route;
    result.cache.field_variant.min_field_count = route.cache.field_variant.min_field_count;
    result.cache.field_variant.max_field_count = route.cache.field_variant.max_field_count;
    result.cache.field_variant.admission_threshold = route.cache.field_variant.admission_threshold;
    result.cache.field_variant.ttl_max_ms = route.cache.field_variant.ttl_max_ms;

    result.cache.vary_headers.names.clear();
    for (std::size_t i = 0; i < route.cache.vary_headers.count; ++i) {
        result.cache.vary_headers.names.push_back(route.cache.vary_headers.names[i]);
    }

    result.failure_mode = static_cast<taperquery::TqFailureMode>(route.failure_mode);

    // pagination
    result.pagination.enabled = route.pagination.enabled;
    result.pagination.mode = static_cast<taperquery::TqPaginationMode>(route.pagination.mode);
    result.pagination.limit_param = route.pagination.limit_param;
    result.pagination.offset_param = route.pagination.offset_param;
    result.pagination.default_limit = route.pagination.default_limit;
    result.pagination.max_limit = route.pagination.max_limit;
    result.pagination.upstream_supports_pagination = route.pagination.upstream_supports_pagination;
    result.pagination.max_response_bytes_warning = route.pagination.max_response_bytes_warning;

    // compression
    result.compression.enabled = route.compression.enabled;
    result.compression.min_size_bytes = route.compression.min_size_bytes;
    result.compression.eligible_content_types.clear();
    for (std::size_t i = 0; i < route.compression.eligible_content_type_count; ++i) {
        result.compression.eligible_content_types.push_back(
            route.compression.eligible_content_types[i]);
    }
    result.compression.preferred_algorithms.clear();
    for (std::size_t i = 0; i < route.compression.preferred_algorithm_count; ++i) {
        result.compression.preferred_algorithms.push_back(
            static_cast<taperquery::TqCompressionAlgorithm>(
                route.compression.preferred_algorithms[i]));
    }
    result.compression.already_encoded_behavior = static_cast<taperquery::TqAlreadyEncodedBehavior>(
        route.compression.already_encoded_behavior);

    // coalescing
    result.coalescing.enabled = route.coalescing.enabled;
    result.coalescing.mode = static_cast<taperquery::TqCoalescingMode>(route.coalescing.mode);
    result.coalescing.backend_timeout_ms = route.coalescing.backend_timeout_ms;
    result.coalescing.handoff_buffer_ms = route.coalescing.handoff_buffer_ms;
    result.coalescing.result_ready_retention_ms = route.coalescing.result_ready_retention_ms;
    result.coalescing.max_waiters_per_key = route.coalescing.max_waiters_per_key;
    result.coalescing.require_cache_enabled = route.coalescing.require_cache_enabled;
    result.coalescing.allow_authenticated = route.coalescing.allow_authenticated;
    result.coalescing.max_follower_wait_budget_ms = route.coalescing.max_follower_wait_budget_ms;
    result.coalescing.max_active_follower_waiters = route.coalescing.max_active_follower_waiters;
    result.coalescing.max_active_follower_waiters_per_shard =
        route.coalescing.max_active_follower_waiters_per_shard;

    return result;
}

policy::RoutePolicy convert_tq_route_policy_to_runtime(const taperquery::TqRoutePolicy& ir) {
    policy::RoutePolicy res{};
    res.route_id = ir.route_id.c_str();
    res.match_prefix = ir.match_prefix.c_str();
    res.match_kind = static_cast<policy::RouteMatchKind>(ir.match_kind);
    res.mutation = static_cast<policy::MutationMode>(ir.mutation);
    res.allowed_method = static_cast<policy::HttpMethod>(ir.allowed_method);

    res.field_filter.mode = static_cast<policy::FieldFilterMode>(ir.field_filter.mode);
    res.field_filter.field_count = std::min(ir.field_filter.fields.size(), policy::kMaxFields);
    for (std::size_t i = 0; i < res.field_filter.field_count; ++i) {
        std::strncpy(res.field_filter.fields[i], ir.field_filter.fields[i].c_str(),
                     policy::kMaxFieldNameLen - 1);
    }

    res.max_response_bytes = ir.max_response_bytes;

    res.cache.enabled = ir.cache.enabled;
    res.cache.behavior = static_cast<policy::CacheBehavior>(ir.cache.behavior);
    res.cache.ttl_seconds = ir.cache.ttl_ms / 1000u;
    res.cache.l1.enabled = ir.cache.l1.enabled;
    res.cache.l1.capacity_entries = ir.cache.l1.capacity_entries;
    res.cache.l2.enabled = ir.cache.l2.enabled;
    std::strncpy(res.cache.l2.path, ir.cache.l2.path.c_str(), policy::kMaxCachePathLen - 1);
    res.cache.private_cache = ir.cache.private_cache.enabled;
    std::strncpy(res.cache.auth_scope_header, ir.cache.private_cache.auth_scope_header.c_str(),
                 sizeof(res.cache.auth_scope_header) - 1);

    res.cache.field_variant.enabled = ir.cache.field_variant.enabled;
    res.cache.field_variant.max_variants_per_route = ir.cache.field_variant.max_variants_per_route;
    res.cache.field_variant.min_field_count = ir.cache.field_variant.min_field_count;
    res.cache.field_variant.max_field_count = ir.cache.field_variant.max_field_count;
    res.cache.field_variant.admission_threshold = ir.cache.field_variant.admission_threshold;
    res.cache.field_variant.ttl_max_ms = ir.cache.field_variant.ttl_max_ms;

    res.cache.vary_headers.count =
        std::min(ir.cache.vary_headers.names.size(), policy::kMaxCacheVaryHeaders);
    for (std::size_t i = 0; i < res.cache.vary_headers.count; ++i) {
        std::strncpy(res.cache.vary_headers.names[i], ir.cache.vary_headers.names[i].c_str(),
                     policy::kMaxCacheVaryHeaderNameLen - 1);
    }

    res.failure_mode = static_cast<policy::FailureMode>(ir.failure_mode);

    res.pagination.enabled = ir.pagination.enabled;
    res.pagination.mode = static_cast<policy::PaginationMode>(ir.pagination.mode);
    std::strncpy(res.pagination.limit_param, ir.pagination.limit_param.c_str(),
                 sizeof(res.pagination.limit_param) - 1);
    std::strncpy(res.pagination.offset_param, ir.pagination.offset_param.c_str(),
                 sizeof(res.pagination.offset_param) - 1);
    res.pagination.default_limit = ir.pagination.default_limit;
    res.pagination.max_limit = ir.pagination.max_limit;
    res.pagination.upstream_supports_pagination = ir.pagination.upstream_supports_pagination;
    res.pagination.max_response_bytes_warning = ir.pagination.max_response_bytes_warning;

    res.compression.enabled = ir.compression.enabled;
    res.compression.min_size_bytes = ir.compression.min_size_bytes;
    res.compression.eligible_content_type_count =
        std::min(ir.compression.eligible_content_types.size(), policy::kMaxEligibleContentTypes);
    for (std::size_t i = 0; i < res.compression.eligible_content_type_count; ++i) {
        std::strncpy(res.compression.eligible_content_types[i],
                     ir.compression.eligible_content_types[i].c_str(),
                     policy::kMaxContentTypeLen - 1);
    }
    res.compression.preferred_algorithm_count =
        std::min(ir.compression.preferred_algorithms.size(), policy::kMaxCompressionAlgorithms);
    for (std::size_t i = 0; i < res.compression.preferred_algorithm_count; ++i) {
        res.compression.preferred_algorithms[i] =
            static_cast<policy::CompressionAlgorithm>(ir.compression.preferred_algorithms[i]);
    }
    res.compression.already_encoded_behavior =
        static_cast<policy::AlreadyEncodedBehavior>(ir.compression.already_encoded_behavior);

    res.coalescing.enabled = ir.coalescing.enabled;
    res.coalescing.mode = static_cast<policy::CoalescingMode>(ir.coalescing.mode);
    res.coalescing.backend_timeout_ms = ir.coalescing.backend_timeout_ms;
    res.coalescing.handoff_buffer_ms = ir.coalescing.handoff_buffer_ms;
    res.coalescing.result_ready_retention_ms = ir.coalescing.result_ready_retention_ms;
    res.coalescing.max_waiters_per_key = ir.coalescing.max_waiters_per_key;
    res.coalescing.require_cache_enabled = ir.coalescing.require_cache_enabled;
    res.coalescing.allow_authenticated = ir.coalescing.allow_authenticated;
    res.coalescing.max_follower_wait_budget_ms = ir.coalescing.max_follower_wait_budget_ms;
    res.coalescing.max_active_follower_waiters = ir.coalescing.max_active_follower_waiters;
    res.coalescing.max_active_follower_waiters_per_shard =
        ir.coalescing.max_active_follower_waiters_per_shard;

    policy::copy_route_policy_identity_v2_to_legacy_slot(&res);
    return res;
}

} // namespace

RuntimePolicySnapshotBuildResult
build_runtime_policy_snapshot_from_routes(const policy::RoutePolicy* routes,
                                          std::size_t route_count, const char* source_name,
                                          std::uint64_t generation) {
    RuntimePolicySnapshotBuildResult result{};
    auto snapshot = std::make_shared<RuntimePolicySnapshot>();

    if (source_name != nullptr) {
        snapshot->source_name = source_name;
    }
    snapshot->generation = generation;

    if (routes != nullptr && route_count > 0) {
        snapshot->policy_ir.source_name = snapshot->source_name;
        snapshot->policy_ir.routes.reserve(route_count);
        for (std::size_t i = 0; i < route_count; ++i) {
            snapshot->policy_ir.routes.push_back(convert_runtime_route_policy_to_tq(routes[i]));
        }

        snapshot->routes.reserve(route_count);
        for (std::size_t i = 0; i < route_count; ++i) {
            snapshot->routes.push_back(
                convert_tq_route_policy_to_runtime(snapshot->policy_ir.routes[i]));
        }

        policy::compile_route_matcher(snapshot->routes.data(), snapshot->routes.size(),
                                      &snapshot->route_matcher);
        snapshot->route_matcher_ready = true;

        extproc::compile_route_runtime_table(snapshot->routes.data(), snapshot->routes.size(),
                                             &snapshot->route_runtimes);

        snapshot->policy_identity =
            taperquery::compute_policy_document_identity(snapshot->policy_ir);
    } else {
        snapshot->policy_identity = "empty";
    }

    result.ok = true;
    result.snapshot = std::move(snapshot);
    return result;
}

RuntimePolicySnapshotBuildResult
build_runtime_policy_snapshot_from_ir(const taperquery::TqPolicyDocument& policy_ir,
                                      std::uint64_t generation) {
    RuntimePolicySnapshotBuildResult result{};
    auto snapshot = std::make_shared<RuntimePolicySnapshot>();

    snapshot->policy_ir = policy_ir;
    snapshot->source_name = policy_ir.source_name;
    snapshot->generation = generation;

    if (!snapshot->policy_ir.routes.empty()) {
        snapshot->routes.reserve(snapshot->policy_ir.routes.size());
        for (std::size_t i = 0; i < snapshot->policy_ir.routes.size(); ++i) {
            snapshot->routes.push_back(
                convert_tq_route_policy_to_runtime(snapshot->policy_ir.routes[i]));
        }

        policy::compile_route_matcher(snapshot->routes.data(), snapshot->routes.size(),
                                      &snapshot->route_matcher);
        snapshot->route_matcher_ready = true;

        extproc::compile_route_runtime_table(snapshot->routes.data(), snapshot->routes.size(),
                                             &snapshot->route_runtimes);

        snapshot->policy_identity =
            taperquery::compute_policy_document_identity(snapshot->policy_ir);
    } else {
        snapshot->policy_identity = "empty";
    }

    result.ok = true;
    result.snapshot = std::move(snapshot);
    return result;
}

std::shared_ptr<const RuntimePolicySnapshot> RuntimePolicyStore::load() const {
    std::lock_guard<std::mutex> lock(mu_);
    return active_;
}

bool RuntimePolicyStore::install_initial(std::shared_ptr<const RuntimePolicySnapshot> snapshot,
                                         std::string* error_out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (active_ != nullptr) {
        if (error_out != nullptr) {
            *error_out = "store already initialized";
        }
        return false;
    }
    active_ = std::move(snapshot);
    return true;
}

bool RuntimePolicyStore::swap(std::shared_ptr<const RuntimePolicySnapshot> snapshot,
                              std::string* error_out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (active_ == nullptr) {
        if (error_out != nullptr) {
            *error_out = "store not initialized";
        }
        return false;
    }
    active_ = std::move(snapshot);
    return true;
}

std::uint64_t RuntimePolicyStore::next_generation() {
    std::lock_guard<std::mutex> lock(mu_);
    return ++generation_;
}

} // namespace bytetaper::runtime
