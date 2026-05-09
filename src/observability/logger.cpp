// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "observability/logger.h"

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/sinks/ConsoleSink.h"

namespace bytetaper::observability {

namespace {
quill::Logger* g_logger = nullptr;
bool g_enabled = false;
} // namespace

bool logger_init(const LoggerConfig& config) {
    g_enabled = config.enabled;
    if (!g_enabled) {
        return true;
    }
    quill::BackendOptions opts{};
    quill::Backend::start(opts);
    auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
    g_logger = quill::Frontend::create_or_get_logger("bytetaper", std::move(sink));
    return g_logger != nullptr;
}

void logger_shutdown() {
    if (g_enabled) {
        quill::Backend::stop();
    }
}

bool log_enabled(LogLevel /*level*/) {
    return g_enabled && g_logger != nullptr;
}

void log_trace(const char* message) {
    if (g_logger) {
        QUILL_LOG_TRACE_L1(g_logger, "{}", message);
    }
}
void log_debug(const char* message) {
    if (g_logger) {
        QUILL_LOG_DEBUG(g_logger, "{}", message);
    }
}
void log_info(const char* message) {
    if (g_logger) {
        QUILL_LOG_INFO(g_logger, "{}", message);
    }
}
void log_warn(const char* message) {
    if (g_logger) {
        QUILL_LOG_WARNING(g_logger, "{}", message);
    }
}
void log_error(const char* message) {
    if (g_logger) {
        QUILL_LOG_ERROR(g_logger, "{}", message);
    }
}

} // namespace bytetaper::observability
