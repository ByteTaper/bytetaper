// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_COALESCING_WAIT_WINDOW_H
#define BYTETAPER_COALESCING_WAIT_WINDOW_H

#include <cstdint>
#include <string_view>

namespace bytetaper::coalescing {

// Hard cap on wait window to prevent long blocking waits in ExtProc.
static constexpr std::uint32_t kMaxWaitWindowMs = 30000;

/**
 * @brief Result of wait window validation.
 */
enum class WaitWindowValidationResult : std::uint8_t {
    Valid = 0,
    InvalidZero = 1,
    InvalidTooLong = 2,
};

/**
 * @brief Reasons for falling back from a wait state.
 */
enum class WaitFallbackReason : std::uint8_t {
    Timeout = 0,
};

/**
 * @brief Validates that the wait window is strictly > 0 and <= kMaxWaitWindowMs.
 *
 * @param wait_window_ms The window duration to validate.
 * @return WaitWindowValidationResult The validation status.
 */
WaitWindowValidationResult validate_wait_window(std::uint32_t wait_window_ms);

/**
 * @brief Returns a stable string for the fallback reason.
 *
 * @param reason The fallback reason.
 * @return std::string_view The string representation.
 */
std::string_view get_wait_fallback_reason_string(WaitFallbackReason reason);

/**
 * @brief Evaluates if the follower has waited past the allowed window.
 *
 * @param start_epoch_ms The epoch time when the wait started.
 * @param now_epoch_ms The current epoch time.
 * @param wait_window_ms The allowed wait window duration.
 * @return true if the window has expired, false otherwise.
 */
bool has_wait_window_expired(std::uint64_t start_epoch_ms, std::uint64_t now_epoch_ms,
                             std::uint32_t wait_window_ms);

} // namespace bytetaper::coalescing

#endif // BYTETAPER_COALESCING_WAIT_WINDOW_H
