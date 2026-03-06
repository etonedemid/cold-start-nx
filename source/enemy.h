#pragma once
// ─── enemy.h ─── Enemy types ───────────────────────────────────────────────
#include "vec2.h"
#include "constants.h"
#include <SDL2/SDL.h>

enum class EnemyType { Melee, Shooter };
enum class EnemyState { Wander, Chase };

struct Enemy {
    Vec2  pos;
    Vec2  vel         = {0,0};
    float rotation    = 0;
    float size        = ENEMY_SIZE;
    float hp          = ENEMY_HP;
    float maxHp       = ENEMY_HP;
    float speed       = ENEMY_SPEED;
    bool  alive       = true;

    EnemyType  type   = EnemyType::Melee;
    EnemyState state  = EnemyState::Wander;

    // Vision
    float lastSeenTime = -999;
    bool  canSeePlayer = false;

    // Target tracking (multiplayer)
    uint8_t targetPlayerId = 255;   // 255 = unset, will pick first seen / closest
    float   idleTimer      = 0;     // time spent wandering; after 30s retarget to nearest player

    // Wander
    Vec2  wanderTarget;
    float nextWanderTime = 0;

    // Dash attack (melee)
    bool  isDashing    = false;
    bool  dashOnCd     = false;
    float dashTimer    = 0;
    float dashCdTimer  = 0;
    Vec2  dashDir      = {0,0};
    float dashDelayTimer = 0;
    bool  dashCharging = false;
    float flashTimer   = 0;  // visual red flash

    // Shooter
    float shootCooldown = 0;

    // Stun
    float stunTimer    = 0;

    // Sprite color tint for damage
    float damageFlash  = 0;

    // Client-side interpolation
    Vec2  netTargetPos;             // server-authoritative position; client lerps toward this
    bool  netIsDashing    = false;  // replicated from host for trail rendering
    bool  netDashCharging = false;  // replicated from host for charge flash
};
