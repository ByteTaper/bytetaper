// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CACHE_L2_DISK_CACHE_H
#define BYTETAPER_CACHE_L2_DISK_CACHE_H

#include "cache/cache_entry.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::cache {

struct L2DiskCache; // opaque; defined in l2_rocksdb_cache.cpp

// Maximum body size stored to L2. Bodies > kL2BodyBufSize and <= kL2MaxBodySize are stored
// for future reuse but bypass follower handoff — see body-size contract in
// include/coalescing/coalescing_completion_handoff.h.
static constexpr std::size_t kL2MaxBodySize = 1 * 1024 * 1024;

// L2CacheOptions — configurable via environment variables at startup:
//   BYTETAPER_L2_BLOCK_CACHE_MB   >= 1 (default 64)
//   BYTETAPER_L2_WRITE_BUFFER_MB  >= 1 (default 32)
//   BYTETAPER_L2_MAX_BACKGROUND_JOBS >= 1 (default 2)
struct L2CacheOptions {
    std::size_t block_cache_mb = 64;
    std::size_t write_buffer_mb = 32;
    std::size_t max_write_buffer_number = 2;
    std::size_t target_file_size_mb = 64;
    int max_background_jobs = 2;
    bool create_if_missing = true;
    bool disable_wal = true;
    bool cache_index_and_filter_blocks = true;
    bool enable_compression = true;
};

// Opens (or creates) a RocksDB database at path. Returns nullptr on failure.
L2DiskCache* l2_open(const char* path);

// Opens with explicit cache-oriented options.
L2DiskCache* l2_open_with_options(const char* path, const L2CacheOptions& options);

// Closes and frees the database handle. Sets *cache to nullptr.
void l2_close(L2DiskCache** cache);

// Destroys the on-disk database at path (use for test cleanup).
bool l2_destroy(const char* path);

// Stores entry in the database. Returns true on success.
bool l2_put(L2DiskCache* cache, const CacheEntry& entry);

enum class L2PutResult : std::uint8_t {
    Stored,
    EncodeError,
    BodyTooLarge,
    StorageError,
};

L2PutResult l2_put_result(L2DiskCache* cache, const CacheEntry& entry, char* enc_buf,
                          std::size_t enc_buf_size);

// Retrieves entry by key.
// body_buf[body_buf_size] is caller-owned storage; out->body points into it on hit.
// If now_ms > 0 and the stored entry is expired, returns false.
// Returns false on miss or if stored body exceeds body_buf_size.
bool l2_get(L2DiskCache* cache, const char* key, std::int64_t now_ms, CacheEntry* out,
            char* body_buf, std::size_t body_buf_size);

enum class L2GetResult : std::uint8_t {
    Hit,
    Miss,
    Expired,
    DecodeError,
    BodyTooLargeForBuffer,
    RocksDbError,
};

L2GetResult l2_get_result(L2DiskCache* cache, const char* key, std::int64_t now_ms, CacheEntry* out,
                          char* body_buf, std::size_t body_buf_size);

// Removes the entry for key. Returns true if the key existed.
bool l2_remove(L2DiskCache* cache, const char* key);

} // namespace bytetaper::cache

#endif // BYTETAPER_CACHE_L2_DISK_CACHE_H
