#pragma once
// ─── constants.h ─── Game tuning constants ──────────────────────────────────
#include "vec2.h"

// ── Version ──
constexpr const char* GAME_VERSION = "6.1";

// Screen
constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;

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

// Shooter enemy
constexpr float SHOOTER_SPEED       = 140.0f;
constexpr float SHOOTER_SHOOT_CD    = 1.0f;
constexpr float SHOOTER_HP          = 3.0f;
constexpr float SHOOTER_SIZE        = 64.0f;
constexpr float SHOOTER_RENDER_SCALE= 4.5f;

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
constexpr float PARRY_DASH_SPEED    = 700.0f;
constexpr float PARRY_DASH_DURATION = 0.13f;
constexpr float PARRY_DMG      = 4.0f;

// Spawning — wave system
constexpr float WAVE_PAUSE_BASE     = 10.0f; // pause between waves (scales down over time)
constexpr int   WAVE_SIZE_BASE      = 2;     // enemies per wave at start
constexpr int   WAVE_SIZE_GROWTH    = 1;     // extra enemies per wave number
constexpr int   WAVE_MAX_SIZE       = 15;    // cap per wave
constexpr float WAVE_SPAWN_INTERVAL = 0.6f;  // delay between each enemy in a wave
constexpr float WAVE_SHOOTER_CHANCE = 0.25f; // 25% shooter

// Camera
constexpr float CAM_SMOOTH  = 0.18f;
constexpr float CAM_OFFSET  = 75.0f;

// Wander
constexpr float WANDER_RADIUS = 300.0f;
constexpr float WANDER_INTERVAL = 2.0f;

// Lose chase delay
constexpr float LOSE_PLAYER_DELAY = 1.2f;
