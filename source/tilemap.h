#pragma once
#include "constants.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <vector>
#include <unordered_set>

// Portable PRNG for deterministic map generation across platforms
// (stdlib rand() produces different sequences on different libc implementations,
// e.g. glibc on PC vs newlib on Switch)
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
    TILE_TILEFLOOR = 9, // non-solid; tiled floor for rooms/buildings (randomly rotated)
    // Custom non-solid tiles - texture path stored per-map in CSM
    TILE_CUSTOM_0 = 16,
    TILE_CUSTOM_1 = 17,
    TILE_CUSTOM_2 = 18,
    TILE_CUSTOM_3 = 19,
    TILE_CUSTOM_4 = 20,
    TILE_CUSTOM_5 = 21,
    TILE_CUSTOM_6 = 22,
    TILE_CUSTOM_7 = 23,
    TILE_COUNT
};

// Ceiling overlay layer - separate from ground tiles
enum CeilType : uint8_t {
    CEIL_NONE = 0,
    CEIL_GLASS,   // transparent glass roof
};

struct TileMap {
    int width = MAP_DEFAULT_W;
    int height = MAP_DEFAULT_H;
    std::vector<uint8_t> tiles;
    std::vector<uint8_t> ceiling;    // overlay layer (CeilType)
    std::vector<uint8_t> noCollide;  // 1 = tile has no square collision

    // Endless mode: tiles are generated on demand from a seed (storage-free, so
    // distant chunks are never kept in memory). See proceduralTile().
    bool     endless = false;
    uint32_t seed    = 0;
    // Tiles blown open in the endless world (sparse override; the world is
    // otherwise storage-free). Key = (x<<32)^y.
    std::unordered_set<long long> endlessCarved;
    std::unordered_set<long long> scorched;   // bombed tiles, rendered darkened
    static long long tileKey(int x, int y) { return ((long long)x << 32) ^ (long long)(uint32_t)y; }
    bool isScorched(int tx, int ty) const {
        return !scorched.empty() && scorched.count(tileKey(tx, ty)) != 0;
    }

    void generate(int mapWidth, int mapHeight); // procedural arena
    void beginEndless(uint32_t worldSeed);      // switch to infinite seeded world
    void carveExplosion(Vec2 center, float radius); // blow open walls/ceiling in a blast
    void scorchArea(Vec2 center, float radius);     // darken tiles in a blast (no destruction)
    uint8_t  proceduralTile(int tx, int ty) const; // deterministic tile at (tx,ty)
    uint8_t  proceduralCeiling(int tx, int ty) const;
    // Ceiling lookup that works for both stored (finite) and endless worlds.
    uint8_t  ceilingAt(int tx, int ty) const {
        if (endless) return proceduralCeiling(tx, ty);
        if (tx < 0 || ty < 0 || tx >= width || ty >= height) return CEIL_NONE;
        size_t idx = (size_t)ty * width + tx;
        return idx < ceiling.size() ? ceiling[idx] : CEIL_NONE;
    }
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
