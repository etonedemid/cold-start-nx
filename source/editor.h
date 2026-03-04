#pragma once
// ─── editor.h ─── Map Editor for COLD START ─────────────────────────────────
#include "tilemap.h"
#include "mapformat.h"
#include "camera.h"
#include "assets.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>

// ── Editor tile palette entry (loaded from tiles/ subfolders) ──
struct EditorTile {
    std::string name;         // display name
    std::string path;         // file path to PNG
    SDL_Texture* texture;     // loaded texture
    uint8_t tileType;         // TileType enum value
    std::string category;     // "ground", "walls", "ceiling", etc.
};

// ── Editor tool mode ──
enum class EditorTool : uint8_t {
    Tile     = 0,  // paint tiles
    Trigger  = 1,  // place triggers
    Enemy    = 2,  // place enemy spawns
    Erase    = 3,  // remove
    Select   = 4,  // select & configure
};

// ── Trigger placement ghost ──
struct TriggerGhost {
    TriggerType type = TriggerType::LevelStart;
    GoalCondition condition = GoalCondition::DefeatAll;
    uint8_t param = 0;
};

// ── Editor config screen (shown before entering editor) ──
struct EditorConfig {
    enum class Action : uint8_t { NewMap, LoadMap };
    Action action = Action::NewMap;
    int    mapWidth  = 30;
    int    mapHeight = 20;
    std::string mapName    = "Untitled";
    std::string creator    = "Unknown";
    std::string loadPath;        // path to .csm to load
    std::vector<std::string> availableMaps; // scanned .csm files
    int    field     = 0;        // currently selected field
    int    loadIdx   = 0;        // selected map index for loading
    bool   textEditing = false;
    std::string textBuf;
    int    gpCharIdx = 0;        // gamepad char palette index
    int    maxField  = 6;        // 0=action, 1=width, 2=height, 3=name, 4=creator, 5=OK, 6=Cancel
};

class MapEditor {
public:
    bool init(SDL_Renderer* renderer, int screenW, int screenH);
    void shutdown();
    void handleInput(SDL_Event& e);
    void update(float dt);
    void render(SDL_Renderer* renderer);

    // Map operations
    void newMap(int w, int h);
    bool saveMap(const std::string& path);
    bool loadMap(const std::string& path);

    bool isActive() const { return active_; }
    void setActive(bool v) { active_ = v; }

    // Test play — set by editor, checked by Game
    bool wantsTestPlay() const { return wantsTestPlay_; }
    void clearTestPlay() { wantsTestPlay_ = false; }
    CustomMap& getMap() { return map_; }

    // Set map metadata
    void setMapName(const std::string& n) { map_.name = n; }
    void setCreator(const std::string& c) { map_.creator = c; }

    // Config screen
    bool isShowingConfig() const { return showConfig_; }
    void showConfig();    // enter config screen before editing
    EditorConfig& getConfig() { return config_; }

    // Wantsback (user cancelled config)
    bool wantsBack() const { return wantsBack_; }
    void clearWantsBack() { wantsBack_ = false; }

    std::string savePath() const { return savePath_; }

private:
    bool active_ = false;
    SDL_Renderer* renderer_ = nullptr;
    int screenW_ = 1280, screenH_ = 720;

    // Map being edited
    CustomMap map_;
    Camera camera_;

    // Config screen state
    bool showConfig_ = true;     // show config before editing
    bool wantsBack_  = false;    // user cancelled
    EditorConfig config_;
    std::string savePath_ = "maps/editor_map.csm";

    // Palette
    std::vector<EditorTile> palette_;
    int paletteScroll_ = 0;
    int selectedPalette_ = 0;
    std::vector<int> paletteItemY_;  // cached Y positions for click detection

    // Tools
    EditorTool currentTool_ = EditorTool::Tile;
    TriggerGhost triggerGhost_;
    uint8_t enemySpawnType_ = 0; // 0=Melee, 1=Shooter

    // Zoom
    float zoom_ = 1.0f;
    static constexpr float ZOOM_MIN = 0.25f;
    static constexpr float ZOOM_MAX = 4.0f;

    // Trigger/enemy selection & resize
    int selectedTrigger_ = -1;      // index in triggers, -1 = none
    int selectedEnemy_   = -1;      // index in enemySpawns, -1 = none
    bool draggingResize_ = false;   // currently resizing a trigger
    int  resizeCorner_   = -1;      // 0=TL, 1=TR, 2=BL, 3=BR
    float dragStartX_ = 0, dragStartY_ = 0;
    float origTrigW_ = 0, origTrigH_ = 0;
    float origTrigX_ = 0, origTrigY_ = 0;

    // Test play
    bool wantsTestPlay_ = false;

    // Input state
    bool mouseDown_  = false;
    bool rightDown_  = false;
    int  mouseX_     = 0;
    int  mouseY_     = 0;
    bool showGrid_   = true;
    bool showUI_     = true;

    // Gamepad virtual cursor
    float cursorX_ = 640.0f;     // virtual cursor X
    float cursorY_ = 360.0f;     // virtual cursor Y
    bool  useGamepad_ = false;   // gamepad mode active
    float gpCursorSpeed_ = 600.0f; // cursor movement speed

    // Touch state
    bool  touchActive_ = false;
    float touchX_ = 0, touchY_ = 0;

    // Editor UI layout
    static constexpr int PALETTE_W     = 200;
    static constexpr int TOOLBAR_H     = 48;
    static constexpr int TILE_PREVIEW  = 48;

    // Dynamic UI offsets (0 when UI hidden)
    int uiToolbarH() const { return showUI_ ? TOOLBAR_H : 0; }
    int uiPaletteW() const { return showUI_ ? PALETTE_W : 0; }

    // Coordinate helpers (account for zoom + toolbar offset)
    float screenToWorldX(int sx) const { return (float)sx / zoom_ + camera_.pos.x; }
    float screenToWorldY(int sy) const { return (float)(sy - uiToolbarH()) / zoom_ + camera_.pos.y; }
    int   worldToScreenX(float wx) const { return (int)((wx - camera_.pos.x) * zoom_); }
    int   worldToScreenY(float wy) const { return (int)((wy - camera_.pos.y) * zoom_) + uiToolbarH(); }

    // Methods
    void loadPalette();
    void scanTileFolder(const std::string& folder, const std::string& category, uint8_t defaultType);
    void paintTile(int tx, int ty);
    void eraseTile(int tx, int ty);
    void eraseTriggerAt(float wx, float wy);
    void eraseEnemyAt(float wx, float wy);
    void placeTrigger(float wx, float wy);
    void placeEnemy(float wx, float wy);
    int  triggerAt(float wx, float wy) const;
    int  enemyAt(float wx, float wy) const;
    int  triggerResizeHandle(float wx, float wy, int trigIdx) const;
    void renderPalette(SDL_Renderer* renderer);
    void renderToolbar(SDL_Renderer* renderer);
    void renderTriggers(SDL_Renderer* renderer);
    void renderEnemySpawns(SDL_Renderer* renderer);
    void renderGrid(SDL_Renderer* renderer);
    void drawEditorText(SDL_Renderer* renderer, const char* text, int x, int y, int size, SDL_Color color);

    // Generate thumbnail from current map view
    void generateThumbnail();

    // Config screen
    void handleConfigInput(SDL_Event& e);
    void updateConfigGamepad(float dt);
    void renderConfig(SDL_Renderer* renderer);
    void scanAvailableMaps();

    // Gamepad / touch helpers
    void handleGamepadInput(SDL_Event& e);
    void updateGamepadCursor(float dt);
    void handleTouchInput(SDL_Event& e);
    void renderCursor(SDL_Renderer* renderer);
};
