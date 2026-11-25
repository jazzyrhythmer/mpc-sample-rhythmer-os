#!/bin/sh
# Build Rhythmer for the MPC Sample (linux/arm64) in its build image (the base's
# plus tools/plugin_support/Dockerfile),
# and the stem separator beside it (make stems, tools/stems/build.py),
# then run the tests, the sound self-test, the monkey test (random hands on every
# control, src/rhythmer/monkey.cpp) and the page previews.
set -eu
cd "$(dirname "$0")"
[ -f base/base.mk ] && [ -f core/core.mk ] || git submodule update --init
python3 tools/dependencies.py
# The version the Credits page shows: ./release.sh names it, else git describe.
RHYTHMER_VERSION=$(printf %s "${RHYTHMER_VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}" | tr -cd 'A-Za-z0-9._+-')
export RHYTHMER_VERSION
docker build --platform linux/arm64 -t sample-base-builder base/tools
# Rhythmer's own image on top: the plug-in support pack's cross compiler and GUI libraries.
docker build --platform linux/arm64 -t rhythmer-builder tools/plugin_support
docker run --rm --platform linux/arm64 -v "$PWD:/work" -e RHYTHMER_VERSION rhythmer-builder \
    sh -c 'make -j4 && make stems && python3 -m unittest discover -s tests -v && python3 -m unittest discover -s core/tests -v && build/rhythmer --self-test build/test.wav && build/rhythmer --rhythmer-monkey && mkdir -p build/rhythmer-preview && build/rhythmer --rhythmer-preview build/rhythmer-preview'
