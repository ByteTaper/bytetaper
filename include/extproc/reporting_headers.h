// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

namespace bytetaper::extproc {

constexpr const char* kCachedResponseHeader = "x-bytetaper-cached-response";
constexpr const char* kCacheLayerHeader = "x-bytetaper-cache-layer";

constexpr const char* kResponseBodyHeader = "x-bytetaper-extproc-response-body";
constexpr const char* kWasteRemovedFieldsHeader = "x-bytetaper-waste-removed-fields";
constexpr const char* kWasteSavedBytesHeader = "x-bytetaper-waste-saved-bytes";
constexpr const char* kOriginalResponseBytesHeader = "x-bytetaper-original-response-bytes";
constexpr const char* kOptimizedResponseBytesHeader = "x-bytetaper-optimized-response-bytes";
constexpr const char* kTransformAppliedHeader = "x-bytetaper-transform-applied";

constexpr const char* kTrueValue = "true";
constexpr const char* kFalseValue = "false";
constexpr const char* kNoneValue = "none";

} // namespace bytetaper::extproc
