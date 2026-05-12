# ByteTaper — Testing Guide

## Sanitizer coverage

| Sanitizer | What it catches | Test scope | CI cadence |
|-----------|----------------|------------|------------|
| ASAN | Memory errors, use-after-free, bounds | All tests | Every PR + main push |
| UBSAN | Undefined behavior, integer overflow | All tests | Every PR + main push |
| TSAN | Data races, lock-order violations | Concurrency tests only | Push to main + workflow_dispatch |

## Local commands

```bash
# ASAN — memory safety (all tests)
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose run --rm bytetaper-asan-test

# UBSAN — undefined behavior (all tests)
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose run --rm bytetaper-ubsan-test

# TSAN — concurrency (concurrency-tagged tests only)
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose run --rm bytetaper-tsan-test
```

## Notes

- TSAN and ASAN are mutually exclusive (separate build directories: `build-tsan`, `build-asan`, `build-ubsan`).
- UBSAN can be combined with ASAN at the CMake level but runs as a separate CI job for isolation.
- Manual dispatch (`workflow_dispatch`) always runs all three sanitizers.
