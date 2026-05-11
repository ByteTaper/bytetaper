#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

"""
Scans src/stages/*.cpp for forbidden synchronous I/O patterns.

Allowlist:
  - l2_cache_lookup_stage.cpp, l2_cache_store_stage.cpp: skipped (they implement the forbidden ops)
  - coalescing_follower_wait_stage.cpp: allowed to call l2_cache_lookup_stage( only
    (documented exception in docs/runtime/RUNTIME_BOUNDARIES.md)

To add a new exception: update PATTERN_EXCEPTIONS and update RUNTIME_BOUNDARIES.md.
"""

import os, re, sys

STAGES_DIR = os.path.join(os.path.dirname(__file__), "..", "src", "stages")
BOUNDARIES_DOC = os.path.join(os.path.dirname(__file__), "..", "docs", "runtime", "RUNTIME_BOUNDARIES.md")

FORBIDDEN_PATTERNS = [
    ("l2_cache_lookup_stage", r"\bl2_cache_lookup_stage\s*\("),
    ("l2_cache_store_stage", r"\bl2_cache_store_stage\s*\("),
    ("l2_get", r"\bl2_get\s*\("),
    ("l2_get_result", r"\bl2_get_result\s*\("),
    ("l2_put", r"\bl2_put\s*\("),
    ("rocksdb::", r"\brocksdb::"),
]

FULL_SKIP = {"l2_cache_lookup_stage.cpp", "l2_cache_store_stage.cpp"}

PATTERN_EXCEPTIONS = {
    "coalescing_follower_wait_stage.cpp": {"l2_cache_lookup_stage"},
}

REQUIRED_DOC_PHRASES = [
    "Coalescing Follower Sync L2 Probe",
]

def scan_file(path, filename):
    violations = []
    exceptions = PATTERN_EXCEPTIONS.get(filename, set())
    in_block_comment = False
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            stripped = line.strip()
            # Track block comments (simple heuristic)
            if "/*" in stripped:
                in_block_comment = True
            if "*/" in stripped:
                in_block_comment = False
                continue
            if in_block_comment or stripped.startswith("//"):
                continue
            for key, pattern in FORBIDDEN_PATTERNS:
                if re.search(pattern, line) and key not in exceptions:
                    violations.append((lineno, key, line.rstrip()))
    return violations

def check_doc():
    errors = []
    with open(BOUNDARIES_DOC) as f:
        content = f.read()
    for phrase in REQUIRED_DOC_PHRASES:
        if phrase not in content:
            errors.append(f"RUNTIME_BOUNDARIES.md missing required phrase: '{phrase}'")
    return errors

def main():
    errors = []
    for fname in sorted(os.listdir(STAGES_DIR)):
        if not fname.endswith(".cpp"):
            continue
        if fname in FULL_SKIP:
            continue
        path = os.path.join(STAGES_DIR, fname)
        violations = scan_file(path, fname)
        for lineno, pattern, text in violations:
            errors.append(f"  {fname}:{lineno}: forbidden pattern '{pattern}'\n    {text}")

    doc_errors = check_doc()
    errors.extend(doc_errors)

    if errors:
        print("FAIL: runtime boundary violations found:")
        for e in errors:
            print(e)
        sys.exit(1)
    else:
        print(f"OK: {len(list(os.listdir(STAGES_DIR)))} stage files scanned, 0 violations.")
        sys.exit(0)

if __name__ == "__main__":
    main()
