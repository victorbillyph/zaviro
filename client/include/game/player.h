#pragma once

#include "math/vec3.h"
#include <string>
#include <cstdint>
#include <functional>

class Player {
public:
    Player() = default;
    Player(uint64_t id, const std::string& name);

    void update(float dt);
    void move(float forward, float right, float jump, float dt);
    void setMouseDelta(float dx, float dy);

    const Transform& getTransform() const { return m_transform; }
    Transform& getTransform() { return m_transform; }
    uint64_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }

    float speed = 10.0f;
    float sensitivity = 0.1f;
    float gravity = -25.0f;
    float jumpVelocity = 9.0f;
    float height = 1.8f;

    // Height map lookup for ground collision.
    // Returns terrain height at (x, z). Called every frame.
    std::function<float(float, float)> heightAt = [](float, float) { return 0.0f; };

    bool isGrounded() const { return m_grounded; }
    Vec3 getVelocity() const { return m_velocity; }

    float getYaw() const { return m_yaw; }
    float getPitch() const { return m_pitch; }

    Transform transform;

private:
    uint64_t m_id = 0;
    std::string m_name;
    Transform m_transform;
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    Vec3 m_velocity;
    bool m_grounded = false;
};
