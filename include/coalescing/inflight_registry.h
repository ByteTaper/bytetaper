// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_COALESCING_INFLIGHT_REGISTRY_H
#define BYTETAPER_COALESCING_INFLIGHT_REGISTRY_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace bytetaper::coalescing {

/**
 * @brief Role assigned to a request by the registry.
 */
enum class InFlightRole : std::uint8_t {
    Leader = 0,   // First request for this key, will fetch from upstream.
    Follower = 1, // Subsequent request, should wait for leader.
    Reject = 2,   // Queue/Shard full, instantly synthesize error response.
};

/**
 * @brief Result of a request registration.
 */
struct RegistryRegistrationResult {
    InFlightRole role;
    std::uint64_t lifecycle_generation = 0;
};

/**
 * @brief An entry in the in-flight request registry.
 * Following Orthodox C++ style: plain struct with fixed-size key buffer.
 */
enum class InFlightCompletionState : std::uint8_t {
    InFlight = 0,
    Stored = 1,
    NotCacheable = 2,
    Failed = 3,
    L1Ready = 4, // leader committed response to L1; followers should lookup L1
};

static bool is_terminal(InFlightCompletionState s) {
    return s != InFlightCompletionState::InFlight;
}

static constexpr std::size_t kCoalescingSharedBodyMaxSize = 65536;
static constexpr std::size_t kCoalescingContentTypeMaxLen = 64;

struct InFlightSharedResponse {
    std::uint16_t status_code = 0;
    char content_type[kCoalescingContentTypeMaxLen] = {};
    char body[kCoalescingSharedBodyMaxSize] = {};
    std::size_t body_len = 0;
    bool ready = false;
};

struct RegistrySharedResponseOutput {
    std::uint16_t status_code = 0;
    char content_type[kCoalescingContentTypeMaxLen] = {};
    char body[kCoalescingSharedBodyMaxSize] = {};
    std::size_t body_len = 0;
};

/**
 * @brief An entry in the in-flight request registry.
 * Following Orthodox C++ style: plain struct with fixed-size key buffer.
 */
struct InFlightEntry {
    char key[256] = { 0 };
    std::uint64_t created_at_epoch_ms = 0;
    std::uint64_t completed_at_epoch_ms = 0;
    std::uint32_t waiter_count = 0;
    bool active = false;
    InFlightCompletionState state = InFlightCompletionState::InFlight;
    std::uint64_t lifecycle_generation = 0;
    InFlightSharedResponse shared_response{};
};

/**
 * @brief Constants for sharded architecture.
 * Using 8 shards with 16 slots each as described in the memory note of the plan.
 */
static constexpr std::size_t kInFlightShards = 8;
static constexpr std::size_t kSlotsPerShard = 16; // 128 total capacity

/**
 * @brief A shard of the registry, protected by its own mutex to minimize contention.
 */
struct InFlightShard {
    std::mutex mutex;
    std::condition_variable cv; // notified by registry completion functions
    InFlightEntry slots[kSlotsPerShard];
};

/**
 * @brief Thread-safe, sharded, bounded in-memory registry for in-flight requests.
 * Uses lock striping to allow concurrent access to different shards.
 */
struct InFlightRegistry {
    InFlightShard shards[kInFlightShards];
};

/**
 * @brief Initializes the registry.
 *
 * @param registry The registry to initialize.
 */
void registry_init(InFlightRegistry* registry);

/**
 * @brief Registers a request in the registry.
 *
 * Determines if the request is a Leader, Follower, or should be Rejected.
 *
 * @param registry The registry instance.
 * @param key The coalescing key.
 * @param now_ms Current epoch time in milliseconds.
 * @param wait_window_ms Time window in which a request is considered "in flight".
 * @param max_waiters_per_key Maximum number of followers allowed per leader.
 * @return RegistryRegistrationResult The assigned role.
 */
RegistryRegistrationResult registry_register(InFlightRegistry* registry, const char* key,
                                             std::uint64_t now_ms, std::uint32_t wait_window_ms,
                                             std::uint32_t max_waiters_per_key);

/**
 * @brief Completes the in-flight entry with a response snapshot.
 */
bool registry_complete_with_response(InFlightRegistry* registry, const char* key,
                                     std::uint16_t status_code, const char* content_type,
                                     const char* body, std::size_t body_len, std::uint64_t now_ms);

/**
 * @brief Completes the in-flight entry with a simple terminal state.
 */
bool registry_complete_state(InFlightRegistry* registry, const char* key,
                             InFlightCompletionState state, std::uint64_t now_ms);

/**
 * @brief Result of a registry wait operation.
 */
enum class RegistryWaitResult : std::uint8_t {
    SharedResponseReady = 0,
    StoredButNoSnapshot = 1,
    NotCacheable = 2,
    Failed = 3,
    Timeout = 4,
    Missing = 5,
    L1Ready = 6, // leader marked L1Ready; follower should do L1 lookup
};

/**
 * @brief Blocks the calling thread until the leader for key completes or timeout.
 *
 * Uses std::condition_variable internally for efficient wakeup.
 *
 * @param registry The registry instance.
 * @param key The coalescing key.
 * @param wait_window_ms Maximum time to wait in milliseconds.
 * @param response_out Location to write response snapshot, if available.
 * @return RegistryWaitResult The result of the wait.
 */
RegistryWaitResult registry_wait_for_completion(InFlightRegistry* registry, const char* key,
                                                std::uint32_t wait_window_ms,
                                                std::uint64_t expected_lifecycle_generation,
                                                RegistrySharedResponseOutput* response_out);

/**

 * @brief Deregisters a follower/waiter from the registry.
 *
 * Called when a follower times out or otherwise cancels its wait.
 *
 * @param registry The registry instance.
 * @param key The coalescing key.
 */
void registry_remove_waiter(InFlightRegistry* registry, const char* key);

} // namespace bytetaper::coalescing

#endif // BYTETAPER_COALESCING_INFLIGHT_REGISTRY_H
