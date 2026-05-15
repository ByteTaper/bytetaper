# Production Runtime Image

The ByteTaper production runtime container image is engineered to be highly secure, reproducible, traceable, and compliant with Open Container Initiative (OCI) image specifications.

## Architectural Standards

- **Pinned RocksDB foundation**: RocksDB is sourced from the pinned Rockspack image `haluan/rockspack:11.1.1-ubuntu26.04-6cdeb9d`; ByteTaper no longer clones or compiles RocksDB inside its own Dockerfiles.
- **Minimal Attack Surface**: The final runtime image keeps development tooling (`cmake`, `ninja`, compilers, and debuggers) out of the production execution environment.
- **Least Privilege**: All processes execute under the dedicated, non-root `bytetaper` user (`UID 1001`, `GID 1001`).
- **Build Provenance**: Embedded OCI image labels and `/opt/bytetaper/build-info.json` provide explicit git revision, semantic version, and build timestamp tracing.

---

## Runtime Filesystem Contract

| Path | Mode | Purpose |
|---|---|---|
| `/etc/bytetaper/policy.yaml` | Read-Only (Mount) | Main declarative routing and coalescing policy configuration |
| `/var/lib/bytetaper/l2-cache` | Read-Write | High-performance RocksDB shared L2 disk cache directory |
| `/var/run/bytetaper` | Read-Write | Sockets, PID files, and inter-process communication state |
| `/opt/bytetaper/LICENSES` | Read-Only | Comprehensive open-source software license agreements |
| `/opt/bytetaper/THIRD_PARTY_NOTICES.md` | Read-Only | Software attribution and third-party notices |
| `/opt/bytetaper/build-info.json` | Read-Only | Static JSON build provenance metadata |

---

## Container Build Parameters

The `docker/production.Dockerfile` supports parameterized builds via OCI standard build arguments:

```bash
docker build \
  -f docker/production.Dockerfile \
  --build-arg ROCKSPACK_IMAGE="haluan/rockspack:11.1.1-ubuntu26.04-6cdeb9d" \
  --build-arg BYTETAPER_VERSION="1.2.0" \
  --build-arg BYTETAPER_GIT_SHA="a1b2c3d4e5f6..." \
  --build-arg BYTETAPER_BUILD_DATE="2026-05-14T00:00:00Z" \
  -t bytetaper-runtime:production \
  .
```

### Rockspack Integration Contract

The ByteTaper production Dockerfile expects the selected Rockspack image to provide:

- `/usr/local/include/rocksdb/db.h`
- `librocksdb.so` discoverable by `ldconfig`
- RocksDB built from the pinned `11.1.1` family used by ByteTaper's L2 cache implementation

The Docker build fails early if the Rockspack-provided RocksDB header or shared library is not available.

### OCI Image Specification Labels
Inspect embedded OCI metadata using Docker CLI:

```bash
docker image inspect bytetaper-runtime:production --format '{{json .Config.Labels}}' | jq .
```
```json
{
  "org.opencontainers.image.created": "2026-05-14T00:00:00Z",
  "org.opencontainers.image.description": "ByteTaper API Performance Gateway runtime image",
  "org.opencontainers.image.licenses": "AGPL-3.0-only OR LicenseRef-Commercial",
  "org.opencontainers.image.revision": "a1b2c3d4e5f6...",
  "org.opencontainers.image.source": "https://github.com/haluan/bytetaper",
  "org.opencontainers.image.title": "ByteTaper Runtime",
  "org.opencontainers.image.version": "1.2.0"
}
```

---

## Production Execution

### Version Inspection
Verify the binary version and build flags directly:

```bash
docker run --rm bytetaper-runtime:production --version
```
```text
ByteTaper extproc server
version:    1.2.0
git_sha:    a1b2c3d4e5f6...
build_date: 2026-05-14T00:00:00Z
build_type: Release
license:    AGPL-3.0-only OR LicenseRef-Commercial
```

### Launch Command
Execute the production external processor server with explicit volume mounts and network bindings:

```bash
docker run -d --name bytetaper-extproc \
  --user bytetaper \
  --read-only \
  --cap-drop=ALL \
  -p 18080:18080 -p 18081:18081 \
  -v /path/to/production-policy.yaml:/etc/bytetaper/policy.yaml:ro \
  -v bytetaper-l2-cache:/var/lib/bytetaper/l2-cache \
  --tmpfs /var/run/bytetaper \
  bytetaper-runtime:production \
  --listen-address 0.0.0.0:18080 \
  --policy-file /etc/bytetaper/policy.yaml \
  --l2-cache-path /var/lib/bytetaper/l2-cache \
  --metrics-address 0.0.0.0 \
  --metrics-port 18081
```
