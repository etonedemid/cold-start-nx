#pragma once
// Binary format:
// Header (CSM_HEADER)
// Tile data (width * height bytes)
// Ceiling data (width * height bytes)
// Trigger count (uint16_t)
// Trigger array (count * MapTrigger)
// Enemy spawn count (uint16_t)
// Enemy spawn array (count * EnemySpawn)
// Custom tile paths (null-separated strings) - for editor tile references
#include "vec2.h"
#include <cstdint>
#include <string>
#include <vector>

constexpr uint32_t CSM_MAGIC   = 0x4D534343; // "CCSM"
constexpr uint16_t CSM_VERSION = 2;

// Trigger types
enum class TriggerType : uint8_t {
    LevelStart       = 0, // player spawn point (solo / PvE)
    LevelEnd         = 1, // goal (configurable unlock condition)
    Crate            = 2, // breakable crate with optional loot
    Effect           = 3, // visual/audio effect zone
    // Team spawn points (PvP / team modes - team index 0-3)
    TeamSpawnRed     = 10, // team 0 spawn
    TeamSpawnBlue    = 11, // team 1 spawn
    TeamSpawnGreen   = 12, // team 2 spawn
    TeamSpawnYellow  = 13, // team 3 spawn
    LayerFade        = 14, // zone where top image layer fades when player is inside
    CollisionZone    = 15, // invisible solid rectangle (can be rotated); blocks player + enemies
    Cutscene         = 16, // triggers a cutscene; param = cutscene index in the map's .csc library
    COUNT            = 17,
};

// End-goal unlock condition
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
    uint8_t reserved[2];     // [0|1] = rotation in tenths-of-a-degree (uint16_t, LE) for CollisionZone
};

// Rotation helpers for CollisionZone triggers (stored in reserved[0..1])
inline float triggerGetAngle(const MapTrigger& t) {
    uint16_t deg10 = (uint16_t)(t.reserved[0] | (t.reserved[1] << 8));
    return (float)deg10 * 0.1f * (3.14159265f / 180.0f);
}
inline void triggerSetAngleDeg(MapTrigger& t, float deg) {
    // normalise to [0, 360)
    while (deg < 0.0f) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    uint16_t deg10 = (uint16_t)(deg * 10.0f + 0.5f);
    t.reserved[0] = deg10 & 0xFF;
    t.reserved[1] = (deg10 >> 8) & 0xFF;
}

struct EnemySpawn {
    float x, y;          // tile position (converted to world on load)
    uint8_t enemyType;   // 0=Melee, 1=Shooter, 2=Crate, 3=Upgrade, 4=Brute, 5=Scout, 6=Sniper, 7=Gunner
    uint8_t waveGroup;   // which wave this enemy belongs to (0 = immediate)
    uint8_t reserved[2];
};

// Free-placed prop (not snapped to tile grid)
struct PropSpawn {
    float x, y;         // world position (center)
    uint8_t tileType;   // which prop sprite (same tile type IDs as tiles array)
    uint8_t rotation;   // 0-3 (x90 degrees)
    uint8_t reserved[2];
};

// File header
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

// Per-map player restrictions (stored in header reserved bytes [1..5])
struct MapPlayerConfig {
    bool    enabled    = false;   // false = apply no restrictions (backward compat)
    bool    hasGun     = true;
    bool    hasMelee   = true;
    bool    hasBombs   = true;
    bool    hasParry   = true;
    bool    hasPickups = true;
    uint8_t maxHp      = 0;    // 0 = use global game config
    uint8_t startBombs = 1;    // starting bomb count 0-9
    uint8_t speedPct   = 100;  // movement speed percent 50-150
    uint8_t damagePct  = 100;  // damage percent 50-150
};

// Map data container (loaded from .csm)
struct CustomMap {
    CSM_Header header;
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> tiles;
    std::vector<uint8_t> ceiling;
    std::vector<uint8_t> tileRotations;  // per-tile rotation 0-3 (x90°), same size as tiles
    std::vector<uint8_t> tileNoCollide;  // 1 = tile has no square collision, same size as tiles
    std::vector<MapTrigger> triggers;
    std::vector<EnemySpawn> enemySpawns;
    std::vector<PropSpawn>  props;      // free-placed props (world coords)
    std::string name;
    std::string creator;
    uint8_t gameMode = 0;  // 0=Arena, 1=Sandbox
    MapPlayerConfig playerConfig;
    std::string musicPath;   // optional music filename relative to map folder (empty = default)
    std::string bgImagePath;  // full-map background image (1:1 world scale, empty = tile-rendered)
    std::string topImagePath; // full-map top-layer image rendered above entities (empty = none)
    // Texture paths for TILE_CUSTOM_0..7 (empty = not used)
    std::string customTilePaths[8];

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    
    // Find specific triggers
    MapTrigger* findStartTrigger();
    MapTrigger* findEndTrigger();
    MapTrigger* findTeamSpawnTrigger(int team); // team 0-3 -> TeamSpawnRed/Blue/Green/Yellow
    std::vector<MapTrigger*> findTriggersByType(TriggerType type);
};
