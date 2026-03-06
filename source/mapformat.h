#pragma once
// ─── mapformat.h ─── .csm (Cold Start Map) file format ─────────────────────
// Binary format:
//   Header (CSM_HEADER)
//   Tile data (width * height bytes)
//   Ceiling data (width * height bytes)
//   Trigger count (uint16_t)
//   Trigger array (count * MapTrigger)
//   Enemy spawn count (uint16_t)
//   Enemy spawn array (count * EnemySpawn)
//   Custom tile paths (null-separated strings) — for editor tile references
// ─────────────────────────────────────────────────────────────────────────────
#include "vec2.h"
#include <cstdint>
#include <string>
#include <vector>

constexpr uint32_t CSM_MAGIC   = 0x4D534343; // "CCSM"
constexpr uint16_t CSM_VERSION = 1;

// ── Trigger types ──
enum class TriggerType : uint8_t {
    LevelStart       = 0, // player spawn point (solo / PvE)
    LevelEnd         = 1, // goal (configurable unlock condition)
    Crate            = 2, // breakable crate with optional loot
    Effect           = 3, // visual/audio effect zone
    // Team spawn points (PvP / team modes — team index 0–3)
    TeamSpawnRed     = 10, // team 0 spawn
    TeamSpawnBlue    = 11, // team 1 spawn
    TeamSpawnGreen   = 12, // team 2 spawn
    TeamSpawnYellow  = 13, // team 3 spawn
    COUNT            = 14,
};

// ── End-goal unlock condition ──
enum class GoalCondition : uint8_t {
    DefeatAll   = 0, // kill all spawned enemies
    OnTrigger   = 1, // activated by stepping on a specific trigger
    Immediate   = 2, // always open
};

struct MapTrigger {
    TriggerType type;
    float x, y;              // world position
    float width, height;     // trigger area size
    GoalCondition condition;  // only used for LevelEnd
    uint8_t param;            // generic parameter (effect ID, linked trigger, etc.)
    uint8_t reserved[2];
};

struct EnemySpawn {
    float x, y;          // tile position (converted to world on load)
    uint8_t enemyType;   // 0=Melee, 1=Shooter
    uint8_t waveGroup;   // which wave this enemy belongs to (0 = immediate)
    uint8_t reserved[2];
};

// ── File header ──
struct CSM_Header {
    uint32_t magic;       // CSM_MAGIC
    uint16_t version;     // CSM_VERSION
    uint16_t width;       // map width in tiles
    uint16_t height;      // map height in tiles
    char     name[64];    // map name (null-terminated)
    char     creator[32]; // creator name (null-terminated)
    uint8_t  thumbnail[128 * 72 * 3]; // 128x72 RGB thumbnail (vertical preview)
    uint8_t  reserved[32];
};

// ── Map data container (loaded from .csm) ──
struct CustomMap {
    CSM_Header header;
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> tiles;
    std::vector<uint8_t> ceiling;
    std::vector<MapTrigger> triggers;
    std::vector<EnemySpawn> enemySpawns;
    std::string name;
    std::string creator;
    uint8_t gameMode = 0;  // 0=Arena, 1=Sandbox
    // Texture paths for TILE_CUSTOM_0..7 (empty = not used)
    std::string customTilePaths[8];

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    
    // Find specific triggers
    MapTrigger* findStartTrigger();
    MapTrigger* findEndTrigger();
    MapTrigger* findTeamSpawnTrigger(int team); // team 0-3 → TeamSpawnRed/Blue/Green/Yellow
    std::vector<MapTrigger*> findTriggersByType(TriggerType type);
};
