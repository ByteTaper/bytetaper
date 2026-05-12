// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include <cstdint>

namespace bytetaper::taperquery {

struct TqSourcePosition {
    std::uint32_t offset = 0; // 0-based byte offset
    std::uint32_t line = 1;   // 1-based line number
    std::uint32_t column = 1; // 1-based column number (byte-oriented)
};

struct TqSourceSpan {
    TqSourcePosition start{};
    TqSourcePosition end{};
};

} // namespace bytetaper::taperquery
