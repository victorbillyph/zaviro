#pragma once

#include "math/vec3.h"
#include <cstdint>
#include <string>
#include <unordered_map>

struct Entity {
    uint64_t id = 0;
    std::string name;
    Transform transform;
    Color color;
    bool isStatic = false;
};

class World {
public:
    World() = default;

    uint64_t createEntity(const std::string& name, const Vec3& pos, const Vec3& scale, const Color& color);
    void removeEntity(uint64_t id);
    Entity* getEntity(uint64_t id);

    void update(float dt);
    const std::unordered_map<uint64_t, Entity>& getEntities() const { return m_entities; }

    void loadDefaultWorld();

private:
    uint64_t m_nextId = 1;
    std::unordered_map<uint64_t, Entity> m_entities;
};
