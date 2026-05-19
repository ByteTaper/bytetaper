// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_state_key.h"

#include <cinttypes>
#include <cstdio>

namespace bytetaper::control_plane {

namespace {

std::string format_generation(std::uint64_t generation) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%010" PRIu64, generation);
    return buf;
}

std::string policy_prefix(const PolicyResourceKey& key) {
    return "v1/policies/" + key.namespace_name + "/" + key.policy_name;
}

} // namespace

std::string PolicyResourceKey::to_string() const {
    return "policy/" + namespace_name + "/" + policy_name;
}

PolicyResourceKey PolicyResourceKey::default_runtime() {
    PolicyResourceKey key;
    key.namespace_name = "default";
    key.policy_name = "runtime";
    return key;
}

std::string make_active_key(const PolicyResourceKey& key) {
    return policy_prefix(key) + "/active";
}

std::string make_version_key(const PolicyResourceKey& key, std::uint64_t generation) {
    return policy_prefix(key) + "/versions/" + format_generation(generation);
}

std::string make_yaml_key(const PolicyResourceKey& key, std::uint64_t generation) {
    return make_version_key(key, generation) + "/yaml";
}

std::string make_audit_key(const PolicyResourceKey& key, const std::string& apply_id) {
    return policy_prefix(key) + "/audit/" + apply_id;
}

std::string make_job_key(const PolicyResourceKey& key, const std::string& job_id) {
    return policy_prefix(key) + "/jobs/" + job_id;
}

bool parse_resource_key(const std::string& resource_key, PolicyResourceKey* out) {
    if (out == nullptr) {
        return false;
    }
    const std::string prefix = "policy/";
    if (resource_key.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string remainder = resource_key.substr(prefix.size());
    const std::size_t slash = remainder.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= remainder.size()) {
        return false;
    }
    out->namespace_name = remainder.substr(0, slash);
    out->policy_name = remainder.substr(slash + 1);
    return !out->namespace_name.empty() && !out->policy_name.empty();
}

} // namespace bytetaper::control_plane
