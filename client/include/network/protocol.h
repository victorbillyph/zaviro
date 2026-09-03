#pragma once

#include <cstdint>

// Network message types (shared with the Node.js server)
namespace Proto {
    // Client -> Server
    constexpr uint16_t HELLO = 1;
    constexpr uint16_t INPUT = 2;
    constexpr uint16_t CHAT = 3;
    constexpr uint16_t PLACE_BLOCK = 4;
    constexpr uint16_t BREAK_BLOCK = 5;
    constexpr uint16_t JOIN_UNIVERSE = 6;
    constexpr uint16_t LEAVE_UNIVERSE = 7;

    // Server -> Client
    constexpr uint16_t WELCOME = 100;
    constexpr uint16_t WORLD_STATE = 101;
    constexpr uint16_t WORLD_UPDATE = 102;
    constexpr uint16_t PLAYER_JOIN = 103;
    constexpr uint16_t PLAYER_LEAVE = 104;
    constexpr uint16_t ENTITY_CREATE = 105;
    constexpr uint16_t ENTITY_REMOVE = 106;
    constexpr uint16_t CHAT_BROADCAST = 107;
    constexpr uint16_t UNIVERSE_LIST = 108;
    constexpr uint16_t UNIVERSE_JOINED = 109;
}
