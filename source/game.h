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
#include "mappack.h"

#include <SDL2/SDL.h>
#include <vector>

enum class GameState {
    MainMenu,
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

class Game {
public:
    bool init();
    void run();
    void shutdown();

private:
    // ── Core ──
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
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
    bool backInput_ = false;
    bool leftInput_ = false;
    bool rightInput_ = false;
    int  menuSelection_ = 0;
    int  configSelection_ = 0;

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

    // ── Map file browser ──
    std::vector<std::string> mapFiles_;
    int mapSelectIdx_ = 0;

    // ── Map Pack system ──
    std::vector<MapPack> availablePacks_;
    int packSelectIdx_ = 0;
    MapPack currentPack_;           // Currently playing pack
    bool playingPack_ = false;      // In pack campaign mode
    CharacterDef packCharDef_;      // Character loaded from pack

    // ── Visual Polish ──
    float waveAnnounceTimer_ = 0;  // countdown for wave banner display
    int   waveAnnounceNum_   = 0;  // which wave to show
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
    bool enemyCanSeePlayer(const Enemy& e) const;
    Vec2 steerToward(Vec2 from, Vec2 to, float spd, float dt) const;

    // Combat
    void spawnBullet(Vec2 pos, float angle);
    void spawnEnemyBullet(Vec2 pos, Vec2 target);
    void spawnExplosion(Vec2 pos);
    void spawnBomb();
    void spawnEnemy(Vec2 pos, EnemyType type);
    void killEnemy(Enemy& e);
    void playerParry();
    void destroyBox(int tx, int ty);
    void updateBoxFragments(float dt);
    void playMenuMusic();

    // Rendering helpers
    void renderSprite(SDL_Texture* tex, Vec2 worldPos, float angle, float scale = 1.0f);
    void renderSpriteEx(SDL_Texture* tex, Vec2 worldPos, float angle, float scale, SDL_Color tint);
    void renderMap();
    void renderRoofOverlay();
    void renderShadingPass();
    void renderUI();
    void renderMainMenu();
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
    void updateCustomMapGoal();
    void scanMapFiles();
    void scanCharacters();
    void applyCharacter(const CharacterDef& cd);

    // Additional menu renders
    void renderMapSelectMenu();
    void renderCharSelectMenu();
    void renderCustomWinScreen();
    void renderCharCreator();
    void saveCharCreator();
    void saveConfig();
    void loadConfig();

    // Map Pack support
    void scanMapPacks();
    void renderPackSelectMenu();
    void renderPackLevelWin();
    void renderPackComplete();
    void startPackLevel();
    void advancePackLevel();
};
