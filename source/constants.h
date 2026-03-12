#pragma once
// ─── constants.h ─── Game tuning constants ──────────────────────────────────
#include "vec2.h"

// ── Version ──
constexpr const char* GAME_VERSION = "1.1.0";

// Screen (window resolution - runtime configurable)
inline int SCREEN_W = 1280;
inline int SCREEN_H = 720;

// Base game viewport (always rendered at this resolution, scaled to window)
constexpr int BASE_VIEWPORT_W = 1280;
constexpr int BASE_VIEWPORT_H = 720;

// World / tile
constexpr int   TILE_SIZE   = 64;
constexpr int   MAP_DEFAULT_W = 50;   // tiles
constexpr int   MAP_DEFAULT_H = 50;
constexpr float WORLD_W       = MAP_DEFAULT_W * TILE_SIZE;
constexpr float WORLD_H       = MAP_DEFAULT_H * TILE_SIZE;

// Player
constexpr float PLAYER_SPEED        = 520.0f;
constexpr float PLAYER_BOOSTED_SPEED= 560.0f;
constexpr float PLAYER_SMOOTHING    = 10.0f;
constexpr int   PLAYER_MAX_HP       = 10;
constexpr float PLAYER_INVULN_TIME  = 0.7f;
constexpr float PLAYER_SIZE         = 48.0f;
constexpr float GUN_OFFSET_RIGHT    = 12.0f; // perpendicular offset to the right of facing

// Gun
constexpr int   GUN_MAX_AMMO   = 10;
constexpr float GUN_FIRE_RATE  = 10.0f;  // shots/sec
constexpr float GUN_RELOAD_TIME= 1.0f;

// Bullet
constexpr float BULLET_SPEED   = 1200.0f;
constexpr float BULLET_LIFETIME= 1.2f;
constexpr float BULLET_SIZE    = 8.0f;

// Enemy bullet
constexpr float ENEMY_BULLET_SPEED   = 550.0f;
constexpr float ENEMY_BULLET_LIFETIME= 3.0f;

// Melee enemy
constexpr float ENEMY_SPEED      = 220.0f;
constexpr float ENEMY_VISION_DIST= 700.0f;
constexpr float ENEMY_VISION_ANGLE= 120.0f;
constexpr float ENEMY_HP         = 3.0f;
constexpr float ENEMY_SIZE       = 48.0f;
constexpr float ENEMY_DASH_DIST  = 180.0f;
constexpr float ENEMY_DASH_FORCE = 900.0f;
constexpr float ENEMY_DASH_DELAY = 0.3f;
constexpr float ENEMY_DASH_DUR   = 0.2f;
constexpr float ENEMY_DASH_CD    = 1.4f;
constexpr int   ENEMY_DASH_DMG   = 2;
constexpr float MELEE_INERTIA    = 14.0f; // velocity smoothing factor (higher = more responsive)
constexpr float SHOOTER_INERTIA  = 8.0f;  // velocity smoothing for shooter enemies

// Shooter enemy
constexpr float SHOOTER_SPEED       = 140.0f;
constexpr float SHOOTER_SHOOT_CD    = 1.0f;
constexpr float SHOOTER_HP          = 3.0f;
constexpr float SHOOTER_SIZE        = 64.0f;
constexpr float SHOOTER_RENDER_SCALE= 4.5f;

// Enemy variants
constexpr float BRUTE_SPEED         = 160.0f;
constexpr float BRUTE_HP            = 7.0f;
constexpr float BRUTE_SIZE          = 64.0f;
constexpr float BRUTE_DASH_DIST     = 220.0f;
constexpr float BRUTE_DASH_FORCE    = 980.0f;
constexpr float BRUTE_DASH_DELAY    = 0.40f;
constexpr float BRUTE_DASH_DUR      = 0.28f;
constexpr float BRUTE_DASH_CD       = 1.80f;
constexpr int   BRUTE_DASH_DMG      = 3;
constexpr float BRUTE_RENDER_SCALE  = 4.2f;

constexpr float SCOUT_SPEED         = 300.0f;
constexpr float SCOUT_HP            = 2.0f;
constexpr float SCOUT_SIZE          = 40.0f;
constexpr float SCOUT_DASH_DIST     = 210.0f;
constexpr float SCOUT_DASH_FORCE    = 1050.0f;
constexpr float SCOUT_DASH_DELAY    = 0.18f;
constexpr float SCOUT_DASH_DUR      = 0.15f;
constexpr float SCOUT_DASH_CD       = 0.90f;
constexpr int   SCOUT_DASH_DMG      = 1;
constexpr float SCOUT_RENDER_SCALE  = 2.6f;

constexpr float SNIPER_SPEED        = 110.0f;
constexpr float SNIPER_HP           = 3.0f;
constexpr float SNIPER_SIZE         = 60.0f;
constexpr float SNIPER_SHOOT_CD     = 1.65f;
constexpr float SNIPER_RENDER_SCALE = 4.8f;

constexpr float GUNNER_SPEED        = 155.0f;
constexpr float GUNNER_HP           = 4.0f;
constexpr float GUNNER_SIZE         = 62.0f;
constexpr float GUNNER_SHOOT_CD     = 1.25f;
constexpr float GUNNER_BURST_GAP    = 0.11f;
constexpr float GUNNER_RENDER_SCALE = 4.6f;

// Bomb
constexpr float BOMB_ORBIT_SPEED  = 80.0f;
constexpr float BOMB_DASH_SPEED   = 1000.0f;
constexpr float BOMB_SIZE         = 32.0f;
constexpr float EXPLOSION_RADIUS  = 200.0f;
constexpr float EXPLOSION_DAMAGE  = 100.0f;
constexpr float EXPLOSION_DURATION= 1.1f;
constexpr int   KILLS_PER_BOMB    = 5;
constexpr int   MAX_BOMBS         = 5;

// Parry
constexpr float PARRY_WINDOW   = 0.25f;
constexpr float PARRY_COOLDOWN = 1.0f;
constexpr float PARRY_DASH_SPEED    = 1050.0f;  // Match scout dash force
constexpr float PARRY_DASH_DURATION = 0.15f;    // Match scout dash duration
constexpr float PARRY_DMG      = 4.0f;

// Melee (player axe swing)
constexpr float MELEE_DURATION      = 0.30f;  // swing animation duration
constexpr float MELEE_COOLDOWN_TIME = 0.45f;  // cooldown before next swing
constexpr float MELEE_RANGE         = 70.0f;  // reach from player centre
constexpr float MELEE_ARC           = 1.65f;  // half-arc in radians (~95 deg)
constexpr int   MELEE_PLAYER_DAMAGE = 3;      // damage to players (PvP / co-op)
constexpr int   MELEE_ANIM_FIRST    = 3;      // first body frame (sprite 0004)
constexpr int   MELEE_ANIM_LAST     = 9;      // last  body frame (sprite 0010)

// Spawning — wave system
constexpr float WAVE_PAUSE_BASE     = 10.0f; // pause between waves (scales down over time)
constexpr int   WAVE_SIZE_BASE      = 2;     // enemies per wave at start
constexpr int   WAVE_SIZE_GROWTH    = 1;     // extra enemies per wave number
constexpr int   WAVE_MAX_SIZE       = 15;    // cap per wave
constexpr float WAVE_SPAWN_INTERVAL = 0.6f;  // delay between each enemy in a wave
constexpr int   WAVE_MELEE_WEIGHT   = 42;
constexpr int   WAVE_SHOOTER_WEIGHT = 22;
constexpr int   WAVE_BRUTE_WEIGHT   = 10;
constexpr int   WAVE_SCOUT_WEIGHT   = 12;
constexpr int   WAVE_SNIPER_WEIGHT  = 7;
constexpr int   WAVE_GUNNER_WEIGHT  = 7;

// Camera
constexpr float CAM_SMOOTH  = 0.18f;
constexpr float CAM_OFFSET  = 75.0f;

// Wander
constexpr float WANDER_RADIUS = 300.0f;
constexpr float WANDER_INTERVAL = 2.0f;

// Lose chase delay
constexpr float LOSE_PLAYER_DELAY = 1.2f;
