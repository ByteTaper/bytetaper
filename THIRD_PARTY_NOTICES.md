# Third-Party Notices

This file records third-party software, interface definitions, container images,
build tools, and test tools that ByteTaper may use during development,
testing, or distribution.

ByteTaper's own source code is licensed separately under:

```text
AGPL-3.0-only OR LicenseRef-Commercial
```

Third-party components remain under their own licenses. A ByteTaper commercial
license does not relicense third-party components.

## Runtime and linked components

| Component | Use in ByteTaper | License | License text in this repo |
| --- | --- | --- | --- |
| RocksDB | L2 persistent cache backend | Apache-2.0 or GPL-2.0; ByteTaper selects the Apache-2.0 option for commercial-compatible builds | `LICENSES/Apache-2.0.txt` |
| Quill | C++ logging library, fetched by CMake at `v11.1.0` | MIT | `LICENSES/MIT-quill.txt` |
| yaml-cpp | YAML policy loading and validation | MIT | `LICENSES/MIT-yaml-cpp.txt` |
| gRPC C++ | Envoy ExternalProcessor gRPC service implementation | Apache-2.0 | `LICENSES/Apache-2.0.txt` |
| Protocol Buffers | Protobuf compiler/runtime for generated Envoy API bindings | BSD-3-Clause | `LICENSES/BSD-3-Clause-Protocol-Buffers.txt` |
| Envoy Proxy image | Local examples, integration tests, and demo proxy container | Apache-2.0 | `LICENSES/Apache-2.0.txt` |

## Vendored or compatibility interface definitions

| Component | Path | License | Notes |
| --- | --- | --- | --- |
| Envoy API proto snapshot | `proto/envoy/**` | Apache-2.0 | Vendored for ExtProc code generation. Preserve upstream Envoy API copyright, license, and notice metadata when refreshing these files. |
| xDS/UDPA proto snapshot | `proto/xds/**`, `proto/udpa/**` | Apache-2.0 | Used by Envoy API protos. Preserve upstream copyright, license, and notice metadata when refreshing these files. |
| Protoc validation compatibility subset | `proto/validate/validate.proto` | AGPL-3.0-only OR LicenseRef-Commercial | ByteTaper-maintained compatibility subset for the protoc options required by vendored Envoy API protos. If replaced with upstream PGV sources, record the upstream license here. |

## Build, development, and test tools

| Component | Use in ByteTaper | License | Notes |
| --- | --- | --- | --- |
| GoogleTest | Unit and integration tests, fetched by CMake when tests are enabled | BSD-3-Clause | Test dependency only unless test binaries are distributed. |
| Ubuntu base image | Development/build image | Mixed Ubuntu package licenses | Commercial distribution should include an SBOM or image-layer license report. |
| Alpine base image | Benchmark image | Mixed Alpine package licenses | Commercial distribution should include an SBOM or image-layer license report. |
| ccache, CMake, Ninja, clang-format, pkg-config, make, curl, bash, Python, jq, wrk | Build/test/dev tooling from container packages | Mixed licenses | Development/test dependencies unless included in a distributed image. |
| Compression/system libraries: zlib, bzip2, lz4, snappy, zstd, gflags | RocksDB build/runtime dependencies in the development image | Mixed permissive licenses | Preserve package license notices if distributed in a ByteTaper image or appliance. |

## Release packaging expectations

For a commercial ByteTaper release, include at minimum:

1. `LICENSE`
2. `LICENSES/AGPL-3.0-only.txt`
3. `LICENSES/LicenseRef-Commercial.txt`
4. `THIRD_PARTY_NOTICES.md`
5. full license texts and notices for bundled third-party components
6. an SBOM or container image license report for distributed container images

Commercial customers should receive a separate written commercial license
agreement. Possessing this repository, this notice file, or a release artifact
does not grant commercial license rights to ByteTaper itself.

## Refresh rule

When dependencies, container images, vendored `.proto` files, or generated-code
inputs change, update this file in the same pull request.
