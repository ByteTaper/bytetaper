// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/default_pipelines.h"
#include "stages/l2_cache_lookup_stage.h"
#include "stages/l2_cache_store_stage.h"

#include <gtest/gtest.h>

namespace bytetaper::extproc {

/**
 * NOTE: These tests check the compiled stage arrays at runtime.
 * To detect sync I/O hidden inside stage implementations, see:
 *   tools/check_runtime_boundaries.py
 * That script enforces the same contract at the source level and is run in CI.
 */

/**
 * Helper: search for a forbidden stage in a pipeline array.
 * This is used to enforce the RUNTIME_BOUNDARIES.md contract.
 */
static bool contains_stage(const apg::ApgStage* stages, std::size_t count,
                           apg::ApgStage forbidden) {
    for (std::size_t i = 0; i < count; i++) {
        if (stages[i] == forbidden) {
            return true;
        }
    }
    return false;
}

/**
 * HOT-PATH ENFORCEMENT: The request-header pipeline must NOT perform synchronous L2 lookup.
 * Synchronous L2 lookup performs disk I/O which can block the Envoy worker thread.
 */
TEST(DefaultPipelineBoundaries, LookupPipelineHasNoSyncL2Lookup) {
    EXPECT_FALSE(contains_stage(kLookupStages, kLookupStageCount, stages::l2_cache_lookup_stage));
}

/**
 * HOT-PATH ENFORCEMENT: The request-header pipeline must NOT perform synchronous L2 store.
 */
TEST(DefaultPipelineBoundaries, LookupPipelineHasNoSyncL2Store) {
    EXPECT_FALSE(contains_stage(kLookupStages, kLookupStageCount, stages::l2_cache_store_stage));
}

/**
 * HOT-PATH ENFORCEMENT: The response-body pipeline must NOT perform synchronous L2 lookup.
 */
TEST(DefaultPipelineBoundaries, StorePipelineHasNoSyncL2Lookup) {
    EXPECT_FALSE(contains_stage(kStoreStages, kStoreStageCount, stages::l2_cache_lookup_stage));
}

/**
 * HOT-PATH ENFORCEMENT: The response-body pipeline must NOT perform synchronous L2 store.
 * Synchronous L2 store performs disk I/O which can block the Envoy worker thread.
 */
TEST(DefaultPipelineBoundaries, StorePipelineHasNoSyncL2Store) {
    EXPECT_FALSE(contains_stage(kStoreStages, kStoreStageCount, stages::l2_cache_store_stage));
}

TEST(DefaultPipelineBoundaries, LookupPipelineIsNonEmpty) {
    EXPECT_GT(kLookupStageCount, 0u);
}

TEST(DefaultPipelineBoundaries, StorePipelineIsNonEmpty) {
    EXPECT_GT(kStoreStageCount, 0u);
}

} // namespace bytetaper::extproc
