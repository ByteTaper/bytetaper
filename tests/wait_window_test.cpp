// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/wait_window.h"

#include <gtest/gtest.h>

namespace bytetaper::coalescing {

TEST(WaitWindowTest, ValidateZero) {
    EXPECT_EQ(validate_wait_window(0), WaitWindowValidationResult::InvalidZero);
}

TEST(WaitWindowTest, ValidateTooLong) {
    EXPECT_EQ(validate_wait_window(30001), WaitWindowValidationResult::InvalidTooLong);
    EXPECT_EQ(validate_wait_window(kMaxWaitWindowMs + 1),
              WaitWindowValidationResult::InvalidTooLong);
}

TEST(WaitWindowTest, ValidateValid) {
    EXPECT_EQ(validate_wait_window(1), WaitWindowValidationResult::Valid);
    EXPECT_EQ(validate_wait_window(50), WaitWindowValidationResult::Valid);
    EXPECT_EQ(validate_wait_window(100), WaitWindowValidationResult::Valid);
}

TEST(WaitWindowTest, CheckExpiration) {
    std::uint64_t start = 1000;
    std::uint32_t window = 50;

    // Not expired yet
    EXPECT_FALSE(has_wait_window_expired(start, 1000, window));
    EXPECT_FALSE(has_wait_window_expired(start, 1049, window));

    // Expired
    EXPECT_TRUE(has_wait_window_expired(start, 1050, window));
    EXPECT_TRUE(has_wait_window_expired(start, 1100, window));
}

TEST(WaitWindowTest, FallbackReasonString) {
    EXPECT_EQ(get_wait_fallback_reason_string(WaitFallbackReason::Timeout), "timeout");
}

} // namespace bytetaper::coalescing
