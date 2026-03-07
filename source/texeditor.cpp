// ─── texeditor.cpp ─── Pixel Art / Sprite Editor implementation ─────────────
#include "texeditor.h"
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
#include <cctype>
#include <queue>

// ═════════════════════════════════════════════════════════════════════════════
//  Init / Shutdown
// ═════════════════════════════════════════════════════════════════════════════

bool TextureEditor::init(SDL_Renderer* renderer, int screenW, int screenH) {
    renderer_ = renderer;
    screenW_  = screenW;
    screenH_  = screenH;
    zoom_     = 8.0f;
    cursorX_  = screenW / 2.0f;
    cursorY_  = screenH / 2.0f;
    initDefaultPalette();
    showConfig();
    return true;
}

void TextureEditor::shutdown() {
    if (canvasTex_) { SDL_DestroyTexture(canvasTex_); canvasTex_ = nullptr; }
    pixels_.clear();
    undoStack_.clear();
    redoStack_.clear();
}

void TextureEditor::showConfig() {
    state_ = TexEditorState::Config;
    config_.field = 0;
    config_.textEditing = false;
    wantsExit_ = false;
    scanImageFiles();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Default palette — 32 curated colors
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::initDefaultPalette() {
    // Row 1: grayscale
    palette_[0]  = {0,   0,   0,   255};
    palette_[1]  = {34,  34,  34,  255};
    palette_[2]  = {68,  68,  68,  255};
    palette_[3]  = {102, 102, 102, 255};
    palette_[4]  = {136, 136, 136, 255};
    palette_[5]  = {170, 170, 170, 255};
    palette_[6]  = {204, 204, 204, 255};
    palette_[7]  = {255, 255, 255, 255};
    // Row 2: warm colors
    palette_[8]  = {255, 0,   0,   255};
    palette_[9]  = {255, 80,  80,  255};
    palette_[10] = {255, 160, 60,  255};
    palette_[11] = {255, 220, 50,  255};
    palette_[12] = {255, 255, 100, 255};
    palette_[13] = {200, 120, 50,  255};
    palette_[14] = {140, 80,  30,  255};
    palette_[15] = {80,  40,  20,  255};
    // Row 3: cool colors
    palette_[16] = {0,   180, 80,  255};
    palette_[17] = {50,  255, 120, 255};
    palette_[18] = {0,   200, 200, 255};
    palette_[19] = {0,   140, 255, 255};
    palette_[20] = {0,   80,  200, 255};
    palette_[21] = {80,  60,  200, 255};
    palette_[22] = {160, 80,  255, 255};
    palette_[23] = {255, 100, 200, 255};
    // Row 4: skin / nature / extras
    palette_[24] = {255, 220, 180, 255};
    palette_[25] = {220, 180, 140, 255};
    palette_[26] = {180, 140, 100, 255};
    palette_[27] = {100, 160, 60,  255};
    palette_[28] = {60,  120, 40,  255};
    palette_[29] = {40,  80,  30,  255};
    palette_[30] = {0,   0,   0,   0};     // fully transparent
    palette_[31] = {255, 255, 255, 128};   // semi-transparent white

    currentColor_ = palette_[7]; // start with white
    paletteIdx_ = 7;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Canvas management
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::newCanvas(int w, int h) {
    canvasW_ = w;
    canvasH_ = h;
    pixels_.assign(w * h, {0, 0, 0, 0}); // transparent
    undoStack_.clear();
    redoStack_.clear();

    if (canvasTex_) SDL_DestroyTexture(canvasTex_);
    canvasTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureBlendMode(canvasTex_, SDL_BLENDMODE_BLEND);
    updateCanvasTexture();

    // Center view
    float areaW = screenW_ - PALETTE_W;
    float areaH = screenH_ - TOOLBAR_H - STATUS_H;
    zoom_ = std::min(areaW / w, areaH / h) * 0.8f;
    zoom_ = std::max(1.0f, std::min(zoom_, 64.0f));
    panX_ = 0;
    panY_ = 0;
}

void TextureEditor::updateCanvasTexture() {
    if (!canvasTex_) return;
    void* texPixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(canvasTex_, nullptr, &texPixels, &pitch) == 0) {
        for (int y = 0; y < canvasH_; y++) {
            uint8_t* row = (uint8_t*)texPixels + y * pitch;
            for (int x = 0; x < canvasW_; x++) {
                const TexelColor& c = pixels_[y * canvasW_ + x];
                // SDL_PIXELFORMAT_RGBA32 = ABGR on little-endian
                row[x * 4 + 0] = c.r;
                row[x * 4 + 1] = c.g;
                row[x * 4 + 2] = c.b;
                row[x * 4 + 3] = c.a;
            }
        }
        SDL_UnlockTexture(canvasTex_);
    }
}

void TextureEditor::setPixel(int x, int y, TexelColor c) {
    if (x < 0 || x >= canvasW_ || y < 0 || y >= canvasH_) return;
    pixels_[y * canvasW_ + x] = c;
}

TexelColor TextureEditor::getPixel(int x, int y) const {
    if (x < 0 || x >= canvasW_ || y < 0 || y >= canvasH_) return {0,0,0,0};
    return pixels_[y * canvasW_ + x];
}

// ═════════════════════════════════════════════════════════════════════════════
//  Drawing algorithms
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::floodFill(int x, int y, TexelColor target, TexelColor fill) {
    if (target == fill) return;
    if (x < 0 || x >= canvasW_ || y < 0 || y >= canvasH_) return;
    if (getPixel(x, y) != target) return;

    std::queue<std::pair<int,int>> q;
    q.push({x, y});
    setPixel(x, y, fill);

    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        const int dx[] = {0, 0, -1, 1};
        const int dy[] = {-1, 1, 0, 0};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx >= 0 && nx < canvasW_ && ny >= 0 && ny < canvasH_) {
                if (getPixel(nx, ny) == target) {
                    setPixel(nx, ny, fill);
                    q.push({nx, ny});
                }
            }
        }
    }
}

void TextureEditor::drawLinePixels(int x0, int y0, int x1, int y1, TexelColor c) {
    // Bresenham's line
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        setPixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void TextureEditor::drawRectPixels(int x0, int y0, int x1, int y1, TexelColor c, bool filled) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (filled) {
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                setPixel(x, y, c);
    } else {
        for (int x = x0; x <= x1; x++) { setPixel(x, y0, c); setPixel(x, y1, c); }
        for (int y = y0; y <= y1; y++) { setPixel(x0, y, c); setPixel(x1, y, c); }
    }
}

void TextureEditor::drawCirclePixels(int cx, int cy, int rx, int ry, TexelColor c, bool filled) {
    // Midpoint ellipse
    if (rx == 0 && ry == 0) { setPixel(cx, cy, c); return; }
    if (rx == 0) { for (int y = cy - ry; y <= cy + ry; y++) setPixel(cx, y, c); return; }
    if (ry == 0) { for (int x = cx - rx; x <= cx + rx; x++) setPixel(x, cy, c); return; }

    for (int y = -ry; y <= ry; y++) {
        for (int x = -rx; x <= rx; x++) {
            float ex = (float)x / rx;
            float ey = (float)y / ry;
            if (ex * ex + ey * ey <= 1.0f) {
                if (filled) {
                    setPixel(cx + x, cy + y, c);
                } else {
                    // only draw edge pixels
                    float ex2 = (float)(abs(x) - 1) / rx;
                    float ey2 = (float)(abs(y) - 1) / ry;
                    if (ex2 * ex2 + ey2 * ey2 > 1.0f || abs(x) == rx || abs(y) == ry) {
                        setPixel(cx + x, cy + y, c);
                    }
                }
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Undo / Redo
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::pushUndo() {
    undoStack_.push_back(pixels_);
    if ((int)undoStack_.size() > MAX_UNDO)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

void TextureEditor::undo() {
    if (undoStack_.empty()) return;
    redoStack_.push_back(pixels_);
    pixels_ = undoStack_.back();
    undoStack_.pop_back();
    updateCanvasTexture();
}

void TextureEditor::redo() {
    if (redoStack_.empty()) return;
    undoStack_.push_back(pixels_);
    pixels_ = redoStack_.back();
    redoStack_.pop_back();
    updateCanvasTexture();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Coordinate conversion
// ═════════════════════════════════════════════════════════════════════════════

int TextureEditor::canvasOriginX() const {
    float areaW = screenW_ - PALETTE_W;
    return (int)(PALETTE_W + areaW / 2.0f - (canvasW_ * zoom_) / 2.0f - panX_ * zoom_);
}

int TextureEditor::canvasOriginY() const {
    float areaH = screenH_ - TOOLBAR_H - STATUS_H;
    return (int)(TOOLBAR_H + areaH / 2.0f - (canvasH_ * zoom_) / 2.0f - panY_ * zoom_);
}

bool TextureEditor::screenToCanvas(int sx, int sy, int& cx, int& cy) const {
    int ox = canvasOriginX(), oy = canvasOriginY();
    cx = (int)((sx - ox) / zoom_);
    cy = (int)((sy - oy) / zoom_);
    return cx >= 0 && cx < canvasW_ && cy >= 0 && cy < canvasH_;
}

void TextureEditor::canvasToScreen(int cx, int cy, int& sx, int& sy) const {
    sx = canvasOriginX() + (int)(cx * zoom_);
    sy = canvasOriginY() + (int)(cy * zoom_);
}

int TextureEditor::paletteGridY() const {
    // Must match the layout in renderPalette():
    // TOOLBAR_H + 8 (COLOR label Y) + 16 (label height) + 40 (swatch) + 12 (gap) + 16 (PALETTE label)
    return TOOLBAR_H + 8 + 16 + 40 + 12 + 16;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Color picker — HSV conversion
// ═════════════════════════════════════════════════════════════════════════════

TexelColor TextureEditor::hsvToRgb(float h, float s, float v, uint8_t a) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    if      (h < 60)  { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    return { (uint8_t)((r + m) * 255), (uint8_t)((g + m) * 255), (uint8_t)((b + m) * 255), a };
}

void TextureEditor::rgbToHsv(TexelColor c, float& h, float& s, float& v) {
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    float d = mx - mn;
    v = mx;
    s = (mx > 0) ? d / mx : 0;
    if (d == 0) h = 0;
    else if (mx == r) h = 60.0f * fmodf((g - b) / d + 6.0f, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else              h = 60.0f * ((r - g) / d + 4.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
//  File I/O
// ═════════════════════════════════════════════════════════════════════════════

bool TextureEditor::saveImage(const std::string& path) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, canvasW_, canvasH_, 32,
                                                        SDL_PIXELFORMAT_RGBA32);
    if (!surf) return false;
    SDL_LockSurface(surf);
    for (int y = 0; y < canvasH_; y++) {
        uint8_t* row = (uint8_t*)surf->pixels + y * surf->pitch;
        for (int x = 0; x < canvasW_; x++) {
            const TexelColor& c = pixels_[y * canvasW_ + x];
            row[x * 4 + 0] = c.r;
            row[x * 4 + 1] = c.g;
            row[x * 4 + 2] = c.b;
            row[x * 4 + 3] = c.a;
        }
    }
    SDL_UnlockSurface(surf);
    int ok = IMG_SavePNG(surf, path.c_str());
    SDL_FreeSurface(surf);
    return ok == 0;
}

void TextureEditor::performModSave(const std::string& modFolder, int cat) {
    // Map category index → subdirectory
    static const char* catSubPaths[] = {
        "sprites/tiles/ground",
        "sprites/tiles/walls",
        "sprites/tiles/ceiling",
        "sprites/characters/body",
        "sprites/characters/legs"
    };
    const char* sub = (cat >= 0 && cat < 5) ? catSubPaths[cat] : "sprites";

    // Create directory chain
    mkdir((modFolder + "/sprites").c_str(), 0755);
    mkdir((modFolder + "/sprites/tiles").c_str(), 0755);
    mkdir((modFolder + "/sprites/tiles/ground").c_str(), 0755);
    mkdir((modFolder + "/sprites/tiles/walls").c_str(), 0755);
    mkdir((modFolder + "/sprites/tiles/ceiling").c_str(), 0755);
    mkdir((modFolder + "/sprites/characters").c_str(), 0755);
    mkdir((modFolder + "/sprites/characters/body").c_str(), 0755);
    mkdir((modFolder + "/sprites/characters/legs").c_str(), 0755);

    savePath_ = modFolder + "/" + std::string(sub) + "/" + fileName_ + ".png";
    if (saveImage(savePath_))
        saveMessage_ = "Saved to mod: " + savePath_;
    else
        saveMessage_ = "Save FAILED!";
    saveMessageTimer_ = 2.5f;
}

bool TextureEditor::loadImage(const std::string& path) {
    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf) return false;

    // Convert to RGBA32
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surf);
    if (!conv) return false;

    newCanvas(conv->w, conv->h);
    SDL_LockSurface(conv);
    for (int y = 0; y < conv->h; y++) {
        const uint8_t* row = (const uint8_t*)conv->pixels + y * conv->pitch;
        for (int x = 0; x < conv->w; x++) {
            pixels_[y * canvasW_ + x] = { row[x*4+0], row[x*4+1], row[x*4+2], row[x*4+3] };
        }
    }
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);
    updateCanvasTexture();
    pushUndo();
    return true;
}

void TextureEditor::scanImageFiles() {
    loadFiles_.clear();
    const char* dirs[] = { "romfs/sprites", "romfs/tiles", "romfs/characters", "sprites", "tiles", "characters" };
    for (auto& dirPath : dirs) {
        DIR* d = opendir(dirPath);
        if (!d) continue;
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name(ent->d_name);
            if (name.size() < 5) continue;
            std::string ext = name.substr(name.size() - 4);
            for (auto& ch : ext) ch = tolower(ch);
            if (ext == ".png" || ext == ".bmp") {
                loadFiles_.push_back(std::string(dirPath) + "/" + name);
            }
        }
        closedir(d);
    }
    std::sort(loadFiles_.begin(), loadFiles_.end());
    loadFileIdx_ = 0;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Tool application
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::applyBrushAt(int cx, int cy, TexelColor c) {
    int r = brushSize_ - 1; // 0 for single pixel
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < canvasW_ && py >= 0 && py < canvasH_) {
                // Circular brush: skip corners for size >= 3
                if (r >= 2 && dx*dx + dy*dy > r*r) continue;
                setPixel(px, py, c);
            }
        }
    }
}

void TextureEditor::applyToolAtPixel(int cx, int cy) {
    if (cx < 0 || cx >= canvasW_ || cy < 0 || cy >= canvasH_) return;
    switch (currentTool_) {
        case TexTool::Pen:
            if (lastPx_ >= 0 && lastPy_ >= 0) {
                // Bresenham line with brush at each point
                int dx = abs(cx - lastPx_), sx = lastPx_ < cx ? 1 : -1;
                int dy = -abs(cy - lastPy_), sy = lastPy_ < cy ? 1 : -1;
                int err = dx + dy;
                int x = lastPx_, y = lastPy_;
                while (true) {
                    applyBrushAt(x, y, currentColor_);
                    if (x == cx && y == cy) break;
                    int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x += sx; }
                    if (e2 <= dx) { err += dx; y += sy; }
                }
            } else {
                applyBrushAt(cx, cy, currentColor_);
            }
            lastPx_ = cx; lastPy_ = cy;
            break;
        case TexTool::Eraser:
            if (lastPx_ >= 0 && lastPy_ >= 0) {
                int dx = abs(cx - lastPx_), sx = lastPx_ < cx ? 1 : -1;
                int dy = -abs(cy - lastPy_), sy = lastPy_ < cy ? 1 : -1;
                int err = dx + dy;
                int x = lastPx_, y = lastPy_;
                while (true) {
                    applyBrushAt(x, y, {0,0,0,0});
                    if (x == cx && y == cy) break;
                    int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x += sx; }
                    if (e2 <= dx) { err += dx; y += sy; }
                }
            } else {
                applyBrushAt(cx, cy, {0, 0, 0, 0});
            }
            lastPx_ = cx; lastPy_ = cy;
            break;
        case TexTool::Fill:
            floodFill(cx, cy, getPixel(cx, cy), currentColor_);
            break;
        case TexTool::Eyedropper:
            currentColor_ = getPixel(cx, cy);
            rgbToHsv(currentColor_, hue_, sat_, val_);
            break;
        case TexTool::Line:
        case TexTool::Rect:
        case TexTool::Circle:
            // handled on mouse-up
            break;
        default: break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Input handling
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::handleInput(const SDL_Event& e) {
    if (!active_) return;
    switch (state_) {
        case TexEditorState::Config:      handleConfigInput(e);      break;
        case TexEditorState::Editing:     handleEditingInput(e);     break;
        case TexEditorState::ColorPicker: handleColorPickerInput(e); break;
    }
}

void TextureEditor::handleConfigInput(const SDL_Event& e) {
    // Config screen: field navigation & text input
    const int NUM_FIELDS = (config_.action == TexEditorConfig::NewImage) ? 5 : 2;
    // NewImage: 0=action, 1=template, 2=width, 3=height, 4=name(+OK/Cancel at 5,6)
    // LoadImage: 0=action, 1=file list (+OK/Cancel at 2,3)
    const int TOTAL = (config_.action == TexEditorConfig::NewImage) ? 7 : 4;

    if (e.type == SDL_KEYDOWN) {
        SDL_Keycode key = e.key.keysym.sym;

        if (config_.textEditing) {
            if (key == SDLK_RETURN || key == SDLK_ESCAPE) {
                config_.textEditing = false;
            } else if (key == SDLK_BACKSPACE && !config_.name.empty()) {
                config_.name.pop_back();
            }
            return;
        }

        if (key == SDLK_UP)   config_.field = (config_.field - 1 + TOTAL) % TOTAL;
        if (key == SDLK_DOWN) config_.field = (config_.field + 1) % TOTAL;

        if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            int dir = (key == SDLK_RIGHT) ? 1 : -1;
            if (config_.field == 0) {
                config_.action = (config_.action == TexEditorConfig::NewImage)
                    ? TexEditorConfig::LoadImage : TexEditorConfig::NewImage;
                config_.field = 0;
            }
            if (config_.action == TexEditorConfig::NewImage) {
                if (config_.field == 1) {
                    int t = ((int)config_.tmpl + dir + (int)TexTemplate::Count) % (int)TexTemplate::Count;
                    config_.tmpl = (TexTemplate)t;
                    // Apply template sizes
                    switch (config_.tmpl) {
                        case TexTemplate::Custom:    break;
                        case TexTemplate::Tile16:    config_.canvasW = config_.canvasH = 16; break;
                        case TexTemplate::Tile32:    config_.canvasW = config_.canvasH = 32; break;
                        case TexTemplate::Tile64:    config_.canvasW = config_.canvasH = 64; break;
                        case TexTemplate::Sprite32:  config_.canvasW = config_.canvasH = 32; break;
                        case TexTemplate::Sprite64:  config_.canvasW = config_.canvasH = 64; break;
                        case TexTemplate::Sprite128: config_.canvasW = config_.canvasH = 128; break;
                        case TexTemplate::Icon16:    config_.canvasW = config_.canvasH = 16; break;
                        default: break;
                    }
                }
                if (config_.field == 2) {
                    config_.canvasW = std::max(1, std::min(256, config_.canvasW + dir * (config_.canvasW >= 64 ? 16 : config_.canvasW >= 16 ? 8 : 1)));
                    config_.tmpl = TexTemplate::Custom;
                }
                if (config_.field == 3) {
                    config_.canvasH = std::max(1, std::min(256, config_.canvasH + dir * (config_.canvasH >= 64 ? 16 : config_.canvasH >= 16 ? 8 : 1)));
                    config_.tmpl = TexTemplate::Custom;
                }
            } else {
                // Load mode: scroll file list
                if (config_.field == 1 && !loadFiles_.empty()) {
                    loadFileIdx_ = (loadFileIdx_ + dir + (int)loadFiles_.size()) % (int)loadFiles_.size();
                }
            }
        }

        if (key == SDLK_RETURN) {
            if (config_.action == TexEditorConfig::NewImage) {
                if (config_.field == 4) {
                    config_.textEditing = true;
                } else if (config_.field == 5) { // OK
                    newCanvas(config_.canvasW, config_.canvasH);
                    fileName_ = config_.name;
                    savePath_ = "sprites/" + fileName_ + ".png";
                    state_ = TexEditorState::Editing;
                    pushUndo();
                } else if (config_.field == 6) { // Cancel
                    wantsExit_ = true;
                }
            } else {
                if (config_.field == 2) { // OK
                    if (!loadFiles_.empty() && loadFileIdx_ < (int)loadFiles_.size()) {
                        std::string path = loadFiles_[loadFileIdx_];
                        if (loadImage(path)) {
                            savePath_ = path;
                            auto slash = path.find_last_of('/');
                            fileName_ = (slash != std::string::npos) ? path.substr(slash + 1) : path;
                            auto dot = fileName_.find_last_of('.');
                            if (dot != std::string::npos) fileName_ = fileName_.substr(0, dot);
                            state_ = TexEditorState::Editing;
                        }
                    }
                } else if (config_.field == 3) { // Cancel
                    wantsExit_ = true;
                }
            }
        }
        if (key == SDLK_ESCAPE) {
            wantsExit_ = true;
        }
    }
    else if (e.type == SDL_TEXTINPUT && config_.textEditing) {
        if (config_.name.size() < 32)
            config_.name += e.text.text;
    }
    else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
            if (config_.textEditing) config_.textEditing = false;
            else wantsExit_ = true;
        }
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            config_.field = (config_.field - 1 + TOTAL) % TOTAL;
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            config_.field = (config_.field + 1) % TOTAL;
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            int dir = (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 1 : -1;
            if (config_.field == 0) {
                config_.action = (config_.action == TexEditorConfig::NewImage)
                    ? TexEditorConfig::LoadImage : TexEditorConfig::NewImage;
            }
        }
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            // Confirm — same as Enter
            SDL_Event fake; fake.type = SDL_KEYDOWN;
            fake.key.keysym.sym = SDLK_RETURN;
            handleConfigInput(fake);
        }
    }
}

void TextureEditor::handleEditingInput(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN) {
        SDL_Keycode key = e.key.keysym.sym;
        bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;

        if (key == SDLK_ESCAPE) { wantsExit_ = true; return; }

        // Undo / Redo
        if (ctrl && key == SDLK_z) { undo(); return; }
        if (ctrl && key == SDLK_y) { redo(); return; }

        // Save
        if (ctrl && key == SDLK_s) {
            wantsModSave_ = true;
            return;
        }

        // Tool shortcuts
        if (key == SDLK_b || key == SDLK_p) currentTool_ = TexTool::Pen;
        if (key == SDLK_e) currentTool_ = TexTool::Eraser;
        if (key == SDLK_g) currentTool_ = TexTool::Fill;
        if (key == SDLK_l) currentTool_ = TexTool::Line;
        if (key == SDLK_r) currentTool_ = TexTool::Rect;
        if (key == SDLK_o) currentTool_ = TexTool::Circle;
        if (key == SDLK_i) currentTool_ = TexTool::Eyedropper;

        // Grid toggle
        if (key == SDLK_h) showGrid_ = !showGrid_;

        // Brush size
        if (key == SDLK_LEFTBRACKET)  brushSize_ = std::max(1, brushSize_ - 1);
        if (key == SDLK_RIGHTBRACKET) brushSize_ = std::min(16, brushSize_ + 1);

        // Color picker
        if (key == SDLK_c) {
            state_ = TexEditorState::ColorPicker;
            cpDragMode_ = 0;
            rgbToHsv(currentColor_, hue_, sat_, val_);
        }

        // Zoom
        if (key == SDLK_EQUALS || key == SDLK_PLUS)
            zoom_ = std::min(64.0f, zoom_ * 1.25f);
        if (key == SDLK_MINUS)
            zoom_ = std::max(1.0f, zoom_ / 1.25f);

        // Pan with arrow keys
        float panSpeed = 4.0f;
        if (key == SDLK_LEFT)  panX_ -= panSpeed;
        if (key == SDLK_RIGHT) panX_ += panSpeed;
        if (key == SDLK_UP)    panY_ -= panSpeed;
        if (key == SDLK_DOWN)  panY_ += panSpeed;
    }
    else if (e.type == SDL_MOUSEWHEEL) {
        if (e.wheel.y > 0) zoom_ = std::min(64.0f, zoom_ * 1.15f);
        if (e.wheel.y < 0) zoom_ = std::max(1.0f, zoom_ / 1.15f);
    }
    else if (e.type == SDL_MOUSEBUTTONDOWN) {
        int mx = e.button.x, my = e.button.y;

        // Check if clicking palette area (left side)
        if (mx < PALETTE_W) {
            handlePaletteClick(mx, my);
            return;
        }

        // Check toolbar area
        if (my < TOOLBAR_H) {
            handleToolbarClick(mx, my);
            return;
        }

        int cx, cy;
        if (screenToCanvas(mx, my, cx, cy)) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (currentTool_ == TexTool::Line || currentTool_ == TexTool::Rect || currentTool_ == TexTool::Circle) {
                    if (!shapeStarted_) {
                        shapeStarted_ = true;
                        shapeX0_ = cx; shapeY0_ = cy;
                    }
                } else {
                    pushUndo();
                    drawing_ = true;
                    lastPx_ = -1; lastPy_ = -1;
                    applyToolAtPixel(cx, cy);
                    updateCanvasTexture();
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                // Right-click = eyedropper
                currentColor_ = getPixel(cx, cy);
                rgbToHsv(currentColor_, hue_, sat_, val_);
            }
            if (e.button.button == SDL_BUTTON_MIDDLE) {
                // Middle click starts pan drag — handled in MOUSEMOTION
            }
        }
    }
    else if (e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (shapeStarted_) {
                int cx, cy;
                if (screenToCanvas(e.button.x, e.button.y, cx, cy)) {
                    pushUndo();
                    switch (currentTool_) {
                        case TexTool::Line:
                            drawLinePixels(shapeX0_, shapeY0_, cx, cy, currentColor_);
                            break;
                        case TexTool::Rect:
                            drawRectPixels(shapeX0_, shapeY0_, cx, cy, currentColor_, false);
                            break;
                        case TexTool::Circle: {
                            int rx = abs(cx - shapeX0_), ry = abs(cy - shapeY0_);
                            int ccx = (shapeX0_ + cx) / 2, ccy = (shapeY0_ + cy) / 2;
                            drawCirclePixels(ccx, ccy, rx / 2, ry / 2, currentColor_, false);
                            break;
                        }
                        default: break;
                    }
                    updateCanvasTexture();
                }
                shapeStarted_ = false;
            }
            drawing_ = false;
            lastPx_ = -1; lastPy_ = -1;
        }
    }
    else if (e.type == SDL_MOUSEMOTION) {
        if (drawing_) {
            int cx, cy;
            if (screenToCanvas(e.motion.x, e.motion.y, cx, cy)) {
                applyToolAtPixel(cx, cy);
                updateCanvasTexture();
            }
        }
        // Middle button panning
        if (e.motion.state & SDL_BUTTON_MMASK) {
            panX_ -= e.motion.xrel / zoom_;
            panY_ -= e.motion.yrel / zoom_;
        }
    }
    else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        useGamepadCursor_ = true;
        int btn = e.cbutton.button;
        if (btn == SDL_CONTROLLER_BUTTON_B) { wantsExit_ = true; return; }
        if (btn == SDL_CONTROLLER_BUTTON_A) {
            int cx, cy;
            if (screenToCanvas((int)cursorX_, (int)cursorY_, cx, cy)) {
                pushUndo();
                applyToolAtPixel(cx, cy);
                updateCanvasTexture();
            }
        }
        // Tool cycle with bumpers
        if (btn == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            int t = ((int)currentTool_ - 1 + (int)TexTool::Count) % (int)TexTool::Count;
            currentTool_ = (TexTool)t;
        }
        if (btn == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            int t = ((int)currentTool_ + 1) % (int)TexTool::Count;
            currentTool_ = (TexTool)t;
        }
        // Zoom with stick press
        if (btn == SDL_CONTROLLER_BUTTON_LEFTSTICK) zoom_ = std::max(1.0f, zoom_ / 1.25f);
        if (btn == SDL_CONTROLLER_BUTTON_RIGHTSTICK) zoom_ = std::min(64.0f, zoom_ * 1.25f);
        // Grid toggle
        if (btn == SDL_CONTROLLER_BUTTON_Y) showGrid_ = !showGrid_;
        // Save
        if (btn == SDL_CONTROLLER_BUTTON_X) {
            wantsModSave_ = true;
        }
        // Color picker
        if (btn == SDL_CONTROLLER_BUTTON_START) {
            state_ = TexEditorState::ColorPicker;
            cpDragMode_ = 0;
            rgbToHsv(currentColor_, hue_, sat_, val_);
        }
        // Undo
        if (btn == SDL_CONTROLLER_BUTTON_BACK) undo();
    }
    else if (e.type == SDL_CONTROLLERAXISMOTION) {
        useGamepadCursor_ = true;
        // Left stick = cursor movement, Right stick = pan
        float deadzone = 8000;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            // handled in update via SDL_GameControllerGetAxis
        }
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX || e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
            // handled in update
        }
    }
}

void TextureEditor::handleColorPickerInput(const SDL_Event& e) {
    // Color picker: centered 400×340 panel
    int cpX = (screenW_ - 400) / 2;
    int cpY = (screenH_ - 340) / 2;
    int svX = cpX + 20, svY = cpY + 40;
    int svSize = 200;
    int hueX = cpX + 240, hueY = cpY + 40, hueW = 30, hueH = 200;
    int alphaX = cpX + 290, alphaY = cpY + 40, alphaW = 30, alphaH = 200;

    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_c) {
            currentColor_ = hsvToRgb(hue_, sat_, val_, currentColor_.a);
            palette_[paletteIdx_] = currentColor_;
            state_ = TexEditorState::Editing;
        }
        if (e.key.keysym.sym == SDLK_RETURN) {
            currentColor_ = hsvToRgb(hue_, sat_, val_, currentColor_.a);
            palette_[paletteIdx_] = currentColor_;
            state_ = TexEditorState::Editing;
        }
    }
    else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int mx = e.button.x, my = e.button.y;
        if (mx >= svX && mx < svX + svSize && my >= svY && my < svY + svSize) {
            cpDragMode_ = 1;
        } else if (mx >= hueX && mx < hueX + hueW && my >= hueY && my < hueY + hueH) {
            cpDragMode_ = 2;
        } else if (mx >= alphaX && mx < alphaX + alphaW && my >= alphaY && my < alphaY + alphaH) {
            cpDragMode_ = 3;
        } else {
            // Click outside — close
            currentColor_ = hsvToRgb(hue_, sat_, val_, currentColor_.a);
            palette_[paletteIdx_] = currentColor_;
            state_ = TexEditorState::Editing;
            return;
        }
    }
    else if (e.type == SDL_MOUSEBUTTONUP) {
        cpDragMode_ = 0;
    }
    if ((e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN) && cpDragMode_ > 0) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        if (cpDragMode_ == 1) {
            sat_ = std::max(0.0f, std::min(1.0f, (float)(mx - svX) / svSize));
            val_ = std::max(0.0f, std::min(1.0f, 1.0f - (float)(my - svY) / svSize));
        } else if (cpDragMode_ == 2) {
            hue_ = std::max(0.0f, std::min(359.9f, (float)(my - hueY) / hueH * 360.0f));
        } else if (cpDragMode_ == 3) {
            currentColor_.a = (uint8_t)(std::max(0.0f, std::min(1.0f, (float)(my - alphaY) / alphaH)) * 255);
        }
        currentColor_ = hsvToRgb(hue_, sat_, val_, currentColor_.a);
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B || e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            currentColor_ = hsvToRgb(hue_, sat_, val_, currentColor_.a);
            palette_[paletteIdx_] = currentColor_;
            state_ = TexEditorState::Editing;
        }
    }
}

// Helper for palette/toolbar clicks (not declared in header — internal)
void TextureEditor::handlePaletteClick(int mx, int my) {
    // Palette is rendered on the left side, 200px wide
    // Use consistent Y position from paletteGridY()
    int startY = paletteGridY();
    int cellW = 22, cellH = 22, cols = 8;
    int padX = 8;

    for (int i = 0; i < PALETTE_SIZE; i++) {
        int col = i % cols, row = i / cols;
        int cx = padX + col * cellW;
        int cy = startY + row * cellH;
        if (mx >= cx && mx < cx + cellW && my >= cy && my < cy + cellH) {
            paletteIdx_ = i;
            currentColor_ = palette_[i];
            rgbToHsv(currentColor_, hue_, sat_, val_);
            return;
        }
    }
}

void TextureEditor::handleToolbarClick(int mx, int my) {
   // Toolbar buttons — must match layout in renderToolbar()
    int numTools = (int)TexTool::Count;
    int btnW = 52, btnH = 36;
    int startX = PALETTE_W + 10;

    for (int i = 0; i < numTools; i++) {
        int bx = startX + i * (btnW + 4);
        if (mx >= bx && mx < bx + btnW && my >= 6 && my < 6 + btnH) {
            currentTool_ = (TexTool)i;
            return;
        }
    }

    // Separator and subsequent buttons — positions must match renderToolbar
    int sepX = startX + numTools * (btnW + 4) + 8;

    // Save button
    int saveX = sepX + 12;
    if (mx >= saveX && mx < saveX + 60 && my >= 6 && my < 6 + btnH) {
        wantsModSave_ = true;
        return;
    }

    // Undo button
    int undoX = sepX + 84;
    if (mx >= undoX && mx < undoX + 44 && my >= 6 && my < 6 + btnH) {
        undo();
        return;
    }
    // Redo button
    if (mx >= undoX + 48 && mx < undoX + 92 && my >= 6 && my < 6 + btnH) {
        redo();
        return;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Update
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::update(float dt) {
    if (!active_) return;
    if (saveMessageTimer_ > 0) saveMessageTimer_ -= dt;

    // Gamepad cursor
    if (useGamepadCursor_) {
        SDL_GameController* gc = nullptr;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) { gc = SDL_GameControllerOpen(i); break; }
        }
        if (gc) {
            float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            float deadzone = 0.2f;
            if (fabsf(lx) > deadzone) cursorX_ += lx * 600.0f * dt;
            if (fabsf(ly) > deadzone) cursorY_ += ly * 600.0f * dt;
            cursorX_ = std::max(0.0f, std::min((float)screenW_, cursorX_));
            cursorY_ = std::max(0.0f, std::min((float)screenH_, cursorY_));

            // Right stick for pan
            float rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
            float ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
            if (fabsf(rx) > deadzone) panX_ += rx * 20.0f * dt;
            if (fabsf(ry) > deadzone) panY_ += ry * 20.0f * dt;
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Rendering
// ═════════════════════════════════════════════════════════════════════════════

void TextureEditor::render() {
    if (!active_) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    if (state_ == TexEditorState::Config) {
        renderConfig();
        return;
    }

    // Dark background
    SDL_SetRenderDrawColor(renderer_, 18, 20, 30, 255);
    SDL_Rect full = {0, 0, screenW_, screenH_};
    SDL_RenderFillRect(renderer_, &full);

    renderCanvas();
    if (showGrid_ && zoom_ >= 4.0f) renderGrid();
    renderPreview();
    renderToolbar();
    renderPalette();
    renderStatusBar();
    renderCursor();

    if (state_ == TexEditorState::ColorPicker) {
        renderColorPicker();
    }
}

void TextureEditor::renderConfig() {
    auto& A = Assets::instance();
    TTF_Font* font = A.font(20);
    TTF_Font* fontSm = A.font(14);
    TTF_Font* fontLg = A.font(30);

    // Background
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, screenW_, screenH_};
    SDL_RenderFillRect(renderer_, &full);

    // Panel
    int pw = 560, ph = 420;
    int px = (screenW_ - pw) / 2, py = (screenH_ - ph) / 2 - 20;
    SDL_SetRenderDrawColor(renderer_, 14, 16, 26, 255);
    SDL_Rect panel = {px, py, pw, ph};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 80);
    SDL_RenderDrawRect(renderer_, &panel);

    // Title
    SDL_Color cyan = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray = {120, 120, 130, 255};
    SDL_Color green = {50, 255, 100, 255};
    SDL_Color red = {255, 100, 100, 255};

    auto drawText = [&](const char* text, int x, int y, TTF_Font* f, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(f, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    auto drawTextCentered = [&](const char* text, int y, TTF_Font* f, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(f, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {screenW_ / 2 - s->w / 2, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    drawTextCentered("SPRITE EDITOR", py + 16, fontLg, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {px + pw/2 - 100, py + 54, 200, 1};
    SDL_RenderFillRect(renderer_, &tl);

    int y = py + 76;
    int stepY = 40;
    int labelX = px + 30;
    int valueX = px + 200;

    auto drawRow = [&](int idx, const char* label, const char* value, bool arrowHint = true) {
        bool sel = (config_.field == idx);
        SDL_Color c = sel ? white : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
            SDL_Rect bg = {px + 10, y - 2, pw - 20, 32};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 160);
            SDL_Rect bar = {px + 10, y - 2, 3, 32};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawText(label, labelX, y + 4, font, c);
        char buf[128];
        if (sel && arrowHint)
            snprintf(buf, sizeof(buf), "< %s >", value);
        else
            snprintf(buf, sizeof(buf), "%s", value);
        drawText(buf, valueX, y + 4, font, sel ? cyan : gray);
        y += stepY;
    };

    // Action
    drawRow(0, "Action:", config_.action == TexEditorConfig::NewImage ? "New Image" : "Load Image");

    if (config_.action == TexEditorConfig::NewImage) {
        // Template
        const char* tmplNames[] = {"Custom", "Tile 16x16", "Tile 32x32", "Tile 64x64",
                                   "Sprite 32x32", "Sprite 64x64", "Sprite 128x128", "Icon 16x16"};
        drawRow(1, "Template:", tmplNames[(int)config_.tmpl]);

        // Width
        char wBuf[32]; snprintf(wBuf, sizeof(wBuf), "%d", config_.canvasW);
        drawRow(2, "Width:", wBuf);

        // Height
        char hBuf[32]; snprintf(hBuf, sizeof(hBuf), "%d", config_.canvasH);
        drawRow(3, "Height:", hBuf);

        // Name
        {
            bool sel = (config_.field == 4);
            SDL_Color c = sel ? white : gray;
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
                SDL_Rect bg = {px + 10, y - 2, pw - 20, 32};
                SDL_RenderFillRect(renderer_, &bg);
                SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 160);
                SDL_Rect bar = {px + 10, y - 2, 3, 32};
                SDL_RenderFillRect(renderer_, &bar);
            }
            drawText("Name:", labelX, y + 4, font, c);
            std::string dispName = config_.name;
            if (config_.textEditing) dispName += "_";
            drawText(dispName.c_str(), valueX, y + 4, font, config_.textEditing ? green : (sel ? cyan : gray));
            y += stepY;
        }

        // Preview size text
        {
            char prevBuf[64];
            snprintf(prevBuf, sizeof(prevBuf), "Canvas: %d x %d pixels", config_.canvasW, config_.canvasH);
            drawText(prevBuf, px + 30, y, fontSm, {0, 140, 130, 255});
            y += stepY - 4;
        }

        // OK
        {
            bool sel = (config_.field == 5);
            SDL_Color c = sel ? green : gray;
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 20);
                SDL_Rect bg = {screenW_/2 - 80, y - 2, 160, 32};
                SDL_RenderFillRect(renderer_, &bg);
            }
            drawTextCentered(sel ? "> CREATE <" : "CREATE", y + 4, font, c);
            y += stepY;
        }
        // Cancel
        {
            bool sel = (config_.field == 6);
            SDL_Color c = sel ? red : gray;
            drawTextCentered(sel ? "> CANCEL <" : "CANCEL", y + 4, font, c);
        }
    } else {
        // Load mode
        {
            bool sel = (config_.field == 1);
            SDL_Color c = sel ? white : gray;
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
                SDL_Rect bg = {px + 10, y - 2, pw - 20, 32};
                SDL_RenderFillRect(renderer_, &bg);
                SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 160);
                SDL_Rect bar = {px + 10, y - 2, 3, 32};
                SDL_RenderFillRect(renderer_, &bar);
            }
            drawText("File:", labelX, y + 4, font, c);
            if (loadFiles_.empty()) {
                drawText("(no images found)", valueX, y + 4, font, gray);
            } else {
                // Show current file with < > arrows
                std::string fname = loadFiles_[loadFileIdx_];
                auto slash = fname.find_last_of('/');
                if (slash != std::string::npos) fname = fname.substr(slash + 1);
                char fBuf[128];
                if (sel)
                    snprintf(fBuf, sizeof(fBuf), "< %s > (%d/%d)", fname.c_str(), loadFileIdx_+1, (int)loadFiles_.size());
                else
                    snprintf(fBuf, sizeof(fBuf), "%s (%d/%d)", fname.c_str(), loadFileIdx_+1, (int)loadFiles_.size());
                drawText(fBuf, valueX, y + 4, font, sel ? cyan : gray);
            }
            y += stepY;

            // Show file path
            if (!loadFiles_.empty() && loadFileIdx_ < (int)loadFiles_.size()) {
                drawText(loadFiles_[loadFileIdx_].c_str(), px + 30, y, fontSm, {0, 140, 130, 255});
            }
            y += stepY;
        }

        // OK
        {
            bool sel = (config_.field == 2);
            SDL_Color c = sel ? green : gray;
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 20);
                SDL_Rect bg = {screenW_/2 - 80, y - 2, 160, 32};
                SDL_RenderFillRect(renderer_, &bg);
            }
            drawTextCentered(sel ? "> LOAD <" : "LOAD", y + 4, font, c);
            y += stepY;
        }
        // Cancel
        {
            bool sel = (config_.field == 3);
            SDL_Color c = sel ? red : gray;
            drawTextCentered(sel ? "> CANCEL <" : "CANCEL", y + 4, font, c);
        }
    }

    // Bottom hint
    drawTextCentered("Arrow keys navigate  Left/Right change  Enter confirm  Esc cancel",
                     screenH_ - 36, fontSm, {80, 80, 90, 255});
}

void TextureEditor::renderCanvas() {
    if (!canvasTex_) return;

    int ox = canvasOriginX(), oy = canvasOriginY();
    int cw = (int)(canvasW_ * zoom_), ch = (int)(canvasH_ * zoom_);

    // Checkerboard background (transparency indicator)
    int checkSize = std::max(1, (int)(zoom_ * 0.5f));
    if (checkSize < 2) checkSize = (int)zoom_;
    if (checkSize < 1) checkSize = 1;

    // Draw a simple checkerboard behind the canvas
    SDL_Rect canvasArea = {ox, oy, cw, ch};
    SDL_RenderSetClipRect(renderer_, &canvasArea);
    for (int cy = 0; cy < ch; cy += checkSize) {
        for (int cx = 0; cx < cw; cx += checkSize) {
            bool dark = ((cx / checkSize + cy / checkSize) % 2) == 0;
            SDL_SetRenderDrawColor(renderer_, dark ? 40 : 60, dark ? 40 : 60, dark ? 44 : 64, 255);
            SDL_Rect r = {ox + cx, oy + cy, checkSize, checkSize};
            SDL_RenderFillRect(renderer_, &r);
        }
    }
    SDL_RenderSetClipRect(renderer_, nullptr);

    // Draw canvas texture
    SDL_Rect dst = {ox, oy, cw, ch};
    SDL_RenderCopy(renderer_, canvasTex_, nullptr, &dst);

    // Canvas border
    SDL_SetRenderDrawColor(renderer_, 0, 200, 180, 120);
    SDL_Rect border = {ox - 1, oy - 1, cw + 2, ch + 2};
    SDL_RenderDrawRect(renderer_, &border);

    // Shape preview (line/rect/circle rubber-band)
    if (shapeStarted_) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        int sx0, sy0, sx1, sy1;
        canvasToScreen(shapeX0_, shapeY0_, sx0, sy0);
        sx1 = mx; sy1 = my;

        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 120);
        if (currentTool_ == TexTool::Line) {
            SDL_RenderDrawLine(renderer_, sx0, sy0, sx1, sy1);
        } else if (currentTool_ == TexTool::Rect) {
            SDL_Rect r;
            r.x = std::min(sx0, sx1); r.y = std::min(sy0, sy1);
            r.w = abs(sx1 - sx0); r.h = abs(sy1 - sy0);
            SDL_RenderDrawRect(renderer_, &r);
        } else if (currentTool_ == TexTool::Circle) {
            // Simple visual feedback — draw bounding rect
            SDL_Rect r;
            r.x = std::min(sx0, sx1); r.y = std::min(sy0, sy1);
            r.w = abs(sx1 - sx0); r.h = abs(sy1 - sy0);
            SDL_RenderDrawRect(renderer_, &r);
        }
    }
}

void TextureEditor::renderGrid() {
    if (!canvasTex_) return;
    int ox = canvasOriginX(), oy = canvasOriginY();
    int cw = (int)(canvasW_ * zoom_), ch = (int)(canvasH_ * zoom_);

    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 20);
    // Vertical lines
    for (int x = 0; x <= canvasW_; x++) {
        int sx = ox + (int)(x * zoom_);
        SDL_RenderDrawLine(renderer_, sx, oy, sx, oy + ch);
    }
    // Horizontal lines
    for (int y = 0; y <= canvasH_; y++) {
        int sy = oy + (int)(y * zoom_);
        SDL_RenderDrawLine(renderer_, ox, sy, ox + cw, sy);
    }
}

void TextureEditor::renderToolbar() {
    auto& A = Assets::instance();
    TTF_Font* font = A.font(14);
    TTF_Font* fontSm = A.font(11);

    // Toolbar background
    SDL_SetRenderDrawColor(renderer_, 22, 24, 38, 255);
    SDL_Rect tbBg = {0, 0, screenW_, TOOLBAR_H};
    SDL_RenderFillRect(renderer_, &tbBg);
    SDL_SetRenderDrawColor(renderer_, 0, 140, 130, 40);
    SDL_Rect tbLine = {0, TOOLBAR_H - 1, screenW_, 1};
    SDL_RenderFillRect(renderer_, &tbLine);

    auto drawText = [&](const char* text, int x, int y, TTF_Font* f, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(f, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray = {120, 120, 130, 255};
    SDL_Color cyan = {0, 255, 228, 255};

    // Tool buttons
    const char* toolNames[] = {"Pen", "Era", "Fill", "Line", "Rect", "Circ", "Eye"};
    const char* toolKeys[]  = {"B",   "E",   "G",    "L",    "R",    "O",    "I"};
    SDL_Color toolColors[] = {
        {80, 180, 255, 255},   // Pen - blue
        {255, 140, 80, 255},   // Eraser - orange
        {80, 255, 160, 255},   // Fill - green
        {255, 220, 80, 255},   // Line - yellow
        {200, 140, 255, 255},  // Rect - purple
        {255, 100, 200, 255},  // Circle - pink
        {0, 255, 228, 255},   // Eyedropper - cyan
    };

    int btnW = 52, btnH = 36;
    int startX = PALETTE_W + 10;

    for (int i = 0; i < (int)TexTool::Count; i++) {
        int bx = startX + i * (btnW + 4);
        bool active = (currentTool_ == (TexTool)i);

        // Button background
        if (active) {
            SDL_SetRenderDrawColor(renderer_, toolColors[i].r, toolColors[i].g, toolColors[i].b, 40);
        } else {
            SDL_SetRenderDrawColor(renderer_, 30, 32, 48, 255);
        }
        SDL_Rect btn = {bx, 6, btnW, btnH};
        SDL_RenderFillRect(renderer_, &btn);

        // Active indicator
        if (active) {
            SDL_SetRenderDrawColor(renderer_, toolColors[i].r, toolColors[i].g, toolColors[i].b, 220);
            SDL_Rect ind = {bx, 6 + btnH - 3, btnW, 3};
            SDL_RenderFillRect(renderer_, &ind);
        }

        // Tool name
        SDL_Color tc = active ? toolColors[i] : gray;
        drawText(toolNames[i], bx + 4, 10, font, tc);

        // Key badge
        drawText(toolKeys[i], bx + btnW - 12, 28, fontSm, {60, 60, 70, 255});
    }

    // Separator
    int sepX = startX + (int)TexTool::Count * (btnW + 4) + 8;
    SDL_SetRenderDrawColor(renderer_, 60, 60, 80, 80);
    SDL_Rect sep = {sepX, 10, 1, 28};
    SDL_RenderFillRect(renderer_, &sep);

    // Save button
    {
        int saveX = sepX + 12;
        SDL_SetRenderDrawColor(renderer_, 50, 80, 50, 255);
        SDL_Rect btn = {saveX, 6, 60, btnH};
        SDL_RenderFillRect(renderer_, &btn);
        drawText("Save", saveX + 10, 13, font, {50, 255, 100, 255});
        drawText("^S", saveX + 42, 28, fontSm, {60, 60, 70, 255});
    }

    // Undo / Redo buttons
    {
        int undoX = sepX + 84;
        SDL_SetRenderDrawColor(renderer_, 40, 40, 55, 255);
        SDL_Rect btn1 = {undoX, 6, 44, btnH};
        SDL_RenderFillRect(renderer_, &btn1);
        drawText("Undo", undoX + 2, 13, font, undoStack_.empty() ? gray : white);

        SDL_Rect btn2 = {undoX + 48, 6, 44, btnH};
        SDL_RenderFillRect(renderer_, &btn2);
        drawText("Redo", undoX + 50, 13, font, redoStack_.empty() ? gray : white);
    }

    // Grid toggle
    {
        int gridX = screenW_ - 180;
        drawText(showGrid_ ? "[H] Grid ON" : "[H] Grid OFF", gridX, 16, font, showGrid_ ? cyan : gray);
    }

    // Color picker button
    {
        int cpX = screenW_ - 80;
        drawText("[C] Color", cpX, 16, font, {200, 140, 255, 255});
    }

    // File name
    {
        drawText(fileName_.c_str(), PALETTE_W + 10, TOOLBAR_H - 14, fontSm, {80, 80, 90, 255});
    }
}

void TextureEditor::renderPalette() {
    auto& A = Assets::instance();
    TTF_Font* font = A.font(14);
    TTF_Font* fontSm = A.font(11);

    // Palette panel background
    SDL_SetRenderDrawColor(renderer_, 14, 16, 26, 255);
    SDL_Rect palBg = {0, TOOLBAR_H, PALETTE_W, screenH_ - TOOLBAR_H - STATUS_H};
    SDL_RenderFillRect(renderer_, &palBg);
    // Right border
    SDL_SetRenderDrawColor(renderer_, 0, 140, 130, 40);
    SDL_Rect border = {PALETTE_W - 1, TOOLBAR_H, 1, screenH_ - TOOLBAR_H - STATUS_H};
    SDL_RenderFillRect(renderer_, &border);

    auto drawText = [&](const char* text, int x, int y, TTF_Font* f, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(f, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    SDL_Color dimCyan = {0, 140, 130, 255};

    // Current color preview
    int prevY = TOOLBAR_H + 8;
    drawText("COLOR", 8, prevY, fontSm, dimCyan);
    prevY += 16;

    // Large current color swatch
    int swatchSize = 40;
    // Checkerboard behind alpha
    for (int cy = 0; cy < swatchSize; cy += 8) {
        for (int cx = 0; cx < swatchSize; cx += 8) {
            bool dark = ((cx / 8 + cy / 8) % 2) == 0;
            SDL_SetRenderDrawColor(renderer_, dark ? 40 : 60, dark ? 40 : 60, dark ? 44 : 64, 255);
            SDL_Rect r = {8 + cx, prevY + cy, 8, 8};
            SDL_RenderFillRect(renderer_, &r);
        }
    }
    SDL_SetRenderDrawColor(renderer_, currentColor_.r, currentColor_.g, currentColor_.b, currentColor_.a);
    SDL_Rect swRect = {8, prevY, swatchSize, swatchSize};
    SDL_RenderFillRect(renderer_, &swRect);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 80);
    SDL_RenderDrawRect(renderer_, &swRect);

    // RGB values
    char rgbBuf[64];
    snprintf(rgbBuf, sizeof(rgbBuf), "R:%d G:%d B:%d", currentColor_.r, currentColor_.g, currentColor_.b);
    drawText(rgbBuf, 56, prevY + 2, fontSm, {180, 180, 180, 255});
    snprintf(rgbBuf, sizeof(rgbBuf), "A:%d  #%02X%02X%02X", currentColor_.a, currentColor_.r, currentColor_.g, currentColor_.b);
    drawText(rgbBuf, 56, prevY + 18, fontSm, {140, 140, 140, 255});

    prevY += swatchSize + 12;

    // Palette grid
    drawText("PALETTE", 8, prevY, fontSm, dimCyan);
    prevY += 16;

    int cellW = 22, cellH = 22, cols = 8;
    int padX = 8;

    for (int i = 0; i < PALETTE_SIZE; i++) {
        int col = i % cols, row = i / cols;
        int cx = padX + col * cellW;
        int cy = prevY + row * cellH;

        // Checkerboard for transparent colors
        if (palette_[i].a < 255) {
            for (int py = 0; py < cellH - 2; py += 6) {
                for (int px = 0; px < cellW - 2; px += 6) {
                    bool dark = ((px / 6 + py / 6) % 2) == 0;
                    SDL_SetRenderDrawColor(renderer_, dark ? 40 : 60, dark ? 40 : 60, dark ? 44 : 64, 255);
                    SDL_Rect r = {cx + 1 + px, cy + 1 + py, std::min(6, cellW - 2 - px), std::min(6, cellH - 2 - py)};
                    SDL_RenderFillRect(renderer_, &r);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer_, palette_[i].r, palette_[i].g, palette_[i].b, palette_[i].a);
        SDL_Rect cr = {cx + 1, cy + 1, cellW - 2, cellH - 2};
        SDL_RenderFillRect(renderer_, &cr);

        // Selection highlight
        if (i == paletteIdx_) {
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
            SDL_Rect sel = {cx, cy, cellW, cellH};
            SDL_RenderDrawRect(renderer_, &sel);
        }
    }

    prevY += (PALETTE_SIZE / cols + 1) * cellH + 8;

    // Brush size display
    drawText("BRUSH", 8, prevY, fontSm, dimCyan);
    prevY += 16;
    char brushBuf[32];
    snprintf(brushBuf, sizeof(brushBuf), "Size: %d  [ / ]", brushSize_);
    drawText(brushBuf, 8, prevY, fontSm, {180, 180, 180, 255});
    // Brush preview
    int bpx = 120, bpy = prevY - 2;
    int bpSize = std::min(brushSize_ * 4, 20);
    SDL_SetRenderDrawColor(renderer_, currentColor_.r, currentColor_.g, currentColor_.b, currentColor_.a);
    SDL_Rect bpRect = {bpx, bpy, bpSize, bpSize};
    SDL_RenderFillRect(renderer_, &bpRect);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 80);
    SDL_RenderDrawRect(renderer_, &bpRect);
    prevY += 24;

    // Hint
    drawText("Click to select", 8, prevY, fontSm, {60, 60, 70, 255});
    drawText("[C] Edit color", 8, prevY + 14, fontSm, {60, 60, 70, 255});
    drawText("Right-click: pick", 8, prevY + 28, fontSm, {60, 60, 70, 255});
}

void TextureEditor::renderColorPicker() {
    auto& A = Assets::instance();
    TTF_Font* font = A.font(16);
    TTF_Font* fontSm = A.font(12);

    // Dimmed overlay
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
    SDL_Rect full = {0, 0, screenW_, screenH_};
    SDL_RenderFillRect(renderer_, &full);

    // Panel
    int pw = 400, ph = 340;
    int px = (screenW_ - pw) / 2, py = (screenH_ - ph) / 2;
    SDL_SetRenderDrawColor(renderer_, 18, 20, 34, 255);
    SDL_Rect panel = {px, py, pw, ph};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 100);
    SDL_RenderDrawRect(renderer_, &panel);

    auto drawText = [&](const char* text, int x, int y, TTF_Font* f, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(f, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    drawText("COLOR PICKER", px + 20, py + 10, font, {0, 255, 228, 255});

    // Saturation/Value square (200×200)
    int svX = px + 20, svY = py + 40, svSize = 200;
    for (int sy = 0; sy < svSize; sy++) {
        for (int sx = 0; sx < svSize; sx++) {
            float s = (float)sx / svSize;
            float v = 1.0f - (float)sy / svSize;
            TexelColor c = hsvToRgb(hue_, s, v);
            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
            SDL_RenderDrawPoint(renderer_, svX + sx, svY + sy);
        }
    }
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 200);
    SDL_Rect svBorder = {svX, svY, svSize, svSize};
    SDL_RenderDrawRect(renderer_, &svBorder);

    // Cursor on SV square
    int csx = svX + (int)(sat_ * svSize);
    int csy = svY + (int)((1.0f - val_) * svSize);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer_, csx - 6, csy, csx + 6, csy);
    SDL_RenderDrawLine(renderer_, csx, csy - 6, csx, csy + 6);

    // Hue bar (30×200)
    int hueX = px + 240, hueY = py + 40, hueW = 30, hueH = 200;
    for (int hy = 0; hy < hueH; hy++) {
        float h = ((float)hy / hueH) * 360.0f;
        TexelColor c = hsvToRgb(h, 1, 1);
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
        SDL_RenderDrawLine(renderer_, hueX, hueY + hy, hueX + hueW, hueY + hy);
    }
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 200);
    SDL_Rect hueBorder = {hueX, hueY, hueW, hueH};
    SDL_RenderDrawRect(renderer_, &hueBorder);
    // Hue marker
    int hmy = hueY + (int)(hue_ / 360.0f * hueH);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_Rect hmark = {hueX - 2, hmy - 2, hueW + 4, 4};
    SDL_RenderDrawRect(renderer_, &hmark);

    // Alpha bar (30×200)
    int alphaX = px + 290, alphaY = py + 40, alphaW = 30, alphaH = 200;
    for (int ay = 0; ay < alphaH; ay++) {
        float a = (float)ay / alphaH;
        uint8_t av = (uint8_t)(a * 255);
        TexelColor cc = hsvToRgb(hue_, sat_, val_);
        SDL_SetRenderDrawColor(renderer_, cc.r, cc.g, cc.b, av);
        SDL_RenderDrawLine(renderer_, alphaX, alphaY + ay, alphaX + alphaW, alphaY + ay);
    }
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 200);
    SDL_Rect alphaBorder = {alphaX, alphaY, alphaW, alphaH};
    SDL_RenderDrawRect(renderer_, &alphaBorder);
    int amy = alphaY + (int)((float)currentColor_.a / 255.0f * alphaH);
    SDL_Rect amark = {alphaX - 2, amy - 2, alphaW + 4, 4};
    SDL_RenderDrawRect(renderer_, &amark);

    // Preview swatch
    int prevX = px + 340, prevY = py + 40;
    TexelColor preview = hsvToRgb(hue_, sat_, val_, currentColor_.a);
    SDL_SetRenderDrawColor(renderer_, preview.r, preview.g, preview.b, preview.a);
    SDL_Rect prevRect = {prevX, prevY, 40, 40};
    SDL_RenderFillRect(renderer_, &prevRect);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 80);
    SDL_RenderDrawRect(renderer_, &prevRect);

    // RGB text
    char buf[64];
    snprintf(buf, sizeof(buf), "R: %d", preview.r);  drawText(buf, px + 20, py + 252, fontSm, {200, 100, 100, 255});
    snprintf(buf, sizeof(buf), "G: %d", preview.g);  drawText(buf, px + 80, py + 252, fontSm, {100, 200, 100, 255});
    snprintf(buf, sizeof(buf), "B: %d", preview.b);  drawText(buf, px + 140, py + 252, fontSm, {100, 100, 200, 255});
    snprintf(buf, sizeof(buf), "A: %d", preview.a);  drawText(buf, px + 200, py + 252, fontSm, {200, 200, 200, 255});

    snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", preview.r, preview.g, preview.b, preview.a);
    drawText(buf, px + 20, py + 272, fontSm, {180, 180, 180, 255});

    // Hints
    drawText("Click SV square / Hue bar / Alpha bar", px + 20, py + 300, fontSm, {80, 80, 90, 255});
    drawText("ESC / Enter to confirm", px + 20, py + 316, fontSm, {80, 80, 90, 255});
}

void TextureEditor::renderStatusBar() {
    auto& A = Assets::instance();
    TTF_Font* font = A.font(13);

    int y = screenH_ - STATUS_H;
    SDL_SetRenderDrawColor(renderer_, 14, 16, 26, 255);
    SDL_Rect bg = {0, y, screenW_, STATUS_H};
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, 0, 140, 130, 40);
    SDL_Rect topLine = {0, y, screenW_, 1};
    SDL_RenderFillRect(renderer_, &topLine);

    auto drawText = [&](const char* text, int x, int y, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(font, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    SDL_Color gray = {120, 120, 130, 255};

    // Canvas size
    char buf[128];
    snprintf(buf, sizeof(buf), "%dx%d", canvasW_, canvasH_);
    drawText(buf, 10, y + 6, gray);

    // Zoom level
    snprintf(buf, sizeof(buf), "Zoom: %.0f%%", zoom_ * 100.0f / 8.0f);
    drawText(buf, 100, y + 6, gray);

    // Cursor position
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    int cx, cy;
    if (screenToCanvas(mx, my, cx, cy)) {
        snprintf(buf, sizeof(buf), "Pixel: %d, %d", cx, cy);
        drawText(buf, 240, y + 6, {0, 200, 180, 255});
    }

    // Tool name
    const char* toolNames[] = {"Pen", "Eraser", "Fill", "Line", "Rect", "Circle", "Eyedropper"};
    snprintf(buf, sizeof(buf), "Tool: %s", toolNames[(int)currentTool_]);
    drawText(buf, 400, y + 6, {0, 255, 228, 255});

    // Brush size
    snprintf(buf, sizeof(buf), "Brush: %d", brushSize_);
    drawText(buf, 560, y + 6, {180, 140, 255, 255});

    // Save message
    if (saveMessageTimer_ > 0 && !saveMessage_.empty()) {
        uint8_t alpha = (uint8_t)(std::min(1.0f, saveMessageTimer_ / 0.5f) * 255);
        drawText(saveMessage_.c_str(), 580, y + 6, {50, 255, 100, alpha});
    }

    // File path (right side)
    drawText(savePath_.c_str(), screenW_ - 200, y + 6, {60, 60, 70, 255});
}

void TextureEditor::renderPreview() {
    if (!canvasTex_) return;
    auto& A = Assets::instance();
    TTF_Font* fontSm = A.font(11);

    // Small 1:1 preview in bottom-left of palette area
    int previewY = screenH_ - STATUS_H - canvasH_ - 30;
    if (previewY < TOOLBAR_H + 220) previewY = TOOLBAR_H + 220;

    auto drawText = [&](const char* text, int x, int y, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(fontSm, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    drawText("PREVIEW", 8, previewY - 16, {0, 140, 130, 255});

    // Checkerboard behind
    int maxW = PALETTE_W - 16;
    int scale = std::max(1, std::min(maxW / canvasW_, (screenH_ - STATUS_H - previewY) / canvasH_));
    int pw = canvasW_ * scale, ph = canvasH_ * scale;

    for (int cy = 0; cy < ph; cy += 4) {
        for (int cx = 0; cx < pw; cx += 4) {
            bool dark = ((cx / 4 + cy / 4) % 2) == 0;
            SDL_SetRenderDrawColor(renderer_, dark ? 30 : 50, dark ? 30 : 50, dark ? 34 : 54, 255);
            SDL_Rect r = {8 + cx, previewY + cy, std::min(4, pw - cx), std::min(4, ph - cy)};
            SDL_RenderFillRect(renderer_, &r);
        }
    }

    SDL_Rect dst = {8, previewY, pw, ph};
    SDL_RenderCopy(renderer_, canvasTex_, nullptr, &dst);

    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 40);
    SDL_Rect bdr = {7, previewY - 1, pw + 2, ph + 2};
    SDL_RenderDrawRect(renderer_, &bdr);
}

void TextureEditor::renderCursor() {
    // Get mouse position (or gamepad virtual cursor)
    int mx, my;
    if (useGamepadCursor_) {
        mx = (int)cursorX_;
        my = (int)cursorY_;
    } else {
        SDL_GetMouseState(&mx, &my);
    }

    // Highlight the canvas pixel(s) under the cursor (brush size)
    int cx, cy;
    if (screenToCanvas(mx, my, cx, cy)) {
        int r = brushSize_ - 1;
        int minX = std::max(0, cx - r), minY = std::max(0, cy - r);
        int maxX = std::min(canvasW_ - 1, cx + r), maxY = std::min(canvasH_ - 1, cy + r);

        // Draw brush outline
        int sx0, sy0, sx1, sy1;
        canvasToScreen(minX, minY, sx0, sy0);
        canvasToScreen(maxX + 1, maxY + 1, sx1, sy1);

        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 150);
        SDL_Rect brushRect = {sx0, sy0, sx1 - sx0, sy1 - sy0};
        SDL_RenderDrawRect(renderer_, &brushRect);

        // Center pixel highlight
        int sxc, syc;
        canvasToScreen(cx, cy, sxc, syc);
        int pxSize = (int)zoom_;
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 200);
        SDL_Rect highlight = {sxc, syc, pxSize, pxSize};
        SDL_RenderDrawRect(renderer_, &highlight);

        // Crosshair arms
        int hcx = sxc + pxSize / 2, hcy = syc + pxSize / 2;
        int armLen = std::max(6, pxSize);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 80);
        SDL_RenderDrawLine(renderer_, hcx - armLen, hcy, sxc - 1, hcy);
        SDL_RenderDrawLine(renderer_, sxc + pxSize, hcy, hcx + armLen, hcy);
        SDL_RenderDrawLine(renderer_, hcx, hcy - armLen, hcx, syc - 1);
        SDL_RenderDrawLine(renderer_, hcx, syc + pxSize, hcx, hcy + armLen);
    }

    // Gamepad crosshair (always visible when using gamepad)
    if (useGamepadCursor_) {
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 200);
        SDL_RenderDrawLine(renderer_, mx - 10, my, mx + 10, my);
        SDL_RenderDrawLine(renderer_, mx, my - 10, mx, my + 10);

        // Color dot at center
        SDL_SetRenderDrawColor(renderer_, currentColor_.r, currentColor_.g, currentColor_.b, 200);
        SDL_Rect dot = {mx - 2, my - 2, 4, 4};
        SDL_RenderFillRect(renderer_, &dot);
    }
}
