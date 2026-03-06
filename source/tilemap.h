#pragma once
// ─── tilemap.h ─── Simple tile-based map ────────────────────────────────────
#include "constants.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <vector>

// ── Portable PRNG for deterministic map generation across platforms ──
// (stdlib rand() produces different sequences on different libc implementations,
//  e.g. glibc on PC vs newlib on Switch)
namespace MapRng {
    inline uint32_t& state() { static uint32_t s = 1; return s; }
    inline void seed(uint32_t v) { state() = v ? v : 1; }
    inline int next() {
        uint32_t x = state();
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state() = x;
        return (int)(x & 0x7FFFFFFF);
    }
};
inline void mapSrand(uint32_t s) { MapRng::seed(s); }
inline int  mapRand()            { return MapRng::next(); }

enum TileType : uint8_t {
    TILE_FLOOR = 0,
    TILE_GRASS,
    TILE_GRAVEL,
    TILE_WOOD,
    TILE_SAND,
    TILE_WALL,      // solid
    TILE_GLASS,     // solid, breakable
    TILE_DESK,      // solid
    TILE_BOX,       // solid, breakable by bullets
    TILE_COUNT
};

// Ceiling overlay layer — separate from ground tiles
enum CeilType : uint8_t {
    CEIL_NONE = 0,
    CEIL_GLASS,   // transparent glass roof
};

struct TileMap {
    int width = MAP_DEFAULT_W;
    int height = MAP_DEFAULT_H;
    std::vector<uint8_t> tiles;
    std::vector<uint8_t> ceiling; // overlay layer (CeilType)

    void generate(int mapWidth, int mapHeight); // procedural arena
    bool isSolid(int tx, int ty) const;
    bool isInBounds(int tx, int ty) const;
    bool worldCollides(float wx, float wy, float halfSize) const;
    uint8_t get(int tx, int ty) const;
    void set(int tx, int ty, uint8_t t);

    float worldWidth() const { return width * TILE_SIZE; }
    float worldHeight() const { return height * TILE_SIZE; }

    // Convert world <-> tile coords
    static int   toTile(float w)  { return (int)(w / TILE_SIZE); }
    static float toWorld(int t)   { return t * TILE_SIZE + TILE_SIZE / 2.0f; }

    // Spawn points (open areas for enemy spawning)
    std::vector<Vec2> spawnPoints;
    void findSpawnPoints();
    bool isSpawnSafe(int tx, int ty) const;
};
