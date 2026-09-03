#pragma once

#include <string>
#include <vector>

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }

    float length() const;
    Vec3 normalized() const;
};

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    Color() = default;
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
};

struct Transform {
    Vec3 position;
    Vec3 rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};
