# Build RHYTHMER OS 0.3.0

The matching native application source is in `runtime/`, with its base and core sources included. Use the runtime Makefile and scripts; the root legacy build helpers describe older exports.

On Linux ARM64, install the build packages listed by `runtime/base/tools/Dockerfile`, place the matching dependencies from `third_party/runtime/` under `runtime/build/deps/`, and run `make -C runtime RHYTHMER_VERSION=v0.3.0 build/rhythmer`. Font assets and the generated support pack must be prepared using `runtime/src/rhythmer/dependencies.py` and the runtime Makefile rules. See `runtime/README.md` and `runtime/base/README.md` for the build environment and boot design.

Each `release-ports/` directory preserves the source and build instructions supplied with the corresponding library, including required upstream notices. The support-source archive accompanies the binary release for the existing ARM support pack.

A distribution signature requires a private signing key; it is not part of the source archive. Own builds must use their own key pair and matching public update key.
