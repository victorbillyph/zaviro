#include "game/player.h"
#include <cmath>

Player::Player(uint64_t id, const std::string& name)
    : m_id(id), m_name(name) {
    m_transform.scale = {1.0f, 1.0f, 1.0f};
    transform.scale = {1.0f, 1.0f, 1.0f};
}

void Player::update(float dt) {
    // No physics on client — server is authoritative for all position.
}

void Player::move(float forward, float right, float jump, float dt) {
    float yawRad = m_yaw * 3.14159f / 180.0f;
    float pitchRad = m_pitch * 3.14159f / 180.0f;

    Vec3 front{
        cosf(pitchRad) * cosf(yawRad),
        sinf(pitchRad),
        cosf(pitchRad) * sinf(yawRad)
    };
    front = front.normalized();

    Vec3 frontH{front.x, 0.0f, front.z};
    frontH = frontH.normalized();

    Vec3 rightVec = Vec3{
        cosf(yawRad - 3.14159f/2.0f),
        0.0f,
        sinf(yawRad - 3.14159f/2.0f)
    }.normalized();

    Vec3 wishDir = frontH * forward + rightVec * right;
    float wishLen = sqrtf(wishDir.x*wishDir.x + wishDir.z*wishDir.z);
    if (wishLen > 1.0f) {
        wishDir = wishDir * (1.0f / wishLen);
    }

    m_velocity.x = wishDir.x * speed;
    m_velocity.z = wishDir.z * speed;
    // No local movement — server computes position and sends it back
}

void Player::setMouseDelta(float dx, float dy) {
    m_yaw += dx * sensitivity;
    m_pitch -= dy * sensitivity;

    if (m_pitch > 89.0f) m_pitch = 89.0f;
    if (m_pitch < -89.0f) m_pitch = -89.0f;
}
