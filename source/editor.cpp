// ─── editor.cpp ─── Map Editor implementation ───────────────────────────────
#include "editor.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

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

    auto& a = Assets::instance();
    auto addBuiltin = [&](const char* name, const char* spritePath, uint8_t type, const char* cat) {
        EditorTile et;
        et.name     = name;
        et.path     = spritePath;
        et.texture  = a.tex(spritePath);
        et.tileType = type;
        et.category = cat;
        palette_.push_back(et);
    };

    addBuiltin("Grass",    "sprites/tiles/grass.png",    TILE_GRASS,  "ground");
    addBuiltin("Floor",    "sprites/tiles/floor.png",    TILE_FLOOR,  "ground");
    addBuiltin("Gravel",   "sprites/tiles/gravel.png",   TILE_GRAVEL, "ground");
    addBuiltin("Wood",     "sprites/tiles/wood.png",     TILE_WOOD,   "ground");
    addBuiltin("Sand",     "sprites/tiles/sand.png",     TILE_SAND,   "ground");
    addBuiltin("Wall",     "sprites/tiles/floor.png",    TILE_WALL,   "walls");
    addBuiltin("Glass",    "sprites/tiles/glass.png",    TILE_GLASS,  "walls");
    addBuiltin("Desk",     "sprites/tiles/desk.png",     TILE_DESK,   "walls");
    addBuiltin("Box",      "sprites/props/box.png",      TILE_BOX,    "walls");

    scanTileFolder("tiles/ground",  "ground",  TILE_GRASS);
    scanTileFolder("tiles/walls",   "walls",   TILE_WALL);
    scanTileFolder("tiles/ceiling", "ceiling", TILE_GLASS);
    scanTileFolder("tiles/props",   "props",   TILE_DESK);
    scanTileFolder("romfs/tiles/ground",  "ground",  TILE_GRASS);
    scanTileFolder("romfs/tiles/walls",   "walls",   TILE_WALL);
    scanTileFolder("romfs/tiles/ceiling", "ceiling", TILE_GLASS);
    scanTileFolder("romfs/tiles/props",   "props",   TILE_DESK);

    printf("Editor palette: %d tiles loaded\n", (int)palette_.size());
}

void MapEditor::scanTileFolder(const std::string& folder, const std::string& category, uint8_t defaultType) {
    DIR* dir = opendir(folder.c_str());
    if (!dir) return;

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
        et.tileType = defaultType;
        et.category = category;
        palette_.push_back(et);
    }
    closedir(dir);
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
    return map_.saveToFile(path);
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
            case SDLK_3: currentTool_ = EditorTool::Enemy; break;
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
                if (currentTool_ == EditorTool::Enemy) {
                    enemySpawnType_ = (enemySpawnType_ + 1) % 2;
                }
                break;
            case SDLK_s:
                if (SDL_GetModState() & KMOD_CTRL) {
                    mkdir("maps", 0755);
                    if (saveMap(savePath_))
                        printf("Map saved to %s\n", savePath_.c_str());
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
    if (showConfig_) return; // Config screen doesn't need update

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
                case EditorTool::Enemy:   placeEnemy(wx, wy); mouseDown_ = false; break;
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
    es.enemyType  = enemySpawnType_;
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

    SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
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
    renderTriggers(renderer);
    renderEnemySpawns(renderer);

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

    // ── Zoom indicator ──
    {
        char zoomStr[32];
        snprintf(zoomStr, sizeof(zoomStr), "%.0f%%", zoom_ * 100);
        drawEditorText(renderer, zoomStr, 10, screenH_ - 30, 14, {180, 180, 180, 200});
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

void MapEditor::renderEnemySpawns(SDL_Renderer* renderer) {
    for (int i = 0; i < (int)map_.enemySpawns.size(); i++) {
        auto& es = map_.enemySpawns[i];
        int sx = worldToScreenX(es.x);
        int sy = worldToScreenY(es.y);
        int sz = (int)(24 * zoom_);
        SDL_Rect r = {sx - sz/2, sy - sz/2, sz, sz};

        bool selected = (i == selectedEnemy_);

        if (es.enemyType == 0) {
            SDL_SetRenderDrawColor(renderer, 255, 60, 60, 200);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 160, 40, 200);
        }
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, selected ? 0 : 255, 255, selected ? 0 : 255, 255);
        SDL_RenderDrawRect(renderer, &r);

        const char* label = (es.enemyType == 0) ? "M" : "S";
        drawEditorText(renderer, label, r.x + sz/4, r.y + 2, 14, {255, 255, 255, 255});
    }
}

void MapEditor::renderToolbar(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 20, 20, 28, 240);
    SDL_Rect bg = {0, 0, screenW_, TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);

    // Tool buttons (clickable)
    const char* toolNames[] = {"1:Tile", "2:Trigger", "3:Enemy", "4:Erase", "5:Select"};
    for (int i = 0; i < 5; i++) {
        int bx = 10 + i * 110;
        SDL_Rect btn = {bx, 8, 100, 32};
        bool sel = ((int)currentTool_ == i);
        SDL_SetRenderDrawColor(renderer, sel ? 0 : 50, sel ? 200 : 50, sel ? 180 : 60, 255);
        SDL_RenderFillRect(renderer, &btn);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &btn);
        drawEditorText(renderer, toolNames[i], bx + 8, 14, 14, {255, 255, 255, 255});
    }

    // Test Play button
    {
        int bx = screenW_ - 180;
        SDL_Rect btn = {bx, 8, 100, 32};
        SDL_SetRenderDrawColor(renderer, 30, 180, 60, 255);
        SDL_RenderFillRect(renderer, &btn);
        SDL_SetRenderDrawColor(renderer, 200, 255, 200, 255);
        SDL_RenderDrawRect(renderer, &btn);
        drawEditorText(renderer, "F5: PLAY", bx + 8, 14, 14, {255, 255, 255, 255});
    }

    // Info text
    char info[128];
    snprintf(info, sizeof(info), "%s | %dx%d | G:grid TAB:ui Ctrl+S:save",
        map_.name.c_str(), map_.width, map_.height);
    drawEditorText(renderer, info, 580, 6, 11, {180, 180, 180, 255});

    // Tool-specific info
    if (currentTool_ == EditorTool::Trigger) {
        const char* typeNames[] = {"Start", "End/Goal", "Crate", "Effect"};
        char tInfo[64];
        snprintf(tInfo, sizeof(tInfo), "Trigger: %s (T:cycle C:condition)", typeNames[(int)triggerGhost_.type]);
        drawEditorText(renderer, tInfo, 580, 22, 10, {200, 200, 100, 255});
    } else if (currentTool_ == EditorTool::Enemy) {
        char eInfo[64];
        snprintf(eInfo, sizeof(eInfo), "Enemy: %s (E:toggle)", enemySpawnType_ == 0 ? "Melee" : "Shooter");
        drawEditorText(renderer, eInfo, 580, 22, 10, {200, 100, 100, 255});
    } else if (currentTool_ == EditorTool::Select) {
        drawEditorText(renderer, "Click trigger/enemy to select, drag corners to resize, DEL to remove", 580, 22, 10, {100, 200, 200, 255});
    } else if (currentTool_ == EditorTool::Erase) {
        drawEditorText(renderer, "Click to erase tiles, triggers & enemies", 580, 22, 10, {200, 100, 100, 255});
    }

    // Shortcut hints
    drawEditorText(renderer, "Scroll: zoom | MMB: pan | RClick: erase", 580, 36, 9, {120, 120, 120, 255});
}

void MapEditor::renderPalette(SDL_Renderer* renderer) {
    int px = screenW_ - PALETTE_W;
    SDL_SetRenderDrawColor(renderer, 25, 25, 32, 240);
    SDL_Rect bg = {px, TOOLBAR_H, PALETTE_W, screenH_ - TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);

    // Rebuild palette item Y cache
    paletteItemY_.clear();
    paletteItemY_.resize(palette_.size(), -999);

    int y = TOOLBAR_H + 8 - paletteScroll_;
    std::string lastCat;

    for (int i = 0; i < (int)palette_.size(); i++) {
        auto& pt = palette_[i];

        if (pt.category != lastCat) {
            lastCat = pt.category;
            if (y >= TOOLBAR_H && y < screenH_)
                drawEditorText(renderer, pt.category.c_str(), px + 8, y, 12, {0, 200, 180, 255});
            y += 20;
        }

        paletteItemY_[i] = y;  // cache actual screen Y

        if (y >= TOOLBAR_H - TILE_PREVIEW && y < screenH_) {
            SDL_Rect dst = {px + 8, y, TILE_PREVIEW, TILE_PREVIEW};
            if (pt.texture) {
                SDL_RenderCopy(renderer, pt.texture, nullptr, &dst);
            } else {
                SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
                SDL_RenderFillRect(renderer, &dst);
            }

            if (i == selectedPalette_) {
                SDL_SetRenderDrawColor(renderer, 0, 255, 200, 255);
                SDL_Rect hl = {dst.x - 2, dst.y - 2, dst.w + 4, dst.h + 4};
                SDL_RenderDrawRect(renderer, &hl);
            }

            drawEditorText(renderer, pt.name.c_str(), px + TILE_PREVIEW + 16, y + 14, 12,
                {200, 200, 200, 255});
        }

        y += TILE_PREVIEW + 4;
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
                // In load mode, skip fields 2/3/4 which are for new map only
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 5)
                    cfg.field = 1;
                break;
            case SDLK_DOWN:
                cfg.field++;
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 5)
                    cfg.field = 5;
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
                break;
            case SDLK_RETURN:
                if ((cfg.field == 3 || cfg.field == 4) && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.textEditing = true;
                    cfg.textBuf = (cfg.field == 3) ? cfg.mapName : cfg.creator;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                }
                else if (cfg.field == 5) { // OK
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
                        std::string safeName = cfg.mapName;
                        for (char& c : safeName) {
                            if (c == ' ') c = '_';
                            if (c == '/' || c == '\\') c = '_';
                        }
                        savePath_ = "maps/" + safeName + ".csm";
                    }
                    showConfig_ = false;
                }
                else if (cfg.field == 6) { // Cancel
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
        if (cfg.field > 6) cfg.field = 6;
    }

    // Navigation via gamepad
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                cfg.field--;
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 5)
                    cfg.field = 1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                cfg.field++;
                if (cfg.action == EditorConfig::Action::LoadMap && cfg.field > 1 && cfg.field < 5)
                    cfg.field = 5;
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
                break;
            case SDL_CONTROLLER_BUTTON_A: {
                // Handle A button directly for each field (no recursive call)
                if ((cfg.field == 3 || cfg.field == 4) && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.textEditing = true;
                    cfg.textBuf = (cfg.field == 3) ? cfg.mapName : cfg.creator;
                }
                else if (cfg.field == 5) { // OK
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
                        std::string safeName = cfg.mapName;
                        for (char& c : safeName) {
                            if (c == ' ') c = '_';
                            if (c == '/' || c == '\\') c = '_';
                        }
                        savePath_ = "maps/" + safeName + ".csm";
                    }
                    showConfig_ = false;
                }
                else if (cfg.field == 6) { // Cancel
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
        if (cfg.field > 6) cfg.field = 6;
    }
}

void MapEditor::renderConfig(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 10, 10, 16, 255);
    SDL_Rect full = {0, 0, screenW_, screenH_};
    SDL_RenderFillRect(renderer, &full);

    auto& cfg = config_;
    SDL_Color title = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {150, 150, 150, 255};
    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color yellow = {255, 220, 50, 255};

    drawEditorText(renderer, "MAP EDITOR SETUP", screenW_ / 2 - 140, 30, 30, title);

    int y = 100;
    int step = 42;
    char buf[256];

    auto fieldColor = [&](int idx) -> SDL_Color { return cfg.field == idx ? white : gray; };
    auto valColor   = [&](int idx) -> SDL_Color { return cfg.field == idx ? cyan : gray; };

    // Field 0: Action (New / Load)
    drawEditorText(renderer, "ACTION:", 100, y, 20, fieldColor(0));
    if (cfg.field == 0) {
        drawEditorText(renderer, "<", 420, y, 20, yellow);
        drawEditorText(renderer, cfg.action == EditorConfig::Action::NewMap ? "NEW MAP" : "LOAD MAP", 460, y, 20, valColor(0));
        drawEditorText(renderer, ">", 700, y, 20, yellow);
    } else {
        drawEditorText(renderer, cfg.action == EditorConfig::Action::NewMap ? "NEW MAP" : "LOAD MAP", 460, y, 20, valColor(0));
    }
    y += step;

    if (cfg.action == EditorConfig::Action::NewMap) {
        // Field 1: Width
        snprintf(buf, sizeof(buf), "%d", cfg.mapWidth);
        drawEditorText(renderer, "WIDTH:", 100, y, 20, fieldColor(1));
        if (cfg.field == 1) {
            drawEditorText(renderer, "<", 420, y, 20, yellow);
            drawEditorText(renderer, buf, 460, y, 20, valColor(1));
            drawEditorText(renderer, ">", 560, y, 20, yellow);
        } else {
            drawEditorText(renderer, buf, 460, y, 20, valColor(1));
        }
        y += step;

        // Field 2: Height
        snprintf(buf, sizeof(buf), "%d", cfg.mapHeight);
        drawEditorText(renderer, "HEIGHT:", 100, y, 20, fieldColor(2));
        if (cfg.field == 2) {
            drawEditorText(renderer, "<", 420, y, 20, yellow);
            drawEditorText(renderer, buf, 460, y, 20, valColor(2));
            drawEditorText(renderer, ">", 560, y, 20, yellow);
        } else {
            drawEditorText(renderer, buf, 460, y, 20, valColor(2));
        }
        y += step;

        // Field 3: Map Name
        drawEditorText(renderer, "MAP NAME:", 100, y, 20, fieldColor(3));
        if (cfg.textEditing && cfg.field == 3) {
            std::string disp = cfg.textBuf + "_";
            drawEditorText(renderer, disp.c_str(), 460, y, 20, yellow);
            // Show gamepad char palette
            static const char charPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
            int pi = cfg.gpCharIdx;
            int palLen = (int)strlen(charPalette);
            char charRow[48];
            int cx = 0;
            for (int d = -5; d <= 5; d++) {
                int idx = (pi + d + palLen) % palLen;
                charRow[cx++] = (d == 0) ? '[' : ' ';
                charRow[cx++] = charPalette[idx];
                charRow[cx++] = (d == 0) ? ']' : ' ';
            }
            charRow[cx] = 0;
            drawEditorText(renderer, charRow, 100, y + 24, 16, cyan);
            drawEditorText(renderer, "DPad:select  A:type  Y:del  X:done  B:cancel", 100, y + 44, 12, gray);
        } else {
            drawEditorText(renderer, cfg.mapName.c_str(), 460, y, 20, valColor(3));
            if (cfg.field == 3) drawEditorText(renderer, "[A / ENTER to edit]", 700, y, 14, gray);
        }
        y += step;

        // Field 4: Creator
        drawEditorText(renderer, "CREATOR:", 100, y, 20, fieldColor(4));
        if (cfg.textEditing && cfg.field == 4) {
            std::string disp = cfg.textBuf + "_";
            drawEditorText(renderer, disp.c_str(), 460, y, 20, yellow);
            static const char charPalette2[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
            int pi = cfg.gpCharIdx;
            int palLen = (int)strlen(charPalette2);
            char charRow[48];
            int cx = 0;
            for (int d = -5; d <= 5; d++) {
                int idx = (pi + d + palLen) % palLen;
                charRow[cx++] = (d == 0) ? '[' : ' ';
                charRow[cx++] = charPalette2[idx];
                charRow[cx++] = (d == 0) ? ']' : ' ';
            }
            charRow[cx] = 0;
            drawEditorText(renderer, charRow, 100, y + 24, 16, cyan);
            drawEditorText(renderer, "DPad:select  A:type  Y:del  X:done  B:cancel", 100, y + 44, 12, gray);
        } else {
            drawEditorText(renderer, cfg.creator.c_str(), 460, y, 20, valColor(4));
            if (cfg.field == 4) drawEditorText(renderer, "[A / ENTER to edit]", 700, y, 14, gray);
        }
        y += step;
    } else {
        // Load mode — show available maps
        drawEditorText(renderer, "SELECT MAP:", 100, y, 20, fieldColor(1));
        y += 28;

        if (cfg.availableMaps.empty()) {
            drawEditorText(renderer, "No maps found in maps/ folder", 140, y, 16, gray);
            y += step * 3;
        } else {
            int startShow = std::max(0, cfg.loadIdx - 3);
            int endShow = std::min((int)cfg.availableMaps.size(), startShow + 8);
            for (int i = startShow; i < endShow; i++) {
                std::string fname = cfg.availableMaps[i];
                size_t slash = fname.find_last_of('/');
                if (slash != std::string::npos) fname = fname.substr(slash + 1);
                SDL_Color mc = (i == cfg.loadIdx) ? cyan : gray;
                if (i == cfg.loadIdx) {
                    snprintf(buf, sizeof(buf), "> %s", fname.c_str());
                } else {
                    snprintf(buf, sizeof(buf), "  %s", fname.c_str());
                }
                drawEditorText(renderer, buf, 140, y, 16, mc);
                y += 26;
            }
            y += 10;
        }
    }

    y = screenH_ - 140;
    // Field 5: OK button
    SDL_Color okC = (cfg.field == 5) ? white : gray;
    drawEditorText(renderer, cfg.field == 5 ? "> START EDITOR <" : "START EDITOR", screenW_ / 2 - 80, y, 22, okC);
    y += step;

    // Field 6: Cancel
    SDL_Color cancelC = (cfg.field == 6) ? white : gray;
    drawEditorText(renderer, cfg.field == 6 ? "> BACK <" : "BACK", screenW_ / 2 - 40, y, 22, cancelC);

    // Instructions
    drawEditorText(renderer, "UP/DOWN navigate  LEFT/RIGHT adjust  ENTER confirm  BACKSPACE back",
        screenW_ / 2 - 280, screenH_ - 35, 14, gray);
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
                if (saveMap(savePath_))
                    printf("Map saved to %s\n", savePath_.c_str());
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
                }
                if (currentTool_ == EditorTool::Enemy) enemySpawnType_ = (enemySpawnType_ + 1) % 2;
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
            case SDL_CONTROLLER_BUTTON_BACK: // Delete selection
                if (selectedTrigger_ >= 0 && selectedTrigger_ < (int)map_.triggers.size()) {
                    map_.triggers.erase(map_.triggers.begin() + selectedTrigger_);
                    selectedTrigger_ = -1;
                }
                if (selectedEnemy_ >= 0 && selectedEnemy_ < (int)map_.enemySpawns.size()) {
                    map_.enemySpawns.erase(map_.enemySpawns.begin() + selectedEnemy_);
                    selectedEnemy_ = -1;
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
        if (SDL_IsGameController(i)) { gc = SDL_GameControllerFromInstanceID(i); break; }
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
