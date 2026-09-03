# Zaviro Network Protocol
# ========================
# Client <-> Server communication uses JSON over WebSocket
# Future: migrate to MessagePack or Protobuf for performance
#
# All messages have the format:
# {
#   "type": "messageType",
#   "data": { ... }
# }

# ==========================================
# CLIENT -> SERVER MESSAGES
# ==========================================

# Player input (sent every frame or on change)
# input:
#   type: "input"
#   data:
#     forward: float  (-1, 0, 1)
#     right:   float  (-1, 0, 1)
#     up:      float  (-1, 0, 1)  # jump when > 0
#     mouseX:  float
#     mouseY:  float

# Chat message
# chat:
#   type: "chat"
#   data:
#     message: string

# Place a block
# placeBlock:
#   type: "placeBlock"
#   data:
#     position: { x, y, z }
#     color: { r, g, b }
#     scale: { x, y, z }  (optional, default 1,1,1)

# Break a block
# breakBlock:
#   type: "breakBlock"
#   data:
#     entityId: uint64


# ==========================================
# SERVER -> CLIENT MESSAGES
# ==========================================

# Welcome message on connect
# welcome:
#   type: "welcome"
#   data:
#     clientId: uint64
#     playerName: string

# Full world state (sent on connect)
# worldState:
#   type: "worldState"
#   data:
#     tick: uint64
#     players: { id: { id, name, position, rotation } }
#     entities: { id: { id, name, position, scale, color, isStatic } }

# Player joined
# playerJoin:
#   type: "playerJoin"
#   data:
#     clientId: uint64
#     playerName: string
#     position: { x, y, z }

# Player left
# playerLeave:
#   type: "playerLeave"
#   data:
#     clientId: uint64

# World update (sent every tick)
# worldUpdate:
#   type: "worldUpdate"
#   data:
#     tick: uint64
#     players: { id: { id, name, position, rotation } }

# Entity created (block placed)
# entityCreate:
#   type: "entityCreate"
#   data:
#     id: uint64
#     name: string
#     position: { x, y, z }
#     scale: { x, y, z }
#     color: { r, g, b }
#     isStatic: bool

# Entity removed (block broken)
# entityRemove:
#   type: "entityRemove"
#   data:
#     entityId: uint64

# Chat broadcast
# chat:
#   type: "chat"
#   data:
#     clientId: uint64
#     playerName: string
#     message: string
