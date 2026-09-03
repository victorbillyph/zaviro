#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include "game/world.h"
#include "game/player.h"
#include "network/client.h"

class Window;
class Renderer;

struct RemotePlayer {
    std::string name;
    Vec3 position;
    float yaw = -90.0f;
    float pitch = 0.0f;
};

struct UniverseInfo {
    int id = 0;
    std::string name;
    std::string description;
    int players = 0;
    int maxPlayers = 32;
    float bgColorR = 0.15f;
    float bgColorG = 0.15f;
    float bgColorB = 0.2f;
    int seed = 0;
};

enum class GameState {
    LOBBY,
    LOADING,
    IN_GAME
};

class Engine {
public:
    static Engine& instance();

    bool initialize(const std::string& title, int width, int height, const std::string& serverHost = "127.0.0.1", int serverPort = 8765);
    void run();
    void shutdown();

    Window* getWindow() const { return m_window.get(); }
    Renderer* getRenderer() const { return m_renderer.get(); }
    World* getWorld() { return &m_world; }
    Player* getPlayer() { return &m_player; }

    bool isRunning() const { return m_running; }
    bool isOnline() const { return m_network.isConnected(); }
    GameState getGameState() const { return m_gameState; }
    void stop() { m_running = false; }

private:
    Engine() = default;
    void onNetworkMessage(const NetworkMessage& msg);

    // Lobby
    void runLobby();
    void renderLobby();
    void handleLobbyClick(float mouseX, float mouseY);

    // Game
    void runGame();
    void renderGame();

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    World m_world;
    Player m_player;
    NetworkClient m_network;
    uint64_t m_clientId = 0;
    std::unordered_map<uint64_t, RemotePlayer> m_remotePlayers;

    GameState m_gameState = GameState::LOBBY;
    int m_selectedUniverse = -1;
    int m_hoveredUniverse = -1;
    float m_mouseX = 0, m_mouseY = 0;
    bool m_firstMouse = true;
    bool m_wantJoin = false;

    std::vector<UniverseInfo> m_universes;
    bool m_online = false;

    bool m_running = false;
};
