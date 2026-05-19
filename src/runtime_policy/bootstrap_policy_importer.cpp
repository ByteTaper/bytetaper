// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/bootstrap_policy_importer.h"

#include "control_plane/policy_state_key.h"
#include "observability/logger.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/policy_mismatch_classifier.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace bytetaper::runtime_policy {

namespace {

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

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

RuntimePolicyHealth check_bootstrap_drift(const BootstrapImportInput& input,
                                          const control_plane::ActivePolicyPointer& committed) {
    if (input.bootstrap_policy_file == nullptr || input.bootstrap_policy_file[0] == '\0') {
        return RuntimePolicyHealth::Unknown;
    }

    const taperquery::PolicyIrLoadResult load_res =
        taperquery::load_policy_ir_from_yaml_file(input.bootstrap_policy_file);
    if (!load_res.ok) {
        return RuntimePolicyHealth::Unknown;
    }

    const taperquery::PolicyIrYamlEmitResult emit_res =
        taperquery::emit_policy_ir_canonical_yaml(load_res.policy);
    if (!emit_res.ok) {
        return RuntimePolicyHealth::Unknown;
    }

    const std::string policy_id = taperquery::compute_policy_document_identity(load_res.policy);
    const std::string canonical_hash = "sha256:" + compute_sha256_hex(emit_res.yaml);

    if (policy_id != committed.policy_id ||
        strip_sha256_prefix(canonical_hash) != strip_sha256_prefix(committed.canonical_hash)) {
        return RuntimePolicyHealth::BootstrapDriftDetected;
    }

    return RuntimePolicyHealth::Unknown;
}

BootstrapImportResult import_bootstrap_policy(const BootstrapImportInput& input) {
    BootstrapImportResult result{};

    if (input.store == nullptr || input.resource_key == nullptr) {
        result.error = "bootstrap import requires policy state store and resource key";
        result.health = RuntimePolicyHealth::StartupFailed;
        return result;
    }
    if (input.bootstrap_policy_file == nullptr || input.bootstrap_policy_file[0] == '\0') {
        result.error = "bootstrap policy file is not configured";
        result.health = RuntimePolicyHealth::NoPolicyConfigured;
        return result;
    }

    const control_plane::LoadActivePointerResult active =
        input.store->load_active_pointer(*input.resource_key);
    if (active.ok) {
        const RuntimePolicyHealth drift = check_bootstrap_drift(input, active.pointer);
        if (drift == RuntimePolicyHealth::BootstrapDriftDetected) {
            result.health = RuntimePolicyHealth::BootstrapDriftDetected;
            result.error = "bootstrap policy differs from committed active pointer; import skipped";
            return result;
        }
        result.health = RuntimePolicyHealth::Active;
        result.error = "active policy pointer already exists; bootstrap import skipped";
        return result;
    }
    if (!active.not_found) {
        result.error = active.error;
        result.health = RuntimePolicyHealth::StartupFailed;
        return result;
    }

    taperquery::PolicyIrLoadResult load_res =
        taperquery::load_policy_ir_from_yaml_file(input.bootstrap_policy_file);
    if (!load_res.ok) {
        result.error = "failed to load bootstrap policy: " + load_res.error;
        result.health = RuntimePolicyHealth::StartupFailed;
        return result;
    }

    taperquery::TqPolicyDocument doc = load_res.policy;
    doc.generation = 1;
    doc.schema_version_num = 1;
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.policy_id = taperquery::compute_policy_document_identity(doc);

    const taperquery::PolicyIrYamlEmitResult emit_res =
        taperquery::emit_policy_ir_canonical_yaml(doc);
    if (!emit_res.ok) {
        result.error = "failed to emit canonical bootstrap yaml: " + emit_res.error;
        result.health = RuntimePolicyHealth::StartupFailed;
        return result;
    }

    const std::string canonical_hash = "sha256:" + compute_sha256_hex(emit_res.yaml);
    const std::uint64_t committed_at = now_ms();
    const control_plane::PolicyResourceKey& key = *input.resource_key;

    control_plane::PolicyVersionRecord version;
    version.resource_key = key.to_string();
    version.generation = 1;
    version.policy_id = doc.policy_id;
    version.canonical_hash = canonical_hash;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.yaml_key = control_plane::make_yaml_key(key, 1);
    version.source_type = "bootstrap-import";
    version.apply_id = "bootstrap-import";
    version.created_at_unix_epoch_ms = committed_at;

    auto store_version = input.store->store_policy_version(key, version, emit_res.yaml);
    if (!store_version.ok) {
        result.error = "failed to store bootstrap policy version: " + store_version.error;
        result.health = RuntimePolicyHealth::StartupFailed;
        return result;
    }

    control_plane::ActivePolicyPointer pointer;
    pointer.resource_key = key.to_string();
    pointer.generation = 1;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = canonical_hash;
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.version_key = control_plane::make_version_key(key, 1);
    pointer.yaml_key = control_plane::make_yaml_key(key, 1);
    pointer.source_type = "bootstrap-import";
    pointer.apply_id = "bootstrap-import";
    pointer.committed_at_unix_epoch_ms = committed_at;

    control_plane::ExpectedActivePolicy expected;
    expected.generation = 0;
    auto promote = input.store->compare_and_promote_active(key, expected, pointer);
    if (!promote.ok) {
        result.error = "failed to promote bootstrap active pointer: " + promote.error;
        result.health = RuntimePolicyHealth::StartupFailed;
        return result;
    }

    if (input.persistence_config != nullptr && input.persistence_config->enabled) {
        taperquery::PersistedPolicyMetadata meta;
        meta.policy_identity = doc.policy_id;
        meta.generation = 1;
        meta.source_type = "bootstrap-import";
        meta.metadata_schema_version = 1;
        meta.resource_key = key.to_string();
        meta.apply_id = "bootstrap-import";
        meta.schema_version = 1;
        meta.api_version = doc.api_version;
        meta.kind = doc.kind;
        meta.active_policy_file = input.persistence_config->active_policy_filename;
        meta.written_at_unix_epoch_ms = committed_at;
        meta.committed_at_unix_epoch_ms = committed_at;
        meta.canonical_hash_algorithm = "sha256";
        meta.bootstrap.file = input.bootstrap_policy_file;
        meta.bootstrap.role = "initial-default-only";
        meta.bootstrap.overwrite_protection = true;

        const auto persist_res =
            taperquery::persist_active_policy_canonical_yaml(*input.persistence_config, doc, meta);
        if (!persist_res.ok) {
            result.error = "failed to write bootstrap policy mirror: " + persist_res.error;
            result.health = RuntimePolicyHealth::StartupFailed;
            return result;
        }
    }

    auto build_res = runtime::build_runtime_policy_snapshot_from_ir(doc, 1);
    if (!build_res.ok) {
        result.error = "failed to build bootstrap snapshot: " + build_res.error;
        result.health = RuntimePolicyHealth::StartupFailed;
        return result;
    }

    bytetaper::observability::log_info("bootstrap policy imported into policy state store");

    result.ok = true;
    result.health = RuntimePolicyHealth::BootstrapImported;
    result.snapshot = build_res.snapshot;
    return result;
}

} // namespace bytetaper::runtime_policy
