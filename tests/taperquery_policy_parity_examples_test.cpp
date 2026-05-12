// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_compare.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_printer.h"

#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>
#include <string>

namespace bytetaper::taperquery {

namespace {

std::string find_file_path(const std::string& relative_path) {
    std::string p1 = relative_path;
    std::string p2 = "../" + relative_path;
    std::string p3 = "/workspace/" + relative_path;

    const std::string* paths[] = { &p1, &p2, &p3 };
    for (const std::string* p : paths) {
        std::ifstream f(*p);
        if (f.good()) {
            return *p;
        }
    }
    return relative_path; // fallback
}

std::string read_file_content(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void write_file_content(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (f.is_open()) {
        f << content;
    }
}

// Parses snapshot format into a TqPolicyDocument
TqPolicyDocument parse_snapshot_to_policy_ir(const std::string& content) {
    TqPolicyDocument doc;
    std::stringstream ss(content);
    std::string line;
    TqRoutePolicy current_route;
    bool in_route = false;
    bool in_cache = false;
    bool in_l1 = false;
    bool in_l2 = false;
    bool in_private_cache = false;
    bool in_field_variant = false;
    bool in_vary_headers = false;
    bool in_field_filter = false;
    bool in_pagination = false;
    bool in_compression = false;
    bool in_coalescing = false;

    auto trim = [](const std::string& s) {
        auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return std::string();
        }
        auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, (last - first + 1));
    };

    while (std::getline(ss, line)) {
        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') {
            indent++;
        }

        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        // Reset list states on new high-level sections
        if (trimmed == "route:") {
            if (in_route) {
                doc.routes.push_back(current_route);
            }
            current_route = TqRoutePolicy();
            in_route = true;
            in_cache = in_l1 = in_l2 = in_private_cache = in_field_variant = in_vary_headers =
                false;
            in_field_filter = in_pagination = in_compression = in_coalescing = false;
            continue;
        }

        auto colon = trimmed.find(':');
        if (colon != std::string::npos) {
            std::string key = trim(trimmed.substr(0, colon));
            std::string val = trim(trimmed.substr(colon + 1));

            if (in_cache && indent <= 4) {
                in_l1 = in_l2 = in_private_cache = in_field_variant = in_vary_headers = false;
            }

            if (key == "schema_version") {
                doc.schema_version = val;
            } else if (key == "document_id") {
                doc.document_id = val;
            } else if (key == "source_name") {
                doc.source_name = val;
            } else if (key == "expected_base_sha") {
                doc.expected_base_sha = val;
            } else if (key == "route_id") {
                current_route.route_id = val;
            } else if (key == "match_prefix") {
                current_route.match_prefix = val;
            } else if (key == "match_kind") {
                current_route.match_kind =
                    (val == "exact") ? TqRouteMatchKind::Exact : TqRouteMatchKind::Prefix;
            } else if (key == "mutation") {
                current_route.mutation = (val == "headers_only") ? TqMutationMode::HeadersOnly
                                         : (val == "full")       ? TqMutationMode::Full
                                                                 : TqMutationMode::Disabled;
            } else if (key == "allowed_method") {
                if (val == "get") {
                    current_route.allowed_method = TqHttpMethod::Get;
                } else if (val == "post") {
                    current_route.allowed_method = TqHttpMethod::Post;
                } else if (val == "put") {
                    current_route.allowed_method = TqHttpMethod::Put;
                } else if (val == "delete") {
                    current_route.allowed_method = TqHttpMethod::Delete;
                } else if (val == "patch") {
                    current_route.allowed_method = TqHttpMethod::Patch;
                } else {
                    current_route.allowed_method = TqHttpMethod::Any;
                }
            } else if (key == "max_response_bytes") {
                current_route.max_response_bytes = std::stoul(val);
            } else if (key == "failure_mode") {
                current_route.failure_mode =
                    (val == "fail_closed") ? TqFailureMode::FailClosed : TqFailureMode::FailOpen;
            } else if (key == "cache") {
                in_cache = true;
                in_l1 = in_l2 = in_private_cache = in_field_variant = in_vary_headers = false;
                in_field_filter = in_pagination = in_compression = in_coalescing = false;
            } else if (key == "field_filter") {
                in_field_filter = true;
                in_cache = in_pagination = in_compression = in_coalescing = false;
            } else if (key == "pagination") {
                in_pagination = true;
                in_cache = in_field_filter = in_compression = in_coalescing = false;
            } else if (key == "compression") {
                in_compression = true;
                in_cache = in_field_filter = in_pagination = in_coalescing = false;
            } else if (key == "coalescing") {
                in_coalescing = true;
                in_cache = in_field_filter = in_pagination = in_compression = false;
            } else if (in_cache) {
                if (in_l1) {
                    if (key == "enabled") {
                        current_route.cache.l1.enabled = (val == "true");
                    } else if (key == "capacity_entries") {
                        current_route.cache.l1.capacity_entries = std::stoul(val);
                    }
                } else if (in_l2) {
                    if (key == "enabled") {
                        current_route.cache.l2.enabled = (val == "true");
                    } else if (key == "path") {
                        current_route.cache.l2.path = val;
                    }
                } else if (in_private_cache) {
                    if (key == "enabled") {
                        current_route.cache.private_cache.enabled = (val == "true");
                    } else if (key == "auth_scope_header") {
                        current_route.cache.private_cache.auth_scope_header = val;
                    }
                } else if (in_field_variant) {
                    if (key == "enabled") {
                        current_route.cache.field_variant.enabled = (val == "true");
                    } else if (key == "max_variants_per_route") {
                        current_route.cache.field_variant.max_variants_per_route = std::stoul(val);
                    } else if (key == "min_field_count") {
                        current_route.cache.field_variant.min_field_count = std::stoul(val);
                    } else if (key == "max_field_count") {
                        current_route.cache.field_variant.max_field_count = std::stoul(val);
                    } else if (key == "admission_threshold") {
                        current_route.cache.field_variant.admission_threshold = std::stoul(val);
                    } else if (key == "ttl_max_ms") {
                        current_route.cache.field_variant.ttl_max_ms = std::stoul(val);
                    }
                } else {
                    if (key == "enabled") {
                        current_route.cache.enabled = (val == "true");
                    } else if (key == "behavior") {
                        current_route.cache.behavior = (val == "bypass") ? TqCacheBehavior::Bypass
                                                       : (val == "store")
                                                           ? TqCacheBehavior::Store
                                                           : TqCacheBehavior::Default;
                    } else if (key == "ttl_ms") {
                        current_route.cache.ttl_ms = std::stoull(val);
                    } else if (key == "l1") {
                        in_l1 = true;
                        in_l2 = in_private_cache = in_field_variant = in_vary_headers = false;
                    } else if (key == "l2") {
                        in_l2 = true;
                        in_l1 = in_private_cache = in_field_variant = in_vary_headers = false;
                    } else if (key == "private_cache") {
                        in_private_cache = true;
                        in_l1 = in_l2 = in_field_variant = in_vary_headers = false;
                    } else if (key == "field_variant") {
                        in_field_variant = true;
                        in_l1 = in_l2 = in_private_cache = in_vary_headers = false;
                    } else if (key == "vary_headers") {
                        in_vary_headers = true;
                        in_l1 = in_l2 = in_private_cache = in_field_variant = false;
                    }
                }
            } else if (in_field_filter) {
                if (key == "mode") {
                    current_route.field_filter.mode =
                        (val == "allowlist")  ? TqFieldFilterMode::Allowlist
                        : (val == "denylist") ? TqFieldFilterMode::Denylist
                                              : TqFieldFilterMode::None;
                } else if (key == "fields") {
                    // handled by bullet elements
                }
            } else if (in_pagination) {
                if (key == "enabled") {
                    current_route.pagination.enabled = (val == "true");
                } else if (key == "mode") {
                    current_route.pagination.mode = (val == "limit_offset")
                                                        ? TqPaginationMode::LimitOffset
                                                    : (val == "cursor") ? TqPaginationMode::Cursor
                                                                        : TqPaginationMode::None;
                } else if (key == "limit_param") {
                    current_route.pagination.limit_param = val;
                } else if (key == "offset_param") {
                    current_route.pagination.offset_param = val;
                } else if (key == "default_limit") {
                    current_route.pagination.default_limit = std::stoul(val);
                } else if (key == "max_limit") {
                    current_route.pagination.max_limit = std::stoul(val);
                } else if (key == "upstream_supports_pagination") {
                    current_route.pagination.upstream_supports_pagination = (val == "true");
                } else if (key == "max_response_bytes_warning") {
                    current_route.pagination.max_response_bytes_warning = std::stoul(val);
                }
            } else if (in_compression) {
                if (key == "enabled") {
                    current_route.compression.enabled = (val == "true");
                } else if (key == "min_size_bytes") {
                    current_route.compression.min_size_bytes = std::stoul(val);
                } else if (key == "eligible_content_types" || key == "preferred_algorithms") {
                    // list elements
                } else if (key == "already_encoded_behavior") {
                    current_route.compression.already_encoded_behavior =
                        (val == "passthrough") ? TqAlreadyEncodedBehavior::Passthrough
                                               : TqAlreadyEncodedBehavior::Skip;
                }
            } else if (in_coalescing) {
                if (key == "enabled") {
                    current_route.coalescing.enabled = (val == "true");
                } else if (key == "mode") {
                    current_route.coalescing.mode = TqCoalescingMode::CacheAssisted;
                } else if (key == "backend_timeout_ms") {
                    current_route.coalescing.backend_timeout_ms = std::stoul(val);
                } else if (key == "handoff_buffer_ms") {
                    current_route.coalescing.handoff_buffer_ms = std::stoul(val);
                } else if (key == "result_ready_retention_ms") {
                    current_route.coalescing.result_ready_retention_ms = std::stoul(val);
                } else if (key == "max_waiters_per_key") {
                    current_route.coalescing.max_waiters_per_key = std::stoul(val);
                } else if (key == "require_cache_enabled") {
                    current_route.coalescing.require_cache_enabled = (val == "true");
                } else if (key == "allow_authenticated") {
                    current_route.coalescing.allow_authenticated = (val == "true");
                } else if (key == "max_follower_wait_budget_ms") {
                    current_route.coalescing.max_follower_wait_budget_ms = std::stoul(val);
                } else if (key == "max_active_follower_waiters") {
                    current_route.coalescing.max_active_follower_waiters = std::stoul(val);
                } else if (key == "max_active_follower_waiters_per_shard") {
                    current_route.coalescing.max_active_follower_waiters_per_shard =
                        std::stoul(val);
                }
            }
        } else if (trimmed.rfind("- ", 0) == 0) {
            std::string list_val = trim(trimmed.substr(2));
            if (in_cache && in_vary_headers) {
                current_route.cache.vary_headers.names.push_back(list_val);
            } else if (in_field_filter) {
                current_route.field_filter.fields.push_back(list_val);
            } else if (in_compression) {
                // Determine list kind by parent or content type form
                if (list_val.find('/') != std::string::npos) {
                    current_route.compression.eligible_content_types.push_back(list_val);
                } else {
                    TqCompressionAlgorithm algo = TqCompressionAlgorithm::None;
                    if (list_val == "gzip") {
                        algo = TqCompressionAlgorithm::Gzip;
                    } else if (list_val == "brotli") {
                        algo = TqCompressionAlgorithm::Brotli;
                    } else if (list_val == "zstd") {
                        algo = TqCompressionAlgorithm::Zstd;
                    }
                    current_route.compression.preferred_algorithms.push_back(algo);
                }
            }
        }
    }
    if (in_route) {
        doc.routes.push_back(current_route);
    }
    return doc;
}

void verify_policy_parity(const std::string& yaml_name, const std::string& snapshot_name) {
    std::string yaml_path = find_file_path("examples/policy/" + yaml_name);
    std::string snapshot_path = find_file_path("tests/taperquery/snapshots/" + snapshot_name);

    auto load_res = load_policy_ir_from_yaml_file(yaml_path.c_str());
    ASSERT_TRUE(load_res.ok) << "Failed to load policy: " << yaml_path
                             << ", err: " << load_res.error;

    // Retrieve environment variable
    const char* update_snapshots_env = std::getenv("BYTETAPER_UPDATE_TQ_SNAPSHOTS");
    bool update_snapshots =
        (update_snapshots_env != nullptr && std::string(update_snapshots_env) == "1");

    std::string actual_canonical = print_canonical_policy_ir(load_res.policy);

    if (update_snapshots) {
        // Safe updates: write printed output back to the snapshot path
        write_file_content(snapshot_path, actual_canonical);
        std::cout << "[INFO] Updated snapshot: " << snapshot_path << std::endl;
        return;
    }

    std::string snapshot_content = read_file_content(snapshot_path);
    ASSERT_FALSE(snapshot_content.empty())
        << "Snapshot content is empty or missing: " << snapshot_path;

    TqPolicyDocument expected_doc = parse_snapshot_to_policy_ir(snapshot_content);
    PolicyIrDiff diff = compare_policy_ir(expected_doc, load_res.policy);

    if (!diff.equal) {
        std::string err_msg;
        for (const auto& d : diff.field_diffs) {
            err_msg += "policy: examples/policy/" + yaml_name + "\n";
            err_msg += "route: " + (d.route_id.empty() ? "<document>" : d.route_id) + "\n";
            err_msg += "field: " + d.field_path + "\n";
            err_msg += "expected: " + d.expected + "\n";
            err_msg += "actual: " + d.actual + "\n";
            err_msg += "hint: " + d.hint + "\n\n";
        }
        FAIL() << "Parity mismatch for " << yaml_name << "!\n\n" << err_msg;
    }
}

} // namespace

TEST(TaperQueryPolicyParityExamplesTest, BytetaperPolicyYaml) {
    verify_policy_parity("bytetaper-policy.yaml", "bytetaper-policy.ir.txt");
}

TEST(TaperQueryPolicyParityExamplesTest, CacheBenchmarkPolicyYaml) {
    verify_policy_parity("cache-benchmark-policy.yaml", "cache-benchmark-policy.ir.txt");
}

TEST(TaperQueryPolicyParityExamplesTest, CoalescingBenchmarkPolicyYaml) {
    verify_policy_parity("coalescing-benchmark-policy.yaml", "coalescing-benchmark-policy.ir.txt");
}

TEST(TaperQueryPolicyParityExamplesTest, CompressionBenchmarkPolicyYaml) {
    verify_policy_parity("compression-benchmark-policy.yaml",
                         "compression-benchmark-policy.ir.txt");
}

TEST(TaperQueryPolicyParityExamplesTest, FieldFilteringPolicyYaml) {
    verify_policy_parity("field-filtering-policy.yaml", "field-filtering-policy.ir.txt");
}

TEST(TaperQueryPolicyParityExamplesTest, HeaderVarianceBenchmarkPolicyYaml) {
    verify_policy_parity("header-variance-benchmark-policy.yaml",
                         "header-variance-benchmark-policy.ir.txt");
}

TEST(TaperQueryPolicyParityExamplesTest, ObserveModePolicyYaml) {
    verify_policy_parity("observe-mode-policy.yaml", "observe-mode-policy.ir.txt");
}

TEST(TaperQueryPolicyParityExamplesTest, PaginationBenchmarkPolicyYaml) {
    verify_policy_parity("pagination-benchmark-policy.yaml", "pagination-benchmark-policy.ir.txt");
}

} // namespace bytetaper::taperquery
