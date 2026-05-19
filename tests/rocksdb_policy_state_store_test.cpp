// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/policy_state_result.h"
#include "control_plane/rocksdb_policy_state_store.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <random>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <sstream>
#include <string>

namespace bytetaper::control_plane {
namespace {

namespace fs = std::filesystem;

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    const auto suffix = rng();
    return (fs::temp_directory_path() / ("bt_pss_test_" + std::to_string(suffix))).string();
}

void destroy_db_path(const std::string& path) {
    rocksdb::DestroyDB(path, rocksdb::Options{});
    std::error_code ec;
    fs::remove_all(path, ec);
}

class SHA256 {
public:
    SHA256() {
        state_[0] = 0x6a09e667;
        state_[1] = 0xbb67ae85;
        state_[2] = 0x3c6ef372;
        state_[3] = 0xa54ff53a;
        state_[4] = 0x510e527f;
        state_[5] = 0x9b05688c;
        state_[6] = 0x1f83d9ab;
        state_[7] = 0x5be0cd19;
        bitlen_ = 0;
        datalen_ = 0;
    }

    void update(const std::uint8_t* bytes, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            data_[datalen_++] = bytes[i];
            if (datalen_ == 64) {
                transform();
                bitlen_ += 512;
                datalen_ = 0;
            }
        }
    }

    void final(std::uint8_t hash[32]) {
        std::uint32_t i = datalen_;
        if (datalen_ < 56) {
            data_[i++] = 0x80;
            while (i < 56) {
                data_[i++] = 0x00;
            }
        } else {
            data_[i++] = 0x80;
            while (i < 64) {
                data_[i++] = 0x00;
            }
            transform();
            std::memset(data_, 0, 56);
        }

        bitlen_ += datalen_ * 8;
        data_[56] = bitlen_ >> 56;
        data_[57] = bitlen_ >> 48;
        data_[58] = bitlen_ >> 40;
        data_[59] = bitlen_ >> 32;
        data_[60] = bitlen_ >> 24;
        data_[61] = bitlen_ >> 16;
        data_[62] = bitlen_ >> 8;
        data_[63] = bitlen_;
        transform();

        for (std::uint32_t j = 0; j < 4; ++j) {
            hash[j] = (state_[0] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 4] = (state_[1] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 8] = (state_[2] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 12] = (state_[3] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 16] = (state_[4] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 20] = (state_[5] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 24] = (state_[6] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 28] = (state_[7] >> (24 - j * 8)) & 0x000000ff;
        }
    }

private:
    void transform() {
        std::uint32_t m[64];
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::uint32_t c = 0;
        std::uint32_t d = 0;
        std::uint32_t e = 0;
        std::uint32_t f = 0;
        std::uint32_t g = 0;
        std::uint32_t h = 0;

        for (std::size_t i = 0, j = 0; i < 16; ++i, j += 4) {
            m[i] = (data_[j] << 24) | (data_[j + 1] << 16) | (data_[j + 2] << 8) | data_[j + 3];
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = ((m[i - 15] >> 7) | (m[i - 15] << 25)) ^
                                     ((m[i - 15] >> 18) | (m[i - 15] << 14)) ^ (m[i - 15] >> 3);
            const std::uint32_t s1 = ((m[i - 2] >> 17) | (m[i - 2] << 15)) ^
                                     ((m[i - 2] >> 19) | (m[i - 2] << 13)) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }

        a = state_[0];
        b = state_[1];
        c = state_[2];
        d = state_[3];
        e = state_[4];
        f = state_[5];
        g = state_[6];
        h = state_[7];

        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2
        };

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t sig0 =
                ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t1 = h + sig0 + maj + k[i] + m[i];
            const std::uint32_t sig1 =
                ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t2 = sig1 + ch;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::uint32_t state_[8];
    std::uint64_t bitlen_;
    std::uint8_t data_[64];
    std::uint32_t datalen_;
};

std::string compute_sha256_hex(const std::string& input) {
    SHA256 ctx;
    ctx.update(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    std::uint8_t hash[32];
    ctx.final(hash);
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

PolicyVersionRecord make_version_record(const PolicyResourceKey& key, std::uint64_t generation,
                                        const std::string& policy_id,
                                        const std::string& canonical_hash) {
    PolicyVersionRecord record;
    record.resource_key = key.to_string();
    record.generation = generation;
    record.policy_id = policy_id;
    record.canonical_hash = canonical_hash;
    record.schema_version = 1;
    record.api_version = "bytetaper.io/v1alpha1";
    record.kind = "RuntimePolicy";
    record.yaml_key = make_yaml_key(key, generation);
    record.source_type = "taperql-apply";
    record.apply_id = "apply-" + std::to_string(generation);
    record.created_at_unix_epoch_ms = 1'700'000'000'000ULL + generation;
    return record;
}

ActivePolicyPointer make_active_pointer(const PolicyResourceKey& key, std::uint64_t generation,
                                        const std::string& policy_id,
                                        const std::string& canonical_hash) {
    ActivePolicyPointer pointer;
    pointer.resource_key = key.to_string();
    pointer.generation = generation;
    pointer.policy_id = policy_id;
    pointer.canonical_hash = canonical_hash;
    pointer.schema_version = 1;
    pointer.api_version = "bytetaper.io/v1alpha1";
    pointer.kind = "RuntimePolicy";
    pointer.version_key = make_version_key(key, generation);
    pointer.yaml_key = make_yaml_key(key, generation);
    pointer.source_type = "taperql-apply";
    pointer.apply_id = "apply-" + std::to_string(generation);
    pointer.committed_at_unix_epoch_ms = 1'700'000'000'000ULL + generation;
    return pointer;
}

class RocksDBPolicyStateStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db_path(db_path_);
    }

    void TearDown() override {
        destroy_db_path(db_path_);
    }

    std::string db_path_;
};

} // namespace

TEST_F(RocksDBPolicyStateStoreTest, OpenCreatesDbWhenMissing) {
    RocksDBPolicyStateStore store(db_path_.c_str());
    EXPECT_TRUE(store.is_open());
    EXPECT_TRUE(store.open_error().empty());
    EXPECT_TRUE(fs::exists(db_path_));
}

TEST_F(RocksDBPolicyStateStoreTest, OpenFailsOnInvalidPath) {
    const std::string invalid_path = db_path_ + "/not-a-directory";
    {
        std::ofstream blocker(invalid_path);
        blocker << "blocker";
    }

    RocksDBPolicyStateStore store(invalid_path.c_str());
    EXPECT_FALSE(store.is_open());
    EXPECT_FALSE(store.open_error().empty());
}

TEST_F(RocksDBPolicyStateStoreTest, StoreAndLoadPolicyVersion) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string yaml = "apiVersion: bytetaper.io/v1alpha1\nkind: RuntimePolicy\n";
    const std::string hash = "sha256:" + compute_sha256_hex(yaml);

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version = make_version_record(key, 1, "sha256:abcd1234", hash);
    ASSERT_TRUE(store.store_policy_version(key, version, yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.canonical_yaml, yaml);
    EXPECT_EQ(loaded.record.generation, 1u);
    EXPECT_EQ(loaded.record.policy_id, "sha256:abcd1234");
}

TEST_F(RocksDBPolicyStateStoreTest, StoreSameVersionSameHashIdempotent) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string yaml = "routes: []\n";
    const std::string hash = "sha256:" + compute_sha256_hex(yaml);

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version = make_version_record(key, 1, "sha256:policy1", hash);
    ASSERT_TRUE(store.store_policy_version(key, version, yaml).ok);
    ASSERT_TRUE(store.store_policy_version(key, version, yaml).ok);
}

TEST_F(RocksDBPolicyStateStoreTest, StoreSameVersionSameHashMismatchedYamlFails) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string yaml_a = "routes: []\n";
    const std::string yaml_b = "routes: [ { id: r1 } ]\n";
    const std::string hash_a = "sha256:" + compute_sha256_hex(yaml_a);

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version = make_version_record(key, 1, "sha256:policy1", hash_a);
    ASSERT_TRUE(store.store_policy_version(key, version, yaml_a).ok);

    PolicyVersionRecord mismatch = version;
    auto res = store.store_policy_version(key, mismatch, yaml_b);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.code, PolicyStateErrorCode::VersionConflict);
}

TEST_F(RocksDBPolicyStateStoreTest, StoreSameVersionDifferentHashFails) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string yaml_a = "routes: []\n";
    const std::string yaml_b = "routes: [ { id: r1 } ]\n";
    const std::string hash_a = "sha256:" + compute_sha256_hex(yaml_a);

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version = make_version_record(key, 1, "sha256:policy1", hash_a);
    ASSERT_TRUE(store.store_policy_version(key, version, yaml_a).ok);

    PolicyVersionRecord conflict = version;
    conflict.canonical_hash = "sha256:" + compute_sha256_hex(yaml_b);
    auto res = store.store_policy_version(key, conflict, yaml_b);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.code, PolicyStateErrorCode::VersionConflict);
}

TEST_F(RocksDBPolicyStateStoreTest, PromoteActiveFromEmptySucceeds) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string yaml = "generation: 1\n";
    const std::string hash = "sha256:" + compute_sha256_hex(yaml);

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version = make_version_record(key, 1, "sha256:policy1", hash);
    ASSERT_TRUE(store.store_policy_version(key, version, yaml).ok);

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ActivePolicyPointer next = make_active_pointer(key, 1, "sha256:policy1", hash);
    auto promote = store.compare_and_promote_active(key, expected, next);
    ASSERT_TRUE(promote.ok) << promote.error;

    auto active = store.load_active_pointer(key);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 1u);
}

TEST_F(RocksDBPolicyStateStoreTest, PromoteActiveMatchingExpectedSucceeds) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    const std::string yaml1 = "generation: 1\n";
    const std::string hash1 = "sha256:" + compute_sha256_hex(yaml1);
    ASSERT_TRUE(
        store.store_policy_version(key, make_version_record(key, 1, "sha256:policy1", hash1), yaml1)
            .ok);

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store
                    .compare_and_promote_active(
                        key, expected, make_active_pointer(key, 1, "sha256:policy1", hash1))
                    .ok);

    const std::string yaml2 = "generation: 2\n";
    const std::string hash2 = "sha256:" + compute_sha256_hex(yaml2);
    ASSERT_TRUE(
        store.store_policy_version(key, make_version_record(key, 2, "sha256:policy2", hash2), yaml2)
            .ok);

    ExpectedActivePolicy expected_gen1;
    expected_gen1.generation = 1;
    expected_gen1.policy_id = "sha256:policy1";
    ActivePolicyPointer next = make_active_pointer(key, 2, "sha256:policy2", hash2);
    next.previous_generation = 1;
    next.previous_policy_id = "sha256:policy1";

    auto promote = store.compare_and_promote_active(key, expected_gen1, next);
    ASSERT_TRUE(promote.ok) << promote.error;

    auto active = store.load_active_pointer(key);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 2u);
    EXPECT_EQ(active.pointer.policy_id, "sha256:policy2");
}

TEST_F(RocksDBPolicyStateStoreTest, PromoteActiveStaleFails) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    const std::string yaml1 = "generation: 1\n";
    const std::string hash1 = "sha256:" + compute_sha256_hex(yaml1);
    ASSERT_TRUE(
        store.store_policy_version(key, make_version_record(key, 1, "sha256:policy1", hash1), yaml1)
            .ok);

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store
                    .compare_and_promote_active(
                        key, expected, make_active_pointer(key, 1, "sha256:policy1", hash1))
                    .ok);

    const std::string yaml2 = "generation: 2\n";
    const std::string hash2 = "sha256:" + compute_sha256_hex(yaml2);
    ASSERT_TRUE(
        store.store_policy_version(key, make_version_record(key, 2, "sha256:policy2", hash2), yaml2)
            .ok);

    ExpectedActivePolicy stale;
    stale.generation = 99;
    stale.policy_id = "sha256:policy1";
    ActivePolicyPointer next = make_active_pointer(key, 2, "sha256:policy2", hash2);
    auto promote = store.compare_and_promote_active(key, stale, next);
    EXPECT_FALSE(promote.ok);
    EXPECT_EQ(promote.code, PolicyStateErrorCode::ActivePointerConflict);
}

TEST_F(RocksDBPolicyStateStoreTest, PromoteAlreadyActivePtrIdempotent) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    const std::string yaml = "generation: 1\n";
    const std::string hash = "sha256:" + compute_sha256_hex(yaml);
    ASSERT_TRUE(
        store.store_policy_version(key, make_version_record(key, 1, "sha256:policy1", hash), yaml)
            .ok);

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ActivePolicyPointer active = make_active_pointer(key, 1, "sha256:policy1", hash);
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, active).ok);

    ExpectedActivePolicy expected_gen1;
    expected_gen1.generation = 1;
    expected_gen1.policy_id = "sha256:policy1";
    auto promote = store.compare_and_promote_active(key, expected_gen1, active);
    EXPECT_TRUE(promote.ok);
    EXPECT_TRUE(promote.idempotent);
}

TEST_F(RocksDBPolicyStateStoreTest, ActivePointerTargetMissingDetected) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ActivePolicyPointer next = make_active_pointer(key, 99, "sha256:missing", "sha256:deadbeef");
    auto promote = store.compare_and_promote_active(key, expected, next);
    EXPECT_FALSE(promote.ok);
    EXPECT_EQ(promote.code, PolicyStateErrorCode::ActivePointerTargetMissing);
}

TEST_F(RocksDBPolicyStateStoreTest, CanonicalYamlHashPreserved) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string yaml = "apiVersion: bytetaper.io/v1alpha1\nkind: RuntimePolicy\nroutes: []\n";
    const std::string hash = "sha256:" + compute_sha256_hex(yaml);

    RocksDBPolicyStateStore store(db_path_.c_str());
    ASSERT_TRUE(store.is_open());

    ASSERT_TRUE(
        store.store_policy_version(key, make_version_record(key, 1, "sha256:policy1", hash), yaml)
            .ok);
    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    std::string expected = loaded.record.canonical_hash;
    if (expected.rfind("sha256:", 0) == 0) {
        expected = expected.substr(7);
    }
    EXPECT_EQ(compute_sha256_hex(loaded.canonical_yaml), expected);
}

TEST_F(RocksDBPolicyStateStoreTest, AuditRecordAppendSucceeds) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string apply_id = "apply-20260519-001";

    {
        RocksDBPolicyStateStore store(db_path_.c_str());
        ASSERT_TRUE(store.is_open());

        PolicyAuditRecord audit;
        audit.apply_id = apply_id;
        audit.generation = 1;
        audit.policy_id = "sha256:policy1";
        audit.source_type = "taperql-apply";
        audit.operator_id = "operator-1";
        audit.request_id = "req-1";
        audit.recorded_at_unix_epoch_ms = 1'700'000'000'000ULL;
        ASSERT_TRUE(store.append_audit_record(key, audit).ok);
    }

    rocksdb::Options opts;
    opts.create_if_missing = false;
    std::unique_ptr<rocksdb::DB> raw_db;
    ASSERT_TRUE(rocksdb::DB::Open(opts, db_path_, &raw_db).ok());

    std::string json;
    const std::string audit_key = make_audit_key(key, apply_id);
    ASSERT_TRUE(raw_db->Get(rocksdb::ReadOptions{}, audit_key, &json).ok());

    PolicyAuditRecord loaded;
    ASSERT_TRUE(deserialize_audit_record(json, &loaded));
    EXPECT_EQ(loaded.apply_id, apply_id);
    EXPECT_EQ(loaded.policy_id, "sha256:policy1");
    EXPECT_EQ(loaded.operator_id, "operator-1");
}

TEST_F(RocksDBPolicyStateStoreTest, StoreSurvivesCloseReopen) {
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const std::string yaml = "generation: 1\n";
    const std::string hash = "sha256:" + compute_sha256_hex(yaml);

    {
        RocksDBPolicyStateStore store(db_path_.c_str());
        ASSERT_TRUE(store.is_open());
        ASSERT_TRUE(store
                        .store_policy_version(
                            key, make_version_record(key, 1, "sha256:policy1", hash), yaml)
                        .ok);

        ExpectedActivePolicy expected;
        expected.generation = 0;
        ASSERT_TRUE(store
                        .compare_and_promote_active(
                            key, expected, make_active_pointer(key, 1, "sha256:policy1", hash))
                        .ok);
    }

    RocksDBPolicyStateStore reopened(db_path_.c_str());
    ASSERT_TRUE(reopened.is_open());
    auto active = reopened.load_active_pointer(key);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 1u);
    EXPECT_EQ(active.pointer.policy_id, "sha256:policy1");
}

} // namespace bytetaper::control_plane
