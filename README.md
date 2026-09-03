# Zaviro

A game engine platform inspired by Roblox, with a C++ client and Node.js server.

## Architecture

```
Zaviro/
├── client/          # C++ game client (SDL2/OpenGL)
│   ├── CMakeLists.txt
│   ├── include/     # Header files
│   └── src/         # Source files
│       ├── engine/  # Core engine (renderer, window, game loop)
│       ├── network/ # Client-side networking
│       └── game/    # Game logic (world, player)
├── server/          # Node.js game server (WebSocket)
│   ├── src/
│   │   ├── game/    # Server-side game logic
│   │   └── network/ # WebSocket server
│   └── config/      # Server configuration
├── proto/           # Network protocol documentation
└── setup.sh         # Setup script
```

## Features

- **Client (C++)**
  - OpenGL 3.3 renderer with shaders
  - Window management with GLFW
  - 3D math library (vectors, matrices)
  - TCP networking with threading
  - Entity-component system

- **Server (Node.js)**
  - WebSocket real-time communication
  - Authoritative game state
  - Basic physics simulation (gravity, collisions)
  - Multiplayer support (up to 64 players)
  - Entity management (place/break blocks)

## Prerequisites

### Server
- Node.js >= 18
- npm

### Client
- CMake >= 3.20
- C++20 compiler (GCC/Clang)
- GLFW3: `sudo apt install libglfw3-dev`
- GLM: `sudo apt install libglm-dev`
- nlohmann-json: `sudo apt install nlohmann-json3-dev`
- GLAD: https://glad.dav1d.de/

## Quick Start

```bash
# 1. Setup (install dependencies)
./setup.sh

# 2. Start the server
cd server
npm start

# 3. Build and run the client
cd client
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./zaviro_client
```

## Controls

- **WASD** - Move
- **Mouse** - Look around
- **Space** - Jump

## Network Protocol

All communication uses JSON over WebSocket. See `proto/protocol.md` for details.

## Configuration

Server config is in `server/config/default.json`:

```json
{
  "server": {
    "port": 8765,
    "maxPlayers": 64,
    "tickRate": 30
  },
  "world": {
    "gravity": -20.0,
    "maxEntities": 10000
  }
}
```

## License

MIT
