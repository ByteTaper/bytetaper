// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry_codec.h"
#include "cache/cache_ttl.h"
#include "cache/l2_disk_cache.h"

#include <cstring>
#include <memory>
#include <rocksdb/cache.h>
#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/table.h>
#include <string>
#include <vector>

namespace bytetaper::cache {

struct L2DiskCache {
    rocksdb::DB* db = nullptr;
    std::shared_ptr<rocksdb::Cache> block_cache;
    rocksdb::WriteOptions write_options{};
    rocksdb::ReadOptions read_options{};
};

L2DiskCache* l2_open_with_options(const char* path, const L2CacheOptions& options) {
    if (!path)
        return nullptr;

    rocksdb::BlockBasedTableOptions table_opts;
    table_opts.block_cache = rocksdb::NewLRUCache(options.block_cache_mb * 1024 * 1024);
    table_opts.cache_index_and_filter_blocks = options.cache_index_and_filter_blocks;

    rocksdb::Options db_opts;
    db_opts.create_if_missing = options.create_if_missing;
    db_opts.write_buffer_size = options.write_buffer_mb * 1024 * 1024;
    db_opts.max_write_buffer_number = static_cast<int>(options.max_write_buffer_number);
    db_opts.target_file_size_base = options.target_file_size_mb * 1024 * 1024;
    db_opts.compression =
        options.enable_compression ? rocksdb::kLZ4Compression : rocksdb::kNoCompression;
    db_opts.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_opts));
    db_opts.IncreaseParallelism(options.max_background_jobs);

    std::unique_ptr<rocksdb::DB> raw_db;
    rocksdb::Status status = rocksdb::DB::Open(db_opts, path, &raw_db);
    if (!status.ok())
        return nullptr;

    auto* cache = new L2DiskCache{};
    cache->db = raw_db.release();
    cache->block_cache = table_opts.block_cache;
    cache->write_options.disableWAL = options.disable_wal;
    cache->read_options.fill_cache = true;
    return cache;
}

L2DiskCache* l2_open(const char* path) {
    return l2_open_with_options(path, L2CacheOptions{});
}

void l2_close(L2DiskCache** cache) {
    if (!cache || !*cache)
        return;

    delete (*cache)->db;
    delete *cache;
    *cache = nullptr;
}

bool l2_destroy(const char* path) {
    if (!path)
        return false;

    rocksdb::Status status = rocksdb::DestroyDB(path, rocksdb::Options{});
    return status.ok();
}

bool l2_put(L2DiskCache* cache, const CacheEntry& entry) {
    if (!cache || !cache->db)
        return false;

    const std::size_t enc_size = kCacheEntryEncodedOverhead + entry.body_len;
    std::vector<char> enc_buf(enc_size);
    const std::size_t written = cache_entry_encode(entry, enc_buf.data(), enc_buf.size());
    if (written == 0) {
        return false;
    }

    rocksdb::Status status =
        cache->db->Put(cache->write_options, entry.key, rocksdb::Slice(enc_buf.data(), written));
    return status.ok();
}

bool l2_get(L2DiskCache* cache, const char* key, std::int64_t now_ms, CacheEntry* out,
            char* body_buf, std::size_t body_buf_size) {
    if (!cache || !cache->db || !key || !out)
        return false;

    std::string raw;
    rocksdb::Status status = cache->db->Get(cache->read_options, key, &raw);
    if (!status.ok()) {
        return false;
    }

    CacheEntry decoded{};
    bool ok = cache_entry_decode(raw.data(), raw.size(), &decoded, body_buf, body_buf_size);
    if (!ok) {
        return false;
    }

    // TTL Check
    if (now_ms > 0 && !cache_ttl_valid(now_ms, decoded.expires_at_epoch_ms)) {
        return false;
    }

    *out = decoded;
    return true;
}

bool l2_remove(L2DiskCache* cache, const char* key) {
    if (!cache || !cache->db || !key)
        return false;

    rocksdb::Status status = cache->db->Delete(cache->write_options, key);
    return status.ok();
}

} // namespace bytetaper::cache
