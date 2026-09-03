#include "game/world.h"
#include <cmath>

float Vec3::length() const {
    return sqrtf(x*x + y*y + z*z);
}

Vec3 Vec3::normalized() const {
    float l = length();
    if (l < 0.0001f) return {0, 0, 0};
    return {x/l, y/l, z/l};
}

uint64_t World::createEntity(const std::string& name, const Vec3& pos, const Vec3& scale, const Color& color) {
    Entity e;
    e.id = m_nextId++;
    e.name = name;
    e.transform.position = pos;
    e.transform.scale = scale;
    e.color = color;
    m_entities[e.id] = e;
    return e.id;
}

void World::removeEntity(uint64_t id) {
    m_entities.erase(id);
}

Entity* World::getEntity(uint64_t id) {
    auto it = m_entities.find(id);
    return it != m_entities.end() ? &it->second : nullptr;
}

void World::update(float dt) {
    // Physics and entity updates will go here
}

void World::loadDefaultWorld() {
    // Terrain is now procedural (renderer). Only add decorative blocks.

    createEntity("Block1", {2.0f, 1.0f, 2.0f}, {1.0f, 1.0f, 1.0f}, {0.9f, 0.2f, 0.2f});
    createEntity("Block2", {-2.0f, 1.0f, -2.0f}, {1.0f, 1.0f, 1.0f}, {0.2f, 0.2f, 0.9f});
    createEntity("Block3", {0.0f, 2.0f, -3.0f}, {2.0f, 1.0f, 1.0f}, {0.9f, 0.9f, 0.2f});
    createEntity("Block4", {3.0f, 1.5f, 0.0f}, {1.0f, 2.0f, 1.0f}, {0.2f, 0.9f, 0.9f});
    createEntity("Block5", {-3.0f, 3.0f, 3.0f}, {1.0f, 1.0f, 1.0f}, {0.9f, 0.5f, 0.9f});
}
