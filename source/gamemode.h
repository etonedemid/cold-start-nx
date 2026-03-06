#pragma once
// ─── gamemode.h ─── Gamemode system for multiplayer & singleplayer ──────────
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

// ── Built-in gamemode types ──
enum class GameModeType : uint8_t {
    Arena,          // Classic singleplayer wave survival (default)
    CoopArena,      // Multiplayer cooperative wave survival
    Deathmatch,     // Free-for-all PvP
    TeamDeathmatch, // Team-based PvP (2 or 4 teams)
    CoopPlaylist,   // Multiplayer cooperative map pack
    Custom,         // Custom single map play
    CustomGamemode, // Mod-defined gamemode
    COUNT
};

// ── Gamemode rules / configuration ──
struct GameModeRules {
    GameModeType type            = GameModeType::Arena;
    std::string  name            = "Arena";
    std::string  description     = "Survive enemy waves";

    // Player settings
    int   maxPlayers             = 1;
    bool  friendlyFire           = false;
    int   lives                  = 1;       // 0 = infinite
    bool  sharedLives            = false;   // all players share life pool

    // Scoring
    bool  hasScore               = true;
    bool  hasTimer               = false;
    float timeLimit              = 0;       // seconds, 0 = unlimited

    // Spawning
    bool  spawnEnemies           = true;
    float spawnRateScale         = 1.0f;
    bool  spawnCrates            = true;
    float crateSpawnChance       = 0.15f;   // per wave

    // Win/lose conditions
    bool  winOnAllEnemiesDead    = false;   // for custom maps
    bool  winOnTimeSurvived      = false;
    int   winKillTarget          = 0;       // 0 = no kill target
    int   deathmatchScoreLimit   = 20;      // kills to win DM

    // PvP-specific
    bool  pvpEnabled             = false;
    float respawnTime            = 3.0f;    // seconds after death in DM

    // Team settings
    int   teamCount              = 0;       // 0 = no teams, 2/4 = teams
    bool  upgradesShared         = false;   // upgrade applies to all players or just picker
};

// ── Lobby settings synced from host to all clients ──
struct LobbySettings {
    // Gamemode
    bool  friendlyFire           = false;
    bool  pvpEnabled             = false;
    bool  isPvp                  = true;    // true = PVP mode, false = PVE mode
    int   teamCount              = 0;       // 0, 2, or 4
    bool  upgradesShared         = false;

    // Map
    int   mapWidth               = 40;
    int   mapHeight              = 40;

    // Enemy
    float enemyHpScale           = 1.0f;
    float enemySpeedScale        = 1.0f;
    float spawnRateScale         = 1.0f;

    // Player
    int   playerMaxHp            = 10;
    int   livesPerPlayer         = 0;       // 0 = infinite lives
    bool  livesShared            = false;   // all players share one lives pool

    // PVP-specific
    float crateInterval          = 25.0f;   // seconds between crate spawns in PVP
    float pvpMatchDuration       = 0.0f;    // 0 = unlimited (last alive wins), >0 = timer in seconds

    // PVE-specific
    int   waveCount              = 0;       // 0 = infinite waves, >0 = victory after clearing N waves

    // Host setup
    int   maxPlayers             = 8;       // max players for this lobby (2-16)
};

// ── Predefined gamemode factories ──
GameModeRules createArenaRules();
GameModeRules createCoopArenaRules(int maxPlayers = 4);
GameModeRules createDeathmatchRules(int scoreLimit = 20, int maxPlayers = 8);
GameModeRules createCoopPlaylistRules(int maxPlayers = 4);
GameModeRules createCustomRules();
GameModeRules createTeamDeathmatchRules(int teamCount = 2, int scoreLimit = 20, int maxPlayers = 8);

// ── Gamemode registry (for mod-added gamemodes) ──
struct GameModeEntry {
    std::string   id;           // unique string ID e.g. "deathmatch", "mymod_ctf"
    std::string   displayName;
    std::string   description;
    GameModeRules defaultRules;
    std::string   modSource;    // "" for built-in, mod ID otherwise
};

class GameModeRegistry {
public:
    static GameModeRegistry& instance();

    void registerMode(const GameModeEntry& entry);
    const GameModeEntry* find(const std::string& id) const;
    const std::vector<GameModeEntry>& all() const { return modes_; }

    void registerBuiltins();

private:
    std::vector<GameModeEntry> modes_;
};
