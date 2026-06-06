#include "editor.h"
#include "mod.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#  define cs_stricmp _stricmp
#else
#  include <strings.h>
#  define cs_stricmp strcasecmp
#endif
#ifdef _WIN32
#  include <direct.h>
#  define mkdir(p, m) _mkdir(p)
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace {
// On Switch, A/B and X/Y are physically swapped compared to Xbox layout
inline Uint8 remapButton(Uint8 btn) {
#ifdef __SWITCH__
    switch (btn) {
        case SDL_CONTROLLER_BUTTON_A: return SDL_CONTROLLER_BUTTON_B;
        case SDL_CONTROLLER_BUTTON_B: return SDL_CONTROLLER_BUTTON_A;
        case SDL_CONTROLLER_BUTTON_X: return SDL_CONTROLLER_BUTTON_Y;
        case SDL_CONTROLLER_BUTTON_Y: return SDL_CONTROLLER_BUTTON_X;
        default: return btn;
    }
#else
    return btn;
#endif
}
} // namespace

// Init / Shutdown

bool MapEditor::init(SDL_Renderer* renderer, int screenW, int screenH, UI::Context* ui) {
    renderer_ = renderer;
    ui_       = ui;
    screenW_  = screenW;
    screenH_  = screenH;
    zoom_     = 1.0f;
    cursorX_  = screenW / 2.0f;
    cursorY_  = screenH / 2.0f;

    loadPalette();
    newMap(32, 32); // default map size

    csEditor_.init(renderer, screenW, screenH, ui);
    csEditor_.setLibrary(&csLib_);

    return true;
}

void MapEditor::setScreenSize(int w, int h) {
    screenW_ = w;
    screenH_ = h;
    cursorX_ = std::max(0.0f, std::min(cursorX_, (float)screenW_));
    cursorY_ = std::max(0.0f, std::min(cursorY_, (float)screenH_));
}

void MapEditor::shutdown() {
    for (auto& pt : palette_) {
        if (pt.texture) SDL_DestroyTexture(pt.texture);
    }
    palette_.clear();
}

// Fill out[0..7] with palette textures for TILE_CUSTOM_0..7 (used by test play).
void MapEditor::getCustomTileTextures(SDL_Texture** out) const {
    for (int i = 0; i < 8; i++) out[i] = nullptr;
    for (auto& pt : palette_) {
        int cs = (int)pt.tileType - (int)TILE_CUSTOM_0;
        if (cs >= 0 && cs < 8 && !out[cs])
            out[cs] = pt.texture;
    }
}

// Undo / Redo

void MapEditor::pushUndo() {
    UndoState s;
    s.tiles         = map_.tiles;
    s.ceiling       = map_.ceiling;
    s.tileRotations = map_.tileRotations;
    s.tileNoCollide = map_.tileNoCollide;
    s.triggers      = map_.triggers;
    s.enemySpawns   = map_.enemySpawns;
    s.props         = map_.props;
    undoStack_.push_back(std::move(s));
    if ((int)undoStack_.size() > UNDO_MAX) undoStack_.pop_front();
    redoStack_.clear();
}

void MapEditor::undo() {
    if (undoStack_.empty()) return;
    UndoState cur;
    cur.tiles         = map_.tiles;
    cur.ceiling       = map_.ceiling;
    cur.tileRotations = map_.tileRotations;
    cur.tileNoCollide = map_.tileNoCollide;
    cur.triggers      = map_.triggers;
    cur.enemySpawns   = map_.enemySpawns;
    cur.props         = map_.props;
    redoStack_.push_back(std::move(cur));
    if ((int)redoStack_.size() > UNDO_MAX) redoStack_.pop_front();
    auto& s = undoStack_.back();
    map_.tiles         = s.tiles;
    map_.ceiling       = s.ceiling;
    map_.tileRotations = s.tileRotations;
    map_.tileNoCollide = s.tileNoCollide;
    map_.triggers      = s.triggers;
    map_.enemySpawns   = s.enemySpawns;
    map_.props         = s.props;
    undoStack_.pop_back();
    selectedTrigger_ = -1;
    selectedEnemy_   = -1;
}

void MapEditor::redo() {
    if (redoStack_.empty()) return;
    UndoState cur;
    cur.tiles         = map_.tiles;
    cur.ceiling       = map_.ceiling;
    cur.tileRotations = map_.tileRotations;
    cur.tileNoCollide = map_.tileNoCollide;
    cur.triggers      = map_.triggers;
    cur.enemySpawns   = map_.enemySpawns;
    cur.props         = map_.props;
    undoStack_.push_back(std::move(cur));
    if ((int)undoStack_.size() > UNDO_MAX) undoStack_.pop_front();
    auto& s = redoStack_.back();
    map_.tiles         = s.tiles;
    map_.ceiling       = s.ceiling;
    map_.tileRotations = s.tileRotations;
    map_.tileNoCollide = s.tileNoCollide;
    map_.triggers      = s.triggers;
    map_.enemySpawns   = s.enemySpawns;
    map_.props         = s.props;
    redoStack_.pop_back();
    selectedTrigger_ = -1;
    selectedEnemy_   = -1;
}

// Palette loading

void MapEditor::loadPalette() {
    palette_.clear();

    // Scan structured tile directories for PNG files
    // Each subfolder maps to a category and default tile type
    struct FolderDef { const char* path; const char* category; uint8_t defaultType; };
    FolderDef folders[] = {
        // Custom user tile folders
        {"tiles/ground",  "ground",  TILE_GRASS},
        {"tiles/walls",   "walls",   TILE_WALL},
        {"tiles/ceiling", "ceiling", TILE_GLASS},
        {"tiles/props",   "props",   TILE_DESK},
        // RomFS tile folders (PC build)
        {"romfs/tiles/ground",  "ground",  TILE_GRASS},
        {"romfs/tiles/walls",   "walls",   TILE_WALL},
        {"romfs/tiles/ceiling", "ceiling", TILE_GLASS},
        {"romfs/tiles/props",   "props",   TILE_DESK},
        // Switch romfs: paths
        {"romfs:/tiles/ground",  "ground",  TILE_GRASS},
        {"romfs:/tiles/walls",   "walls",   TILE_WALL},
        {"romfs:/tiles/ceiling", "ceiling", TILE_GLASS},
        {"romfs:/tiles/props",   "props",   TILE_DESK},
    };

    for (auto& fd : folders) {
        scanTileFolder(fd.path, fd.category, fd.defaultType);
    }

    // Ensure basic tiles exist even if folders are empty.
    // Case-insensitive dedup against what scanTileFolder already loaded.
    auto& a = Assets::instance();
    auto tryAdd = [&](const char* name, const char* tilePath, uint8_t type, const char* cat) {
        for (auto& pt : palette_) {
            if (cs_stricmp(pt.name.c_str(), name) == 0) return;
            if (pt.tileType == type)                     return; // same type already covered
        }
        SDL_Texture* t = a.tex(tilePath);
        if (!t) return;
        EditorTile et;
        et.name     = name;
        et.path     = tilePath;
        et.texture  = t;
        et.tileType = type;
        et.category = cat;
        palette_.push_back(et);
    };

    tryAdd("Grass",  "tiles/ground/grass.png",      TILE_GRASS,  "ground");
    tryAdd("Floor",  "tiles/walls/floor.png",       TILE_FLOOR,  "ground");
    tryAdd("Gravel", "tiles/ground/gravel.png",     TILE_GRAVEL, "ground");
    tryAdd("Wood",   "tiles/walls/wood.png",         TILE_WOOD,   "ground");
    tryAdd("Sand",   "tiles/ground/sand.png",        TILE_SAND,   "ground");
    tryAdd("Wall",   "tiles/walls/floor.png",        TILE_WALL,   "walls");
    tryAdd("Glass",  "tiles/ceiling/glasstile.png",  TILE_GLASS,  "ceiling");
    tryAdd("Desk",   "tiles/walls/desk.png",         TILE_DESK,   "props");
    tryAdd("Box",    "tiles/props/box.png",           TILE_BOX,    "props");

    buildTileTextureLookup();
    rebuildFilteredPalette();
    printf("Editor palette: %d tiles loaded\n", (int)palette_.size());
}

// Build a canonical tileType -> texture map for rendering.
// Priority: first entry per type wins (structured folders are scanned first).
void MapEditor::buildTileTextureLookup() {
    memset(tileTextures_, 0, sizeof(tileTextures_));
    for (auto& pt : palette_) {
        if (pt.texture && !tileTextures_[pt.tileType]) {
            tileTextures_[pt.tileType] = pt.texture;
        }
    }
}

void MapEditor::scanTileFolder(const std::string& folder, const std::string& category, uint8_t defaultType) {
    DIR* dir = opendir(folder.c_str());
    if (!dir) return;

    // Filename stem -> tile type lookup
    auto tileTypeFromStem = [](const std::string& s) -> uint8_t {
        if (s == "floor")                           return TILE_FLOOR;
        if (s == "grass")                           return TILE_GRASS;
        if (s == "gravel")                          return TILE_GRAVEL;
        if (s == "wood")                            return TILE_WOOD;
        if (s == "sand")                            return TILE_SAND;
        if (s == "wall")                            return TILE_WALL;
        if (s == "glass" || s == "glasstile")       return TILE_GLASS;
        if (s == "desk")                            return TILE_DESK;
        if (s == "box")                             return TILE_BOX;
        return 0xFF; // unknown
    };

    // Color tiles that should each get their own unique TILE_CUSTOM slot
    auto isColorTile = [](const std::string& s) -> bool {
        return s == "blue" || s == "red" || s == "green" || s == "white" ||
               s == "yellow" || s == "orange" || s == "purple" || s == "cyan" || s == "pink";
    };

    // Skip transition sprites and internal rendering assets
    auto shouldSkip = [](const std::string& s) -> bool {
        // gravel-grass transition variants - not user-placeable tiles
        if (s.size() >= 12 && s.substr(0, 12) == "gravel-grass") return true;
        return false;
    };

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname(entry->d_name);
        if (fname.size() < 5) continue;
        std::string ext = fname.substr(fname.size() - 4);
        if (ext != ".png" && ext != ".PNG") continue;

        std::string displayName = fname.substr(0, fname.size() - 4);

        // Skip internal/transition sprites
        if (shouldSkip(displayName)) continue;

        // Deduplicate by name globally (regardless of category)
        bool exists = false;
        for (auto& pt : palette_) {
            if (pt.name == displayName) { exists = true; break; }
        }
        if (exists) continue;

        std::string fullPath = folder + "/" + fname;
        SDL_Surface* surf = IMG_Load(fullPath.c_str());
        if (!surf) continue;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_FreeSurface(surf);
        if (!tex) continue;

        EditorTile et;
        et.name     = displayName;
        et.path     = fullPath;
        et.texture  = tex;
        et.category = category;

        uint8_t detectedType = tileTypeFromStem(displayName);
        if (detectedType != 0xFF) {
            // For "floor" in the walls folder, use the folder's default type
            if (detectedType == TILE_FLOOR && defaultType == TILE_WALL)
                et.tileType = TILE_WALL;
            else
                et.tileType = detectedType;
        } else if (isColorTile(displayName)) {
            // Color tiles each get their own TILE_CUSTOM slot
            int customSlot = -1;
            for (int cs = 0; cs < 8; cs++) {
                bool used = false;
                for (auto& existing : palette_) {
                    if (existing.tileType == (uint8_t)(TILE_CUSTOM_0 + cs)) { used = true; break; }
                }
                if (!used) { customSlot = cs; break; }
            }
            et.tileType = (customSlot >= 0) ? (uint8_t)(TILE_CUSTOM_0 + customSlot) : defaultType;
        } else {
            // Truly unknown file - assign custom slot
            int customSlot = -1;
            for (int cs = 0; cs < 8; cs++) {
                bool used = false;
                for (auto& existing : palette_) {
                    if (existing.tileType == (uint8_t)(TILE_CUSTOM_0 + cs)) { used = true; break; }
                }
                if (!used) { customSlot = cs; break; }
            }
            et.tileType = (customSlot >= 0) ? (uint8_t)(TILE_CUSTOM_0 + customSlot) : defaultType;
        }
        palette_.push_back(et);
    }
    closedir(dir);
}

void MapEditor::rebuildFilteredPalette() {
    filteredPalette_.clear();
    for (int i = 0; i < (int)palette_.size(); i++) {
        bool match = false;
        switch (paletteTab_) {
            case PaletteTab::All:     match = true; break;
            case PaletteTab::Ground:  match = (palette_[i].category == "ground"); break;
            case PaletteTab::Walls:   match = (palette_[i].category == "walls"); break;
            case PaletteTab::Ceiling: match = (palette_[i].category == "ceiling"); break;
            case PaletteTab::Props:   match = (palette_[i].category == "props"); break;
            default: match = true; break;
        }
        if (match) filteredPalette_.push_back(i);
    }
    if (filteredSelection_ >= (int)filteredPalette_.size())
        filteredSelection_ = std::max(0, (int)filteredPalette_.size() - 1);
    // Update selectedPalette_ from filtered selection
    if (!filteredPalette_.empty() && filteredSelection_ >= 0)
        selectedPalette_ = filteredPalette_[filteredSelection_];
}

// Palette scroll helpers

int MapEditor::paletteContentHeight() const {
    if (palette_.empty()) return 0;
    int h = 10;  // initial top padding
    std::string lastCat;
    for (auto& pt : palette_) {
        if (pt.category != lastCat) { lastCat = pt.category; h += 20; }
        h += TILE_PREVIEW + 6;
    }
    return h + 10;  // bottom padding
}

int MapEditor::paletteItemRawY(int idx) const {
    int y = TOOLBAR_H + 10;
    std::string lastCat;
    for (int i = 0; i < (int)palette_.size(); i++) {
        if (palette_[i].category != lastCat) { lastCat = palette_[i].category; y += 20; }
        if (i == idx) return y;
        y += TILE_PREVIEW + 6;
    }
    return y;
}

void MapEditor::scrollPaletteToSelection() {
    if (selectedPalette_ < 0 || selectedPalette_ >= (int)palette_.size()) return;
    int rawY   = paletteItemRawY(selectedPalette_);
    int viewH  = screenH_ - TOOLBAR_H;
    // Scroll down if item is below the visible area
    if (rawY - paletteScroll_ + TILE_PREVIEW > screenH_ - 10)
        paletteScroll_ = rawY + TILE_PREVIEW - screenH_ + 10;
    // Scroll up if item is above the visible area
    if (rawY - paletteScroll_ < TOOLBAR_H + 4)
        paletteScroll_ = rawY - TOOLBAR_H - 4;
    if (paletteScroll_ < 0) paletteScroll_ = 0;
    int maxScroll = paletteContentHeight() - viewH;
    if (maxScroll > 0 && paletteScroll_ > maxScroll) paletteScroll_ = maxScroll;
}

// Map operations

void MapEditor::newMap(int w, int h) {
    map_.width  = w;
    map_.height = h;
    map_.tiles.assign(w * h, TILE_GRASS);
    map_.ceiling.assign(w * h, CEIL_NONE);
    map_.tileRotations.assign(w * h, 0);
    map_.tileNoCollide.assign(w * h, 0);
    map_.triggers.clear();
    map_.enemySpawns.clear();
    map_.props.clear();
    map_.name    = "Untitled";
    map_.creator = "Unknown";

    for (int x = 0; x < w; x++) {
        map_.tiles[0 * w + x] = TILE_WALL;
        map_.tiles[(h-1) * w + x] = TILE_WALL;
    }
    for (int y = 0; y < h; y++) {
        map_.tiles[y * w + 0] = TILE_WALL;
        map_.tiles[y * w + (w-1)] = TILE_WALL;
    }

    MapTrigger start;
    start.type = TriggerType::LevelStart;
    start.x = (w / 2.0f) * TILE_SIZE;
    start.y = (h / 2.0f) * TILE_SIZE;
    start.width  = TILE_SIZE * 2;
    start.height = TILE_SIZE * 2;
    start.condition = GoalCondition::Immediate;
    start.param = 0;
    memset(start.reserved, 0, sizeof(start.reserved));
    map_.triggers.push_back(start);

    camera_.pos = {0, 0};
    camera_.worldW = w * TILE_SIZE;
    camera_.worldH = h * TILE_SIZE;
    selectedTrigger_ = -1;
    selectedEnemy_   = -1;
    undoStack_.clear();
    redoStack_.clear();
    undoPushedForStroke_ = false;
    hasExplicitSavePath_ = false;
    savePath_ = "maps/editor_map.csm";
}

bool MapEditor::saveMap(const std::string& path) {
    // Normalize custom tile paths: strip "romfs:/" or "romfs/" prefix so
    // Assets::tex() can prepend the correct platform prefix at load time.
    auto normPath = [](const std::string& p) -> std::string {
        if (p.size() > 7 && p.substr(0, 7) == "romfs:/") return p.substr(7);
        if (p.size() > 6 && p.substr(0, 6) == "romfs/") return p.substr(6);
        return p;
    };
    // Record custom tile paths (for any TILE_CUSTOM_N types used from palette)
    for (int i = 0; i < 8; i++) map_.customTilePaths[i] = "";
    for (auto& pt : palette_) {
        int cs = (int)pt.tileType - (int)TILE_CUSTOM_0;
        if (cs >= 0 && cs < 8) map_.customTilePaths[cs] = normPath(pt.path);
    }
    generateThumbnail();
    bool ok = map_.saveToFile(path);
    if (ok) {
        saveMessage_ = "Saved to " + path;
        saveCutsceneLib();
    } else {
        saveMessage_ = "Save failed!";
    }
    saveMessageTimer_ = 2.5f;
    return ok;
}

void MapEditor::saveCutsceneLib() {
    if (savePath_.empty()) return;
    std::string cscPath = savePath_;
    size_t dot = cscPath.rfind('.');
    if (dot != std::string::npos) cscPath = cscPath.substr(0, dot);
    cscPath += ".csc";
    if (!csLib_.cutscenes.empty())
        csLib_.save(cscPath);
}

void MapEditor::loadCutsceneLib() {
    if (savePath_.empty()) return;
    std::string cscPath = savePath_;
    size_t dot = cscPath.rfind('.');
    if (dot != std::string::npos) cscPath = cscPath.substr(0, dot);
    cscPath += ".csc";
    csLib_.load(cscPath); // fails silently if file doesn't exist yet
}

void MapEditor::performModSave(const std::string& modFolder) {
    // Build a safe filename from the current map name
    std::string safeName = map_.name;
    for (char& c : safeName) {
        if (c == ' ') c = '_';
        if (c == '/' || c == '\\') c = '_';
    }
    if (safeName.empty()) safeName = "untitled";

    std::string mapsDir = modFolder + "/maps";
    mkdir(mapsDir.c_str(), 0755);
    savePath_ = mapsDir + "/" + safeName + ".csm";
    hasExplicitSavePath_ = true;
    saveMap(savePath_);
}

bool MapEditor::loadMap(const std::string& path) {
    if (!map_.loadFromFile(path)) return false;
    // Ensure parallel arrays match tile count (older maps may be missing them)
    map_.tileRotations.resize(map_.tiles.size(), 0);
    map_.tileNoCollide.resize(map_.tiles.size(), 0);
    camera_.pos = {0, 0};
    camera_.worldW = map_.width * TILE_SIZE;
    camera_.worldH = map_.height * TILE_SIZE;
    savePath_ = path;
    hasExplicitSavePath_ = true;
    selectedTrigger_ = -1;
    selectedEnemy_   = -1;
    undoStack_.clear();
    redoStack_.clear();
    undoPushedForStroke_ = false;
    loadCutsceneLib();

    // Auto-detect layer images from map filename when not stored in CSM
    {
        std::string base = path;
        size_t sl = base.find_last_of("/\\");
        if (sl != std::string::npos) base = base.substr(sl + 1);
        size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        if (map_.bgImagePath.empty()) {
            std::string cand = "sprites/" + base + ".png";
            if (Assets::instance().loadRelTex(cand)) map_.bgImagePath = cand;
        }
        if (map_.topImagePath.empty()) {
            std::string cand = "sprites/" + base + "top.png";
            if (Assets::instance().loadRelTex(cand)) map_.topImagePath = cand;
        }
    }

    return true;
}

// Hit-testing helpers

int MapEditor::triggerAt(float wx, float wy) const {
    for (int i = (int)map_.triggers.size() - 1; i >= 0; i--) {
        auto& t = map_.triggers[i];
        if (wx >= t.x - t.width/2 && wx <= t.x + t.width/2 &&
            wy >= t.y - t.height/2 && wy <= t.y + t.height/2) {
            return i;
        }
    }
    return -1;
}

int MapEditor::enemyAt(float wx, float wy) const {
    for (int i = (int)map_.enemySpawns.size() - 1; i >= 0; i--) {
        auto& es = map_.enemySpawns[i];
        float sz = 24.0f;
        if (wx >= es.x - sz && wx <= es.x + sz &&
            wy >= es.y - sz && wy <= es.y + sz) {
            return i;
        }
    }
    return -1;
}

int MapEditor::triggerResizeHandle(float wx, float wy, int trigIdx) const {
    if (trigIdx < 0 || trigIdx >= (int)map_.triggers.size()) return -1;
    auto& t = map_.triggers[trigIdx];
    float hw = t.width / 2, hh = t.height / 2;
    float handleSz = 12.0f / zoom_;  // handle hit area scales with zoom

    // Check 4 corners: TL=0, TR=1, BL=2, BR=3
    float cx[4] = {t.x - hw, t.x + hw, t.x - hw, t.x + hw};
    float cy[4] = {t.y - hh, t.y - hh, t.y + hh, t.y + hh};
    for (int i = 0; i < 4; i++) {
        if (fabsf(wx - cx[i]) < handleSz && fabsf(wy - cy[i]) < handleSz)
            return i;
    }
    return -1;
}

// Input

void MapEditor::handleInput(SDL_Event& e) {
    if (!active_) return;

    // Config screen intercepts all input
    if (showConfig_) {
        handleConfigInput(e);
        return;
    }

    // Cutscene editor intercepts events when panel is open
    if (showCutsceneEditor_) {
        csEditor_.handleEvent(e, zoom_, camera_.pos.x, camera_.pos.y);
        // If the click landed in the cutscene panel, don't let the map editor consume it
        if (e.type == SDL_MOUSEBUTTONDOWN) {
            int my = e.button.y;
            if (my >= screenH_ - CS_EDITOR_PANEL_H) return;
        }
    }

    // Touch events
    if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP ||
        e.type == SDL_FINGERMOTION || e.type == SDL_MULTIGESTURE) {
        handleTouchInput(e);
        return;
    }

    // Gamepad events
    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
        handleGamepadInput(e);
        return;
    }

    // Mouse overrides gamepad cursor
    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN) {
        useGamepad_ = false;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN) {
        if (e.button.button == SDL_BUTTON_LEFT)  mouseDown_ = true;
        if (e.button.button == SDL_BUTTON_RIGHT) rightDown_ = true;

        mouseX_ = e.button.x;
        mouseY_ = e.button.y;

        // Toolbar click detection
        if (e.button.button == SDL_BUTTON_LEFT && showUI_ && mouseY_ < TOOLBAR_H) {
            for (int i = 0; i < 6; i++) {
                int bx = 4 + i * 80;
                if (mouseX_ >= bx && mouseX_ < bx + 76 && mouseY_ >= 4 && mouseY_ < TOOLBAR_H - 4) {
                    currentTool_ = (EditorTool)i;
                    selectedTrigger_ = -1;
                    selectedEnemy_   = -1;
                    rectStartTX_ = -1; rectStartTY_ = -1;
                    break;
                }
            }
            // Save button
            int saveBx = screenW_ - PALETTE_W - 230;
            if (mouseX_ >= saveBx && mouseX_ < saveBx + 106 && mouseY_ >= 6 && mouseY_ < TOOLBAR_H - 6) {
                wantsModSave_ = true;
            }
            // Test Play button
            int playBx = screenW_ - PALETTE_W - 118;
            if (mouseX_ >= playBx && mouseX_ < playBx + 106 && mouseY_ >= 6 && mouseY_ < TOOLBAR_H - 6) {
                wantsTestPlay_ = true;
            }
            mouseDown_ = false;  // consume click
            return;
        }

        // Palette click
        if (e.button.button == SDL_BUTTON_LEFT && showUI_ && mouseX_ >= screenW_ - PALETTE_W) {
            int clickY = mouseY_;
            // Find which palette item was clicked using cached Y positions
            for (int i = 0; i < (int)paletteItemY_.size() && i < (int)palette_.size(); i++) {
                // paletteItemY_ stores the actual screen Y where each item was rendered
                if (clickY >= paletteItemY_[i] && clickY < paletteItemY_[i] + TILE_PREVIEW) {
                    selectedPalette_ = i;
                    break;
                }
            }
            mouseDown_ = false;
            return;
        }

        // Grab ball drag start (Select tool, something selected)
        if (e.button.button == SDL_BUTTON_LEFT && currentTool_ == EditorTool::Select &&
            (selectedTrigger_ >= 0 || selectedEnemy_ >= 0)) {
            int cx, cy;
            if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
                cx = worldToScreenX(map_.enemySpawns[selectedEnemy_].x);
                cy = worldToScreenY(map_.enemySpawns[selectedEnemy_].y);
            } else if (selectedTrigger_ < (int)map_.triggers.size()) {
                cx = worldToScreenX(map_.triggers[selectedTrigger_].x);
                cy = worldToScreenY(map_.triggers[selectedTrigger_].y);
            } else { cx = cy = -1; }

            const int ballR = 9;
            int bdx = mouseX_ - cx, bdy = mouseY_ - cy;
            if (bdx*bdx + bdy*bdy <= ballR*ballR) {
                pushUndo();
                draggingMove_ = true;
                moveDragSX_ = mouseX_; moveDragSY_ = mouseY_;
                if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
                    moveObjOrigX_ = map_.enemySpawns[selectedEnemy_].x;
                    moveObjOrigY_ = map_.enemySpawns[selectedEnemy_].y;
                } else if (selectedTrigger_ < (int)map_.triggers.size()) {
                    moveObjOrigX_ = map_.triggers[selectedTrigger_].x;
                    moveObjOrigY_ = map_.triggers[selectedTrigger_].y;
                }
                mouseDown_ = false;
                return;
            }
        }

        // Context/properties panel (left sidebar) blocks map interaction
        if (e.button.button == SDL_BUTTON_LEFT && showUI_ &&
            mouseX_ < 8 + 220 && mouseY_ >= uiToolbarH()) {
            mouseDown_ = false;
            return;
        }

        // Map area click: Select tool handles trigger selection + resize start
        if (e.button.button == SDL_BUTTON_LEFT && currentTool_ == EditorTool::Select &&
            mouseX_ < screenW_ - uiPaletteW() && mouseY_ > uiToolbarH()) {
            float wx = screenToWorldX(mouseX_);
            float wy = screenToWorldY(mouseY_);

            // Check if clicking a resize handle on already selected trigger
            if (selectedTrigger_ >= 0) {
                int handle = triggerResizeHandle(wx, wy, selectedTrigger_);
                if (handle >= 0) {
                    pushUndo();
                    draggingResize_ = true;
                    resizeCorner_ = handle;
                    dragStartX_ = wx;
                    dragStartY_ = wy;
                    origTrigW_ = map_.triggers[selectedTrigger_].width;
                    origTrigH_ = map_.triggers[selectedTrigger_].height;
                    origTrigX_ = map_.triggers[selectedTrigger_].x;
                    origTrigY_ = map_.triggers[selectedTrigger_].y;
                    mouseDown_ = false;
                    return;
                }
            }

            // Try selecting a trigger
            int ti = triggerAt(wx, wy);
            if (ti >= 0) {
                selectedTrigger_ = ti;
                selectedEnemy_ = -1;
                mouseDown_ = false;
                return;
            }
            // Try selecting an enemy
            int ei = enemyAt(wx, wy);
            if (ei >= 0) {
                selectedEnemy_ = ei;
                selectedTrigger_ = -1;
                mouseDown_ = false;
                return;
            }
        }

        // Rect tool: record start tile on mouse down
        if (e.button.button == SDL_BUTTON_LEFT && currentTool_ == EditorTool::Rect &&
            mouseX_ < screenW_ - uiPaletteW() && mouseY_ > uiToolbarH()) {
            float wx2 = screenToWorldX(mouseX_);
            float wy2 = screenToWorldY(mouseY_);
            rectStartTX_ = (int)(wx2 / TILE_SIZE);
            rectStartTY_ = (int)(wy2 / TILE_SIZE);
        }

        // Trigger tool: record drag start in world coords (no tile snap)
        if (e.button.button == SDL_BUTTON_LEFT && currentTool_ == EditorTool::Trigger &&
            mouseX_ < screenW_ - uiPaletteW() && mouseY_ > uiToolbarH()) {
            trigDragStartX_ = screenToWorldX(mouseX_);
            trigDragStartY_ = screenToWorldY(mouseY_);
            trigDragging_ = true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            // Rect tool: fill the rectangle on mouse release
            if (currentTool_ == EditorTool::Rect && rectStartTX_ >= 0 &&
                e.button.x < screenW_ - uiPaletteW() && e.button.y > uiToolbarH()) {
                float wx2 = screenToWorldX(e.button.x);
                float wy2 = screenToWorldY(e.button.y);
                int endTX = (int)(wx2 / TILE_SIZE);
                int endTY = (int)(wy2 / TILE_SIZE);
                int x0 = std::min(rectStartTX_, endTX), x1 = std::max(rectStartTX_, endTX);
                int y0 = std::min(rectStartTY_, endTY), y1 = std::max(rectStartTY_, endTY);
                pushUndo();
                for (int ry = y0; ry <= y1; ry++) {
                    for (int rx = x0; rx <= x1; rx++) {
                        if (rectFilled_ || ry == y0 || ry == y1 || rx == x0 || rx == x1)
                            paintTile(rx, ry);
                    }
                }
            }
            rectStartTX_ = -1; rectStartTY_ = -1;

            // Trigger tool: place trigger from drag rect on release
            if (currentTool_ == EditorTool::Trigger && trigDragging_) {
                float wx2 = screenToWorldX(e.button.x);
                float wy2 = screenToWorldY(e.button.y);
                float x0 = std::min(trigDragStartX_, wx2);
                float y0 = std::min(trigDragStartY_, wy2);
                float w  = std::max((float)(TILE_SIZE / 4), std::abs(wx2 - trigDragStartX_));
                float h  = std::max((float)(TILE_SIZE / 4), std::abs(wy2 - trigDragStartY_));
                pushUndo();
                MapTrigger t{};
                t.type      = triggerGhost_.type;
                t.x         = x0 + w * 0.5f; // store center
                t.y         = y0 + h * 0.5f;
                t.width     = w;
                t.height    = h;
                t.condition = triggerGhost_.condition;
                t.param     = triggerGhost_.param;
                map_.triggers.push_back(t);
            }
            trigDragging_ = false;

            mouseDown_ = false; draggingResize_ = false; draggingMove_ = false;
            undoPushedForStroke_ = false;
        }
        if (e.button.button == SDL_BUTTON_RIGHT) {
            rightDown_ = false;
            undoPushedForStroke_ = false;
        }
    }

    if (e.type == SDL_MOUSEMOTION) {
        mouseX_ = e.motion.x;
        mouseY_ = e.motion.y;

        // Move drag
        if (draggingMove_) {
            float wdx = (mouseX_ - moveDragSX_) / zoom_;
            float wdy = (mouseY_ - moveDragSY_) / zoom_;
            if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
                map_.enemySpawns[selectedEnemy_].x = moveObjOrigX_ + wdx;
                map_.enemySpawns[selectedEnemy_].y = moveObjOrigY_ + wdy;
            } else if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
                map_.triggers[selectedTrigger_].x = moveObjOrigX_ + wdx;
                map_.triggers[selectedTrigger_].y = moveObjOrigY_ + wdy;
            }
        }

        // Resize drag
        if (draggingResize_ && selectedTrigger_ >= 0) {
            float wx = screenToWorldX(mouseX_);
            float wy = screenToWorldY(mouseY_);
            float dx = wx - dragStartX_;
            float dy = wy - dragStartY_;
            auto& t = map_.triggers[selectedTrigger_];

            // Resize based on which corner is being dragged
            float minSz = (float)(TILE_SIZE / 4);
            switch (resizeCorner_) {
            case 0: // Top-Left: shrink from TL
                t.width  = fmaxf(minSz, origTrigW_ - dx);
                t.height = fmaxf(minSz, origTrigH_ - dy);
                t.x = origTrigX_ + (origTrigW_ - t.width) / 2;
                t.y = origTrigY_ + (origTrigH_ - t.height) / 2;
                break;
            case 1: // Top-Right
                t.width  = fmaxf(minSz, origTrigW_ + dx);
                t.height = fmaxf(minSz, origTrigH_ - dy);
                t.x = origTrigX_ + (t.width - origTrigW_) / 2;
                t.y = origTrigY_ + (origTrigH_ - t.height) / 2;
                break;
            case 2: // Bottom-Left
                t.width  = fmaxf(minSz, origTrigW_ - dx);
                t.height = fmaxf(minSz, origTrigH_ + dy);
                t.x = origTrigX_ + (origTrigW_ - t.width) / 2;
                t.y = origTrigY_ + (t.height - origTrigH_) / 2;
                break;
            case 3: // Bottom-Right
                t.width  = fmaxf(minSz, origTrigW_ + dx);
                t.height = fmaxf(minSz, origTrigH_ + dy);
                t.x = origTrigX_ + (t.width - origTrigW_) / 2;
                t.y = origTrigY_ + (t.height - origTrigH_) / 2;
                break;
            }
        }

        // Camera pan with middle mouse
        if (e.motion.state & SDL_BUTTON_MMASK) {
            camera_.pos.x -= e.motion.xrel / zoom_;
            camera_.pos.y -= e.motion.yrel / zoom_;
        }
    }

    if (e.type == SDL_MOUSEWHEEL) {
        if (showUI_ && mouseX_ >= screenW_ - PALETTE_W) {
            // Palette scroll
            paletteScroll_ -= e.wheel.y * 30;
            if (paletteScroll_ < 0) paletteScroll_ = 0;
            // Clamp to max
            int maxScroll = paletteContentHeight() - (screenH_ - TOOLBAR_H);
            if (maxScroll > 0 && paletteScroll_ > maxScroll) paletteScroll_ = maxScroll;
        } else {
            // Zoom in/out
            float oldZoom = zoom_;
            zoom_ *= (e.wheel.y > 0) ? 1.15f : (1.0f / 1.15f);
            zoom_ = fmaxf(ZOOM_MIN, fminf(ZOOM_MAX, zoom_));

            // Zoom toward mouse cursor
            float wx = (float)mouseX_ / oldZoom + camera_.pos.x;
            float wy = (float)(mouseY_ - uiToolbarH()) / oldZoom + camera_.pos.y;
            camera_.pos.x = wx - (float)mouseX_ / zoom_;
            camera_.pos.y = wy - (float)(mouseY_ - uiToolbarH()) / zoom_;
        }
    }

    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        switch (e.key.keysym.sym) {
            case SDLK_1: currentTool_ = EditorTool::Tile; break;
            case SDLK_2: currentTool_ = EditorTool::Trigger; break;
            case SDLK_3: currentTool_ = EditorTool::Entity; break;
            case SDLK_4: currentTool_ = EditorTool::Erase; break;
            case SDLK_5: currentTool_ = EditorTool::Select; break;
            case SDLK_g: showGrid_ = !showGrid_; break;
            case SDLK_TAB: showUI_ = !showUI_; break;

            case SDLK_F5: wantsTestPlay_ = true; break;  // Test play

            case SDLK_z:
                if (SDL_GetModState() & KMOD_CTRL) {
                    if (SDL_GetModState() & KMOD_SHIFT) redo();
                    else undo();
                }
                break;
            case SDLK_y:
                if (SDL_GetModState() & KMOD_CTRL) redo();
                break;
            case SDLK_DELETE:
            case SDLK_BACKSPACE: {
                bool deleted = false;
                if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
                    if (!deleted) { pushUndo(); deleted = true; }
                    map_.triggers.erase(map_.triggers.begin() + selectedTrigger_);
                    selectedTrigger_ = -1;
                }
                if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
                    if (!deleted) { pushUndo(); deleted = true; }
                    map_.enemySpawns.erase(map_.enemySpawns.begin() + selectedEnemy_);
                    selectedEnemy_ = -1;
                }
                break;
            }

            case SDLK_q: {
                // Rotate selected CollisionZone trigger CCW by 15°
                if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
                    auto& t = map_.triggers[selectedTrigger_];
                    if (t.type == TriggerType::CollisionZone) {
                        uint16_t deg10 = (uint16_t)(t.reserved[0] | (t.reserved[1] << 8));
                        deg10 = (uint16_t)((deg10 - 150 + 3600) % 3600);
                        t.reserved[0] = deg10 & 0xFF;
                        t.reserved[1] = (deg10 >> 8) & 0xFF;
                    }
                }
                break;
            }

            case SDLK_t:
                if (currentTool_ == EditorTool::Trigger) {
                    static const TriggerType kValidTypes[] = {
                        TriggerType::LevelStart,
                        TriggerType::LevelEnd,
                        TriggerType::Crate,
                        TriggerType::Effect,
                        TriggerType::TeamSpawnRed,
                        TriggerType::TeamSpawnBlue,
                        TriggerType::TeamSpawnGreen,
                        TriggerType::TeamSpawnYellow,
                        TriggerType::LayerFade,
                        TriggerType::CollisionZone,
                    };
                    static const int kTypeCount = 10;
                    int cur = 0;
                    for (int i = 0; i < kTypeCount; i++) {
                        if (kValidTypes[i] == triggerGhost_.type) { cur = i; break; }
                    }
                    triggerGhost_.type = kValidTypes[(cur + 1) % kTypeCount];
                }
                break;
            case SDLK_c:
                if (currentTool_ == EditorTool::Trigger && triggerGhost_.type == TriggerType::LevelEnd) {
                    int v = (int)triggerGhost_.condition + 1;
                    if (v > 2) v = 0;
                    triggerGhost_.condition = (GoalCondition)v;
                }
                break;
            case SDLK_e:
                if (currentTool_ == EditorTool::Entity) {
                    entitySpawnType_ = (entitySpawnType_ + 1) % ENTITY_TYPE_COUNT;
                }
                // Rotate selected CollisionZone CW by 15°
                if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
                    auto& t = map_.triggers[selectedTrigger_];
                    if (t.type == TriggerType::CollisionZone) {
                        uint16_t deg10 = (uint16_t)(t.reserved[0] | (t.reserved[1] << 8));
                        deg10 = (uint16_t)((deg10 + 150) % 3600);
                        t.reserved[0] = deg10 & 0xFF;
                        t.reserved[1] = (deg10 >> 8) & 0xFF;
                    }
                }
                break;
            case SDLK_LEFTBRACKET:  // [ decrease brush size
                brushSize_--; if (brushSize_ < 1) brushSize_ = 1; break;
            case SDLK_RIGHTBRACKET: // ] increase brush size
                brushSize_++; if (brushSize_ > 9) brushSize_ = 9; break;
            case SDLK_f:  // toggle rect fill mode
                if (currentTool_ == EditorTool::Rect || currentTool_ == EditorTool::Tile)
                    rectFilled_ = !rectFilled_;
                break;
            case SDLK_s:
                if (SDL_GetModState() & KMOD_CTRL) {
                    wantsModSave_ = true;
                }
                break;
        }
    }
}

// Update

void MapEditor::update(float dt) {
    if (!active_) return;
    if (showConfig_) return;

    // Tick timers
    if (saveMessageTimer_ > 0) saveMessageTimer_ -= dt;

    // Cutscene editor
    if (showCutsceneEditor_) csEditor_.update(dt);

    // Update gamepad virtual cursor
    updateGamepadCursor(dt);

    // Camera pan with arrow keys or Shift+WASD
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float panSpeed = 500.0f / zoom_ * dt;
    if (keys[SDL_SCANCODE_UP]    || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_W])) camera_.pos.y -= panSpeed;
    if (keys[SDL_SCANCODE_DOWN]  || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_S])) camera_.pos.y += panSpeed;
    if (keys[SDL_SCANCODE_LEFT]  || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_A])) camera_.pos.x -= panSpeed;
    if (keys[SDL_SCANCODE_RIGHT] || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_D])) camera_.pos.x += panSpeed;

    // Reset undo stroke flag when no buttons held
    if (!mouseDown_ && !rightDown_) undoPushedForStroke_ = false;

    // Paint/erase on mouse hold (only in map area, not over any UI panel)
    const bool overLeftPanel = showUI_ && mouseX_ < 8 + 220 && mouseY_ >= uiToolbarH();
    if (!draggingResize_ && !draggingMove_ && !overLeftPanel &&
        mouseX_ < screenW_ - uiPaletteW() && mouseY_ > uiToolbarH()) {
        float wx = screenToWorldX(mouseX_);
        float wy = screenToWorldY(mouseY_);
        int tx = (int)(wx / TILE_SIZE);
        int ty = (int)(wy / TILE_SIZE);

        // Determine if selected palette entry is a free-placed prop
        bool isFreeProp = false;
        if (selectedPalette_ >= 0 && selectedPalette_ < (int)palette_.size())
            isFreeProp = (palette_[selectedPalette_].category == "props");

        if (mouseDown_) {
            switch (currentTool_) {
                case EditorTool::Tile: {
                    if (!undoPushedForStroke_) { pushUndo(); undoPushedForStroke_ = true; }
                    if (isFreeProp) {
                        // Place free prop at world coords on click (not held drag)
                        auto& pt = palette_[selectedPalette_];
                        PropSpawn ps;
                        ps.x = wx; ps.y = wy;
                        ps.tileType = pt.tileType;
                        ps.rotation = randomRotation_ ? (uint8_t)(rand() % 4) : 0;
                        map_.props.push_back(ps);
                        mouseDown_ = false; // single placement per click
                    } else {
                        int half = (brushSize_ - 1) / 2;
                        for (int dy = -half; dy <= half; dy++)
                            for (int dx = -half; dx <= half; dx++)
                                paintTile(tx + dx, ty + dy);
                    }
                    break;
                }
                case EditorTool::Erase: {
                    if (!undoPushedForStroke_) { pushUndo(); undoPushedForStroke_ = true; }
                    int half = (brushSize_ - 1) / 2;
                    for (int dy = -half; dy <= half; dy++)
                        for (int dx = -half; dx <= half; dx++)
                            eraseTile(tx + dx, ty + dy);
                    eraseTriggerAt(wx, wy);
                    eraseEnemyAt(wx, wy);
                    break;
                }
                case EditorTool::Trigger:
                    /* drag-to-place: handled on mouseUp */
                    break;
                case EditorTool::Entity:
                    pushUndo();
                    placeEnemy(wx, wy);
                    mouseDown_ = false;
                    break;
                case EditorTool::Rect:    /* paint on mouseUp */ break;
                default: break;
            }
        }
        if (rightDown_) {
            if (!undoPushedForStroke_) { pushUndo(); undoPushedForStroke_ = true; }
            // Right-click erases tiles, triggers, enemies, and free props
            if (!isFreeProp) eraseTile(tx, ty);
            eraseTriggerAt(wx, wy);
            eraseEnemyAt(wx, wy);
            erasePropAt(wx, wy);
            rightDown_ = false;  // single action
        }
    }
}

void MapEditor::paintTile(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= map_.width || ty >= map_.height) return;
    if (selectedPalette_ >= 0 && selectedPalette_ < (int)palette_.size()) {
        auto& pt = palette_[selectedPalette_];
        int idx = ty * map_.width + tx;
        uint8_t rot = randomRotation_ ? (uint8_t)(rand() % 4) : 0;
        if (pt.category == "ceiling") {
            map_.ceiling[idx] = CEIL_GLASS;
        } else {
            map_.tiles[idx] = pt.tileType;
            if ((int)map_.tileRotations.size() > idx)
                map_.tileRotations[idx] = rot;
            if ((int)map_.tileNoCollide.size() > idx)
                map_.tileNoCollide[idx] = noCollision_ ? 1 : 0;
        }
    }
}

void MapEditor::eraseTile(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= map_.width || ty >= map_.height) return;
    map_.tiles[ty * map_.width + tx] = TILE_GRASS;
    map_.ceiling[ty * map_.width + tx] = CEIL_NONE;
}

void MapEditor::eraseTriggerAt(float wx, float wy) {
    int idx = triggerAt(wx, wy);
    if (idx >= 0) {
        map_.triggers.erase(map_.triggers.begin() + idx);
        if (selectedTrigger_ == idx) selectedTrigger_ = -1;
        else if (selectedTrigger_ > idx) selectedTrigger_--;
    }
}

void MapEditor::eraseEnemyAt(float wx, float wy) {
    int idx = enemyAt(wx, wy);
    if (idx >= 0) {
        map_.enemySpawns.erase(map_.enemySpawns.begin() + idx);
        if (selectedEnemy_ == idx) selectedEnemy_ = -1;
        else if (selectedEnemy_ > idx) selectedEnemy_--;
    }
}

void MapEditor::erasePropAt(float wx, float wy) {
    const float r = (float)TILE_SIZE * 0.6f;
    for (int i = (int)map_.props.size() - 1; i >= 0; i--) {
        auto& p = map_.props[i];
        if (fabsf(p.x - wx) < r && fabsf(p.y - wy) < r) {
            map_.props.erase(map_.props.begin() + i);
            return;
        }
    }
}

void MapEditor::placeTrigger(float wx, float wy) {
    MapTrigger t;
    t.type = triggerGhost_.type;
    t.x = wx;
    t.y = wy;
    t.width  = TILE_SIZE * 2;
    t.height = TILE_SIZE * 2;
    t.condition = triggerGhost_.condition;
    t.param = triggerGhost_.param;
    memset(t.reserved, 0, sizeof(t.reserved));
    map_.triggers.push_back(t);
}

void MapEditor::placeEnemy(float wx, float wy) {
    EnemySpawn es;
    es.x = wx;
    es.y = wy;
    es.enemyType  = entitySpawnType_;
    es.waveGroup  = 0;
    memset(es.reserved, 0, sizeof(es.reserved));
    map_.enemySpawns.push_back(es);
}

// Rendering (all coordinates use zoom_)

void MapEditor::render(SDL_Renderer* renderer) {
    if (!active_) return;

    // Show config screen instead of editor
    if (showConfig_) {
        renderConfig(renderer);
        return;
    }

    SDL_SetRenderDrawColor(renderer, 18, 20, 30, 255);
    SDL_RenderClear(renderer);

    float ts = TILE_SIZE * zoom_;

    // Background image layer (if map uses one)
    if (!map_.bgImagePath.empty()) {
        SDL_Texture* bgTex = Assets::instance().loadRelTex(map_.bgImagePath);
        if (bgTex) {
            int sx = worldToScreenX(0.0f);
            int sy = worldToScreenY(0.0f);
            int sw = (int)(map_.width  * TILE_SIZE * zoom_);
            int sh = (int)(map_.height * TILE_SIZE * zoom_);
            SDL_Rect dst = {sx, sy, sw, sh};
            SDL_SetTextureBlendMode(bgTex, SDL_BLENDMODE_NONE);
            SDL_RenderCopy(renderer, bgTex, nullptr, &dst);
        }
    }

    // Map tiles
    int startX = (int)(camera_.pos.x / TILE_SIZE);
    int startY = (int)(camera_.pos.y / TILE_SIZE);
    int tilesAcross = (int)((screenW_ - uiPaletteW()) / ts) + 2;
    int tilesDown   = (int)((screenH_ - uiToolbarH()) / ts) + 2;
    int endX = startX + tilesAcross;
    int endY = startY + tilesDown;
    startX = std::max(0, startX);
    startY = std::max(0, startY);
    endX   = std::min(map_.width, endX);
    endY   = std::min(map_.height, endY);

    bool hasBgImage = !map_.bgImagePath.empty();
    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            int idx = y * map_.width + x;
            uint8_t tile = map_.tiles[idx];
            uint8_t rot  = (idx < (int)map_.tileRotations.size()) ? map_.tileRotations[idx] : 0;
            int sx = worldToScreenX((float)(x * TILE_SIZE));
            int sy = worldToScreenY((float)(y * TILE_SIZE));
            SDL_Rect dst = {sx, sy, (int)ceilf(ts), (int)ceilf(ts)};

            bool isSolid = (tile >= TILE_WALL && tile <= TILE_BOX);

            // When a background image covers the map, only show solid tiles
            // (walls/desks/boxes) as semi-transparent overlays for editing reference.
            if (hasBgImage && !isSolid) {
                // Skip non-solid tiles - the bg image is the floor visual
                if (map_.ceiling[idx] == CEIL_GLASS) {
                    SDL_SetRenderDrawColor(renderer, 100, 160, 220, 30);
                    SDL_RenderFillRect(renderer, &dst);
                }
                continue;
            }

            SDL_Texture* tex = tileTextures_[tile];
            if (tex) {
                if (hasBgImage) SDL_SetTextureAlphaMod(tex, 160);
                double angle = rot * 90.0;
                if (angle == 0.0)
                    SDL_RenderCopy(renderer, tex, nullptr, &dst);
                else
                    SDL_RenderCopyEx(renderer, tex, nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
                if (hasBgImage) SDL_SetTextureAlphaMod(tex, 255);
            } else {
                SDL_Color c = {60, 120, 60, 255};
                if (tile == TILE_WALL) c = {100, 90, 80, 255};
                else if (tile == TILE_FLOOR) c = {70, 70, 75, 255};
                Uint8 a = hasBgImage ? 160 : 255;
                SDL_SetRenderDrawBlendMode(renderer, hasBgImage ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, a);
                SDL_RenderFillRect(renderer, &dst);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }

            if (map_.ceiling[idx] == CEIL_GLASS) {
                SDL_SetRenderDrawColor(renderer, 100, 160, 220, 40);
                SDL_RenderFillRect(renderer, &dst);
            }
        }
    }

    // Draw free-placed props
    for (auto& ps : map_.props) {
        int sx = worldToScreenX(ps.x) - (int)(ts * 0.5f);
        int sy = worldToScreenY(ps.y) - (int)(ts * 0.5f);
        SDL_Rect dst = {sx, sy, (int)ceilf(ts), (int)ceilf(ts)};
        SDL_Texture* tex = tileTextures_[ps.tileType];
        if (tex) {
            double angle = ps.rotation * 90.0;
            if (angle == 0.0)
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
            else
                SDL_RenderCopyEx(renderer, tex, nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
        } else {
            SDL_SetRenderDrawColor(renderer, 180, 120, 60, 200);
            SDL_RenderFillRect(renderer, &dst);
        }
        // subtle outline so free props are distinguishable
        SDL_SetRenderDrawColor(renderer, 255, 200, 80, 180);
        SDL_RenderDrawRect(renderer, &dst);
    }

    if (showGrid_) renderGrid(renderer);

    // Map boundary outline
    {
        int bx0 = worldToScreenX(0);
        int by0 = worldToScreenY(0);
        int bx1 = worldToScreenX((float)(map_.width * TILE_SIZE));
        int by1 = worldToScreenY((float)(map_.height * TILE_SIZE));
        SDL_SetRenderDrawColor(renderer, 0, 200, 180, 120);
        SDL_Rect border = {bx0, by0, bx1 - bx0, by1 - by0};
        SDL_RenderDrawRect(renderer, &border);
    }

    // Top image layer (semi-transparent over tiles)
    if (!map_.topImagePath.empty() && showTopLayer_) {
        SDL_Texture* topTex = Assets::instance().loadRelTex(map_.topImagePath);
        if (topTex) {
            int sx = worldToScreenX(0.0f);
            int sy = worldToScreenY(0.0f);
            int sw = (int)(map_.width  * TILE_SIZE * zoom_);
            int sh = (int)(map_.height * TILE_SIZE * zoom_);
            SDL_Rect dst = {sx, sy, sw, sh};
            SDL_SetTextureBlendMode(topTex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(topTex, 160);
            SDL_RenderCopy(renderer, topTex, nullptr, &dst);
            SDL_SetTextureAlphaMod(topTex, 255);
        }
    }

    renderTriggers(renderer);
    renderEntitySpawns(renderer);

    // Cursor highlight
    if (mouseX_ < screenW_ - uiPaletteW() && mouseY_ > uiToolbarH()) {
        float wx = screenToWorldX(mouseX_);
        float wy = screenToWorldY(mouseY_);
        int tx = (int)(wx / TILE_SIZE);
        int ty = (int)(wy / TILE_SIZE);
        int sx = worldToScreenX((float)(tx * TILE_SIZE));
        int sy = worldToScreenY((float)(ty * TILE_SIZE));

        // Brush-sized outline
        int half = (brushSize_ - 1) / 2;
        int bsx = worldToScreenX((float)((tx - half) * TILE_SIZE));
        int bsy = worldToScreenY((float)((ty - half) * TILE_SIZE));
        int bsizePx = (int)ceilf(ts * brushSize_);
        SDL_Rect cur = {bsx, bsy, bsizePx, bsizePx};

        if (currentTool_ == EditorTool::Erase) {
            SDL_SetRenderDrawColor(renderer, 255, 60, 60, 140);
            SDL_RenderDrawRect(renderer, &cur);
        } else if (currentTool_ == EditorTool::Trigger) {
            // Draw trigger drag preview
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            if (trigDragging_) {
                float cwx = screenToWorldX(mouseX_);
                float cwy = screenToWorldY(mouseY_);
                float px0 = std::min(trigDragStartX_, cwx);
                float py0 = std::min(trigDragStartY_, cwy);
                float px1 = std::max(trigDragStartX_, cwx);
                float py1 = std::max(trigDragStartY_, cwy);
                int psx = worldToScreenX(px0), psy = worldToScreenY(py0);
                int psw = std::max(1, worldToScreenX(px1) - psx);
                int psh = std::max(1, worldToScreenY(py1) - psy);
                SDL_Rect pRect = {psx, psy, psw, psh};
                SDL_SetRenderDrawColor(renderer, 100, 200, 255, 30);
                SDL_RenderFillRect(renderer, &pRect);
                SDL_SetRenderDrawColor(renderer, 100, 200, 255, 220);
                SDL_RenderDrawRect(renderer, &pRect);
            } else {
                // Show crosshair cursor when not dragging
                SDL_SetRenderDrawColor(renderer, 100, 200, 255, 160);
                SDL_RenderDrawLine(renderer, mouseX_ - 8, mouseY_, mouseX_ + 8, mouseY_);
                SDL_RenderDrawLine(renderer, mouseX_, mouseY_ - 8, mouseX_, mouseY_ + 8);
            }
        } else if (currentTool_ == EditorTool::Rect) {
            // Draw rect preview while dragging
            if (rectStartTX_ >= 0 && mouseDown_) {
                int rx0 = std::min(rectStartTX_, tx), rx1 = std::max(rectStartTX_, tx);
                int ry0 = std::min(rectStartTY_, ty), ry1 = std::max(rectStartTY_, ty);
                int rsx = worldToScreenX((float)(rx0 * TILE_SIZE));
                int rsy = worldToScreenY((float)(ry0 * TILE_SIZE));
                int rpw = (int)ceilf((rx1 - rx0 + 1) * ts);
                int rph = (int)ceilf((ry1 - ry0 + 1) * ts);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 255, 200, 50, rectFilled_ ? 35 : 10);
                SDL_Rect rFill = {rsx, rsy, rpw, rph};
                SDL_RenderFillRect(renderer, &rFill);
                SDL_SetRenderDrawColor(renderer, 255, 200, 50, 220);
                SDL_RenderDrawRect(renderer, &rFill);
            }
            SDL_SetRenderDrawColor(renderer, 255, 200, 50, 140);
            SDL_Rect singleCur = {sx, sy, (int)ceilf(ts), (int)ceilf(ts)};
            SDL_RenderDrawRect(renderer, &singleCur);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 120);
            SDL_RenderDrawRect(renderer, &cur);
        }

        // Preview selected tile texture on hovered cell
        SDL_Rect singleCur = {sx, sy, (int)ceilf(ts), (int)ceilf(ts)};
        if ((currentTool_ == EditorTool::Tile || currentTool_ == EditorTool::Rect) &&
            selectedPalette_ >= 0 && selectedPalette_ < (int)palette_.size() &&
            palette_[selectedPalette_].texture) {
            SDL_SetTextureAlphaMod(palette_[selectedPalette_].texture, 120);
            SDL_RenderCopy(renderer, palette_[selectedPalette_].texture, nullptr, &singleCur);
            SDL_SetTextureAlphaMod(palette_[selectedPalette_].texture, 255);
        }
    }

    // Move handles (arrows + grab ball) for selected object
    if (currentTool_ == EditorTool::Select)
        renderMoveHandles(renderer);

    if (showUI_) {
        renderToolbar(renderer);
        renderPalette(renderer);
    }

    // Properties / context panel
    if (showUI_)
        renderPropertiesPanel(renderer);

    // Map properties floating panel
    renderMapPropsPanel(renderer);

    // Status bar (bottom-left)
    {
        SDL_SetRenderDrawColor(renderer, 12, 14, 24, 200);
        SDL_Rect statusBg = {0, screenH_ - 28, screenW_ - uiPaletteW(), 28};
        SDL_RenderFillRect(renderer, &statusBg);
        SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
        SDL_RenderDrawLine(renderer, 0, screenH_ - 28, screenW_ - uiPaletteW(), screenH_ - 28);

        char zoomStr[32];
        snprintf(zoomStr, sizeof(zoomStr), "Zoom: %.0f%%", zoom_ * 100);
        drawEditorText(renderer, zoomStr, 8, screenH_ - 24, 12, {100, 100, 110, 255});

        // Tile count info
        char countStr[64];
        snprintf(countStr, sizeof(countStr), "Triggers: %d  Entities: %d",
                 (int)map_.triggers.size(), (int)map_.enemySpawns.size());
        drawEditorText(renderer, countStr, 120, screenH_ - 24, 12, {100, 100, 110, 255});

        // Save message
        if (saveMessageTimer_ > 0) {
            Uint8 alpha = (saveMessageTimer_ < 0.5f) ? (Uint8)(saveMessageTimer_ * 510) : 255;
            bool isError = saveMessage_.find("failed") != std::string::npos;
            SDL_Color msgC = isError ? SDL_Color{255, 80, 80, alpha} : SDL_Color{50, 255, 100, alpha};
            drawEditorText(renderer, saveMessage_.c_str(), 360, screenH_ - 24, 12, msgC);
        }
    }

    // Gamepad / touch cursor
    renderCursor(renderer);

    // Cutscene editor panel (bottom strip)
    if (showCutsceneEditor_) {
        int panelY = screenH_ - CS_EDITOR_PANEL_H;
        csEditor_.render(renderer, screenW_, panelY);
        renderCutsceneActorOverlays(renderer);
    }
}

void MapEditor::renderCutsceneActorOverlays(SDL_Renderer* renderer) {
    const Cutscene* cs = csEditor_.currentCutscene();
    if (!cs) return;
    for (int i = 0; i < csEditor_.actorCount(); i++) {
        const CsActorState* s = csEditor_.actorStateAt(i);
        if (!s || !s->visible) continue;
        const CsActor& a = cs->actors[i];

        int sx = worldToScreenX(s->x);
        int sy = worldToScreenY(s->y);

        // Draw a simple colored diamond marker for each actor
        SDL_Color col;
        switch (a.type) {
            case CsActorType::Player:     col = {100, 200, 255, 200}; break;
            case CsActorType::Enemy:      col = {255, 120,  80, 200}; break;
            default:                      col = {180, 255, 180, 200}; break;
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        int r = (int)(10 * zoom_);
        SDL_RenderDrawLine(renderer, sx-r, sy,   sx,   sy-r);
        SDL_RenderDrawLine(renderer, sx,   sy-r, sx+r, sy);
        SDL_RenderDrawLine(renderer, sx+r, sy,   sx,   sy+r);
        SDL_RenderDrawLine(renderer, sx,   sy+r, sx-r, sy);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        // Label
        drawEditorText(renderer, a.name.c_str(), sx+r+2, sy-8, 11, {col.r, col.g, col.b, 255});
    }
}

void MapEditor::renderGrid(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 40);
    float ts = TILE_SIZE * zoom_;
    int startX = (int)(camera_.pos.x / TILE_SIZE);
    int startY = (int)(camera_.pos.y / TILE_SIZE);

    for (int x = startX; x <= startX + (int)((screenW_ - uiPaletteW()) / ts) + 1; x++) {
        int sx = worldToScreenX((float)(x * TILE_SIZE));
        SDL_RenderDrawLine(renderer, sx, uiToolbarH(), sx, screenH_);
    }
    for (int y = startY; y <= startY + (int)((screenH_ - uiToolbarH()) / ts) + 1; y++) {
        int sy = worldToScreenY((float)(y * TILE_SIZE));
        SDL_RenderDrawLine(renderer, 0, sy, screenW_ - uiPaletteW(), sy);
    }
}

void MapEditor::renderTriggers(SDL_Renderer* renderer) {
    for (int i = 0; i < (int)map_.triggers.size(); i++) {
        auto& t = map_.triggers[i];
        int sx = worldToScreenX(t.x - t.width/2);
        int sy = worldToScreenY(t.y - t.height/2);
        int sw = (int)(t.width  * zoom_);
        int sh = (int)(t.height * zoom_);
        SDL_Rect r = {sx, sy, sw, sh};

        bool selected = (i == selectedTrigger_);

        // Label size scales with zoom; suppress entirely when too small to read
        const int labelSz = std::max(0, std::min(14, (int)(11.0f * zoom_)));

        switch (t.type) {
            case TriggerType::LevelStart:
                SDL_SetRenderDrawColor(renderer, 50, 255, 50, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 50, 255, 50, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "START", r.x + 4, r.y + 4, labelSz, {50, 255, 50, 255});
                break;
            case TriggerType::LevelEnd: {
                SDL_SetRenderDrawColor(renderer, 255, 200, 50, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 200, 50, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                const char* condStr = "GOAL:OPEN";
                if (t.condition == GoalCondition::DefeatAll) condStr = "GOAL:KILL ALL";
                else if (t.condition == GoalCondition::OnTrigger) condStr = "GOAL:TRIGGER";
                if (labelSz >= 8) drawEditorText(renderer, condStr, r.x + 4, r.y + 4, labelSz, {255, 200, 50, 255});
                break;
            }
            case TriggerType::Crate:
                SDL_SetRenderDrawColor(renderer, 180, 130, 60, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 180, 130, 60, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "CRATE", r.x + 4, r.y + 4, labelSz, {180, 130, 60, 255});
                break;
            case TriggerType::Effect:
                SDL_SetRenderDrawColor(renderer, 180, 50, 255, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 180, 50, 255, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "EFFECT", r.x + 4, r.y + 4, labelSz, {180, 50, 255, 255});
                break;
            case TriggerType::TeamSpawnRed:
                SDL_SetRenderDrawColor(renderer, 220, 50, 50, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 70, 70, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "SPAWN RED", r.x + 4, r.y + 4, labelSz, {255, 80, 80, 255});
                break;
            case TriggerType::TeamSpawnBlue:
                SDL_SetRenderDrawColor(renderer, 50, 80, 220, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 70, 120, 255, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "SPAWN BLUE", r.x + 4, r.y + 4, labelSz, {80, 140, 255, 255});
                break;
            case TriggerType::TeamSpawnGreen:
                SDL_SetRenderDrawColor(renderer, 50, 200, 80, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 70, 230, 100, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "SPAWN GREEN", r.x + 4, r.y + 4, labelSz, {80, 240, 110, 255});
                break;
            case TriggerType::TeamSpawnYellow:
                SDL_SetRenderDrawColor(renderer, 220, 200, 30, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 230, 40, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "SPAWN YEL", r.x + 4, r.y + 4, labelSz, {255, 235, 50, 255});
                break;
            case TriggerType::LayerFade:
                SDL_SetRenderDrawColor(renderer, 80, 180, 255, 25);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 100, 200, 255, selected ? 255 : 140);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) drawEditorText(renderer, "LAYER FADE", r.x + 4, r.y + 4, labelSz, {120, 210, 255, 255});
                break;
            case TriggerType::CollisionZone: {
                float angle = triggerGetAngle(t);
                // t.x / t.y is the CENTER (same convention as all other triggers)
                float scx = (float)worldToScreenX(t.x);
                float scy = (float)worldToScreenY(t.y);
                float hw = t.width  * 0.5f * zoom_;
                float hh = t.height * 0.5f * zoom_;
                float ca = cosf(angle), sa = sinf(angle);
                float lhw = t.width * 0.5f, lhh = t.height * 0.5f;
                SDL_FPoint pts[5];
                pts[0] = { scx + (-lhw*ca + lhh*sa)*zoom_, scy + (-lhw*sa - lhh*ca)*zoom_ };
                pts[1] = { scx + ( lhw*ca + lhh*sa)*zoom_, scy + ( lhw*sa - lhh*ca)*zoom_ };
                pts[2] = { scx + ( lhw*ca - lhh*sa)*zoom_, scy + ( lhw*sa + lhh*ca)*zoom_ };
                pts[3] = { scx + (-lhw*ca - lhh*sa)*zoom_, scy + (-lhw*sa + lhh*ca)*zoom_ };
                pts[4] = pts[0];
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 255, 80, 40, 25);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 120, 60, selected ? 255 : 200);
                for (int si = 0; si < 4; si++)
                    SDL_RenderDrawLineF(renderer, pts[si].x, pts[si].y, pts[si+1].x, pts[si+1].y);
                // Rotation handle dot above centre
                float hx = scx + (-sa) * std::min(hh, hw) * 0.6f;
                float hy = scy + (-ca) * std::min(hh, hw) * 0.6f;
                SDL_Rect handle = {(int)hx - 5, (int)hy - 5, 10, 10};
                SDL_SetRenderDrawColor(renderer, 255, 180, 80, 255);
                SDL_RenderFillRect(renderer, &handle);
                if (labelSz >= 8) drawEditorText(renderer, "COLLIDE", r.x + 4, r.y + 4, labelSz, {255, 150, 80, 255});
                break;
            }
            default: break;
        }

        // Resize handles for selected trigger
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            int hsz = 8;
            int cx[4] = {r.x, r.x + r.w, r.x, r.x + r.w};
            int cy[4] = {r.y, r.y, r.y + r.h, r.y + r.h};
            for (int h = 0; h < 4; h++) {
                SDL_Rect handle = {cx[h] - hsz/2, cy[h] - hsz/2, hsz, hsz};
                SDL_RenderFillRect(renderer, &handle);
            }
        }
    }
}

void MapEditor::renderEntitySpawns(SDL_Renderer* renderer) {
    for (int i = 0; i < (int)map_.enemySpawns.size(); i++) {
        auto& es = map_.enemySpawns[i];
        int sx = worldToScreenX(es.x);
        int sy = worldToScreenY(es.y);
        int sz = (int)(24 * zoom_);
        SDL_Rect r = {sx - sz/2, sy - sz/2, sz, sz};

        bool selected = (i == selectedEnemy_);

        // Color by entity type
        switch (es.enemyType) {
            case ENTITY_MELEE:         SDL_SetRenderDrawColor(renderer, 255, 60, 60, 200); break;
            case ENTITY_SHOOTER:       SDL_SetRenderDrawColor(renderer, 255, 160, 40, 200); break;
            case ENTITY_BRUTE:         SDL_SetRenderDrawColor(renderer, 170, 70, 70, 220); break;
            case ENTITY_SCOUT:         SDL_SetRenderDrawColor(renderer, 255, 100, 170, 220); break;
            case ENTITY_SNIPER:        SDL_SetRenderDrawColor(renderer, 160, 120, 255, 220); break;
            case ENTITY_GUNNER:        SDL_SetRenderDrawColor(renderer, 255, 215, 100, 220); break;
            case ENTITY_CRATE:         SDL_SetRenderDrawColor(renderer, 160, 120, 60, 200); break;
            case ENTITY_UPGRADE_CRATE: SDL_SetRenderDrawColor(renderer, 220, 180, 40, 200); break;
            default:                   SDL_SetRenderDrawColor(renderer, 180, 180, 180, 200); break;
        }
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, selected ? 0 : 255, 255, selected ? 0 : 255, 255);
        SDL_RenderDrawRect(renderer, &r);

        const char* labels[] = {"M", "S", "C", "U", "B", "F", "N", "G"};
        const char* label = (es.enemyType >= 0 && es.enemyType < ENTITY_TYPE_COUNT) ? labels[es.enemyType] : "?";
        drawEditorText(renderer, label, r.x + sz/4, r.y + 2, 14, {255, 255, 255, 255});
    }
}

// Move Handles - grab ball for selected object

void MapEditor::renderMoveHandles(SDL_Renderer* renderer) {
    if (currentTool_ != EditorTool::Select) return;

    int cx, cy;
    if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
        cx = worldToScreenX(map_.enemySpawns[selectedEnemy_].x);
        cy = worldToScreenY(map_.enemySpawns[selectedEnemy_].y);
    } else if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
        auto& t = map_.triggers[selectedTrigger_];
        int hrx = worldToScreenX(t.x - t.width/2);
        int hry = worldToScreenY(t.y - t.height/2);
        int hrw = (int)(t.width * zoom_); int hrh = (int)(t.height * zoom_);
        cx = hrx + hrw/2; cy = hry + hrh/2;
    } else {
        return;
    }

    const int ballR = 9;
    const Uint8 fillA = draggingMove_ ? 255 : 220;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(renderer, 255, 220, 50, fillA);
    for (int dy = -ballR; dy <= ballR; dy++) {
        int dx2 = (int)sqrtf((float)(ballR * ballR - dy * dy));
        SDL_RenderDrawLine(renderer, cx - dx2, cy + dy, cx + dx2, cy + dy);
    }
    SDL_SetRenderDrawColor(renderer, 80, 50, 0, 200);
    for (int a = 0; a < 32; a++) {
        float ang = a * 6.2832f / 32.0f;
        SDL_RenderDrawPoint(renderer,
            cx + (int)((ballR + 0.5f) * cosf(ang)),
            cy + (int)((ballR + 0.5f) * sinf(ang)));
    }
}

// Properties Panel (inspector for selected entity/trigger)

void MapEditor::renderPropertiesPanel(SDL_Renderer* renderer) {
    if (!ui_) return;

    const int panelW = 220;
    const int panelX = 8;
    const int panelY = TOOLBAR_H + 8;
    char buf[128];

    // Layout constants
    const int pad    = 8;
    const int lblW   = 60;
    const int btnSz  = 20;
    // fieldW: panelW - 2*pad - lblW - gap1 - btnSz - gap2 - btnSz - gap3 = 220-16-60-4-20-2-20-2 = 96
    const int fieldW = 96;
    const int rowH   = 28;
    const int lx     = panelX + pad;
    const int arLx   = panelX + pad + lblW + 4;
    const int fldx   = arLx + btnSz + 2;
    const int arRx   = fldx + fieldW + 2;
    // read-only field: spans from arLx to panel right interior edge
    const int roFldW = panelW - pad - (arLx - panelX);  // 140

    if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
        auto& es = map_.enemySpawns[selectedEnemy_];
        // TitleH(22) + top_pad(8) + 3 rows(84) + gap(8) + delete(26) + bot_pad(8)
        const int panelH = UI::W98::TitleH + 8 + 3 * rowH + 8 + 26 + 8;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Entity");
        int y = panelY + UI::W98::TitleH + 8;

        // Type
        static const char* eNames[] = {"Melee","Shooter","Crate","Upgrade","Brute","Scout","Sniper","Gunner"};
        ui_->drawText("Type", lx, y + 5, 11, UI::W98::Black);
        if (ui_->win98Button(200, "<", arLx, y, btnSz, btnSz, false)) {
            pushUndo();
            es.enemyType = (es.enemyType + ENTITY_TYPE_COUNT - 1) % ENTITY_TYPE_COUNT;
        }
        ui_->drawWin98TextField(fldx, y, fieldW, btnSz,
            (es.enemyType < ENTITY_TYPE_COUNT) ? eNames[es.enemyType] : "?", false);
        if (ui_->win98Button(201, ">", arRx, y, btnSz, btnSz, false)) {
            pushUndo();
            es.enemyType = (es.enemyType + 1) % ENTITY_TYPE_COUNT;
        }
        y += rowH;

        // Wave
        ui_->drawText("Wave", lx, y + 5, 11, UI::W98::Black);
        if (ui_->win98Button(202, "<", arLx, y, btnSz, btnSz, false)) {
            pushUndo();
            if (es.waveGroup > 0) es.waveGroup--;
        }
        snprintf(buf, sizeof(buf), "%d", es.waveGroup);
        ui_->drawWin98TextField(fldx, y, fieldW, btnSz, buf, false);
        if (ui_->win98Button(203, ">", arRx, y, btnSz, btnSz, false)) {
            pushUndo();
            if (es.waveGroup < 255) es.waveGroup++;
        }
        y += rowH;

        // Pos (read-only)
        snprintf(buf, sizeof(buf), "%.0f, %.0f", es.x, es.y);
        ui_->drawText("Pos", lx, y + 5, 11, UI::W98::Black);
        ui_->drawWin98TextField(arLx, y, roFldW, btnSz, buf, false);
        y += rowH;

        // Delete
        y += 8;
        if (ui_->win98Button(210, "Delete", lx, y, panelW - pad * 2, 26, false)) {
            pushUndo();
            map_.enemySpawns.erase(map_.enemySpawns.begin() + selectedEnemy_);
            selectedEnemy_ = -1;
        }
    }
    else if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
        auto& t = map_.triggers[selectedTrigger_];

        static const char* tTypeNames[] = {
            "Start","End","Crate","Effect",
            "SpnR","SpnB","SpnG","SpnY",
            "Fade","Collision",
        };
        static const TriggerType kTypes[] = {
            TriggerType::LevelStart, TriggerType::LevelEnd, TriggerType::Crate, TriggerType::Effect,
            TriggerType::TeamSpawnRed, TriggerType::TeamSpawnBlue, TriggerType::TeamSpawnGreen, TriggerType::TeamSpawnYellow,
            TriggerType::LayerFade, TriggerType::CollisionZone,
        };
        constexpr int kTypeCount2 = 10;
        int typeIdx = 0;
        for (int j = 0; j < kTypeCount2; j++) { if (kTypes[j] == t.type) { typeIdx = j; break; } }
        bool hasCondRow = (t.type == TriggerType::LevelEnd);
        // TitleH(22) + top_pad(8) + rows + gap(8) + delete(26) + bot_pad(8)
        const int panelH = UI::W98::TitleH + 8 + (3 + (hasCondRow ? 1 : 0)) * rowH + 8 + 26 + 8;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Trigger");
        int y = panelY + UI::W98::TitleH + 8;

        // Type
        ui_->drawText("Type", lx, y + 5, 11, UI::W98::Black);
        if (ui_->win98Button(220, "<", arLx, y, btnSz, btnSz, false)) {
            pushUndo();
            typeIdx = (typeIdx + kTypeCount2 - 1) % kTypeCount2;
            t.type = kTypes[typeIdx];
        }
        ui_->drawWin98TextField(fldx, y, fieldW, btnSz, tTypeNames[typeIdx], false);
        if (ui_->win98Button(221, ">", arRx, y, btnSz, btnSz, false)) {
            pushUndo();
            typeIdx = (typeIdx + 1) % kTypeCount2;
            t.type = kTypes[typeIdx];
        }
        y += rowH;

        // Condition (LevelEnd only)
        if (hasCondRow) {
            static const char* condNames[] = {"Open", "Kill All", "Trigger"};
            int condIdx = (int)t.condition;
            if (condIdx < 0 || condIdx > 2) condIdx = 0;
            ui_->drawText("Goal", lx, y + 5, 11, UI::W98::Black);
            if (ui_->win98Button(222, "<", arLx, y, btnSz, btnSz, false)) {
                pushUndo();
                condIdx = (condIdx + 2) % 3;
                t.condition = (GoalCondition)condIdx;
            }
            ui_->drawWin98TextField(fldx, y, fieldW, btnSz, condNames[condIdx], false);
            if (ui_->win98Button(223, ">", arRx, y, btnSz, btnSz, false)) {
                pushUndo();
                condIdx = (condIdx + 1) % 3;
                t.condition = (GoalCondition)condIdx;
            }
            y += rowH;
        }

        // Size (read-only)
        snprintf(buf, sizeof(buf), "%.0fx%.0f", t.width, t.height);
        ui_->drawText("Size", lx, y + 5, 11, UI::W98::Black);
        ui_->drawWin98TextField(arLx, y, roFldW, btnSz, buf, false);
        y += rowH;

        // Pos (read-only)
        snprintf(buf, sizeof(buf), "%.0f, %.0f", t.x, t.y);
        ui_->drawText("Pos", lx, y + 5, 11, UI::W98::Black);
        ui_->drawWin98TextField(arLx, y, roFldW, btnSz, buf, false);
        y += rowH;

        // Delete
        y += 8;
        if (ui_->win98Button(230, "Delete", lx, y, panelW - pad * 2, 26, false)) {
            pushUndo();
            map_.triggers.erase(map_.triggers.begin() + selectedTrigger_);
            selectedTrigger_ = -1;
        }
    }
    else if (currentTool_ == EditorTool::Tile || currentTool_ == EditorTool::Erase) {
        // Brush size + current tile preview
        const int previewSz = 42;
        const int panelH = UI::W98::TitleH + 8 + rowH + 6 + 1 + 6 + previewSz + 8;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Brush Settings");
        int y = panelY + UI::W98::TitleH + 8;

        ui_->drawText("Brush", lx, y + 5, 11, UI::W98::Black);
        if (ui_->win98Button(240, "<", arLx, y, btnSz, btnSz, false))
            if (brushSize_ > 1) brushSize_--;
        snprintf(buf, sizeof(buf), "%d", brushSize_);
        ui_->drawWin98TextField(fldx, y, fieldW, btnSz, buf, false);
        if (ui_->win98Button(241, ">", arRx, y, btnSz, btnSz, false))
            if (brushSize_ < 9) brushSize_++;
        y += rowH + 6;

        SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
        SDL_RenderDrawLine(renderer, lx, y, panelX + panelW - pad, y);
        y += 6;

        if (selectedPalette_ >= 0 && selectedPalette_ < (int)palette_.size()) {
            auto& pt = palette_[selectedPalette_];
            ui_->drawWin98Bevel(lx - 1, y - 1, previewSz + 2, previewSz + 2, false);
            SDL_Rect dst = {lx, y, previewSz, previewSz};
            if (pt.texture) {
                SDL_SetTextureColorMod(pt.texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(pt.texture, 255);
                SDL_RenderCopy(renderer, pt.texture, nullptr, &dst);
            } else {
                SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
                SDL_RenderFillRect(renderer, &dst);
            }
            int tx = lx + previewSz + 6;
            ui_->drawText(pt.name.c_str(), tx, y + 6, 11, UI::W98::Black);
            ui_->drawText(pt.category.c_str(), tx, y + 22, 10, UI::W98::Shadow);
        } else {
            ui_->drawText("No tile selected", lx, y + previewSz / 2 - 6, 11, UI::W98::Shadow);
        }
    }
    else if (currentTool_ == EditorTool::Trigger) {
        static const char* tNames[] = {
            "Level Start", "Level End", "Crate", "Effect",
            "Spawn Red", "Spawn Blue", "Spawn Green", "Spawn Yellow",
            "Layer Fade", "Collision",
        };
        static const TriggerType kT[] = {
            TriggerType::LevelStart, TriggerType::LevelEnd, TriggerType::Crate, TriggerType::Effect,
            TriggerType::TeamSpawnRed, TriggerType::TeamSpawnBlue, TriggerType::TeamSpawnGreen, TriggerType::TeamSpawnYellow,
            TriggerType::LayerFade, TriggerType::CollisionZone,
        };
        const int itemH = 22;
        const int panelH = UI::W98::TitleH + 8 + 10 * itemH + 8;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Trigger Type");
        int y = panelY + UI::W98::TitleH + 8;
        const int btnW = panelW - pad * 2;
        for (int i = 0; i < 10; i++) {
            if (ui_->win98Button(240 + i, tNames[i], lx, y, btnW, itemH, triggerGhost_.type == kT[i]))
                triggerGhost_.type = kT[i];
            y += itemH;
        }
    }
    else if (currentTool_ == EditorTool::Entity) {
        static const char* eNames[] = {
            "Melee", "Shooter", "Crate", "Upgrade Crate",
            "Brute", "Scout", "Sniper", "Gunner",
        };
        const int itemH = 22;
        const int panelH = UI::W98::TitleH + 8 + 8 * itemH + 8;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Entity Type");
        int y = panelY + UI::W98::TitleH + 8;
        const int btnW = panelW - pad * 2;
        for (int i = 0; i < 8; i++) {
            if (ui_->win98Button(250 + i, eNames[i], lx, y, btnW, itemH, entitySpawnType_ == (uint8_t)i))
                entitySpawnType_ = (uint8_t)i;
            y += itemH;
        }
    }
    else if (currentTool_ == EditorTool::Rect) {
        const int halfW = (panelW - pad * 2 - 4) / 2;
        const int panelH = UI::W98::TitleH + 8 + 26 + 8 + rowH + 8;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Rect Settings");
        int y = panelY + UI::W98::TitleH + 8;

        if (ui_->win98Button(240, "Filled",  lx,              y, halfW, 26, rectFilled_))  rectFilled_ = true;
        if (ui_->win98Button(241, "Outline", lx + halfW + 4,  y, halfW, 26, !rectFilled_)) rectFilled_ = false;
        y += 26 + 8;

        ui_->drawText("Brush", lx, y + 5, 11, UI::W98::Black);
        if (ui_->win98Button(242, "<", arLx, y, btnSz, btnSz, false))
            if (brushSize_ > 1) brushSize_--;
        snprintf(buf, sizeof(buf), "%d", brushSize_);
        ui_->drawWin98TextField(fldx, y, fieldW, btnSz, buf, false);
        if (ui_->win98Button(243, ">", arRx, y, btnSz, btnSz, false))
            if (brushSize_ < 9) brushSize_++;
    }
}

// Map Properties Panel (game mode + player config, editable from within editor)

void MapEditor::renderMapPropsPanel(SDL_Renderer* renderer) {
    if (!showMapProps_ || !ui_) return;

    auto& pc = map_.playerConfig;

    const int panelW  = 230;
    const int pad     = 8;
    const int btnSz   = 22;
    const int rowH    = 30;
    const int innerW  = panelW - pad * 2;  // 214

    // Row count: gamemode + abilities-label + abilities-buttons + maxhp + startbombs + speed + damage + reset + close = 9
    const int rows    = 9;
    const int panelH  = UI::W98::TitleH + pad + rows * rowH + pad;
    const int panelX  = screenW_ - uiPaletteW() - panelW - 8;
    const int panelY  = uiToolbarH() + 8;

    ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Map Properties");

    int y    = panelY + UI::W98::TitleH + pad;
    int lx   = panelX + pad;
    int arLx = lx + 72;
    int arRx = arLx + btnSz + 44 + 2;
    char buf[64];

    // Game Mode
    static const char* modeNames[] = { "Arena", "Sandbox" };
    ui_->drawText("Mode", lx, y + 5, 11, UI::W98::Black);
    if (ui_->win98Button(500, "<", arLx, y, btnSz, btnSz, false))
        map_.gameMode = (map_.gameMode + 1) % 2;
    ui_->drawWin98TextField(arLx + btnSz + 2, y, 44, btnSz,
        modeNames[map_.gameMode & 1], false);
    if (ui_->win98Button(501, ">", arRx, y, btnSz, btnSz, false))
        map_.gameMode = (map_.gameMode + 1) % 2;
    y += rowH;

    // Abilities header
    ui_->drawText("Abilities", lx, y + 5, 11, UI::W98::Black);
    y += rowH;

    // Abilities toggle buttons (5 equal columns across full interior)
    {
        const char* abNames[]  = { "Gun", "Mlee", "Bomb", "Parry", "Pick" };
        bool*       abFields[] = { &pc.hasGun, &pc.hasMelee, &pc.hasBombs, &pc.hasParry, &pc.hasPickups };
        const int abW = innerW / 5;  // 42px each, well within panel
        for (int i = 0; i < 5; i++) {
            if (ui_->win98Button(510 + i, abNames[i], lx + i * abW, y, abW - 2, btnSz, *abFields[i]))
                *abFields[i] = !*abFields[i];
        }
    }
    y += rowH;

    // Max HP
    ui_->drawText("Max HP", lx, y + 5, 11, UI::W98::Black);
    if (ui_->win98Button(520, "<", arLx, y, btnSz, btnSz, false))
        if (pc.maxHp > 0) pc.maxHp--;
    snprintf(buf, sizeof(buf), pc.maxHp == 0 ? "Default" : "%d", (int)pc.maxHp);
    ui_->drawWin98TextField(arLx + btnSz + 2, y, 44, btnSz, buf, false);
    if (ui_->win98Button(521, ">", arRx, y, btnSz, btnSz, false))
        if (pc.maxHp < 99) pc.maxHp++;
    y += rowH;

    // Start Bombs
    ui_->drawText("Bombs", lx, y + 5, 11, UI::W98::Black);
    if (ui_->win98Button(522, "<", arLx, y, btnSz, btnSz, false))
        if (pc.startBombs > 0) pc.startBombs--;
    snprintf(buf, sizeof(buf), "%d", (int)pc.startBombs);
    ui_->drawWin98TextField(arLx + btnSz + 2, y, 44, btnSz, buf, false);
    if (ui_->win98Button(523, ">", arRx, y, btnSz, btnSz, false))
        if (pc.startBombs < 9) pc.startBombs++;
    y += rowH;

    // Speed %
    ui_->drawText("Speed%", lx, y + 5, 11, UI::W98::Black);
    if (ui_->win98Button(524, "<", arLx, y, btnSz, btnSz, false))
        if (pc.speedPct > 50) pc.speedPct = (uint8_t)(pc.speedPct - 5);
    snprintf(buf, sizeof(buf), "%d%%", (int)pc.speedPct);
    ui_->drawWin98TextField(arLx + btnSz + 2, y, 44, btnSz, buf, false);
    if (ui_->win98Button(525, ">", arRx, y, btnSz, btnSz, false))
        if (pc.speedPct < 150) pc.speedPct = (uint8_t)(pc.speedPct + 5);
    y += rowH;

    // Damage %
    ui_->drawText("Damage%", lx, y + 5, 11, UI::W98::Black);
    if (ui_->win98Button(526, "<", arLx, y, btnSz, btnSz, false))
        if (pc.damagePct > 50) pc.damagePct = (uint8_t)(pc.damagePct - 5);
    snprintf(buf, sizeof(buf), "%d%%", (int)pc.damagePct);
    ui_->drawWin98TextField(arLx + btnSz + 2, y, 44, btnSz, buf, false);
    if (ui_->win98Button(527, ">", arRx, y, btnSz, btnSz, false))
        if (pc.damagePct < 150) pc.damagePct = (uint8_t)(pc.damagePct + 5);
    y += rowH;

    // Reset to defaults
    if (ui_->win98Button(528, "Reset Defaults", lx, y, panelW - pad * 2, btnSz, false)) {
        pc = MapPlayerConfig{};
        map_.gameMode = 0;
    }
    y += rowH;

    // Layer images (bg / top)
    ui_->drawWin98Bevel(lx, y, innerW, 2, false); y += 6;

    // Helper: extract basename (no ext) from savePath_
    auto mapBaseName = [&]() {
        std::string base = savePath_;
        size_t sl = base.find_last_of("/\\");
        if (sl != std::string::npos) base = base.substr(sl + 1);
        size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        return base;
    };

    // Auto=46px, X=22px, gap=2px between each -> field = innerW-46-2-22-2 = innerW-72
    const int autoW = 46, clearW = 22, imgGap = 2;
    const int imgFieldW = innerW - autoW - imgGap - clearW - imgGap;
    ui_->drawText("BG image:", lx, y, 10, UI::W98::Shadow);
    {
        std::string disp = map_.bgImagePath.empty() ? "(none)" : map_.bgImagePath;
        if (disp.size() > 22) disp = disp.substr(disp.size() - 22);
        ui_->drawWin98TextField(lx, y + 12, imgFieldW, 20, disp.c_str(), false);
        if (ui_->win98Button(530, "Auto", lx + imgFieldW + imgGap, y + 12, autoW, 20, false))
            map_.bgImagePath = "sprites/" + mapBaseName() + ".png";
        if (ui_->win98Button(531, "X", lx + imgFieldW + imgGap + autoW + imgGap, y + 12, clearW, 20, false))
            map_.bgImagePath.clear();
    }
    y += 34;
    ui_->drawText("Top image:", lx, y, 10, UI::W98::Shadow);
    {
        std::string disp = map_.topImagePath.empty() ? "(none)" : map_.topImagePath;
        if (disp.size() > 22) disp = disp.substr(disp.size() - 22);
        ui_->drawWin98TextField(lx, y + 12, imgFieldW, 20, disp.c_str(), false);
        if (ui_->win98Button(532, "Auto", lx + imgFieldW + imgGap, y + 12, autoW, 20, false))
            map_.topImagePath = "sprites/" + mapBaseName() + "top.png";
        if (ui_->win98Button(533, "X", lx + imgFieldW + imgGap + autoW + imgGap, y + 12, clearW, 20, false))
            map_.topImagePath.clear();
    }
    y += 34;

    // Close
    if (ui_->win98Button(529, "Close", lx, y, panelW - pad * 2, btnSz, false))
        showMapProps_ = false;
}

void MapEditor::renderToolbar(SDL_Renderer* renderer) {
    // Win98 toolbar panel
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
    SDL_Rect bg = {0, 0, screenW_, TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);
    // Bottom raised edge
    SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
    SDL_RenderDrawLine(renderer, 0, TOOLBAR_H - 2, screenW_, TOOLBAR_H - 2);
    SDL_SetRenderDrawColor(renderer, UI::W98::White.r, UI::W98::White.g, UI::W98::White.b, 255);
    SDL_RenderDrawLine(renderer, 0, TOOLBAR_H - 1, screenW_, TOOLBAR_H - 1);

    if (!ui_) return;

    const int toolBtnW = 62, toolBtnH = TOOLBAR_H - 8, toolGap = 2, toolStartX = 4;
    const char* toolNames[] = {"Tile", "Trigger", "Entity", "Erase", "Select", "Rect"};

    // Tool buttons (IDs 100-105)
    for (int i = 0; i < 6; i++) {
        int bx = toolStartX + i * (toolBtnW + toolGap);
        if (ui_->win98Button(100 + i, toolNames[i], bx, 4, toolBtnW, toolBtnH, (int)currentTool_ == i)) {
            currentTool_ = (EditorTool)i;
            selectedTrigger_ = -1;
            selectedEnemy_   = -1;
            rectStartTX_ = -1; rectStartTY_ = -1;
            trigDragging_ = false;
        }
    }

    // Vertical separator
    auto drawSep = [&](int x) {
        SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
        SDL_RenderDrawLine(renderer, x, 6, x, TOOLBAR_H - 6);
        SDL_SetRenderDrawColor(renderer, UI::W98::White.r, UI::W98::White.g, UI::W98::White.b, 255);
        SDL_RenderDrawLine(renderer, x + 1, 6, x + 1, TOOLBAR_H - 6);
    };

    // 6 buttons × (62+2) = 384, starting at x=4, so right edge = 388
    int sepX = toolStartX + 6 * (toolBtnW + toolGap) + 2;  // 390
    drawSep(sepX);

    // Undo / Redo (IDs 112-113)
    int auxX = sepX + 6;
    if (ui_->win98Button(112, "Undo", auxX,      4, 48, toolBtnH, false)) undo();
    if (ui_->win98Button(113, "Redo", auxX + 50, 4, 48, toolBtnH, false)) redo();

    sepX = auxX + 100 + 2;  // 498
    drawSep(sepX);

    // Grid toggle (ID 114)
    int gridX = sepX + 6;  // 504
    if (ui_->win98Button(114, "Grid", gridX, 4, 48, toolBtnH, showGrid_))
        showGrid_ = !showGrid_;

    // Props panel toggle (ID 116)
    if (ui_->win98Button(116, "Props", gridX + 54, 4, 60, toolBtnH, showMapProps_))
        showMapProps_ = !showMapProps_;

    // Random rotation toggle (ID 117)
    if (ui_->win98Button(117, "Rnd~", gridX + 118, 4, 50, toolBtnH, randomRotation_))
        randomRotation_ = !randomRotation_;

    // No-collision toggle (ID 119)
    if (ui_->win98Button(119, "NoCo", gridX + 172, 4, 50, toolBtnH, noCollision_))
        noCollision_ = !noCollision_;

    // Top layer visibility toggle (ID 118)
    if (!map_.topImagePath.empty()) {
        if (ui_->win98Button(118, "Top", gridX + 226, 4, 40, toolBtnH, showTopLayer_))
            showTopLayer_ = !showTopLayer_;
    }

    // Center: map info
    int centerX = gridX + 274;
    int rightEdge = screenW_ - PALETTE_W - 230;

    // Scene (cutscene editor) toggle — only unlocked while K is held
    {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        bool kHeld = ks && ks[SDL_SCANCODE_K];
        if (ui_->win98Button(120, "Scene", gridX + 280, 4, 60, toolBtnH, showCutsceneEditor_) && kHeld) {
            showCutsceneEditor_ = !showCutsceneEditor_;
            csEditor_.setActive(showCutsceneEditor_);
        }
        if (!kHeld) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 192, 192, 192, 160);
            SDL_Rect dim = {gridX + 280, 4, 60, toolBtnH};
            SDL_RenderFillRect(renderer_, &dim);
        }
    }

    // Save / Play buttons
    if (ui_->win98Button(110, "Save", screenW_ - PALETTE_W - 228, 4, 110, toolBtnH, false))
        wantsModSave_ = true;
    if (ui_->win98Button(111, "Play", screenW_ - PALETTE_W - 112, 4, 110, toolBtnH, false))
        wantsTestPlay_ = true;
}

void MapEditor::renderPalette(SDL_Renderer* renderer) {
    if (!ui_) return;
    int px = screenW_ - PALETTE_W;

    // Win98 panel background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
    SDL_Rect bg = {px, TOOLBAR_H, PALETTE_W, screenH_ - TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);
    // Left border
    SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
    SDL_RenderDrawLine(renderer, px, TOOLBAR_H, px, screenH_);
    SDL_SetRenderDrawColor(renderer, UI::W98::White.r, UI::W98::White.g, UI::W98::White.b, 255);
    SDL_RenderDrawLine(renderer, px + 1, TOOLBAR_H, px + 1, screenH_);

    // Tab buttons (IDs 120-124)
    static const char* tabNames[] = {"All", "Gnd", "Wall", "Ceil", "Prop"};
    int tabCount = (int)PaletteTab::TAB_COUNT;
    int tabW = (PALETTE_W - 6) / tabCount;
    int tabY = TOOLBAR_H + 4;
    int tabH = 22;

    for (int t = 0; t < tabCount; t++) {
        int tx = px + 3 + t * tabW;
        bool active = ((int)paletteTab_ == t);
        if (ui_->win98Button(120 + t, tabNames[t], tx, tabY, tabW - 1, tabH, active)) {
            paletteTab_ = (PaletteTab)t;
            rebuildFilteredPalette();
            paletteScroll_ = 0;
        }
    }

    int contentTop = TOOLBAR_H + tabH + 8;

    // Clip to palette content area
    SDL_Rect clip = {px + 2, contentTop, PALETTE_W - 2, screenH_ - contentTop};
    SDL_RenderSetClipRect(renderer, &clip);

    paletteItemY_.clear();
    paletteItemY_.resize(palette_.size(), -999);

    int y = contentTop + 4 - paletteScroll_;
    std::string lastCat;
    const int rowH = TILE_PREVIEW + 6;
    const int imgSz = TILE_PREVIEW - 4;

    for (int i = 0; i < (int)palette_.size(); i++) {
        auto& pt = palette_[i];

        // Tab filter
        if (paletteTab_ != PaletteTab::All) {
            bool match = false;
            switch (paletteTab_) {
                case PaletteTab::Ground:  match = (pt.category == "ground");  break;
                case PaletteTab::Walls:   match = (pt.category == "walls");   break;
                case PaletteTab::Ceiling: match = (pt.category == "ceiling"); break;
                case PaletteTab::Props:   match = (pt.category == "props");   break;
                default: match = true; break;
            }
            if (!match) continue;
        }

        // Category header
        if (pt.category != lastCat) {
            lastCat = pt.category;
            if (y + 20 >= contentTop && y < screenH_) {
                // Separator line
                SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
                SDL_RenderDrawLine(renderer, px + 4, y + 2, px + PALETTE_W - 4, y + 2);
                std::string catLabel = pt.category;
                for (auto& c : catLabel) c = (char)toupper(c);
                ui_->drawText(catLabel.c_str(), px + 6, y + 5, 11, UI::W98::Navy);
            }
            y += 20;
        }

        paletteItemY_[i] = y;

        if (y + rowH >= contentTop && y < screenH_) {
            bool sel = (i == selectedPalette_);
            bool hover = ui_->pointInRect(ui_->mouseX, ui_->mouseY, px + 3, y, PALETTE_W - 6, rowH - 2);

            // Row background
            if (sel) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 128, 255);
                SDL_Rect hl = {px + 3, y, PALETTE_W - 6, rowH - 2};
                SDL_RenderFillRect(renderer, &hl);
            } else if (hover) {
                ui_->drawWin98Bevel(px + 3, y, PALETTE_W - 6, rowH - 2, true);
            }

            if (hover && ui_->mouseClicked) {
                selectedPalette_ = i;
                ui_->mouseClicked = false;
                ui_->clickCooldownFrames = 3;
            }

            // Tile preview thumbnail (sunken inset)
            int imgX = px + 6, imgY = y + 2;
            ui_->drawWin98Bevel(imgX - 1, imgY - 1, imgSz + 2, imgSz + 2, false);
            SDL_Rect dst = {imgX, imgY, imgSz, imgSz};
            if (pt.texture) {
                SDL_SetTextureColorMod(pt.texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(pt.texture, 255);
                SDL_RenderCopy(renderer, pt.texture, nullptr, &dst);
            } else {
                SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
                SDL_RenderFillRect(renderer, &dst);
            }

            // Name
            SDL_Color nameC = sel ? UI::W98::White : UI::W98::Black;
            ui_->drawText(pt.name.c_str(), px + imgSz + 12, y + 6, 12, nameC);
            if (sel) {
                std::string catLabel = pt.category;
                for (auto& c : catLabel) c = (char)toupper(c);
                ui_->drawText(catLabel.c_str(), px + imgSz + 12, y + 22, 9, UI::W98::White);
            }
        }

        y += rowH;
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    // Win98 scroll bar
    {
        int totalH = paletteContentHeight();
        int viewH  = screenH_ - contentTop;
        if (totalH > viewH) {
            int barX = px + PALETTE_W - 8;
            SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
            SDL_Rect track = {barX, contentTop, 6, viewH};
            SDL_RenderFillRect(renderer, &track);

            float ratio = (float)viewH / totalH;
            int barH = std::max(20, (int)(viewH * ratio));
            float scrollRatio = (totalH > viewH) ? (float)paletteScroll_ / (totalH - viewH) : 0;
            int barY = contentTop + (int)((viewH - barH) * scrollRatio);
            SDL_SetRenderDrawColor(renderer, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
            SDL_Rect bar = {barX, barY, 6, barH};
            SDL_RenderFillRect(renderer, &bar);
            ui_->drawWin98Bevel(barX, barY, 6, barH, true);
        }
    }
}

void MapEditor::drawEditorText(SDL_Renderer* renderer, const char* text, int x, int y, int size, SDL_Color color) {
    // Use cached text rendering if UI::Context is available
    if (ui_) {
        ui_->drawText(text, x, y, size, color);
        return;
    }
    TTF_Font* f = Assets::instance().font(size);
    if (!f || !text || text[0] == '\0') return;
    SDL_Surface* surf = TTF_RenderText_Blended(f, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void MapEditor::generateThumbnail() {
    memset(map_.header.thumbnail, 0, sizeof(map_.header.thumbnail));
    float scaleX = (float)map_.width / 128.0f;
    float scaleY = (float)map_.height / 72.0f;

    for (int py = 0; py < 72; py++) {
        for (int px = 0; px < 128; px++) {
            int tx = (int)(px * scaleX);
            int ty = (int)(py * scaleY);
            if (tx >= map_.width) tx = map_.width - 1;
            if (ty >= map_.height) ty = map_.height - 1;

            uint8_t tile = map_.tiles[ty * map_.width + tx];
            uint8_t r = 60, g = 120, b = 60;
            switch (tile) {
                case TILE_FLOOR:  r = 70; g = 70; b = 75; break;
                case TILE_GRAVEL: r = 90; g = 85; b = 75; break;
                case TILE_WOOD:   r = 120; g = 80; b = 50; break;
                case TILE_SAND:   r = 180; g = 170; b = 120; break;
                case TILE_WALL:   r = 100; g = 90; b = 80; break;
                case TILE_GLASS:  r = 120; g = 160; b = 200; break;
                case TILE_DESK:   r = 80; g = 60; b = 40; break;
                case TILE_BOX:    r = 140; g = 100; b = 50; break;
                default: break;
            }
            int idx = (py * 128 + px) * 3;
            map_.header.thumbnail[idx + 0] = r;
            map_.header.thumbnail[idx + 1] = g;
            map_.header.thumbnail[idx + 2] = b;
        }
    }
}

// Config Screen (shown before editor opens)

void MapEditor::showConfig() {
    showConfig_ = true;
    wantsBack_  = false;
    config_ = EditorConfig{};
    scanAvailableMaps();
}

void MapEditor::scanAvailableMaps() {
    config_.availableMaps.clear();
    const char* dirs[] = {"maps", "romfs/maps", "romfs:/maps"};
    for (const char* dir : dirs) {
        DIR* d = opendir(dir);
        if (!d) continue;
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string fname(entry->d_name);
            if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".csm") {
                config_.availableMaps.push_back(std::string(dir) + "/" + fname);
            }
        }
        closedir(d);
    }

    // Also include maps from all loaded mods
    for (const std::string& p : ModManager::instance().allMapPaths()) {
        // Avoid duplicates
        bool dup = false;
        for (auto& existing : config_.availableMaps) {
            if (existing == p) { dup = true; break; }
        }
        if (!dup) config_.availableMaps.push_back(p);
    }
}

void MapEditor::handleConfigInput(SDL_Event& e) {
    auto& cfg = config_;

    // Mouse clicks on the config screen are handled by win98Button/pointInRect in renderConfig().
    // Consume the event here so it doesn't fall through to editor-canvas handling.
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        return;
    }

    // Character palette for gamepad text input
    static const char charPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
    int& charIdx = cfg.gpCharIdx;

    // Text input mode for name/creator/width/height fields
    if (cfg.textEditing) {
        bool isNumField = (cfg.field == 1 || cfg.field == 2);
        if (e.type == SDL_TEXTINPUT) {
            const char* t = e.text.text;
            if (isNumField) {
                // only accept digits
                for (; *t; ++t)
                    if (*t >= '0' && *t <= '9' && cfg.textBuf.size() < 3)
                        cfg.textBuf += *t;
            } else {
                cfg.textBuf += t;
            }
            return;
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN) {
                if (isNumField) {
                    int v = cfg.textBuf.empty() ? 0 : std::stoi(cfg.textBuf);
                    v = std::max(10, std::min(200, v));
                    if (cfg.field == 1) cfg.mapWidth  = v;
                    else                cfg.mapHeight = v;
                } else if (cfg.field == 3) cfg.mapName = cfg.textBuf;
                else if (cfg.field == 4)   cfg.creator = cfg.textBuf;
                cfg.textEditing = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                cfg.textEditing = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            } else if (e.key.keysym.sym == SDLK_BACKSPACE && !cfg.textBuf.empty()) {
                cfg.textBuf.pop_back();
            }
        }
        // Gamepad text input: DPad L/R = cycle char, A = append, B = backspace, Start = confirm
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            int paletteLen = (int)strlen(charPalette);
            Uint8 btn = remapButton(e.cbutton.button);
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    charIdx = (charIdx - 1 + paletteLen) % paletteLen;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    charIdx = (charIdx + 1) % paletteLen;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    charIdx = (charIdx - 10 + paletteLen) % paletteLen;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    charIdx = (charIdx + 10) % paletteLen;
                    break;
                case SDL_CONTROLLER_BUTTON_A:
                    cfg.textBuf += charPalette[charIdx];
                    break;
                case SDL_CONTROLLER_BUTTON_Y:
                    if (!cfg.textBuf.empty()) cfg.textBuf.pop_back();
                    break;
                case SDL_CONTROLLER_BUTTON_START:
                case SDL_CONTROLLER_BUTTON_X:
                    // Confirm text
                    if (isNumField) {
                        int v = cfg.textBuf.empty() ? 0 : std::stoi(cfg.textBuf);
                        v = std::max(10, std::min(200, v));
                        if (cfg.field == 1) cfg.mapWidth  = v;
                        else                cfg.mapHeight = v;
                    } else if (cfg.field == 3) cfg.mapName = cfg.textBuf;
                    else if (cfg.field == 4)   cfg.creator = cfg.textBuf;
                    cfg.textEditing = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    // Cancel text edit
                    cfg.textEditing = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    break;
            }
        }
        return;
    }

    // Navigation via keyboard
    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        switch (e.key.keysym.sym) {
            case SDLK_UP:
                cfg.field--;
                // In load mode, skip fields 2-10 which are for new map only
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 11)
                    cfg.field = 1;
                break;
            case SDLK_DOWN:
                cfg.field++;
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 11)
                    cfg.field = 11;
                break;
            case SDLK_LEFT:
                if (cfg.field == 0) cfg.action = EditorConfig::Action::NewMap;
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.mapWidth -= 2; if (cfg.mapWidth < 10) cfg.mapWidth = 10;
                }
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::LoadMap) {
                    cfg.loadIdx--; if (cfg.loadIdx < 0) cfg.loadIdx = 0;
                }
                else if (cfg.field == 2) { cfg.mapHeight -= 2; if (cfg.mapHeight < 10) cfg.mapHeight = 10; }
                else if (cfg.field == 5) { cfg.gameMode = 0; }
                break;
            case SDLK_RIGHT:
                if (cfg.field == 0) {
                    cfg.action = EditorConfig::Action::LoadMap;
                    if (cfg.availableMaps.empty()) cfg.action = EditorConfig::Action::NewMap;
                }
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.mapWidth += 2; if (cfg.mapWidth > 200) cfg.mapWidth = 200;
                }
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::LoadMap) {
                    cfg.loadIdx++;
                    if (cfg.loadIdx >= (int)cfg.availableMaps.size())
                        cfg.loadIdx = std::max(0, (int)cfg.availableMaps.size() - 1);
                }
                else if (cfg.field == 2) { cfg.mapHeight += 2; if (cfg.mapHeight > 200) cfg.mapHeight = 200; }
                else if (cfg.field == 5) { cfg.gameMode = 1; }
                break;
            case SDLK_RETURN:
                if ((cfg.field == 1 || cfg.field == 2) && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.textEditing = true;
                    cfg.textBuf = std::to_string(cfg.field == 1 ? cfg.mapWidth : cfg.mapHeight);
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                } else if ((cfg.field == 3 || cfg.field == 4) && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.textEditing = true;
                    cfg.textBuf = (cfg.field == 3) ? cfg.mapName : cfg.creator;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                }
                else if (cfg.field == 6) { // OK
                    if (cfg.action == EditorConfig::Action::LoadMap && !cfg.availableMaps.empty()) {
                        int idx = cfg.loadIdx;
                        if (idx >= 0 && idx < (int)cfg.availableMaps.size()) {
                            loadMap(cfg.availableMaps[idx]);
                            savePath_ = cfg.availableMaps[idx];
                        }
                    } else {
                        newMap(cfg.mapWidth, cfg.mapHeight);
                        map_.name    = cfg.mapName;
                        map_.creator = cfg.creator;
                        map_.gameMode = (uint8_t)cfg.gameMode;
                        std::string safeName = cfg.mapName;
                        for (char& c : safeName) {
                            if (c == ' ' || c == '/' || c == '\\') c = '_';
                        }
                        savePath_ = "maps/" + safeName + ".csm";
                    }
                    showConfig_ = false;
                }
                else if (cfg.field == 7) { // Cancel
                    wantsBack_ = true;
                    showConfig_ = false;
                }
                break;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                wantsBack_ = true;
                showConfig_ = false;
                break;
        }
        // Clamp field
        if (cfg.field < 0) cfg.field = 0;
        if (cfg.field > 7) cfg.field = 7;
    }

    // Navigation via gamepad
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = remapButton(e.cbutton.button);
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                cfg.field--;
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 6)
                    cfg.field = 1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                cfg.field++;
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 6)
                    cfg.field = 6;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                if (cfg.field == 0) cfg.action = EditorConfig::Action::NewMap;
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.mapWidth -= 2; if (cfg.mapWidth < 10) cfg.mapWidth = 10;
                }
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::LoadMap) {
                    cfg.loadIdx--; if (cfg.loadIdx < 0) cfg.loadIdx = 0;
                }
                else if (cfg.field == 2) { cfg.mapHeight -= 2; if (cfg.mapHeight < 10) cfg.mapHeight = 10; }
                else if (cfg.field == 5) { cfg.gameMode = 0; }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (cfg.field == 0) {
                    cfg.action = EditorConfig::Action::LoadMap;
                    if (cfg.availableMaps.empty()) cfg.action = EditorConfig::Action::NewMap;
                }
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.mapWidth += 2; if (cfg.mapWidth > 200) cfg.mapWidth = 200;
                }
                else if (cfg.field == 1 && cfg.action == EditorConfig::Action::LoadMap) {
                    cfg.loadIdx++;
                    if (cfg.loadIdx >= (int)cfg.availableMaps.size())
                        cfg.loadIdx = std::max(0, (int)cfg.availableMaps.size() - 1);
                }
                else if (cfg.field == 2) { cfg.mapHeight += 2; if (cfg.mapHeight > 200) cfg.mapHeight = 200; }
                else if (cfg.field == 5) { cfg.gameMode = 1; }
                break;
            case SDL_CONTROLLER_BUTTON_A: {
                // Handle A button directly for each field (no recursive call)
                if ((cfg.field == 3 || cfg.field == 4) && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.textEditing = true;
                    cfg.textBuf = (cfg.field == 3) ? cfg.mapName : cfg.creator;
                }
                else if (cfg.field == 6) { // OK
                    if (cfg.action == EditorConfig::Action::LoadMap && !cfg.availableMaps.empty()) {
                        int idx = cfg.loadIdx;
                        if (idx >= 0 && idx < (int)cfg.availableMaps.size()) {
                            loadMap(cfg.availableMaps[idx]);
                            savePath_ = cfg.availableMaps[idx];
                        }
                    } else {
                        newMap(cfg.mapWidth, cfg.mapHeight);
                        map_.name    = cfg.mapName;
                        map_.creator = cfg.creator;
                        map_.gameMode = (uint8_t)cfg.gameMode;
                        std::string safeName = cfg.mapName;
                        for (char& c : safeName) {
                            if (c == ' ' || c == '/' || c == '\\') c = '_';
                        }
                        savePath_ = "maps/" + safeName + ".csm";
                    }
                    showConfig_ = false;
                }
                else if (cfg.field == 7) { // Cancel
                    wantsBack_ = true;
                    showConfig_ = false;
                }
                else if (cfg.field == 0) {
                    // Toggle action
                    if (cfg.action == EditorConfig::Action::NewMap)
                        cfg.action = EditorConfig::Action::LoadMap;
                    else
                        cfg.action = EditorConfig::Action::NewMap;
                    if (cfg.availableMaps.empty()) cfg.action = EditorConfig::Action::NewMap;
                }
                break;
            }
            case SDL_CONTROLLER_BUTTON_B:
                wantsBack_ = true;
                showConfig_ = false;
                break;
        }
        if (cfg.field < 0) cfg.field = 0;
        if (cfg.field > 7) cfg.field = 7;
    }
}

void MapEditor::renderConfig(SDL_Renderer* renderer) {
    auto& cfg = config_;
    char buf[256];

    // Win98 desktop background
    ui_->drawDesktop();

    // Centered window
    const int winW  = 520;
    const int winH  = screenH_ - 100;
    const int winX  = (screenW_ - winW) / 2;
    const int winY  = 40;
    const char* winTitle = (cfg.action == EditorConfig::Action::NewMap)
                           ? "New Map" : "Load Map";
    ui_->drawWin98Window(winX, winY, winW, winH, winTitle);

    // X button closes back to main menu
    {
        const int cbSz = UI::W98::TitleH - 4;
        if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY,
                winX + winW - 3 - cbSz, winY + 5, cbSz, cbSz)) {
            wantsBack_ = true;
            showConfig_ = false;
            return;
        }
    }

    // Content area starts just below title bar
    const int padX   = 14;
    const int rowH   = 28;
    const int rowGap = 8;
    const int step   = rowH + rowGap;
    int y = winY + UI::W98::TitleH + 12;

    // Helper: non-text field row with Win98 bevel + < > buttons
    // Layout: label (90px) | sunken value box (middle) | < > buttons (22px each)
    auto drawArrowField = [&](int idx, const char* label, const char* value) {
        bool sel = (cfg.field == idx);
        int rx = winX + padX;
        int rw = winW - padX * 2;

        const int labelW  = 90;
        const int btnW    = 22;
        const int btnGapR = 4;
        const int btnH    = rowH - 4;
        int leftBtnX  = rx + rw - btnGapR - btnW * 2 - 2;
        int rightBtnX = rx + rw - btnGapR - btnW;
        int valX      = rx + labelW;
        int valW      = leftBtnX - valX - 4;

        // Outer bevel for the whole row
        ui_->drawWin98Bevel(rx, y, rw, rowH, false);
        SDL_SetRenderDrawColor(renderer, sel ? 215 : 192, sel ? 215 : 192, sel ? 215 : 192, 255);
        SDL_Rect fill = {rx + 2, y + 2, rw - 4, rowH - 4};
        SDL_RenderFillRect(renderer, &fill);

        // Label on the left
        ui_->drawText(label, rx + 8, y + 6, 14, sel ? UI::W98::Navy : UI::W98::Black);

        // Sunken value box in the middle - editable for numeric fields 1/2
        bool numEditing = cfg.textEditing && cfg.field == idx && (idx == 1 || idx == 2);
        float blinkT = numEditing ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
        const char* displayVal = numEditing ? cfg.textBuf.c_str() : value;
        ui_->drawWin98TextField(valX, y + 2, valW, rowH - 4, displayVal, numEditing, false, blinkT);

        // Click the value box to start typing a number (fields 1/2 only)
        if ((idx == 1 || idx == 2) && !cfg.textEditing &&
            ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, valX, y + 2, valW, rowH - 4)) {
            cfg.field = idx;
            cfg.textEditing = true;
            cfg.textBuf = std::to_string(idx == 1 ? cfg.mapWidth : cfg.mapHeight);
#ifndef __SWITCH__
            SDL_StartTextInput();
#endif
        }

        // < button (hidden while value box is being typed)
        if (!numEditing && ui_->win98Button(200 + idx * 2, "<", leftBtnX, y + 2, btnW, btnH, false)) {
            cfg.field = idx;
            SDL_Event fakeKey; memset(&fakeKey, 0, sizeof(fakeKey));
            fakeKey.type = SDL_KEYDOWN;
            fakeKey.key.keysym.sym = SDLK_LEFT;
            handleConfigInput(fakeKey);
        }
        // > button
        if (!numEditing && ui_->win98Button(200 + idx * 2 + 1, ">", rightBtnX, y + 2, btnW, btnH, false)) {
            cfg.field = idx;
            SDL_Event fakeKey; memset(&fakeKey, 0, sizeof(fakeKey));
            fakeKey.type = SDL_KEYDOWN;
            fakeKey.key.keysym.sym = SDLK_RIGHT;
            handleConfigInput(fakeKey);
        }

        // Click row to select
        if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, rx, y, rw, rowH)) {
            cfg.field = idx;
        }
        y += step;
    };

    // Helper: text field row with Win98 text field
    // Layout: label (90px left) | text field (remainder, same line)
    auto drawTextFieldRow = [&](int idx, const char* label) {
        bool edit = (cfg.textEditing && cfg.field == idx);
        const char* curVal = (idx == 3) ? cfg.mapName.c_str() : cfg.creator.c_str();
        int rx = winX + padX;
        int rw = winW - padX * 2;

        const int labelW = 90;
        int fieldX = rx + labelW;
        int fieldW = rw - labelW;

        // Label on the left, vertically centered with the field
        ui_->drawText(label, rx + 2, y + 6, 13, UI::W98::Black);

        // Text field to the right of the label
        const char* displayVal = edit ? cfg.textBuf.c_str() : curVal;
        ui_->drawWin98TextField(fieldX, y, fieldW, rowH, displayVal, edit, false,
                                edit ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f);

        if (edit) {
            // Gamepad char palette below the field
            static const char charPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
            int palLen = (int)strlen(charPalette);
            int cellW = 18, cellH = 22;
            int cols = 20;
            int rows = (palLen + cols - 1) / cols;
            int palStartX = rx;
            int palStartY = y + rowH + 4;

            ui_->drawWin98Bevel(palStartX - 2, palStartY - 4, cols * cellW + 8, rows * cellH + 8, false);
            SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
            SDL_Rect palBg = {palStartX, palStartY, cols * cellW + 4, rows * cellH};
            SDL_RenderFillRect(renderer, &palBg);

            for (int ci = 0; ci < palLen; ci++) {
                int col = ci % cols;
                int row = ci / cols;
                int cxPos = palStartX + col * cellW;
                int cyPos = palStartY + row * cellH;
                bool isSel = (ci == cfg.gpCharIdx);
                if (isSel) {
                    SDL_SetRenderDrawColor(renderer, 0, 0, 128, 255);
                    SDL_Rect bg2 = {cxPos, cyPos, cellW - 1, cellH - 1};
                    SDL_RenderFillRect(renderer, &bg2);
                }
                char ch[2] = { charPalette[ci], 0 };
                SDL_Color chColor = isSel ? UI::W98::White : UI::W98::Black;
                ui_->drawText(ch, cxPos + 3, cyPos + 3, 13, chColor);
            }
            ui_->drawText("A:type  Y:del  X/START:done  B:cancel",
                           palStartX, palStartY + rows * cellH + 6, 11, UI::W98::Shadow);
            y += rowH + 4 + rows * cellH + 20;
        } else {
            // Click to start editing
            if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, fieldX, y, fieldW, rowH)) {
                cfg.field = idx;
                cfg.textEditing = true;
                cfg.textBuf = (idx == 3) ? cfg.mapName : cfg.creator;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
            }
            y += rowH + rowGap;
        }
    };

    // Field 0: Action (arrow selector)
    const char* actionStr = (cfg.action == EditorConfig::Action::NewMap) ? "New Map" : "Load Map";
    drawArrowField(0, "Action:", actionStr);

    // Thin separator
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 200);
    SDL_RenderDrawLine(renderer, winX + padX, y, winX + winW - padX, y);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
    SDL_RenderDrawLine(renderer, winX + padX, y + 1, winX + winW - padX, y + 1);
    y += 10;

    if (cfg.action == EditorConfig::Action::NewMap) {
        snprintf(buf, sizeof(buf), "%d", cfg.mapWidth);
        drawArrowField(1, "Width:", buf);
        snprintf(buf, sizeof(buf), "%d", cfg.mapHeight);
        drawArrowField(2, "Height:", buf);
        drawTextFieldRow(3, "Map Name:");
        drawTextFieldRow(4, "Creator:");
    } else {
        // Load mode: scrollable map list
        ui_->drawText("Select Map:", winX + padX, y, 13, UI::W98::Black);
        y += 18;

        if (cfg.availableMaps.empty()) {
            ui_->drawText("No .csm files found in maps/ or any mod",
                          winX + padX + 4, y + 4, 13, UI::W98::Shadow);
            y += step * 2;
        } else {
            int listX = winX + padX;
            int listW = winW - padX * 2;
            int startShow = std::max(0, cfg.loadIdx - 4);
            int endShow   = std::min((int)cfg.availableMaps.size(), startShow + 8);
            for (int i = startShow; i < endShow; i++) {
                const std::string& fullPath = cfg.availableMaps[i];
                std::string fname = fullPath;
                size_t slash = fname.find_last_of('/');
                if (slash != std::string::npos) fname = fname.substr(slash + 1);
                std::string prefix;
                size_t mapsPos = fullPath.rfind("/maps/");
                if (mapsPos != std::string::npos && mapsPos > 0) {
                    std::string beforeMaps = fullPath.substr(0, mapsPos);
                    size_t ps = beforeMaps.find_last_of('/');
                    std::string modName = (ps != std::string::npos)
                                         ? beforeMaps.substr(ps + 1) : beforeMaps;
                    if (modName != "romfs" && modName != ".") prefix = "[" + modName + "] ";
                }
                std::string label = prefix + fname;
                bool isCur = (i == cfg.loadIdx);
                if (ui_->win98Button(300 + i, label.c_str(), listX, y, listW, rowH, isCur)) {
                    cfg.loadIdx = i;
                    cfg.field   = 5;
                }
                y += rowH + 2;
            }
            y += 8;
        }
    }

    // Bottom buttons (80px each, centered)
    const int btnH   = 28;
    const int btnW   = 80;
    const int btnGap = 16;
    int btnY  = winY + winH - btnH - 14;
    int btn1X = winX + winW / 2 - btnW - btnGap / 2;
    int btn2X = winX + winW / 2 + btnGap / 2;

    if (ui_->win98Button(400, "OK", btn1X, btnY, btnW, btnH, cfg.field == 6)) {
        if (cfg.action == EditorConfig::Action::LoadMap && !cfg.availableMaps.empty()) {
            int idx = std::max(0, std::min(cfg.loadIdx, (int)cfg.availableMaps.size() - 1));
            loadMap(cfg.availableMaps[idx]);
            savePath_ = cfg.availableMaps[idx];
        } else {
            newMap(cfg.mapWidth, cfg.mapHeight);
            map_.name    = cfg.mapName;
            map_.creator = cfg.creator;
            map_.gameMode = (uint8_t)cfg.gameMode;
            std::string safe = cfg.mapName;
            for (char& c : safe) { if (c == ' ' || c == '/' || c == '\\') c = '_'; }
            if (safe.empty()) safe = "untitled";
            savePath_ = "maps/" + safe + ".csm";
        }
        showConfig_ = false;
        return;
    }
    if (ui_->win98Button(401, "Cancel", btn2X, btnY, btnW, btnH, cfg.field == 7)) {
        wantsBack_  = true;
        showConfig_ = false;
        return;
    }

    // Status bar with controls hint
    ui_->drawWin98StatusBar(screenH_ - 24,
        "\xe2\x86\x91\xe2\x86\x93 navigate   \xe2\x86\x90\xe2\x86\x92 adjust   Enter confirm   Esc cancel");
}

// Gamepad Support

void MapEditor::handleGamepadInput(SDL_Event& e) {
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        useGamepad_ = true;
        Uint8 btn = remapButton(e.cbutton.button);

        switch (btn) {
            case SDL_CONTROLLER_BUTTON_A: { // Place / confirm (like left click)
                if (ui_ && ui_->clickCooldownFrames > 0) break;  // 3-frame cooldown
                mouseDown_ = true;
                mouseX_ = (int)cursorX_;
                mouseY_ = (int)cursorY_;
                SDL_Event fakeClick;
                memset(&fakeClick, 0, sizeof(fakeClick));
                fakeClick.type = SDL_MOUSEBUTTONDOWN;
                fakeClick.button.button = SDL_BUTTON_LEFT;
                fakeClick.button.x = (int)cursorX_;
                fakeClick.button.y = (int)cursorY_;
                handleInput(fakeClick);
                if (ui_) ui_->clickCooldownFrames = 3;
                break;
            }
            case SDL_CONTROLLER_BUTTON_B: // Erase (like right click)
                rightDown_ = true;
                break;
            case SDL_CONTROLLER_BUTTON_X: // Cycle tools forward
                currentTool_ = (EditorTool)(((int)currentTool_ + 1) % 5);
                selectedTrigger_ = -1;
                selectedEnemy_ = -1;
                break;
            case SDL_CONTROLLER_BUTTON_Y: // Quick save
                wantsModSave_ = true;
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: // Zoom out
                zoom_ /= 1.15f;
                if (zoom_ < ZOOM_MIN) zoom_ = ZOOM_MIN;
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: // Zoom in
                zoom_ *= 1.15f;
                if (zoom_ > ZOOM_MAX) zoom_ = ZOOM_MAX;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (currentTool_ == EditorTool::Tile && !palette_.empty()) {
                    selectedPalette_--;
                    if (selectedPalette_ < 0) selectedPalette_ = (int)palette_.size() - 1;
                    scrollPaletteToSelection();
                }
                if (currentTool_ == EditorTool::Trigger) {
                    int v = (int)triggerGhost_.type + 1;
                    if (v >= (int)TriggerType::COUNT) v = 0;
                    triggerGhost_.type = (TriggerType)v;
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (currentTool_ == EditorTool::Tile && !palette_.empty()) {
                    selectedPalette_++;
                    if (selectedPalette_ >= (int)palette_.size()) selectedPalette_ = 0;
                    scrollPaletteToSelection();
                }
                if (currentTool_ == EditorTool::Entity) entitySpawnType_ = (entitySpawnType_ + 1) % ENTITY_TYPE_COUNT;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                showGrid_ = !showGrid_;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                showUI_ = !showUI_;
                break;
            case SDL_CONTROLLER_BUTTON_START:
                wantsTestPlay_ = true;
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:  // L3 = Undo
                undo();
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:  // R3 = Redo
                redo();
                break;
            case SDL_CONTROLLER_BUTTON_BACK: // Delete selection  (- button on Switch)
                // If nothing selected, treat as exit-to-menu
                if (selectedTrigger_ < 0 && selectedEnemy_ < 0) {
                    wantsBack_ = true;
                } else {
                    pushUndo();
                    if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
                        map_.triggers.erase(map_.triggers.begin() + selectedTrigger_);
                        selectedTrigger_ = -1;
                    }
                    if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
                        map_.enemySpawns.erase(map_.enemySpawns.begin() + selectedEnemy_);
                        selectedEnemy_ = -1;
                    }
                }
                break;
        }
    }

    if (e.type == SDL_CONTROLLERBUTTONUP) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) mouseDown_ = false;
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) rightDown_ = false;
    }
}

void MapEditor::updateGamepadCursor(float dt) {
    SDL_GameController* gc = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(i);
            gc = SDL_GameControllerFromInstanceID(jid);
            break;
        }
    }
    if (!gc) return;

    // Right stick moves cursor
    float rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
    float ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
    if (fabsf(rx) > 0.15f || fabsf(ry) > 0.15f) {
        useGamepad_ = true;
        cursorX_ += rx * gpCursorSpeed_ * dt;
        cursorY_ += ry * gpCursorSpeed_ * dt;
        cursorX_ = fmaxf(0, fminf((float)screenW_, cursorX_));
        cursorY_ = fmaxf(0, fminf((float)screenH_, cursorY_));
        mouseX_ = (int)cursorX_;
        mouseY_ = (int)cursorY_;
    }

    // Left stick pans the camera
    float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    if (fabsf(lx) > 0.2f || fabsf(ly) > 0.2f) {
        float panSpeed = 500.0f / zoom_ * dt;
        camera_.pos.x += lx * panSpeed;
        camera_.pos.y += ly * panSpeed;
    }

    // ZR = hold to paint continuously
    bool zrHeld = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
    // ZL = erase continuously
    bool zlHeld = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
    if (zrHeld)  mouseDown_ = true;
    if (zlHeld)  rightDown_ = true;
    if (!zrHeld && useGamepad_) mouseDown_ = false;
    if (!zlHeld && useGamepad_) rightDown_ = false;
}

void MapEditor::renderCursor(SDL_Renderer* renderer) {
    if (!useGamepad_ && !touchActive_) return;

    float cx = useGamepad_ ? cursorX_ : touchX_;
    float cy = useGamepad_ ? cursorY_ : touchY_;

    const int gap = 5, len = 12, half = 1;
    int icx = (int)cx, icy = (int)cy;
    SDL_Rect arms[4] = {
        {icx - half, icy - gap - len, 2, len},
        {icx - half, icy + gap,       2, len},
        {icx - gap - len, icy - half, len, 2},
        {icx + gap,       icy - half, len, 2},
    };
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 210);
    for (auto& r : arms) { SDL_Rect o = {r.x-1,r.y-1,r.w+2,r.h+2}; SDL_RenderFillRect(renderer, &o); }
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 235);
    for (auto& r : arms) SDL_RenderFillRect(renderer, &r);
}

// Touch Support

void MapEditor::handleTouchInput(SDL_Event& e) {
    if (e.type == SDL_FINGERDOWN) {
        touchActive_ = true;
        touchX_ = e.tfinger.x * screenW_;
        touchY_ = e.tfinger.y * screenH_;
        mouseX_ = (int)touchX_;
        mouseY_ = (int)touchY_;

        // Toolbar touch detection
        if (showUI_ && mouseY_ < TOOLBAR_H) {
            for (int i = 0; i < 6; i++) {
                int bx = 4 + i * 80;
                if (mouseX_ >= bx && mouseX_ < bx + 76 && mouseY_ >= 4 && mouseY_ < TOOLBAR_H - 4) {
                    currentTool_ = (EditorTool)i;
                    selectedTrigger_ = -1;
                    selectedEnemy_ = -1;
                    break;
                }
            }
            // Save button
            int saveBx = screenW_ - PALETTE_W - 230;
            if (mouseX_ >= saveBx && mouseX_ < saveBx + 106 && mouseY_ >= 6 && mouseY_ < TOOLBAR_H - 6) {
                wantsModSave_ = true;
            }
            // Test Play button
            int playBx = screenW_ - PALETTE_W - 118;
            if (mouseX_ >= playBx && mouseX_ < playBx + 106 && mouseY_ >= 6 && mouseY_ < TOOLBAR_H - 6) {
                wantsTestPlay_ = true;
            }
            mouseDown_ = false;  // consume touch
            return;
        }

        // Palette touch detection
        if (showUI_ && mouseX_ >= screenW_ - PALETTE_W) {
            for (int i = 0; i < (int)paletteItemY_.size() && i < (int)palette_.size(); i++) {
                if (mouseY_ >= paletteItemY_[i] && mouseY_ < paletteItemY_[i] + TILE_PREVIEW) {
                    selectedPalette_ = i;
                    break;
                }
            }
            mouseDown_ = false;
            return;
        }

        mouseDown_ = true;
    }
    if (e.type == SDL_FINGERUP) {
        touchActive_ = false;
        mouseDown_ = false;
    }
    if (e.type == SDL_FINGERMOTION) {
        touchX_ = e.tfinger.x * screenW_;
        touchY_ = e.tfinger.y * screenH_;
        mouseX_ = (int)touchX_;
        mouseY_ = (int)touchY_;

        // Two-finger pan
        if (e.tfinger.fingerId > 0) {
            camera_.pos.x -= e.tfinger.dx * screenW_ / zoom_;
            camera_.pos.y -= e.tfinger.dy * screenH_ / zoom_;
            mouseDown_ = false;
        }
    }

    // Pinch zoom
    if (e.type == SDL_MULTIGESTURE) {
        if (fabsf(e.mgesture.dDist) > 0.002f) {
            float oldZoom = zoom_;
            zoom_ += e.mgesture.dDist * 5.0f;
            zoom_ = fmaxf(ZOOM_MIN, fminf(ZOOM_MAX, zoom_));

            float gx = e.mgesture.x * screenW_;
            float gy = e.mgesture.y * screenH_;
            float wx = gx / oldZoom + camera_.pos.x;
            float wy = (gy - uiToolbarH()) / oldZoom + camera_.pos.y;
            camera_.pos.x = wx - gx / zoom_;
            camera_.pos.y = wy - (gy - uiToolbarH()) / zoom_;
        }
    }
}
