#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

echo "==> Generating checksums for release artifacts..."

mkdir -p dist/release

echo "--> Copying license artifacts..."
cp THIRD_PARTY_NOTICES.md dist/release/
zip -qr dist/release/LICENSES.zip LICENSES/

echo "--> Calculating SHA256 checksums..."
cd dist/release
# Remove any old checksums file if present so it doesn't checksum itself
rm -f bytetaper-checksums.txt

sha256sum * > bytetaper-checksums.txt

if [[ ! -s bytetaper-checksums.txt ]]; then
  echo "ERROR: Generated checksums file is empty."
  exit 1
fi

echo "PASS: Checksums generated successfully at dist/release/bytetaper-checksums.txt:"
cat bytetaper-checksums.txt
