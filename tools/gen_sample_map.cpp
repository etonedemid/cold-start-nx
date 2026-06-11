// Generates the Act One mission 1 sample map (cold_start_a1.csm) using the
// real CustomMap::saveToFile so the binary layout always matches the loader.
// Build:  g++ -std=c++17 -I../source gen_sample_map.cpp ../source/mapformat.cpp -o gen_sample_map
// Run:    ./gen_sample_map ../romfs/maps/cold_start_a1.csm
#include "mapformat.h"
#include <cstdio>

// Tile ids (mirror tilemap.h TileType to avoid pulling SDL headers)
static constexpr uint8_t TILE_FLOOR = 0;
static constexpr uint8_t TILE_WALL  = 5;

// Entity spawn ids mirror editor.h ENTITY_* constants.
enum {
    E_SHOOTER       = 1,
    E_CIVILIAN      = 8,
    E_RESPONDER     = 9,
    E_INFRA_MEDRELAY= 10,
};

static constexpr int TS = 64; // TILE_SIZE
static float wc(int t) { return t * TS + TS / 2.0f; }

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "cold_start_a1.csm";

    CustomMap m;
    m.width = 30; m.height = 20;
    m.name = "Cold Start A1";
    m.creator = "Story";
    m.gameMode = 0; // Arena
    m.tiles.assign(m.width * m.height, TILE_FLOOR);
    m.ceiling.assign(m.width * m.height, 0);
    auto setTile = [&](int x, int y, uint8_t v){ m.tiles[y * m.width + x] = v; };
    // Border walls
    for (int x = 0; x < m.width; x++)  { setTile(x, 0, TILE_WALL); setTile(x, m.height-1, TILE_WALL); }
    for (int y = 0; y < m.height; y++) { setTile(0, y, TILE_WALL); setTile(m.width-1, y, TILE_WALL); }

    auto trig = [&](TriggerType t, int tx, int ty, float w, float h,
                    GoalCondition cond = GoalCondition::Immediate, uint8_t param = 0){
        MapTrigger mt{};
        mt.type = t; mt.x = wc(tx); mt.y = wc(ty);
        mt.width = w; mt.height = h; mt.condition = cond; mt.param = param;
        m.triggers.push_back(mt);
    };

    // Player spawn (left) and exit (right, opens once the cache is recovered)
    trig(TriggerType::LevelStart, 2, 10, 64, 64);
    trig(TriggerType::LevelEnd,   27, 10, 96, 160, GoalCondition::OnFlag);

    // AVA announcement cutscene zone (library index 1 == "ava1")
    trig(TriggerType::Cutscene,   15, 10, 192, 320, GoalCondition::Immediate, 1);

    // SIGNAL reward for taking the clean northern route around the civilians
    trig(TriggerType::SignalZone, 8, 3, 192, 128, GoalCondition::Immediate, (uint8_t)10);

    // Objective: recover the archive cache -> sets goal_open (opens the exit)
    trig(TriggerType::Objective,  24, 10, 96, 96, GoalCondition::Immediate, 0 /*Recover*/);

    auto spawn = [&](uint8_t type, int tx, int ty, uint8_t disableable = 0){
        EnemySpawn es{};
        es.x = wc(tx); es.y = wc(ty); es.enemyType = type; es.waveGroup = 0;
        es.reserved[0] = disableable;
        m.enemySpawns.push_back(es);
    };

    // Squatter civilians clustered mid-map (the first SIGNAL test)
    spawn(E_CIVILIAN, 12, 9);
    spawn(E_CIVILIAN, 13, 10);
    spawn(E_CIVILIAN, 12, 11);
    // Med-relay infrastructure (people live on it)
    spawn(E_INFRA_MEDRELAY, 10, 6);
    // A disableable AVA responder (disable for SIGNAL, destroy to expose operator)
    spawn(E_RESPONDER, 18, 10, /*disableable=*/1);
    // A plain hostile for combat
    spawn(E_SHOOTER, 20, 6);

    if (!m.saveToFile(out)) { fprintf(stderr, "save failed: %s\n", out); return 1; }
    printf("wrote %s\n", out);
    return 0;
}
