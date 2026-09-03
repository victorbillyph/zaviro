FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    wget \
    unzip \
    git \
    mingw-w64 \
    && rm -rf /var/lib/apt/lists/*

# Cross-compile GLFW for Windows (mingw-w64)
RUN cd /tmp && \
    wget -q https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip && \
    unzip -q glfw-3.4.zip && \
    cd glfw-3.4 && \
    cmake -S . -B build-win64 \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
        -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
        -DGLFW_BUILD_EXAMPLES=OFF \
        -DGLFW_BUILD_TESTS=OFF \
        -DGLFW_BUILD_DOCS=OFF \
        -DGLFW_INSTALL=ON \
        -DCMAKE_INSTALL_PREFIX=/opt/glfw-win64 \
        && cmake --build build-win64 -j$(nproc) \
        && cmake --install build-win64

WORKDIR /build
