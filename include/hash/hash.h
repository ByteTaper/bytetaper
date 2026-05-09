// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_HASH_HASH_H
#define BYTETAPER_HASH_HASH_H

#include <cstddef>
#include <cstdint>

namespace bytetaper::hash {

struct HashSeed {
    std::uint64_t k0;
    std::uint64_t k1;
};

void init_process_hash_seed();
void set_process_hash_seed_for_test(HashSeed seed);
void reset_process_hash_seed_for_test();
HashSeed process_hash_seed();

std::uint64_t siphash24_bytes(const char* data, std::size_t len, HashSeed seed);
std::uint64_t hash_cstr_keyed(const char* str, HashSeed seed);
std::uint64_t hash_cstr_runtime(const char* str);

} // namespace bytetaper::hash

#endif // BYTETAPER_HASH_HASH_H
