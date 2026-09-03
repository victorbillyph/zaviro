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

struct RemoteServer {
    std::string onion;
    std::string name;
    std::string region;
    std::string pubKey;
};

// A UI element declared by a universe's scriptClient (custom in-game UI).
struct CustomUIElement {
    std::string type;        // "button" | "panel" | "text" | "image"
    std::string id;
    float x = 0, y = 0, w = 0, h = 0;
    std::string text;
    float r = 0.3f, g = 0.3f, b = 0.3f, a = 0.9f;
    float fontSize = 0.8f;
    std::string texture;     // texture name for "image"
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

    // Tor SOCKS proxy for connecting to .onion servers.
    void setTorProxy(const std::string& proxyHost, int proxyPort) { m_proxyHost = proxyHost; m_proxyPort = proxyPort; }

private:
    Engine() = default;
    void onNetworkMessage(const NetworkMessage& msg);

    // Lobby
    void runLobby();
    void renderLobby();
    void handleLobbyClick(float mouseX, float mouseY);
    void connectToServer(const std::string& host, int port);

    // Server list state
    int m_hoveredServer = -1;
    int m_selectedServer = -1;  // index of currently connected server in list
    std::string m_currentServerHost;
    int m_currentServerPort = 8765;

    // Game
    void runGame();
    void renderGame();

    // Custom in-game UI (from universe scriptClient)
    void renderCustomUI();
    void handleCustomUIClick(float mouseX, float mouseY);

    // Universe editor (lobby)
    void renderEditor();
    void handleEditorClick(float mouseX, float mouseY);
    void handleEditorType(const std::string& typed);

    void decodeTexture(const std::string& name, const std::string& mime, const std::string& base64Data);
    void clearRuntimeTextures();

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
    double m_lastMouseX = 0, m_lastMouseY = 0;

    std::vector<UniverseInfo> m_universes;
    std::vector<RemoteServer> m_serverList;
    std::string m_connectedServerName = "Zaviro Server";
    std::string m_serverPubKey;
    std::vector<RemoteServer> m_federationList;
    bool m_online = false;

    std::string m_proxyHost;
    int m_proxyPort = 9050;
    bool m_useProxy = false;

    // Custom UI state (from universe scriptClient)
    std::vector<CustomUIElement> m_uiElements;
    bool m_customUIEnabled = false;
    bool m_hasServerScript = false;
    std::unordered_map<std::string, unsigned int> m_runtimeTextures;

    // Universe editor state
    bool m_showEditor = false;
    std::string m_editName;
    std::string m_editDesc;
    std::string m_editScript;
    int m_editState = 0;   // 0 = name field active, 1 = desc, 2 = script
    bool m_editIsNew = false;

    bool m_running = false;
};
