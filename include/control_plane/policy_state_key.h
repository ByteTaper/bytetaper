// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_STATE_KEY_H
#define BYTETAPER_CONTROL_PLANE_POLICY_STATE_KEY_H

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

struct PolicyResourceKey {
    std::string namespace_name; // "default"
    std::string policy_name;    // "runtime"

    std::string to_string() const;
    static PolicyResourceKey default_runtime();
};

std::string make_active_key(const PolicyResourceKey& key);
std::string make_version_key(const PolicyResourceKey& key, std::uint64_t generation);
std::string make_yaml_key(const PolicyResourceKey& key, std::uint64_t generation);
std::string make_audit_key(const PolicyResourceKey& key, const std::string& apply_id);
std::string make_job_key(const PolicyResourceKey& key, const std::string& job_id);

bool parse_resource_key(const std::string& resource_key, PolicyResourceKey* out);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_STATE_KEY_H
