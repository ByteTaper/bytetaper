# Third-Party Notices

ByteTaper incorporates, links against, generates code from, or uses during
build/test the third-party software listed below.

This file is provided for attribution and license-notice purposes. It is not
legal advice. If ByteTaper is distributed as a source archive, binary, package,
or container image, the final distribution should include all applicable full
license texts and any notices required by the exact dependency versions included
in that distribution.

## Summary

| Component | Use in ByteTaper | License |
|---|---|---|
| RocksDB | L2 persistent cache backend | Apache-2.0 or GPL-2.0; ByteTaper uses the Apache-2.0 option |
| Envoy API / data-plane-api proto definitions | Envoy `ext_proc` protobuf service definitions | Apache-2.0 |
| gRPC | Envoy ExternalProcessor gRPC service implementation | Apache-2.0 |
| Protocol Buffers | Protobuf compiler/runtime for generated Envoy API bindings | BSD-3-Clause |
| yaml-cpp | YAML policy loading and validation | MIT |
| GoogleTest | Unit/integration test framework | BSD-3-Clause |

## RocksDB

Homepage: https://rocksdb.org/  
Repository: https://github.com/facebook/rocksdb  
License: Apache-2.0 or GPL-2.0
License text (copied): [LICENSES/Apache-2.0.txt]([LICENSES/Apache-2.0.txt])

ByteTaper uses RocksDB under the Apache License, Version 2.0 option.

Notice:

```text
RocksDB
Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
Licensed under the Apache License, Version 2.0, at ByteTaper's option.
```

RocksDB also contains code derived from LevelDB. If RocksDB source code or
source-derived files are redistributed with ByteTaper, preserve the original
upstream headers and notices.

## Envoy API / data-plane-api proto definitions

Homepage: [https://www.envoyproxy.io/](https://www.envoyproxy.io/)
Repository: [https://github.com/envoyproxy/data-plane-api](https://github.com/envoyproxy/data-plane-api)
License: Apache-2.0
License text (copied): [LICENSES/Apache-2.0.txt]([LICENSES/Apache-2.0.txt])

ByteTaper uses Envoy API protobuf definitions, including the External Processor
API, to generate C++ protobuf/gRPC bindings for Envoy `ext_proc` integration.

Notice:

```text
Envoy API / data-plane-api
Copyright Envoy Project Authors.
Licensed under the Apache License, Version 2.0.
```

If Envoy `.proto` files are vendored in this repository, their original
copyright and license headers must be preserved.

## gRPC

Homepage: [https://grpc.io/](https://grpc.io/)
Repository: [https://github.com/grpc/grpc](https://github.com/grpc/grpc)
License: Apache-2.0
License text (copied): [LICENSES/Apache-2.0.txt]([LICENSES/Apache-2.0.txt])

ByteTaper links against gRPC C++ libraries for the Envoy ExternalProcessor
service implementation.

Notice:

```text
gRPC
Copyright 2015 gRPC authors.
Licensed under the Apache License, Version 2.0.
```

## Protocol Buffers

Homepage: [https://protobuf.dev/](https://protobuf.dev/)
Repository: [https://github.com/protocolbuffers/protobuf](https://github.com/protocolbuffers/protobuf)
License: BSD-3-Clause
License text (copied): [LICENSES/BSD-3-Clause-Protocol-Buffers.txt]([LICENSES/BSD-3-Clause-Protocol-Buffers.txt])

ByteTaper uses Protocol Buffers for generated C++ bindings from Envoy API
`.proto` definitions.

Notice:

```text
Protocol Buffers
Copyright 2008 Google LLC.
Licensed under the BSD 3-Clause License.
```

## Quill

Repository: https://github.com/odygrd/quill
License: MIT License
License text (copied): [LICENSES/MIT-quill.txt]([LICENSES/MIT-quill.txt])

## yaml-cpp

Repository: [https://github.com/jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp)
License: MIT
License text (copied): [LICENSES/MIT-yaml-cpp.txt]([LICENSES/MIT-yaml-cpp.txt])

ByteTaper uses yaml-cpp for YAML route policy loading and validation.

Notice:

```text
yaml-cpp
Copyright (c) 2008-2015 Jesse Beder.
Licensed under the MIT License.
```

## GoogleTest

Repository: [https://github.com/google/googletest](https://github.com/google/googletest)
License: BSD-3-Clause

ByteTaper uses GoogleTest for unit and integration tests. GoogleTest is a
build/test dependency and is not required by the ByteTaper runtime unless test
binaries are distributed.

Notice:

```text
GoogleTest
Copyright 2008 Google Inc.
Licensed under the BSD 3-Clause License.
```

## Transitive and system dependencies

ByteTaper may also link against transitive or system libraries depending on the
build environment, package manager, and final distribution format.

These may include, but are not limited to:

* C and C++ runtime libraries
* pthread / system threading libraries
* zlib
* bzip2
* lz4
* snappy
* zstd
* OpenSSL or another TLS/crypto provider
* Abseil
* c-ares
* RE2
* other gRPC, Protobuf, RocksDB, or distribution-level dependencies

The exact transitive dependency set can differ between local builds, CI builds,
Linux distribution packages, and container images.

Before releasing a binary or container image, inspect the final artifact and
include any additional required notices.

## Vendored source files

If ByteTaper vendors any third-party source files, generated files, `.proto`
definitions, headers, or patches, the original copyright notices and license
headers must be preserved.

Do not replace upstream license headers with ByteTaper headers.

If a third-party file is modified, add a clear modification notice where
appropriate, while preserving the original upstream notice.

## Distribution note

This notice file is intended to describe ByteTaper's direct third-party
dependencies. It does not replace a full dependency audit of the final release
artifact.

For every source, binary, package, or container distribution, ensure that:

1. The applicable full license texts are included.
2. Required copyright notices are preserved.
3. Required NOTICE or attribution files from upstream projects are retained.
4. The final dependency inventory or SBOM matches the actual released artifact.

