// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_EXTPROC_STARTUP_PARSE_H
#define BYTETAPER_EXTPROC_STARTUP_PARSE_H

#include <charconv>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace bytetaper::extproc::startup {

// Helper to strictly parse an unsigned integer using std::from_chars
inline bool strict_parse_unsigned(const char* val, unsigned long long* out_val) {
    if (val == nullptr || val[0] == '\0') {
        return false;
    }
    // Reject leading whitespace or signs
    if (val[0] == '+' || val[0] == '-' || val[0] == ' ' || val[0] == '\t' || val[0] == '\r' ||
        val[0] == '\n') {
        return false;
    }
    const std::size_t len = std::strlen(val);
    // Reject trailing whitespace
    if (val[len - 1] == ' ' || val[len - 1] == '\t' || val[len - 1] == '\r' ||
        val[len - 1] == '\n') {
        return false;
    }

    unsigned long long parsed = 0;
    auto [ptr, ec] = std::from_chars(val, val + len, parsed);
    if (ec != std::errc{} || ptr != val + len) {
        return false;
    }
    *out_val = parsed;
    return true;
}

// Parses a positive std::size_t env var. Absent → keeps default (returns true).
// Present but invalid, empty, or zero → sets *err_name, returns false.
inline bool parse_env_size(const char* name, std::size_t* out, const char** err_name) {
    const char* val = std::getenv(name);
    if (val == nullptr) {
        return true;
    }
    unsigned long long parsed = 0;
    if (!strict_parse_unsigned(val, &parsed) || parsed == 0) {
        *err_name = name;
        return false;
    }
    *out = static_cast<std::size_t>(parsed);
    return true;
}

// Parses a positive int env var (for fields typed as int). Absent → keeps default.
// Present but invalid, empty, zero, or negative → sets *err_name, returns false.
inline bool parse_env_positive_int(const char* name, int* out, const char** err_name) {
    const char* val = std::getenv(name);
    if (val == nullptr) {
        return true;
    }
    unsigned long long parsed = 0;
    if (!strict_parse_unsigned(val, &parsed) || parsed == 0 ||
        parsed > static_cast<unsigned long long>(INT_MAX)) {
        *err_name = name;
        return false;
    }
    *out = static_cast<int>(parsed);
    return true;
}

// Parses a CLI --metrics-port value (uint16_t, range 1..65535).
// On failure, prints to stderr and returns false. On success, writes *out and returns true.
inline bool parse_u16_port(const char* arg_name, const char* val, std::uint16_t* out) {
    if (val == nullptr || val[0] == '\0') {
        std::fprintf(stderr, "error: %s requires a value\n", arg_name);
        return false;
    }
    unsigned long long parsed = 0;
    if (!strict_parse_unsigned(val, &parsed) || parsed == 0 || parsed > 65535) {
        std::fprintf(stderr, "error: %s must be an integer in range 1..65535, got: %s\n", arg_name,
                     val);
        return false;
    }
    *out = static_cast<std::uint16_t>(parsed);
    return true;
}

} // namespace bytetaper::extproc::startup

#endif // BYTETAPER_EXTPROC_STARTUP_PARSE_H
