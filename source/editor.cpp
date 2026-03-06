// ─── editor.cpp ─── Map Editor implementation ───────────────────────────────
#include "editor.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

// ═════════════════════════════════════════════════════════════════════════════
//  Init / Shutdown
// ═════════════════════════════════════════════════════════════════════════════

bool MapEditor::init(SDL_Renderer* renderer, int screenW, int screenH) {
    renderer_ = renderer;
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

// ═════════════════════════════════════════════════════════════════════════════
//  Palette loading
// ═════════════════════════════════════════════════════════════════════════════

void MapEditor::loadPalette() {
    palette_.clear();

    // Scan all sprite/tile directories for PNG files
    // Each subfolder maps to a category and default tile type
    struct FolderDef { const char* path; const char* category; uint8_t defaultType; };
    FolderDef folders[] = {
        // Custom user tile folders
        {"tiles/ground",  "ground",  TILE_GRASS},
        {"tiles/walls",   "walls",   TILE_WALL},
        {"tiles/ceiling", "ceiling", TILE_GLASS},
        {"tiles/props",   "props",   TILE_DESK},
        // RomFS tile folders (Switch)
        {"romfs/tiles/ground",  "ground",  TILE_GRASS},
        {"romfs/tiles/walls",   "walls",   TILE_WALL},
        {"romfs/tiles/ceiling", "ceiling", TILE_GLASS},
        {"romfs/tiles/props",   "props",   TILE_DESK},
        // RomFS sprites/tiles (flat folder — auto-categorize)
        {"romfs/sprites/tiles", "ground",  TILE_GRASS},
        // Switch romfs: paths
        {"romfs:/tiles/ground",  "ground",  TILE_GRASS},
        {"romfs:/tiles/walls",   "walls",   TILE_WALL},
        {"romfs:/tiles/ceiling", "ceiling", TILE_GLASS},
        {"romfs:/tiles/props",   "props",   TILE_DESK},
        {"romfs:/sprites/tiles", "ground",  TILE_GRASS},
    };

    for (auto& fd : folders) {
        scanTileFolder(fd.path, fd.category, fd.defaultType);
    }

    // Also try to pick up tiles from the Assets system (already-loaded sprites)
    auto& a = Assets::instance();
    auto tryAdd = [&](const char* name, const char* spritePath, uint8_t type, const char* cat) {
        // Only add if not already in palette
        for (auto& pt : palette_) {
            if (pt.name == name && pt.category == cat) return;
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

    // Ensure a basic set of tiles is always available even if folders are empty
    tryAdd("Grass",    "sprites/tiles/grass.png",    TILE_GRASS,  "ground");
    tryAdd("Floor",    "sprites/tiles/floor.png",    TILE_FLOOR,  "ground");
    tryAdd("Gravel",   "sprites/tiles/gravel.png",   TILE_GRAVEL, "ground");
    tryAdd("Wall",     "sprites/tiles/floor.png",    TILE_WALL,   "walls");
    tryAdd("Glass",    "sprites/tiles/glasstile.png", TILE_GLASS,  "walls");
    tryAdd("Box",      "sprites/props/box.png",      TILE_BOX,    "props");

    rebuildFilteredPalette();
    printf("Editor palette: %d tiles loaded\n", (int)palette_.size());
}

void MapEditor::scanTileFolder(const std::string& folder, const std::string& category, uint8_t defaultType) {
    DIR* dir = opendir(folder.c_str());
    if (!dir) return;

    // Filename stem → tile type lookup (works across all folder scans)
    auto tileTypeFromStem = [](const std::string& s) -> uint8_t {
        if (s == "floor")                           return TILE_FLOOR;
        if (s == "grass")                           return TILE_GRASS;
        if (s.substr(0,6) == "gravel")              return TILE_GRAVEL;
        if (s == "wood")                            return TILE_WOOD;
        if (s == "sand")                            return TILE_SAND;
        if (s == "wall")                            return TILE_WALL;
        if (s == "glass" || s == "glasstile")       return TILE_GLASS;
        if (s == "desk")                            return TILE_DESK;
        if (s == "box")                             return TILE_BOX;
        return 0xFF; // unknown — use defaultType
    };

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname(entry->d_name);
        if (fname.size() < 5) continue;
        std::string ext = fname.substr(fname.size() - 4);
        if (ext != ".png" && ext != ".PNG") continue;

        std::string fullPath = folder + "/" + fname;
        std::string displayName = fname.substr(0, fname.size() - 4);

        bool exists = false;
        for (auto& pt : palette_) {
            if (pt.name == displayName && pt.category == category) { exists = true; break; }
        }
        if (exists) continue;

        SDL_Surface* surf = IMG_Load(fullPath.c_str());
        if (!surf) continue;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_FreeSurface(surf);
        if (!tex) continue;

        EditorTile et;
        et.name     = displayName;
        et.path     = fullPath;
        et.texture  = tex;
        uint8_t detectedType = tileTypeFromStem(displayName);
        et.tileType = (detectedType != 0xFF) ? detectedType : defaultType;
        et.category = category;
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
}

bool MapEditor::saveMap(const std::string& path) {
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
    saveMap(savePath_);
}

bool MapEditor::loadMap(const std::string& path) {
    if (!map_.loadFromFile(path)) return false;
    camera_.pos = {0, 0};
    camera_.worldW = map_.width * TILE_SIZE;
    camera_.worldH = map_.height * TILE_SIZE;
    selectedTrigger_ = -1;
    selectedEnemy_   = -1;
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
            for (int i = 0; i < 5; i++) {
                int bx = 10 + i * 110;
                if (mouseX_ >= bx && mouseX_ < bx + 100 && mouseY_ >= 8 && mouseY_ < 40) {
                    currentTool_ = (EditorTool)i;
                    selectedTrigger_ = -1;
                    selectedEnemy_ = -1;
                    break;
                }
            }
            // Test Play button (right side of toolbar)
            if (mouseX_ >= screenW_ - 180 && mouseX_ < screenW_ - 80 && mouseY_ >= 8 && mouseY_ < 40) {
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
            // Clicked nothing — deselect
            selectedTrigger_ = -1;
            selectedEnemy_ = -1;
        }
    }

    if (e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.button == SDL_BUTTON_LEFT)  { mouseDown_ = false; draggingResize_ = false; }
        if (e.button.button == SDL_BUTTON_RIGHT) rightDown_ = false;
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

            case SDLK_DELETE:
            case SDLK_BACKSPACE:
                // Delete selected trigger/enemy
                if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
                    map_.triggers.erase(map_.triggers.begin() + selectedTrigger_);
                    selectedTrigger_ = -1;
                }
                if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
                    map_.enemySpawns.erase(map_.enemySpawns.begin() + selectedEnemy_);
                    selectedEnemy_ = -1;
                }
                break;

            case SDLK_t:
                if (currentTool_ == EditorTool::Trigger) {
                    int v = (int)triggerGhost_.type + 1;
                    if (v >= (int)TriggerType::COUNT) v = 0;
                    triggerGhost_.type = (TriggerType)v;
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

    // Paint/erase on mouse hold (only in map area, not dragging resize)
    if (!draggingResize_ && mouseX_ < screenW_ - uiPaletteW() && mouseY_ > uiToolbarH()) {
        float wx = screenToWorldX(mouseX_);
        float wy = screenToWorldY(mouseY_);
        int tx = (int)(wx / TILE_SIZE);
        int ty = (int)(wy / TILE_SIZE);

        if (mouseDown_) {
            switch (currentTool_) {
                case EditorTool::Tile:    paintTile(tx, ty); break;
                case EditorTool::Erase: {
                    eraseTile(tx, ty);
                    eraseTriggerAt(wx, wy);
                    eraseEnemyAt(wx, wy);
                    break;
                }
                case EditorTool::Trigger: placeTrigger(wx, wy); mouseDown_ = false; break;
                case EditorTool::Entity:  placeEnemy(wx, wy); mouseDown_ = false; break;
                default: break;
            }
        }
        if (rightDown_) {
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
        map_.tiles[ty * map_.width + tx] = palette_[selectedPalette_].tileType;
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

            SDL_Texture* tex = nullptr;
            for (auto& pt : palette_) {
                if (pt.tileType == tile && pt.texture) { tex = pt.texture; break; }
            }
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
        SDL_Rect cur = {sx, sy, (int)ceilf(ts), (int)ceilf(ts)};

        if (currentTool_ == EditorTool::Erase) {
            SDL_SetRenderDrawColor(renderer, 255, 60, 60, 140);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 120);
        }
        SDL_RenderDrawRect(renderer, &cur);

        // Preview selected tile
        if (currentTool_ == EditorTool::Tile && selectedPalette_ >= 0 &&
            selectedPalette_ < (int)palette_.size() && palette_[selectedPalette_].texture) {
            SDL_SetTextureAlphaMod(palette_[selectedPalette_].texture, 120);
            SDL_RenderCopy(renderer, palette_[selectedPalette_].texture, nullptr, &cur);
            SDL_SetTextureAlphaMod(palette_[selectedPalette_].texture, 255);
        }
    }

    if (showUI_) {
        renderToolbar(renderer);
        renderPalette(renderer);
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

void MapEditor::renderToolbar(SDL_Renderer* renderer) {
    // Background
    SDL_SetRenderDrawColor(renderer, 12, 14, 24, 245);
    SDL_Rect bg = {0, 0, screenW_, TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);
    // Bottom border
    SDL_SetRenderDrawColor(renderer, 0, 120, 110, 60);
    SDL_RenderDrawLine(renderer, 0, TOOLBAR_H - 1, screenW_, TOOLBAR_H - 1);

    // Tool buttons
    const char* toolNames[] = {"Tile", "Trigger", "Entity", "Erase", "Select"};
    const char* toolKeys[]  = {"1", "2", "3", "4", "5"};
    SDL_Color toolColors[] = {
        {80, 200, 255, 255},   // Tile: blue
        {255, 200, 50, 255},   // Trigger: yellow
        {255, 80, 80, 255},    // Enemy: red
        {200, 60, 60, 255},    // Erase: dark red
        {100, 220, 200, 255},  // Select: teal
    };

    for (int i = 0; i < 5; i++) {
        int bx = 8 + i * 100;
        SDL_Rect btn = {bx, 6, 90, 34};
        bool sel = ((int)currentTool_ == i);

        if (sel) {
            SDL_SetRenderDrawColor(renderer, toolColors[i].r / 4, toolColors[i].g / 4, toolColors[i].b / 4, 200);
            SDL_RenderFillRect(renderer, &btn);
            SDL_SetRenderDrawColor(renderer, toolColors[i].r, toolColors[i].g, toolColors[i].b, 200);
            SDL_RenderDrawRect(renderer, &btn);
            // Active indicator bar
            SDL_Rect ind = {bx, TOOLBAR_H - 3, 90, 3};
            SDL_RenderFillRect(renderer, &ind);
        } else {
            SDL_SetRenderDrawColor(renderer, 30, 32, 44, 200);
            SDL_RenderFillRect(renderer, &btn);
            SDL_SetRenderDrawColor(renderer, 50, 52, 64, 180);
            SDL_RenderDrawRect(renderer, &btn);
        }

        // Key badge
        SDL_Color keyC = sel ? toolColors[i] : SDL_Color{80, 80, 90, 255};
        drawEditorText(renderer, toolKeys[i], bx + 6, 10, 12, keyC);
        // Label
        SDL_Color labelC = sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{160, 160, 170, 255};
        drawEditorText(renderer, toolNames[i], bx + 20, 12, 14, labelC);
    }

    // Test Play button (right side)
    {
        int bx = screenW_ - 200;
        SDL_Rect btn = {bx, 6, 110, 34};
        SDL_SetRenderDrawColor(renderer, 20, 100, 40, 255);
        SDL_RenderFillRect(renderer, &btn);
        SDL_SetRenderDrawColor(renderer, 60, 200, 80, 200);
        SDL_RenderDrawRect(renderer, &btn);
        // Green indicator
        SDL_SetRenderDrawColor(renderer, 50, 255, 100, 255);
        SDL_Rect ind = {bx, TOOLBAR_H - 3, 110, 3};
        SDL_RenderFillRect(renderer, &ind);
        drawEditorText(renderer, "\xe2\x96\xb6 F5 PLAY", bx + 12, 12, 14, {200, 255, 200, 255});
    }

    // Save button
    {
        int bx = screenW_ - 310;
        SDL_Rect btn = {bx, 6, 100, 34};
        SDL_SetRenderDrawColor(renderer, 25, 28, 45, 220);
        SDL_RenderFillRect(renderer, &btn);
        SDL_SetRenderDrawColor(renderer, 50, 52, 64, 180);
        SDL_RenderDrawRect(renderer, &btn);
        drawEditorText(renderer, "Ctrl+S Save", bx + 8, 12, 12, {140, 140, 150, 255});
    }

    // Info: map name + dimensions (second row inside toolbar)
    {
        char info[128];
        snprintf(info, sizeof(info), "%s  %dx%d", map_.name.c_str(), map_.width, map_.height);
        drawEditorText(renderer, info, 520, 6, 12, {100, 100, 110, 255});
    }

    // Tool-specific hint (second line of toolbar area)
    if (currentTool_ == EditorTool::Trigger) {
        const char* typeNames[] = {"Start", "End/Goal", "Crate", "Effect"};
        char tInfo[64];
        snprintf(tInfo, sizeof(tInfo), "Type: %s  |  T:cycle  C:condition", typeNames[(int)triggerGhost_.type]);
        drawEditorText(renderer, tInfo, 520, 22, 10, {200, 200, 100, 200});
    } else if (currentTool_ == EditorTool::Entity) {
        const char* entityNames[] = {"Melee", "Shooter", "Crate", "Upgrade Crate"};
        const char* eName = (entitySpawnType_ >= 0 && entitySpawnType_ < ENTITY_TYPE_COUNT) ? entityNames[entitySpawnType_] : "?";
        char eInfo[64];
        snprintf(eInfo, sizeof(eInfo), "Type: %s  |  E:cycle", eName);
        drawEditorText(renderer, eInfo, 520, 22, 10, {200, 100, 100, 200});
    } else if (currentTool_ == EditorTool::Select) {
        drawEditorText(renderer, "Click to select  |  Drag corners to resize  |  DEL to remove", 520, 22, 10, {100, 200, 200, 200});
    } else if (currentTool_ == EditorTool::Erase) {
        drawEditorText(renderer, "Click to erase tiles, triggers & enemies", 520, 22, 10, {200, 100, 100, 200});
    }

    // Shortcut hints
    drawEditorText(renderer, "Scroll:zoom  MMB:pan  RClick:erase  G:grid  TAB:UI", 520, 36, 9, {70, 70, 80, 255});
}

void MapEditor::renderPalette(SDL_Renderer* renderer) {
    int px = screenW_ - PALETTE_W;

    // Panel background
    SDL_SetRenderDrawColor(renderer, 12, 14, 24, 245);
    SDL_Rect bg = {px, TOOLBAR_H, PALETTE_W, screenH_ - TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);
    // Left border
    SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
    SDL_RenderDrawLine(renderer, px, TOOLBAR_H, px, screenH_);

    // Clip rendering to palette panel so nothing bleeds below/above
    SDL_Rect clip = {px, TOOLBAR_H, PALETTE_W, screenH_ - TOOLBAR_H};
    SDL_RenderSetClipRect(renderer, &clip);

    // Rebuild palette item Y cache
    paletteItemY_.clear();
    paletteItemY_.resize(palette_.size(), -999);

    int y = TOOLBAR_H + 10 - paletteScroll_;
    std::string lastCat;

    for (int i = 0; i < (int)palette_.size(); i++) {
        auto& pt = palette_[i];

        // Category header — always advance by fixed height, render only if visible
        if (pt.category != lastCat) {
            lastCat = pt.category;
            if (y >= TOOLBAR_H && y < screenH_) {
                SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
                SDL_RenderDrawLine(renderer, px + 8, y, px + PALETTE_W - 8, y);
                y += 4;
                std::string catLabel = pt.category;
                for (auto& c : catLabel) c = (char)toupper(c);
                drawEditorText(renderer, catLabel.c_str(), px + 10, y, 11, {0, 180, 160, 200});
                y += 16;
            } else {
                y += 20;  // same total as rendered path (4 + 16)
            }
        }

        paletteItemY_[i] = y;

        if (y >= TOOLBAR_H - TILE_PREVIEW && y < screenH_) {
            bool sel = (i == selectedPalette_);

            if (sel) {
                SDL_SetRenderDrawColor(renderer, 0, 180, 160, 30);
                SDL_Rect hl = {px + 2, y - 2, PALETTE_W - 4, TILE_PREVIEW + 4};
                SDL_RenderFillRect(renderer, &hl);
                SDL_SetRenderDrawColor(renderer, 0, 255, 228, 200);
                SDL_Rect acc = {px + 2, y - 2, 3, TILE_PREVIEW + 4};
                SDL_RenderFillRect(renderer, &acc);
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
            } else {
                SDL_SetRenderDrawColor(renderer, 40, 42, 55, 180);
            }
            SDL_RenderDrawRect(renderer, &dst);

            SDL_Color nameC = sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{160, 160, 170, 255};
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
        int viewH  = screenH_ - TOOLBAR_H;
        if (totalH > viewH) {
            float ratio  = (float)viewH / totalH;
            int barH = std::max(20, (int)(viewH * ratio));
            float scrollRatio = (float)paletteScroll_ / (totalH - viewH);
            int barY = TOOLBAR_H + (int)((viewH - barH) * scrollRatio);
            SDL_SetRenderDrawColor(renderer, 0, 120, 110, 60);
            SDL_Rect scrollBar = {px + PALETTE_W - 6, barY, 4, barH};
            SDL_RenderFillRect(renderer, &scrollBar);

            // Scroll hint at bottom
            drawEditorText(renderer, "\xe2\x86\x91\xe2\x86\x93 scroll", px + 8, screenH_ - 20, 9, {60, 60, 70, 200});
        }
    }
}

void MapEditor::drawEditorText(SDL_Renderer* renderer, const char* text, int x, int y, int size, SDL_Color color) {
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
}

void MapEditor::handleConfigInput(SDL_Event& e) {
    auto& cfg = config_;

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
    SDL_SetRenderDrawColor(renderer, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, screenW_, screenH_};
    SDL_RenderFillRect(renderer, &full);

    auto& cfg = config_;
    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color dimGray= {80, 80, 90, 255};
    SDL_Color yellow = {255, 220, 50, 255};
    SDL_Color green  = {50, 255, 100, 255};

    // Title
    drawEditorText(renderer, "MAP EDITOR", screenW_ / 2 - 100, 28, 32, cyan);
    SDL_SetRenderDrawColor(renderer, 0, 180, 160, 60);
    SDL_Rect tl = {screenW_ / 2 - 80, 68, 160, 1};
    SDL_RenderFillRect(renderer, &tl);

    // Panel background
    int panelX = screenW_ / 2 - 320;
    int panelW = 640;
    int panelY = 88;
    int panelH = screenH_ - 140;
    SDL_SetRenderDrawColor(renderer, 12, 14, 26, 200);
    SDL_Rect panel = {panelX, panelY, panelW, panelH};
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
    SDL_RenderDrawRect(renderer, &panel);

    int y = panelY + 16;
    int step = 44;
    int labelX = panelX + 30;
    int valX   = panelX + 240;
    char buf[256];

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
        drawEditorText(renderer, label, labelX, y, 18, sel ? white : gray);
        if (hasArrows && sel) {
            drawEditorText(renderer, "\xe2\x97\x80", valX - 24, y, 16, yellow);
            drawEditorText(renderer, value, valX, y, 18, cyan);
            int tw = (int)strlen(value) * 10 + 8;
            drawEditorText(renderer, "\xe2\x96\xb6", valX + tw, y, 16, yellow);
        } else {
            drawEditorText(renderer, value, valX, y, 18, sel ? cyan : dimGray);
        }
        y += step;
    };

    auto drawTextFieldEditing = [&](int idx, const char* label) {
        bool sel   = (cfg.field == idx);
        bool edit  = (cfg.textEditing && cfg.field == idx);
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 0, 180, 160, 30);
            SDL_Rect bar = {panelX + 4, y - 4, panelW - 8, 34};
            SDL_RenderFillRect(renderer, &bar);
            SDL_SetRenderDrawColor(renderer, 0, 200, 180, 180);
            SDL_Rect acc = {panelX + 4, y - 4, 3, 34};
            SDL_RenderFillRect(renderer, &acc);
        }
        drawEditorText(renderer, label, labelX, y, 18, sel ? white : gray);
        const char* curVal = (idx == 3) ? cfg.mapName.c_str() : cfg.creator.c_str();
        if (edit) {
            // Text input box
            SDL_SetRenderDrawColor(renderer, 20, 22, 38, 255);
            SDL_Rect box = {valX - 4, y - 4, 280, 28};
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 0, 200, 180, 180);
            SDL_RenderDrawRect(renderer, &box);
            std::string disp = cfg.textBuf + "_";
            drawEditorText(renderer, disp.c_str(), valX, y, 18, yellow);

            // Gamepad char palette (below field)
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
                int cx = palStartX + col * cellW;
                int cy = palStartY + row * cellH;
                bool isSel = (ci == cfg.gpCharIdx);
                if (isSel) {
                    SDL_SetRenderDrawColor(renderer, 0, 180, 160, 255);
                    SDL_Rect bg = {cx - 1, cy - 1, cellW - 2, cellH - 2};
                    SDL_RenderFillRect(renderer, &bg);
                }
                char ch[2] = { charPalette[ci], 0 };
                drawEditorText(renderer, ch, cx + 3, cy + 2, isSel ? 16 : 13, isSel ? white : dimGray);
            }
            drawEditorText(renderer, "A:type  Y:del  X/START:done  B:cancel", palStartX, palStartY + rows * cellH + 6, 11, dimGray);
            y += 34 + rows * cellH + 28; // Extra space for palette
        } else {
            drawEditorText(renderer, curVal, valX, y, 18, sel ? cyan : dimGray);
            if (sel) drawEditorText(renderer, "[A / ENTER]", valX + 200, y + 2, 12, dimGray);
            y += step;
        }
    };

    // ── Field 0: Action ──
    const char* actionStr = (cfg.action == EditorConfig::Action::NewMap) ? "NEW MAP" : "LOAD MAP";
    drawField(0, "ACTION", actionStr, true);

    // Separator
    SDL_SetRenderDrawColor(renderer, 40, 42, 55, 120);
    SDL_Rect sep = {panelX + 20, y - step / 2 + 4, panelW - 40, 1};
    SDL_RenderFillRect(renderer, &sep);

    if (cfg.action == EditorConfig::Action::NewMap) {
        // ── Field 1: Width ──
        snprintf(buf, sizeof(buf), "%d", cfg.mapWidth);
        drawField(1, "WIDTH", buf, true);

        // ── Field 2: Height ──
        snprintf(buf, sizeof(buf), "%d", cfg.mapHeight);
        drawField(2, "HEIGHT", buf, true);

        // ── Field 3: Map Name ──
        drawTextFieldEditing(3, "MAP NAME");

        // ── Field 4: Creator ──
        drawTextFieldEditing(4, "CREATOR");

        // ── Field 5: Game Mode ──
        drawField(5, "GAME MODE", cfg.gameMode == 0 ? "ARENA" : "SANDBOX", true);
    } else {
        // ── Load mode: map list ──
        bool sel = (cfg.field == 1);
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 0, 180, 160, 30);
            SDL_Rect bar = {panelX + 4, y - 4, panelW - 8, 34};
            SDL_RenderFillRect(renderer, &bar);
        }
        drawEditorText(renderer, "SELECT MAP", labelX, y, 18, sel ? white : gray);
        y += 30;

        if (cfg.availableMaps.empty()) {
            drawEditorText(renderer, "No .csm files found in maps/", labelX + 20, y, 16, dimGray);
            y += step * 2;
        } else {
            int startShow = std::max(0, cfg.loadIdx - 4);
            int endShow = std::min((int)cfg.availableMaps.size(), startShow + 8);
            for (int i = startShow; i < endShow; i++) {
                std::string fname = cfg.availableMaps[i];
                size_t slash = fname.find_last_of('/');
                if (slash != std::string::npos) fname = fname.substr(slash + 1);
                bool isCur = (i == cfg.loadIdx);
                if (isCur) {
                    SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
                    SDL_Rect row = {labelX + 12, y - 2, panelW - 80, 24};
                    SDL_RenderFillRect(renderer, &row);
                }
                snprintf(buf, sizeof(buf), "%s %s", isCur ? "\xe2\x96\xb6" : " ", fname.c_str());
                drawEditorText(renderer, buf, labelX + 20, y, isCur ? 16 : 14, isCur ? cyan : dimGray);
                y += 24;
            }
            y += 8;
        }
    }

    // ── Bottom buttons ──
    int btnY = screenH_ - 100;

    // Field 6: OK
    bool okSel = (cfg.field == 6);
    if (okSel) {
        SDL_SetRenderDrawColor(renderer, 0, 180, 160, 40);
        SDL_Rect bar = {screenW_ / 2 - 120, btnY - 4, 240, 32};
        SDL_RenderFillRect(renderer, &bar);
    }
    drawEditorText(renderer, okSel ? "> START EDITOR <" : "START EDITOR",
                   screenW_ / 2 - 80, btnY, 22, okSel ? green : gray);
    btnY += step;

    // Field 7: Cancel
    bool cancelSel = (cfg.field == 7);
    drawEditorText(renderer, cancelSel ? "> BACK <" : "BACK",
                   screenW_ / 2 - 32, btnY, 20, cancelSel ? white : dimGray);

    // Controls hint
    drawEditorText(renderer, "\xe2\x86\x91\xe2\x86\x93 navigate   \xe2\x86\x90\xe2\x86\x92 adjust   ENTER confirm   ESC back",
        screenW_ / 2 - 200, screenH_ - 30, 12, {60, 60, 70, 255});
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
            case SDL_CONTROLLER_BUTTON_BACK: // Delete selection  (- button on Switch)
                // If nothing selected, treat as exit-to-menu
                if (selectedTrigger_ < 0 && selectedEnemy_ < 0) {
                    wantsBack_ = true;
                } else {
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
            for (int i = 0; i < 5; i++) {
                int bx = 10 + i * 110;
                if (mouseX_ >= bx && mouseX_ < bx + 100 && mouseY_ >= 8 && mouseY_ < 40) {
                    currentTool_ = (EditorTool)i;
                    selectedTrigger_ = -1;
                    selectedEnemy_ = -1;
                    break;
                }
            }
            // Test Play button
            if (mouseX_ >= screenW_ - 180 && mouseX_ < screenW_ - 80 && mouseY_ >= 8 && mouseY_ < 40) {
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
