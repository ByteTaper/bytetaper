// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "hash/hash.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <unistd.h>

namespace bytetaper::hash {

namespace {

static HashSeed g_process_seed{ 0, 0 };
static std::atomic<bool> g_initialized{ false };
static std::mutex g_init_mutex;

inline std::uint64_t rotl(std::uint64_t x, int b) {
    return (x << b) | (x >> (64 - b));
}

inline void sipround(std::uint64_t& v0, std::uint64_t& v1, std::uint64_t& v2, std::uint64_t& v3) {
    v0 += v1;
    v2 += v3;
    v1 = rotl(v1, 13);
    v3 = rotl(v3, 16);
    v1 ^= v0;
    v3 ^= v2;
    v0 = rotl(v0, 32);

    v2 += v1;
    v0 += v3;
    v1 = rotl(v1, 17);
    v3 = rotl(v3, 21);
    v1 ^= v2;
    v3 ^= v0;
    v2 = rotl(v2, 32);
}

bool parse_hex_seed(const char* hex, HashSeed& seed) {
    if (hex == nullptr || std::strlen(hex) != 32) {
        return false;
    }

    // Verify all chars are hex
    for (int i = 0; i < 32; ++i) {
        char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }

    auto parse_64 = [](const char* hex_part) -> std::uint64_t {
        std::uint64_t val = 0;
        for (int i = 0; i < 16; ++i) {
            char c = hex_part[i];
            val <<= 4;
            if (c >= '0' && c <= '9') {
                val |= (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val |= (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                val |= (c - 'A' + 10);
            }
        }
        return val;
    };

    seed.k0 = parse_64(hex);
    seed.k1 = parse_64(hex + 16);
    return true;
}

HashSeed generate_entropy_seed() {
    std::uint64_t k0 = 0;
    std::uint64_t k1 = 0;

    // First attempt: std::random_device
    std::random_device rd;
    std::uint64_t r0 = (static_cast<std::uint64_t>(rd()) << 32) | rd();
    std::uint64_t r1 = (static_cast<std::uint64_t>(rd()) << 32) | rd();
    if (r0 != 0 || r1 != 0) {
        k0 = r0;
        k1 = r1;
    }

    // Always mix with robust secondary entropy to be safe
    std::uint64_t time_entropy = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uint64_t pid_entropy = static_cast<std::uint64_t>(getpid());
    std::uintptr_t addr_entropy = reinterpret_cast<std::uintptr_t>(&g_process_seed);

    k0 ^= time_entropy ^ (pid_entropy << 32) ^ addr_entropy;
    k1 ^= (time_entropy >> 32) ^ pid_entropy ^ (addr_entropy << 32);

    return HashSeed{ k0, k1 };
}

} // namespace

void init_process_hash_seed() {
    if (g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_initialized.load(std::memory_order_relaxed)) {
        return;
    }

    HashSeed seed{};
    const char* hex_env = std::getenv("BYTETAPER_HASH_SEED_HEX");
    if (hex_env != nullptr && parse_hex_seed(hex_env, seed)) {
        g_process_seed = seed;
    } else {
        g_process_seed = generate_entropy_seed();
    }

    g_initialized.store(true, std::memory_order_release);
}

bool validate_hash_seed_hex(const char* value) {
    if (value == nullptr || std::strlen(value) != 32) {
        return false;
    }
    for (int i = 0; i < 32; ++i) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

void set_process_hash_seed_for_test(HashSeed seed) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    g_process_seed = seed;
    g_initialized.store(true, std::memory_order_release);
}

void reset_process_hash_seed_for_test() {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    g_process_seed = { 0, 0 };
    g_initialized.store(false, std::memory_order_release);
}

HashSeed process_hash_seed() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        init_process_hash_seed();
    }
    return g_process_seed;
}

std::uint64_t siphash24_bytes(const char* data, std::size_t len, HashSeed seed) {
    if (data == nullptr) {
        len = 0;
    }
    const std::uint8_t* m = reinterpret_cast<const std::uint8_t*>(data);

    std::uint64_t v0 = seed.k0 ^ 0x736f6d6570736575ULL;
    std::uint64_t v1 = seed.k1 ^ 0x646f72616e646f6dULL;
    std::uint64_t v2 = seed.k0 ^ 0x6c7967656e657261ULL;
    std::uint64_t v3 = seed.k1 ^ 0x7465646279746573ULL;

    const std::size_t end = len & ~7;
    for (std::size_t offset = 0; offset < end; offset += 8) {
        std::uint64_t mi = 0;
        for (int i = 0; i < 8; ++i) {
            mi |= (static_cast<std::uint64_t>(m[offset + i]) << (i * 8));
        }
        v3 ^= mi;
        sipround(v0, v1, v2, v3);
        sipround(v0, v1, v2, v3);
        v0 ^= mi;
    }

    const std::size_t left = len & 7;
    std::uint64_t last = static_cast<std::uint64_t>(len & 0xff) << 56;
    switch (left) {
    case 7:
        last |= static_cast<std::uint64_t>(m[end + 6]) << 48; // fallthrough
    case 6:
        last |= static_cast<std::uint64_t>(m[end + 5]) << 40; // fallthrough
    case 5:
        last |= static_cast<std::uint64_t>(m[end + 4]) << 32; // fallthrough
    case 4:
        last |= static_cast<std::uint64_t>(m[end + 3]) << 24; // fallthrough
    case 3:
        last |= static_cast<std::uint64_t>(m[end + 2]) << 16; // fallthrough
    case 2:
        last |= static_cast<std::uint64_t>(m[end + 1]) << 8; // fallthrough
    case 1:
        last |= static_cast<std::uint64_t>(m[end + 0]);
        break;
    case 0:
        break;
    }

    v3 ^= last;
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);
    v0 ^= last;

    v2 ^= 0xff;
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);

    return v0 ^ v1 ^ v2 ^ v3;
}

std::uint64_t hash_cstr_keyed(const char* str, HashSeed seed) {
    if (str == nullptr) {
        return siphash24_bytes(nullptr, 0, seed);
    }
    return siphash24_bytes(str, std::strlen(str), seed);
}

std::uint64_t hash_cstr_runtime(const char* str) {
    if (!g_initialized.load(std::memory_order_acquire)) {
        init_process_hash_seed();
    }
    return hash_cstr_keyed(str, g_process_seed);
}

} // namespace bytetaper::hash
