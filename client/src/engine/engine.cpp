#include "engine/engine.h"
#include "engine/renderer.h"
#include "engine/window.h"
#include "network/protocol.h"
#include <nlohmann/json.hpp>
#include "stb_image.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

using nlohmann::json;

#define KEY_W 87
#define KEY_A 65
#define KEY_S 83
#define KEY_D 68
#define KEY_SPACE 32
#define KEY_ESCAPE 256
#define MOUSE_LEFT 0

// Copy GLFW key codes used by the editor text fields (ASCII letters/digits)
#define KEY_0 48
#define KEY_1 49

static std::vector<unsigned char> base64Decode(const std::string& in) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> out;
    int val = 0, vbits = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        size_t idx = chars.find((char)c);
        if (idx == std::string::npos) continue;
        val = (val << 6) | (int)idx;
        vbits += 6;
        if (vbits >= 0) {
            out.push_back((unsigned char)((val >> vbits) & 0xFF));
            vbits -= 8;
        }
    }
    return out;
}

Engine& Engine::instance() {
    static Engine inst;
    return inst;
}

bool Engine::initialize(const std::string& title, int width, int height, const std::string& serverHost, int serverPort) {
    m_window = std::make_unique<Window>(width, height, title);
    if (!m_window) {
        std::cerr << "Failed to create window" << std::endl;
        return false;
    }

    m_renderer = std::make_unique<Renderer>();
    if (!m_renderer->initialize(width, height)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return false;
    }

    // Network
    std::cout << "Connecting to server at " << serverHost << ":" << serverPort << "..." << std::endl;
    bool isOnion = serverHost.size() >= 8 && serverHost.rfind(".onion") == serverHost.size() - 6;
    if (isOnion && !m_proxyHost.empty()) {
        m_network.setProxy(m_proxyHost, m_proxyPort);
        m_network.setUseProxy(true);
        std::cout << "Connecting via Tor SOCKS proxy " << m_proxyHost << ":" << m_proxyPort << std::endl;
    }
    if (m_network.connect(serverHost, serverPort)) {
        std::cout << "Connected to server!" << std::endl;
        m_online = true;
        m_network.setMessageCallback([this](const NetworkMessage& msg) {
            onNetworkMessage(msg);
        });
        m_network.send(Proto::HELLO, json({{"name", "Player1"}}).dump());
    } else {
        std::cout << "Running in offline mode" << std::endl;
    }

    m_running = true;
    m_window->setCursorDisabled(false);
    m_currentServerHost = serverHost;
    m_currentServerPort = serverPort;
    std::cout << "Zaviro Engine initialized" << std::endl;
    return true;
}

void Engine::connectToServer(const std::string& host, int port) {
    if (m_network.isConnected()) {
        m_network.disconnect();
    }
    m_online = false;
    m_serverList.clear();
    m_universes.clear();
    m_clientId = 0;
    m_remotePlayers.clear();
    m_gameState = GameState::LOBBY;

    bool isOnion = host.size() >= 6 && host.rfind(".onion") == host.size() - 6;
    if (isOnion) {
        if (m_proxyHost.empty()) {
            // Default Tor SOCKS proxy
            m_proxyHost = "127.0.0.1";
            m_proxyPort = 19050;
        }
        m_network.setProxy(m_proxyHost, m_proxyPort);
        m_network.setUseProxy(true);
        std::cout << "Connecting via Tor SOCKS proxy to " << host << ":" << port << std::endl;
    } else {
        m_network.setUseProxy(false);
        std::cout << "Connecting directly to " << host << ":" << port << std::endl;
    }

    if (m_network.connect(host, port)) {
        m_online = true;
        m_network.setMessageCallback([this](const NetworkMessage& msg) {
            onNetworkMessage(msg);
        });
        m_network.send(Proto::HELLO, json({{"name", "Player1"}}).dump());
        m_currentServerHost = host;
        m_currentServerPort = port;
        std::cout << "Connected!" << std::endl;
    } else {
        std::cout << "Connection failed" << std::endl;
    }
}

void Engine::onNetworkMessage(const NetworkMessage& msg) {
    json j;
    try {
        j = json::parse(msg.data.empty() ? "{}" : msg.data);
    } catch (...) { return; }

    switch (msg.type) {
        case Proto::WELCOME:
            m_clientId = j.value("clientId", 0);
            if (j.contains("server")) {
                m_connectedServerName = j["server"].value("name", "Zaviro Server");
                if (j["server"].contains("pubKey")) {
                    m_serverPubKey = j["server"].value("pubKey", "");
                }
            }
            break;

        case Proto::SERVER_LIST: {
            m_serverList.clear();
            if (j.contains("servers") && j["servers"].is_array()) {
                for (const auto& s : j["servers"]) {
                    RemoteServer rs;
                    rs.onion = s.value("onion", "");
                    rs.name = s.value("name", "unknown");
                    rs.region = s.value("region", "");
                    rs.pubKey = s.value("pubKey", "");
                    if (!rs.onion.empty()) m_serverList.push_back(rs);
                }
            }
            break;
        }

        case Proto::UNIVERSE_LIST: {
            m_universes.clear();
            if (j.contains("universes") && j["universes"].is_array()) {
                for (const auto& u : j["universes"]) {
                    UniverseInfo info;
                    info.id = u.at("id").get<int>();
                    info.name = u.value("name", "");
                    info.description = u.value("description", "");
                    info.players = u.value("players", 0);
                    info.maxPlayers = u.value("maxPlayers", 32);
                    if (u.contains("bgColor")) {
                        info.bgColorR = u["bgColor"].value("r", 0.15f);
                        info.bgColorG = u["bgColor"].value("g", 0.15f);
                        info.bgColorB = u["bgColor"].value("b", 0.2f);
                    }
                    info.seed = u.value("seed", 0);
                    m_universes.push_back(info);
                }
            }
            break;
        }

        case Proto::UNIVERSE_JOINED: {
            if (j.value("success", false)) {
                m_gameState = GameState::LOADING;
                m_wantJoin = true;
                std::cout << "Joining universe..." << std::endl;
            }
            break;
        }

        case Proto::WORLD_STATE: {
            m_remotePlayers.clear();
            if (j.contains("world") && j["world"].contains("players") && j["world"]["players"].is_array()) {
                for (const auto& p : j["world"]["players"]) {
                    uint64_t pid = p.at("id").get<uint64_t>();
                    if (pid == m_clientId) continue;
                    RemotePlayer rp;
                    rp.name = p.value("name", "");
                    if (p.contains("position")) {
                        rp.position.x = p["position"].value("x", 0.0f);
                        rp.position.y = p["position"].value("y", 0.0f);
                        rp.position.z = p["position"].value("z", 0.0f);
                    }
                    m_remotePlayers[pid] = rp;
                }
            }
            if (j.contains("world") && j["world"].contains("entities") && j["world"]["entities"].is_array()) {
                for (const auto& e : j["world"]["entities"]) {
                    uint64_t eid = e.at("id").get<uint64_t>();
                    Vec3 pos(e.at("position").at("x").get<float>(),
                             e.at("position").at("y").get<float>(),
                             e.at("position").at("z").get<float>());
                    Vec3 scale(e.at("scale").at("x").get<float>(),
                               e.at("scale").at("y").get<float>(),
                               e.at("scale").at("z").get<float>());
                    Color col(e.at("color").at("r").get<float>(),
                              e.at("color").at("g").get<float>(),
                              e.at("color").at("b").get<float>());
                    auto* existing = m_world.getEntity(eid);
                    if (!existing) {
                        m_world.createEntity(e.value("name", "Block"), pos, scale, col);
                    }
                }
            }
            if (m_gameState == GameState::LOADING) {
                m_gameState = GameState::IN_GAME;
                m_player = Player(1, "Player1");
                m_player.heightAt = [this](float x, float z) {
                    return m_renderer->terrainHeightAt(x, z);
                };
                m_player.transform.position = {0.0f, 10.0f, 8.0f};
                m_window->setCursorDisabled(true);
                m_firstMouse = true;
                std::cout << "Game loaded! Playing." << std::endl;
            }
            break;
        }

        case Proto::WORLD_UPDATE: {
            if (j.contains("players") && j["players"].is_object()) {
                for (auto& [key, p] : j["players"].items()) {
                    uint64_t pid = std::stoull(key);
                    if (pid == m_clientId) continue;
                    RemotePlayer rp;
                    rp.name = p.value("name", "");
                    rp.position.x = p["position"].value("x", 0.0f);
                    rp.position.y = p["position"].value("y", 0.0f);
                    rp.position.z = p["position"].value("z", 0.0f);
                    rp.yaw = p["rotation"].value("yaw", -90.0f);
                    rp.pitch = p["rotation"].value("pitch", 0.0f);
                    m_remotePlayers[pid] = rp;
                }
            }
            // Remove players that left
            if (j.contains("players") && j["players"].is_object()) {
                std::vector<uint64_t> toRemove;
                for (const auto& [pid, rp] : m_remotePlayers) {
                    if (!j["players"].contains(std::to_string(pid))) {
                        toRemove.push_back(pid);
                    }
                }
                for (auto pid : toRemove) {
                    m_remotePlayers.erase(pid);
                }
            }
            if (j.contains("self") && j["self"].contains("position") && m_clientId != 0) {
                float sx = j["self"]["position"].value("x", 0.0f);
                float sy = j["self"]["position"].value("y", 0.0f);
                float sz = j["self"]["position"].value("z", 0.0f);
                // Server authoritative — correct client prediction
                m_player.transform.position.x += (sx - m_player.transform.position.x) * 0.5f;
                m_player.transform.position.y = sy;  // snap Y (server controls gravity)
                m_player.transform.position.z += (sz - m_player.transform.position.z) * 0.5f;
            }
            break;
        }

        case Proto::PLAYER_JOIN: {
            uint64_t pid = j.value("clientId", 0);
            if (pid == m_clientId) break;
            RemotePlayer rp;
            rp.name = j.value("playerName", "Player");
            if (j.contains("position")) {
                rp.position.x = j["position"].value("x", 0.0f);
                rp.position.y = j["position"].value("y", 0.0f);
                rp.position.z = j["position"].value("z", 0.0f);
            }
            m_remotePlayers[pid] = rp;
            break;
        }

        case Proto::PLAYER_LEAVE:
            m_remotePlayers.erase(j.value("clientId", 0));
            break;

        case Proto::ENTITY_CREATE: {
            Vec3 pos(j.at("position").at("x").get<float>(),
                     j.at("position").at("y").get<float>(),
                     j.at("position").at("z").get<float>());
            Vec3 scale(j.at("scale").at("x").get<float>(),
                       j.at("scale").at("y").get<float>(),
                       j.at("scale").at("z").get<float>());
            Color col(j.at("color").at("r").get<float>(),
                      j.at("color").at("g").get<float>(),
                      j.at("color").at("b").get<float>());
            uint64_t eid = j.at("id").get<uint64_t>();
            auto* e = m_world.getEntity(eid);
            if (!e) {
                m_world.createEntity(j.value("name", "Block"), pos, scale, col);
            }
            break;
        }

        case Proto::ENTITY_REMOVE:
            m_world.removeEntity(j.value("entityId", 0));
            break;

        case Proto::UNIVERSE_CREATED:
        case Proto::UNIVERSE_UPDATED:
        case Proto::UNIVERSE_DELETED: {
            if (j.value("success", false)) {
                std::cout << "Universe update acknowledged" << std::endl;
            }
            break;
        }

        case Proto::UI_DEFINITION: {
            m_uiElements.clear();
            m_customUIEnabled = false;
            if (j.contains("ui") && j["ui"].is_array()) {
                for (const auto& el : j["ui"]) {
                    CustomUIElement e;
                    e.type = el.value("type", "panel");
                    e.id = el.value("id", "");
                    e.x = el.value("x", 0.0f);
                    e.y = el.value("y", 0.0f);
                    e.w = el.value("w", 100.0f);
                    e.h = el.value("h", 40.0f);
                    e.text = el.value("text", "");
                    e.fontSize = el.value("fontSize", 0.8f);
                    e.texture = el.value("texture", "");
                    if (el.contains("color")) {
                        e.r = el["color"].value("r", 0.3f);
                        e.g = el["color"].value("g", 0.3f);
                        e.b = el["color"].value("b", 0.3f);
                        e.a = el["color"].value("a", 0.9f);
                    }
                    m_uiElements.push_back(e);
                }
                m_customUIEnabled = true;
            }
            m_hasServerScript = j.value("hasServerScript", false);
            m_window->setCursorDisabled(false); // show cursor for UI interaction
            std::cout << "Custom UI loaded: " << m_uiElements.size() << " elements" << std::endl;
            break;
        }

        case Proto::TEXTURE_DATA: {
            std::string name = j.value("name", "");
            std::string mime = j.value("mime", "image/png");
            std::string b64 = j.value("data", "");
            if (!name.empty() && !b64.empty()) {
                decodeTexture(name, mime, b64);
            }
            break;
        }
    }
}

void Engine::decodeTexture(const std::string& name, const std::string& mime, const std::string& base64Data) {
    auto bytes = base64Decode(base64Data);
    if (bytes.empty()) {
        std::cout << "Texture " << name << ": empty data" << std::endl;
        return;
    }
    int width = 0, height = 0, channels = 0;
    unsigned char* img = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &width, &height, &channels, 4);
    if (!img) {
        std::cout << "Texture " << name << ": failed to decode" << std::endl;
        return;
    }
    unsigned int tex = m_renderer->createTextureFromData(img, width, height, 4);
    stbi_image_free(img);
    // Release any existing texture with the same name
    auto it = m_runtimeTextures.find(name);
    if (it != m_runtimeTextures.end() && it->second) {
        m_renderer->deleteTexture(it->second);
    }
    m_runtimeTextures[name] = tex;
    std::cout << "Texture loaded: " << name << " (" << width << "x" << height << ")" << std::endl;
}

void Engine::clearRuntimeTextures() {
    for (auto& [name, tex] : m_runtimeTextures) {
        if (tex) m_renderer->deleteTexture(tex);
    }
    m_runtimeTextures.clear();
}

void Engine::run() {
    while (m_running && !m_window->shouldClose()) {
        m_window->pollEvents();
        m_network.update();

        float dt = m_renderer->getDeltaTime();
        if (dt <= 0.0f) dt = 0.016f;

        switch (m_gameState) {
            case GameState::LOBBY:
            case GameState::LOADING:
                runLobby();
                break;
            case GameState::IN_GAME:
                runGame();
                break;
        }
    }
}

// ============ LOBBY ============

void Engine::runLobby() {
    // Ensure cursor is visible in lobby (fixes click detection)
    if (m_window->isCursorDisabled()) {
        m_window->setCursorDisabled(false);
    }

    // Mouse position for hover detection
    double mx, my;
    m_window->getMousePosition(mx, my);
    m_mouseX = (float)mx;
    m_mouseY = (float)my;

    if (m_showEditor) {
        // Text input for the editor form
        std::string typed = m_window->consumeTypedText();
        if (!typed.empty()) handleEditorType(typed);
        if (m_window->isKeyJustPressed(GLFW_KEY_ENTER) && m_editState == 2 && !m_editScript.empty()) {
            // Save button is the primary action; Enter just commits focused field
        }
        if (m_window->isMouseJustPressed(MOUSE_LEFT)) {
            handleEditorClick(m_mouseX, m_mouseY);
        }
    } else {
        // Handle click on universe card
        if (m_window->isMouseJustPressed(MOUSE_LEFT)) {
            handleLobbyClick(m_mouseX, m_mouseY);
        }
    }

    renderLobby();
}

void Engine::handleLobbyClick(float mx, float my) {
    float w = (float)m_renderer->getUiWidth();
    float h = (float)m_renderer->getUiHeight();

    // "New Universe" button (top-right)
    float nbW = 200.0f, nbH = 44.0f;
    float nbX = w - nbW - 16.0f, nbY = 16.0f;
    if (m_online && mx >= nbX && mx <= nbX + nbW && my >= nbY && my <= nbY + nbH) {
        m_showEditor = true;
        m_editIsNew = true;
        m_editName = "";
        m_editDesc = "";
        m_editScript = "";
        m_editState = 0;
        m_window->setCursorDisabled(false);
        std::cout << "Opening universe editor (new)" << std::endl;
        return;
    }

    // Card layout: centered column
    float cardW = 600.0f;
    float cardH = 80.0f;
    float gap = 12.0f;
    float startX = (w - cardW) / 2.0f;
    float startY = 120.0f;

    for (size_t i = 0; i < m_universes.size(); i++) {
        float cy = startY + i * (cardH + gap);
        if (mx >= startX && mx <= startX + cardW && my >= cy && my <= cy + cardH) {
            m_selectedUniverse = (int)i;
            // Send JOIN_UNIVERSE
            json join = {{"universeId", m_universes[i].id}};
            m_network.send(Proto::JOIN_UNIVERSE, join.dump());
            std::cout << "Requesting to join: " << m_universes[i].name << std::endl;
            break;
        }
    }

    // Server card clicks
    float serverY = startY + (float)m_universes.size() * (cardH + gap) + 30.0f + 22.0f;
    float serverCardW = 400.0f;
    float serverCardH = 50.0f;

    // Current server card (skip)
    serverY += serverCardH + 6.0f;

    // Remote server cards
    for (size_t i = 0; i < m_serverList.size(); i++) {
        if (mx >= startX && mx <= startX + serverCardW &&
            my >= serverY && my <= serverY + serverCardH) {
            const auto& s = m_serverList[i];
            std::cout << "Connecting to server: " << s.name << " (" << s.onion << ")" << std::endl;
            connectToServer(s.onion, 8765);
            return;
        }
        serverY += serverCardH + 6.0f;
    }
}

void Engine::renderLobby() {
    m_renderer->beginFrame();

    // Skybox background
    Vec3 camPos(0, 5, 20);
    Vec3 camTarget(0, 5, 0);
    m_renderer->setCamera(camPos, camTarget);
    m_renderer->drawSkybox(camPos);
    m_renderer->drawTerrain(camPos);

    m_renderer->uiBegin();

    float w = (float)m_renderer->getUiWidth();
    float h = (float)m_renderer->getUiHeight();

    // Semi-transparent dark overlay
    m_renderer->drawRect(0, 0, w, h, Color(0.02f, 0.02f, 0.08f, 0.85f));

    // Title
    std::string title = "ZAVIRO";
    m_renderer->drawText(title, w/2.0f - 50.0f, 30.0f, 2.0f, Color(1.0f, 0.92f, 0.3f, 1.0f));

    // Subtitle
    std::string subtitle = "Escolha um Universo para entrar";
    m_renderer->drawText(subtitle, w/2.0f - 140.0f, 75.0f, 1.0f, Color(0.8f, 0.8f, 0.9f, 1.0f));

    // New Universe button (top-right)
    if (m_online) {
        float nbW = 200.0f, nbH = 44.0f;
        float nbX = w - nbW - 16.0f, nbY = 16.0f;
        bool nHov = (m_mouseX >= nbX && m_mouseX <= nbX + nbW && m_mouseY >= nbY && m_mouseY <= nbY + nbH);
        m_renderer->drawRect(nbX, nbY, nbW, nbH, Color(nHov ? 0.15f : 0.08f, 0.55f, 0.3f, 0.95f));
        m_renderer->drawText("+ Novo Universo", nbX + 18.0f, nbY + 12.0f, 1.1f, Color(1,1,1,1));
    }

    // Universe cards
    float cardW = 600.0f;
    float cardH = 80.0f;
    float gap = 12.0f;
    float startX = (w - cardW) / 2.0f;
    float startY = 120.0f;

    m_hoveredUniverse = -1;
    for (size_t i = 0; i < m_universes.size(); i++) {
        const auto& u = m_universes[i];
        float cy = startY + i * (cardH + gap);

        // Hover detection
        bool hovered = (m_mouseX >= startX && m_mouseX <= startX + cardW &&
                        m_mouseY >= cy && m_mouseY <= cy + cardH);
        if (hovered) m_hoveredUniverse = (int)i;

        // Card background
        Color cardBg(hovered ? 0.2f : 0.08f, hovered ? 0.2f : 0.08f, hovered ? 0.35f : 0.12f, 0.95f);
        m_renderer->drawRect(startX, cy, cardW, cardH, cardBg);

        // Accent color stripe
        Color accent(u.bgColorR, u.bgColorG, u.bgColorB, 1.0f);
        m_renderer->drawRect(startX, cy, 6.0f, cardH, accent);

        // Universe name
        m_renderer->drawText(u.name, startX + 20.0f, cy + 8.0f, 1.4f, Color(1,1,1,1));

        // Description
        m_renderer->drawText(u.description, startX + 20.0f, cy + 32.0f, 0.85f, Color(0.7f,0.7f,0.8f,1));

        // Player count
        std::string count = std::to_string(u.players) + "/" + std::to_string(u.maxPlayers);
        m_renderer->drawText(count, startX + cardW - 80.0f, cy + 8.0f, 1.0f, Color(0.4f,0.9f,0.4f,1));

        // Status text
        std::string status = u.players > 0 ? "JOGANDO" : "LIVRE";
        Color statusColor = u.players > 0 ? Color(1.0f, 0.7f, 0.2f, 1.0f) : Color(0.3f, 0.8f, 0.3f, 1.0f);
        m_renderer->drawText(status, startX + cardW - 80.0f, cy + 30.0f, 0.8f, statusColor);
    }

    // Server list section
    float serverY = startY + (float)m_universes.size() * (cardH + gap) + 30.0f;
    m_renderer->drawText("Servidores", startX, serverY, 1.1f, Color(0.7f, 0.8f, 1.0f, 1.0f));
    serverY += 22.0f;

    float serverCardW = 400.0f;
    float serverCardH = 50.0f;
    m_hoveredServer = -1;

    // Current server (always first)
    {
        bool hov = (m_mouseX >= startX && m_mouseX <= startX + serverCardW &&
                    m_mouseY >= serverY && m_mouseY <= serverY + serverCardH);
        if (hov) m_hoveredServer = 0;
        m_renderer->drawRect(startX, serverY, serverCardW, serverCardH,
            Color(hov ? 0.2f : 0.08f, hov ? 0.2f : 0.08f, 0.35f, 0.95f));
        m_renderer->drawRect(startX, serverY, 4.0f, serverCardH, Color(0.3f, 0.8f, 0.3f, 1.0f));
        std::string label = m_connectedServerName + " (conectado)";
        m_renderer->drawText(label, startX + 14.0f, serverY + 15.0f, 1.0f, Color(0.3f, 0.9f, 0.3f, 1));
        if (!m_serverPubKey.empty()) {
            m_renderer->drawText(m_serverPubKey.substr(0, 16) + "...", startX + 14.0f, serverY + 32.0f, 0.7f, Color(0.5f, 0.7f, 0.9f, 0.7f));
        }
        serverY += serverCardH + 6.0f;
    }

    // Remote servers from federation
    for (size_t i = 0; i < m_serverList.size(); i++) {
        const auto& s = m_serverList[i];
        bool hov = (m_mouseX >= startX && m_mouseX <= startX + serverCardW &&
                    m_mouseY >= serverY && m_mouseY <= serverY + serverCardH);
        if (hov) m_hoveredServer = (int)(i + 1);

        Color cardBg(hov ? 0.2f : 0.08f, hov ? 0.15f : 0.08f, 0.12f, 0.95f);
        m_renderer->drawRect(startX, serverY, serverCardW, serverCardH, cardBg);
        m_renderer->drawRect(startX, serverY, 4.0f, serverCardH, Color(0.8f, 0.5f, 0.2f, 1.0f));

        std::string sname = s.name.empty() ? s.onion.substr(0, 20) + "..." : s.name;
        m_renderer->drawText(sname, startX + 14.0f, serverY + 8.0f, 1.0f, Color(1,1,1,1));
        m_renderer->drawText(s.onion.substr(0, 30) + "...", startX + 14.0f, serverY + 26.0f, 0.7f, Color(0.6f, 0.7f, 0.9f, 0.8f));
        serverY += serverCardH + 6.0f;
    }

    // Bottom hints
    if (m_online) {
        std::string conn = "Clique em um Universo para jogar";
        if (!m_serverList.empty()) {
            conn += " | Rede: " + std::to_string(m_serverList.size() + 1) + " servidores";
        }
        m_renderer->drawText(conn, 10.0f, h - 22.0f, 0.9f, Color(0.4f, 0.8f, 0.4f, 0.8f));
    } else {
        m_renderer->drawText("Offline - Servidor indisponivel", 10.0f, h - 22.0f, 0.9f, Color(0.8f, 0.3f, 0.3f, 0.8f));
    }

    // Loading indicator
    if (m_gameState == GameState::LOADING) {
        m_renderer->drawRect(w/2.0f - 100.0f, h/2.0f - 20.0f, 200.0f, 40.0f, Color(0.1f, 0.1f, 0.2f, 0.9f));
        m_renderer->drawText("Carregando...", w/2.0f - 55.0f, h/2.0f - 5.0f, 1.2f, Color(1,1,1,1));
    }

    if (m_showEditor) {
        renderEditor();
    }

    m_renderer->uiEnd();
    m_renderer->endFrame();
    m_window->swapBuffers();
}

// ============ GAME ============

void Engine::runGame() {
    if (m_firstMouse) {
        m_window->getMousePosition(m_lastMouseX, m_lastMouseY);
        m_firstMouse = false;
    }

    float dt = m_renderer->getDeltaTime();
    if (dt <= 0.0f) dt = 0.016f;

    // Mouse look
    double mouseX, mouseY;
    m_window->getMousePosition(mouseX, mouseY);
    float dx = (float)(mouseX - m_lastMouseX);
    float dy = (float)(mouseY - m_lastMouseY);
    m_lastMouseX = mouseX;
    m_lastMouseY = mouseY;

    // If cursor is visible, use lobby mouse position for UI
    m_mouseX = (float)mouseX;
    m_mouseY = (float)mouseY;

    m_player.setMouseDelta(dx, dy);

    // Keyboard input
    float forward = 0, right = 0, jump = 0;
    if (m_window->isKeyPressed(KEY_W)) forward += 1.0f;
    if (m_window->isKeyPressed(KEY_S)) forward -= 1.0f;
    if (m_window->isKeyPressed(KEY_A)) right += 1.0f;
    if (m_window->isKeyPressed(KEY_D)) right -= 1.0f;
    if (m_window->isKeyPressed(KEY_SPACE)) jump += 1.0f;

    m_player.move(forward, right, jump, dt);
    m_player.update(dt);
    m_world.update(dt);

    // Send input to server
    if (m_network.isConnected()) {
        json input = {
            {"forward", forward}, {"right", right},
            {"jump", jump}, {"mouseX", dx}, {"mouseY", dy}
        };
        m_network.send(Proto::INPUT, input.dump());
    }

    // Custom UI interaction (show cursor, click elements)
    if (m_customUIEnabled) {
        // If cursor is still disabled from FPS mode, show it for UI interaction
        if (m_window->isCursorDisabled()) {
            m_window->setCursorDisabled(false);
        }
        m_mouseX = (float)mouseX;
        m_mouseY = (float)mouseY;
        if (m_window->isMouseJustPressed(MOUSE_LEFT)) {
            handleCustomUIClick(m_mouseX, m_mouseY);
        }
    }

    // ESC to go back to lobby
    if (m_window->isKeyPressed(KEY_ESCAPE)) {
        if (m_window->isCursorDisabled() || m_customUIEnabled) {
            m_window->setCursorDisabled(false);
            m_firstMouse = true;
            clearRuntimeTextures();
            m_uiElements.clear();
            m_customUIEnabled = false;
            m_gameState = GameState::LOBBY;
            m_network.send(Proto::LEAVE_UNIVERSE, "{}");
            m_remotePlayers.clear();
            return;
        }
    }

    renderGame();
}

void Engine::renderGame() {
    m_renderer->beginFrame();

    Vec3 camPos = m_player.transform.position + Vec3(0.0f, 1.5f, 0.0f);
    float yawRad = m_player.getYaw() * 3.14159f / 180.0f;
    float pitchRad = m_player.getPitch() * 3.14159f / 180.0f;
    Vec3 front(
        cosf(pitchRad) * cosf(yawRad),
        sinf(pitchRad),
        cosf(pitchRad) * sinf(yawRad)
    );

    m_renderer->setCamera(camPos, camPos + front);
    m_renderer->drawSkybox(camPos);
    m_renderer->drawTerrain(camPos);

    // World entities
    m_renderer->setCubeProgram();
    for (const auto& [id, entity] : m_world.getEntities()) {
        m_renderer->drawCube(entity.transform.position, entity.transform.scale, entity.color);
    }

    // Remote players
    for (const auto& [id, rp] : m_remotePlayers) {
        m_renderer->drawCube(rp.position + Vec3(0.0f, 1.0f, 0.0f),
                             {0.5f, 1.0f, 0.5f}, Color(0.3f, 0.9f, 0.3f));
        m_renderer->drawCube(rp.position + Vec3(0.0f, 1.7f, 0.0f),
                             {0.4f, 0.4f, 0.4f}, Color(0.95f, 0.85f, 0.6f));
    }

    // UI
    m_renderer->uiBegin();
    float cx = m_renderer->getUiWidth() * 0.5f;
    float cy = m_renderer->getUiHeight() * 0.5f;

    // Crosshair
    Color white(1,1,1,1);
    m_renderer->drawRect(cx - 8.0f, cy - 1.0f, 16.0f, 2.0f, white);
    m_renderer->drawRect(cx - 1.0f, cy - 8.0f, 2.0f, 16.0f, white);
    m_renderer->drawRect(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f, Color(1,1,1,0.4f));

    // FPS + position + online
    float fps = 1.0f / m_renderer->getDeltaTime();
    std::string fpsText = "FPS: " + std::to_string((int)fps) +
        " | " + std::to_string((int)m_player.transform.position.x) + "," +
        std::to_string((int)m_player.transform.position.y) + "," +
        std::to_string((int)m_player.transform.position.z) +
        " | " + std::to_string(m_remotePlayers.size()) + " players";
    m_renderer->drawText(fpsText, 10.0f, 10.0f, 1.2f, white);

    // Title
    m_renderer->drawText("ZAVIRO", cx - 40.0f, 10.0f, 1.4f, Color(1, 0.92f, 0.3f, 1));

    // ESC hint
    m_renderer->drawText("ESC - Voltar ao Lobby | WASD - Mover | SPACE - Pular", 10.0f, m_renderer->getUiHeight() - 22.0f, 0.9f, Color(1,1,1,0.6f));

    if (m_customUIEnabled) {
        renderCustomUI();
    }

    m_renderer->uiEnd();
    m_renderer->endFrame();
    m_window->swapBuffers();
}

void Engine::renderCustomUI() {
    float w = (float)m_renderer->getUiWidth();
    float h = (float)m_renderer->getUiHeight();
    for (const auto& e : m_uiElements) {
        float x = e.x > 0 && e.x <= 1 ? e.x * w : e.x;
        float y = e.y > 0 && e.y <= 1 ? e.y * h : e.y;
        float ew = e.w > 0 && e.w <= 1 ? e.w * w : e.w;
        float eh = e.h > 0 && e.h <= 1 ? e.h * h : e.h;
        if (e.type == "button") {
            m_renderer->drawRect(x, y, ew, eh, Color(e.r, e.g, e.b, e.a));
            m_renderer->drawText(e.text, x + 8.0f, y + eh*0.25f, e.fontSize, Color(1,1,1,1));
        } else if (e.type == "panel") {
            m_renderer->drawRect(x, y, ew, eh, Color(e.r, e.g, e.b, e.a));
        } else if (e.type == "text") {
            m_renderer->drawText(e.text, x, y, e.fontSize, Color(e.r, e.g, e.b, e.a));
        } else if (e.type == "image") {
            auto it = m_runtimeTextures.find(e.texture);
            if (it != m_runtimeTextures.end() && it->second) {
                m_renderer->drawTexturedRect(x, y, ew, eh, it->second, Color(1,1,1,1));
            }
        }
    }
}

void Engine::handleCustomUIClick(float x, float y) {
    float w = (float)m_renderer->getUiWidth();
    float h = (float)m_renderer->getUiHeight();
    bool hit = false;
    for (const auto& e : m_uiElements) {
        if (e.type != "button") continue;
        float ex = e.x > 0 && e.x <= 1 ? e.x * w : e.x;
        float ey = e.y > 0 && e.y <= 1 ? e.y * h : e.y;
        float ew = e.w > 0 && e.w <= 1 ? e.w * w : e.w;
        float eh = e.h > 0 && e.h <= 1 ? e.h * h : e.h;
        if (x >= ex && x <= ex + ew && y >= ey && y <= ey + eh) {
            json ev = {{"elementId", e.id}, {"value", true}};
            m_network.send(Proto::UI_EVENT, ev.dump());
            std::cout << "UI click: " << e.id << std::endl;
            hit = true;
            break;
        }
    }
    (void)hit;
}

// ============ UNIVERSE EDITOR ============

void Engine::renderEditor() {
    float w = (float)m_renderer->getUiWidth();
    float h = (float)m_renderer->getUiHeight();

    // Darken background
    m_renderer->drawRect(0, 0, w, h, Color(0, 0, 0, 0.55f));

    // Panel
    float pw = 620.0f, ph = 430.0f;
    float px = (w - pw) / 2.0f, py = (h - ph) / 2.0f;
    m_renderer->drawRect(px, py, pw, ph, Color(0.1f, 0.1f, 0.16f, 0.98f));

    m_renderer->drawText(m_editIsNew ? "Criar Universo" : "Editar Universo", px + 20.0f, py + 14.0f, 1.5f, Color(1, 0.92f, 0.3f, 1));

    // Name field
    float ly = py + 70.0f;
    m_renderer->drawText("Nome", px + 20.0f, ly, 1.0f, Color(0.8f, 0.8f, 0.9f, 1));
    m_renderer->drawRect(px + 20.0f, ly + 24.0f, pw - 40.0f, 40.0f,
        m_editState == 0 ? Color(0.25f, 0.25f, 0.4f, 1) : Color(0.16f, 0.16f, 0.25f, 1));
    m_renderer->drawText(m_editName.empty() ? "(digite o nome)" : m_editName, px + 28.0f, ly + 34.0f, 1.0f,
        m_editName.empty() ? Color(0.5f, 0.5f, 0.6f, 1) : Color(1, 1, 1, 1));

    // Description field
    ly += 90.0f;
    m_renderer->drawText("Descricao", px + 20.0f, ly, 1.0f, Color(0.8f, 0.8f, 0.9f, 1));
    m_renderer->drawRect(px + 20.0f, ly + 24.0f, pw - 40.0f, 40.0f,
        m_editState == 1 ? Color(0.25f, 0.25f, 0.4f, 1) : Color(0.16f, 0.16f, 0.25f, 1));
    m_renderer->drawText(m_editDesc.empty() ? "(digite a descricao)" : m_editDesc, px + 28.0f, ly + 34.0f, 1.0f,
        m_editDesc.empty() ? Color(0.5f, 0.5f, 0.6f, 1) : Color(1, 1, 1, 1));

    // Script field
    ly += 90.0f;
    m_renderer->drawText("Script do servidor (JavaScript)", px + 20.0f, ly, 1.0f, Color(0.8f, 0.8f, 0.9f, 1));
    m_renderer->drawRect(px + 20.0f, ly + 24.0f, pw - 40.0f, 86.0f,
        m_editState == 2 ? Color(0.25f, 0.25f, 0.4f, 1) : Color(0.16f, 0.16f, 0.25f, 1));
    // Preview first 3 lines of script
    std::string scriptPreview = m_editScript.empty()
        ? "(eventos: onInit, onJoin, onTick, onChat, onPlaceBlock, onUIEvent)"
        : m_editScript;
    m_renderer->drawText(scriptPreview.substr(0, 90), px + 26.0f, ly + 32.0f, 0.7f,
        m_editScript.empty() ? Color(0.5f, 0.5f, 0.6f, 1) : Color(1, 1, 1, 1));

    // Buttons
    float by = py + ph - 70.0f;
    m_renderer->drawRect(px + 20.0f, by, 180.0f, 46.0f, Color(0.15f, 0.6f, 0.3f, 1));
    m_renderer->drawText("Salvar", px + 65.0f, by + 13.0f, 1.2f, Color(1, 1, 1, 1));
    m_renderer->drawRect(px + pw - 200.0f, by, 180.0f, 46.0f, Color(0.5f, 0.16f, 0.16f, 1));
    m_renderer->drawText("Cancelar", px + pw - 155.0f, by + 13.0f, 1.2f, Color(1, 1, 1, 1));
}

void Engine::handleEditorClick(float mx, float my) {
    float w = (float)m_renderer->getUiWidth();
    float h = (float)m_renderer->getUiHeight();
    float pw = 620.0f, ph = 430.0f;
    float px = (w - pw) / 2.0f, py = (h - ph) / 2.0f;

    // Focus fields (click inside field box selects it)
    float ly = py + 70.0f;
    if (mx >= px + 20.0f && mx <= px + pw - 20.0f && my >= ly + 24.0f && my <= ly + 64.0f) { m_editState = 0; return; }
    float ly2 = ly + 90.0f;
    if (mx >= px + 20.0f && mx <= px + pw - 20.0f && my >= ly2 + 24.0f && my <= ly2 + 64.0f) { m_editState = 1; return; }
    float ly3 = ly2 + 90.0f;
    if (mx >= px + 20.0f && mx <= px + pw - 20.0f && my >= ly3 + 24.0f && my <= ly3 + 110.0f) { m_editState = 2; return; }

    // Save
    float by = py + ph - 70.0f;
    if (mx >= px + 20.0f && mx <= px + 200.0f && my >= by && my <= by + 46.0f) {
        if (m_editName.empty()) { std::cout << "Nome vazio" << std::endl; return; }
        json payload = {
            {"name", m_editName},
            {"description", m_editDesc},
            {"scriptServer", m_editScript}
        };
        if (m_editIsNew) {
            m_network.send(Proto::CREATE_UNIVERSE, payload.dump());
        } else {
            payload["universeId"] = 0; // placeholder; editing existing universe id TBD
            m_network.send(Proto::UPDATE_UNIVERSE, payload.dump());
        }
        std::cout << "Saving universe: " << m_editName << std::endl;
        m_showEditor = false;
        return;
    }

    // Cancel
    if (mx >= px + pw - 200.0f && mx <= px + pw - 20.0f && my >= by && my <= by + 46.0f) {
        m_showEditor = false;
        return;
    }
}

void Engine::handleEditorType(const std::string& typed) {
    std::string* target = nullptr;
    if (m_editState == 0) target = &m_editName;
    else if (m_editState == 1) target = &m_editDesc;
    else if (m_editState == 2) target = &m_editScript;
    if (!target) return;

    for (unsigned char c : typed) {
        if (c == '\n' || c == '\r') continue;
        if (c == 0) continue;
        // Skip control chars
        if (c < 32) continue;
        // Backspace is handled via key (not char). Add char:
        target->push_back((char)c);
    }
    // Backspace handling
    if (m_window->isKeyJustPressed(GLFW_KEY_BACKSPACE) && !target->empty()) {
        target->pop_back();
    }
}

void Engine::shutdown() {
    m_network.disconnect();

    if (m_renderer) m_renderer->shutdown();
    m_window.reset();
    m_renderer.reset();
    std::cout << "Zaviro Engine shutdown" << std::endl;
}
