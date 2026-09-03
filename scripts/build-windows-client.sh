#!/bin/bash
# build-windows-client.sh — Cross-compile the Zaviro client for Windows using Docker + mingw-w64
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
DOCKER_DIR="$REPO_ROOT/docker/cross-build"
BUILD_DIR="$REPO_ROOT/client/build-win64"

echo "=== Building Docker image for cross-compilation ==="
docker build -t zaviro-cross-build "$DOCKER_DIR"

echo "=== Cross-compiling client for Windows x64 ==="
mkdir -p "$BUILD_DIR"

docker run --rm \
    -v "$REPO_ROOT:/src:ro" \
    -v "$BUILD_DIR:/out" \
    zaviro-cross-build \
    bash -c '
        set -e
        mkdir -p /build/client
        cp -r /src/client/* /build/client/
        cp -r /src/client/third_party /build/client/

        # Download glm + nlohmann_json headers for cross-compile
        cd /tmp
        wget -q https://github.com/g-truc/glm/releases/download/1.0.1/glm-1.0.1.zip
        unzip -q glm-1.0.1.zip
        mkdir -p /tmp/glm-install
        cp -r /tmp/glm-1.0.1/glm /tmp/glm-install/

        wget -q https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        tar xf json.tar.xz
        cp -r /tmp/json-3.11.3/include/nlohmann /tmp/glm-install/

        cd /build/client
        cmake -S . -B build-win64 \
            -DCMAKE_TOOLCHAIN_FILE=/src/client/cmake/mingw-toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DGLM_INCLUDE_DIR=/tmp/glm-install \
            -Dnlohmann_json_DIR=/tmp/json-3.11.3

        cmake --build build-win64 -j$(nproc)

        # Copy result
        cp build-win64/zaviro_client.exe /out/ 2>/dev/null || \
        cp build-win64/*.exe /out/ 2>/dev/null || \
        echo "WARNING: no .exe found, listing build dir:" && ls -la build-win64/
    '

echo "=== Done! Windows binary at: $BUILD_DIR/zaviro_client.exe ==="
ls -la "$BUILD_DIR/"*.exe 2>/dev/null || echo "(check build output above)"
