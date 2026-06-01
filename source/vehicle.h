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

    // Arcade driving model
    static constexpr float ACCEL           = 900.0f;  // px/s² forward thrust
    static constexpr float REV_ACCEL       = 420.0f;  // px/s² reverse thrust (slower)
    static constexpr float BRAKE           = 1800.0f; // px/s² braking deceleration
    static constexpr float COAST_DRAG      = 160.0f;  // px/s² coast drag
    static constexpr float MAX_FWD_SPD     = 780.0f;  // max forward speed (px/s)
    static constexpr float MAX_REV_SPD     = 280.0f;  // max reverse speed (px/s)
    static constexpr float STEER_RATE      = 2.5f;    // steering angular rate (rad/s)
    static constexpr float GRIP            = 8.0f;    // lateral grip rate (higher = less drift)
    static constexpr float RUNOVER_SPD     = 180.0f;  // min speed to hurt enemies on contact
    static constexpr float RUNOVER_DMG     = 40.0f;   // damage dealt to enemies when run over
    static constexpr float ENTER_RADIUS    = 90.0f;   // max distance to enter/exit
};
