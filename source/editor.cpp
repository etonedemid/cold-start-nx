// ─── editor.cpp ─── Map Editor implementation ───────────────────────────────
#include "editor.h"
#include "mod.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#  define mkdir(p, m) _mkdir(p)
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

// ═════════════════════════════════════════════════════════════════════════════
//  Init / Shutdown
// ═════════════════════════════════════════════════════════════════════════════

bool MapEditor::init(SDL_Renderer* renderer, int screenW, int screenH, UI::Context* ui) {
    renderer_ = renderer;
    ui_       = ui;
    screenW_  = screenW;
    screenH_  = screenH;
    zoom_     = 1.0f;
    cursorX_  = screenW / 2.0f;
    cursorY_  = screenH / 2.0f;

    loadPalette();
    newMap(30, 20); // default map size
    return true;
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

// ═════════════════════════════════════════════════════════════════════════════
//  Undo / Redo
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::pushUndo() {
    UndoState s;
    s.tiles       = map_.tiles;
    s.ceiling     = map_.ceiling;
    s.triggers    = map_.triggers;
    s.enemySpawns = map_.enemySpawns;
    undoStack_.push_back(std::move(s));
    if ((int)undoStack_.size() > UNDO_MAX) undoStack_.pop_front();
    redoStack_.clear();   // new action invalidates redo history
}

void MapEditor::undo() {
    if (undoStack_.empty()) return;
    UndoState cur;
    cur.tiles       = map_.tiles;
    cur.ceiling     = map_.ceiling;
    cur.triggers    = map_.triggers;
    cur.enemySpawns = map_.enemySpawns;
    redoStack_.push_back(std::move(cur));
    if ((int)redoStack_.size() > UNDO_MAX) redoStack_.pop_front();
    auto& s = undoStack_.back();
    map_.tiles       = s.tiles;
    map_.ceiling     = s.ceiling;
    map_.triggers    = s.triggers;
    map_.enemySpawns = s.enemySpawns;
    undoStack_.pop_back();
    selectedTrigger_ = -1;
    selectedEnemy_   = -1;
}

void MapEditor::redo() {
    if (redoStack_.empty()) return;
    UndoState cur;
    cur.tiles       = map_.tiles;
    cur.ceiling     = map_.ceiling;
    cur.triggers    = map_.triggers;
    cur.enemySpawns = map_.enemySpawns;
    undoStack_.push_back(std::move(cur));
    if ((int)undoStack_.size() > UNDO_MAX) undoStack_.pop_front();
    auto& s = redoStack_.back();
    map_.tiles       = s.tiles;
    map_.ceiling     = s.ceiling;
    map_.triggers    = s.triggers;
    map_.enemySpawns = s.enemySpawns;
    redoStack_.pop_back();
    selectedTrigger_ = -1;
    selectedEnemy_   = -1;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Palette loading
// ═════════════════════════════════════════════════════════════════════════════

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

    // Also try to pick up tiles from the Assets system (already-loaded sprites)
    auto& a = Assets::instance();
    auto tryAdd = [&](const char* name, const char* spritePath, uint8_t type, const char* cat) {
        // Only add if not already in palette (match by name globally)
        for (auto& pt : palette_) {
            if (pt.name == name) return;
        }
        SDL_Texture* t = a.tex(spritePath);
        if (!t) return;
        EditorTile et;
        et.name     = name;
        et.path     = spritePath;
        et.texture  = t;
        et.tileType = type;
        et.category = cat;
        palette_.push_back(et);
    };

    // Ensure basic tiles exist even if folders are empty
    tryAdd("Grass",    "sprites/tiles/grass.png",    TILE_GRASS,  "ground");
    tryAdd("Floor",    "sprites/tiles/floor.png",    TILE_FLOOR,  "ground");
    tryAdd("Gravel",   "sprites/tiles/gravel.png",   TILE_GRAVEL, "ground");
    tryAdd("Wood",     "sprites/tiles/wood.png",     TILE_WOOD,   "ground");
    tryAdd("Sand",     "sprites/tiles/sand.png",     TILE_SAND,   "ground");
    tryAdd("Wall",     "sprites/tiles/floor.png",    TILE_WALL,   "walls");
    tryAdd("Glass",    "sprites/tiles/glasstile.png", TILE_GLASS, "ceiling");
    tryAdd("Desk",     "sprites/tiles/desk.png",     TILE_DESK,   "props");
    tryAdd("Box",      "sprites/props/box.png",      TILE_BOX,    "props");

    buildTileTextureLookup();
    rebuildFilteredPalette();
    printf("Editor palette: %d tiles loaded\n", (int)palette_.size());
}

// Build a canonical tileType → texture map for rendering.
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

    // Filename stem → tile type lookup
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
        // gravel-grass transition variants — not user-placeable tiles
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
            // Truly unknown file — assign custom slot
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

// ── Palette scroll helpers ──

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

// ═════════════════════════════════════════════════════════════════════════════
//  Map operations
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::newMap(int w, int h) {
    map_.width  = w;
    map_.height = h;
    map_.tiles.assign(w * h, TILE_GRASS);
    map_.ceiling.assign(w * h, CEIL_NONE);
    map_.triggers.clear();
    map_.enemySpawns.clear();
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
    } else {
        saveMessage_ = "Save failed!";
    }
    saveMessageTimer_ = 2.5f;
    return ok;
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
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Hit-testing helpers
// ═════════════════════════════════════════════════════════════════════════════

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

// ═════════════════════════════════════════════════════════════════════════════
//  Input
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::handleInput(SDL_Event& e) {
    if (!active_) return;

    // Config screen intercepts all input
    if (showConfig_) {
        handleConfigInput(e);
        return;
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

        // ── Toolbar click detection ──
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

        // ── Palette click ──
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

        // ── Map area click: Select tool handles trigger selection + resize start ──
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
            mouseDown_ = false; draggingResize_ = false;
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

        // ── Resize drag ──
        if (draggingResize_ && selectedTrigger_ >= 0) {
            float wx = screenToWorldX(mouseX_);
            float wy = screenToWorldY(mouseY_);
            float dx = wx - dragStartX_;
            float dy = wy - dragStartY_;
            auto& t = map_.triggers[selectedTrigger_];

            // Resize based on which corner is being dragged
            float minSz = (float)TILE_SIZE;
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
            // ── Zoom in/out ──
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
                // Delete selected trigger/enemy
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
                    };
                    static const int kTypeCount = 8;
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

// ═════════════════════════════════════════════════════════════════════════════
//  Update
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::update(float dt) {
    if (!active_) return;
    if (showConfig_) return;

    // Tick timers
    if (saveMessageTimer_ > 0) saveMessageTimer_ -= dt;

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

    // Paint/erase on mouse hold (only in map area, not dragging resize)
    if (!draggingResize_ && mouseX_ < screenW_ - uiPaletteW() && mouseY_ > uiToolbarH()) {
        float wx = screenToWorldX(mouseX_);
        float wy = screenToWorldY(mouseY_);
        int tx = (int)(wx / TILE_SIZE);
        int ty = (int)(wy / TILE_SIZE);

        if (mouseDown_) {
            switch (currentTool_) {
                case EditorTool::Tile: {
                    if (!undoPushedForStroke_) { pushUndo(); undoPushedForStroke_ = true; }
                    int half = (brushSize_ - 1) / 2;
                    for (int dy = -half; dy <= half; dy++)
                        for (int dx = -half; dx <= half; dx++)
                            paintTile(tx + dx, ty + dy);
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
                    pushUndo();
                    placeTrigger(wx, wy);
                    mouseDown_ = false;
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
            // Right-click always erases (tiles + triggers + enemies)
            eraseTile(tx, ty);
            eraseTriggerAt(wx, wy);
            eraseEnemyAt(wx, wy);
            rightDown_ = false;  // single action
        }
    }
}

void MapEditor::paintTile(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= map_.width || ty >= map_.height) return;
    if (selectedPalette_ >= 0 && selectedPalette_ < (int)palette_.size()) {
        auto& pt = palette_[selectedPalette_];
        int idx = ty * map_.width + tx;
        if (pt.category == "ceiling") {
            // Ceiling tiles go into the ceiling layer — leave the floor tile unchanged
            map_.ceiling[idx] = CEIL_GLASS;
        } else {
            // Floor / walls / props go into the tiles layer — leave ceiling layer unchanged
            map_.tiles[idx] = pt.tileType;
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

// ═════════════════════════════════════════════════════════════════════════════
//  Rendering (all coordinates use zoom_)
// ═════════════════════════════════════════════════════════════════════════════

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

    // ── Map tiles ──
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

    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            uint8_t tile = map_.tiles[y * map_.width + x];
            int sx = worldToScreenX((float)(x * TILE_SIZE));
            int sy = worldToScreenY((float)(y * TILE_SIZE));
            SDL_Rect dst = {sx, sy, (int)ceilf(ts), (int)ceilf(ts)};

            SDL_Texture* tex = tileTextures_[tile];
            if (tex) {
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
            } else {
                SDL_Color c = {60, 120, 60, 255};
                if (tile == TILE_WALL) c = {100, 90, 80, 255};
                else if (tile == TILE_FLOOR) c = {70, 70, 75, 255};
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(renderer, &dst);
            }

            if (map_.ceiling[y * map_.width + x] == CEIL_GLASS) {
                SDL_SetRenderDrawColor(renderer, 100, 160, 220, 40);
                SDL_RenderFillRect(renderer, &dst);
            }
        }
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

    renderTriggers(renderer);
    renderEntitySpawns(renderer);

    // ── Cursor highlight ──
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

    if (showUI_) {
        renderToolbar(renderer);
        renderPalette(renderer);
    }

    // ── Properties panel for selected entity/trigger ──
    if (showUI_ && (selectedEnemy_ >= 0 || selectedTrigger_ >= 0)) {
        renderPropertiesPanel(renderer);
    }

    // ── Status bar (bottom-left) ──
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
        snprintf(countStr, sizeof(countStr), "Triggers: %d  Enemies: %d",
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

    // ── Gamepad / touch cursor ──
    renderCursor(renderer);
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

        switch (t.type) {
            case TriggerType::LevelStart:
                SDL_SetRenderDrawColor(renderer, 50, 255, 50, 100);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 50, 255, 50, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                drawEditorText(renderer, "START", r.x + 4, r.y + 4, 12, {50, 255, 50, 255});
                break;
            case TriggerType::LevelEnd: {
                SDL_SetRenderDrawColor(renderer, 255, 200, 50, 100);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 200, 50, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                const char* condStr = "GOAL:OPEN";
                if (t.condition == GoalCondition::DefeatAll) condStr = "GOAL:KILL ALL";
                else if (t.condition == GoalCondition::OnTrigger) condStr = "GOAL:TRIGGER";
                drawEditorText(renderer, condStr, r.x + 4, r.y + 4, 10, {255, 200, 50, 255});
                break;
            }
            case TriggerType::Crate:
                SDL_SetRenderDrawColor(renderer, 180, 130, 60, 100);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 180, 130, 60, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                drawEditorText(renderer, "CRATE", r.x + 4, r.y + 4, 12, {180, 130, 60, 255});
                break;
            case TriggerType::Effect:
                SDL_SetRenderDrawColor(renderer, 180, 50, 255, 100);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 180, 50, 255, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                drawEditorText(renderer, "EFFECT", r.x + 4, r.y + 4, 12, {180, 50, 255, 255});
                break;
            case TriggerType::TeamSpawnRed:
                SDL_SetRenderDrawColor(renderer, 220, 50, 50, 110);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 70, 70, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                drawEditorText(renderer, "SPAWN RED", r.x + 4, r.y + 4, 11, {255, 80, 80, 255});
                break;
            case TriggerType::TeamSpawnBlue:
                SDL_SetRenderDrawColor(renderer, 50, 80, 220, 110);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 70, 120, 255, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                drawEditorText(renderer, "SPAWN BLUE", r.x + 4, r.y + 4, 11, {80, 140, 255, 255});
                break;
            case TriggerType::TeamSpawnGreen:
                SDL_SetRenderDrawColor(renderer, 50, 200, 80, 110);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 70, 230, 100, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                drawEditorText(renderer, "SPAWN GREEN", r.x + 4, r.y + 4, 11, {80, 240, 110, 255});
                break;
            case TriggerType::TeamSpawnYellow:
                SDL_SetRenderDrawColor(renderer, 220, 200, 30, 110);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 230, 40, selected ? 255 : 200);
                SDL_RenderDrawRect(renderer, &r);
                drawEditorText(renderer, "SPAWN YEL", r.x + 4, r.y + 4, 11, {255, 235, 50, 255});
                break;
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
            case ENTITY_CRATE:         SDL_SetRenderDrawColor(renderer, 160, 120, 60, 200); break;
            case ENTITY_UPGRADE_CRATE: SDL_SetRenderDrawColor(renderer, 220, 180, 40, 200); break;
            default:                   SDL_SetRenderDrawColor(renderer, 180, 180, 180, 200); break;
        }
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, selected ? 0 : 255, 255, selected ? 0 : 255, 255);
        SDL_RenderDrawRect(renderer, &r);

        const char* labels[] = {"M", "S", "C", "U"};
        const char* label = (es.enemyType >= 0 && es.enemyType < ENTITY_TYPE_COUNT) ? labels[es.enemyType] : "?";
        drawEditorText(renderer, label, r.x + sz/4, r.y + 2, 14, {255, 255, 255, 255});
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Properties Panel (inspector for selected entity/trigger)
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::renderPropertiesPanel(SDL_Renderer* renderer) {
    const int panelW = 220;
    const int panelH = 260;
    const int panelX = 8;
    const int panelY = TOOLBAR_H + 8;
    char buf[128];

    // Panel background
    if (ui_) ui_->drawPanel(panelX, panelY, panelW, panelH);
    else {
        SDL_SetRenderDrawColor(renderer, 10, 12, 24, 230);
        SDL_Rect bg = {panelX, panelY, panelW, panelH};
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawColor(renderer, 0, 120, 110, 60);
        SDL_RenderDrawRect(renderer, &bg);
    }

    int y = panelY + 8;
    int lx = panelX + 10;
    int vx = panelX + 100;

    if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
        auto& es = map_.enemySpawns[selectedEnemy_];
        drawEditorText(renderer, "ENTITY", lx, y, 14, UI::Color::Cyan);
        y += 24;

        // Separator
        SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
        SDL_RenderDrawLine(renderer, lx, y, panelX + panelW - 10, y);
        y += 8;

        // Type
        static const char* eNames[] = {"Melee", "Shooter", "Crate", "Upgrade"};
        const char* typeName = (es.enemyType < ENTITY_TYPE_COUNT) ? eNames[es.enemyType] : "Unknown";
        drawEditorText(renderer, "Type", lx, y, 12, UI::Color::Gray);
        // Left arrow
        bool hoverL = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx - 16, y - 2, 14, 18);
        drawEditorText(renderer, "\xe2\x97\x80", vx - 16, y, 11, hoverL ? UI::Color::White : UI::Color::Yellow);
        drawEditorText(renderer, typeName, vx + 4, y, 12, UI::Color::Cyan);
        int tw = ui_ ? ui_->textWidth(typeName, 12) : (int)strlen(typeName) * 8;
        bool hoverR = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx + tw + 8, y - 2, 14, 18);
        drawEditorText(renderer, "\xe2\x96\xb6", vx + tw + 8, y, 11, hoverR ? UI::Color::White : UI::Color::Yellow);
        if (hoverL && ui_ && ui_->mouseClicked) {
            pushUndo();
            es.enemyType = (es.enemyType + ENTITY_TYPE_COUNT - 1) % ENTITY_TYPE_COUNT;
        }
        if (hoverR && ui_ && ui_->mouseClicked) {
            pushUndo();
            es.enemyType = (es.enemyType + 1) % ENTITY_TYPE_COUNT;
        }
        y += 22;

        // Wave Group
        drawEditorText(renderer, "Wave", lx, y, 12, UI::Color::Gray);
        snprintf(buf, sizeof(buf), "%d", es.waveGroup);
        bool hoverWL = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx - 16, y - 2, 14, 18);
        drawEditorText(renderer, "\xe2\x97\x80", vx - 16, y, 11, hoverWL ? UI::Color::White : UI::Color::Yellow);
        drawEditorText(renderer, buf, vx + 4, y, 12, UI::Color::Cyan);
        tw = ui_ ? ui_->textWidth(buf, 12) : (int)strlen(buf) * 8;
        bool hoverWR = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx + tw + 8, y - 2, 14, 18);
        drawEditorText(renderer, "\xe2\x96\xb6", vx + tw + 8, y, 11, hoverWR ? UI::Color::White : UI::Color::Yellow);
        if (hoverWL && ui_ && ui_->mouseClicked) {
            pushUndo();
            if (es.waveGroup > 0) es.waveGroup--;
        }
        if (hoverWR && ui_ && ui_->mouseClicked) {
            pushUndo();
            if (es.waveGroup < 255) es.waveGroup++;
        }
        y += 22;

        // Position
        drawEditorText(renderer, "Pos", lx, y, 12, UI::Color::Gray);
        snprintf(buf, sizeof(buf), "%.0f, %.0f", es.x, es.y);
        drawEditorText(renderer, buf, vx, y, 12, UI::Color::HintGray);
        y += 22;

        // Delete button
        y += 8;
        bool hoverDel = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY,
                            lx, y - 2, panelW - 20, 22);
        SDL_SetRenderDrawColor(renderer, hoverDel ? 80 : 40, 20, 20, 200);
        SDL_Rect delBg = {lx, y - 2, panelW - 20, 22};
        SDL_RenderFillRect(renderer, &delBg);
        SDL_SetRenderDrawColor(renderer, 200, 60, 60, 200);
        SDL_RenderDrawRect(renderer, &delBg);
        drawEditorText(renderer, "DELETE", lx + (panelW - 20) / 2 - 20, y, 12,
                       hoverDel ? UI::Color::White : UI::Color::Red);
        if (hoverDel && ui_ && ui_->mouseClicked) {
            pushUndo();
            map_.enemySpawns.erase(map_.enemySpawns.begin() + selectedEnemy_);
            selectedEnemy_ = -1;
        }
    }
    else if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
        auto& t = map_.triggers[selectedTrigger_];
        drawEditorText(renderer, "TRIGGER", lx, y, 14, UI::Color::Yellow);
        y += 24;

        SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
        SDL_RenderDrawLine(renderer, lx, y, panelX + panelW - 10, y);
        y += 8;

        // Type
        static const char* tTypeNames[] = {"Start", "End", "Crate", "Effect", "SpawnR", "SpawnB", "SpawnG", "SpawnY"};
        static const TriggerType kTypes[] = {
            TriggerType::LevelStart, TriggerType::LevelEnd, TriggerType::Crate, TriggerType::Effect,
            TriggerType::TeamSpawnRed, TriggerType::TeamSpawnBlue, TriggerType::TeamSpawnGreen, TriggerType::TeamSpawnYellow,
        };
        int typeIdx = 0;
        for (int j = 0; j < 8; j++) { if (kTypes[j] == t.type) { typeIdx = j; break; } }
        drawEditorText(renderer, "Type", lx, y, 12, UI::Color::Gray);
        bool hoverTL = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx - 16, y - 2, 14, 18);
        drawEditorText(renderer, "\xe2\x97\x80", vx - 16, y, 11, hoverTL ? UI::Color::White : UI::Color::Yellow);
        drawEditorText(renderer, tTypeNames[typeIdx], vx + 4, y, 12, UI::Color::Cyan);
        int tw = ui_ ? ui_->textWidth(tTypeNames[typeIdx], 12) : (int)strlen(tTypeNames[typeIdx]) * 8;
        bool hoverTR = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx + tw + 8, y - 2, 14, 18);
        drawEditorText(renderer, "\xe2\x96\xb6", vx + tw + 8, y, 11, hoverTR ? UI::Color::White : UI::Color::Yellow);
        if (hoverTL && ui_ && ui_->mouseClicked) {
            pushUndo();
            typeIdx = (typeIdx + 7) % 8;
            t.type = kTypes[typeIdx];
        }
        if (hoverTR && ui_ && ui_->mouseClicked) {
            pushUndo();
            typeIdx = (typeIdx + 1) % 8;
            t.type = kTypes[typeIdx];
        }
        y += 22;

        // Condition (only for LevelEnd)
        if (t.type == TriggerType::LevelEnd) {
            static const char* condNames[] = {"Open", "Kill All", "Trigger"};
            int condIdx = (int)t.condition;
            if (condIdx < 0 || condIdx > 2) condIdx = 0;
            drawEditorText(renderer, "Goal", lx, y, 12, UI::Color::Gray);
            bool hoverCL = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx - 16, y - 2, 14, 18);
            drawEditorText(renderer, "\xe2\x97\x80", vx - 16, y, 11, hoverCL ? UI::Color::White : UI::Color::Yellow);
            drawEditorText(renderer, condNames[condIdx], vx + 4, y, 12, UI::Color::Cyan);
            tw = ui_ ? ui_->textWidth(condNames[condIdx], 12) : (int)strlen(condNames[condIdx]) * 8;
            bool hoverCR = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, vx + tw + 8, y - 2, 14, 18);
            drawEditorText(renderer, "\xe2\x96\xb6", vx + tw + 8, y, 11, hoverCR ? UI::Color::White : UI::Color::Yellow);
            if (hoverCL && ui_ && ui_->mouseClicked) {
                pushUndo();
                condIdx = (condIdx + 2) % 3;
                t.condition = (GoalCondition)condIdx;
            }
            if (hoverCR && ui_ && ui_->mouseClicked) {
                pushUndo();
                condIdx = (condIdx + 1) % 3;
                t.condition = (GoalCondition)condIdx;
            }
            y += 22;
        }

        // Size
        drawEditorText(renderer, "Size", lx, y, 12, UI::Color::Gray);
        snprintf(buf, sizeof(buf), "%.0fx%.0f", t.width, t.height);
        drawEditorText(renderer, buf, vx, y, 12, UI::Color::HintGray);
        y += 22;

        // Position
        drawEditorText(renderer, "Pos", lx, y, 12, UI::Color::Gray);
        snprintf(buf, sizeof(buf), "%.0f, %.0f", t.x, t.y);
        drawEditorText(renderer, buf, vx, y, 12, UI::Color::HintGray);
        y += 22;

        // Delete button
        y += 8;
        bool hoverDel = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY,
                            lx, y - 2, panelW - 20, 22);
        SDL_SetRenderDrawColor(renderer, hoverDel ? 80 : 40, 20, 20, 200);
        SDL_Rect delBg = {lx, y - 2, panelW - 20, 22};
        SDL_RenderFillRect(renderer, &delBg);
        SDL_SetRenderDrawColor(renderer, 200, 60, 60, 200);
        SDL_RenderDrawRect(renderer, &delBg);
        drawEditorText(renderer, "DELETE", lx + (panelW - 20) / 2 - 20, y, 12,
                       hoverDel ? UI::Color::White : UI::Color::Red);
        if (hoverDel && ui_ && ui_->mouseClicked) {
            pushUndo();
            map_.triggers.erase(map_.triggers.begin() + selectedTrigger_);
            selectedTrigger_ = -1;
        }
    }
}

void MapEditor::renderToolbar(SDL_Renderer* renderer) {
    // Background
    SDL_SetRenderDrawColor(renderer, 12, 14, 24, 245);
    SDL_Rect bg = {0, 0, screenW_, TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 0, 120, 110, 60);
    SDL_RenderDrawLine(renderer, 0, TOOLBAR_H - 1, screenW_, TOOLBAR_H - 1);

    // ── Tool buttons ──
    const char* toolNames[] = {"Tile", "Trigger", "Entity", "Erase", "Select", "Rect"};
    const char* toolKeys[]  = {"1", "2", "3", "4", "5", "6"};
    SDL_Color toolColors[] = {
        {80,  200, 255, 255},
        {255, 200,  50, 255},
        {255,  80,  80, 255},
        {200,  60,  60, 255},
        {100, 220, 200, 255},
        {255, 160,  50, 255},
    };

    int toolBtnW = 76;
    int toolGap  = 4;
    int toolStartX = 4;
    for (int i = 0; i < 6; i++) {
        int bx = toolStartX + i * (toolBtnW + toolGap);
        SDL_Rect btn = {bx, 4, toolBtnW, TOOLBAR_H - 8};
        bool sel = ((int)currentTool_ == i);
        bool hover = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, bx, 4, toolBtnW, TOOLBAR_H - 8);

        if (sel) {
            SDL_SetRenderDrawColor(renderer, toolColors[i].r / 5, toolColors[i].g / 5, toolColors[i].b / 5, 220);
            SDL_RenderFillRect(renderer, &btn);
            SDL_SetRenderDrawColor(renderer, toolColors[i].r, toolColors[i].g, toolColors[i].b, 220);
            SDL_RenderDrawRect(renderer, &btn);
            SDL_Rect ind = {bx, TOOLBAR_H - 3, toolBtnW, 3};
            SDL_RenderFillRect(renderer, &ind);
        } else if (hover) {
            SDL_SetRenderDrawColor(renderer, 36, 38, 50, 230);
            SDL_RenderFillRect(renderer, &btn);
            SDL_SetRenderDrawColor(renderer, toolColors[i].r / 2, toolColors[i].g / 2, toolColors[i].b / 2, 140);
            SDL_RenderDrawRect(renderer, &btn);
        } else {
            SDL_SetRenderDrawColor(renderer, 26, 28, 38, 200);
            SDL_RenderFillRect(renderer, &btn);
            SDL_SetRenderDrawColor(renderer, 46, 48, 58, 180);
            SDL_RenderDrawRect(renderer, &btn);
        }

        // Click detection for tool buttons
        if (hover && ui_ && ui_->mouseClicked) {
            currentTool_ = (EditorTool)i;
            selectedTrigger_ = -1;
            selectedEnemy_   = -1;
            rectStartTX_ = -1; rectStartTY_ = -1;
        }

        SDL_Color keyC   = sel ? toolColors[i] : (hover ? SDL_Color{100, 100, 115, 255} : SDL_Color{70, 70, 80, 255});
        SDL_Color labelC = sel ? SDL_Color{255,255,255,255} : (hover ? SDL_Color{200,200,210,255} : SDL_Color{140,140,150,255});
        drawEditorText(renderer, toolKeys[i],  bx + 4, 8, 10, keyC);
        drawEditorText(renderer, toolNames[i], bx + 14, 7, 13, labelC);
        // Second row: tool-specific subtitle
        if (sel) {
            const char* sub = "";
            char subBuf[32];
            if (i == 0 || i == 3) { snprintf(subBuf, sizeof(subBuf), "Brush:%d", brushSize_); sub = subBuf; }
            else if (i == 5) { sub = rectFilled_ ? "Filled" : "Outline"; }
            else if (i == 1) {
                static const char* ttNames[] = {"Start","End","Crate","Effect","SpnR","SpnB","SpnG","SpnY"};
                static const TriggerType kVT[] = {
                    TriggerType::LevelStart, TriggerType::LevelEnd, TriggerType::Crate, TriggerType::Effect,
                    TriggerType::TeamSpawnRed, TriggerType::TeamSpawnBlue, TriggerType::TeamSpawnGreen, TriggerType::TeamSpawnYellow,
                };
                int idx = 0; for (int j = 0; j < 8; j++) { if (kVT[j] == triggerGhost_.type) { idx = j; break; } }
                sub = ttNames[idx];
            }
            else if (i == 2) {
                static const char* eNames[] = {"Melee","Shooter","Crate","Upgrade"};
                sub = (entitySpawnType_ < ENTITY_TYPE_COUNT) ? eNames[entitySpawnType_] : "?";
            }
            if (sub[0]) drawEditorText(renderer, sub, bx + 4, 24, 10, {toolColors[i].r, toolColors[i].g, toolColors[i].b, 160});
        }
    }

    // ── Center: map info + hint ──
    int centerX = toolStartX + 6 * (toolBtnW + toolGap) + 12;
    int rightEdge = screenW_ - PALETTE_W - 240;
    if (centerX < rightEdge) {
        char info[128];
        snprintf(info, sizeof(info), "%s  %dx%d", map_.name.c_str(), map_.width, map_.height);
        drawEditorText(renderer, info, centerX, 6, 12, UI::Color::Gray);
        drawEditorText(renderer, "Scroll:zoom  MMB:pan  G:grid  TAB:ui  [/]:brush  F:rectfill", centerX, 24, 9, UI::Color::HintGray);
    }

    // ── Save button ──
    {
        int bx = screenW_ - PALETTE_W - 230;
        bool hover = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, bx, 6, 106, TOOLBAR_H - 12);
        SDL_Rect btn = {bx, 6, 106, TOOLBAR_H - 12};
        SDL_SetRenderDrawColor(renderer, hover ? 15 : 10, hover ? 70 : 55, hover ? 45 : 35, 220);
        SDL_RenderFillRect(renderer, &btn);
        SDL_SetRenderDrawColor(renderer, hover ? 60 : 40, hover ? 200 : 170, hover ? 110 : 90, 200);
        SDL_RenderDrawRect(renderer, &btn);
        SDL_Rect ind = {bx, TOOLBAR_H - 3, 106, 3};
        SDL_SetRenderDrawColor(renderer, 40, 190, 100, 255);
        SDL_RenderFillRect(renderer, &ind);
        drawEditorText(renderer, "Save", bx + 30, 8, 14, hover ? SDL_Color{200, 255, 210, 255} : SDL_Color{160, 255, 180, 255});
        drawEditorText(renderer, "Ctrl+S", bx + 24, 24, 9, {80, 140, 100, 200});
        if (hover && ui_ && ui_->mouseClicked) wantsModSave_ = true;
    }

    // ── Play button ──
    {
        int bx = screenW_ - PALETTE_W - 118;
        bool hover = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, bx, 6, 106, TOOLBAR_H - 12);
        SDL_Rect btn = {bx, 6, 106, TOOLBAR_H - 12};
        SDL_SetRenderDrawColor(renderer, hover ? 30 : 20, hover ? 100 : 80, hover ? 55 : 40, 255);
        SDL_RenderFillRect(renderer, &btn);
        SDL_SetRenderDrawColor(renderer, hover ? 80 : 60, hover ? 230 : 200, hover ? 100 : 80, 200);
        SDL_RenderDrawRect(renderer, &btn);
        SDL_Rect ind = {bx, TOOLBAR_H - 3, 106, 3};
        SDL_SetRenderDrawColor(renderer, 50, 255, 100, 255);
        SDL_RenderFillRect(renderer, &ind);
        drawEditorText(renderer, "\xe2\x96\xb6 Play", bx + 22, 8, 14, hover ? SDL_Color{230, 255, 230, 255} : SDL_Color{200, 255, 200, 255});
        drawEditorText(renderer, "F5", bx + 42, 24, 9, {100, 180, 120, 200});
        if (hover && ui_ && ui_->mouseClicked) wantsTestPlay_ = true;
    }
}

void MapEditor::renderPalette(SDL_Renderer* renderer) {
    int px = screenW_ - PALETTE_W;

    // Panel background
    SDL_SetRenderDrawColor(renderer, 12, 14, 24, 245);
    SDL_Rect bg = {px, TOOLBAR_H, PALETTE_W, screenH_ - TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
    SDL_RenderDrawLine(renderer, px, TOOLBAR_H, px, screenH_);

    // ── Tab buttons ──
    static const char* tabNames[] = {"All", "Gnd", "Wall", "Ceil", "Prop"};
    int tabCount = (int)PaletteTab::TAB_COUNT;
    int tabW = (PALETTE_W - 8) / tabCount;
    int tabY = TOOLBAR_H + 4;
    int tabH = 22;
    for (int t = 0; t < tabCount; t++) {
        int tx = px + 4 + t * tabW;
        bool active = ((int)paletteTab_ == t);
        bool hover = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY, tx, tabY, tabW - 2, tabH);
        if (active) {
            SDL_SetRenderDrawColor(renderer, 0, 120, 110, 120);
            SDL_Rect tabBg = {tx, tabY, tabW - 2, tabH};
            SDL_RenderFillRect(renderer, &tabBg);
            SDL_SetRenderDrawColor(renderer, 0, 255, 228, 180);
            SDL_Rect ind = {tx, tabY + tabH - 2, tabW - 2, 2};
            SDL_RenderFillRect(renderer, &ind);
        } else if (hover) {
            SDL_SetRenderDrawColor(renderer, 30, 32, 44, 200);
            SDL_Rect tabBg = {tx, tabY, tabW - 2, tabH};
            SDL_RenderFillRect(renderer, &tabBg);
        }
        drawEditorText(renderer, tabNames[t], tx + 4, tabY + 3, 11,
                       active ? UI::Color::Cyan : (hover ? UI::Color::White : UI::Color::Gray));
        // Click to switch tab
        if (hover && ui_ && ui_->mouseClicked) {
            paletteTab_ = (PaletteTab)t;
            rebuildFilteredPalette();
            paletteScroll_ = 0;
        }
    }

    int contentTop = TOOLBAR_H + tabH + 10;

    // Clip rendering to palette panel below tabs
    SDL_Rect clip = {px, contentTop, PALETTE_W, screenH_ - contentTop};
    SDL_RenderSetClipRect(renderer, &clip);

    // Rebuild palette item Y cache
    paletteItemY_.clear();
    paletteItemY_.resize(palette_.size(), -999);

    int y = contentTop + 4 - paletteScroll_;
    std::string lastCat;

    for (int i = 0; i < (int)palette_.size(); i++) {
        auto& pt = palette_[i];

        // Filter based on current tab
        if (paletteTab_ != PaletteTab::All) {
            bool match = false;
            switch (paletteTab_) {
                case PaletteTab::Ground:  match = (pt.category == "ground"); break;
                case PaletteTab::Walls:   match = (pt.category == "walls"); break;
                case PaletteTab::Ceiling: match = (pt.category == "ceiling"); break;
                case PaletteTab::Props:   match = (pt.category == "props"); break;
                default: match = true; break;
            }
            if (!match) continue;
        }

        // Category header
        if (pt.category != lastCat) {
            lastCat = pt.category;
            if (y >= contentTop && y < screenH_) {
                SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
                SDL_RenderDrawLine(renderer, px + 8, y, px + PALETTE_W - 8, y);
                y += 4;
                std::string catLabel = pt.category;
                for (auto& c : catLabel) c = (char)toupper(c);
                drawEditorText(renderer, catLabel.c_str(), px + 10, y, 11, {0, 180, 160, 200});
                y += 16;
            } else {
                y += 20;
            }
        }

        paletteItemY_[i] = y;

        if (y >= contentTop - TILE_PREVIEW && y < screenH_) {
            bool sel = (i == selectedPalette_);
            bool hover = ui_ && ui_->pointInRect(ui_->mouseX, ui_->mouseY,
                             px + 2, y - 2, PALETTE_W - 4, TILE_PREVIEW + 4);

            if (sel) {
                SDL_SetRenderDrawColor(renderer, 0, 180, 160, 30);
                SDL_Rect hl = {px + 2, y - 2, PALETTE_W - 4, TILE_PREVIEW + 4};
                SDL_RenderFillRect(renderer, &hl);
                SDL_SetRenderDrawColor(renderer, 0, 255, 228, 200);
                SDL_Rect acc = {px + 2, y - 2, 3, TILE_PREVIEW + 4};
                SDL_RenderFillRect(renderer, &acc);
            } else if (hover) {
                SDL_SetRenderDrawColor(renderer, 0, 100, 90, 20);
                SDL_Rect hl = {px + 2, y - 2, PALETTE_W - 4, TILE_PREVIEW + 4};
                SDL_RenderFillRect(renderer, &hl);
            }

            // Click to select palette item
            if (hover && ui_ && ui_->mouseClicked) {
                selectedPalette_ = i;
            }

            SDL_Rect dst = {px + 10, y, TILE_PREVIEW, TILE_PREVIEW};
            if (pt.texture) {
                SDL_RenderCopy(renderer, pt.texture, nullptr, &dst);
            } else {
                SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);
                SDL_RenderFillRect(renderer, &dst);
            }

            if (sel) {
                SDL_SetRenderDrawColor(renderer, 0, 255, 228, 200);
            } else if (hover) {
                SDL_SetRenderDrawColor(renderer, 0, 200, 180, 120);
            } else {
                SDL_SetRenderDrawColor(renderer, 40, 42, 55, 180);
            }
            SDL_RenderDrawRect(renderer, &dst);

            SDL_Color nameC = sel ? SDL_Color{255, 255, 255, 255}
                            : (hover ? SDL_Color{200, 200, 210, 255}
                                     : SDL_Color{160, 160, 170, 255});
            drawEditorText(renderer, pt.name.c_str(), px + TILE_PREVIEW + 18, y + 4, 13, nameC);

            if (sel) {
                drawEditorText(renderer, pt.category.c_str(), px + TILE_PREVIEW + 18, y + 22, 9, {80, 80, 90, 200});
            }
        }

        y += TILE_PREVIEW + 6;
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    // Scroll bar
    {
        int totalH = paletteContentHeight();
        int viewH  = screenH_ - contentTop;
        if (totalH > viewH) {
            float ratio  = (float)viewH / totalH;
            int barH = std::max(20, (int)(viewH * ratio));
            float scrollRatio = (float)paletteScroll_ / (totalH - viewH);
            int barY = contentTop + (int)((viewH - barH) * scrollRatio);
            SDL_SetRenderDrawColor(renderer, 0, 120, 110, 60);
            SDL_Rect scrollBar = {px + PALETTE_W - 6, barY, 4, barH};
            SDL_RenderFillRect(renderer, &scrollBar);
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

// ═════════════════════════════════════════════════════════════════════════════
//  Config Screen (shown before editor opens)
// ═════════════════════════════════════════════════════════════════════════════

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

    // ── Mouse click support (config screen) ─────────────────────────────
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int mx = e.button.x, my = e.button.y;
        int panelY = 88;
        // Field 0: Action toggle row
        int f0Y = panelY + 16;
        if (my >= f0Y - 4 && my < f0Y + 34) {
            cfg.field = 0;
            if (mx >= screenW_ / 2 - 80) { // clicked on value side
                if (cfg.action == EditorConfig::Action::NewMap)
                    cfg.action = (!cfg.availableMaps.empty()) ? EditorConfig::Action::LoadMap : EditorConfig::Action::NewMap;
                else
                    cfg.action = EditorConfig::Action::NewMap;
            }
        }
        if (cfg.action == EditorConfig::Action::LoadMap && !cfg.availableMaps.empty()) {
            // Click on a map list item
            int listBaseY = panelY + 16 + 44 + 30; // after field-0 + separator + SELECT MAP header
            int startShow = std::max(0, cfg.loadIdx - 4);
            int endShow = std::min((int)cfg.availableMaps.size(), startShow + 8);
            for (int i = startShow; i < endShow; i++) {
                int itemY = listBaseY + (i - startShow) * 24;
                if (my >= itemY - 2 && my < itemY + 22) {
                    cfg.loadIdx = i;
                    cfg.field = 1;
                    break;
                }
            }
        } else if (cfg.action == EditorConfig::Action::NewMap) {
            int yWalk = panelY + 16 + 44; // 148 after field 0
            if (my >= yWalk - 4 && my < yWalk + 34) cfg.field = 1;      yWalk += 44;
            if (my >= yWalk - 4 && my < yWalk + 34) cfg.field = 2;      yWalk += 44;
            if (my >= yWalk - 4 && my < yWalk + 34) { cfg.field = 3; }  yWalk += 44;
            if (my >= yWalk - 4 && my < yWalk + 34) { cfg.field = 4; }  yWalk += 44;
            if (my >= yWalk - 4 && my < yWalk + 34) { cfg.field = 5; cfg.gameMode = 1 - cfg.gameMode; }
        }
        // OK button
        int btnY = screenH_ - 100;
        if (my >= btnY - 4 && my < btnY + 30 &&
            mx >= screenW_ / 2 - 120 && mx < screenW_ / 2 + 120) {
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
        // Cancel button
        int cancelY = btnY + 44;
        if (my >= cancelY - 4 && my < cancelY + 28 &&
            mx >= screenW_ / 2 - 60 && mx < screenW_ / 2 + 60) {
            wantsBack_ = true;
            showConfig_ = false;
            return;
        }
        return; // consume all clicks on config screen
    }

    // Character palette for gamepad text input
    static const char charPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
    int& charIdx = cfg.gpCharIdx;

    // Text input mode for name/creator fields
    if (cfg.textEditing) {
        if (e.type == SDL_TEXTINPUT) {
            cfg.textBuf += e.text.text;
            return;
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN) {
                if (cfg.field == 3) cfg.mapName = cfg.textBuf;
                else if (cfg.field == 4) cfg.creator = cfg.textBuf;
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
            switch (e.cbutton.button) {
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
                    if (cfg.field == 3) cfg.mapName = cfg.textBuf;
                    else if (cfg.field == 4) cfg.creator = cfg.textBuf;
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
                // In load mode, skip fields 2/3/4/5 which are for new map only
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 6)
                    cfg.field = 1;
                break;
            case SDLK_DOWN:
                cfg.field++;
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 6)
                    cfg.field = 6;
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
                else if (cfg.field == 5) { cfg.gameMode = 1 - cfg.gameMode; }
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
                else if (cfg.field == 5) { cfg.gameMode = 1 - cfg.gameMode; }
                break;
            case SDLK_RETURN:
                if ((cfg.field == 3 || cfg.field == 4) && cfg.action == EditorConfig::Action::NewMap) {
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
                        map_.name = cfg.mapName;
                        map_.creator = cfg.creator;
                        map_.gameMode = (uint8_t)cfg.gameMode;
                        std::string safeName = cfg.mapName;
                        for (char& c : safeName) {
                            if (c == ' ') c = '_';
                            if (c == '/' || c == '\\') c = '_';
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
        switch (e.cbutton.button) {
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
                else if (cfg.field == 5) { cfg.gameMode = 1 - cfg.gameMode; }
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
                else if (cfg.field == 5) { cfg.gameMode = 1 - cfg.gameMode; }
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
                        map_.name = cfg.mapName;
                        map_.creator = cfg.creator;
                        map_.gameMode = (uint8_t)cfg.gameMode;
                        std::string safeName = cfg.mapName;
                        for (char& c : safeName) {
                            if (c == ' ') c = '_';
                            if (c == '/' || c == '\\') c = '_';
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
    const int cx = screenW_ / 2;
    char buf[256];

    // Dark background
    if (ui_) ui_->drawDarkOverlay(255, 6, 8, 16);
    else {
        SDL_SetRenderDrawColor(renderer, 6, 8, 16, 255);
        SDL_Rect full = {0, 0, screenW_, screenH_};
        SDL_RenderFillRect(renderer, &full);
    }

    // Title
    drawEditorText(renderer, "MAP EDITOR", cx - 100, 28, 32, UI::Color::Cyan);
    if (ui_) ui_->drawSeparator(cx, 68, 80);

    // Panel
    int panelX = cx - 320, panelW = 640, panelY = 88;
    int panelH = screenH_ - 140;
    if (ui_) ui_->drawPanel(panelX, panelY, panelW, panelH,
                            {12, 14, 26, 200}, {0, 120, 110, 40});
    else {
        SDL_SetRenderDrawColor(renderer, 12, 14, 26, 200);
        SDL_Rect panel = {panelX, panelY, panelW, panelH};
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
        SDL_RenderDrawRect(renderer, &panel);
    }

    int y = panelY + 16;
    int step = 44;
    int labelX = panelX + 30;
    int valX   = panelX + 240;

    // Helper: draw a selectable config row with optional left/right arrows
    auto drawField = [&](int idx, const char* label, const char* value, bool hasArrows = false) {
        bool sel = (cfg.field == idx);
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 0, 180, 160, 30);
            SDL_Rect bar = {panelX + 4, y - 4, panelW - 8, 34};
            SDL_RenderFillRect(renderer, &bar);
            SDL_SetRenderDrawColor(renderer, 0, 200, 180, 180);
            SDL_Rect acc = {panelX + 4, y - 4, 3, 34};
            SDL_RenderFillRect(renderer, &acc);
        }
        drawEditorText(renderer, label, labelX, y, 18, sel ? UI::Color::White : UI::Color::Gray);
        if (hasArrows && sel) {
            drawEditorText(renderer, "\xe2\x97\x80", valX - 24, y, 16, UI::Color::Yellow);
            drawEditorText(renderer, value, valX, y, 18, UI::Color::Cyan);
            int tw = ui_ ? ui_->textWidth(value, 18) + 8 : (int)strlen(value) * 10 + 8;
            drawEditorText(renderer, "\xe2\x96\xb6", valX + tw, y, 16, UI::Color::Yellow);
        } else {
            drawEditorText(renderer, value, valX, y, 18, sel ? UI::Color::Cyan : UI::Color::HintGray);
        }
        // Click on this row = select it
        if (ui_ && ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY,
                panelX + 4, y - 4, panelW - 8, 34)) {
            cfg.field = idx;
            // Click on arrows: adjust value
            if (hasArrows) {
                if (ui_->mouseX < valX) {
                    // Left arrow area - simulate left key press
                    SDL_Event fakeKey;
                    memset(&fakeKey, 0, sizeof(fakeKey));
                    fakeKey.type = SDL_KEYDOWN;
                    fakeKey.key.keysym.sym = SDLK_LEFT;
                    handleConfigInput(fakeKey);
                } else {
                    SDL_Event fakeKey;
                    memset(&fakeKey, 0, sizeof(fakeKey));
                    fakeKey.type = SDL_KEYDOWN;
                    fakeKey.key.keysym.sym = SDLK_RIGHT;
                    handleConfigInput(fakeKey);
                }
            }
        }
        y += step;
    };

    // Helper: text field with inline editor
    auto drawTextFieldEditing = [&](int idx, const char* label) {
        bool sel  = (cfg.field == idx);
        bool edit = (cfg.textEditing && cfg.field == idx);
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 0, 180, 160, 30);
            SDL_Rect bar = {panelX + 4, y - 4, panelW - 8, 34};
            SDL_RenderFillRect(renderer, &bar);
            SDL_SetRenderDrawColor(renderer, 0, 200, 180, 180);
            SDL_Rect acc = {panelX + 4, y - 4, 3, 34};
            SDL_RenderFillRect(renderer, &acc);
        }
        drawEditorText(renderer, label, labelX, y, 18, sel ? UI::Color::White : UI::Color::Gray);
        const char* curVal = (idx == 3) ? cfg.mapName.c_str() : cfg.creator.c_str();
        if (edit) {
            // Text input box
            SDL_SetRenderDrawColor(renderer, 20, 22, 38, 255);
            SDL_Rect box = {valX - 4, y - 4, 280, 28};
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 0, 200, 180, 180);
            SDL_RenderDrawRect(renderer, &box);
            std::string disp = cfg.textBuf + "_";
            drawEditorText(renderer, disp.c_str(), valX, y, 18, UI::Color::Yellow);

            // Gamepad char palette
            static const char charPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
            int palLen = (int)strlen(charPalette);
            int cellW = 18, cellH = 22;
            int cols = 20;
            int rows = (palLen + cols - 1) / cols;
            int palStartX = panelX + 30;
            int palStartY = y + 34;

            SDL_SetRenderDrawColor(renderer, 14, 16, 28, 240);
            SDL_Rect palBg = {palStartX - 6, palStartY - 4, cols * cellW + 12, rows * cellH + 8};
            SDL_RenderFillRect(renderer, &palBg);

            for (int ci = 0; ci < palLen; ci++) {
                int col = ci % cols;
                int row = ci / cols;
                int cxPos = palStartX + col * cellW;
                int cyPos = palStartY + row * cellH;
                bool isSel = (ci == cfg.gpCharIdx);
                if (isSel) {
                    SDL_SetRenderDrawColor(renderer, 0, 180, 160, 255);
                    SDL_Rect bg2 = {cxPos - 1, cyPos - 1, cellW - 2, cellH - 2};
                    SDL_RenderFillRect(renderer, &bg2);
                }
                char ch[2] = { charPalette[ci], 0 };
                drawEditorText(renderer, ch, cxPos + 3, cyPos + 2,
                               isSel ? 16 : 13, isSel ? UI::Color::White : UI::Color::HintGray);
            }
            drawEditorText(renderer, "A:type  Y:del  X/START:done  B:cancel",
                           palStartX, palStartY + rows * cellH + 6, 11, UI::Color::HintGray);
            y += 34 + rows * cellH + 28;
        } else {
            drawEditorText(renderer, curVal, valX, y, 18, sel ? UI::Color::Cyan : UI::Color::HintGray);
            if (sel) drawEditorText(renderer, "[ENTER]", valX + 200, y + 2, 12, UI::Color::HintGray);
            // Click to start editing
            if (ui_ && ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY,
                    panelX + 4, y - 4, panelW - 8, 34)) {
                cfg.field = idx;
                cfg.textEditing = true;
                cfg.textBuf = (idx == 3) ? cfg.mapName : cfg.creator;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
            }
            y += step;
        }
    };

    // ── Field 0: Action ──
    const char* actionStr = (cfg.action == EditorConfig::Action::NewMap) ? "NEW MAP" : "LOAD MAP";
    drawField(0, "ACTION", actionStr, true);

    // Separator
    if (ui_) ui_->drawSeparator(cx, y - step / 2 + 4, panelW / 2 - 20, {40, 42, 55, 120});
    else {
        SDL_SetRenderDrawColor(renderer, 40, 42, 55, 120);
        SDL_Rect sep = {panelX + 20, y - step / 2 + 4, panelW - 40, 1};
        SDL_RenderFillRect(renderer, &sep);
    }

    if (cfg.action == EditorConfig::Action::NewMap) {
        snprintf(buf, sizeof(buf), "%d", cfg.mapWidth);
        drawField(1, "WIDTH", buf, true);
        snprintf(buf, sizeof(buf), "%d", cfg.mapHeight);
        drawField(2, "HEIGHT", buf, true);
        drawTextFieldEditing(3, "MAP NAME");
        drawTextFieldEditing(4, "CREATOR");
        drawField(5, "GAME MODE", cfg.gameMode == 0 ? "ARENA" : "SANDBOX", true);
    } else {
        // ── Load mode: map list ──
        bool sel = (cfg.field == 1);
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 0, 180, 160, 30);
            SDL_Rect bar = {panelX + 4, y - 4, panelW - 8, 34};
            SDL_RenderFillRect(renderer, &bar);
        }
        drawEditorText(renderer, "SELECT MAP", labelX, y, 18, sel ? UI::Color::White : UI::Color::Gray);
        y += 30;

        if (cfg.availableMaps.empty()) {
            drawEditorText(renderer, "No .csm files found in maps/ or any mod", labelX + 20, y, 16, UI::Color::HintGray);
            y += step * 2;
        } else {
            int startShow = std::max(0, cfg.loadIdx - 4);
            int endShow = std::min((int)cfg.availableMaps.size(), startShow + 8);
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
                    std::string modName = (ps != std::string::npos) ? beforeMaps.substr(ps + 1) : beforeMaps;
                    if (modName != "romfs" && modName != ".") {
                        prefix = "[" + modName + "] ";
                    }
                }
                bool isCur = (i == cfg.loadIdx);
                if (isCur) {
                    SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
                    SDL_Rect row = {labelX + 12, y - 2, panelW - 80, 24};
                    SDL_RenderFillRect(renderer, &row);
                }
                snprintf(buf, sizeof(buf), "%s %s%s", isCur ? "\xe2\x96\xb6" : " ", prefix.c_str(), fname.c_str());
                drawEditorText(renderer, buf, labelX + 20, y, isCur ? 16 : 14, isCur ? UI::Color::Cyan : UI::Color::HintGray);
                // Click to select a map item
                if (ui_ && ui_->mouseClicked &&
                    ui_->pointInRect(ui_->mouseX, ui_->mouseY, labelX + 12, y - 2, panelW - 80, 24)) {
                    cfg.loadIdx = i;
                    cfg.field = 1;
                }
                y += 24;
            }
            y += 8;
        }
    }

    // ── Bottom buttons (using menuItem for click support) ──
    int btnY = screenH_ - 100;
    if (ui_) {
        if (ui_->menuItem(20, "START EDITOR", cx, btnY, 260, 32,
                          UI::Color::Green, cfg.field == 6, 20, 24)) {
            // OK clicked
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
        btnY += step;
        if (ui_->menuItem(21, "BACK", cx, btnY, 160, 28,
                          UI::Color::White, cfg.field == 7, 18, 22)) {
            wantsBack_ = true;
            showConfig_ = false;
            return;
        }
    } else {
        // Fallback: old-style rendering
        bool okSel = (cfg.field == 6);
        if (okSel) {
            SDL_SetRenderDrawColor(renderer, 0, 180, 160, 40);
            SDL_Rect bar = {cx - 120, btnY - 4, 240, 32};
            SDL_RenderFillRect(renderer, &bar);
        }
        drawEditorText(renderer, okSel ? "> START EDITOR <" : "START EDITOR",
                       cx - 80, btnY, 22, okSel ? UI::Color::Green : UI::Color::Gray);
        btnY += step;
        bool cancelSel = (cfg.field == 7);
        drawEditorText(renderer, cancelSel ? "> BACK <" : "BACK",
                       cx - 32, btnY, 20, cancelSel ? UI::Color::White : UI::Color::HintGray);
    }

    // Controls hint
    if (ui_) {
        UI::HintPair hints[] = {
            {UI::Action::Navigate, "navigate"},
            {UI::Action::Left, "adjust"},
            {UI::Action::Confirm, "confirm"},
            {UI::Action::Back, "back"},
        };
        ui_->drawHintBar(hints, 4, screenH_ - 36);
    } else {
        drawEditorText(renderer, "\xe2\x86\x91\xe2\x86\x93 navigate   \xe2\x86\x90\xe2\x86\x92 adjust   ENTER confirm   ESC back",
            cx - 200, screenH_ - 30, 12, {60, 60, 70, 255});
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Gamepad Support
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::handleGamepadInput(SDL_Event& e) {
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        useGamepad_ = true;

        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: { // Place / confirm (like left click)
                mouseDown_ = true;
                mouseX_ = (int)cursorX_;
                mouseY_ = (int)cursorY_;
                // Simulate mouse click for toolbar/palette/map
                SDL_Event fakeClick;
                memset(&fakeClick, 0, sizeof(fakeClick));
                fakeClick.type = SDL_MOUSEBUTTONDOWN;
                fakeClick.button.button = SDL_BUTTON_LEFT;
                fakeClick.button.x = (int)cursorX_;
                fakeClick.button.y = (int)cursorY_;
                handleInput(fakeClick);
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
    int sz = 16;

    // Crosshair cursor
    SDL_SetRenderDrawColor(renderer, 0, 255, 228, 200);
    SDL_RenderDrawLine(renderer, (int)cx - sz, (int)cy, (int)cx + sz, (int)cy);
    SDL_RenderDrawLine(renderer, (int)cx, (int)cy - sz, (int)cx, (int)cy + sz);
    // Center dot
    SDL_Rect dot = {(int)cx - 2, (int)cy - 2, 4, 4};
    SDL_RenderFillRect(renderer, &dot);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Touch Support
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::handleTouchInput(SDL_Event& e) {
    if (e.type == SDL_FINGERDOWN) {
        touchActive_ = true;
        touchX_ = e.tfinger.x * screenW_;
        touchY_ = e.tfinger.y * screenH_;
        mouseX_ = (int)touchX_;
        mouseY_ = (int)touchY_;

        // ── Toolbar touch detection ──
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

        // ── Palette touch detection ──
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
