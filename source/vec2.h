#pragma once
// ─── vec2.h ───  Minimal 2D vector math ────────────────────────────────────
#include <cmath>

struct Vec2 {
    float x = 0, y = 0;

    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(Vec2 o)  const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o)  const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s){ x *= s;   y *= s;   return *this; }

    float length()   const { return sqrtf(x*x + y*y); }
    float lengthSq() const { return x*x + y*y; }
    float dot(Vec2 o) const { return x*o.x + y*o.y; }

    Vec2 normalized() const {
        float l = length();
        return (l > 0.0001f) ? Vec2{x/l, y/l} : Vec2{0,0};
    }

    static Vec2 lerp(Vec2 a, Vec2 b, float t) {
        return a + (b - a) * t;
    }

    float angle() const { return atan2f(y, x); }

    static Vec2 fromAngle(float rad) {
        return {cosf(rad), sinf(rad)};
    }

    static float dist(Vec2 a, Vec2 b) { return (a-b).length(); }
};
