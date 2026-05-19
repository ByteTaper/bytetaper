// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_persistence.h"

#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"

#include <array>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace bytetaper::taperquery {

namespace {

// Standard SHA-256 implementation
class SHA256 {
private:
    std::uint32_t state[8];
    std::uint64_t bitlen;
    std::uint8_t data[64];
    std::uint32_t datalen;

    void transform() {
        std::uint32_t m[64];
        std::uint32_t a, b, c, d, e, f, g, h, t1, t2;

        for (std::size_t i = 0, j = 0; i < 16; ++i, j += 4) {
            m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            std::uint32_t s0 = ((m[i - 15] >> 7) | (m[i - 15] << 25)) ^
                               ((m[i - 15] >> 18) | (m[i - 15] << 14)) ^ (m[i - 15] >> 3);
            std::uint32_t s1 = ((m[i - 2] >> 17) | (m[i - 2] << 15)) ^
                               ((m[i - 2] >> 19) | (m[i - 2] << 13)) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }

        a = state[0];
        b = state[1];
        c = state[2];
        d = state[3];
        e = state[4];
        f = state[5];
        g = state[6];
        h = state[7];

        const std::uint32_t k[64] = {
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
            std::uint32_t sig0 =
                ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t1_v = h + sig0 + maj + k[i] + m[i];
            std::uint32_t sig1 =
                ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t2_v = sig1 + ch;
            h = g;
            g = f;
            f = e;
            e = d + t1_v;
            d = c;
            c = b;
            b = a;
            a = t1_v + t2_v;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

public:
    SHA256() {
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
        bitlen = 0;
        datalen = 0;
    }

    void update(const std::uint8_t* bytes, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            data[datalen] = bytes[i];
            datalen++;
            if (datalen == 64) {
                transform();
                bitlen += 512;
                datalen = 0;
            }
        }
    }

    void final(std::uint8_t hash[32]) {
        std::uint32_t i = datalen;
        if (datalen < 56) {
            data[i++] = 0x80;
            while (i < 56) {
                data[i++] = 0x00;
            }
        } else {
            data[i++] = 0x80;
            while (i < 64) {
                data[i++] = 0x00;
            }
            transform();
            std::memset(data, 0, 56);
        }

        bitlen += datalen * 8;
        data[56] = bitlen >> 56;
        data[57] = bitlen >> 48;
        data[58] = bitlen >> 40;
        data[59] = bitlen >> 32;
        data[60] = bitlen >> 24;
        data[61] = bitlen >> 16;
        data[62] = bitlen >> 8;
        data[63] = bitlen;
        transform();

        for (std::uint32_t j = 0; j < 4; ++j) {
            hash[j] = (state[0] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 4] = (state[1] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 8] = (state[2] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 12] = (state[3] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 16] = (state[4] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 20] = (state[5] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 24] = (state[6] >> (24 - j * 8)) & 0x000000ff;
            hash[j + 28] = (state[7] >> (24 - j * 8)) & 0x000000ff;
        }
    }
};

// Returns e.g. "versions/0000000013-sha256_abcd1234.yaml"
// policyId is expected in "sha256:abcd1234..." form; only the first 8 hex chars are used.
static std::string make_versioned_filename(std::uint64_t generation, const std::string& policy_id) {
    char gen_buf[16];
    std::snprintf(gen_buf, sizeof(gen_buf), "%010" PRIu64, generation);

    std::string id_part = policy_id;
    if (id_part.size() >= 7 && id_part.substr(0, 7) == "sha256:") {
        id_part = id_part.substr(7);
    }
    if (id_part.size() > 8) {
        id_part = id_part.substr(0, 8);
    }

    return std::string("versions/") + gen_buf + "-sha256_" + id_part + ".yaml";
}

std::string compute_sha256(const std::string& input) {
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

static std::string escape_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else if (static_cast<unsigned char>(c) < 32) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<int>(c));
            out += hex;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string serialize_metadata(const PersistedPolicyMetadata& meta) {
    if (meta.metadata_schema_version == 0) {
        std::string json = "{\n";
        json += "  \"policy_identity\": \"" + escape_json_string(meta.policy_identity) + "\",\n";
        json += "  \"candidate_policy_identity\": \"" +
                escape_json_string(meta.candidate_policy_identity) + "\",\n";
        json += "  \"persisted_policy_identity\": \"" +
                escape_json_string(meta.persisted_policy_identity) + "\",\n";
        json += "  \"previous_policy_identity\": \"" +
                escape_json_string(meta.previous_policy_identity) + "\",\n";
        json += "  \"expected_base_identity\": \"" +
                escape_json_string(meta.expected_base_identity) + "\",\n";
        json += "  \"generation\": " + std::to_string(meta.generation) + ",\n";
        json += "  \"source_type\": \"" + escape_json_string(meta.source_type) + "\",\n";
        json += "  \"written_at_unix_epoch_ms\": " + std::to_string(meta.written_at_unix_epoch_ms) +
                ",\n";
        json += "  \"operator_id\": \"" + escape_json_string(meta.operator_id) + "\",\n";
        json += "  \"request_id\": \"" + escape_json_string(meta.request_id) + "\",\n";
        json += "  \"canonical_yaml_sha256\": \"" + escape_json_string(meta.canonical_yaml_sha256) +
                "\",\n";
        json +=
            "  \"active_policy_file\": \"" + escape_json_string(meta.active_policy_file) + "\",\n";
        json += "  \"canonical_hash\": \"" + escape_json_string(meta.canonical_hash) + "\",\n";
        json += "  \"canonical_hash_algorithm\": \"" +
                escape_json_string(meta.canonical_hash_algorithm) + "\",\n";
        json += "  \"versioned_policy_file\": \"" + escape_json_string(meta.versioned_policy_file) +
                "\"\n";
        json += "}\n";
        return json;
    }

    std::string json = "{\n";
    json += "  \"metadataSchemaVersion\": " + std::to_string(meta.metadata_schema_version) + ",\n";
    json += "  \"resourceKey\": \"" + escape_json_string(meta.resource_key) + "\",\n";
    json += "  \"generation\": " + std::to_string(meta.generation) + ",\n";
    json += "  \"policyId\": \"" + escape_json_string(meta.policy_identity) + "\",\n";
    json += "  \"candidatePolicyIdentity\": \"" +
            escape_json_string(meta.candidate_policy_identity) + "\",\n";
    json += "  \"persistedPolicyIdentity\": \"" +
            escape_json_string(meta.persisted_policy_identity) + "\",\n";
    json += "  \"previousPolicyIdentity\": \"" + escape_json_string(meta.previous_policy_identity) +
            "\",\n";
    json += "  \"expectedBaseIdentity\": \"" + escape_json_string(meta.expected_base_identity) +
            "\",\n";
    json += "  \"sourceType\": \"" + escape_json_string(meta.source_type) + "\",\n";
    json += "  \"writtenAtUnixEpochMs\": " + std::to_string(meta.written_at_unix_epoch_ms) + ",\n";
    json += "  \"operatorId\": \"" + escape_json_string(meta.operator_id) + "\",\n";
    json += "  \"requestId\": \"" + escape_json_string(meta.request_id) + "\",\n";
    json += "  \"applyId\": \"" + escape_json_string(meta.apply_id) + "\",\n";
    json += "  \"previousGeneration\": " + std::to_string(meta.previous_generation) + ",\n";
    json += "  \"previousPolicyId\": \"" + escape_json_string(meta.previous_policy_id) + "\",\n";
    json += "  \"schemaVersion\": " + std::to_string(meta.schema_version) + ",\n";
    json += "  \"apiVersion\": \"" + escape_json_string(meta.api_version) + "\",\n";
    json += "  \"kind\": \"" + escape_json_string(meta.kind) + "\",\n";
    json += "  \"activePolicyFile\": \"" + escape_json_string(meta.active_policy_file) + "\",\n";
    json +=
        "  \"versionedPolicyFile\": \"" + escape_json_string(meta.versioned_policy_file) + "\",\n";
    json +=
        "  \"committedAtUnixEpochMs\": " + std::to_string(meta.committed_at_unix_epoch_ms) + ",\n";
    json += "  \"canonicalHash\": \"" + escape_json_string(meta.canonical_hash) + "\",\n";
    json += "  \"canonicalHashAlgorithm\": \"" + escape_json_string(meta.canonical_hash_algorithm) +
            "\",\n";
    json += "  \"bootstrap\": {\n";
    json += "    \"file\": \"" + escape_json_string(meta.bootstrap.file) + "\",\n";
    json += "    \"role\": \"" + escape_json_string(meta.bootstrap.role) + "\",\n";
    json += "    \"overwriteProtection\": " +
            std::string(meta.bootstrap.overwrite_protection ? "true" : "false") + "\n";
    json += "  },\n";
    json += "  \"compatibility\": {\n";
    json += "    \"policyIrVersion\": \"" +
            escape_json_string(meta.compatibility.policy_ir_version) + "\",\n";
    json += "    \"identityVersion\": \"" +
            escape_json_string(meta.compatibility.identity_version) + "\",\n";
    json += "    \"emitterVersion\": \"" + escape_json_string(meta.compatibility.emitter_version) +
            "\",\n";
    json += "    \"runtimeMinVersion\": \"" +
            escape_json_string(meta.compatibility.runtime_min_version) + "\",\n";
    json += "    \"runtimeCapabilityProfile\": \"" +
            escape_json_string(meta.compatibility.runtime_capability_profile) + "\"\n";
    json += "  }\n";
    json += "}\n";
    return json;
}

std::string get_json_string_field(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    std::size_t pos = json.find(search_key);
    if (pos == std::string::npos)
        return "";

    std::size_t colon_pos = json.find(':', pos + search_key.size());
    if (colon_pos == std::string::npos)
        return "";

    std::size_t quote_start = json.find('"', colon_pos);
    if (quote_start == std::string::npos)
        return "";

    std::size_t quote_end = quote_start + 1;
    std::string val;
    while (quote_end < json.size()) {
        if (json[quote_end] == '"') {
            break;
        }
        if (json[quote_end] == '\\' && quote_end + 1 < json.size()) {
            char next = json[quote_end + 1];
            if (next == '"')
                val += '"';
            else if (next == '\\')
                val += '\\';
            else if (next == 'n')
                val += '\n';
            else if (next == 't')
                val += '\t';
            else if (next == 'r')
                val += '\r';
            else
                val += next;
            quote_end += 2;
        } else {
            val += json[quote_end];
            quote_end++;
        }
    }
    return val;
}

std::uint64_t get_json_uint64_field(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    std::size_t pos = json.find(search_key);
    if (pos == std::string::npos)
        return 0;

    std::size_t colon_pos = json.find(':', pos + search_key.size());
    if (colon_pos == std::string::npos)
        return 0;

    std::size_t val_start = colon_pos + 1;
    while (val_start < json.size() &&
           (std::isspace(static_cast<unsigned char>(json[val_start])) || json[val_start] == '"')) {
        val_start++;
    }
    std::size_t val_end = val_start;
    while (val_end < json.size() && std::isdigit(static_cast<unsigned char>(json[val_end]))) {
        val_end++;
    }
    if (val_end == val_start)
        return 0;

    return std::stoull(json.substr(val_start, val_end - val_start));
}

bool write_atomic_posix(const std::string& file_path, const std::string& content,
                        std::string& err) {
    std::string tmp_path = file_path + ".tmp";
    int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        err = "Failed to open temporary file: " + std::string(std::strerror(errno));
        return false;
    }

    std::size_t total_written = 0;
    while (total_written < content.size()) {
        ssize_t bytes = write(fd, content.data() + total_written, content.size() - total_written);
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            err = "Failed to write temporary file: " + std::string(std::strerror(errno));
            close(fd);
            unlink(tmp_path.c_str());
            return false;
        }
        total_written += bytes;
    }

    if (fsync(fd) < 0) {
        err = "Failed to fsync temporary file: " + std::string(std::strerror(errno));
        close(fd);
        unlink(tmp_path.c_str());
        return false;
    }

    if (close(fd) < 0) {
        err = "Failed to close temporary file: " + std::string(std::strerror(errno));
        unlink(tmp_path.c_str());
        return false;
    }

    if (rename(tmp_path.c_str(), file_path.c_str()) < 0) {
        err = "Failed to rename temporary file to target: " + std::string(std::strerror(errno));
        unlink(tmp_path.c_str());
        return false;
    }

    return true;
}

void fsync_directory(const std::string& dir_path) {
    int dir_fd = open(dir_path.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }
}

} // namespace

PolicyPersistenceWriteResult
persist_active_policy_canonical_yaml(const LocalPolicyPersistenceConfig& config,
                                     const TqPolicyDocument& document,
                                     const PersistedPolicyMetadata& metadata) {
    PolicyPersistenceWriteResult res;
    if (!config.enabled) {
        res.ok = true;
        return res;
    }

    if (config.state_dir.empty()) {
        res.ok = false;
        res.error = "State directory is empty";
        return res;
    }

    std::error_code ec;
    std::filesystem::create_directories(config.state_dir, ec);
    if (ec) {
        res.ok = false;
        res.error = "Failed to create state directory: " + ec.message();
        return res;
    }

    std::string yaml_path =
        (std::filesystem::path(config.state_dir) / config.active_policy_filename).string();
    std::string meta_path =
        (std::filesystem::path(config.state_dir) / config.metadata_filename).string();

    res.active_policy_path = yaml_path;
    res.metadata_path = meta_path;

    // 1. Emit canonical YAML
    PolicyIrYamlEmitResult emit_res = emit_policy_ir_canonical_yaml(document);
    if (!emit_res.ok) {
        res.ok = false;
        res.error = "Canonical YAML emission failed: " + emit_res.error;
        return res;
    }

    // 2. Compute SHA-256 over raw YAML bytes (hex, no prefix)
    std::string yaml_sha = compute_sha256(emit_res.yaml);
    const std::string canonical_hash_full = "sha256:" + yaml_sha;
    const std::string versioned_rel =
        make_versioned_filename(metadata.generation, metadata.policy_identity);
    const std::filesystem::path versions_dir = std::filesystem::path(config.state_dir) / "versions";
    const std::string versioned_path =
        (std::filesystem::path(config.state_dir) / versioned_rel).string();

    std::error_code ec_mk;
    std::filesystem::create_directories(versions_dir, ec_mk);
    if (ec_mk) {
        res.ok = false;
        res.error =
            "Failed to create versions directory (VERSIONED_POLICY_DIRECTORY_CREATE_FAILED): " +
            ec_mk.message();
        return res;
    }

    std::string err;
    if (std::filesystem::exists(versioned_path)) {
        std::ifstream existing(versioned_path);
        if (!existing.is_open()) {
            res.ok = false;
            res.error =
                "Failed to open existing versioned policy file (VERSIONED_POLICY_WRITE_FAILED)";
            return res;
        }
        std::stringstream ebuf;
        ebuf << existing.rdbuf();
        const std::string existing_bytes = ebuf.str();
        const std::string existing_sha = compute_sha256(existing_bytes);
        if (existing_sha != yaml_sha) {
            res.ok = false;
            res.error = "Versioned policy generation conflict (VERSIONED_POLICY_CONFLICT)";
            return res;
        }
        // Idempotent: same bytes — skip re-write
    } else {
        if (!write_atomic_posix(versioned_path, emit_res.yaml, err)) {
            res.ok = false;
            res.error = "Versioned policy write failed (VERSIONED_POLICY_WRITE_FAILED): " + err;
            return res;
        }
    }
    fsync_directory(versions_dir.string());

    if (!write_atomic_posix(yaml_path, emit_res.yaml, err)) {
        res.ok = false;
        res.error = err;
        return res;
    }
    fsync_directory(config.state_dir);

    PersistedPolicyMetadata updated_meta = metadata;
    updated_meta.canonical_yaml_sha256 = yaml_sha;
    updated_meta.canonical_hash = canonical_hash_full;
    updated_meta.canonical_hash_algorithm = "sha256";
    updated_meta.versioned_policy_file = versioned_rel;
    std::string meta_json = serialize_metadata(updated_meta);

    // Write metadata to .tmp, fsync, rename, fsync dir
    if (!write_atomic_posix(meta_path, meta_json, err)) {
        res.ok = false;
        res.error = err;
        return res;
    }
    fsync_directory(config.state_dir);

    res.ok = true;
    return res;
}

PolicyPersistenceLoadResult
load_persisted_active_policy(const LocalPolicyPersistenceConfig& config) {
    PolicyPersistenceLoadResult res;
    if (!config.enabled) {
        res.ok = false;
        res.error = "Persistence is disabled";
        return res;
    }

    if (config.state_dir.empty()) {
        res.ok = false;
        res.error = "State directory is empty";
        return res;
    }

    std::string yaml_path =
        (std::filesystem::path(config.state_dir) / config.active_policy_filename).string();
    std::string meta_path =
        (std::filesystem::path(config.state_dir) / config.metadata_filename).string();

    // 1. Check if files exist
    bool yaml_exists = std::filesystem::exists(yaml_path);
    bool meta_exists = std::filesystem::exists(meta_path);
    if (!yaml_exists && !meta_exists) {
        res.ok = false;
        res.error = "Persisted active policy and metadata files do not exist";
        res.files_missing = true;
        return res;
    } else if (!yaml_exists || !meta_exists) {
        res.ok = false;
        res.error = "Incomplete persisted state: one of active policy or metadata is missing";
        res.files_missing = false;
        return res;
    }

    // 2. Read and parse metadata JSON
    std::ifstream meta_file(meta_path);
    if (!meta_file.is_open()) {
        res.ok = false;
        res.error = "Failed to open metadata file: " + meta_path;
        return res;
    }
    std::stringstream meta_buf;
    meta_buf << meta_file.rdbuf();
    std::string meta_json = meta_buf.str();
    meta_file.close();

    PersistedPolicyMetadata meta;

    bool has_new_schema = (meta_json.find("\"metadataSchemaVersion\"") != std::string::npos);

    if (has_new_schema) {
        meta.metadata_schema_version =
            static_cast<std::uint32_t>(get_json_uint64_field(meta_json, "metadataSchemaVersion"));
        meta.resource_key = get_json_string_field(meta_json, "resourceKey");
        meta.generation = get_json_uint64_field(meta_json, "generation");
        meta.policy_identity = get_json_string_field(meta_json, "policyId");

        meta.candidate_policy_identity =
            get_json_string_field(meta_json, "candidatePolicyIdentity");
        meta.persisted_policy_identity =
            get_json_string_field(meta_json, "persistedPolicyIdentity");
        meta.previous_policy_identity = get_json_string_field(meta_json, "previousPolicyIdentity");
        meta.expected_base_identity = get_json_string_field(meta_json, "expectedBaseIdentity");

        meta.source_type = get_json_string_field(meta_json, "sourceType");
        meta.written_at_unix_epoch_ms = get_json_uint64_field(meta_json, "writtenAtUnixEpochMs");
        meta.operator_id = get_json_string_field(meta_json, "operatorId");
        meta.request_id = get_json_string_field(meta_json, "requestId");
        meta.canonical_hash = get_json_string_field(meta_json, "canonicalHash");
        meta.canonical_hash_algorithm = get_json_string_field(meta_json, "canonicalHashAlgorithm");

        meta.apply_id = get_json_string_field(meta_json, "applyId");
        meta.previous_generation = get_json_uint64_field(meta_json, "previousGeneration");
        meta.previous_policy_id = get_json_string_field(meta_json, "previousPolicyId");
        meta.schema_version =
            static_cast<std::uint32_t>(get_json_uint64_field(meta_json, "schemaVersion"));
        meta.api_version = get_json_string_field(meta_json, "apiVersion");
        meta.kind = get_json_string_field(meta_json, "kind");
        meta.active_policy_file = get_json_string_field(meta_json, "activePolicyFile");
        meta.versioned_policy_file = get_json_string_field(meta_json, "versionedPolicyFile");
        meta.committed_at_unix_epoch_ms =
            get_json_uint64_field(meta_json, "committedAtUnixEpochMs");

        meta.bootstrap.file = get_json_string_field(meta_json, "file");
        meta.bootstrap.role = get_json_string_field(meta_json, "role");

        std::size_t op_pos = meta_json.find("\"overwriteProtection\"");
        if (op_pos != std::string::npos) {
            std::size_t colon = meta_json.find(':', op_pos);
            if (colon != std::string::npos) {
                std::size_t true_pos = meta_json.find("true", colon);
                std::size_t false_pos = meta_json.find("false", colon);
                if (true_pos != std::string::npos &&
                    (false_pos == std::string::npos || true_pos < false_pos)) {
                    meta.bootstrap.overwrite_protection = true;
                } else if (false_pos != std::string::npos) {
                    meta.bootstrap.overwrite_protection = false;
                }
            }
        }

        meta.compatibility.policy_ir_version = get_json_string_field(meta_json, "policyIrVersion");
        meta.compatibility.identity_version = get_json_string_field(meta_json, "identityVersion");
        meta.compatibility.emitter_version = get_json_string_field(meta_json, "emitterVersion");
        meta.compatibility.runtime_min_version =
            get_json_string_field(meta_json, "runtimeMinVersion");
        meta.compatibility.runtime_capability_profile =
            get_json_string_field(meta_json, "runtimeCapabilityProfile");
    } else {
        // Fall back to legacy snake_case fields
        meta.metadata_schema_version = 0;
        meta.policy_identity = get_json_string_field(meta_json, "policy_identity");
        meta.candidate_policy_identity =
            get_json_string_field(meta_json, "candidate_policy_identity");
        meta.persisted_policy_identity =
            get_json_string_field(meta_json, "persisted_policy_identity");
        meta.previous_policy_identity =
            get_json_string_field(meta_json, "previous_policy_identity");
        meta.expected_base_identity = get_json_string_field(meta_json, "expected_base_identity");
        meta.generation = get_json_uint64_field(meta_json, "generation");
        meta.source_type = get_json_string_field(meta_json, "source_type");
        meta.written_at_unix_epoch_ms =
            get_json_uint64_field(meta_json, "written_at_unix_epoch_ms");
        meta.operator_id = get_json_string_field(meta_json, "operator_id");
        meta.request_id = get_json_string_field(meta_json, "request_id");
        meta.canonical_yaml_sha256 = get_json_string_field(meta_json, "canonical_yaml_sha256");
        meta.active_policy_file = get_json_string_field(meta_json, "active_policy_file");
        meta.canonical_hash = get_json_string_field(meta_json, "canonical_hash");
        meta.canonical_hash_algorithm =
            get_json_string_field(meta_json, "canonical_hash_algorithm");
        meta.versioned_policy_file = get_json_string_field(meta_json, "versioned_policy_file");
    }

    if (meta.metadata_schema_version > 1) {
        res.ok = false;
        res.error = "Metadata schema version unsupported (METADATA_SCHEMA_UNSUPPORTED)";
        return res;
    }

    // Strict validation for new-schema version 1 required fields
    if (meta.metadata_schema_version == 1) {
        if (meta.resource_key.empty() || meta.generation == 0 || meta.policy_identity.empty() ||
            meta.canonical_hash.empty() || meta.canonical_hash_algorithm.empty() ||
            meta.source_type.empty() || meta.schema_version == 0 || meta.api_version.empty() ||
            meta.kind.empty() || meta.active_policy_file.empty() ||
            meta.written_at_unix_epoch_ms == 0) {

            res.ok = false;
            res.error = "Metadata is missing required fields (METADATA_REQUIRED_FIELD_MISSING)";
            return res;
        }
        if (meta.source_type != "bootstrap-import" && meta.source_type != "taperql-apply" &&
            meta.source_type != "rollback" && meta.source_type != "manual-repair" &&
            meta.source_type != "manual-adopt" && meta.source_type != "unknown") {

            res.ok = false;
            res.error = "Metadata sourceType is invalid (METADATA_REQUIRED_FIELD_MISSING)";
            return res;
        }
    } else {
        // Legacy validation
        if (meta.policy_identity.empty() && meta.canonical_hash.empty()) {
            res.ok = false;
            res.error = "Metadata missing policy identity";
            return res;
        }

        const std::string& integrity_hash =
            meta.canonical_hash.empty() ? meta.canonical_yaml_sha256 : meta.canonical_hash;
        if (integrity_hash.empty()) {
            res.ok = false;
            res.error = "Metadata missing integrity hash (METADATA_REQUIRED_FIELD_MISSING)";
            return res;
        }
    }

    // Path safety checks
    if (!meta.active_policy_file.empty()) {
        if (meta.active_policy_file.find("..") != std::string::npos ||
            meta.active_policy_file.front() == '/') {
            res.ok = false;
            res.error = "Metadata active_policy_file path invalid (METADATA_PATH_INVALID)";
            return res;
        }
    }
    if (!meta.versioned_policy_file.empty()) {
        if (meta.versioned_policy_file.find("..") != std::string::npos ||
            meta.versioned_policy_file.front() == '/') {
            res.ok = false;
            res.error = "Metadata versioned_policy_file path invalid (METADATA_PATH_INVALID)";
            return res;
        }
    }

    // 3. Read and verify YAML file integrity
    std::ifstream yaml_file(yaml_path);
    if (!yaml_file.is_open()) {
        res.ok = false;
        res.error = "Failed to open active policy file: " + yaml_path;
        return res;
    }
    std::stringstream yaml_buf;
    yaml_buf << yaml_file.rdbuf();
    std::string yaml_content = yaml_buf.str();
    yaml_file.close();

    std::string actual_sha = compute_sha256(yaml_content);
    std::string expected_hash =
        meta.metadata_schema_version == 1
            ? meta.canonical_hash
            : (meta.canonical_hash.empty() ? meta.canonical_yaml_sha256 : meta.canonical_hash);
    if (expected_hash.substr(0, 7) == "sha256:") {
        expected_hash = expected_hash.substr(7);
    }
    if (actual_sha != expected_hash) {
        res.ok = false;
        res.error = "Active policy file integrity check failed (METADATA_CANONICAL_HASH_MISMATCH)";
        return res;
    }

    if (!meta.versioned_policy_file.empty()) {
        const std::string versioned_abs =
            (std::filesystem::path(config.state_dir) / meta.versioned_policy_file).string();

        if (!std::filesystem::exists(versioned_abs)) {
            res.ok = false;
            res.error =
                "Versioned policy file missing (VERSIONED_POLICY_MISSING): " + versioned_abs;
            return res;
        }

        std::ifstream vf(versioned_abs);
        if (!vf.is_open()) {
            res.ok = false;
            res.error = "Failed to open versioned policy file: " + versioned_abs;
            return res;
        }
        std::stringstream vbuf;
        vbuf << vf.rdbuf();
        const std::string versioned_content = vbuf.str();
        vf.close();

        const std::string versioned_hash = compute_sha256(versioned_content);
        std::string expected_versioned = meta.canonical_hash;
        if (expected_versioned.size() >= 7 && expected_versioned.substr(0, 7) == "sha256:") {
            expected_versioned = expected_versioned.substr(7);
        }
        if (!expected_versioned.empty() && versioned_hash != expected_versioned) {
            res.ok = false;
            res.error = "Versioned policy hash mismatch (VERSIONED_POLICY_HASH_MISMATCH)";
            return res;
        }
    }

    // 4. Parse YAML using load_policy_ir_from_yaml_file
    PolicyIrLoadResult yaml_load = load_policy_ir_from_yaml_file(yaml_path.c_str());
    if (!yaml_load.ok) {
        res.ok = false;
        res.error = "Failed to parse active policy YAML: " + yaml_load.error;
        return res;
    }

    // 5. Cross-validate against YAML contents for schema version 1
    if (meta.metadata_schema_version == 1) {
        if (yaml_load.policy.generation != meta.generation) {
            res.ok = false;
            res.error =
                "Generation mismatch between YAML and metadata (METADATA_GENERATION_MISMATCH)";
            return res;
        }

        if (yaml_load.policy.policy_id != meta.policy_identity) {
            res.ok = false;
            res.error = "PolicyId mismatch between YAML and metadata (METADATA_POLICY_ID_MISMATCH)";
            return res;
        }

        if (yaml_load.policy.schema_version_num != meta.schema_version) {
            res.ok = false;
            res.error = "Schema version mismatch between YAML and metadata";
            return res;
        }

        if (yaml_load.policy.api_version != meta.api_version) {
            res.ok = false;
            res.error = "API version unsupported or mismatch (METADATA_API_VERSION_UNSUPPORTED)";
            return res;
        }

        if (yaml_load.policy.kind != meta.kind) {
            res.ok = false;
            res.error = "Kind unsupported or mismatch (METADATA_KIND_UNSUPPORTED)";
            return res;
        }
    }

    // 6. Verify computed identity matches metadata
    std::string computed_identity = compute_policy_document_identity(yaml_load.policy);
    if (computed_identity != meta.policy_identity) {
        res.ok = false;
        res.error = "Policy identity mismatch (computed=" + computed_identity +
                    ", meta=" + meta.policy_identity + ")";
        return res;
    }

    res.ok = true;
    res.document = std::move(yaml_load.policy);
    res.metadata = std::move(meta);
    return res;
}

StartupPolicyLoadResult
load_startup_policy_with_persistence(const StartupPolicyLoadConfig& config) {
    StartupPolicyLoadResult res;

    // Helper to load bootstrap policy
    auto load_bootstrap = [&]() -> StartupPolicyLoadResult {
        StartupPolicyLoadResult bootstrap_res;
        if (config.bootstrap_policy_file.empty()) {
            bootstrap_res.ok = false;
            bootstrap_res.error = "Bootstrap policy file is not configured";
            return bootstrap_res;
        }

        PolicyIrLoadResult load_res =
            load_policy_ir_from_yaml_file(config.bootstrap_policy_file.c_str());
        if (!load_res.ok) {
            bootstrap_res.ok = false;
            bootstrap_res.error = "Failed to load bootstrap policy file: " + load_res.error;
            return bootstrap_res;
        }

        bootstrap_res.ok = true;
        bootstrap_res.loaded_source = "bootstrap";
        bootstrap_res.policy_identity = compute_policy_document_identity(load_res.policy);
        bootstrap_res.generation = 1;
        bootstrap_res.policy_ir = std::move(load_res.policy);
        return bootstrap_res;
    };

    // 1. If persistence disabled, load bootstrap
    if (!config.policy_persistence_enabled) {
        return load_bootstrap();
    }

    // 2. If state directory is empty, load bootstrap
    if (config.policy_state_dir.empty()) {
        return load_bootstrap();
    }

    // 3. Load persisted active policy
    LocalPolicyPersistenceConfig local_config;
    local_config.enabled = true;
    local_config.state_dir = config.policy_state_dir;
    local_config.active_policy_filename = config.active_policy_filename;
    local_config.metadata_filename = config.metadata_filename;

    PolicyPersistenceLoadResult recovery = load_persisted_active_policy(local_config);
    if (recovery.ok) {
        res.ok = true;
        res.loaded_source = "persisted";
        res.policy_identity = recovery.metadata.policy_identity;
        res.generation = recovery.metadata.generation;
        res.policy_ir = std::move(recovery.document);
        return res;
    }

    // If recovery failed, check if files were missing vs. corrupt
    if (recovery.files_missing) {
        // Safe, first-time bootstrap fallback
        return load_bootstrap();
    }

    // Integrity check failed or file is corrupt / partially missing
    if (config.fallback_to_bootstrap_on_persisted_policy_error) {
        return load_bootstrap();
    }

    // Strict fail closed
    res.ok = false;
    res.error = "Persisted active policy file exists but is corrupt: " + recovery.error;
    return res;
}

} // namespace bytetaper::taperquery
