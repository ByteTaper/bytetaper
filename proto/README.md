# Vendored Proto Attribution

ByteTaper vendors a minimal proto snapshot used to generate C++ bindings for
Envoy External Processor integration.

## Envoy API, xDS, and UDPA protos

Files under these paths are treated as vendored third-party interface
definitions:

- `proto/envoy/**`
- `proto/xds/**`
- `proto/udpa/**`

These files originate from the Envoy API / data-plane-api ecosystem and related
xDS/UDPA proto definitions, which are licensed under the Apache License,
Version 2.0.

The Apache-2.0 license text is included at:

```text
LICENSES/Apache-2.0.txt
```

When refreshing these proto files, preserve upstream copyright, license,
package, option, and notice metadata. Do not replace upstream license headers
with ByteTaper headers.

## Local compatibility subset

`proto/validate/validate.proto` is a ByteTaper-maintained compatibility subset
for the protoc validation options required by the vendored Envoy API protos. It
is not intended to replace upstream validation projects.

If this compatibility subset is replaced with upstream validation sources, add
the upstream project and license to `THIRD_PARTY_NOTICES.md` in the same change.
