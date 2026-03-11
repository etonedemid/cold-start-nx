#pragma once
// ─── player.h ─── Player state ─────────────────────────────────────────────
#include "vec2.h"
#include "constants.h"
#include <SDL2/SDL.h>
#include <vector>

struct Player {
    Vec2  pos = {WORLD_W/2, WORLD_H/2};
    Vec2  vel = {0, 0};
    float rotation   = 0;    // body (aiming) angle rad
    float legRotation= 0;    // leg walking angle rad
    float speed      = PLAYER_SPEED;

    int   hp         = PLAYER_MAX_HP;
    int   maxHp      = PLAYER_MAX_HP;
    bool  invulnerable  = false;
    float invulnTimer   = 0;
    float invulnDuration = PLAYER_INVULN_TIME; // settable per-gamemode (PvP: 0.01f)

    // Gun
    int   ammo       = GUN_MAX_AMMO;
    int   maxAmmo    = GUN_MAX_AMMO;
    float fireRate   = GUN_FIRE_RATE;
    float reloadTime = GUN_RELOAD_TIME;
    bool  reloading  = false;
    float reloadTimer= 0;
    float fireCooldown= 0;

    // Parry
    bool  canParry     = true;
    bool  isParrying   = false;
    float parryTimer   = 0;
    float parryCdTimer = 0;
    Vec2  parryDir     = {0,0};

    // Animation
    int   animFrame    = 0;
    float animTimer    = 0;
    int   legAnimFrame = 0;
    float legAnimTimer = 0;
    bool  moving       = false;
    bool  hasFiredOnce = false;
    float shootAnimTimer = 0;

    // Active weapon: 0 = gun, 1 = axe
    int   activeWeapon   = 0;

    // Melee (axe swing)
    bool  isMeleeSwinging  = false;  // axe swing in progress
    bool  meleeHit         = false;  // damage already applied this swing
    bool  meleeSwingReverse = false; // true = next swing plays frames 9→3 (returning)
    bool  hadMeleeSwing    = false;  // hold melee idle pose after first swing
    float meleeTimer       = 0.0f;
    float meleeCooldown    = 0.0f;

    // Bombs
    int   killCounter  = 0;
    int   bombCount    = 0;

    bool  dead         = false;
    float deathTimer   = 0;

    void takeDamage(int dmg);
    void die();
};
