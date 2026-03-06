#pragma once
// ─── camera.h ─── Camera state ─────────────────────────────────────────────
#include "vec2.h"
#include "constants.h"

struct Camera {
    Vec2  pos = {0, 0};          // top-left in world coords
    Vec2  vel = {0, 0};
    float shake      = 0;
    float shakeDecay = 5.5f;
    Vec2  shakeOffset= {0, 0};
    float worldW     = WORLD_W;
    float worldH     = WORLD_H;

    void update(Vec2 target, Vec2 aimDir, float dt);
    void addShake(float amount);
    Vec2 worldToScreen(Vec2 world) const;
    Vec2 screenToWorld(Vec2 screen) const;
};
