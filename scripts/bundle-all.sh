#!/bin/bash
# bundle-all.sh — Build + package portable bundles for Zaviro
# Creates: dist/zaviro-linux-x64/  and  dist/zaviro-windows-x64/
# Each contains: client, server, tor binary, config, start scripts
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
DIST="$REPO_ROOT/dist"
VERSION="${1:-0.2.0}"

rm -rf "$DIST"
mkdir -p "$DIST/zaviro-linux-x64" "$DIST/zaviro-windows-x64"

echo "============================================"
echo " Zaviro Portable Bundle Builder v${VERSION}"
echo "============================================"

# ---- 1. Build Linux client ----
echo ""
echo "[1/5] Building Linux client..."
cd "$REPO_ROOT/client"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cp build/zaviro_client "$DIST/zaviro-linux-x64/zaviro_client"
chmod +x "$DIST/zaviro-linux-x64/zaviro_client"

# ---- 2. Cross-compile Windows client ----
echo ""
echo "[2/5] Cross-compiling Windows client via Docker..."
bash "$SCRIPT_DIR/build-windows-client.sh"
cp "$REPO_ROOT/client/build-win64/zaviro_client.exe" \
   "$DIST/zaviro-windows-x64/zaviro_client.exe" 2>/dev/null || {
    echo "WARNING: Windows .exe not found, skipping Windows build"
}

# ---- 3. Bundle server (Node.js source + npm ci) ----
echo ""
echo "[3/5] Bundling server..."
cd "$REPO_ROOT"

# Linux server bundle
mkdir -p "$DIST/zaviro-linux-x64/server"
cp -r server/src server/config server/package.json server/package-lock.json \
    "$DIST/zaviro-linux-x64/server/"

# Windows server bundle (same source, runs with Node.js on Windows)
mkdir -p "$DIST/zaviro-windows-x64/server"
cp -r server/src server/config server/package.json server/package-lock.json \
    "$DIST/zaviro-windows-x64/server/"

# ---- 4. Bundle Tor ----
echo ""
echo "[4/5] Bundling Tor..."

# Linux: copy system tor or download
if [ -f /usr/bin/tor ]; then
    cp /usr/bin/tor "$DIST/zaviro-linux-x64/tor"
    chmod +x "$DIST/zaviro-linux-x64/tor"
    echo "  Linux: copied system tor"
else
    echo "  Linux: tor not found at /usr/bin/tor"
    echo "  Linux: user must install tor separately"
fi

# Windows: download Tor Expert Bundle
WIN_TOR_DIR="$DIST/zaviro-windows-x64/tor"
mkdir -p "$WIN_TOR_DIR"
echo "  Windows: downloading Tor Expert Bundle..."
cd /tmp
# Try the latest stable tor expert bundle
TOR_URL="https://dist.torproject.org/torbrowser/14.5.6/tor-expert-bundle-windows-x86_64-14.5.6-all-unsigned.tar.gz"
if wget -q --timeout=30 "$TOR_URL" -O tor-win64.tar.gz 2>/dev/null; then
    tar xzf tor-win64.tar.gz 2>/dev/null
    # Find and copy tor.exe + Data directory
    find . -name "tor.exe" -exec cp {} "$WIN_TOR_DIR/" \;
    find . -name "torrc" -exec cp {} "$WIN_TOR_DIR/" \;
    cp -r ./tor*/Data "$WIN_TOR_DIR/" 2>/dev/null || true
    rm -rf tor-win64.tar.gz ./tor*
    echo "  Windows: Tor bundled"
else
    echo "  Windows: could not download Tor, bundling placeholder"
    echo "# Download tor.exe from https://www.torproject.org/download/tor/" > "$WIN_TOR_DIR/README.txt"
fi
cd "$REPO_ROOT"

# ---- 5. Create start scripts ----
echo ""
echo "[5/5] Creating start scripts..."

# Linux start script
cat > "$DIST/zaviro-linux-x64/start-server.sh" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")"

# Start Tor in background (if available)
if [ -x ./tor ]; then
    echo "Starting Tor..."
    ./tor &
    TOR_PID=$!
    sleep 3
fi

# Install server dependencies (first run)
if [ ! -d server/node_modules ]; then
    echo "Installing server dependencies..."
    cd server && npm install && cd ..
fi

# Start server
echo "Starting Zaviro server..."
cd server
exec node src/index.js \
    --tor:enabled=true \
    --tor:socksPort=19050
EOF
chmod +x "$DIST/zaviro-linux-x64/start-server.sh"

cat > "$DIST/zaviro-linux-x64/start-client.sh" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")"
./zaviro_client 127.0.0.1 --port 8765
EOF
chmod +x "$DIST/zaviro-linux-x64/start-client.sh"

# Windows start scripts
cat > "$DIST/zaviro-windows-x64/start-server.bat" << 'EOF'
@echo off
cd /d "%~dp0"

REM Start Tor in background (if available)
if exist tor\tor.exe (
    echo Starting Tor...
    start /B tor\tor.exe
    timeout /t 3 /nobreak >nul
)

REM Install server dependencies (first run)
if not exist server\node_modules (
    echo Installing server dependencies...
    cd server && npm install && cd ..
)

REM Start server
echo Starting Zaviro server...
cd server
node src/index.js --tor:enabled=true --tor:socksPort=19050
EOF

cat > "$DIST/zaviro-windows-x64/start-client.bat" << 'EOF'
@echo off
cd /d "%~dp0"
zaviro_client.exe 127.0.0.1 --port 8765
EOF

# ---- Create tarballs ----
echo ""
echo "Creating release archives..."
cd "$DIST"

tar czf "zaviro-linux-x64-v${VERSION}.tar.gz" "zaviro-linux-x64"
tar czf "zaviro-windows-x64-v${VERSION}.tar.gz" "zaviro-windows-x64"

echo ""
echo "============================================"
echo " Build complete!"
echo "============================================"
echo ""
echo "Linux:   dist/zaviro-linux-x64-v${VERSION}.tar.gz"
echo "Windows: dist/zaviro-windows-x64-v${VERSION}.tar.gz"
echo ""
echo "Each bundle contains:"
echo "  - Client (zaviro_client / zaviro_client.exe)"
echo "  - Server (server/ with Node.js source)"
echo "  - Tor binary (tor / tor/tor.exe)"
echo "  - Start scripts (start-server + start-client)"
echo ""
ls -lh "$DIST"/*.tar.gz 2>/dev/null
