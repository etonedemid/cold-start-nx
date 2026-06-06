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

bool TextureEditor::init(SDL_Renderer* renderer, int screenW, int screenH, UI::Context* ui) {
    renderer_ = renderer;
    ui_       = ui;
    screenW_  = screenW;
    screenH_  = screenH;
    zoom_     = 8.0f;
    cursorX_  = screenW / 2.0f;
    cursorY_  = screenH / 2.0f;
    initDefaultPalette();
    showConfig();
    return true;
}

void TextureEditor::setScreenSize(int w, int h) {
    screenW_ = w;
    screenH_ = h;
    cursorX_ = std::max(0.0f, std::min(cursorX_, (float)screenW_));
    cursorY_ = std::max(0.0f, std::min(cursorY_, (float)screenH_));
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

// Default palette - 32 curated colors

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

// Canvas management

void TextureEditor::newCanvas(int w, int h) {
    canvasW_ = w;
    canvasH_ = h;
    pixels_.assign(w * h, {0, 0, 0, 0}); // transparent
    frames_.assign(1, pixels_);          // start with a single animation frame
    curFrame_ = 0; playing_ = false; playFrame_ = 0; frameTimer_ = 0.0f;
    undoStack_.clear();
    redoStack_.clear();

    if (canvasTex_) SDL_DestroyTexture(canvasTex_);
    canvasTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureBlendMode(canvasTex_, SDL_BLENDMODE_BLEND);
    updateCanvasTexture();

    // Center view
    float areaW = screenW_ - PALETTE_W;
    float areaH = screenH_ - TOOLBAR_H - STATUS_H - FRAME_STRIP_H;
    zoom_ = std::min(areaW / w, areaH / h) * 0.8f;
    zoom_ = std::max(1.0f, std::min(zoom_, 64.0f));
    panX_ = 0;
    panY_ = 0;
}

// ---- Animation frames ----

void TextureEditor::commitFrame() {
    if (curFrame_ >= 0 && curFrame_ < (int)frames_.size())
        frames_[curFrame_] = pixels_;
}

void TextureEditor::setFrame(int i) {
    if (frames_.empty()) return;
    commitFrame();
    curFrame_ = std::max(0, std::min((int)frames_.size() - 1, i));
    pixels_ = frames_[curFrame_];
    undoStack_.clear();
    redoStack_.clear();
    updateCanvasTexture();
}

void TextureEditor::addFrame(bool duplicate) {
    commitFrame();
    std::vector<TexelColor> nf = duplicate
        ? pixels_
        : std::vector<TexelColor>(canvasW_ * canvasH_, TexelColor{0, 0, 0, 0});
    frames_.insert(frames_.begin() + curFrame_ + 1, nf);
    setFrame(curFrame_ + 1);
}

void TextureEditor::deleteFrame() {
    if (frames_.size() <= 1) return;  // always keep at least one frame
    frames_.erase(frames_.begin() + curFrame_);
    curFrame_ = std::min(curFrame_, (int)frames_.size() - 1);
    pixels_ = frames_[curFrame_];     // do NOT commit: the deleted frame is gone
    undoStack_.clear();
    redoStack_.clear();
    updateCanvasTexture();
}

void TextureEditor::doSave() {
    commitFrame();
    std::string base = savePath_;
    auto dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    if (frames_.size() <= 1) {
        savePath_ = base + ".png";
        saveMessage_ = saveImage(savePath_) ? ("Saved: " + savePath_) : "Save FAILED!";
    } else {
        // Export one numbered PNG per frame: base-0001.png, base-0002.png, ...
        std::vector<TexelColor> backup = pixels_;
        int ok = 0;
        for (int i = 0; i < (int)frames_.size(); i++) {
            pixels_ = frames_[i];
            char path[512];
            snprintf(path, sizeof(path), "%s-%04d.png", base.c_str(), i + 1);
            if (saveImage(path)) ok++;
        }
        pixels_ = backup;
        char msg[160];
        snprintf(msg, sizeof(msg), "Saved %d/%d frames to %s-NNNN.png", ok, (int)frames_.size(), base.c_str());
        saveMessage_ = msg;
    }
    saveMessageTimer_ = 2.5f;
}

void TextureEditor::liftSelection() {
    if (selLifted_ || !selActive_) return;
    selOrigW_ = selRect_.w;
    selOrigH_ = selRect_.h;
    selOrigPixels_.resize(selOrigW_ * selOrigH_);
    for (int y = 0; y < selOrigH_; y++) {
        for (int x = 0; x < selOrigW_; x++) {
            selOrigPixels_[y * selOrigW_ + x] = getPixel(selRect_.x + x, selRect_.y + y);
            setPixel(selRect_.x + x, selRect_.y + y, {0,0,0,0});
        }
    }
    updateCanvasTexture();
    selLifted_ = true;
}

void TextureEditor::stampSelection() {
    if (!selActive_ || selOrigPixels_.empty()) return;
    int dw = selRect_.w, dh = selRect_.h;
    if (dw <= 0 || dh <= 0) return;
    float rad = selAngle_ * 3.14159265f / 180.0f;
    float cosA = cosf(rad), sinA = sinf(rad);
    float srcCX = selOrigW_ * 0.5f, srcCY = selOrigH_ * 0.5f;
    float scaleX = (float)selOrigW_ / dw, scaleY = (float)selOrigH_ / dh;
    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            int canX = selRect_.x + dx, canY = selRect_.y + dy;
            if (canX < 0 || canX >= canvasW_ || canY < 0 || canY >= canvasH_) continue;
            float lx = dx - dw * 0.5f + 0.5f, ly = dy - dh * 0.5f + 0.5f;
            float sfx = lx * scaleX, sfy = ly * scaleY;
            int sx = (int)floorf(cosA * sfx + sinA * sfy + srcCX);
            int sy = (int)floorf(-sinA * sfx + cosA * sfy + srcCY);
            if (sx < 0 || sx >= selOrigW_ || sy < 0 || sy >= selOrigH_) continue;
            TexelColor src = selOrigPixels_[sy * selOrigW_ + sx];
            if (src.a == 0) continue;
            TexelColor& dst = pixels_[canY * canvasW_ + canX];
            int sa = src.a, da = dst.a * (255 - sa) / 255;
            int outA = sa + da;
            dst.r = outA ? (uint8_t)((src.r * sa + dst.r * da) / outA) : 0;
            dst.g = outA ? (uint8_t)((src.g * sa + dst.g * da) / outA) : 0;
            dst.b = outA ? (uint8_t)((src.b * sa + dst.b * da) / outA) : 0;
            dst.a = (uint8_t)outA;
        }
    }
}

void TextureEditor::commitSelection() {
    if (!selActive_) return;
    if (selLifted_) {
        pushUndo();
        stampSelection();
        updateCanvasTexture();
    }
    selActive_  = false;
    selLifted_  = false;
    selRect_    = {0,0,0,0};
    selOrigPixels_.clear();
    selOrigW_   = 0; selOrigH_ = 0;
    selAngle_   = 0.0f;
    selDragMode_= 0;
}

void TextureEditor::blitToCanvasTex(const std::vector<TexelColor>& src) {
    if (!canvasTex_ || (int)src.size() < canvasW_ * canvasH_) return;
    void* texPixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(canvasTex_, nullptr, &texPixels, &pitch) == 0) {
        for (int y = 0; y < canvasH_; y++) {
            uint8_t* row = (uint8_t*)texPixels + y * pitch;
            for (int x = 0; x < canvasW_; x++) {
                const TexelColor& c = src[y * canvasW_ + x];
                row[x * 4 + 0] = c.r;
                row[x * 4 + 1] = c.g;
                row[x * 4 + 2] = c.b;
                row[x * 4 + 3] = c.a;
            }
        }
        SDL_UnlockTexture(canvasTex_);
    }
}

void TextureEditor::updateCanvasTexture() {
    blitToCanvasTex(pixels_);  // mirror the active frame
}

void TextureEditor::setPixel(int x, int y, TexelColor c) {
    if (x < 0 || x >= canvasW_ || y < 0 || y >= canvasH_) return;
    pixels_[y * canvasW_ + x] = c;
}

TexelColor TextureEditor::getPixel(int x, int y) const {
    if (x < 0 || x >= canvasW_ || y < 0 || y >= canvasH_) return {0,0,0,0};
    return pixels_[y * canvasW_ + x];
}

// Drawing algorithms

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
    rx = abs(rx); ry = abs(ry);
    if (rx == 0 && ry == 0) { setPixel(cx, cy, c); return; }
    if (rx == 0) { for (int y = -ry; y <= ry; y++) setPixel(cx, cy + y, c); return; }
    if (ry == 0) { for (int x = -rx; x <= rx; x++) setPixel(cx + x, cy, c); return; }

    // A point is inside the ellipse when (x/rx)^2 + (y/ry)^2 <= 1.
    auto inside = [rx, ry](int x, int y) {
        float ex = (float)x / rx, ey = (float)y / ry;
        return ex * ex + ey * ey <= 1.0f;
    };
    for (int y = -ry; y <= ry; y++) {
        for (int x = -rx; x <= rx; x++) {
            if (!inside(x, y)) continue;
            // Outline = an inside pixel with at least one 4-neighbour outside.
            // This yields a clean, gap-free 1px border (the old test left holes).
            if (filled ||
                !inside(x - 1, y) || !inside(x + 1, y) ||
                !inside(x, y - 1) || !inside(x, y + 1)) {
                setPixel(cx + x, cy + y, c);
            }
        }
    }
}

void TextureEditor::applyBlur() {
    if (pixels_.empty()) return;
    std::vector<TexelColor> src = pixels_;
    for (int y = 0; y < canvasH_; y++) {
        for (int x = 0; x < canvasW_; x++) {
            // Alpha-weighted RGB average over a 3x3 kernel so transparent
            // pixels don't bleed black halos into the edges.
            int sumA = 0, n = 0;
            long sumR = 0, sumG = 0, sumB = 0, sumW = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= canvasW_ || ny < 0 || ny >= canvasH_) continue;
                    const TexelColor& s = src[ny * canvasW_ + nx];
                    sumA += s.a; n++;
                    sumR += (long)s.r * s.a; sumG += (long)s.g * s.a; sumB += (long)s.b * s.a;
                    sumW += s.a;
                }
            }
            TexelColor& d = pixels_[y * canvasW_ + x];
            d.a = (uint8_t)(n ? sumA / n : 0);
            if (sumW > 0) {
                d.r = (uint8_t)(sumR / sumW);
                d.g = (uint8_t)(sumG / sumW);
                d.b = (uint8_t)(sumB / sumW);
            }
        }
    }
    updateCanvasTexture();
}

void TextureEditor::applyRandomShift() {
    if (pixels_.empty()) return;
    std::vector<TexelColor> src = pixels_;
    int amt = std::max(1, brushSize_);  // jitter magnitude follows brush size
    int span = 2 * amt + 1;
    for (int y = 0; y < canvasH_; y++) {
        for (int x = 0; x < canvasW_; x++) {
            int sx = x + (rand() % span - amt);
            int sy = y + (rand() % span - amt);
            sx = std::max(0, std::min(canvasW_ - 1, sx));
            sy = std::max(0, std::min(canvasH_ - 1, sy));
            pixels_[y * canvasW_ + x] = src[sy * canvasW_ + sx];
        }
    }
    updateCanvasTexture();
}

// Undo / Redo

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

// Coordinate conversion

int TextureEditor::canvasOriginX() const {
    float areaW = screenW_ - PALETTE_W;
    return (int)(PALETTE_W + areaW / 2.0f - (canvasW_ * zoom_) / 2.0f - panX_ * zoom_);
}

int TextureEditor::canvasOriginY() const {
    float areaH = screenH_ - TOOLBAR_H - STATUS_H - FRAME_STRIP_H;
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

void TextureEditor::selHandlePos(int i, int& cx, int& cy) const {
    // 0=top-left 1=top-right 2=bottom-right 3=bottom-left
    // 4=top-mid  5=right-mid 6=bottom-mid  7=left-mid
    int x0 = selRect_.x, y0 = selRect_.y;
    int x1 = x0 + selRect_.w, y1 = y0 + selRect_.h;
    int xm = x0 + selRect_.w / 2, ym = y0 + selRect_.h / 2;
    const int hx[8] = {x0,x1,x1,x0,xm,x1,xm,x0};
    const int hy[8] = {y0,y0,y1,y1,y0,ym,y1,ym};
    if (i >= 0 && i < 8) { cx = hx[i]; cy = hy[i]; }
    else { cx = xm; cy = ym; }
}

int TextureEditor::hitTestSelHandle(int mx, int my) const {
    if (!selActive_) return -1;
    const int HR = 6;
    // Rotation handle: 20px above screen position of handle 4 (top-mid)
    {
        int hcx, hcy, sx, sy;
        selHandlePos(4, hcx, hcy);
        canvasToScreen(hcx, hcy, sx, sy);
        sy -= 20;
        if (abs(mx - sx) <= HR && abs(my - sy) <= HR) return 8;
    }
    for (int i = 0; i < 8; i++) {
        int hcx, hcy, sx, sy;
        selHandlePos(i, hcx, hcy);
        canvasToScreen(hcx, hcy, sx, sy);
        if (abs(mx - sx) <= HR && abs(my - sy) <= HR) return i;
    }
    // Interior
    int x0s, y0s, x1s, y1s;
    canvasToScreen(selRect_.x, selRect_.y, x0s, y0s);
    canvasToScreen(selRect_.x + selRect_.w, selRect_.y + selRect_.h, x1s, y1s);
    if (mx >= x0s && mx <= x1s && my >= y0s && my <= y1s) return 9;
    return -1;
}

int TextureEditor::paletteGridY() const {
    // Must match the layout in renderPalette():
    // TOOLBAR_H + 8 (COLOR label Y) + 16 (label height) + 40 (swatch) + 12 (gap) + 16 (PALETTE label)
    return TOOLBAR_H + 8 + 16 + 40 + 12 + 16;
}

// Color picker - HSV conversion

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

// File I/O

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
    // Map category index -> subdirectory
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
    if (!frames_.empty()) frames_[0] = pixels_;  // loaded image becomes frame 0
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

// Tool application

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

// Input handling

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
        Uint8 btn = remapButton(e.cbutton.button);
        if (btn == SDL_CONTROLLER_BUTTON_B) {
            if (config_.textEditing) config_.textEditing = false;
            else wantsExit_ = true;
        }
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_UP)
            config_.field = (config_.field - 1 + TOTAL) % TOTAL;
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            config_.field = (config_.field + 1) % TOTAL;
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_LEFT || btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            int dir = (btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 1 : -1;
            if (config_.field == 0) {
                config_.action = (config_.action == TexEditorConfig::NewImage)
                    ? TexEditorConfig::LoadImage : TexEditorConfig::NewImage;
            }
        }
        if (btn == SDL_CONTROLLER_BUTTON_A) {
            // Confirm - same as Enter
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

        if (key == SDLK_ESCAPE) {
            if (selActive_) { commitSelection(); return; }
            wantsExit_ = true; return;
        }

        // Undo / Redo
        if (ctrl && key == SDLK_z) { undo(); return; }
        if (ctrl && key == SDLK_y) { redo(); return; }

        // Save
        if (ctrl && key == SDLK_s) {
            doSave();
            return;
        }

        // Selection: stamp on Enter, select-all on Ctrl+A
        if (key == SDLK_RETURN && selActive_) { commitSelection(); return; }
        if (ctrl && key == SDLK_a) {
            commitSelection();
            selRect_ = {0, 0, canvasW_, canvasH_};
            selActive_ = true;
            currentTool_ = TexTool::Select;
            return;
        }

        // Commit floating selection when switching away from Select tool
        if (selActive_ && (key == SDLK_b || key == SDLK_p || key == SDLK_e ||
            key == SDLK_g || key == SDLK_l || key == SDLK_r || key == SDLK_o ||
            key == SDLK_i)) {
            commitSelection();
        }

        // Tool shortcuts
        if (key == SDLK_b || key == SDLK_p) currentTool_ = TexTool::Pen;
        if (key == SDLK_e) currentTool_ = TexTool::Eraser;
        if (key == SDLK_g) currentTool_ = TexTool::Fill;
        if (key == SDLK_l) currentTool_ = TexTool::Line;
        if (key == SDLK_r) currentTool_ = TexTool::Rect;
        if (key == SDLK_o) currentTool_ = TexTool::Circle;
        if (key == SDLK_i) currentTool_ = TexTool::Eyedropper;
        if (key == SDLK_v) currentTool_ = TexTool::Select;

        // Fill toggle (rect/circle) + one-shot canvas effects
        if (key == SDLK_f) fillShapes_ = !fillShapes_;
        if (key == SDLK_m) { pushUndo(); applyBlur(); }
        if (key == SDLK_n) { pushUndo(); applyRandomShift(); }

        // Animation: step frames with , and .
        if (key == SDLK_COMMA)  setFrame(curFrame_ - 1);
        if (key == SDLK_PERIOD) setFrame(curFrame_ + 1);

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

        // Frame strip band (full width, just above the status bar)
        int stripY = screenH_ - STATUS_H - FRAME_STRIP_H;
        if (my >= stripY && my < screenH_ - STATUS_H) {
            handleFrameStripClick(mx, my);
            return;
        }

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

        // Select tool: intercept before canvas-bounds check (handles may be near edge)
        if (currentTool_ == TexTool::Select && e.button.button == SDL_BUTTON_LEFT) {
            int h = hitTestSelHandle(mx, my);
            if (h >= 0 && h <= 7) {
                liftSelection();
                selDragMode_ = h + 2;
                int scx, scy; screenToCanvas(mx, my, scx, scy);
                selAnchorCX_ = scx; selAnchorCY_ = scy;
                selRectAtDragStart_ = selRect_;
            } else if (h == 8) {
                selDragMode_ = 10;
                selAnchorCX_ = mx; selAnchorCY_ = my;  // screen coords for angle calc
                selRectAtDragStart_ = selRect_;
                selAngleAtDragStart_ = selAngle_;
            } else if (h == 9) {
                liftSelection();
                selDragMode_ = 1;
                int scx, scy; screenToCanvas(mx, my, scx, scy);
                selAnchorCX_ = scx; selAnchorCY_ = scy;
                selRectAtDragStart_ = selRect_;
            } else {
                if (selActive_) commitSelection();
                int scx, scy; screenToCanvas(mx, my, scx, scy);
                scx = std::max(0, std::min(canvasW_ - 1, scx));
                scy = std::max(0, std::min(canvasH_ - 1, scy));
                selActive_ = true; selLifted_ = false;
                selAngle_  = 0.0f;
                selRect_   = {scx, scy, 0, 0};
                selDragMode_ = 11;
                selAnchorCX_ = scx; selAnchorCY_ = scy;
                selRectAtDragStart_ = selRect_;
            }
            return;
        }

        int cx, cy;
        if (screenToCanvas(mx, my, cx, cy)) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (playing_) { playing_ = false; updateCanvasTexture(); }  // editing stops playback
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
                // Middle click starts pan drag - handled in MOUSEMOTION
            }
        }
    }
    else if (e.type == SDL_MOUSEBUTTONUP) {
        if (currentTool_ == TexTool::Select && selDragMode_ != 0) {
            if (selDragMode_ == 11 && (selRect_.w <= 0 || selRect_.h <= 0)) {
                selActive_  = false;
                selRect_    = {0,0,0,0};
            }
            selDragMode_ = 0;
        }
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
                            drawRectPixels(shapeX0_, shapeY0_, cx, cy, currentColor_, fillShapes_);
                            break;
                        case TexTool::Circle: {
                            int rx = abs(cx - shapeX0_), ry = abs(cy - shapeY0_);
                            int ccx = (shapeX0_ + cx) / 2, ccy = (shapeY0_ + cy) / 2;
                            drawCirclePixels(ccx, ccy, rx / 2, ry / 2, currentColor_, fillShapes_);
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
        if (currentTool_ == TexTool::Select && selDragMode_ != 0) {
            int smx = e.motion.x, smy = e.motion.y;
            int cx2, cy2;
            screenToCanvas(smx, smy, cx2, cy2);
            cx2 = std::max(0, std::min(canvasW_ - 1, cx2));
            cy2 = std::max(0, std::min(canvasH_ - 1, cy2));
            if (selDragMode_ == 11) {
                selRect_.x = std::min(cx2, selAnchorCX_);
                selRect_.y = std::min(cy2, selAnchorCY_);
                selRect_.w = abs(cx2 - selAnchorCX_);
                selRect_.h = abs(cy2 - selAnchorCY_);
            } else if (selDragMode_ == 1) {
                selRect_.x = selRectAtDragStart_.x + (cx2 - selAnchorCX_);
                selRect_.y = selRectAtDragStart_.y + (cy2 - selAnchorCY_);
            } else if (selDragMode_ == 10) {
                int scx, scy;
                canvasToScreen(selRectAtDragStart_.x + selRectAtDragStart_.w / 2,
                               selRectAtDragStart_.y + selRectAtDragStart_.h / 2, scx, scy);
                selAngle_ = atan2f((float)(smy - scy), (float)(smx - scx)) * 180.0f / 3.14159265f + 90.0f;
            } else {
                int hi = selDragMode_ - 2;
                int rx0 = selRectAtDragStart_.x, ry0 = selRectAtDragStart_.y;
                int rx1 = rx0 + selRectAtDragStart_.w, ry1 = ry0 + selRectAtDragStart_.h;
                switch (hi) {
                    case 0: rx0 = cx2; ry0 = cy2; break;
                    case 1: rx1 = cx2; ry0 = cy2; break;
                    case 2: rx1 = cx2; ry1 = cy2; break;
                    case 3: rx0 = cx2; ry1 = cy2; break;
                    case 4: ry0 = cy2; break;
                    case 5: rx1 = cx2; break;
                    case 6: ry1 = cy2; break;
                    case 7: rx0 = cx2; break;
                    default: break;
                }
                if (rx1 <= rx0) rx1 = rx0 + 1;
                if (ry1 <= ry0) ry1 = ry0 + 1;
                selRect_ = {rx0, ry0, rx1 - rx0, ry1 - ry0};
            }
            return;
        }
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
        Uint8 btn = remapButton(e.cbutton.button);
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
            doSave();
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
            // Click outside - close
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
        Uint8 btn = remapButton(e.cbutton.button);
        if (btn == SDL_CONTROLLER_BUTTON_B || btn == SDL_CONTROLLER_BUTTON_A) {
            currentColor_ = hsvToRgb(hue_, sat_, val_, currentColor_.a);
            palette_[paletteIdx_] = currentColor_;
            state_ = TexEditorState::Editing;
        }
    }
}

// Helper for palette/toolbar clicks (not declared in header - internal)
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
    // Button layout MUST stay in sync with renderToolbar().
    const int y = 6, btnH = 36, tw = 46, gap = 3;
    auto hit = [&](int x, int w) { return mx >= x && mx < x + w && my >= y && my < y + btnH; };

    int x = PALETTE_W + 8;
    for (int i = 0; i < (int)TexTool::Count; i++) {
        if (hit(x, tw)) { currentTool_ = (TexTool)i; return; }
        x += tw + gap;
    }
    x += 6;  // separator
    if (hit(x, tw)) { fillShapes_ = !fillShapes_;          return; }  x += tw + gap;
    if (hit(x, tw)) { pushUndo(); applyBlur();             return; }  x += tw + gap;
    if (hit(x, tw)) { pushUndo(); applyRandomShift();      return; }  x += tw + gap;
    x += 6;
    if (hit(x, 54)) { doSave();                return; }  x += 54 + gap;
    if (hit(x, tw)) { undo();                              return; }  x += tw + gap;
    if (hit(x, tw)) { redo();                              return; }  x += tw + gap;
}

void TextureEditor::handleFrameStripClick(int mx, int my) {
    // Layout MUST stay in sync with renderFrameStrip().
    int stripY = screenH_ - STATUS_H - FRAME_STRIP_H;
    const int by = stripY + 6, bh = 26;
    auto hit = [&](int x, int w) { return mx >= x && mx < x + w && my >= by && my < by + bh; };

    int x = 8;
    if (hit(x, 56)) { addFrame(false); return; }  x += 60;
    if (hit(x, 40)) { addFrame(true);  return; }  x += 44;  // duplicate current
    if (hit(x, 40)) { deleteFrame();   return; }  x += 44;
    if (hit(x, 50)) {                              // play / stop
        playing_ = !playing_;
        if (playing_) { playFrame_ = curFrame_; frameTimer_ = 0; }
        else updateCanvasTexture();
        return;
    }
    x += 60;
    for (int i = 0; i < (int)frames_.size(); i++) {
        if (hit(x, 30)) {
            if (playing_) { playing_ = false; }
            setFrame(i);
            return;
        }
        x += 34;
        if (x > screenW_ - 40) break;
    }
}

// Update

void TextureEditor::update(float dt) {
    if (!active_) return;
    if (saveMessageTimer_ > 0) saveMessageTimer_ -= dt;

    // Marching ants animation
    if (selActive_) {
        selMarchTimer_ += dt;
        if (selMarchTimer_ >= 0.05f) {
            selMarchTimer_ = 0.0f;
            selMarchPhase_ = (selMarchPhase_ + 2) % 12;
        }
    }

    // Animation playback: cycle the displayed frame on a timer.
    if (playing_ && frames_.size() > 1) {
        frameTimer_ += dt;
        if (frameTimer_ >= frameDelay_) {
            frameTimer_ -= frameDelay_;
            playFrame_ = (playFrame_ + 1) % (int)frames_.size();
            blitToCanvasTex(frames_[playFrame_]);
        }
    }

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

// Rendering

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
    if (selActive_) renderSelectionOverlay();
    renderPreview();
    renderToolbar();
    renderPalette();
    renderFrameStrip();
    renderStatusBar();
    renderCursor();

    if (state_ == TexEditorState::ColorPicker) {
        renderColorPicker();
    }
}

void TextureEditor::renderSelectionOverlay() {
    if (!selActive_) return;

    // Draw floating pixels (lifted off canvas, shown at current transformed position)
    if (selLifted_ && !selOrigPixels_.empty()) {
        int dw = selRect_.w, dh = selRect_.h;
        if (dw > 0 && dh > 0) {
            float rad = selAngle_ * 3.14159265f / 180.0f;
            float cosA = cosf(rad), sinA = sinf(rad);
            float srcCX = selOrigW_ * 0.5f, srcCY = selOrigH_ * 0.5f;
            float scaleX = (float)selOrigW_ / dw, scaleY = (float)selOrigH_ / dh;
            int pxSz = std::max(1, (int)zoom_);
            for (int dy = 0; dy < dh; dy++) {
                for (int dx = 0; dx < dw; dx++) {
                    float lx = dx - dw * 0.5f + 0.5f, ly = dy - dh * 0.5f + 0.5f;
                    float sfx = lx * scaleX, sfy = ly * scaleY;
                    int sx2 = (int)floorf(cosA * sfx + sinA * sfy + srcCX);
                    int sy2 = (int)floorf(-sinA * sfx + cosA * sfy + srcCY);
                    if (sx2 < 0 || sx2 >= selOrigW_ || sy2 < 0 || sy2 >= selOrigH_) continue;
                    TexelColor c = selOrigPixels_[sy2 * selOrigW_ + sx2];
                    if (c.a == 0) continue;
                    int scx, scy;
                    canvasToScreen(selRect_.x + dx, selRect_.y + dy, scx, scy);
                    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
                    SDL_Rect pr = {scx, scy, pxSz, pxSz};
                    SDL_RenderFillRect(renderer_, &pr);
                }
            }
        }
    }

    // Marching-ants selection border
    int x0s, y0s, x1s, y1s;
    canvasToScreen(selRect_.x, selRect_.y, x0s, y0s);
    canvasToScreen(selRect_.x + selRect_.w, selRect_.y + selRect_.h, x1s, y1s);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_Rect outerR = {x0s - 1, y0s - 1, x1s - x0s + 2, y1s - y0s + 2};
    SDL_RenderDrawRect(renderer_, &outerR);

    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    const int dash = 6;
    int phase = selMarchPhase_, idx = 0;
    for (int x = x0s; x <= x1s; x++, idx++)
        if (((idx + phase) / dash) % 2 == 0) SDL_RenderDrawPoint(renderer_, x, y0s);
    for (int y = y0s + 1; y <= y1s; y++, idx++)
        if (((idx + phase) / dash) % 2 == 0) SDL_RenderDrawPoint(renderer_, x1s, y);
    for (int x = x1s - 1; x >= x0s; x--, idx++)
        if (((idx + phase) / dash) % 2 == 0) SDL_RenderDrawPoint(renderer_, x, y1s);
    for (int y = y1s - 1; y > y0s; y--, idx++)
        if (((idx + phase) / dash) % 2 == 0) SDL_RenderDrawPoint(renderer_, x0s, y);

    // 8 scale handles (white squares with black border)
    for (int i = 0; i < 8; i++) {
        int hcx, hcy, sx, sy;
        selHandlePos(i, hcx, hcy);
        canvasToScreen(hcx, hcy, sx, sy);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_Rect hr = {sx - 4, sy - 4, 8, 8};
        SDL_RenderFillRect(renderer_, &hr);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer_, &hr);
    }

    // Rotation handle: green square 20px above top-mid handle
    {
        int hcx, hcy, sx, sy;
        selHandlePos(4, hcx, hcy);
        canvasToScreen(hcx, hcy, sx, sy);
        int rsx = sx, rsy = sy - 20;
        SDL_SetRenderDrawColor(renderer_, 150, 150, 150, 180);
        SDL_RenderDrawLine(renderer_, sx, sy, rsx, rsy);
        SDL_SetRenderDrawColor(renderer_, 0, 200, 100, 255);
        SDL_Rect rh = {rsx - 5, rsy - 5, 10, 10};
        SDL_RenderFillRect(renderer_, &rh);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer_, &rh);
        if (fabsf(selAngle_) > 0.5f && ui_) {
            char abuf[16]; snprintf(abuf, sizeof(abuf), "%.0f", selAngle_);
            ui_->drawText(abuf, rsx + 8, rsy - 8, 11, UI::W98::White);
        }
    }
}

void TextureEditor::renderFrameStrip() {
    if (!ui_) return;
    int stripY = screenH_ - STATUS_H - FRAME_STRIP_H;

    // Silver band with a raised top edge.
    SDL_SetRenderDrawColor(renderer_, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
    SDL_Rect bg = {0, stripY, screenW_, FRAME_STRIP_H};
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, UI::W98::White.r, UI::W98::White.g, UI::W98::White.b, 255);
    SDL_RenderDrawLine(renderer_, 0, stripY, screenW_, stripY);

    const int by = stripY + 6, bh = 26;
    auto btn = [&](int x, int w, const char* label, bool active) {
        ui_->drawWin98Bevel(x, by, w, bh, !active);
        SDL_Color c = active ? UI::W98::Navy : UI::W98::Silver;
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
        SDL_Rect f = {x + 2, by + 2, w - 4, bh - 4};
        SDL_RenderFillRect(renderer_, &f);
        int lw = ui_->textWidth(label, 12);
        ui_->drawText(label, x + (w - lw) / 2, by + 6, 12, active ? UI::W98::White : UI::W98::Black);
    };

    ui_->drawText("FRAMES", 8, stripY + 2, 11, UI::W98::Navy);

    int x = 8;
    btn(x, 56, "+ Frame", false); x += 60;
    btn(x, 40, "Dup",     false); x += 44;
    btn(x, 40, "Del",     false); x += 44;
    btn(x, 50, playing_ ? "Stop" : "Play", playing_); x += 60;

    for (int i = 0; i < (int)frames_.size(); i++) {
        char num[8]; snprintf(num, sizeof(num), "%d", i + 1);
        btn(x, 30, num, i == curFrame_);
        if (playing_ && i == playFrame_) {  // playback position marker
            SDL_SetRenderDrawColor(renderer_, 255, 200, 0, 255);
            SDL_Rect m = {x, by - 3, 30, 2};
            SDL_RenderFillRect(renderer_, &m);
        }
        x += 34;
        if (x > screenW_ - 40) break;  // no horizontal scroll yet
    }

    char info[64];
    snprintf(info, sizeof(info), "%d frame%s   %.0f ms/frame",
             (int)frames_.size(), frames_.size() == 1 ? "" : "s", frameDelay_ * 1000.0f);
    ui_->drawText(info, 8, stripY + FRAME_STRIP_H - 14, 11, UI::W98::Shadow);
}

void TextureEditor::renderConfig() {
    if (!ui_) return;  // Win98 UI context is wired in by Game::init

    // Win98 desktop + centered window, matching the rest of the app.
    ui_->drawDesktop();

    const int pw = 560, ph = 420;
    const int px = (screenW_ - pw) / 2;
    const int py = (screenH_ - ph) / 2 - 20;
    ui_->drawWin98Window(px, py, pw, ph, "Sprite Editor");

    const int pad    = 16;
    const int lX     = px + pad;
    const int rW     = pw - pad * 2;
    const int rowH   = 26;
    const int rowGap = 5;
    const int labelW = 110;
    const int valX   = lX + labelW;
    const int valW   = rW - labelW;
    int cy = py + UI::W98::TitleH + 14;

    // Sunken row: label + value; selected row highlighted navy/white.
    auto row = [&](int idx, const char* label, const char* value, bool arrows) {
        bool sel = (config_.field == idx);
        ui_->drawWin98Bevel(lX, cy, rW, rowH, false);
        SDL_Color bg = sel ? UI::W98::Navy : UI::W98::Silver;
        SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
        SDL_Rect fill = {lX + 2, cy + 2, rW - 4, rowH - 4};
        SDL_RenderFillRect(renderer_, &fill);
        SDL_Color tc = sel ? UI::W98::White : UI::W98::Black;
        ui_->drawText(label, lX + 8, cy + 6, 13, tc);
        char buf[160];
        if (sel && arrows) snprintf(buf, sizeof(buf), "< %s >", value);
        else               snprintf(buf, sizeof(buf), "%s", value);
        ui_->drawText(buf, valX, cy + 6, 13, tc);
        cy += rowH + rowGap;
    };

    // Centered Win98 button (keyboard-selected; this screen is key-driven).
    auto button = [&](int idx, const char* label) {
        bool sel = (config_.field == idx);
        const int bw = 150, bh = 28;
        int bx = px + (pw - bw) / 2;
        ui_->drawWin98Bevel(bx, cy, bw, bh, !sel);  // sunken when selected
        SDL_Color bg = sel ? UI::W98::Navy : UI::W98::Silver;
        SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
        SDL_Rect fill = {bx + 2, cy + 2, bw - 4, bh - 4};
        SDL_RenderFillRect(renderer_, &fill);
        int tw = ui_->textWidth(label, 14);
        ui_->drawText(label, bx + (bw - tw) / 2, cy + 6, 14, sel ? UI::W98::White : UI::W98::Black);
        cy += bh + rowGap;
    };

    row(0, "Action", config_.action == TexEditorConfig::NewImage ? "New Image" : "Load Image", true);

    if (config_.action == TexEditorConfig::NewImage) {
        const char* tmplNames[] = {"Custom", "Tile 16x16", "Tile 32x32", "Tile 64x64",
                                   "Sprite 32x32", "Sprite 64x64", "Sprite 128x128", "Icon 16x16"};
        row(1, "Template", tmplNames[(int)config_.tmpl], true);
        char wBuf[32]; snprintf(wBuf, sizeof(wBuf), "%d", config_.canvasW); row(2, "Width",  wBuf, true);
        char hBuf[32]; snprintf(hBuf, sizeof(hBuf), "%d", config_.canvasH); row(3, "Height", hBuf, true);

        // Name row (4) with a sunken text field + caret while editing.
        {
            bool sel = (config_.field == 4);
            ui_->drawWin98Bevel(lX, cy, rW, rowH, false);
            SDL_Color bg = sel ? UI::W98::Navy : UI::W98::Silver;
            SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
            SDL_Rect fill = {lX + 2, cy + 2, rW - 4, rowH - 4};
            SDL_RenderFillRect(renderer_, &fill);
            ui_->drawText("Name", lX + 8, cy + 6, 13, sel ? UI::W98::White : UI::W98::Black);
            ui_->drawWin98TextField(valX, cy + 3, valW, rowH - 6, config_.name.c_str(),
                                    config_.textEditing, false, 0.0f);
            cy += rowH + rowGap;
        }

        char prevBuf[64];
        snprintf(prevBuf, sizeof(prevBuf), "Canvas: %d x %d pixels", config_.canvasW, config_.canvasH);
        ui_->drawText(prevBuf, lX, cy + 2, 11, UI::W98::Shadow);
        cy += 22;

        button(5, "CREATE");
        button(6, "CANCEL");
    } else {
        std::string fileLabel;
        if (loadFiles_.empty()) {
            fileLabel = "(no images found)";
        } else {
            std::string fname = loadFiles_[loadFileIdx_];
            auto slash = fname.find_last_of('/');
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            char fBuf[128];
            snprintf(fBuf, sizeof(fBuf), "%s (%d/%d)", fname.c_str(), loadFileIdx_ + 1, (int)loadFiles_.size());
            fileLabel = fBuf;
        }
        row(1, "File", fileLabel.c_str(), !loadFiles_.empty());
        if (!loadFiles_.empty() && loadFileIdx_ < (int)loadFiles_.size())
            ui_->drawText(loadFiles_[loadFileIdx_].c_str(), lX, cy + 2, 10, UI::W98::Shadow);
        cy += 22;

        button(2, "LOAD");
        button(3, "CANCEL");
    }

    ui_->drawWin98StatusBar(screenH_ - 24,
        "Arrow keys: navigate   Left/Right: change   Enter: confirm   Esc: cancel");
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
            // Simple visual feedback - draw bounding rect
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
    if (!ui_) return;

    // Silver toolbar strip with a shadow edge along the bottom.
    SDL_SetRenderDrawColor(renderer_, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
    SDL_Rect tbBg = {0, 0, screenW_, TOOLBAR_H};
    SDL_RenderFillRect(renderer_, &tbBg);
    SDL_SetRenderDrawColor(renderer_, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
    SDL_RenderDrawLine(renderer_, 0, TOOLBAR_H - 1, screenW_, TOOLBAR_H - 1);

    const int y = 6, btnH = 36, tw = 46, gap = 3;

    // A bevel button: sunken + navy/white when active, else raised silver/black.
    auto btn = [&](int x, int w, const char* label, bool active) {
        ui_->drawWin98Bevel(x, y, w, btnH, !active);
        SDL_Color bg = active ? UI::W98::Navy : UI::W98::Silver;
        SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
        SDL_Rect f = {x + 2, y + 2, w - 4, btnH - 4};
        SDL_RenderFillRect(renderer_, &f);
        int lw = ui_->textWidth(label, 12);
        ui_->drawText(label, x + (w - lw) / 2, y + (btnH - 14) / 2, 12,
                      active ? UI::W98::White : UI::W98::Black);
    };

    const char* toolNames[] = {"Pen", "Era", "Fill", "Line", "Rect", "Circ", "Eye", "Sel"};
    int x = PALETTE_W + 8;
    for (int i = 0; i < (int)TexTool::Count; i++) {
        btn(x, tw, toolNames[i], currentTool_ == (TexTool)i);
        x += tw + gap;
    }
    x += 6;
    btn(x, tw, "Fill", fillShapes_); x += tw + gap;  // rect/circle fill toggle
    btn(x, tw, "Blur",  false);      x += tw + gap;
    btn(x, tw, "Shift", false);      x += tw + gap;
    x += 6;
    btn(x, 54, "Save", false); x += 54 + gap;
    btn(x, tw, "Undo", false); x += tw + gap;
    btn(x, tw, "Redo", false); x += tw + gap;

    // Right-hand status: grid toggle + key hints.
    int rx = screenW_ - 170;
    ui_->drawText(showGrid_ ? "Grid: ON" : "Grid: OFF", rx, 9, 12,
                  showGrid_ ? UI::W98::Navy : UI::W98::Shadow);
    ui_->drawText("[C] Color  [F] Fill", rx, 27, 11, UI::W98::Shadow);
}

void TextureEditor::renderPalette() {
    auto& A = Assets::instance();
    TTF_Font* font = A.font(14);
    TTF_Font* fontSm = A.font(11);

    // Palette panel background (Win98 silver + sunken bevel)
    int palH = screenH_ - TOOLBAR_H - STATUS_H - FRAME_STRIP_H;
    SDL_SetRenderDrawColor(renderer_, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
    SDL_Rect palBg = {0, TOOLBAR_H, PALETTE_W, palH};
    SDL_RenderFillRect(renderer_, &palBg);
    if (ui_) ui_->drawWin98Bevel(0, TOOLBAR_H, PALETTE_W, palH, false);

    auto drawText = [&](const char* text, int x, int y, TTF_Font* f, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(f, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    SDL_Color dimCyan = UI::W98::Navy;  // section headers (kept var name)

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
    drawText(rgbBuf, 56, prevY + 2, fontSm, {0, 0, 0, 255});
    snprintf(rgbBuf, sizeof(rgbBuf), "A:%d  #%02X%02X%02X", currentColor_.a, currentColor_.r, currentColor_.g, currentColor_.b);
    drawText(rgbBuf, 56, prevY + 18, fontSm, {64, 64, 64, 255});

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
    drawText(brushBuf, 8, prevY, fontSm, {0, 0, 0, 255});
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

    // Panel (Win98 dialog window; title bar shows "Color Picker")
    int pw = 400, ph = 340;
    int px = (screenW_ - pw) / 2, py = (screenH_ - ph) / 2;
    if (ui_) ui_->drawWin98Window(px, py, pw, ph, "Color Picker");

    auto drawText = [&](const char* text, int x, int y, TTF_Font* f, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(f, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    // (title is drawn by the Win98 window title bar above)

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
    SDL_SetRenderDrawColor(renderer_, UI::W98::Silver.r, UI::W98::Silver.g, UI::W98::Silver.b, 255);
    SDL_Rect bg = {0, y, screenW_, STATUS_H};
    SDL_RenderFillRect(renderer_, &bg);
    // Raised top edge (white highlight over a shadow line)
    SDL_SetRenderDrawColor(renderer_, UI::W98::White.r, UI::W98::White.g, UI::W98::White.b, 255);
    SDL_RenderDrawLine(renderer_, 0, y, screenW_, y);

    auto drawText = [&](const char* text, int x, int y, SDL_Color c) {
        SDL_Surface* s = TTF_RenderText_Blended(font, text, c);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        SDL_Rect dst = {x, y, s->w, s->h};
        SDL_RenderCopy(renderer_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    };

    SDL_Color gray = UI::W98::Black;  // primary status text (kept var name)

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
        drawText(buf, 240, y + 6, UI::W98::Navy);
    }

    // Tool name
    const char* toolNames[] = {"Pen", "Eraser", "Fill", "Line", "Rect", "Circle", "Eyedropper", "Select"};
    snprintf(buf, sizeof(buf), "Tool: %s%s", toolNames[(int)currentTool_],
             fillShapes_ ? " (filled)" : "");
    drawText(buf, 400, y + 6, UI::W98::Navy);

    // Brush size
    snprintf(buf, sizeof(buf), "Brush: %d", brushSize_);
    drawText(buf, 560, y + 6, UI::W98::Navy);

    // Selection info
    if (selActive_) {
        snprintf(buf, sizeof(buf), "Sel: %d,%d  %dx%d%s",
                 selRect_.x, selRect_.y, selRect_.w, selRect_.h,
                 selLifted_ ? " [float]" : "");
        drawText(buf, 680, y + 6, {200, 150, 50, 255});
    }

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
