// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/inflight_registry.h"

#include "metrics/coalescing_metrics.h"

#include <atomic>
#include <chrono>
#include <cstring>

namespace bytetaper::coalescing {

namespace {

/**
 * @brief Simple FNV-1a hash for strings.
 */
std::uint64_t hash_string(const char* s) {
    std::uint64_t hash = 14695981039346656037ULL;
    if (s == nullptr)
        return hash;
    while (*s) {
        hash ^= static_cast<std::uint64_t>(*s++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static std::atomic<std::uint64_t> g_lifecycle_counter{ 1 };

} // namespace

void registry_init(InFlightRegistry* registry) {
    if (registry == nullptr)
        return;
    for (std::size_t i = 0; i < kInFlightShards; ++i) {
        std::lock_guard<std::mutex> lock(registry->shards[i].mutex);
        for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
            registry->shards[i].slots[j].active = false;
            registry->shards[i].slots[j].state = CoalescingState::LeaderRunning;
            registry->shards[i].slots[j].lifecycle_generation = 0;
            registry->shards[i].slots[j].leader_request_id = 0;
            registry->shards[i].slots[j].shared_response = {};
        }
    }
}

static constexpr std::uint32_t kResultRetentionWindowMs = 50;

RegistryRegistrationResult registry_register(InFlightRegistry* registry, const char* key,
                                             std::uint64_t now_ms, std::uint32_t wait_window_ms,
                                             std::uint32_t max_waiters_per_key,
                                             std::uint64_t leader_request_id) {
    RegistryRegistrationResult res{};
    if (registry == nullptr || key == nullptr) {
        res.role = InFlightRole::Reject;
        res.attach_failure_reason = AttachFailureReason::ShardFull;
        return res;
    }

    std::uint64_t hash = hash_string(key);
    res.key_hash = hash;

    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mutex);

    // Linear probing within the shard's slots
    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];

        // Check for existing active entry
        if (slot.active && std::strcmp(slot.key, key) == 0) {
            if (is_terminal(slot.state)) {
                CoalescingState orig_state = slot.state;
                res.state_before = orig_state;

                bool should_recycle = false;
                if (orig_state == CoalescingState::ResultReady) {
                    if (now_ms >= slot.completed_at_epoch_ms + kResultRetentionWindowMs) {
                        should_recycle = true;
                    }
                } else {
                    should_recycle = true;
                }

                if (should_recycle) {
                    if (orig_state == CoalescingState::ResultReady && slot.waiter_count > 0) {
                        res.role = InFlightRole::Reject;
                        res.attach_failure_reason = AttachFailureReason::StateMismatch;
                        res.state_after = orig_state;
                        return res;
                    }
                    // treat as new leader
                    slot.state = CoalescingState::LeaderRunning;
                    slot.shared_response = {};
                    slot.created_at_epoch_ms = now_ms;
                    slot.completed_at_epoch_ms = 0;
                    slot.waiter_count = 0;
                    slot.active = true;
                    slot.lifecycle_generation =
                        g_lifecycle_counter.fetch_add(1, std::memory_order_relaxed);
                    slot.leader_request_id = leader_request_id;

                    res.role = InFlightRole::Leader;
                    res.state_after = CoalescingState::LeaderRunning;
                    res.lifecycle_generation = slot.lifecycle_generation;
                    res.leader_request_id = slot.leader_request_id;
                    res.group_id = static_cast<std::uint32_t>(res.lifecycle_generation);
                    return res;
                }

                // Still within retention window, join as follower
                if (slot.waiter_count < max_waiters_per_key) {
                    slot.waiter_count++;
                    res.role = InFlightRole::Follower;
                    res.state_after = orig_state;
                    res.lifecycle_generation = slot.lifecycle_generation;
                    res.leader_request_id = slot.leader_request_id;
                    res.group_id = static_cast<std::uint32_t>(res.lifecycle_generation);
                    res.terminal_result_join_flag = true;
                    return res;
                } else {
                    res.role = InFlightRole::Reject;
                    res.attach_failure_reason = AttachFailureReason::MaxWaitersEnforced;
                    res.state_after = orig_state;
                    return res;
                }
            } else {
                // LeaderRunning
                CoalescingState orig_state = slot.state;
                res.state_before = orig_state;

                if (now_ms >= slot.created_at_epoch_ms + wait_window_ms) {
                    if (slot.waiter_count > 0) {
                        res.role = InFlightRole::Reject;
                        res.attach_failure_reason = AttachFailureReason::StateMismatch;
                        res.state_after = orig_state;
                        return res;
                    }
                    // treat as new leader (leader timed out)
                    slot.state = CoalescingState::LeaderRunning;
                    slot.shared_response = {};
                    slot.created_at_epoch_ms = now_ms;
                    slot.completed_at_epoch_ms = 0;
                    slot.waiter_count = 0;
                    slot.active = true;
                    slot.lifecycle_generation =
                        g_lifecycle_counter.fetch_add(1, std::memory_order_relaxed);
                    slot.leader_request_id = leader_request_id;

                    res.role = InFlightRole::Leader;
                    res.state_after = CoalescingState::LeaderRunning;
                    res.lifecycle_generation = slot.lifecycle_generation;
                    res.leader_request_id = slot.leader_request_id;
                    res.group_id = static_cast<std::uint32_t>(res.lifecycle_generation);
                    return res;
                }

                // Still in flight, check waiter limit
                if (slot.waiter_count < max_waiters_per_key) {
                    slot.waiter_count++;
                    res.role = InFlightRole::Follower;
                    res.state_after = orig_state;
                    res.lifecycle_generation = slot.lifecycle_generation;
                    res.leader_request_id = slot.leader_request_id;
                    res.group_id = static_cast<std::uint32_t>(res.lifecycle_generation);
                    return res;
                } else {
                    res.role = InFlightRole::Reject;
                    res.attach_failure_reason = AttachFailureReason::MaxWaitersEnforced;
                    res.state_after = orig_state;
                    return res;
                }
            }
        }
    }

    // Try to find an empty or reusable slot in the shard for a DIFFERENT key
    InFlightEntry* best_reusable_slot = nullptr;
    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];
        if (!slot.active) {
            best_reusable_slot = &slot;
            break;
        }

        if (is_terminal(slot.state) && slot.waiter_count == 0) {
            bool can_reuse = false;
            if (slot.state == CoalescingState::ResultReady) {
                if (now_ms >= slot.completed_at_epoch_ms + kResultRetentionWindowMs) {
                    can_reuse = true;
                }
            } else {
                can_reuse = true;
            }

            if (can_reuse) {
                best_reusable_slot = &slot;
                break;
            }
        }
    }

    if (best_reusable_slot != nullptr) {
        res.state_before =
            best_reusable_slot->active ? best_reusable_slot->state : CoalescingState::LeaderRunning;

        std::strncpy(best_reusable_slot->key, key, sizeof(best_reusable_slot->key) - 1);
        best_reusable_slot->key[sizeof(best_reusable_slot->key) - 1] = '\0';
        best_reusable_slot->created_at_epoch_ms = now_ms;
        best_reusable_slot->completed_at_epoch_ms = 0;
        best_reusable_slot->waiter_count = 0;
        best_reusable_slot->state = CoalescingState::LeaderRunning;
        best_reusable_slot->shared_response = {};
        best_reusable_slot->active = true;
        best_reusable_slot->lifecycle_generation =
            g_lifecycle_counter.fetch_add(1, std::memory_order_relaxed);
        best_reusable_slot->leader_request_id = leader_request_id;

        res.role = InFlightRole::Leader;
        res.state_after = CoalescingState::LeaderRunning;
        res.lifecycle_generation = best_reusable_slot->lifecycle_generation;
        res.leader_request_id = best_reusable_slot->leader_request_id;
        res.group_id = static_cast<std::uint32_t>(res.lifecycle_generation);
        return res;
    }

    // [BT-130-005] Shard full, instantly drop traffic
    res.role = InFlightRole::Reject;
    res.attach_failure_reason = AttachFailureReason::ShardFull;
    res.state_before = CoalescingState::LeaderRunning;
    res.state_after = CoalescingState::LeaderRunning;
    return res;
}

bool registry_complete_with_response(InFlightRegistry* registry, const char* key,
                                     std::uint16_t status_code, const char* content_type,
                                     const char* body, std::size_t body_len, std::uint64_t now_ms) {
    if (registry == nullptr || key == nullptr) {
        return false;
    }

    if (body_len > kCoalescingSharedBodyMaxSize) {
        return registry_complete_state(registry, key, CoalescingState::NotCacheable, now_ms);
    }

    std::uint64_t hash = hash_string(key);
    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];

    bool found = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);

        for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
            InFlightEntry& slot = shard.slots[j];
            if (slot.active && std::strcmp(slot.key, key) == 0) {
                slot.state = CoalescingState::ResultReady;
                slot.completed_at_epoch_ms = now_ms;

                slot.shared_response.status_code = status_code;
                if (content_type != nullptr) {
                    std::strncpy(slot.shared_response.content_type, content_type,
                                 sizeof(slot.shared_response.content_type) - 1);
                    slot.shared_response
                        .content_type[sizeof(slot.shared_response.content_type) - 1] = '\0';
                } else {
                    slot.shared_response.content_type[0] = '\0';
                }
                if (body != nullptr && body_len > 0) {
                    std::memcpy(slot.shared_response.body, body, body_len);
                }
                slot.shared_response.body_len = body_len;
                slot.shared_response.ready = true;

                found = true;
                break;
            }
        }
    }

    if (found) {
        shard.cv.notify_all();
    }
    return found;
}

bool registry_complete_state(InFlightRegistry* registry, const char* key, CoalescingState state,
                             std::uint64_t now_ms) {
    if (registry == nullptr || key == nullptr) {
        return false;
    }

    std::uint64_t hash = hash_string(key);
    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];

    bool found = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);

        for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
            InFlightEntry& slot = shard.slots[j];
            if (slot.active && std::strcmp(slot.key, key) == 0) {
                slot.state = state;
                slot.completed_at_epoch_ms = now_ms;

                found = true;
                break;
            }
        }
    }

    if (found) {
        shard.cv.notify_all();
    }
    return found;
}

void registry_remove_waiter(InFlightRegistry* registry, const char* key) {
    if (registry == nullptr || key == nullptr) {
        return;
    }

    std::uint64_t hash = hash_string(key);
    InFlightShard& shard = registry->shards[hash % kInFlightShards];

    std::lock_guard<std::mutex> lock(shard.mutex);

    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];
        if (slot.active && std::strcmp(slot.key, key) == 0) {
            if (slot.waiter_count > 0) {
                slot.waiter_count--;
            }
            return;
        }
    }
}

RegistryWaitResult registry_wait_for_completion(InFlightRegistry* registry, const char* key,
                                                std::uint32_t wait_window_ms,
                                                RegistrySharedResponseOutput* response_out) {
    if (registry == nullptr || key == nullptr) {
        return RegistryWaitResult::Missing;
    }

    const std::uint64_t hash = hash_string(key);
    InFlightShard& shard = registry->shards[hash % kInFlightShards];

    std::unique_lock<std::mutex> lock(shard.mutex);

    // 1. Find the entry
    InFlightEntry* entry = nullptr;
    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        if (shard.slots[j].active && std::strcmp(shard.slots[j].key, key) == 0) {
            entry = &shard.slots[j];
            break;
        }
    }

    if (entry == nullptr) {
        return RegistryWaitResult::Missing;
    }

    // 2. Already completed?
    if (is_terminal(entry->state)) {
        if (entry->state == CoalescingState::ResultReady) {
            if (entry->shared_response.ready) {
                if (response_out != nullptr) {
                    response_out->status_code = entry->shared_response.status_code;
                    std::memcpy(response_out->content_type, entry->shared_response.content_type,
                                sizeof(response_out->content_type));
                    std::memcpy(response_out->body, entry->shared_response.body,
                                entry->shared_response.body_len);
                    response_out->body_len = entry->shared_response.body_len;
                }
                return RegistryWaitResult::SharedResponseReady;
            }
            return RegistryWaitResult::StoredButNoSnapshot;
        } else if (entry->state == CoalescingState::NotCacheable) {
            return RegistryWaitResult::NotCacheable;
        } else if (entry->state == CoalescingState::LeaderFailed) {
            return RegistryWaitResult::Failed;
        } else if (entry->state == CoalescingState::TimedOut) {
            return RegistryWaitResult::Timeout;
        } else if (entry->state == CoalescingState::Cancelled) {
            return RegistryWaitResult::Missing;
        }
    }

    // 3. Wait for notification or timeout
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_window_ms);

    const bool signalled = shard.cv.wait_until(
        lock, deadline, [entry] { return is_terminal(entry->state) || !entry->active; });

    if (!signalled) {
        return RegistryWaitResult::Timeout;
    }

    if (!entry->active) {
        return RegistryWaitResult::Missing;
    }

    if (entry->state == CoalescingState::ResultReady) {
        if (entry->shared_response.ready) {
            if (response_out != nullptr) {
                response_out->status_code = entry->shared_response.status_code;
                std::memcpy(response_out->content_type, entry->shared_response.content_type,
                            sizeof(response_out->content_type));
                std::memcpy(response_out->body, entry->shared_response.body,
                            entry->shared_response.body_len);
                response_out->body_len = entry->shared_response.body_len;
            }
            return RegistryWaitResult::SharedResponseReady;
        }
        return RegistryWaitResult::StoredButNoSnapshot;
    } else if (entry->state == CoalescingState::NotCacheable) {
        return RegistryWaitResult::NotCacheable;
    } else if (entry->state == CoalescingState::LeaderFailed) {
        return RegistryWaitResult::Failed;
    } else if (entry->state == CoalescingState::TimedOut) {
        return RegistryWaitResult::Timeout;
    } else if (entry->state == CoalescingState::Cancelled) {
        return RegistryWaitResult::Missing;
    }

    return RegistryWaitResult::Missing;
}

void registry_evaluate_group_invariants_and_summary(InFlightRegistry* registry, const char* key,
                                                    std::uint64_t now_ms,
                                                    metrics::CoalescingMetrics* metrics) {
    if (registry == nullptr || key == nullptr) {
        return;
    }
    std::uint64_t hash = hash_string(key);
    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];
    std::lock_guard<std::mutex> lock(shard.mutex);

    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];
        if (slot.active && std::strcmp(slot.key, key) == 0) {
            std::uint64_t duration_ms =
                (now_ms >= slot.created_at_epoch_ms) ? (now_ms - slot.created_at_epoch_ms) : 0;

            std::printf("[COALESCING SUMMARY] Thread group completed processing for key: %s\n",
                        key);
            std::printf("  - Key Hash: %llu, Group ID: %u\n", (unsigned long long) hash,
                        static_cast<std::uint32_t>(hash));
            std::printf("  - Duration (monotonic): %llu ms\n", (unsigned long long) duration_ms);
            std::printf("  - State: %d, Completed Epoch: %llu ms\n", static_cast<int>(slot.state),
                        (unsigned long long) slot.completed_at_epoch_ms);
            std::printf("  - Waiter Count: %u\n", slot.waiter_count);
            std::printf("  - Shared Response: status=%u, len=%zu, ready=%s\n",
                        slot.shared_response.status_code, slot.shared_response.body_len,
                        slot.shared_response.ready ? "true" : "false");

            bool invariant_failed = false;
            if (!is_terminal(slot.state)) {
                invariant_failed = true;
            }
            if (slot.state == CoalescingState::ResultReady) {
                if (!slot.shared_response.ready || slot.shared_response.status_code < 200 ||
                    slot.shared_response.status_code >= 300) {
                    invariant_failed = true;
                }
            } else {
                if (slot.shared_response.ready && slot.shared_response.status_code != 0) {
                    invariant_failed = true;
                }
            }

            if (invariant_failed) {
                std::printf("  [ERROR] Group Invariant Violation detected for key %s!\n", key);
                if (metrics != nullptr) {
                    record_coalescing_event(metrics,
                                            metrics::CoalescingMetricEvent::GroupInvariantFailures);
                }
            } else {
                std::printf("  [SUCCESS] All Group Invariants passed for key %s.\n", key);
            }
            break;
        }
    }
}

} // namespace bytetaper::coalescing
