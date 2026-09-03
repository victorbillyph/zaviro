#!/bin/bash
# Zaviro - Setup script

set -e

echo "=== Zaviro Game Setup ==="
echo ""

# Install server dependencies
echo "[1/2] Installing server dependencies..."
cd server
npm install
cd ..
echo "Server dependencies installed!"
echo ""

# Check client dependencies
echo "[2/2] Checking client dependencies..."
echo "Required system packages for client:"
echo "  - CMake >= 3.20"
echo "  - GLFW3"
echo "  - GLAD"
echo "  - GLM"
echo "  - nlohmann-json"
echo ""
echo "On Ubuntu/Debian:"
echo "  sudo apt install cmake libglfw3-dev libglm-dev nlohmann-json3-dev"
echo ""
echo "  For GLAD, download from https://glad.dav1d.de/ or use:"
echo "  git clone https://github.com/Dav1dde/glad.git third_party/glad"
echo ""

echo "=== Setup Complete ==="
echo ""
echo "To run the server:"
echo "  cd server && npm start"
echo ""
echo "To build the client:"
echo "  cd client && mkdir -p build && cd build"
echo "  cmake .. && make -j\$(nproc)"
echo "  ./zaviro_client"
