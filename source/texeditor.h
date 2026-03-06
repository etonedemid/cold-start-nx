#pragma once
// ─── texeditor.h ─── Pixel Art / Sprite Editor for COLD START ───────────────
// A built-in Aseprite-style pixel art tool for creating map tiles, characters,
// props, and UI sprites.  Supports canvas editing, color picker, palette,
// drawing tools, undo/redo, grid overlay, and PNG save/load.
// ─────────────────────────────────────────────────────────────────────────────
#include "assets.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>

// ── Pixel color ─────────────────────────────────────────────────────────────
struct TexelColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const TexelColor& o) const { return r==o.r && g==o.g && b==o.b && a==o.a; }
    bool operator!=(const TexelColor& o) const { return !(*this == o); }
};

// ── Drawing tools ───────────────────────────────────────────────────────────
enum class TexTool {
    Pen,
    Eraser,
    Fill,
    Line,
    Rect,
    Circle,
    Eyedropper,
    Count
};

// ── Editor sub-state ────────────────────────────────────────────────────────
enum class TexEditorState {
    Config,     // new/load dialog
    Editing,    // main canvas editing
    ColorPicker // color picker overlay
};

// ── Template presets for quick canvas creation ──────────────────────────────
enum class TexTemplate {
    Custom,       // user-defined size
    Tile16,       // 16×16  — small tile
    Tile32,       // 32×32  — standard tile
    Tile64,       // 64×64  — large tile (COLD START default)
    Sprite32,     // 32×32  — small sprite
    Sprite64,     // 64×64  — character sprite
    Sprite128,    // 128×128 — large sprite
    Icon16,       // 16×16  — UI icon
    Count
};

// ── Config screen data ─────────────────────────────────────────────────────
struct TexEditorConfig {
    enum Action { NewImage, LoadImage } action = NewImage;
    int canvasW = 32;
    int canvasH = 32;
    TexTemplate tmpl = TexTemplate::Tile32;
    std::string name = "sprite";
    std::string loadPath;
    int field = 0;      // selected field in config UI
    bool textEditing = false;
    int gpCharIdx = 0;  // gamepad char palette index
};

// ── Main class ──────────────────────────────────────────────────────────────
class TextureEditor {
public:
    bool init(SDL_Renderer* renderer, int screenW, int screenH);
    void shutdown();

    void handleInput(const SDL_Event& e);
    void update(float dt);
    void render();

    void showConfig();          // show config screen (new/load dialog)
    bool isInConfig() const { return state_ == TexEditorState::Config; }
    bool wantsExit() const { return wantsExit_; }
    void setActive(bool a) { active_ = a; }
    bool isActive() const { return active_; }

    // Mod-save handshake
    bool wantsModSave()     const { return wantsModSave_; }
    void clearWantsModSave()      { wantsModSave_ = false; }
    std::string pendingFileName() const { return fileName_; }
    void performModSave(const std::string& modFolder, int cat);

    TexEditorConfig& config() { return config_; }
    const TexEditorConfig& config() const { return config_; }

private:
    // ── Canvas ──────────────────────────────────────────────────────────
    int canvasW_ = 32, canvasH_ = 32;
    std::vector<TexelColor> pixels_;      // canvasW_ * canvasH_
    SDL_Texture* canvasTex_ = nullptr;    // GPU texture mirror of pixels_

    void newCanvas(int w, int h);
    void updateCanvasTexture();
    void setPixel(int x, int y, TexelColor c);
    TexelColor getPixel(int x, int y) const;
    void floodFill(int x, int y, TexelColor target, TexelColor fill);
    void drawLinePixels(int x0, int y0, int x1, int y1, TexelColor c);
    void drawRectPixels(int x0, int y0, int x1, int y1, TexelColor c, bool filled);
    void drawCirclePixels(int cx, int cy, int rx, int ry, TexelColor c, bool filled);

    // ── Undo / Redo ─────────────────────────────────────────────────────
    static constexpr int MAX_UNDO = 50;
    std::vector<std::vector<TexelColor>> undoStack_;
    std::vector<std::vector<TexelColor>> redoStack_;
    void pushUndo();
    void undo();
    void redo();

    // ── Tools ───────────────────────────────────────────────────────────
    TexTool currentTool_ = TexTool::Pen;
    TexelColor currentColor_ = {255, 255, 255, 255};
    int brushSize_ = 1;             // brush radius (1=single pixel, 2=3x3, etc.)
    bool drawing_ = false;          // mouse button held
    int lastPx_ = -1, lastPy_ = -1; // last painted pixel (for pen stroke)
    bool shapeStarted_ = false;     // line/rect/circle first point set
    int shapeX0_ = 0, shapeY0_ = 0;

    // ── Palette ─────────────────────────────────────────────────────────
    static constexpr int PALETTE_SIZE = 32;
    TexelColor palette_[PALETTE_SIZE];
    int paletteIdx_ = 0;
    void initDefaultPalette();

    // ── Color picker (HSV) ──────────────────────────────────────────────
    float hue_ = 0, sat_ = 1, val_ = 1;
    int cpDragMode_ = 0;           // 0=none 1=sv square 2=hue bar 3=alpha bar
    static TexelColor hsvToRgb(float h, float s, float v, uint8_t a = 255);
    static void rgbToHsv(TexelColor c, float& h, float& s, float& v);

    // ── View ────────────────────────────────────────────────────────────
    float zoom_ = 8.0f;            // pixels on screen per canvas pixel
    float panX_ = 0, panY_ = 0;   // canvas scroll offset (in canvas pixels)
    bool showGrid_ = true;

    // ── Coordinate conversion ───────────────────────────────────────────
    //  canvasOriginX/Y: screen position of canvas pixel (0,0)
    int canvasOriginX() const;
    int canvasOriginY() const;
    bool screenToCanvas(int sx, int sy, int& cx, int& cy) const;
    void canvasToScreen(int cx, int cy, int& sx, int& sy) const;

    // ── Layout constants ────────────────────────────────────────────────
    static constexpr int TOOLBAR_H   = 48;
    static constexpr int PALETTE_W   = 200;
    static constexpr int STATUS_H    = 28;

    // ── Layout helpers ──────────────────────────────────────────────────
    int paletteGridY() const;       // Y position of palette color grid

    // ── Rendering sub-functions ─────────────────────────────────────────
    void renderConfig();
    void renderCanvas();
    void renderGrid();
    void renderToolbar();
    void renderPalette();
    void renderColorPicker();
    void renderStatusBar();
    void renderPreview();
    void renderCursor();
    void renderBrushSettings();

    // ── File I/O ────────────────────────────────────────────────────────
    bool saveImage(const std::string& path);
    bool loadImage(const std::string& path);
    std::string savePath_;
    std::string fileName_ = "sprite";
    std::string saveMessage_;
    float saveMessageTimer_ = 0;

    // ── File browser (for Load) ─────────────────────────────────────────
    std::vector<std::string> loadFiles_;
    int loadFileIdx_ = 0;
    void scanImageFiles();

    // ── State ───────────────────────────────────────────────────────────
    TexEditorState state_ = TexEditorState::Config;
    TexEditorConfig config_;
    bool active_       = false;
    bool wantsExit_    = false;
    bool wantsModSave_ = false;

    // ── SDL ─────────────────────────────────────────────────────────────
    SDL_Renderer* renderer_ = nullptr;
    int screenW_ = 1280, screenH_ = 720;

    // ── Gamepad virtual cursor ──────────────────────────────────────────
    float cursorX_ = 640, cursorY_ = 360;
    bool useGamepadCursor_ = false;

    // ── Input helpers ───────────────────────────────────────────────────
    void handleConfigInput(const SDL_Event& e);
    void handleEditingInput(const SDL_Event& e);
    void handleColorPickerInput(const SDL_Event& e);
    void handlePaletteClick(int mx, int my);
    void handleToolbarClick(int mx, int my);
    void applyToolAtPixel(int cx, int cy);
    void applyBrushAt(int cx, int cy, TexelColor c);  // apply color in brush-sized area
};
