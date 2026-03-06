#pragma once
// ─── bomb.h ─── Bomb entity ────────────────────────────────────────────────
#include "vec2.h"
#include "constants.h"

struct Bomb {
    Vec2  pos;
    Vec2  vel         = {0,0};
    float orbitAngle  = 0;          // current angle around player
    float orbitRadius = 60.0f;      // distance from player
    float orbitSpeed  = BOMB_ORBIT_SPEED;
    float dashSpeed   = BOMB_DASH_SPEED;
    bool    hasDashed   = false;
    bool    alive       = true;
    float   lifetime    = 10.0f;
    float   age         = 0;
    uint8_t ownerId     = 255;     // player who owns/launched this bomb (255 = local/unowned)
    int     homingTarget   = -1;   // index into enemies_ for homing, -1 = none
    uint8_t homingPlayerId = 255;  // remote player ID to home toward (PvP), 255 = none
    float   homingStr      = 0.0f;

    // Animation
    int   animFrame   = 0;
    float animTimer   = 0;

    void activate(Vec2 direction);
};

struct Explosion {
    Vec2    pos;
    float   radius    = EXPLOSION_RADIUS;
    float   damage    = EXPLOSION_DAMAGE;
    float   duration  = EXPLOSION_DURATION;
    float   timer     = 0;
    bool    alive     = true;
    bool    dealtDmg  = false;  // apply damage only once
    uint8_t ownerId   = 255;    // player who triggered the explosion (255 = unowned/AI)
};
