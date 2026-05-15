// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_EXTPROC_DEFAULT_PIPELINES_H
#define BYTETAPER_EXTPROC_DEFAULT_PIPELINES_H

#include "apg/stage.h"
#include "policy/route_policy.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::extproc {

constexpr std::size_t kMaxLookupStages = 12;
constexpr std::size_t kMaxStoreStages = 8;
constexpr std::size_t kMaxResponseStages = 12;

struct CompiledRouteRuntime {
    apg::ApgStage lookup_stages[kMaxLookupStages] = {};
    std::size_t lookup_count = 0;

    apg::ApgStage store_stages[kMaxStoreStages] = {};
    std::size_t store_count = 0;

    apg::ApgStage response_stages[kMaxResponseStages] = {};
    std::size_t response_count = 0;

    const policy::RoutePolicy* policy = nullptr;
};

constexpr std::size_t kMaxCompiledRoutesCapacity = 64;

struct CompiledRouteRuntimeTable {
    CompiledRouteRuntime entries[kMaxCompiledRoutesCapacity] = {};
    std::size_t count = 0;
};

// Compiles a single route policy into pre-arranged stages arrays.
void compile_route_runtime(const policy::RoutePolicy& policy, CompiledRouteRuntime* runtime);

// Compiles an array of route policies into the table.
void compile_route_runtime_table(const policy::RoutePolicy* policies, std::size_t policy_count,
                                 CompiledRouteRuntimeTable* table);

// Finds a precompiled CompiledRouteRuntime corresponding to matched route policy.
// Returns nullptr if not found.
const CompiledRouteRuntime* find_compiled_route_runtime(const CompiledRouteRuntimeTable& table,
                                                        const policy::RoutePolicy* policy);

/**
 * HOT-PATH: request-header pipeline.
 *
 * This pipeline runs synchronously on the Envoy ExtProc request thread.
 * It must NOT contain any stages that perform synchronous disk I/O (e.g., RocksDB get).
 *
 * See docs/runtime/RUNTIME_BOUNDARIES.md for the enforcement contract.
 */
extern const apg::ApgStage kLookupStages[];
extern const std::size_t kLookupStageCount;

/**
 * HOT-PATH: response-body pipeline.
 *
 * This pipeline runs synchronously on the Envoy ExtProc response thread.
 * It must NOT contain any stages that perform synchronous disk I/O (e.g., RocksDB put).
 *
 * See docs/runtime/RUNTIME_BOUNDARIES.md for the enforcement contract.
 */
extern const apg::ApgStage kStoreStages[];
extern const std::size_t kStoreStageCount;

extern const apg::ApgStage kResponseStages[];
extern const std::size_t kResponseStageCount;

} // namespace bytetaper::extproc

#endif // BYTETAPER_EXTPROC_DEFAULT_PIPELINES_H
