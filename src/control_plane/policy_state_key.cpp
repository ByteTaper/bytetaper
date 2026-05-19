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

} // namespace bytetaper::control_plane
