#include "engine/engine.h"
#include "engine/renderer.h"
#include "engine/window.h"
#include "network/protocol.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>

using nlohmann::json;

#define KEY_W 87
#define KEY_A 65
#define KEY_S 83
#define KEY_D 68
#define KEY_SPACE 32
#define KEY_ESCAPE 256
#define MOUSE_LEFT 0

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
    std::cout << "Zaviro Engine initialized" << std::endl;
    return true;
}

void Engine::onNetworkMessage(const NetworkMessage& msg) {
    json j;
    try {
        j = json::parse(msg.data.empty() ? "{}" : msg.data);
    } catch (...) { return; }

    switch (msg.type) {
        case Proto::WELCOME:
            m_clientId = j.value("clientId", 0);
            break;

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
                Vec3 serverPos(sx, sy, sz);
                Vec3 diff = serverPos - m_player.transform.position;
                m_player.transform.position = m_player.transform.position + diff * 0.2f;
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
    }
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
    // Mouse position for hover detection
    double mx, my;
    m_window->getMousePosition(mx, my);
    m_mouseX = (float)mx;
    m_mouseY = (float)my;

    // Handle click on universe card
    if (m_window->isMouseJustPressed(MOUSE_LEFT)) {
        handleLobbyClick(m_mouseX, m_mouseY);
    }

    renderLobby();
}

void Engine::handleLobbyClick(float mx, float my) {
    float w = (float)m_renderer->getUiWidth();
    float h = (float)m_renderer->getUiHeight();

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

    // Bottom hints
    if (m_online) {
        m_renderer->drawText("Conectado ao servidor | Clique em um Universo para jogar", 10.0f, h - 22.0f, 0.9f, Color(0.4f, 0.8f, 0.4f, 0.8f));
    } else {
        m_renderer->drawText("Offline - Servidor indisponivel", 10.0f, h - 22.0f, 0.9f, Color(0.8f, 0.3f, 0.3f, 0.8f));
    }

    // Loading indicator
    if (m_gameState == GameState::LOADING) {
        m_renderer->drawRect(w/2.0f - 100.0f, h/2.0f - 20.0f, 200.0f, 40.0f, Color(0.1f, 0.1f, 0.2f, 0.9f));
        m_renderer->drawText("Carregando...", w/2.0f - 55.0f, h/2.0f - 5.0f, 1.2f, Color(1,1,1,1));
    }

    m_renderer->uiEnd();
    m_renderer->endFrame();
    m_window->swapBuffers();
}

// ============ GAME ============

void Engine::runGame() {
    double lastMouseX = 0, lastMouseY = 0;
    if (m_firstMouse) {
        m_window->getMousePosition(lastMouseX, lastMouseY);
        m_firstMouse = false;
    }

    float dt = m_renderer->getDeltaTime();
    if (dt <= 0.0f) dt = 0.016f;

    // Mouse look
    double mouseX, mouseY;
    m_window->getMousePosition(mouseX, mouseY);
    float dx = (float)(mouseX - lastMouseX);
    float dy = (float)(mouseY - lastMouseY);
    lastMouseX = mouseX;
    lastMouseY = mouseY;

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

    // ESC to go back to lobby
    if (m_window->isKeyPressed(KEY_ESCAPE)) {
        if (m_window->isCursorDisabled()) {
            m_window->setCursorDisabled(false);
            m_gameState = GameState::LOBBY;
            m_firstMouse = true;
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

    m_renderer->uiEnd();
    m_renderer->endFrame();
    m_window->swapBuffers();
}

void Engine::shutdown() {
    m_network.disconnect();
    if (m_renderer) m_renderer->shutdown();
    m_window.reset();
    m_renderer.reset();
    std::cout << "Zaviro Engine shutdown" << std::endl;
}
