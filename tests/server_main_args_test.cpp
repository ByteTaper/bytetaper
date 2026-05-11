// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/startup_parse.h"
#include "hash/hash.h"

#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::extproc::startup {

// Helper class to isolate env var mutations in tests
class EnvGuard {
public:
    EnvGuard(const char* name) : name_(name) {
        const char* val = std::getenv(name);
        if (val != nullptr) {
            has_original_ = true;
            original_ = val;
        }
    }

    ~EnvGuard() {
        if (has_original_) {
            setenv(name_, original_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

    void set(const char* value) {
        setenv(name_, value, 1);
    }

    void clear() {
        unsetenv(name_);
    }

private:
    const char* name_;
    bool has_original_ = false;
    std::string original_;
};

// --- ParseU16Port Tests ---

TEST(ServerMainArgsTest, ParseU16Port_Valid) {
    std::uint16_t port = 0;
    EXPECT_TRUE(parse_u16_port("--metrics-port", "1", &port));
    EXPECT_EQ(port, 1);

    EXPECT_TRUE(parse_u16_port("--metrics-port", "8080", &port));
    EXPECT_EQ(port, 8080);

    EXPECT_TRUE(parse_u16_port("--metrics-port", "65535", &port));
    EXPECT_EQ(port, 65535);
}

TEST(ServerMainArgsTest, ParseU16Port_Zero) {
    std::uint16_t port = 1234;
    EXPECT_FALSE(parse_u16_port("--metrics-port", "0", &port));
    EXPECT_EQ(port, 1234); // Unchanged
}

TEST(ServerMainArgsTest, ParseU16Port_Overflow) {
    std::uint16_t port = 1234;
    EXPECT_FALSE(parse_u16_port("--metrics-port", "65536", &port));
    EXPECT_FALSE(parse_u16_port("--metrics-port", "99999", &port));
    EXPECT_EQ(port, 1234); // Unchanged
}

TEST(ServerMainArgsTest, ParseU16Port_NonNumeric) {
    std::uint16_t port = 1234;
    EXPECT_FALSE(parse_u16_port("--metrics-port", "abc", &port));
    EXPECT_EQ(port, 1234); // Unchanged
}

TEST(ServerMainArgsTest, ParseU16Port_TrailingChars) {
    std::uint16_t port = 1234;
    EXPECT_FALSE(parse_u16_port("--metrics-port", "8080abc", &port));
    EXPECT_EQ(port, 1234); // Unchanged
}

TEST(ServerMainArgsTest, ParseU16Port_Negative) {
    std::uint16_t port = 1234;
    EXPECT_FALSE(parse_u16_port("--metrics-port", "-1", &port));
    EXPECT_EQ(port, 1234); // Unchanged
}

TEST(ServerMainArgsTest, ParseU16Port_SignedAndWhitespace) {
    std::uint16_t port = 1234;
    EXPECT_FALSE(parse_u16_port("--metrics-port", "+8080", &port));
    EXPECT_FALSE(parse_u16_port("--metrics-port", " 8080", &port));
    EXPECT_FALSE(parse_u16_port("--metrics-port", "8080 ", &port));
    EXPECT_EQ(port, 1234);
}

// --- ParseEnvSize Tests ---

TEST(ServerMainArgsTest, ParseEnvSize_AbsentKeepsDefault) {
    EnvGuard guard("TEST_VAR");
    guard.clear();

    std::size_t out = 42;
    const char* err_name = nullptr;
    EXPECT_TRUE(parse_env_size("TEST_VAR", &out, &err_name));
    EXPECT_EQ(out, 42); // Default preserved
    EXPECT_EQ(err_name, nullptr);
}

TEST(ServerMainArgsTest, ParseEnvSize_ValidPositive) {
    EnvGuard guard("TEST_VAR");
    guard.set("64");

    std::size_t out = 42;
    const char* err_name = nullptr;
    EXPECT_TRUE(parse_env_size("TEST_VAR", &out, &err_name));
    EXPECT_EQ(out, 64);
    EXPECT_EQ(err_name, nullptr);
}

TEST(ServerMainArgsTest, ParseEnvSize_ZeroRejects) {
    EnvGuard guard("TEST_VAR");
    guard.set("0");

    std::size_t out = 42;
    const char* err_name = nullptr;
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));
    EXPECT_STREQ(err_name, "TEST_VAR");
    EXPECT_EQ(out, 42); // Unchanged
}

TEST(ServerMainArgsTest, ParseEnvSize_NonNumericRejects) {
    EnvGuard guard("TEST_VAR");
    guard.set("abc");

    std::size_t out = 42;
    const char* err_name = nullptr;
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));
    EXPECT_STREQ(err_name, "TEST_VAR");
    EXPECT_EQ(out, 42); // Unchanged
}

TEST(ServerMainArgsTest, ParseEnvSize_EdgeCases) {
    EnvGuard guard("TEST_VAR");
    std::size_t out = 42;
    const char* err_name = nullptr;

    // Negatives, signs, and space
    guard.set("-1");
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));
    EXPECT_STREQ(err_name, "TEST_VAR");

    guard.set("+1");
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));

    guard.set(" 1");
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));

    guard.set("1 ");
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));

    guard.set("1abc");
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));

    // Empty string counts as invalid present variable
    guard.set("");
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));

    // Overflow
    guard.set("999999999999999999999999999999999999999999999999");
    EXPECT_FALSE(parse_env_size("TEST_VAR", &out, &err_name));
}

// --- ParseEnvPositiveInt Tests ---

TEST(ServerMainArgsTest, ParseEnvPositiveInt_ValidPositive) {
    EnvGuard guard("TEST_VAR");
    guard.set("4");

    int out = 42;
    const char* err_name = nullptr;
    EXPECT_TRUE(parse_env_positive_int("TEST_VAR", &out, &err_name));
    EXPECT_EQ(out, 4);
    EXPECT_EQ(err_name, nullptr);
}

TEST(ServerMainArgsTest, ParseEnvPositiveInt_ZeroRejects) {
    EnvGuard guard("TEST_VAR");
    guard.set("0");

    int out = 42;
    const char* err_name = nullptr;
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));
    EXPECT_STREQ(err_name, "TEST_VAR");
    EXPECT_EQ(out, 42); // Unchanged
}

TEST(ServerMainArgsTest, ParseEnvPositiveInt_NegativeRejects) {
    EnvGuard guard("TEST_VAR");
    guard.set("-1");

    int out = 42;
    const char* err_name = nullptr;
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));
    EXPECT_STREQ(err_name, "TEST_VAR");
    EXPECT_EQ(out, 42); // Unchanged
}

TEST(ServerMainArgsTest, ParseEnvPositiveInt_EdgeCases) {
    EnvGuard guard("TEST_VAR");
    int out = 42;
    const char* err_name = nullptr;

    guard.set("+1");
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));

    guard.set(" 1");
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));

    guard.set("1 ");
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));

    guard.set("1abc");
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));

    guard.set("");
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));

    // Overflow int limit
    guard.set("3000000000"); // > INT_MAX (2147483647)
    EXPECT_FALSE(parse_env_positive_int("TEST_VAR", &out, &err_name));
}

// --- Hash Seed Validation Tests ---

TEST(ServerMainArgsTest, HashSeed_Validation) {
    // Valid 32-character hexadecimal seeds
    EXPECT_TRUE(bytetaper::hash::validate_hash_seed_hex("0123456789abcdef0123456789abcdef"));
    EXPECT_TRUE(bytetaper::hash::validate_hash_seed_hex("ABCDEF0123456789ABCDEF0123456789"));

    // Invalid lengths
    EXPECT_FALSE(bytetaper::hash::validate_hash_seed_hex(nullptr));
    EXPECT_FALSE(bytetaper::hash::validate_hash_seed_hex(""));
    EXPECT_FALSE(bytetaper::hash::validate_hash_seed_hex("0123456789abcdef"));
    EXPECT_FALSE(bytetaper::hash::validate_hash_seed_hex("0123456789abcdef0123456789abcdef0"));

    // Invalid characters
    EXPECT_FALSE(bytetaper::hash::validate_hash_seed_hex("0123456789abcdef0123456789abcdeg"));
    EXPECT_FALSE(bytetaper::hash::validate_hash_seed_hex("0123456789abcdef0123456789abcdef "));
    EXPECT_FALSE(bytetaper::hash::validate_hash_seed_hex(" 123456789abcdef0123456789abcdef"));
}

} // namespace bytetaper::extproc::startup
