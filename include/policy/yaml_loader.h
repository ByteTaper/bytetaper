// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_POLICY_YAML_LOADER_H
#define BYTETAPER_POLICY_YAML_LOADER_H

#include "policy/route_policy.h"

#include <cstddef>

namespace bytetaper::policy {

static constexpr std::size_t kMaxRoutes = 64;
static constexpr std::size_t kMaxRouteIdLen = 64;
static constexpr std::size_t kMaxPrefixLen = 256;

struct PolicyFileResult {
    bool ok = false;
    const char* error = nullptr; // static string, no ownership
    std::size_t count = 0;

    char route_id_storage[kMaxRoutes][kMaxRouteIdLen] = {};
    char match_prefix_storage[kMaxRoutes][kMaxPrefixLen] = {};
    RoutePolicy policies[kMaxRoutes] = {};
    char warning[256] = {}; // non-fatal deprecation notice; empty if none
};

// Load from YAML file path. Returns false and sets result->error on failure.
bool load_policy_from_file(const char* path, PolicyFileResult* result);

// Load from YAML string content (useful for testing without disk files).
bool load_policy_from_string(const char* yaml_content, PolicyFileResult* result);

} // namespace bytetaper::policy

#endif // BYTETAPER_POLICY_YAML_LOADER_H
