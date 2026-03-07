#pragma once
// ─── game.h ─── Main game state machine ─────────────────────────────────────
#include "vec2.h"
#include "constants.h"
#include "entity.h"
#include "player.h"
#include "enemy.h"
#include "bomb.h"
#include "camera.h"
#include "tilemap.h"
#include "assets.h"
#include "mapformat.h"
#include "charformat.h"
#include "editor.h"
#include "texeditor.h"
#include "mappack.h"
#include "pickup.h"
#include "gamemode.h"
#include "mod.h"
#include "network.h"
#include "ui.h"

#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <set>
#include <functional>

enum class GameState {
    MainMenu,
    PlayModeMenu,    // Choose between generated map, custom map, or pack
    ConfigMenu,
    Playing,
    Paused,
    Dead,
    EditorConfig,    // Editor setup screen (new/load map, size, name)
    Editor,          // Map editor
    MapSelect,       // Choose a .csm to play
    CharSelect,      // Choose a .cschar
    CharCreator,     // Create/edit a .cschar
    PlayingCustom,   // Playing a loaded .csm map
    CustomPaused,
    CustomDead,
    CustomWin,       // Level complete
    PackSelect,      // Choose a map pack
    PlayingPack,     // Playing a map pack campaign
    PackPaused,
    PackDead,
    PackLevelWin,    // Between levels in a pack
    PackComplete,    // All levels done
    // ── Multiplayer ──
    MultiplayerMenu, // Host/Join/Browse
    HostSetup,       // Configuring game before hosting
    JoinMenu,        // Enter IP to connect
    Lobby,           // Pre-game lobby (host & clients)
    MultiplayerGame, // Active multiplayer game
    MultiplayerPaused,
    MultiplayerDead, // Waiting to respawn
    Scoreboard,      // Match results / scoreboard
    TeamSelect,      // Team selection screen before team game starts
    MultiplayerSpectator, // Player exhausted all lives — free-roam ghost, no respawn
    WinLoss,             // Win/Loss result screen (shown before Scoreboard)
    // ── Map game mode select (before playing a custom map) ──
    MapConfig,       // Choose Arena / Sandbox before starting a custom map
    // ── Sprite editor ──
    SpriteEditor,    // Pixel art / sprite editor
    // ── Mod management ──
    ModMenu,         // Enable/disable mods
};

struct GameConfig {
    int mapWidth = MAP_DEFAULT_W;
    int mapHeight = MAP_DEFAULT_H;
    int playerMaxHp = PLAYER_MAX_HP;
    float spawnRateScale = 1.0f;
    float enemyHpScale = 1.0f;
    float enemySpeedScale = 1.0f;
    int musicVolume = 80;   // 0-128
    int sfxVolume   = 100;  // 0-128
    std::string username = "Player"; // multiplayer display name
    bool fullscreen = false;
};

enum class DecalType : uint8_t { Blood, Scorch };

struct BloodDecal {
    Vec2  pos;
    float rotation;
    float alpha = 1.0f;
    float scale = 1.0f;
    DecalType type = DecalType::Blood;
};

struct BoxFragment {
    Vec2  pos;
    Vec2  vel;
    float rotation;
    float rotSpeed;
    float size;
    float lifetime = 0.6f;
    float age = 0;
    bool  alive = true;
    SDL_Color color;
};

// ── Mod-save dialog state ────────────────────────────────────────────────────
struct ModSaveDialogState {
    enum Phase  { Closed, ChooseMod, NameNewMod, ChooseCategory };
    enum Asset  { AssetMap, AssetSprite, AssetCharacter };

    Phase phase = Closed;
    Asset asset = AssetMap;

    // Snapshot of available mods shown at open time
    std::vector<std::string> modIds;
    std::vector<std::string> modNames;
    int selIdx = 0;    // 0 = "＋ New Mod", 1+ = existing mods

    // New-mod name input
    std::string newModId;
    bool        textEditing = false;
    int         gpCharIdx   = 0;

    // Sprite category selection (0=Ground Tile, 1=Wall Tile, 2=Ceiling Tile,
    //                            3=Character Body, 4=Character Legs)
    int catIdx = 0;
    static constexpr int CAT_COUNT = 5;
    static constexpr const char* CAT_NAMES[5] = {
        "Ground Tile  (sprites/tiles/ground/)",
        "Wall Tile    (sprites/tiles/walls/)",
        "Ceiling Tile (sprites/tiles/ceiling/)",
        "Character Body (sprites/characters/body/)",
        "Character Legs (sprites/characters/legs/)"
    };

    // Results
    bool        confirmed           = false;
    std::string confirmedModFolder;  // e.g. "mods/mymod"
    int         confirmedCat        = 0;

    bool isOpen() const { return phase != Closed; }
    void close()        { phase = Closed; confirmed = false; }
};

class Game {
public:
    bool init();
    void run();
    void shutdown();

private:
    // ── Core ──
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    UI::Context   ui_;  // Immediate-mode UI system
    bool running_ = true;
    GameState state_ = GameState::MainMenu;
    float gameTime_ = 0;      // seconds since level start
    float dt_ = 0;

    // ── Input (mapped to Joy-Con / Pro Controller) ──
    Vec2 moveInput_  = {0,0};
    Vec2 aimInput_   = {0,0};
    bool fireInput_  = false;
    bool bombInput_  = false;
    bool bombLaunchInput_ = false;
    bool bombLaunchHeld_ = false;  // debounce for trigger
    bool parryInput_ = false;
    bool pauseInput_ = false;
    bool confirmInput_ = false;
    bool usingGamepad_ = false;  // true when last input was from a gamepad (shows soft KB on PC)
    bool backInput_ = false;
    bool leftInput_ = false;
    bool rightInput_ = false;
    bool tabInput_   = false;  // Y button / Tab — kick-mode toggle in lobby
    int  menuSelection_ = 0;
    int  configSelection_ = 0;
    int  lobbyKickCursor_ = -1;  // -1 = not in player-kick mode; >=0 = player index
    std::set<uint8_t> bannedPlayerIds_;  // session ban list (host only)
    std::string lobbyPassword_ = "";     // password for this hosted lobby
    bool hostPasswordTyping_ = false;    // editing lobby password in HostSetup
    int  hostPasswordCharIdx_ = 0;       // char picker index for host password

    // ── World ──
    Player              player_;
    std::vector<Enemy>  enemies_;
    std::vector<Entity> bullets_;       // player bullets
    std::vector<Entity> enemyBullets_;
    std::vector<Bomb>   bombs_;
    std::vector<Explosion> explosions_;
    std::vector<Entity> debris_;
    std::vector<BloodDecal> blood_;
    std::vector<BoxFragment> boxFragments_;
    std::vector<PickupCrate> crates_;      // upgrade crates
    std::vector<Pickup>      pickups_;     // floating pickups
    PlayerUpgrades           upgrades_;    // active player upgrades
    Camera              camera_;
    TileMap             map_;

    // ── Spawning (wave system) ──
    int   waveNumber_     = 0;
    int   waveEnemiesLeft_= 0;     // enemies still to spawn this wave
    float waveSpawnTimer_ = 0;     // delay between individual spawns in a wave
    float wavePauseTimer_ = 0;     // countdown between waves
    bool  waveActive_     = false; // currently spawning a wave
    GameConfig config_{};

    // ── Map Editor ──
    MapEditor editor_;

    // ── Sprite / Texture Editor ──
    TextureEditor texEditor_;

    // ── Custom map play ──
    CustomMap customMap_;
    bool      playingCustomMap_ = false;
    bool      testPlayFromEditor_ = false;  // return to editor when done
    bool      customGoalOpen_   = false;  // end goal door is open
    int       customEnemiesTotal_ = 0;    // total enemy spawns in custom map

    // ── Character system ──
    std::vector<CharacterDef> availableChars_;
    int selectedChar_ = -1;  // -1 = default character

    // ── Character Creator ──
    struct CharCreatorState {
        std::string name = "NewChar";
        float speed = 520.0f;
        int   hp    = 10;
        int   ammo  = 10;
        float fireRate = 10.0f;
        float reloadTime = 1.0f;
        int   bodyFrames = 10;
        int   legFrames  = 8;
        int   deathFrames = 12;
        int   field = 0;      // currently selected field (0-8)
        bool  textEditing = false;  // editing name text
        std::string textBuf;        // temp text input buffer
        int   gpCharIdx = 0;        // gamepad char palette index
    } charCreator_;

    // ── Play Mode Menu ──
    int playModeSelection_ = 0;   // 0=Generated,1=Map,2=Pack,3-8=sliders,9=Back
    GameState prevMenuState_ = GameState::MainMenu; // for back nav in MapSelect/PackSelect

    // ── Map file browser ──
    std::vector<std::string> mapFiles_;
    int mapSelectIdx_ = 0;

    // ── Map pre-game config (mode selection) ──
    int  mapConfigMode_  = 0;   // 0=Arena, 1=Sandbox
    bool sandboxMode_    = false; // no enemies/crates when true

    // ── Map Pack system ──
    std::vector<MapPack> availablePacks_;
    int packSelectIdx_ = 0;
    MapPack currentPack_;           // Currently playing pack
    bool playingPack_ = false;      // In pack campaign mode
    CharacterDef packCharDef_;      // Character loaded from pack

    // ── Visual Polish ──
    float waveAnnounceTimer_ = 0;  // countdown for wave banner display
    int   waveAnnounceNum_   = 0;  // which wave to show

    // Pickup name popup (reuses wave banner style)
    float pickupPopupTimer_ = 0;
    std::string pickupPopupName_;
    std::string pickupPopupDesc_;
    SDL_Color   pickupPopupColor_ = {255, 255, 255, 255};
    float muzzleFlashTimer_  = 0;  // bright flash near gun when firing
    Vec2  muzzleFlashPos_     = {0,0}; // world position of muzzle flash
    float screenFlashTimer_  = 0;  // brief white flash (e.g. explosion)
    float screenFlashR_ = 255, screenFlashG_ = 255, screenFlashB_ = 255;

    // ── Sprites ──
    std::vector<SDL_Texture*> playerSprites_;
    std::vector<SDL_Texture*> playerDeathSprites_;
    std::vector<SDL_Texture*> legSprites_;
    std::vector<SDL_Texture*> bombSprites_;
    SDL_Texture* enemySprite_   = nullptr;
    SDL_Texture* shooterSprite_ = nullptr;
    SDL_Texture* bulletSprite_  = nullptr;
    SDL_Texture* enemyBulletSprite_ = nullptr;
    SDL_Texture* shieldSprite_  = nullptr;
    SDL_Texture* mainmenuBg_   = nullptr;
    SDL_Texture* bloodTex_     = nullptr;
    SDL_Texture* floorTex_     = nullptr;
    SDL_Texture* grassTex_     = nullptr;
    SDL_Texture* gravelTex_    = nullptr;
    SDL_Texture* woodTex_      = nullptr;
    SDL_Texture* sandTex_      = nullptr;
    SDL_Texture* wallTex_      = nullptr;
    SDL_Texture* glassTex_     = nullptr;
    SDL_Texture* deskTex_      = nullptr;
    SDL_Texture* boxTex_       = nullptr;
    SDL_Texture* gravelGrass1Tex_ = nullptr;
    SDL_Texture* gravelGrass2Tex_ = nullptr;
    SDL_Texture* gravelGrass3Tex_ = nullptr;
    SDL_Texture* glassTileTex_   = nullptr;
    SDL_Texture* vignetteTex_    = nullptr;
    // Custom tile textures loaded from map-embedded paths (TILE_CUSTOM_0..7)
    SDL_Texture* customTileTextures_[8] = {nullptr};

    // ── SFX ──
    Mix_Chunk* sfxShoot_    = nullptr;
    Mix_Chunk* sfxEnemyShoot_ = nullptr;
    Mix_Chunk* sfxReload_   = nullptr;
    Mix_Chunk* sfxHurt_     = nullptr;
    Mix_Chunk* sfxDeath_    = nullptr;
    Mix_Chunk* sfxExplosion_= nullptr;
    Mix_Chunk* sfxParry_    = nullptr;
    Mix_Chunk* sfxSwoosh_   = nullptr;
    Mix_Chunk* sfxBeep_     = nullptr;
    Mix_Chunk* sfxPress_    = nullptr;
    Mix_Music* bgMusic_     = nullptr;
    Mix_Music* menuMusic_   = nullptr;

    // ── Methods ──
    void loadAssets();
    void startGame();
    void handleInput();
    void update();
    void render();

    // Update sub-systems
    void updatePlayer(float dt);
    void updateEnemies(float dt);
    void updateBullets(float dt);
    void updateBombs(float dt);
    void updateExplosions(float dt);
    void updateSpawning(float dt);
    void resolveCollisions();

    // Enemy AI helpers
    void enemyWander(Enemy& e, float dt);
    void enemyChase(Enemy& e, float dt);
    void enemyDash(Enemy& e, float dt);
    void enemyShoot(Enemy& e, float dt);
    bool enemyCanSeeAnyPlayer(Enemy& e);      // sets e.targetPlayerId, returns true if any player visible
    Vec2 getEnemyTargetPos(const Enemy& e) const; // returns position of the enemy's current target
    Vec2 steerToward(Vec2 from, Vec2 to, float spd, float dt) const;

    // Combat
    void spawnBullet(Vec2 pos, float angle);
    void spawnEnemyBullet(Vec2 pos, Vec2 target);
    void spawnExplosion(Vec2 pos, uint8_t ownerId = 255);
    void spawnBomb();
    Vec2 pickSpawnPos();  // team-corner or random spawn (multiplayer)
    void spawnEnemy(Vec2 pos, EnemyType type);
    void killEnemy(Enemy& e, bool trackKill = true);
    void playerParry();
    void destroyBox(int tx, int ty);
    void updateBoxFragments(float dt);
    void playMenuMusic();

    // Rendering helpers
    void renderSprite(SDL_Texture* tex, Vec2 worldPos, float angle, float scale = 1.0f);
    void renderSpriteEx(SDL_Texture* tex, Vec2 worldPos, float angle, float scale, SDL_Color tint);
    void renderExplosionPixelated(const Explosion& ex);
    void renderMap();
    void renderWallOverlay();
    void renderDecals();
    void renderRoofOverlay();
    void renderShadingPass();
    void renderUI();
    void renderMainMenu();
    void renderPlayModeMenu();
    void renderConfigMenu();
    void renderPauseMenu();
    void renderDeathScreen();
    void drawText(const char* text, int x, int y, int size, SDL_Color color);
    void drawTextCentered(const char* text, int y, int size, SDL_Color color);

    // Collision helpers
    bool wallCollision(Vec2 pos, float halfSize) const;
    void slideResolve(Vec2& pos, Vec2& vel, float halfSize);

    // ── New systems ──
    void startCustomMap(const std::string& path);
    void startCustomMapMultiplayer(const std::string& path);
    void updateCustomMapGoal();
    void scanMapFiles();
    void scanCharacters();
    void applyCharacter(const CharacterDef& cd);

    // Additional menu renders
    void renderMapSelectMenu();
    void renderMapConfigMenu();  // Arena / Sandbox mode select
    void renderCharSelectMenu();
    void renderCustomWinScreen();
    void renderCharCreator();
    void saveCharCreator();
    void saveCharCreatorToMod(const std::string& modFolder);

    // ── Mod-save overlay dialog ──
    ModSaveDialogState modSaveDialog_;
    bool charCreatorWantsModSave_ = false;
    void openModSaveDialog(ModSaveDialogState::Asset asset);
    void handleModSaveDialogEvent(const SDL_Event& e);
    void renderModSaveDialog();
    static std::string modBuildFolder(const std::string& modId, const std::string& displayName);
    void saveConfig();
    void loadConfig();

    // Map Pack support
    void scanMapPacks();
    void renderPackSelectMenu();
    void renderPackLevelWin();
    void renderPackComplete();
    void startPackLevel();
    void advancePackLevel();

    // ── Pickup / Crate system ──
    void updateCrates(float dt);
    void updatePickups(float dt);
    void spawnCrate(Vec2 pos);
    void collectPickup(Pickup& p);
    void applyUpgrade(UpgradeType type);
    void renderCrates();
    void renderPickups();
    float crateSpawnTimer_  = 0;
    float cratePopupTimer_  = 0;  // countdown for "SUPPLY DROP" popup

    // ── Multiplayer ──
    GameModeRules currentRules_;            // active gamemode rules
    float netStateSendTimer_ = 0;           // rate-limit state sends
    float enemySendTimer_    = 0;           // rate-limit enemy state sends (host only)
    bool  enemyStatesNeedUpdate_ = false;   // send enemy states immediately on next frame (after kill)
    uint32_t nextBulletNetId_ = 1;          // monotonically increasing bullet network ID
    float respawnTimer_ = 0;                // countdown after death
    float connectTimer_ = 0;                // connection attempt timeout
    bool  suppressNetExplosion_ = false;    // prevent re-sending network-spawned explosions
    bool  pvpDamageThisFrame_ = false;       // flag for PVP bullet-player collision
    std::string joinAddress_ = "127.0.0.1"; // address to join
    std::string connectStatus_;              // connection status message
    bool        ipTyping_    = false;        // currently editing IP on gamepad
    bool        usernameTyping_ = false;     // editing username in config
    bool        mpUsernameTyping_ = false;   // editing username in host/join menus
    int         usernameCharIdx_ = 0;        // palette index for username char picker
    int         kbNavHeldButton_ = -1;       // D-pad button held during keyboard picker nav
    Uint32      kbNavRepeatAt_   = 0;        // SDL_GetTicks target for next repeat step

    // ── Centralized soft keyboard ──
    struct SoftKeyboard {
        bool     active     = false;
        const char* palette = nullptr;
        int      cols       = 16;
        int      charIdx    = 0;
        int      maxLen     = 32;
        int      heldButton = -1;
        Uint32   repeatAt   = 0;
        std::string* target = nullptr;
        std::function<void(bool confirmed)> onDone;

        void open(const char* pal, int c, std::string* tgt, int max,
                  std::function<void(bool confirmed)> done = nullptr);
        void close(bool confirmed);
    };
    SoftKeyboard softKB_;
    bool handleSoftKBEvent(SDL_Event& e);
    void updateSoftKBRepeat();
    void renderSoftKB(int centerY);
    std::string presetNameBuf_;  // buffer for naming host presets
    int         ipCharIdx_   = 0;            // palette index for IP char picker
    int  multiMenuSelection_ = 0;
    int  lobbyMenuSelection_ = 0;
    int  hostSetupSelection_ = 0;
    int  joinMenuSelection_  = 0;             // 0=edit addr, 1=edit port, 2=edit username, 3=password, 4=connect, 5=save, 6=back
    std::string joinPassword_ = "";              // password to send when joining
    bool joinPasswordTyping_ = false;            // editing join password
    int  joinPasswordCharIdx_ = 0;               // char picker index for join password
    int  gamemodeSelectIdx_ = 0;
    int  hostMapSelectIdx_ = 0;              // map selection in host setup
    int  hostMaxPlayers_ = 8;                   // configurable max players (2-16)
    int  hostPort_ = 7777;                      // configurable host port
    bool portTyping_  = false;                   // currently editing host port with keyboard
    int  portCharIdx_ = 0;                       // palette index for host port char picker
    std::string portStr_ = "7777";               // host port as editable string
    int  joinPort_ = 7777;                       // port used when joining
    bool joinPortTyping_ = false;                // currently editing join port with keyboard
    int  joinPortCharIdx_ = 0;                   // palette index for join port char picker
    std::string joinPortStr_ = "7777";           // join port as editable string
    int  modMenuSelection_ = 0;
    int  modMenuTab_       = 0;  // 0=Mods 1=Characters 2=Maps 3=Playlists
    bool lobbyReady_ = false;

    // ── Lobby settings (host-controlled) ──
    LobbySettings lobbySettings_;           // synced from host to clients
    int  lobbySettingsSel_   = 0;           // which settings row is selected (host)
    int  lobbySettingsScroll_= 0;           // scroll offset for settings panel
    int  lobbyGamemodeIdx_   = 0;           // index into GameModeRegistry for lobby
    int  lobbyMapIdx_        = 0;           // 0=random, 1+=custom maps

    // ── Team selection ──
    int8_t localTeam_        = -1;          // local player's chosen team (-1=none)
    int    teamSelectCursor_ = 0;           // cursor for team selection screen
    bool   teamLocked_       = false;       // player locked in team choice

    // ── Lives system ──
    int    localLives_       = -1;          // -1=infinite, >=0=remaining lives (individual)
    int    sharedLives_      = -1;          // -1=infinite, >=0=remaining (shared pool, host only)
    bool   spectatorMode_    = false;       // ran out of lives, spectating as ghost

    // ── Admin menu (host-only overlay during gameplay) ──
    bool   adminMenuOpen_    = false;
    int    adminMenuSel_     = 0;           // selected player index
    int    adminMenuAction_  = 0;           // 0=kick, 1=respawn, 2=team-, 3=team+

    // ── Multiplayer pause sub-state ──
    int    pauseMenuSub_     = 0;           // 0=main list, 1=team-pick inline
    int    pauseTeamCursor_  = 0;           // cursor when picking team from pause

    // ── Saved servers ──
    struct SavedServer {
        std::string name;    // display name
        std::string address; // IP address
        int port = 7777;
    };
    std::vector<SavedServer> savedServers_;
    int serverListSelection_ = 0;
    bool serverNameEditing_ = false;
    std::string serverNameBuf_;
    int serverNameCharIdx_ = 0;

    // ── Server config presets ──
    struct ServerPreset {
        std::string name;
        std::string gamemodeId;
        int maxPlayers = 8;
        int hostPort = 7777;
        int mapIndex = 0;        // 0 = generated
        LobbySettings lobbySettings; // saved lobby game settings
    };
    std::vector<ServerPreset> serverPresets_;
    int presetSelection_ = 0;

    void initMultiplayer();
    void shutdownMultiplayer();
    void updateMultiplayer(float dt);
    void renderMultiplayerMenu();
    void renderHostSetup();
    void renderJoinMenu();
    void renderLobby();
    void renderMultiplayerGame();
    void renderMultiplayerHUD();
    void renderMultiplayerPause();
    void renderMultiplayerDeath();
    // ── Match result ──
    enum class MatchEndReason : uint8_t {
        Unknown      = 0,
        WavesCleared = 1, // PVE: all target waves cleared
        TeamWiped    = 2, // PVE: all players exhausted lives
        LastAlive    = 3, // PVP: last player/team standing
        TimeUp       = 4, // PVP: match timer expired, most kills wins
        HostEnded    = 5, // manual: host pressed "End Game"
    };
    struct MatchResult {
        bool           isWin       = false;
        MatchEndReason reason      = MatchEndReason::Unknown;
        std::string    headline;     // "VICTORY" / "DEFEAT" / "GAME OVER"
        std::string    subtitle;     // short result description
        std::string    winnerName;   // FFA winner player name
        int            winnerTeam   = -1; // winning team index, -1 = none
        int            winnerKills  = 0;
        float          matchElapsed = 0.f; // seconds the match lasted
    };
    MatchResult matchResult_;
    float matchTimer_   = 0.f;    // counts DOWN from pvpMatchDuration (0 if no limit)
    float matchElapsed_ = 0.f;    // counts UP from game start
    void renderWinLoss();
    void renderScoreboard();
    void renderTeamSelect();
    void renderAdminMenu();
    void renderModMenu();
    void renderRemotePlayers();
    void setupNetworkCallbacks();
    void hostGame();
    void joinGame();
    void startMultiplayerGame();  // host only

    // Saved servers
    void loadSavedServers();
    void saveSavedServers();
    void addSavedServer(const std::string& name, const std::string& addr, int port);
    void removeSavedServer(int idx);

    // Server config presets
    void loadServerPresets();
    void saveServerPresets();
    void addServerPreset(const std::string& name, const std::string& gamemodeId, int maxPlayers, int port, int mapIdx, const LobbySettings& ls = {});
    void removeServerPreset(int idx);
    void applyServerPreset(int idx);

    // ── IP utility ──
    std::string getLocalIP();

    // ── Mod system ──
    void initMods();
    void applyModOverrides();
};
