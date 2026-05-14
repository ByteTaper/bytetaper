#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

BASE_REF="${1:-HEAD~1}"
HEAD_REF="${2:-HEAD}"
MODE="${BYTETAPER_NOTICE_FRESHNESS_MODE:-warn}"

echo "Checking notice freshness between $BASE_REF and $HEAD_REF..."

# Get list of changed files
if ! CHANGED_FILES=$(git diff --name-only "$BASE_REF" "$HEAD_REF" 2>/dev/null); then
    echo "ERROR: Could not compare base ref '$BASE_REF' and head ref '$HEAD_REF'. Ensure full git history is fetched."
    exit 1
fi

if [ -z "$CHANGED_FILES" ]; then
    echo "No files changed."
    exit 0
fi

DEP_SENSITIVE=0
NOTICE_UPDATED=0

for file in $CHANGED_FILES; do
    # Check if notice or license updated
    if [[ "$file" == "THIRD_PARTY_NOTICES.md" || "$file" == LICENSES/* ]]; then
        NOTICE_UPDATED=1
    fi

    # Check if dependency sensitive files updated
    if [[ "$file" == "docker/production.Dockerfile" || \
          "$file" == "docker/dev.Dockerfile" || \
          "$file" == "CMakeLists.txt" || \
          "$file" == cmake/* || \
          "$file" == proto/* ]]; then
        DEP_SENSITIVE=1
    fi
done

if [ $DEP_SENSITIVE -eq 1 ] && [ $NOTICE_UPDATED -eq 0 ]; then
    MSG="Dependency-sensitive configuration files were modified, but THIRD_PARTY_NOTICES.md and LICENSES/ were not updated."
    echo "::warning::$MSG"
    echo "WARN: $MSG"
    if [ "$MODE" = "fail" ]; then
        exit 1
    fi
else
    echo "SUCCESS: Legal notices and license attributions are fresh."
fi
