#include "game/player.h"
#include <cmath>

Player::Player(uint64_t id, const std::string& name)
    : m_id(id), m_name(name) {
    m_transform.scale = {1.0f, 1.0f, 1.0f};
    transform.scale = {1.0f, 1.0f, 1.0f};
}

void Player::update(float dt) {
    // Gravity + ground collision is handled by the server.
    // Client only applies horizontal movement for immediate response.
    // Vertical position is reconciled via server WORLD_UPDATE.

    // Still apply ground clamp locally to prevent falling through terrain
    // while waiting for server position update.
    float groundY = heightAt(transform.position.x, transform.position.z) + height;
    if (transform.position.y < groundY) {
        transform.position.y = groundY;
        m_velocity.y = 0.0f;
        m_grounded = true;
    } else {
        m_grounded = false;
    }
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

    // Movement direction projected onto horizontal plane
    Vec3 frontH{front.x, 0.0f, front.z};
    frontH = frontH.normalized();

    Vec3 rightVec = Vec3{
        cosf(yawRad - 3.14159f/2.0f),
        0.0f,
        sinf(yawRad - 3.14159f/2.0f)
    }.normalized();

    // Store input for sending to server (server is authoritative for position)
    Vec3 wishDir = frontH * forward + rightVec * right;
    float wishLen = sqrtf(wishDir.x*wishDir.x + wishDir.z*wishDir.z);
    if (wishLen > 1.0f) {
        wishDir = wishDir * (1.0f / wishLen);
    }

    m_velocity.x = wishDir.x * speed;
    m_velocity.z = wishDir.z * speed;

    // Jump
    if (jump > 0.0f && m_grounded) {
        m_velocity.y = jumpVelocity;
        m_grounded = false;
    }
    // NOTE: position.x/z/y is NOT updated here.
    // Server computes position from input and sends it back via WORLD_UPDATE.
    // Client only reconciles vertical via ground clamp in update().
}

void Player::setMouseDelta(float dx, float dy) {
    m_yaw += dx * sensitivity;
    m_pitch -= dy * sensitivity;

    if (m_pitch > 89.0f) m_pitch = 89.0f;
    if (m_pitch < -89.0f) m_pitch = -89.0f;
}
