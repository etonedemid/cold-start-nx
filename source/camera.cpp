// ─── camera.cpp ─── Camera follow + screenshake ─────────────────────────────
#include "camera.h"
#include <cstdlib>
#include <cmath>

static float randf() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

void Camera::update(Vec2 target, Vec2 aimDir, float dt) {
    // Target is player pos offset toward aim direction
    Vec2 desired = target + aimDir * CAM_OFFSET;
    desired.x -= SCREEN_W / 2.0f;
    desired.y -= SCREEN_H / 2.0f;

    // Smooth damp
    pos = Vec2::lerp(pos, desired, dt / CAM_SMOOTH);

    // Clamp to world bounds (use dynamic world size)
    if (worldW > SCREEN_W) {
        if (pos.x < 0) pos.x = 0;
        if (pos.x > worldW - SCREEN_W) pos.x = worldW - SCREEN_W;
    } else {
        pos.x = -(SCREEN_W - worldW) / 2.0f;
    }
    if (worldH > SCREEN_H) {
        if (pos.y < 0) pos.y = 0;
        if (pos.y > worldH - SCREEN_H) pos.y = worldH - SCREEN_H;
    } else {
        pos.y = -(SCREEN_H - worldH) / 2.0f;
    }

    // Shake
    if (shake > 0.01f) {
        shakeOffset = {randf() * shake, randf() * shake};
        shake -= shake * shakeDecay * dt;
    } else {
        shake = 0;
        shakeOffset = {0,0};
    }
}

void Camera::addShake(float amount) {
    shake += amount;
    if (shake > 20.0f) shake = 20.0f;
}

Vec2 Camera::worldToScreen(Vec2 world) const {
    return {world.x - pos.x + shakeOffset.x,
            world.y - pos.y + shakeOffset.y};
}

Vec2 Camera::screenToWorld(Vec2 screen) const {
    return {screen.x + pos.x, screen.y + pos.y};
}
