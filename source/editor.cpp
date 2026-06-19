#include "editor.h"
#include "mod.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#ifdef __WIIU__
#include <SDL2/SDL_syswm.h>  // native swkbd OK/CANCEL events
#endif

// Open the platform text-input/IME for a config text field, seeding it with the
// current value. Switch uses the gamepad char-palette (no IME). Wii U shows the
// native nn::swkbd; the OK/CANCEL it raises is handled in handleConfigInput.
static inline void editorBeginTextInput(const std::string& initial) {
#if defined(__SWITCH__)
    (void)initial;
#else
#  if defined(__WIIU__)
    SDL_WiiUSetSWKBDInitialText(initial.c_str());
    SDL_WiiUSetSWKBDOKLabel("OK");
#  endif
    SDL_StartTextInput();
#endif
}
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

// Toolbar icon glyphs, drawn procedurally so the editor needs no image assets.
enum class TIcon {
    Tile, Trigger, Entity, Erase, Select, Rect, Fill,
    Undo, Redo, Grid, Props, Rnd, NoCo, Top, Scene, Help
};

// Draw a ~16px monochrome glyph centred on (cx, cy).
void drawGlyph(SDL_Renderer* r, TIcon ic, int cx, int cy, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    auto rect = [&](int x, int y, int w, int h) { SDL_Rect q = {x, y, w, h}; SDL_RenderDrawRect(r, &q); };
    auto fill = [&](int x, int y, int w, int h) { SDL_Rect q = {x, y, w, h}; SDL_RenderFillRect(r, &q); };
    auto line = [&](int x0, int y0, int x1, int y1) { SDL_RenderDrawLine(r, x0, y0, x1, y1); };
    auto disc = [&](int x0, int y0, int rad) {
        for (int dy = -rad; dy <= rad; dy++) {
            int dx = (int)sqrtf((float)(rad * rad - dy * dy));
            line(x0 - dx, y0 + dy, x0 + dx, y0 + dy);
        }
    };
    switch (ic) {
        case TIcon::Tile:  // 2x2 tiles
            fill(cx-7, cy-7, 6, 6); fill(cx+1, cy-7, 6, 6);
            fill(cx-7, cy+1, 6, 6); fill(cx+1, cy+1, 6, 6); break;
        case TIcon::Trigger:  // dashed zone
            for (int i = -7; i < 7; i += 3) { line(cx+i, cy-7, cx+i+1, cy-7); line(cx+i, cy+6, cx+i+1, cy+6); }
            for (int i = -7; i < 7; i += 3) { line(cx-7, cy+i, cx-7, cy+i+1); line(cx+6, cy+i, cx+6, cy+i+1); }
            break;
        case TIcon::Entity:  // figure: head + body
            disc(cx, cy-4, 3); fill(cx-4, cy+0, 9, 7); break;
        case TIcon::Erase:  // eraser block with band
            fill(cx-7, cy-3, 14, 8); rect(cx-7, cy-3, 14, 8);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 180); fill(cx-7, cy-3, 14, 3);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); break;
        case TIcon::Select: {  // cursor arrow
            for (int i = 0; i < 10; i++) line(cx-5, cy-7+i, cx-5+i, cy-7+i);
            line(cx-1, cy+1, cx+2, cy+6); line(cx+0, cy+0, cx+3, cy+5);
            break;
        }
        case TIcon::Rect:  rect(cx-7, cy-6, 14, 12); break;
        case TIcon::Fill:  // paint bucket
            line(cx-6, cy-2, cx, cy-8); line(cx-6, cy-2, cx+2, cy+4);
            line(cx, cy-8, cx+6, cy-1); line(cx+2, cy+4, cx+6, cy-1);
            fill(cx+5, cy+1, 3, 5); break;
        case TIcon::Undo:  // curved left arrow
            for (int a = -70; a <= 180; a += 12) {
                float rad = a * 3.14159f / 180.0f;
                SDL_RenderDrawPoint(r, cx + (int)(6*cosf(rad)), cy - (int)(6*sinf(rad)));
            }
            line(cx-6, cy-1, cx-6, cy+5); line(cx-6, cy+5, cx-1, cy+5); break;
        case TIcon::Redo:  // curved right arrow
            for (int a = 0; a <= 250; a += 12) {
                float rad = a * 3.14159f / 180.0f;
                SDL_RenderDrawPoint(r, cx + (int)(6*cosf(rad)), cy - (int)(6*sinf(rad)));
            }
            line(cx+6, cy-1, cx+6, cy+5); line(cx+6, cy+5, cx+1, cy+5); break;
        case TIcon::Grid:  // hash
            line(cx-3, cy-7, cx-3, cy+7); line(cx+3, cy-7, cx+3, cy+7);
            line(cx-7, cy-3, cx+7, cy-3); line(cx-7, cy+3, cx+7, cy+3); break;
        case TIcon::Props:  // sliders
            for (int k = -5; k <= 5; k += 5) { line(cx-7, cy+k, cx+7, cy+k); }
            fill(cx-2, cy-7, 4, 4); fill(cx+1, cy-2, 4, 4); fill(cx-5, cy+3, 4, 4); break;
        case TIcon::Rnd:  // die
            rect(cx-7, cy-7, 14, 14);
            fill(cx-4, cy-4, 2, 2); fill(cx-1, cy-1, 2, 2); fill(cx+2, cy+2, 2, 2); break;
        case TIcon::NoCo:  // square with slash
            rect(cx-7, cy-7, 14, 14); line(cx-7, cy+7, cx+7, cy-7); break;
        case TIcon::Top:  // stacked layers
            rect(cx-7, cy-1, 11, 8); rect(cx-3, cy-6, 11, 8); break;
        case TIcon::Scene:  // clapperboard
            fill(cx-7, cy-2, 14, 9); fill(cx-7, cy-6, 14, 4);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
            line(cx-4, cy-6, cx-6, cy-2); line(cx+0, cy-6, cx-2, cy-2); line(cx+4, cy-6, cx+2, cy-2);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); break;
        case TIcon::Help:  break;  // drawn as "?" text
    }
}

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
    dirty_ = true;
}

// True when a UI panel covers the given screen point (clicks there belong to
// the immediate-mode widgets drawn in render(), never to the map canvas).
bool MapEditor::isOverUI(int sx, int sy) const {
    if (showHelp_) return true;
    if (showUI_) {
        if (sy < TOOLBAR_H) return true;                  // toolbar
        if (sx >= screenW_ - PALETTE_W) return true;      // tile palette
        if (leftPanelH_ > 0 &&
            sx >= 8 && sx < 8 + 220 &&
            sy >= TOOLBAR_H + 8 && sy < TOOLBAR_H + 8 + leftPanelH_)
            return true;                                  // properties panel
    }
    if (showMapProps_ && mapPropsH_ > 0) {
        int px = screenW_ - uiPaletteW() - 230 - 8;
        if (sx >= px && sx < px + 230 &&
            sy >= uiToolbarH() + 8 && sy < uiToolbarH() + 8 + mapPropsH_)
            return true;                                  // map properties panel
    }
    if (showCutsceneEditor_ && sy >= screenH_ - csEditor_.panelHeight())
        return true;                                      // cutscene panel
    return false;
}

// Bucket-fill the connected region of identical tiles under (tx, ty)
// with the selected palette tile.
void MapEditor::floodFill(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= map_.width || ty >= map_.height) return;
    if (selectedPalette_ < 0 || selectedPalette_ >= (int)palette_.size()) return;
    auto& pt = palette_[selectedPalette_];
    if (pt.category == "props") return;  // free props cannot be flood-filled

    const int w = map_.width, h = map_.height;
    if (pt.category == "ceiling") {
        uint8_t target = map_.ceiling[ty * w + tx];
        if (target == CEIL_GLASS) return;
        std::vector<int> stack = {ty * w + tx};
        while (!stack.empty()) {
            int idx = stack.back(); stack.pop_back();
            if (map_.ceiling[idx] != target) continue;
            map_.ceiling[idx] = CEIL_GLASS;
            int x = idx % w, y = idx / w;
            if (x > 0)     stack.push_back(idx - 1);
            if (x < w - 1) stack.push_back(idx + 1);
            if (y > 0)     stack.push_back(idx - w);
            if (y < h - 1) stack.push_back(idx + w);
        }
        return;
    }

    uint8_t target = map_.tiles[ty * w + tx];
    if (target == pt.tileType) return;
    std::vector<int> stack = {ty * w + tx};
    while (!stack.empty()) {
        int idx = stack.back(); stack.pop_back();
        if (map_.tiles[idx] != target) continue;
        map_.tiles[idx] = pt.tileType;
        if ((int)map_.tileRotations.size() > idx)
            map_.tileRotations[idx] = randomRotation_ ? (uint8_t)(rand() % 4) : 0;
        if ((int)map_.tileNoCollide.size() > idx)
            map_.tileNoCollide[idx] = noCollision_ ? 1 : 0;
        int x = idx % w, y = idx / w;
        if (x > 0)     stack.push_back(idx - 1);
        if (x < w - 1) stack.push_back(idx + 1);
        if (y > 0)     stack.push_back(idx - w);
        if (y < h - 1) stack.push_back(idx + w);
    }
}

// Eyedropper: select the palette entry matching whatever is under the cursor
// (free prop first, then ceiling, then the ground/wall tile).
void MapEditor::pickTileAt(int tx, int ty, float wx, float wy) {
    int wantType = -1;
    bool wantCeiling = false, wantProp = false;

    const float r = (float)TILE_SIZE * 0.6f;
    for (int i = (int)map_.props.size() - 1; i >= 0; i--) {
        auto& p = map_.props[i];
        if (fabsf(p.x - wx) < r && fabsf(p.y - wy) < r) {
            wantType = p.tileType;
            wantProp = true;
            break;
        }
    }
    if (wantType < 0 && tx >= 0 && ty >= 0 && tx < map_.width && ty < map_.height) {
        int idx = ty * map_.width + tx;
        if (map_.ceiling[idx] == CEIL_GLASS) {
            wantCeiling = true;
        } else {
            wantType = map_.tiles[idx];
        }
    }

    int best = -1;
    for (int i = 0; i < (int)palette_.size(); i++) {
        auto& p = palette_[i];
        if (wantCeiling) {
            if (p.category == "ceiling") { best = i; break; }
            continue;
        }
        if ((int)p.tileType != wantType) continue;
        bool catMatch = wantProp ? (p.category == "props") : (p.category != "props");
        if (best < 0 || catMatch) best = i;
        if (catMatch) break;
    }
    if (best < 0) return;

    selectedPalette_ = best;
    if (currentTool_ != EditorTool::Rect && currentTool_ != EditorTool::Fill)
        currentTool_ = EditorTool::Tile;
    // Make sure the pick is visible in the palette list
    paletteTab_ = PaletteTab::All;
    rebuildFilteredPalette();
    for (int i = 0; i < (int)filteredPalette_.size(); i++)
        if (filteredPalette_[i] == best) { filteredSelection_ = i; break; }
    scrollPaletteToSelection();
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
        // Wii U content paths (fs:/vol/content/)
        {"fs:/vol/content/tiles/ground",  "ground",  TILE_GRASS},
        {"fs:/vol/content/tiles/walls",   "walls",   TILE_WALL},
        {"fs:/vol/content/tiles/ceiling", "ceiling", TILE_GLASS},
        {"fs:/vol/content/tiles/props",   "props",   TILE_DESK},
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
    dirty_ = false;
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
        dirty_ = false;
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
    dirty_ = false;
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

    // Help overlay: any click or key closes it and is consumed
    if (showHelp_) {
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_KEYDOWN ||
            e.type == SDL_CONTROLLERBUTTONDOWN) {
            showHelp_ = false;
            if (ui_) { ui_->mouseClicked = false; ui_->clickCooldownFrames = 2; }
        }
        return;
    }

    // Cutscene editor gets first pick of events while its panel is open
    if (showCutsceneEditor_) {
        int emx = mouseX_, emy = mouseY_;
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            emx = e.button.x; emy = e.button.y;
        } else if (e.type == SDL_MOUSEMOTION) {
            emx = e.motion.x; emy = e.motion.y;
        }
        bool consumed = csEditor_.handleEvent(e, screenToWorldX(emx), screenToWorldY(emy), zoom_);
        if (csEditor_.takeUiClickSwallow() && ui_) {
            // Hide this click from all immediate-mode widgets this frame
            ui_->mouseClicked = false;
            if (ui_->clickCooldownFrames < 1) ui_->clickCooldownFrames = 1;
        }
        if (consumed) return;
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

        // Clicks on UI panels are handled by the immediate-mode widgets in
        // render(); never let them fall through to the map canvas.
        if (isOverUI(mouseX_, mouseY_)) {
            mouseDown_ = false;
            rightDown_ = false;
            return;
        }
        
        // Eyedropper: Alt+click picks the hovered tile into the palette
        if (e.button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_ALT)) {
            float wx = screenToWorldX(mouseX_);
            float wy = screenToWorldY(mouseY_);
            pickTileAt((int)(wx / TILE_SIZE), (int)(wy / TILE_SIZE), wx, wy);
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

            const int ballR = 12;
            int bdx = mouseX_ - cx, bdy = mouseY_ - cy;
            if (bdx*bdx + bdy*bdy <= ballR*ballR) {
                draggingMove_ = true;
                draggingMovePushed_ = false;  // undo pushed on first movement
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

            // Click an enemy: select it and start dragging it right away
            // (enemies are small, so they win over overlapping triggers)
            int ei = enemyAt(wx, wy);
            if (ei >= 0) {
                selectedEnemy_ = ei;
                selectedTrigger_ = -1;
                draggingMove_ = true;
                draggingMovePushed_ = false;
                moveDragSX_ = mouseX_; moveDragSY_ = mouseY_;
                moveObjOrigX_ = map_.enemySpawns[ei].x;
                moveObjOrigY_ = map_.enemySpawns[ei].y;
                mouseDown_ = false;
                return;
            }
            // Click a trigger: select it and start dragging its body
            int ti = triggerAt(wx, wy);
            if (ti >= 0) {
                selectedTrigger_ = ti;
                selectedEnemy_ = -1;
                draggingMove_ = true;
                draggingMovePushed_ = false;
                moveDragSX_ = mouseX_; moveDragSY_ = mouseY_;
                moveObjOrigX_ = map_.triggers[ti].x;
                moveObjOrigY_ = map_.triggers[ti].y;
                mouseDown_ = false;
                return;
            }
            // Click on empty canvas: deselect
            selectedTrigger_ = -1;
            selectedEnemy_   = -1;
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
            draggingMovePushed_ = false;
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

        // Move drag (undo snapshot taken on first actual movement so plain
        // click-to-select doesn't pollute the undo stack)
        if (draggingMove_) {
            if (!draggingMovePushed_ &&
                (abs(mouseX_ - moveDragSX_) > 2 || abs(mouseY_ - moveDragSY_) > 2)) {
                pushUndo();
                draggingMovePushed_ = true;
            }
            if (draggingMovePushed_) {
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

    // Var name text editing for trigger condition
    if (trigCondEditingName_) {
        // Find live pointer
        TriggerCondition* tc = nullptr;
        for (auto& c : csLib_.triggerConditions)
            if (c.triggerIndex == selectedTrigger_) { tc = &c; break; }
        if (!tc) { trigCondEditingName_ = false; }
        else if (e.type == SDL_TEXTINPUT) {
            trigCondNameBuf_ += e.text.text;
            return;
        } else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_BACKSPACE && !trigCondNameBuf_.empty()) {
                trigCondNameBuf_.pop_back();
            } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE) {
                if (e.key.keysym.sym == SDLK_RETURN && !trigCondNameBuf_.empty())
                    tc->varName = trigCondNameBuf_;
                trigCondEditingName_ = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            }
            return;
        }
    }

    // Cooldown text editing for multi-fire config
    if (trigMultiCooldownEditing_) {
        TriggerMultiConfig* mc = nullptr;
        for (auto& m : csLib_.triggerMultiConfigs)
            if (m.triggerIndex == selectedTrigger_) { mc = &m; break; }
        if (!mc) { trigMultiCooldownEditing_ = false; }
        else if (e.type == SDL_TEXTINPUT) {
            // Only allow digits and a single decimal point
            for (const char* p = e.text.text; *p; ++p) {
                if ((*p >= '0' && *p <= '9') || (*p == '.' && trigMultiCooldownBuf_.find('.') == std::string::npos))
                    trigMultiCooldownBuf_ += *p;
            }
            return;
        } else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_BACKSPACE && !trigMultiCooldownBuf_.empty()) {
                trigMultiCooldownBuf_.pop_back();
            } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE) {
                if (e.key.keysym.sym == SDLK_RETURN && !trigMultiCooldownBuf_.empty()) {
                    float v = (float)atof(trigMultiCooldownBuf_.c_str());
                    if (v < 0.0f) v = 0.0f;
                    mc->cooldown = v;
                }
                trigMultiCooldownEditing_ = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            }
            return;
        }
    }

    // Var list name editing
    if (varListEditingName_ && showVarList_) {
        // Collect current var name list for context
        std::vector<std::string> vnames;
        for (const auto& va : csLib_.triggerVarActions) {
            bool found = false;
            for (const auto& n : vnames) if (n == va.key) { found = true; break; }
            if (!found && !va.key.empty()) vnames.push_back(va.key);
        }
        for (const auto& tc : csLib_.triggerConditions) {
            bool found = false;
            for (const auto& n : vnames) if (n == tc.varName) { found = true; break; }
            if (!found && !tc.varName.empty()) vnames.push_back(tc.varName);
        }
        for (const auto& kv : csLib_.varDefaults) {
            bool found = false;
            for (const auto& n : vnames) if (n == kv.first) { found = true; break; }
            if (!found) vnames.push_back(kv.first);
        }
        std::sort(vnames.begin(), vnames.end());

        bool isNew = (varListSelected_ == (int)vnames.size());

        if (e.type == SDL_TEXTINPUT) {
            varListNameBuf_ += e.text.text;
            return;
        } else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_BACKSPACE && !varListNameBuf_.empty()) {
                varListNameBuf_.pop_back();
            } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE) {
                if (e.key.keysym.sym == SDLK_RETURN && !varListNameBuf_.empty()) {
                    pushUndo();
                    std::string oldName = isNew ? "" : vnames[varListSelected_];
                    std::string newName = varListNameBuf_;
                    if (!isNew) {
                        // Rename: update all triggerVarActions and triggerConditions
                        for (auto& va : csLib_.triggerVarActions)
                            if (va.key == oldName) va.key = newName;
                        for (auto& tc : csLib_.triggerConditions)
                            if (tc.varName == oldName) tc.varName = newName;
                        auto node = csLib_.varDefaults.extract(oldName);
                        if (!node.empty()) { node.key() = newName; csLib_.varDefaults.insert(std::move(node)); }
                    } else {
                        // New variable: add to varDefaults with default value 0
                        csLib_.varDefaults[newName] = 0;
                    }
                }
                varListEditingName_ = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            }
            return;
        }
    }

    // Var list value editing
    if (varListEditingValue_ && showVarList_) {
        if (e.type == SDL_TEXTINPUT) {
            for (const char* p = e.text.text; *p; ++p) {
                if ((*p >= '0' && *p <= '9') ||
                    (*p == '-' && varListValueBuf_.empty()))
                    varListValueBuf_ += *p;
            }
            return;
        } else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_BACKSPACE && !varListValueBuf_.empty()) {
                varListValueBuf_.pop_back();
            } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE) {
                if (e.key.keysym.sym == SDLK_RETURN) {
                    // Find the var name at varListSelected_
                    std::vector<std::string> vnames2;
                    for (const auto& va : csLib_.triggerVarActions) {
                        bool found = false;
                        for (const auto& n : vnames2) if (n == va.key) { found = true; break; }
                        if (!found && !va.key.empty()) vnames2.push_back(va.key);
                    }
                    for (const auto& tc : csLib_.triggerConditions) {
                        bool found = false;
                        for (const auto& n : vnames2) if (n == tc.varName) { found = true; break; }
                        if (!found && !tc.varName.empty()) vnames2.push_back(tc.varName);
                    }
                    for (const auto& kv : csLib_.varDefaults) {
                        bool found = false;
                        for (const auto& n : vnames2) if (n == kv.first) { found = true; break; }
                        if (!found) vnames2.push_back(kv.first);
                    }
                    std::sort(vnames2.begin(), vnames2.end());
                    if (varListSelected_ >= 0 && varListSelected_ < (int)vnames2.size()) {
                        pushUndo();
                        csLib_.varDefaults[vnames2[varListSelected_]] = atoi(varListValueBuf_.c_str());
                    }
                }
                varListEditingValue_ = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            }
            return;
        }
    }

    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        switch (e.key.keysym.sym) {
            case SDLK_1: currentTool_ = EditorTool::Tile; break;
            case SDLK_2: currentTool_ = EditorTool::Trigger; break;
            case SDLK_3: currentTool_ = EditorTool::Entity; break;
            case SDLK_4: currentTool_ = EditorTool::Erase; break;
            case SDLK_5: currentTool_ = EditorTool::Select; break;
            case SDLK_6: currentTool_ = EditorTool::Rect; break;
            case SDLK_7: currentTool_ = EditorTool::Fill; break;
            case SDLK_g: showGrid_ = !showGrid_; break;
            case SDLK_v: showVarList_ = !showVarList_; break;
            case SDLK_TAB: showUI_ = !showUI_; break;
            case SDLK_F1: showHelp_ = true; break;

            case SDLK_F5: wantsTestPlay_ = true; break;  // Test play

            case SDLK_i: {
                // Eyedropper: pick the hovered tile into the palette
                float wx = screenToWorldX(mouseX_);
                float wy = screenToWorldY(mouseY_);
                pickTileAt((int)(wx / TILE_SIZE), (int)(wy / TILE_SIZE), wx, wy);
                break;
            }
            case SDLK_ESCAPE:
        // If an item is selected or a drag is in progress, cancel/deselect it
        if (selectedTrigger_ >= 0 || selectedEnemy_ >= 0 || trigDragging_ || rectStartTX_ >= 0) {
            selectedTrigger_ = -1;
            selectedEnemy_   = -1;
            trigDragging_    = false;
            rectStartTX_     = -1; 
            rectStartTY_     = -1;
        } 
        // Otherwise, exit the editor
        else {
            wantsBack_ = true;
        }
        break;
            case SDLK_MINUS:
            case SDLK_EQUALS: {
                // Zoom around the view center
                float oldZoom = zoom_;
                zoom_ *= (e.key.keysym.sym == SDLK_EQUALS) ? 1.25f : (1.0f / 1.25f);
                zoom_ = fmaxf(ZOOM_MIN, fminf(ZOOM_MAX, zoom_));
                int cx = (screenW_ - uiPaletteW()) / 2;
                int cy = uiToolbarH() + (screenH_ - uiToolbarH() - csEditorBottom()) / 2;
                float wx = (float)cx / oldZoom + camera_.pos.x;
                float wy = (float)(cy - uiToolbarH()) / oldZoom + camera_.pos.y;
                camera_.pos.x = wx - (float)cx / zoom_;
                camera_.pos.y = wy - (float)(cy - uiToolbarH()) / zoom_;
                break;
            }
            case SDLK_0: {
                // Fit the whole map in the viewport
                float viewW = (float)(screenW_ - uiPaletteW());
                float viewH = (float)(screenH_ - uiToolbarH() - csEditorBottom());
                float worldW = (float)(map_.width  * TILE_SIZE);
                float worldH = (float)(map_.height * TILE_SIZE);
                if (worldW > 1 && worldH > 1) {
                    zoom_ = fmaxf(ZOOM_MIN, fminf(ZOOM_MAX,
                            fminf(viewW / worldW, viewH / worldH) * 0.95f));
                    camera_.pos.x = (worldW - viewW / zoom_) * 0.5f;
                    camera_.pos.y = (worldH - viewH / zoom_) * 0.5f;
                }
                break;
            }

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
                        TriggerType::Cutscene,
                        TriggerType::Waypoint,
                        TriggerType::SignalZone,
                        TriggerType::Objective,
                    };
                    static const int kTypeCount = 14;
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
    if (showCutsceneEditor_) {
        csEditor_.update(dt);
        if (csEditor_.wantsClose()) {
            csEditor_.clearWantsClose();
            showCutsceneEditor_ = false;
            csEditor_.setActive(false);
        }
    }

    // Update gamepad virtual cursor
    updateGamepadCursor(dt);

    // Camera pan with arrow keys or Shift+WASD (not while typing in a field)
    bool csTyping = showCutsceneEditor_ && csEditor_.textEditing();
    if (!csTyping) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float panSpeed = 500.0f / zoom_ * dt;
        if (keys[SDL_SCANCODE_UP]    || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_W])) camera_.pos.y -= panSpeed;
        if (keys[SDL_SCANCODE_DOWN]  || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_S])) camera_.pos.y += panSpeed;
        if (keys[SDL_SCANCODE_LEFT]  || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_A])) camera_.pos.x -= panSpeed;
        if (keys[SDL_SCANCODE_RIGHT] || (keys[SDL_SCANCODE_LSHIFT] && keys[SDL_SCANCODE_D])) camera_.pos.x += panSpeed;
    }

    // Reset undo stroke flag when no buttons held
    if (!mouseDown_ && !rightDown_) undoPushedForStroke_ = false;

    // Paint/erase on mouse hold (only in map area, not over any UI panel)
    if (!showHelp_ && !draggingResize_ && !draggingMove_ && !isOverUI(mouseX_, mouseY_) &&
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
                    erasePropAt(wx, wy);
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
                case EditorTool::Fill:
                    pushUndo();
                    floodFill(tx, ty);
                    mouseDown_ = false;
                    break;
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

    // While the full-screen dialog editor is open, hide the editor's own
    // panels so they cannot draw over or steal clicks from the modal.
    const bool dlgModal = showCutsceneEditor_ && csEditor_.dialogModalOpen();

    if (showUI_ && !dlgModal) {
        renderToolbar(renderer);
        renderPalette(renderer);
    }

    // Properties / context panel
    if (showUI_ && !dlgModal)
        renderPropertiesPanel(renderer);

    // Map properties floating panel
    if (!dlgModal)
        renderMapPropsPanel(renderer);

    // Variable list floating panel
    if (showVarList_ && !dlgModal)
        renderVarListPanel(renderer);

    // Status bar (hidden while the cutscene panel covers the bottom strip)
    if (!showCutsceneEditor_) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 12, 14, 24, 200);
        SDL_Rect statusBg = {0, screenH_ - 28, screenW_ - uiPaletteW(), 28};
        SDL_RenderFillRect(renderer, &statusBg);
        SDL_SetRenderDrawColor(renderer, 0, 120, 110, 40);
        SDL_RenderDrawLine(renderer, 0, screenH_ - 28, screenW_ - uiPaletteW(), screenH_ - 28);

        // Left: map name + dirty marker, tool, contextual hint
        static const char* toolNames[(int)EditorTool::TOOL_COUNT] =
            {"Tile", "Trigger", "Entity", "Erase", "Select", "Rect", "Fill"};
        static const char* toolHints[(int)EditorTool::TOOL_COUNT] = {
            "LMB paint  RMB erase  [ ] brush  Alt+click pick",
            "Drag to place  T cycle type",
            "Click to place  E cycle type",
            "LMB/RMB erase tiles, triggers, entities, props",
            "Click select  drag ball to move  Del delete  Q/E rotate zone",
            "Drag rectangle  F filled/outline",
            "Click to flood-fill connected tiles",
        };
        char left[256];
        snprintf(left, sizeof(left), "%s%s  |  %s: %s",
                 map_.name.c_str(), dirty_ ? " *" : "",
                 toolNames[(int)currentTool_], toolHints[(int)currentTool_]);
        drawEditorText(renderer, left, 8, screenH_ - 22, 12, {130, 132, 145, 255});

        // Right: cursor tile, counts, zoom, help reminder
        {
            float wx = screenToWorldX(mouseX_), wy = screenToWorldY(mouseY_);
            char right[160];
            snprintf(right, sizeof(right), "T:%d E:%d  (%d,%d)  %.0f%%  F1 help",
                     (int)map_.triggers.size(), (int)map_.enemySpawns.size(),
                     (int)(wx / TILE_SIZE), (int)(wy / TILE_SIZE), zoom_ * 100);
            int tw = ui_ ? ui_->textWidth(right, 12) : 0;
            drawEditorText(renderer, right, screenW_ - uiPaletteW() - tw - 10,
                           screenH_ - 22, 12, {100, 102, 115, 255});
        }

        // Save message
        if (saveMessageTimer_ > 0) {
            Uint8 alpha = (saveMessageTimer_ < 0.5f) ? (Uint8)(saveMessageTimer_ * 510) : 255;
            bool isError = saveMessage_.find("failed") != std::string::npos;
            SDL_Color msgC = isError ? SDL_Color{255, 80, 80, alpha} : SDL_Color{50, 255, 100, alpha};
            int tw = ui_ ? ui_->textWidth(saveMessage_.c_str(), 13) : 0;
            drawEditorText(renderer, saveMessage_.c_str(),
                           (screenW_ - uiPaletteW() - tw) / 2, screenH_ - 56, 13, msgC);
        }
    } else if (saveMessageTimer_ > 0) {
        Uint8 alpha = (saveMessageTimer_ < 0.5f) ? (Uint8)(saveMessageTimer_ * 510) : 255;
        bool isError = saveMessage_.find("failed") != std::string::npos;
        SDL_Color msgC = isError ? SDL_Color{255, 80, 80, alpha} : SDL_Color{50, 255, 100, alpha};
        drawEditorText(renderer, saveMessage_.c_str(), 8, uiToolbarH() + 6, 13, msgC);
    }

    // Cutscene editor panel (bottom strip); actor overlays go under the panel
    if (showCutsceneEditor_) {
        renderCutsceneActorOverlays(renderer);
        csEditor_.render(renderer, screenW_, screenH_, screenH_ - csEditor_.panelHeight());
    }

    // Help overlay (F1)
    if (showHelp_) renderHelpOverlay(renderer);

    // Gamepad / touch cursor
    renderCursor(renderer);
}

void MapEditor::renderHelpOverlay(SDL_Renderer* renderer) {
    if (!ui_) return;
    ui_->drawDarkOverlay(210);

    const int winW = 720, winH = 480;
    const int winX = (screenW_ - winW) / 2;
    const int winY = (screenH_ - winH) / 2;
    ui_->drawWin98Window(winX, winY, winW, winH, "Editor Help (press any key to close)");

    struct Entry { const char* key; const char* desc; };
    static const Entry colA[] = {
        {"1-7",        "Select tool"},
        {"LMB",        "Paint / place / select"},
        {"RMB",        "Erase under cursor"},
        {"MMB drag",   "Pan camera"},
        {"Wheel",      "Zoom (palette: scroll)"},
        {"Arrows",     "Pan camera"},
        {"- / =",      "Zoom out / in"},
        {"0",          "Fit map to view"},
        {"[ / ]",      "Brush size down / up"},
        {"G",          "Toggle grid"},
        {"Tab",        "Toggle UI panels"},
    };
    static const Entry colB[] = {
        {"Alt+click",  "Eyedropper: pick hovered tile"},
        {"I",          "Eyedropper at cursor"},
        {"T",          "Cycle trigger type (Trigger tool)"},
        {"C",          "Cycle goal condition (Level End)"},
        {"E",          "Cycle entity type / rotate zone CW"},
        {"Q",          "Rotate collision zone CCW"},
        {"F",          "Rect tool: filled / outline"},
        {"Del",        "Delete selected trigger/entity"},
        {"Ctrl+Z", "Undo"},
        {"Ctrl+S",     "Save map"},
        {"F5",         "Test play"},
    };
    static const Entry colC[] = {
        {"Scene btn",  "Open the cutscene editor panel"},
        {"Space",      "Cutscenes: play / pause"},
        {"Shift",      "Cutscenes: toggle time snapping"},
        {"Ctrl+C/V",   "Cutscenes: copy / paste event"},
        {"R-click",    "Cutscenes: delete event block"},
        {"Drag edge",  "Cutscenes: resize panel (top border)"},
    };

    auto drawCol = [&](const Entry* es, int n, int x, int y) {
        for (int i = 0; i < n; i++) {
            ui_->drawText(es[i].key, x, y, 12, UI::W98::Navy);
            ui_->drawText(es[i].desc, x + 80, y, 12, UI::W98::Black);
            y += 20;
        }
        return y;
    };

    int cx = winX + 18;
    int cy = winY + UI::W98::TitleH + 14;
    ui_->drawText("MAP EDITOR", cx, cy, 13, UI::W98::Navy);
    int endA = drawCol(colA, (int)(sizeof(colA) / sizeof(colA[0])), cx, cy + 22);

    int cx2 = winX + winW / 2 + 10;
    ui_->drawText("EDITING", cx2, cy, 13, UI::W98::Navy);
    drawCol(colB, (int)(sizeof(colB) / sizeof(colB[0])), cx2, cy + 22);

    ui_->drawText("CUTSCENES", cx, endA + 14, 13, UI::W98::Navy);
    drawCol(colC, (int)(sizeof(colC) / sizeof(colC[0])), cx, endA + 36);
}

void MapEditor::renderCutsceneActorOverlays(SDL_Renderer* renderer) {
    const Cutscene* cs = csEditor_.currentCutscene();
    if (!cs) {
        if (csEditor_.pickArmed())
            drawEditorText(renderer, csEditor_.pickHint(), 12, uiToolbarH() + 8, 13,
                           {255, 220, 60, 255});
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Move paths of the selected actor's events (and camera moves when the
    // selected event is one), with the selected event emphasized.
    int selEvent = csEditor_.selectedEvent();
    int selActor = csEditor_.selectedActor();
    for (int ei = 0; ei < (int)cs->events.size(); ei++) {
        const CsEvent& ev = cs->events[ei];
        if (ev.type != CsEventType::Move && ev.type != CsEventType::CameraMove) continue;
        bool isSel = (ei == selEvent);
        bool ownedBySel = false;
        if (selActor >= 0 && selActor < (int)cs->actors.size())
            ownedBySel = (ev.actorId == cs->actors[selActor].id);
        if (!isSel && !ownedBySel) continue;

        int x0 = worldToScreenX(ev.fromX), y0 = worldToScreenY(ev.fromY);
        int x1 = worldToScreenX(ev.toX),   y1 = worldToScreenY(ev.toY);
        SDL_Color pc = (ev.type == CsEventType::CameraMove)
                     ? SDL_Color{255, 160, 200, 255} : SDL_Color{90, 170, 255, 255};
        Uint8 alpha = isSel ? 255 : 110;
        SDL_SetRenderDrawColor(renderer, pc.r, pc.g, pc.b, alpha);
        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
        // Endpoint squares: hollow = from, filled = to
        SDL_Rect fromR = {x0 - 4, y0 - 4, 8, 8};
        SDL_Rect toR   = {x1 - 4, y1 - 4, 8, 8};
        SDL_RenderDrawRect(renderer, &fromR);
        SDL_RenderFillRect(renderer, &toR);
        if (isSel) {
            drawEditorText(renderer, "from", x0 + 6, y0 - 6, 10, {pc.r, pc.g, pc.b, 255});
            drawEditorText(renderer, "to",   x1 + 6, y1 - 6, 10, {pc.r, pc.g, pc.b, 255});
        }
    }

    // Actor markers - draw the real sprite at its previewed pose
    for (int i = 0; i < csEditor_.actorCount(); i++) {
        const CsActorState* s = csEditor_.actorStateAt(i);
        if (!s) continue;
        const CsActor& a = cs->actors[i];
        bool sel = (i == selActor);

        int sx = worldToScreenX(s->x);
        int sy = worldToScreenY(s->y);

        SDL_Color col;
        switch (a.type) {
            case CsActorType::Player: col = {100, 200, 255, 255}; break;
            case CsActorType::Enemy:  col = {255, 120,  80, 255}; break;
            default:                  col = {180, 255, 180, 255}; break;
        }

        SDL_Texture* tex = csActorTexture(a);
        int boundR;  // screen radius used for label / selection ring placement
        if (tex) {
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            if (tw <= 0) tw = 32;
            if (th <= 0) th = 32;
            int dw = std::max(4, (int)(tw * zoom_ * s->scaleX));
            int dh = std::max(4, (int)(th * zoom_ * s->scaleY));
            SDL_Rect dst = {sx - dw / 2, sy - dh / 2, dw, dh};
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, (Uint8)(s->alpha * (s->visible ? 255 : 90)));
            SDL_RenderCopyEx(renderer, tex, nullptr, &dst, (double)s->rot, nullptr,
                             a.flipH ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
            SDL_SetTextureAlphaMod(tex, 255);
            boundR = std::max(dw, dh) / 2;
        } else {
            // Fallback diamond if the sprite is missing (e.g. empty FreeSprite path)
            int r = std::max(6, (int)(10 * zoom_));
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, s->visible ? 230 : 90);
            for (int dy = -r; dy <= r; dy++) {
                int half = r - abs(dy);
                SDL_RenderDrawLine(renderer, sx - half, sy + dy, sx + half, sy + dy);
            }
            boundR = r;
        }

        // Selection box
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
            SDL_Rect ring = {sx - boundR - 3, sy - boundR - 3, (boundR + 3) * 2, (boundR + 3) * 2};
            SDL_RenderDrawRect(renderer, &ring);
        }

        drawEditorText(renderer, a.name.c_str(), sx + boundR + 4, sy - 8, 11,
                       {col.r, col.g, col.b, 255});
        if (!s->visible)
            drawEditorText(renderer, "(hidden)", sx + boundR + 4, sy + 4, 9, {140, 140, 150, 255});
    }

    // Rotation handle for the selected actor / Rotate event
    {
        float rcx, rcy, a0, a1; bool two;
        if (csEditor_.rotationHandle(rcx, rcy, a0, a1, two)) {
            const float D2R = 3.14159265f / 180.0f;
            int scx = worldToScreenX(rcx), scy = worldToScreenY(rcy);
            int R = std::max(16, (int)(CS_ROT_R * zoom_));
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            // Ring
            SDL_SetRenderDrawColor(renderer, 255, 220, 90, 110);
            for (int aa = 0; aa < 360; aa += 12) {
                float r0 = aa * D2R, r1 = (aa + 12) * D2R;
                SDL_RenderDrawLine(renderer, scx + (int)(R * cosf(r0)), scy + (int)(R * sinf(r0)),
                                             scx + (int)(R * cosf(r1)), scy + (int)(R * sinf(r1)));
            }
            auto knob = [&](float deg, bool filled, SDL_Color c) {
                int kx = scx + (int)(R * cosf(deg * D2R));
                int ky = scy + (int)(R * sinf(deg * D2R));
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 220);
                SDL_RenderDrawLine(renderer, scx, scy, kx, ky);
                SDL_Rect k = {kx - 5, ky - 5, 10, 10};
                if (filled) SDL_RenderFillRect(renderer, &k);
                else        SDL_RenderDrawRect(renderer, &k);
            };
            if (two) {
                knob(a0, false, {180, 180, 190, 255});      // from (hollow)
                knob(a1, true,  {255, 200, 60, 255});       // to (filled)
            } else {
                knob(a0, true,  {255, 200, 60, 255});
            }
        }
    }

    // Scale gizmo for the selected Scale event (from = hollow, to = filled)
    {
        float scxw, scyw, fsx, fsy, tsx, tsy;
        if (csEditor_.scaleHandle(scxw, scyw, fsx, fsy, tsx, tsy)) {
            int scx = worldToScreenX(scxw), scy = worldToScreenY(scyw);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            auto box = [&](float sxv, float syv, SDL_Color c, bool filledKnobs) {
                int hw = std::max(6, (int)(CS_SCALE_REF * sxv * zoom_));
                int hh = std::max(6, (int)(CS_SCALE_REF * syv * zoom_));
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 200);
                SDL_Rect rb = {scx - hw, scy - hh, hw * 2, hh * 2};
                SDL_RenderDrawRect(renderer, &rb);
                int corx[4] = {scx - hw, scx + hw, scx - hw, scx + hw};
                int cory[4] = {scy - hh, scy - hh, scy + hh, scy + hh};
                for (int k = 0; k < 4; k++) {
                    SDL_Rect kr = {corx[k] - 4, cory[k] - 4, 8, 8};
                    if (filledKnobs) SDL_RenderFillRect(renderer, &kr);
                    else             SDL_RenderDrawRect(renderer, &kr);
                }
            };
            box(fsx, fsy, {170, 170, 185, 255}, false);   // from
            box(tsx, tsy, {120, 230, 150, 255}, true);    // to
            int hh = std::max(6, (int)(CS_SCALE_REF * tsy * zoom_));
            drawEditorText(renderer, "drag corner to scale", scx + 6, scy - hh - 16, 10,
                           {120, 230, 150, 255});
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Pick-mode banner
    if (csEditor_.pickArmed())
        drawEditorText(renderer, csEditor_.pickHint(), 12, uiToolbarH() + 8, 13,
                       {255, 220, 60, 255});
}

SDL_Texture* MapEditor::csActorTexture(const CsActor& a) const {
    auto& A = Assets::instance();
    switch (a.type) {
        case CsActorType::Player:
            return A.loadRelTex("sprites/player/body-0001.png");
        case CsActorType::Enemy: {
            const char* p = "sprites/enemy/melee.png";
            switch (a.enemyType) {
                case CsEnemyType::Melee:   p = "sprites/enemy/melee.png";      break;
                case CsEnemyType::Shooter: p = "sprites/enemy/shooter.png";    break;
                case CsEnemyType::Brute:   p = "sprites/enemy/boss_brute.png"; break;
                case CsEnemyType::Scout:   p = "sprites/enemy/scout.png";      break;
                case CsEnemyType::Sniper:  p = "sprites/enemy/sniper.png";     break;
                case CsEnemyType::Gunner:  p = "sprites/enemy/gunner.png";     break;
            }
            return A.loadRelTex(p);
        }
        case CsActorType::FreeSprite:
            return a.spritePath.empty() ? nullptr : A.loadRelTex(a.spritePath);
    }
    return nullptr;
}

void MapEditor::renderGrid(SDL_Renderer* renderer) {
    // Always enable alpha blending - the grid is meant to be a faint overlay,
    // and leaving it to the renderer's leftover blend state made it render
    // opaque after some transitions.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    // Brighter and slightly more opaque than before so it reads over light
    // tiles, but still clearly a guide rather than a solid line.
    SDL_SetRenderDrawColor(renderer, 150, 160, 175, 70);
    float ts = TILE_SIZE * zoom_;
    int startX = (int)(camera_.pos.x / TILE_SIZE);
    int startY = (int)(camera_.pos.y / TILE_SIZE);

    const int top = uiToolbarH();
    const int bottom = screenH_ - csEditorBottom();
    const int right  = screenW_ - uiPaletteW();
    for (int x = startX; x <= startX + (int)((screenW_ - uiPaletteW()) / ts) + 1; x++) {
        int sx = worldToScreenX((float)(x * TILE_SIZE));
        SDL_RenderDrawLine(renderer, sx, top, sx, bottom);
    }
    for (int y = startY; y <= startY + (int)((screenH_ - uiToolbarH()) / ts) + 1; y++) {
        int sy = worldToScreenY((float)(y * TILE_SIZE));
        if (sy < top || sy > bottom) continue;
        SDL_RenderDrawLine(renderer, 0, sy, right, sy);
    }
}

void MapEditor::renderTriggers(SDL_Renderer* renderer) {
    // Trigger fills are intentionally faint (low alpha); force blending so
    // they don't render as solid blocks when the renderer state is left NONE.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
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
            case TriggerType::Cutscene: {
                SDL_SetRenderDrawColor(renderer, 255, 120, 200, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 255, 120, 200, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) {
                    char lbl[24]; snprintf(lbl, sizeof(lbl), "CUTSCENE %d", (int)t.param);
                    drawEditorText(renderer, lbl, r.x + 4, r.y + 4, labelSz, {255, 150, 210, 255});
                }
                break;
            }
            case TriggerType::Waypoint: {
                SDL_SetRenderDrawColor(renderer, 60, 220, 220, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 60, 220, 220, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) {
                    const char* lbl = (t.param == 2) ? "WAYPOINT B" : "WAYPOINT A";
                    drawEditorText(renderer, lbl, r.x + 4, r.y + 4, labelSz, {90, 240, 240, 255});
                }
                break;
            }
            case TriggerType::SignalZone: {
                int d = (int)(int8_t)t.param;
                SDL_Color c = (d >= 0) ? SDL_Color{80, 255, 120, 0} : SDL_Color{255, 90, 90, 0};
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) {
                    char lbl[24]; snprintf(lbl, sizeof(lbl), "SIGNAL %+d", d);
                    drawEditorText(renderer, lbl, r.x + 4, r.y + 4, labelSz, {c.r, c.g, c.b, 255});
                }
                break;
            }
            case TriggerType::Objective: {
                SDL_SetRenderDrawColor(renderer, 230, 200, 90, 35);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 230, 200, 90, selected ? 255 : 180);
                SDL_RenderDrawRect(renderer, &r);
                if (labelSz >= 8) {
                    const char* k = (t.param == 1) ? "OBJ:PROTECT"
                                  : (t.param == 2) ? "OBJ:ESCORT" : "OBJ:RECOVER";
                    drawEditorText(renderer, k, r.x + 4, r.y + 4, labelSz, {245, 215, 110, 255});
                }
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
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
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
            case ENTITY_CIVILIAN:      SDL_SetRenderDrawColor(renderer, 120, 220, 255, 220); break;
            case ENTITY_RESPONDER:     SDL_SetRenderDrawColor(renderer, 255, 120, 60, 220); break;
            case ENTITY_INFRA_MEDRELAY:SDL_SetRenderDrawColor(renderer, 120, 255, 160, 220); break;
            case ENTITY_INFRA_POWER:   SDL_SetRenderDrawColor(renderer, 255, 230, 80, 220); break;
            case ENTITY_INFRA_WATER:   SDL_SetRenderDrawColor(renderer, 90, 180, 255, 220); break;
            case ENTITY_INFRA_ANTENNA: SDL_SetRenderDrawColor(renderer, 200, 160, 255, 220); break;
            default:                   SDL_SetRenderDrawColor(renderer, 180, 180, 180, 200); break;
        }
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, selected ? 0 : 255, 255, selected ? 0 : 255, 255);
        SDL_RenderDrawRect(renderer, &r);

        const char* labels[ENTITY_TYPE_COUNT] = {
            "M", "S", "C", "U", "B", "F", "N", "G",
            "Civ", "Rsp", "Med", "Pwr", "Wtr", "Ant"
        };
        const char* label = (es.enemyType < ENTITY_TYPE_COUNT) ? labels[es.enemyType] : "?";
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
    leftPanelH_ = 0;
    if (!ui_) return;

    // Cancel trigger text editing if the trigger is deselected
    if (selectedTrigger_ < 0) {
        if (trigCondEditingName_) {
            trigCondEditingName_ = false;
#ifndef __SWITCH__
            SDL_StopTextInput();
#endif
        }
        if (trigMultiCooldownEditing_) {
            trigMultiCooldownEditing_ = false;
#ifndef __SWITCH__
            SDL_StopTextInput();
#endif
        }
    }

    const int panelW = 220;
    const int panelX = 8;
    const int panelY = TOOLBAR_H + 8;
    // Don't draw under the cutscene panel when it is open and tall
    const int maxPanelH = screenH_ - csEditorBottom() - panelY - 8;
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
        bool isResp = (es.enemyType == ENTITY_RESPONDER);
        // TitleH(22) + top_pad(8) + rows + gap(8) + delete(26) + bot_pad(8)
        const int panelH = UI::W98::TitleH + 8 + (3 + (isResp ? 1 : 0)) * rowH + 8 + 26 + 8;
        if (panelH > maxPanelH) return;
        leftPanelH_ = panelH;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Entity");
        int y = panelY + UI::W98::TitleH + 8;

        // Type
        static const char* eNames[ENTITY_TYPE_COUNT] = {
            "Melee","Shooter","Crate","Upgrade","Brute","Scout","Sniper","Gunner",
            "Civilian","Responder","Med-Relay","Power","Water","Antenna"
        };
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

        // Disable toggle (responders only): if on, the unit can be non-lethally
        // disabled instead of destroyed (SIGNAL credit). Stored in reserved[0].
        if (isResp) {
            ui_->drawText("Disable", lx, y + 5, 11, UI::W98::Black);
            bool on = (es.reserved[0] != 0);
            if (ui_->win98Button(204, on ? "Disableable: ON" : "Disableable: OFF",
                                 arLx, y, roFldW, btnSz, on)) {
                pushUndo();
                es.reserved[0] = on ? 0 : 1;
            }
            y += rowH;
        }

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
            "Cutscene","Waypoint","Signal","Objective",
            "SetVar","LoadMap",
        };
        static const TriggerType kTypes[] = {
            TriggerType::LevelStart, TriggerType::LevelEnd, TriggerType::Crate, TriggerType::Effect,
            TriggerType::TeamSpawnRed, TriggerType::TeamSpawnBlue, TriggerType::TeamSpawnGreen, TriggerType::TeamSpawnYellow,
            TriggerType::LayerFade, TriggerType::CollisionZone,
            TriggerType::Cutscene, TriggerType::Waypoint, TriggerType::SignalZone, TriggerType::Objective,
            TriggerType::SetVariable, TriggerType::LoadMap,
        };
        constexpr int kTypeCount2 = (int)(sizeof(kTypes) / sizeof(kTypes[0]));
        int typeIdx = 0;
        for (int j = 0; j < kTypeCount2; j++) { if (kTypes[j] == t.type) { typeIdx = j; break; } }
        bool hasCondRow  = (t.type == TriggerType::LevelEnd);
        bool hasParamRow = (t.type == TriggerType::Cutscene ||
                            t.type == TriggerType::Waypoint ||
                            t.type == TriggerType::SignalZone ||
                            t.type == TriggerType::Objective ||
                            t.type == TriggerType::SetVariable);

        // Find variable condition for this trigger (may be null)
        TriggerCondition* trigCond = nullptr;
        for (auto& tc : csLib_.triggerConditions)
            if (tc.triggerIndex == selectedTrigger_) { trigCond = &tc; break; }
        bool hasVarCond = (trigCond != nullptr);

        // Find multi config for this trigger (may be null)
        TriggerMultiConfig* trigMulti = nullptr;
        for (auto& mc : csLib_.triggerMultiConfigs)
            if (mc.triggerIndex == selectedTrigger_) { trigMulti = &mc; break; }
        bool hasMulti = (trigMulti != nullptr);

        // TitleH(22) + top_pad(8) + rows + gap(8) + delete(26) + bot_pad(8)
        int extraRows = (hasCondRow ? 1 : 0) + (hasParamRow ? 1 : 0)
                      + 1                         // Var cond toggle
                      + (hasVarCond ? 3 : 0)      // name + cmp + value when enabled
                      + 1                         // Multi toggle
                      + (hasMulti ? 1 : 0);       // cooldown when enabled
        const int panelH = UI::W98::TitleH + 8 + (3 + extraRows) * rowH + 8 + 26 + 8;
        if (panelH > maxPanelH) return;
        leftPanelH_ = panelH;
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
            static const char* condNames[] = {"Open", "Kill All", "Trigger", "Flag"};
            int condIdx = (int)t.condition;
            if (condIdx < 0 || condIdx > 3) condIdx = 0;
            ui_->drawText("Goal", lx, y + 5, 11, UI::W98::Black);
            if (ui_->win98Button(222, "<", arLx, y, btnSz, btnSz, false)) {
                pushUndo();
                condIdx = (condIdx + 3) % 4;
                t.condition = (GoalCondition)condIdx;
            }
            ui_->drawWin98TextField(fldx, y, fieldW, btnSz, condNames[condIdx], false);
            if (ui_->win98Button(223, ">", arRx, y, btnSz, btnSz, false)) {
                pushUndo();
                condIdx = (condIdx + 1) % 4;
                t.condition = (GoalCondition)condIdx;
            }
            y += rowH;
        }

        // Param (story triggers): cutscene index / route / signal delta / objective kind
        if (hasParamRow) {
            const char* label = "Param";
            char valBuf[24];
            int step = 1, lo = 0, hi = 255;
            if (t.type == TriggerType::Cutscene) {
                label = "CS idx"; lo = 0; hi = 63;
                if ((int)t.param < (int)csLib_.cutscenes.size())
                    snprintf(valBuf, sizeof(valBuf), "%d %s", (int)t.param,
                             csLib_.cutscenes[t.param].id.c_str());
                else
                    snprintf(valBuf, sizeof(valBuf), "%d (none)", (int)t.param);
            } else if (t.type == TriggerType::Waypoint) {
                label = "Route"; lo = 1; hi = 2;
                snprintf(valBuf, sizeof(valBuf), "%s", t.param == 2 ? "B (Signal)" : "A (Spear)");
            } else if (t.type == TriggerType::SignalZone) {
                label = "Delta"; step = 5;
                snprintf(valBuf, sizeof(valBuf), "%+d", (int)(int8_t)t.param);
            } else { // Objective
                label = "Kind";
                snprintf(valBuf, sizeof(valBuf), "%s",
                         t.param == 1 ? "Protect" : t.param == 2 ? "Escort" : "Recover");
            }
            ui_->drawText(label, lx, y + 5, 11, UI::W98::Black);
            if (ui_->win98Button(224, "<", arLx, y, btnSz, btnSz, false)) {
                pushUndo();
                if (t.type == TriggerType::SignalZone) {
                    int v = (int)(int8_t)t.param - step;
                    if (v < -100) v = -100;
                    t.param = (uint8_t)(int8_t)v;
                } else if (t.type == TriggerType::Objective) {
                    t.param = (uint8_t)((t.param + 2) % 3);
                } else {
                    int v = (int)t.param - step; if (v < lo) v = lo;
                    t.param = (uint8_t)v;
                }
            }
            ui_->drawWin98TextField(fldx, y, fieldW, btnSz, valBuf, false);
            if (ui_->win98Button(225, ">", arRx, y, btnSz, btnSz, false)) {
                pushUndo();
                if (t.type == TriggerType::SignalZone) {
                    int v = (int)(int8_t)t.param + step;
                    if (v > 100) v = 100;
                    t.param = (uint8_t)(int8_t)v;
                } else if (t.type == TriggerType::Objective) {
                    t.param = (uint8_t)((t.param + 1) % 3);
                } else {
                    int v = (int)t.param + step; if (v > hi) v = hi;
                    t.param = (uint8_t)v;
                }
            }
            y += rowH;
        }

        // Var condition toggle + fields
        {
            ui_->drawText("Var cond", lx, y + 5, 11, UI::W98::Black);
            if (ui_->win98Button(250, hasVarCond ? "ON" : "OFF", arLx, y, roFldW, btnSz, hasVarCond)) {
                pushUndo();
                if (hasVarCond) {
                    // Remove condition
                    for (auto it = csLib_.triggerConditions.begin(); it != csLib_.triggerConditions.end(); ++it) {
                        if (it->triggerIndex == selectedTrigger_) {
                            csLib_.triggerConditions.erase(it); break;
                        }
                    }
                    trigCond = nullptr; hasVarCond = false;
                    trigCondEditingName_ = false;
                } else {
                    // Add condition with defaults
                    TriggerCondition nc;
                    nc.triggerIndex = selectedTrigger_;
                    nc.varName = "var";
                    nc.value = 0; nc.cmp = 0;
                    csLib_.triggerConditions.push_back(nc);
                    trigCond = &csLib_.triggerConditions.back();
                    hasVarCond = true;
                }
            }
            y += rowH;

            if (hasVarCond && trigCond) {
                // Var name (inline editable)
                ui_->drawText("Var", lx, y + 5, 11, UI::W98::Black);
                bool editingThis = trigCondEditingName_;
                const char* nameDisp = editingThis ? trigCondNameBuf_.c_str() : trigCond->varName.c_str();
                float blink = editingThis ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
                ui_->drawWin98TextField(arLx, y, roFldW, btnSz, nameDisp, editingThis, false, blink);
                if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, arLx, y, roFldW, btnSz)) {
                    ui_->mouseClicked = false;
                    ui_->clickCooldownFrames = 2;
                    trigCondEditingName_ = true;
                    trigCondNameBuf_ = trigCond->varName;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                }
                y += rowH;

                // Cmp operator
                static const char* cmpNames[] = {"==","!=",">","<",">=","<="};
                ui_->drawText("Cmp", lx, y + 5, 11, UI::W98::Black);
                if (ui_->win98Button(251, "<", arLx, y, btnSz, btnSz, false)) {
                    pushUndo();
                    trigCond->cmp = (uint8_t)((trigCond->cmp + 5) % 6);
                }
                ui_->drawWin98TextField(fldx, y, fieldW, btnSz, cmpNames[trigCond->cmp % 6], false);
                if (ui_->win98Button(252, ">", arRx, y, btnSz, btnSz, false)) {
                    pushUndo();
                    trigCond->cmp = (uint8_t)((trigCond->cmp + 1) % 6);
                }
                y += rowH;

                // Value
                ui_->drawText("Value", lx, y + 5, 11, UI::W98::Black);
                if (ui_->win98Button(253, "-", arLx, y, btnSz, btnSz, false)) {
                    pushUndo();
                    trigCond->value--;
                }
                snprintf(buf, sizeof(buf), "%d", trigCond->value);
                ui_->drawWin98TextField(fldx, y, fieldW, btnSz, buf, false);
                if (ui_->win98Button(254, "+", arRx, y, btnSz, btnSz, false)) {
                    pushUndo();
                    trigCond->value++;
                }
                y += rowH;
            }
        }

        // Multi-fire toggle + cooldown field
        {
            ui_->drawText("Multi", lx, y + 5, 11, UI::W98::Black);
            if (ui_->win98Button(260, hasMulti ? "ON" : "OFF", arLx, y, roFldW, btnSz, hasMulti)) {
                pushUndo();
                if (hasMulti) {
                    for (auto it = csLib_.triggerMultiConfigs.begin(); it != csLib_.triggerMultiConfigs.end(); ++it) {
                        if (it->triggerIndex == selectedTrigger_) {
                            csLib_.triggerMultiConfigs.erase(it); break;
                        }
                    }
                    trigMulti = nullptr; hasMulti = false;
                    trigMultiCooldownEditing_ = false;
                } else {
                    TriggerMultiConfig mc;
                    mc.triggerIndex = selectedTrigger_;
                    mc.cooldown = 0.0f;
                    csLib_.triggerMultiConfigs.push_back(mc);
                    trigMulti = &csLib_.triggerMultiConfigs.back();
                    hasMulti = true;
                }
            }
            y += rowH;

            if (hasMulti && trigMulti) {
                // Cooldown (editable text field, seconds)
                ui_->drawText("Cooldown", lx, y + 5, 11, UI::W98::Black);
                bool editingCd = trigMultiCooldownEditing_;
                char cdDisp[32];
                if (editingCd) {
                    snprintf(cdDisp, sizeof(cdDisp), "%s", trigMultiCooldownBuf_.c_str());
                } else {
                    snprintf(cdDisp, sizeof(cdDisp), "%.2fs", trigMulti->cooldown);
                }
                float blinkCd = editingCd ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
                ui_->drawWin98TextField(arLx, y, roFldW, btnSz, cdDisp, editingCd, false, blinkCd);
                if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, arLx, y, roFldW, btnSz)) {
                    ui_->mouseClicked = false;
                    ui_->clickCooldownFrames = 2;
                    trigMultiCooldownEditing_ = true;
                    snprintf(cdDisp, sizeof(cdDisp), "%.2f", trigMulti->cooldown);
                    trigMultiCooldownBuf_ = cdDisp;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                }
                y += rowH;
            }
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
    else if (currentTool_ == EditorTool::Tile || currentTool_ == EditorTool::Erase ||
             currentTool_ == EditorTool::Fill) {
        // Brush size + current tile preview
        const int previewSz = 42;
        const int panelH = UI::W98::TitleH + 8 + rowH + 6 + 1 + 6 + previewSz + 8;
        if (panelH > maxPanelH) return;
        leftPanelH_ = panelH;
        const char* title = (currentTool_ == EditorTool::Fill) ? "Fill Settings" : "Brush Settings";
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, title);
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
            "Cutscene", "Waypoint", "Signal Zone", "Objective",
        };
        static const TriggerType kT[] = {
            TriggerType::LevelStart, TriggerType::LevelEnd, TriggerType::Crate, TriggerType::Effect,
            TriggerType::TeamSpawnRed, TriggerType::TeamSpawnBlue, TriggerType::TeamSpawnGreen, TriggerType::TeamSpawnYellow,
            TriggerType::LayerFade, TriggerType::CollisionZone,
            TriggerType::Cutscene, TriggerType::Waypoint, TriggerType::SignalZone, TriggerType::Objective,
        };
        const int kTrigCount = (int)(sizeof(kT) / sizeof(kT[0]));
        const int itemH = 22;
        const int panelH = UI::W98::TitleH + 8 + kTrigCount * itemH + 8;
        if (panelH > maxPanelH) return;
        leftPanelH_ = panelH;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Trigger Type");
        int y = panelY + UI::W98::TitleH + 8;
        const int btnW = panelW - pad * 2;
        for (int i = 0; i < kTrigCount; i++) {
            if (ui_->win98Button(240 + i, tNames[i], lx, y, btnW, itemH, triggerGhost_.type == kT[i]))
                triggerGhost_.type = kT[i];
            y += itemH;
        }
    }
    else if (currentTool_ == EditorTool::Entity) {
        static const char* eNames[] = {
            "Melee", "Shooter", "Crate", "Upgrade Crate",
            "Brute", "Scout", "Sniper", "Gunner",
            "Civilian", "Responder", "Med-Relay", "Power", "Water", "Antenna",
        };
        const int kEntCount = (int)(sizeof(eNames) / sizeof(eNames[0]));
        const int itemH = 20;
        const int panelH = UI::W98::TitleH + 8 + kEntCount * itemH + 8;
        if (panelH > maxPanelH) return;
        leftPanelH_ = panelH;
        ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Entity Type");
        int y = panelY + UI::W98::TitleH + 8;
        const int btnW = panelW - pad * 2;
        for (int i = 0; i < kEntCount; i++) {
            if (ui_->win98Button(250 + i, eNames[i], lx, y, btnW, itemH, entitySpawnType_ == (uint8_t)i))
                entitySpawnType_ = (uint8_t)i;
            y += itemH;
        }
    }
    else if (currentTool_ == EditorTool::Rect) {
        const int halfW = (panelW - pad * 2 - 4) / 2;
        const int panelH = UI::W98::TitleH + 8 + 26 + 8 + rowH + 8;
        if (panelH > maxPanelH) return;
        leftPanelH_ = panelH;
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
    mapPropsH_ = 0;
    if (!showMapProps_ || !ui_) return;

    auto& pc = map_.playerConfig;

    const int panelW  = 230;
    const int pad     = 8;
    const int btnSz   = 22;
    const int rowH    = 30;
    const int innerW  = panelW - pad * 2;  // 214

    // 8 rows (mode, abilities header, abilities, hp, bombs, speed, damage,
    // reset) + separator + two image pickers + close button
    const int panelH  = UI::W98::TitleH + pad + 8 * rowH + 6 + 68 + btnSz + pad;
    const int panelX  = screenW_ - uiPaletteW() - panelW - 8;
    const int panelY  = uiToolbarH() + 8;
    // Don't draw under the cutscene panel
    if (panelY + panelH > screenH_ - csEditorBottom()) return;
    mapPropsH_ = panelH;

    // Any button inside this panel changes the map (Reset/Auto/X included)
    bool firedBefore = ui_->buttonFired;

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

    if (ui_->buttonFired && !firedBefore) dirty_ = true;
}

void MapEditor::renderVarListPanel(SDL_Renderer* renderer) {
    if (!ui_) return;

    // Collect all variable names referenced in the current .csc
    std::vector<std::string> varNames;
    auto addName = [&](const std::string& n) {
        if (n.empty()) return;
        for (const auto& existing : varNames)
            if (existing == n) return;
        varNames.push_back(n);
    };
    for (const auto& va : csLib_.triggerVarActions)  addName(va.key);
    for (const auto& tc : csLib_.triggerConditions)   addName(tc.varName);
    // Also include anything already in varDefaults (user may have added manually)
    for (const auto& kv : csLib_.varDefaults)         addName(kv.first);
    // Sort alphabetically
    std::sort(varNames.begin(), varNames.end());

    const int panelW  = 256;
    const int pad     = 8;
    const int rowH    = 28;
    const int btnSz   = 22;
    const int maxRows = 10;
    // +1 for "add" button row, +1 for close button
    bool addingNew = (varListSelected_ == (int)varNames.size() && varListEditingName_);
    int visRows = std::min((int)varNames.size(), maxRows - 1) + 1;  // data rows + add row
    const int panelH  = UI::W98::TitleH + pad + 20 + visRows * rowH + pad + btnSz + pad;
    const int panelX  = screenW_ - uiPaletteW() - panelW - 8;
    int panelY        = uiToolbarH() + 8;
    // If map props panel is open and would overlap, push below it
    if (mapPropsH_ > 0) panelY += mapPropsH_ + 8;
    if (panelY + panelH > screenH_ - csEditorBottom() - 8) return;

    ui_->drawWin98Window(panelX, panelY, panelW, panelH, "Variables");

    int y   = panelY + UI::W98::TitleH + pad;
    int lx  = panelX + pad;
    // Inner usable width accounting for Win98 border (2px each side)
    int rvw = panelW - pad * 2 - 4;  // 236
    // Column layout inside rvw: name | value | scope | del
    // nameW + gap(3) + valW + gap(2) + scopeW + gap(2) + delW = 236
    const int delW   = btnSz;    // 22
    const int scopeW = 26;
    const int valW   = 54;
    const int nameW  = rvw - valW - scopeW - delW - 3 - 2 - 2;  // 127
    const int valX   = lx + nameW + 3;
    const int scopeX = valX + valW + 2;
    const int delX   = scopeX + scopeW + 2;

    char buf[64];

    // Header row
    ui_->drawText("Name", lx,     y + 6, 10, UI::Color::Gray);
    ui_->drawText("Default",valX, y + 6, 10, UI::Color::Gray);
    ui_->drawText("Scp",  scopeX, y + 6, 10, UI::Color::Gray);
    y += 20;

    for (int i = 0; i < (int)varNames.size() && i < maxRows - 1; i++) {
        const std::string& vn = varNames[i];
        bool sel = (varListSelected_ == i);

        // Determine scope from triggerVarActions (local=0, pack=1)
        uint8_t scope = 0;
        for (const auto& va : csLib_.triggerVarActions)
            if (va.key == vn) { scope = va.scope; break; }

        int defVal = 0;
        auto dit = csLib_.varDefaults.find(vn);
        if (dit != csLib_.varDefaults.end()) defVal = dit->second;

        // Name field
        bool editingName = (sel && varListEditingName_);
        const char* nameDisp = editingName ? varListNameBuf_.c_str() : vn.c_str();
        float blinkN = editingName ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
        ui_->drawWin98TextField(lx, y, nameW, btnSz, nameDisp, editingName, false, blinkN);
        if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, lx, y, nameW, btnSz)) {
            ui_->mouseClicked = false;
            ui_->clickCooldownFrames = 2;
            if (!editingName) {
                varListSelected_ = i;
                varListEditingName_ = true;
                varListEditingValue_ = false;
                varListNameBuf_ = vn;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
            }
        }

        // Value field
        bool editingVal = (sel && varListEditingValue_);
        if (editingVal) {
            snprintf(buf, sizeof(buf), "%s", varListValueBuf_.c_str());
        } else {
            snprintf(buf, sizeof(buf), "%d", defVal);
        }
        float blinkV = editingVal ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
        ui_->drawWin98TextField(valX, y, valW, btnSz, buf, editingVal, false, blinkV);
        if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, valX, y, valW, btnSz)) {
            ui_->mouseClicked = false;
            ui_->clickCooldownFrames = 2;
            if (!editingVal) {
                varListSelected_ = i;
                varListEditingName_ = false;
                varListEditingValue_ = true;
                snprintf(buf, sizeof(buf), "%d", defVal);
                varListValueBuf_ = buf;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
            }
        }

        // Scope toggle (L=local, P=pack)
        if (ui_->win98Button(700 + i * 3 + 1, scope == 1 ? "P" : "L", scopeX, y, scopeW, btnSz, scope == 1)) {
            pushUndo();
            uint8_t newScope = scope == 1 ? 0 : 1;
            for (auto& va : csLib_.triggerVarActions)
                if (va.key == vn) va.scope = newScope;
        }

        // Delete button
        if (ui_->win98Button(700 + i * 3 + 2, "X", delX, y, delW, btnSz, false)) {
            pushUndo();
            // Remove from varDefaults only (not from trigger actions - those are authored data)
            csLib_.varDefaults.erase(vn);
            if (varListSelected_ == i) {
                varListSelected_ = -1;
                varListEditingName_ = false;
                varListEditingValue_ = false;
            }
        }

        y += rowH;
    }

    // "Add new variable" row
    if (addingNew) {
        // Show name text field spanning full width while typing
        float blinkNew = (float)fmod(SDL_GetTicks() * 0.001, 1.0);
        ui_->drawWin98TextField(lx, y, rvw, btnSz, varListNameBuf_.c_str(), true, false, blinkNew);
    } else {
        if (ui_->win98Button(800, "+ Add Variable", lx, y, rvw, btnSz, false)) {
            varListSelected_ = (int)varNames.size();
            varListEditingName_ = true;
            varListEditingValue_ = false;
            varListNameBuf_.clear();
#ifndef __SWITCH__
            SDL_StartTextInput();
#endif
        }
    }
    y += rowH;

    // Close button
    y += 4;
    if (ui_->win98Button(699, "Close", lx, y, rvw, btnSz, false))
        showVarList_ = false;
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

    const int btnH = TOOLBAR_H - 8;
    const int btnW = btnH;  // square icon buttons

    int x = 6;
    const char* tipName = nullptr;  // hovered button's tooltip
    int tipX = 0;

    // Icon button: square, draws an image icon (with procedural-glyph fallback)
    // and registers a hover tooltip. `texName` may be null to force the glyph.
    auto ib = [&](int id, const char* texName, TIcon glyph, bool sel, const char* name) -> bool {
        bool fired = ui_->win98Button(id, "", x, 5, btnW, btnH, sel);
        // Active/selected tool reads as pressed-in.
        if (sel) ui_->drawWin98Bevel(x, 5, btnW, btnH, false);
        bool pressed = sel || (ui_->pointInRect(ui_->mouseX, ui_->mouseY, x, 5, btnW, btnH) && ui_->mouseDown);
        int off = pressed ? 1 : 0;
        int cx = x + btnW / 2 + off, cy = 5 + btnH / 2 + off;
        SDL_Texture* t = texName ? Assets::instance().loadRelTex(texName) : nullptr;
        if (t) {
            const int isz = 16;
            SDL_SetTextureColorMod(t, 255, 255, 255);
            SDL_SetTextureAlphaMod(t, 255);
            SDL_Rect dst = {cx - isz / 2, cy - isz / 2, isz, isz};
            SDL_RenderCopy(renderer, t, nullptr, &dst);
        } else if (glyph != TIcon::Help) {
            drawGlyph(renderer, glyph, cx, cy, UI::W98::Black);
        } else {
            ui_->drawText("?", cx - 3, cy - 8, 16, UI::W98::Black);
        }
        if (ui_->pointInRect(ui_->mouseX, ui_->mouseY, x, 5, btnW, btnH)) { tipName = name; tipX = x; }
        x += btnW + 3;
        return fired;
    };
    auto drawSep = [&]() {
        x += 3;
        SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
        SDL_RenderDrawLine(renderer, x, 7, x, TOOLBAR_H - 7);
        SDL_SetRenderDrawColor(renderer, UI::W98::White.r, UI::W98::White.g, UI::W98::White.b, 255);
        SDL_RenderDrawLine(renderer, x + 1, 7, x + 1, TOOLBAR_H - 7);
        x += 7;
    };

    // Tool buttons (IDs 100-106) - MS Paint style icons
    static const char* toolTex[(int)EditorTool::TOOL_COUNT] = {
        "sprites/ui/ed_tile.png", "sprites/ui/ed_trigger.png", "sprites/ui/ed_entity.png",
        "sprites/ui/ed_erase.png", "sprites/ui/ed_select.png", "sprites/ui/ed_rect.png",
        "sprites/ui/ed_fill.png"};
    static const TIcon toolGlyph[(int)EditorTool::TOOL_COUNT] = {
        TIcon::Tile, TIcon::Trigger, TIcon::Entity, TIcon::Erase,
        TIcon::Select, TIcon::Rect, TIcon::Fill};
    static const char* toolNames[(int)EditorTool::TOOL_COUNT] = {
        "Tile (1)", "Trigger (2)", "Entity (3)", "Erase (4)",
        "Select (5)", "Rect (6)", "Fill (7)"};
    for (int i = 0; i < (int)EditorTool::TOOL_COUNT; i++) {
        if (ib(100 + i, toolTex[i], toolGlyph[i], (int)currentTool_ == i, toolNames[i])) {
            currentTool_ = (EditorTool)i;
            selectedTrigger_ = -1;
            selectedEnemy_   = -1;
            rectStartTX_ = -1; rectStartTY_ = -1;
            trigDragging_ = false;
        }
    }
    drawSep();

    // Undo / Redo (IDs 112-113)
    if (ib(112, nullptr, TIcon::Undo, false, "Undo (Ctrl+Z)")) undo();
    if (ib(113, nullptr, TIcon::Redo, false, "Redo (Ctrl+Y)")) redo();
    drawSep();

    // Toggles (IDs 114-120)
    if (ib(114, nullptr, TIcon::Grid, showGrid_, "Grid (G)"))              showGrid_ = !showGrid_;
    if (ib(116, nullptr, TIcon::Props, showMapProps_, "Map Properties"))   showMapProps_ = !showMapProps_;
    if (ib(117, nullptr, TIcon::Rnd, randomRotation_, "Random rotation"))  randomRotation_ = !randomRotation_;
    if (ib(119, nullptr, TIcon::NoCo, noCollision_, "No collision"))       noCollision_ = !noCollision_;
    if (!map_.topImagePath.empty())
        if (ib(118, nullptr, TIcon::Top, showTopLayer_, "Top layer"))      showTopLayer_ = !showTopLayer_;
    drawSep();

    // Cutscene editor toggle
    if (ib(120, nullptr, TIcon::Scene, showCutsceneEditor_, "Cutscene editor")) {
        showCutsceneEditor_ = !showCutsceneEditor_;
        csEditor_.setActive(showCutsceneEditor_);
    }
    // Variable list toggle (ID 122)
    {
        // Draw a small "V" glyph manually since we have no icon
        bool firedVars = ui_->win98Button(122, "Vars", x, 5, btnW + 8, btnH, showVarList_);
        if (showVarList_) ui_->drawWin98Bevel(x, 5, btnW + 8, btnH, false);
        if (ui_->pointInRect(ui_->mouseX, ui_->mouseY, x, 5, btnW + 8, btnH)) { tipName = "Variables"; tipX = x; }
        x += btnW + 11;
        if (firedVars) showVarList_ = !showVarList_;
    }
    // Help (F1)
    if (ib(121, nullptr, TIcon::Help, showHelp_, "Help (F1)")) showHelp_ = true;

    // Save / Play in the top-right corner (right-aligned, image icons).
    {
        int rx = screenW_ - 6;
        auto cornerBtn = [&](int id, const char* texName, const char* label, const char* tip) -> bool {
            int tw = ui_->textWidth(label, 14);
            int w  = 22 + tw + 12;     // icon + gap + label + padding
            rx -= w;
            bool fired = ui_->win98Button(id, "", rx, 5, w, btnH, false);
            SDL_Texture* t = Assets::instance().loadRelTex(texName);
            if (t) {
                SDL_SetTextureColorMod(t, 255, 255, 255);
                SDL_SetTextureAlphaMod(t, 255);
                SDL_Rect dst = {rx + 6, 5 + (btnH - 16) / 2, 16, 16};
                SDL_RenderCopy(renderer, t, nullptr, &dst);
            }
            ui_->drawText(label, rx + 24, 5 + (btnH - ui_->textHeight(14)) / 2, 14, UI::W98::Black);
            if (ui_->pointInRect(ui_->mouseX, ui_->mouseY, rx, 5, w, btnH)) { tipName = tip; tipX = rx; }
            rx -= 5;
            return fired;
        };
        if (cornerBtn(111, "sprites/ui/tb_start.png", "Play", "Test play (F5)"))
            wantsTestPlay_ = true;
        if (cornerBtn(110, "sprites/ui/tb_save.png", dirty_ ? "Save*" : "Save", "Save map (Ctrl+S)"))
            wantsModSave_ = true;
    }

    // Hover tooltip, drawn just below the toolbar (after all buttons so it wins)
    if (tipName) {
        int tw = ui_->textWidth(tipName, 12);
        int boxX = std::min(tipX, screenW_ - tw - 12);
        int boxY = TOOLBAR_H + 2;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 255, 255, 225, 255);
        SDL_Rect tb = {boxX, boxY, tw + 10, 18};
        SDL_RenderFillRect(renderer, &tb);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &tb);
        ui_->drawText(tipName, boxX + 5, boxY + 3, 12, UI::W98::Black);
    }
}

void MapEditor::renderPalette(SDL_Renderer* renderer) {
    if (!ui_) return;
    int px = screenW_ - PALETTE_W;
    // Stop the palette at the top of the cutscene panel so the two never
    // overlap; otherwise palette rows would silently eat clicks meant for the
    // cutscene inspector underneath.
    const int palBottom = screenH_ - csEditorBottom();

    // Win98 panel background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
    SDL_Rect bg = {px, TOOLBAR_H, PALETTE_W, palBottom - TOOLBAR_H};
    SDL_RenderFillRect(renderer, &bg);
    // Left border
    SDL_SetRenderDrawColor(renderer, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
    SDL_RenderDrawLine(renderer, px, TOOLBAR_H, px, palBottom);
    SDL_SetRenderDrawColor(renderer, UI::W98::White.r, UI::W98::White.g, UI::W98::White.b, 255);
    SDL_RenderDrawLine(renderer, px + 1, TOOLBAR_H, px + 1, palBottom);

    // Tab buttons (IDs 120-124)
    static const char* tabNames[] = {"All", "Gnd", "Wal", "Cei", "Prp"};
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

    // Clip to palette content area (bounded by the cutscene panel)
    SDL_Rect clip = {px + 2, contentTop, PALETTE_W - 2, palBottom - contentTop};
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
            if (y + 20 >= contentTop && y < palBottom) {
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

        if (y + rowH >= contentTop && y < palBottom) {
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
        int viewH  = palBottom - contentTop;
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
    const char* dirs[] = {"maps", "romfs/maps", "romfs:/maps", "fs:/vol/content/maps"};
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
#ifdef __WIIU__
        // Native swkbd raises these on OK/Cancel. Without handling them textEditing
        // never clears and the keyboard reopens every frame ("OK infinitely enters").
        if (e.type == SDL_SYSWMEVENT && e.syswm.msg) {
            switch (e.syswm.msg->msg.wiiu.event) {
                case SDL_WIIU_SYSWM_SWKBD_OK_START_EVENT:
                    cfg.textBuf.clear();   // swkbd delivers the whole string; replace
                    break;
                case SDL_WIIU_SYSWM_SWKBD_OK_FINISH_EVENT:
                    commitConfigEdit();    // commit + textEditing=false + StopTextInput
                    break;
                case SDL_WIIU_SYSWM_SWKBD_CANCEL_EVENT:
                    cfg.textEditing = false;
                    SDL_StopTextInput();
                    break;
            }
            return;
        }
#endif
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
                // In load mode, skip fields 2-5 which are for new map only
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
                    editorBeginTextInput(cfg.textBuf);
                } else if ((cfg.field == 3 || cfg.field == 4) && cfg.action == EditorConfig::Action::NewMap) {
                    cfg.textEditing = true;
                    cfg.textBuf = (cfg.field == 3) ? cfg.mapName : cfg.creator;
                    editorBeginTextInput(cfg.textBuf);
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
                    editorBeginTextInput(cfg.textBuf);
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

// Commit the value currently being typed into a config text field. Called when
// the user presses Enter (in handleConfigInput) or clicks off the field.
void MapEditor::commitConfigEdit() {
    auto& cfg = config_;
    if (!cfg.textEditing) return;
    bool isNumField = (cfg.field == 1 || cfg.field == 2);
    if (isNumField) {
        int v = cfg.textBuf.empty() ? 0 : std::stoi(cfg.textBuf);
        v = std::max(10, std::min(200, v));
        if (cfg.field == 1) cfg.mapWidth  = v;
        else                cfg.mapHeight = v;
    } else if (cfg.field == 3) {
        cfg.mapName = cfg.textBuf;
    } else if (cfg.field == 4) {
        cfg.creator = cfg.textBuf;
    }
    cfg.textEditing = false;
#ifndef __SWITCH__
    SDL_StopTextInput();
#endif
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

    // A click anywhere commits the field currently being edited. If the click
    // lands on another field, that field re-opens for editing below, so the
    // typed value is never lost just because the user clicked away.
    if (cfg.textEditing && ui_->mouseClicked)
        commitConfigEdit();

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
            editorBeginTextInput(cfg.textBuf);
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


            // Click to start editing
            if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, fieldX, y, fieldW, rowH)) {
                cfg.field = idx;
                cfg.textEditing = true;
                cfg.textBuf = (idx == 3) ? cfg.mapName : cfg.creator;
                editorBeginTextInput(cfg.textBuf);
            }
            y += rowH + rowGap;
        
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
        drawArrowField(5, "Mode:", cfg.gameMode == 1 ? "Sandbox" : "Arena");
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
                // Feed the immediate-mode UI so toolbar/panel buttons respond
                // to the virtual cursor exactly like a mouse click.
                if (ui_) {
                    ui_->mouseX = mouseX_;
                    ui_->mouseY = mouseY_;
                    ui_->mouseClicked = true;
                }
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
                currentTool_ = (EditorTool)(((int)currentTool_ + 1) % (int)EditorTool::TOOL_COUNT);
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
                    // Cycle only valid trigger types (the enum has gaps)
                    static const TriggerType kGpTypes[] = {
                        TriggerType::LevelStart, TriggerType::LevelEnd,
                        TriggerType::Crate, TriggerType::Effect,
                        TriggerType::TeamSpawnRed, TriggerType::TeamSpawnBlue,
                        TriggerType::TeamSpawnGreen, TriggerType::TeamSpawnYellow,
                        TriggerType::LayerFade, TriggerType::CollisionZone,
                        TriggerType::Cutscene, TriggerType::Waypoint,
                        TriggerType::SignalZone, TriggerType::Objective,
                    };
                    int cur = 0;
                    for (int i = 0; i < 14; i++)
                        if (kGpTypes[i] == triggerGhost_.type) { cur = i; break; }
                    triggerGhost_.type = kGpTypes[(cur + 1) % 14];
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
    // Touches over the cutscene panel: forward as synthesized mouse events so
    // timeline drags and the scrubber work on touch devices.  Widget taps are
    // handled by the immediate-mode UI (input.cpp feeds touch into ui_).
    if (showCutsceneEditor_ &&
        (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION)) {
        int tx = (int)(e.tfinger.x * screenW_);
        int ty = (int)(e.tfinger.y * screenH_);
        bool inPanel = (ty >= screenH_ - csEditor_.panelHeight());
        if (inPanel || e.type == SDL_FINGERUP) {
            SDL_Event me;
            memset(&me, 0, sizeof(me));
            if (e.type == SDL_FINGERDOWN)      me.type = SDL_MOUSEBUTTONDOWN;
            else if (e.type == SDL_FINGERUP)   me.type = SDL_MOUSEBUTTONUP;
            else                               me.type = SDL_MOUSEMOTION;
            if (me.type == SDL_MOUSEMOTION) {
                me.motion.x = tx; me.motion.y = ty;
            } else {
                me.button.button = SDL_BUTTON_LEFT;
                me.button.x = tx; me.button.y = ty;
            }
            bool consumed = csEditor_.handleEvent(me, screenToWorldX(tx), screenToWorldY(ty), zoom_);
            if (csEditor_.takeUiClickSwallow() && ui_) {
                ui_->mouseClicked = false;
                if (ui_->clickCooldownFrames < 1) ui_->clickCooldownFrames = 1;
            }
            if (consumed && e.type != SDL_FINGERUP) {
                touchActive_ = (e.type != SDL_FINGERUP);
                return;
            }
            if (inPanel) {
                touchActive_ = (e.type != SDL_FINGERUP);
                mouseDown_ = false;
                return;
            }
        }
    }

    if (e.type == SDL_FINGERDOWN) {
        touchActive_ = true;
        touchX_ = e.tfinger.x * screenW_;
        touchY_ = e.tfinger.y * screenH_;
        mouseX_ = (int)touchX_;
        mouseY_ = (int)touchY_;

        // Taps on UI panels are handled by the immediate-mode widgets
        if (isOverUI(mouseX_, mouseY_)) {
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
