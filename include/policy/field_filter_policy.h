// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_POLICY_FIELD_FILTER_POLICY_H
#define BYTETAPER_POLICY_FIELD_FILTER_POLICY_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bytetaper::policy {

static constexpr std::size_t kMaxFields = 16;
static constexpr std::size_t kMaxFieldNameLen = 64;

enum class FieldFilterMode : std::uint8_t {
    None = 0,      // pass everything
    Allowlist = 1, // only listed fields are kept
    Denylist = 2,  // listed fields are removed
};

struct FieldFilterPolicy {
    FieldFilterMode mode = FieldFilterMode::None;
    char fields[kMaxFields][kMaxFieldNameLen] = {};
    std::size_t field_count = 0;
};

/**
 * Returns true if a field with the given name should be KEPT in the response.
 */
inline bool apply_field_filter(const FieldFilterPolicy& filter, const char* field_name) {
    if (field_name == nullptr) {
        return false;
    }
    if (filter.mode == FieldFilterMode::None) {
        return true;
    }

    for (std::size_t i = 0; i < filter.field_count; ++i) {
        if (std::strcmp(field_name, filter.fields[i]) == 0) {
            // found in list
            return filter.mode == FieldFilterMode::Allowlist;
        }
    }

    // not in list
    return filter.mode == FieldFilterMode::Denylist;
}

struct FieldVariantCachePolicy {
    bool enabled = false;
    std::uint32_t max_variants_per_route = 8; // cardinality cap
    std::uint32_t min_field_count = 1;        // ignore trivial single-field variants
    std::uint32_t max_field_count = 8;        // ignore overly-specific combos
    std::uint32_t admission_threshold = 2;    // requests before materializing
    std::uint32_t ttl_max_ms = 0;             // 0 = use raw cache TTL
};

} // namespace bytetaper::policy

#endif // BYTETAPER_POLICY_FIELD_FILTER_POLICY_H
