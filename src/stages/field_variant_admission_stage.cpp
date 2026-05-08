// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/field_variant_admission_stage.h"

#include "cache/cache_key.h"
#include "policy/cache_policy.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace bytetaper::stages {

struct VariantAdmissionEntry {
    char key[cache::kCacheKeyMaxLen] = {};
    char route_id[64] = {};
    std::uint32_t count = 0;
    bool admitted = false;
};

class VariantAdmissionRegistry {
public:
    static VariantAdmissionRegistry& get() {
        static VariantAdmissionRegistry instance;
        return instance;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mu_);
        entries_.clear();
    }

    bool record_and_check(const char* route_id, const char* variant_key, std::uint32_t max_variants,
                          std::uint32_t threshold, std::uint32_t* out_count) {
        std::lock_guard<std::mutex> lock(mu_);

        // Find existing entry
        for (auto& entry : entries_) {
            if (std::strcmp(entry.key, variant_key) == 0) {
                entry.count++;
                *out_count = entry.count;
                if (!entry.admitted && entry.count >= threshold) {
                    // Check route-level limit
                    std::uint32_t route_admitted_count = 0;
                    for (const auto& other : entries_) {
                        if (std::strcmp(other.route_id, route_id) == 0 && other.admitted) {
                            route_admitted_count++;
                        }
                    }
                    if (route_admitted_count < max_variants) {
                        entry.admitted = true;
                    }
                }
                return entry.admitted;
            }
        }

        // Create new entry
        VariantAdmissionEntry entry{};
        std::strncpy(entry.key, variant_key, sizeof(entry.key) - 1);
        std::strncpy(entry.route_id, route_id, sizeof(entry.route_id) - 1);
        entry.count = 1;
        *out_count = 1;

        if (threshold <= 1) {
            // Check route-level limit
            std::uint32_t route_admitted_count = 0;
            for (const auto& other : entries_) {
                if (std::strcmp(other.route_id, route_id) == 0 && other.admitted) {
                    route_admitted_count++;
                }
            }
            if (route_admitted_count < max_variants) {
                entry.admitted = true;
            }
        }

        entries_.push_back(entry);
        return entry.admitted;
    }

private:
    std::mutex mu_;
    std::vector<VariantAdmissionEntry> entries_;
};

void field_variant_admission_test_reset() {
    VariantAdmissionRegistry::get().reset();
}

apg::StageOutput field_variant_admission_stage(apg::ApgTransformContext& context) {
    context.variant_admission_passed = false;

    if (!context.variant_cache_key_ready) {
        return { apg::StageResult::Continue, "not-ready" };
    }

    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    const auto& fv_policy = context.matched_policy->cache.field_variant;
    if (!fv_policy.enabled) {
        return { apg::StageResult::Continue, "variant-policy-disabled" };
    }

    // Check field count bounds
    if (context.selected_field_count < fv_policy.min_field_count) {
        return { apg::StageResult::Continue, "below-min-fields" };
    }

    if (context.selected_field_count > fv_policy.max_field_count) {
        return { apg::StageResult::Continue, "above-max-fields" };
    }

    std::uint32_t count = 0;
    bool admitted = VariantAdmissionRegistry::get().record_and_check(
        context.matched_policy->route_id, context.variant_cache_key,
        fv_policy.max_variants_per_route, fv_policy.admission_threshold, &count);

    if (admitted) {
        context.variant_admission_passed = true;
        return { apg::StageResult::Continue, "admitted" };
    }

    return { apg::StageResult::Continue, "throttled" };
}

} // namespace bytetaper::stages
