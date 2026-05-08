// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CACHE_CACHE_ENTRY_CODEC_H
#define BYTETAPER_CACHE_CACHE_ENTRY_CODEC_H

#include "cache/cache_entry.h"

#include <cstddef>

namespace bytetaper::cache {

// Fixed overhead bytes in an encoded entry (header without body).
// char[kCacheKeyMaxLen]          key
// uint16_t                       status_code
// char[kCacheContentTypeMaxLen]  content_type
// uint64_t                       body_len
// int64_t                        created_at_epoch_ms
// int64_t                        expires_at_epoch_ms
static constexpr std::size_t kCacheEntryEncodedOverhead =
    kCacheKeyMaxLen           // 1024
    + sizeof(std::uint16_t)   //    2
    + kCacheContentTypeMaxLen //   64
    + sizeof(std::uint64_t)   //    8
    + sizeof(std::int64_t)    //    8
    + sizeof(std::int64_t);   //    8
                              // = 1114 bytes with current constants

// Encodes entry into buf[buf_size]. Returns bytes written, or 0 on failure
// (buf too small or entry.body_len + kCacheEntryEncodedOverhead > buf_size).
std::size_t cache_entry_encode(const CacheEntry& entry, char* buf, std::size_t buf_size);

// Decodes buf[buf_size] into *out. body_buf[body_buf_size] is caller storage;
// on success out->body points into body_buf. Returns true on success.
bool cache_entry_decode(const char* buf, std::size_t buf_size, CacheEntry* out, char* body_buf,
                        std::size_t body_buf_size);

} // namespace bytetaper::cache

#endif // BYTETAPER_CACHE_CACHE_ENTRY_CODEC_H
