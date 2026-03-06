// ─── gamemode.cpp ─── Gamemode system implementation ────────────────────────
#include "gamemode.h"

// ── Predefined gamemode factories ──
GameModeRules createArenaRules() {
    GameModeRules r;
    r.type = GameModeType::Arena;
    r.name = "Arena";
    r.description = "Survive endless enemy waves";
    r.maxPlayers = 1;
    r.friendlyFire = false;
    r.lives = 1;
    r.hasScore = true;
    r.spawnEnemies = true;
    r.spawnCrates = true;
    r.crateSpawnChance = 0.15f;
    return r;
}

GameModeRules createCoopArenaRules(int maxPlayers) {
    GameModeRules r;
    r.type = GameModeType::CoopArena;
    r.name = "Co-op Arena";
    r.description = "Survive waves together";
    r.maxPlayers = maxPlayers;
    r.friendlyFire = false;
    r.lives = 0; // infinite respawns
    r.sharedLives = false;
    r.hasScore = true;
    r.spawnEnemies = true;
    r.spawnCrates = true;
    r.crateSpawnChance = 0.2f;
    r.spawnRateScale = 1.5f; // more enemies for more players
    r.respawnTime = 5.0f;
    return r;
}

GameModeRules createDeathmatchRules(int scoreLimit, int maxPlayers) {
    GameModeRules r;
    r.type = GameModeType::Deathmatch;
    r.name = "Deathmatch";
    r.description = "Free-for-all, first to score limit wins";
    r.maxPlayers = maxPlayers;
    r.friendlyFire = true; // everyone is an enemy
    r.pvpEnabled = true;
    r.lives = 0; // infinite respawns
    r.hasScore = true;
    r.spawnEnemies = false;
    r.spawnCrates = true;
    r.crateSpawnChance = 0.3f;
    r.deathmatchScoreLimit = scoreLimit;
    r.respawnTime = 3.0f;
    return r;
}

GameModeRules createCoopPlaylistRules(int maxPlayers) {
    GameModeRules r;
    r.type = GameModeType::CoopPlaylist;
    r.name = "Co-op Playlist";
    r.description = "Play through maps together";
    r.maxPlayers = maxPlayers;
    r.friendlyFire = false;
    r.lives = 3;
    r.sharedLives = true;
    r.hasScore = true;
    r.spawnEnemies = true;
    r.spawnCrates = true;
    r.crateSpawnChance = 0.15f;
    r.winOnAllEnemiesDead = true;
    r.respawnTime = 5.0f;
    return r;
}

GameModeRules createCustomRules() {
    GameModeRules r;
    r.type = GameModeType::Custom;
    r.name = "Custom Map";
    r.description = "Play a custom map";
    r.maxPlayers = 1;
    r.spawnEnemies = true;
    r.spawnCrates = true;
    r.crateSpawnChance = 0.1f;
    return r;
}

GameModeRules createTeamDeathmatchRules(int teamCount, int scoreLimit, int maxPlayers) {
    GameModeRules r;
    r.type = GameModeType::TeamDeathmatch;
    r.name = "Team Deathmatch";
    r.description = "Teams fight to the score limit";
    r.maxPlayers = maxPlayers;
    r.friendlyFire = false;
    r.pvpEnabled = true;
    r.lives = 0;
    r.hasScore = true;
    r.spawnEnemies = false;
    r.spawnCrates = true;
    r.crateSpawnChance = 0.3f;
    r.deathmatchScoreLimit = scoreLimit;
    r.respawnTime = 3.0f;
    r.teamCount = teamCount;
    return r;
}

// ── Registry ──
GameModeRegistry& GameModeRegistry::instance() {
    static GameModeRegistry reg;
    return reg;
}

void GameModeRegistry::registerMode(const GameModeEntry& entry) {
    // Don't duplicate
    for (auto& m : modes_) {
        if (m.id == entry.id) {
            m = entry; // update
            return;
        }
    }
    modes_.push_back(entry);
}

const GameModeEntry* GameModeRegistry::find(const std::string& id) const {
    for (auto& m : modes_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

void GameModeRegistry::registerBuiltins() {
    registerMode({"arena",         "Arena",           "Survive endless enemy waves",      createArenaRules(),              ""});
    registerMode({"coop_arena",    "Co-op Arena",     "Survive waves together",           createCoopArenaRules(),          ""});
    registerMode({"deathmatch",    "Deathmatch",      "Free-for-all PvP",                 createDeathmatchRules(),         ""});
    registerMode({"team_dm",       "Team Deathmatch", "Teams fight to score limit",       createTeamDeathmatchRules(),     ""});
    registerMode({"coop_playlist", "Co-op Playlist",  "Play maps together",               createCoopPlaylistRules(),       ""});
    registerMode({"custom",        "Custom Map",      "Play a custom map",                createCustomRules(),             ""});
}
