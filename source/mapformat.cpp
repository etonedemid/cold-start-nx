// ─── mapformat.cpp ─── .csm file I/O ────────────────────────────────────────
#include "mapformat.h"
#include <cstdio>
#include <cstring>

bool CustomMap::saveToFile(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("Cannot open %s for writing\n", path.c_str()); return false; }

    // Build header
    CSM_Header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic   = CSM_MAGIC;
    hdr.version = CSM_VERSION;
    hdr.width   = (uint16_t)width;
    hdr.height  = (uint16_t)height;
    strncpy(hdr.name, name.c_str(), sizeof(hdr.name) - 1);
    strncpy(hdr.creator, creator.c_str(), sizeof(hdr.creator) - 1);
    hdr.reserved[0] = gameMode;  // store game mode in first reserved byte
    // thumbnail is set externally before saving
    memcpy(hdr.thumbnail, header.thumbnail, sizeof(hdr.thumbnail));

    fwrite(&hdr, sizeof(CSM_Header), 1, f);
    fwrite(tiles.data(), 1, tiles.size(), f);
    fwrite(ceiling.data(), 1, ceiling.size(), f);

    uint16_t trigCount = (uint16_t)triggers.size();
    fwrite(&trigCount, sizeof(uint16_t), 1, f);
    if (trigCount > 0)
        fwrite(triggers.data(), sizeof(MapTrigger), trigCount, f);

    uint16_t spawnCount = (uint16_t)enemySpawns.size();
    fwrite(&spawnCount, sizeof(uint16_t), 1, f);
    if (spawnCount > 0)
        fwrite(enemySpawns.data(), sizeof(EnemySpawn), spawnCount, f);

    fclose(f);
    printf("Saved map: %s (%dx%d, %d triggers, %d spawns)\n",
        path.c_str(), width, height, (int)triggers.size(), (int)enemySpawns.size());
    return true;
}

bool CustomMap::loadFromFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { printf("Cannot open %s\n", path.c_str()); return false; }

    CSM_Header hdr;
    if (fread(&hdr, sizeof(CSM_Header), 1, f) != 1) { fclose(f); return false; }
    if (hdr.magic != CSM_MAGIC) { printf("Invalid CSM file\n"); fclose(f); return false; }
    if (hdr.version > CSM_VERSION) { printf("CSM version %d not supported\n", hdr.version); fclose(f); return false; }

    header = hdr;
    width  = hdr.width;
    height = hdr.height;
    name    = std::string(hdr.name);
    creator = std::string(hdr.creator);
    gameMode = hdr.reserved[0];  // 0=Arena, 1=Sandbox

    int area = width * height;
    tiles.resize(area);
    ceiling.resize(area);

    if (fread(tiles.data(), 1, area, f) != (size_t)area) { fclose(f); return false; }
    if (fread(ceiling.data(), 1, area, f) != (size_t)area) { fclose(f); return false; }

    uint16_t trigCount = 0;
    fread(&trigCount, sizeof(uint16_t), 1, f);
    triggers.resize(trigCount);
    if (trigCount > 0)
        fread(triggers.data(), sizeof(MapTrigger), trigCount, f);

    uint16_t spawnCount = 0;
    fread(&spawnCount, sizeof(uint16_t), 1, f);
    enemySpawns.resize(spawnCount);
    if (spawnCount > 0)
        fread(enemySpawns.data(), sizeof(EnemySpawn), spawnCount, f);

    fclose(f);
    printf("Loaded map: %s (%dx%d, %d triggers, %d spawns)\n",
        name.c_str(), width, height, (int)triggers.size(), (int)enemySpawns.size());
    return true;
}

MapTrigger* CustomMap::findStartTrigger() {
    for (auto& t : triggers)
        if (t.type == TriggerType::LevelStart) return &t;
    return nullptr;
}

MapTrigger* CustomMap::findEndTrigger() {
    for (auto& t : triggers)
        if (t.type == TriggerType::LevelEnd) return &t;
    return nullptr;
}

std::vector<MapTrigger*> CustomMap::findTriggersByType(TriggerType type) {
    std::vector<MapTrigger*> result;
    for (auto& t : triggers)
        if (t.type == type) result.push_back(&t);
    return result;
}
