#pragma once
#include "vec2.h"
#include <SDL2/SDL.h>
#include <cstdint>

enum class VehicleType : uint8_t {
    Car = 0,
    COUNT
};

struct Vehicle {
    Vec2  pos      = {0, 0};
    Vec2  vel      = {0, 0};   // world velocity (pixels/sec)
    float rotation = 0.0f;     // facing angle, radians (0 = east, same as Player)

    VehicleType type = VehicleType::Car;
    bool  alive      = true;
    float hp         = 200.0f;
    float maxHp      = 200.0f;

    int   occupantSlot = -1;        // -1 = empty, 0 = main player, 1-3 = co-op

    SDL_Texture* sprite = nullptr;
    int   spriteW = 0, spriteH = 0;
    float size    = 64.0f;          // collision half-extent (radius for overlap checks)

    // Arcade driving model — tuned for fun drift + satisfying momentum
    static constexpr float ACCEL           = 1050.0f; // px/s² forward thrust (snappier)
    static constexpr float REV_ACCEL       = 480.0f;  // px/s² reverse thrust
    static constexpr float BRAKE           = 2000.0f; // px/s² braking deceleration
    static constexpr float COAST_DRAG      = 90.0f;   // px/s² coast drag (momentum carries)
    static constexpr float MAX_FWD_SPD     = 950.0f;  // max forward speed (px/s) — arcade rush
    static constexpr float MAX_REV_SPD     = 320.0f;  // max reverse speed (px/s)
    static constexpr float STEER_RATE      = 2.8f;    // steering angular rate (rad/s) — responsive
    static constexpr float GRIP            = 4.5f;    // lateral grip rate — lower = controlled drift
    static constexpr float RUNOVER_SPD     = 180.0f;  // min speed to hurt enemies on contact
    static constexpr float RUNOVER_DMG     = 40.0f;   // damage dealt to enemies when run over
    static constexpr float ENTER_RADIUS    = 90.0f;   // max distance to enter/exit

    // Wall destruction — car smashes through walls/glass at high speed;
    // boxes break on any contact.
    static constexpr float WALL_CRUSH_SPD     = 180.0f; // px/s minimum speed to destroy a wall/glass tile
    static constexpr float WALL_CRUSH_REBOUND = 0.65f;  // velocity retained after smashing (energy loss)
};
