#include "camera.h"
#include <cstdlib>

static float randf() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

void Camera::update(Vec2 target, Vec2 aimDir, float dt, float lookScale) {
    Vec2 desired = target + aimDir * (CAM_OFFSET * lookScale);
    desired.x -= viewW / 2.0f;
    desired.y -= viewH / 2.0f;

    pos = Vec2::lerp(pos, desired, dt / CAM_SMOOTH);

    // Clamp to world bounds
    if (worldW > viewW) {
        if (pos.x < 0) pos.x = 0;
        if (pos.x > worldW - viewW) pos.x = worldW - viewW;
    } else {
        pos.x = -(viewW - worldW) / 2.0f;
    }
    if (worldH > viewH) {
        if (pos.y < 0) pos.y = 0;
        if (pos.y > worldH - viewH) pos.y = worldH - viewH;
    } else {
        pos.y = -(viewH - worldH) / 2.0f;
    }

    // Shake
    if (shake > 0.01f) {
        float s = shake * shakeScale;
        shakeOffset = {randf() * s, randf() * s};
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
