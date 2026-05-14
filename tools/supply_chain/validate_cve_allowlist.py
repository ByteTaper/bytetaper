#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

import sys
import os
import re
import datetime

def parse_allowlist(content):
    try:
        import yaml
        data = yaml.safe_load(content)
        return data.get('allowlisted', []) if data else []
    except ImportError:
        entries = []
        current = {}
        for line in content.splitlines():
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if line == 'allowlisted: []':
                return []
            if line == 'allowlisted:':
                continue
            if line.startswith('- id:'):
                if current:
                    entries.append(current)
                current = {'id': line.split(':', 1)[1].strip().strip('"\'')}
            elif ':' in line and current is not None:
                k, v = line.split(':', 1)
                k = k.strip()
                if k in ['package', 'reason', 'expires', 'owner']:
                    current[k] = v.strip().strip('"\'')
        if current:
            entries.append(current)
        return entries

def main():
    if len(sys.argv) < 3:
        print("Usage: validate_cve_allowlist.py <allowlist.yaml> <output.trivyignore>")
        sys.exit(1)

    allowlist_path = sys.argv[1]
    ignore_path = sys.argv[2]

    if not os.path.exists(allowlist_path):
        print(f"ERROR: Allowlist file not found: {allowlist_path}")
        sys.exit(1)

    with open(allowlist_path, 'r') as f:
        content = f.read()

    entries = parse_allowlist(content)
    if not isinstance(entries, list):
        print(f"ERROR: Malformed allowlist structure in {allowlist_path}")
        sys.exit(1)

    cve_regex = re.compile(r'^CVE-\d{4}-\d{4,}$')
    seen_cves = set()
    valid_cves = []
    today = datetime.datetime.utcnow().date()

    required_fields = ['id', 'package', 'reason', 'expires', 'owner']

    for entry in entries:
        if not isinstance(entry, dict):
            print("ERROR: Allowlist entry is not a dictionary.")
            sys.exit(1)

        # Check required fields
        for field in required_fields:
            if field not in entry or not entry[field]:
                print(f"ERROR: Allowlist entry missing required field '{field}': {entry}")
                sys.exit(1)

        cve_id = entry['id']
        if not cve_regex.match(cve_id):
            print(f"ERROR: Malformed CVE ID '{cve_id}' in allowlist.")
            sys.exit(1)

        if cve_id in seen_cves:
            print(f"ERROR: Duplicate CVE ID '{cve_id}' in allowlist.")
            sys.exit(1)
        seen_cves.add(cve_id)

        # Check expiry date format and expiration
        expires_str = entry['expires']
        try:
            exp_date = datetime.datetime.strptime(expires_str, '%Y-%m-%d').date()
        except ValueError:
            print(f"ERROR: Invalid expiration date format '{expires_str}' for '{cve_id}'. Must be YYYY-MM-DD.")
            sys.exit(1)

        if exp_date < today:
            print(f"ERROR: Allowlist entry for '{cve_id}' expired on {expires_str}. Please remediate or review.")
            sys.exit(1)

        valid_cves.append(cve_id)

    # Write output trivyignore
    with open(ignore_path, 'w') as f:
        for cve in valid_cves:
            f.write(f"{cve}\n")

    print(f"SUCCESS: Validated {len(valid_cves)} active CVE allowlist entries.")

if __name__ == '__main__':
    main()
