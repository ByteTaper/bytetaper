// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include <cstdint>

namespace bytetaper::observability {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warning, Error };

struct LoggerConfig {
    bool enabled = false;
    LogLevel level = LogLevel::Info;
    const char* sink = "stderr";
};

bool logger_init(const LoggerConfig& config);
void logger_shutdown();

bool log_enabled(LogLevel level);

void log_trace(const char* message);
void log_debug(const char* message);
void log_info(const char* message);
void log_warn(const char* message);
void log_error(const char* message);

} // namespace bytetaper::observability
