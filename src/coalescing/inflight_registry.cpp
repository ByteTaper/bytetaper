// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/inflight_registry.h"

#include "hash/hash.h"

#include <chrono>
#include <cstring>

namespace bytetaper::coalescing {

namespace {

bool terminal_entry_expired(const InFlightEntry& entry, std::uint64_t now_ms,
                            std::uint32_t wait_window_ms) {
    if (!entry.active)
        return false;
    if (!is_terminal(entry.state))
        return false;
    if (entry.waiter_count > 0)
        return false;
    return now_ms >= entry.completed_at_epoch_ms + wait_window_ms;
}

void reset_entry_for_reuse(InFlightEntry* entry) {
    if (entry == nullptr)
        return;
    entry->key[0] = '\0';
    entry->created_at_epoch_ms = 0;
    entry->completed_at_epoch_ms = 0;
    entry->waiter_count = 0;
    entry->active = false;
    entry->state = InFlightCompletionState::InFlight;
    entry->shared_response = {};
}

} // namespace

void registry_init(InFlightRegistry* registry) {
    if (registry == nullptr)
        return;
    registry->active_waiters.store(0, std::memory_order_relaxed);
    for (std::size_t i = 0; i < kInFlightShards; ++i) {
        std::lock_guard<std::mutex> lock(registry->shards[i].mutex);
        registry->shards[i].active_waiters.store(0, std::memory_order_relaxed);
        for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
            registry->shards[i].slots[j].active = false;
            registry->shards[i].slots[j].state = InFlightCompletionState::InFlight;
            registry->shards[i].slots[j].shared_response = {};
        }
    }
}

RegistryRegistrationResult registry_register(InFlightRegistry* registry, const char* key,
                                             std::uint64_t now_ms, std::uint32_t wait_window_ms,
                                             std::uint32_t max_waiters_per_key) {
    if (registry == nullptr || key == nullptr) {
        return { InFlightRole::Reject, 0 };
    }

    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mutex);

    // Linear probing within the shard's slots
    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];

        // Check for existing active entry
        if (slot.active && std::strcmp(slot.key, key) == 0) {
            // Check if completed and stored/L1Ready
            if (slot.state == InFlightCompletionState::Stored ||
                slot.state == InFlightCompletionState::L1Ready) {
                if (now_ms >= slot.completed_at_epoch_ms + wait_window_ms) {
                    // treat as new leader
                    slot.state = InFlightCompletionState::InFlight;
                    slot.shared_response = {};
                    slot.created_at_epoch_ms = now_ms;
                    slot.waiter_count = 0;
                    slot.active = true;
                    slot.lifecycle_generation++;
                    return { InFlightRole::Leader, slot.lifecycle_generation };
                }

                // Still within grace window, join as follower
                if (slot.waiter_count < max_waiters_per_key) {
                    slot.waiter_count++;
                    return { InFlightRole::Follower, slot.lifecycle_generation };
                } else {
                    return { InFlightRole::Reject, 0 };
                }
            } else if (slot.state == InFlightCompletionState::NotCacheable ||
                       slot.state == InFlightCompletionState::Failed) {
                // Completed but not cacheable or failed, any subsequent request starts a new leader
                slot.state = InFlightCompletionState::InFlight;
                slot.shared_response = {};
                slot.created_at_epoch_ms = now_ms;
                slot.waiter_count = 0;
                slot.active = true;
                slot.lifecycle_generation++;
                return { InFlightRole::Leader, slot.lifecycle_generation };
            } else {
                // InFlight
                if (now_ms >= slot.created_at_epoch_ms + wait_window_ms) {
                    // Treat as new leader
                    std::strncpy(slot.key, key, sizeof(slot.key) - 1);
                    slot.created_at_epoch_ms = now_ms;
                    slot.state = InFlightCompletionState::InFlight;
                    slot.shared_response = {};
                    slot.waiter_count = 0;
                    slot.active = true;
                    slot.lifecycle_generation++;
                    return { InFlightRole::Leader, slot.lifecycle_generation };
                }

                // Still in flight, check waiter limit
                if (slot.waiter_count < max_waiters_per_key) {
                    slot.waiter_count++;
                    return { InFlightRole::Follower, slot.lifecycle_generation };
                } else {
                    return { InFlightRole::Reject, 0 };
                }
            }
        }
    }

    // Try to find an empty or reusable slot. Reclaim expired terminal entries first.
    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];
        if (terminal_entry_expired(slot, now_ms, wait_window_ms)) {
            reset_entry_for_reuse(&slot);
        }
        if (!slot.active) {
            std::strncpy(slot.key, key, sizeof(slot.key) - 1);
            slot.created_at_epoch_ms = now_ms;
            slot.completed_at_epoch_ms = 0;
            slot.waiter_count = 0;
            slot.state = InFlightCompletionState::InFlight;
            slot.shared_response = {};
            slot.active = true;
            slot.lifecycle_generation++;
            return { InFlightRole::Leader, slot.lifecycle_generation };
        }
    }

    // Shard full, instantly drop traffic
    return { InFlightRole::Reject, 0 };
}

bool registry_complete_with_response(InFlightRegistry* registry, const char* key,
                                     std::uint16_t status_code, const char* content_type,
                                     const char* body, std::size_t body_len, std::uint64_t now_ms) {
    if (registry == nullptr || key == nullptr) {
        return false;
    }

    if (body_len > kCoalescingSharedBodyMaxSize) {
        return registry_complete_state(registry, key, InFlightCompletionState::NotCacheable,
                                       now_ms);
    }

    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mutex);

    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];
        if (slot.active && std::strcmp(slot.key, key) == 0) {
            slot.state = InFlightCompletionState::Stored;
            slot.completed_at_epoch_ms = now_ms;

            slot.shared_response.status_code = status_code;
            if (content_type != nullptr) {
                std::strncpy(slot.shared_response.content_type, content_type,
                             sizeof(slot.shared_response.content_type) - 1);
                slot.shared_response.content_type[sizeof(slot.shared_response.content_type) - 1] =
                    '\0';
            } else {
                slot.shared_response.content_type[0] = '\0';
            }
            if (body != nullptr && body_len > 0) {
                std::memcpy(slot.shared_response.body, body, body_len);
            }
            slot.shared_response.body_len = body_len;
            slot.shared_response.ready = true;

            shard.cv.notify_all();
            return true;
        }
    }
    return false;
}

bool registry_complete_state(InFlightRegistry* registry, const char* key,
                             InFlightCompletionState state, std::uint64_t now_ms) {
    if (registry == nullptr || key == nullptr) {
        return false;
    }

    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mutex);

    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];
        if (slot.active && std::strcmp(slot.key, key) == 0) {
            slot.state = state;
            slot.completed_at_epoch_ms = now_ms;

            shard.cv.notify_all();
            return true;
        }
    }
    return false;
}

bool registry_complete_state_if_generation(InFlightRegistry* registry, const char* key,
                                           std::uint64_t expected_generation,
                                           InFlightCompletionState state, std::uint64_t now_ms) {
    if (registry == nullptr || key == nullptr) {
        return false;
    }

    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
    std::size_t shard_idx = hash % kInFlightShards;
    InFlightShard& shard = registry->shards[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mutex);

    for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
        InFlightEntry& slot = shard.slots[j];
        if (slot.active && std::strcmp(slot.key, key) == 0) {
            if (expected_generation != 0 && slot.lifecycle_generation != expected_generation) {
                return false;
            }
            slot.state = state;
            slot.completed_at_epoch_ms = now_ms;

            shard.cv.notify_all();
            return true;
        }
    }
    return false;
}

void registry_remove_waiter(InFlightRegistry* registry, const char* key) {
    if (registry == nullptr || key == nullptr) {
        return;
    }

    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
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
                                                std::uint64_t expected_lifecycle_generation,
                                                RegistrySharedResponseOutput* response_out) {
    if (registry == nullptr || key == nullptr) {
        return RegistryWaitResult::Missing;
    }

    const std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
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

    if (expected_lifecycle_generation != 0 &&
        entry->lifecycle_generation != expected_lifecycle_generation) {
        return RegistryWaitResult::Expired;
    }

    // 2. Already completed?
    if (is_terminal(entry->state)) {
        if (entry->state == InFlightCompletionState::Stored) {
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
        } else if (entry->state == InFlightCompletionState::L1Ready) {
            return RegistryWaitResult::L1Ready;
        } else if (entry->state == InFlightCompletionState::L2Ready) {
            return RegistryWaitResult::L2Ready;
        } else if (entry->state == InFlightCompletionState::TooLargeForHandoff) {
            return RegistryWaitResult::TooLargeForHandoff;
        } else if (entry->state == InFlightCompletionState::NotCacheable) {
            return RegistryWaitResult::NotCacheable;
        } else if (entry->state == InFlightCompletionState::Failed) {
            return RegistryWaitResult::Failed;
        }
    }

    // 3. Wait for notification or timeout
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_window_ms);

    const bool signalled =
        shard.cv.wait_until(lock, deadline, [entry, expected_lifecycle_generation] {
            return is_terminal(entry->state) || !entry->active ||
                   (expected_lifecycle_generation != 0 &&
                    entry->lifecycle_generation != expected_lifecycle_generation);
        });

    if (!signalled) {
        return RegistryWaitResult::Timeout;
    }

    if (!entry->active || (expected_lifecycle_generation != 0 &&
                           entry->lifecycle_generation != expected_lifecycle_generation)) {
        if (expected_lifecycle_generation != 0) {
            return RegistryWaitResult::Expired;
        }
        return RegistryWaitResult::Missing;
    }

    if (entry->state == InFlightCompletionState::Stored) {
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
    } else if (entry->state == InFlightCompletionState::L1Ready) {
        return RegistryWaitResult::L1Ready;
    } else if (entry->state == InFlightCompletionState::L2Ready) {
        return RegistryWaitResult::L2Ready;
    } else if (entry->state == InFlightCompletionState::TooLargeForHandoff) {
        return RegistryWaitResult::TooLargeForHandoff;
    } else if (entry->state == InFlightCompletionState::NotCacheable) {
        return RegistryWaitResult::NotCacheable;
    } else if (entry->state == InFlightCompletionState::Failed) {
        return RegistryWaitResult::Failed;
    }

    return RegistryWaitResult::Missing;
}

std::uint32_t registry_active_waiters(const InFlightRegistry* registry) {
    if (registry == nullptr) {
        return 0;
    }
    return registry->active_waiters.load(std::memory_order_relaxed);
}

std::uint32_t registry_shard_active_waiters(const InFlightRegistry* registry, const char* key) {
    if (registry == nullptr || key == nullptr) {
        return 0;
    }
    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
    std::size_t shard_idx = hash % kInFlightShards;
    return registry->shards[shard_idx].active_waiters.load(std::memory_order_relaxed);
}

void registry_enter_wait(InFlightRegistry* registry, const char* key) {
    if (registry == nullptr || key == nullptr) {
        return;
    }
    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
    std::size_t shard_idx = hash % kInFlightShards;
    registry->active_waiters.fetch_add(1, std::memory_order_relaxed);
    registry->shards[shard_idx].active_waiters.fetch_add(1, std::memory_order_relaxed);
}

void registry_exit_wait(InFlightRegistry* registry, const char* key) {
    if (registry == nullptr || key == nullptr) {
        return;
    }
    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
    std::size_t shard_idx = hash % kInFlightShards;
    registry->active_waiters.fetch_sub(1, std::memory_order_relaxed);
    registry->shards[shard_idx].active_waiters.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace bytetaper::coalescing
