#pragma once
// ─── ui.h ─── Immediate-mode UI system with text caching, mouse/touch,
//              and automatic input glyph support ─────────────────────────────
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <functional>
#include "constants.h"

namespace UI {

// ─── Color Palette ──────────────────────────────────────────────────────────
namespace Color {
    constexpr SDL_Color Cyan        = {0, 255, 228, 255};
    constexpr SDL_Color White       = {255, 255, 255, 255};
    constexpr SDL_Color Gray        = {120, 120, 130, 255};
    constexpr SDL_Color DimCyan     = {0, 140, 130, 255};
    constexpr SDL_Color Green       = {50, 255, 150, 255};
    constexpr SDL_Color Red         = {255, 100, 100, 255};
    constexpr SDL_Color DeepRed     = {255, 60, 60, 255};
    constexpr SDL_Color Yellow      = {255, 220, 60, 255};
    constexpr SDL_Color Orange      = {255, 160, 80, 255};
    constexpr SDL_Color Blue        = {80, 200, 255, 255};
    constexpr SDL_Color Purple      = {200, 140, 255, 255};
    constexpr SDL_Color Lavender    = {180, 180, 255, 255};
    constexpr SDL_Color BgDark      = {8, 8, 12, 235};
    constexpr SDL_Color PanelBg     = {10, 12, 24, 240};
    constexpr SDL_Color HintGray    = {80, 80, 90, 255};
    constexpr SDL_Color Transparent = {0, 0, 0, 0};
}

// ─── Text Cache ─────────────────────────────────────────────────────────────
// Caches rendered text as white SDL_Textures.  Color is applied per-draw via
// SDL_SetTextureColorMod / AlphaMod, so the same cached texture can be reused
// across multiple colors without re-rasterizing.
class TextCache {
public:
    struct Entry {
        SDL_Texture* texture = nullptr;
        int width  = 0;
        int height = 0;
        uint32_t frameUsed = 0;
    };

    void init(SDL_Renderer* r) { renderer_ = r; }

    // Returns a cached entry.  The texture is white; caller must color-mod it.
    const Entry& get(const char* text, int size);

    void beginFrame()                  { frame_++; }
    void evict(uint32_t maxAge = 600); // discard entries unused for N frames
    void clear();

private:
    struct Key {
        std::string text;
        int size;
        bool operator==(const Key& o) const { return text == o.text && size == o.size; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            size_t h = std::hash<std::string>()(k.text);
            h ^= std::hash<int>()(k.size) << 16;
            return h;
        }
    };

    SDL_Renderer* renderer_ = nullptr;
    uint32_t frame_ = 0;
    std::unordered_map<Key, Entry, KeyHash> cache_;
    static constexpr size_t MAX_ENTRIES = 512;
};

// ─── Input Glyphs ───────────────────────────────────────────────────────────
enum class Action { Confirm, Back, Left, Right, Navigate, Pause, Tab, Bomb };

// Returns a short label for the action based on the current input device.
// Examples: gamepad → "A",  keyboard → "Enter"
const char* glyphLabel(Action action, bool gamepad);

// Builds a full hint bar string like "[A] Select    [D-Pad] Navigate    [B] Back"
// `pairs` is a list of (Action, description) pairs.
struct HintPair { Action action; const char* desc; };
std::string buildHintBar(const HintPair* pairs, int count, bool gamepad);

// ─── Context ────────────────────────────────────────────────────────────────
// Holds per-frame UI state.  One instance lives inside Game.
struct Context {
    SDL_Renderer* renderer = nullptr;
    TextCache     textCache;

    // Mouse / touch state — updated each frame from SDL events
    int  mouseX  = 0;
    int  mouseY  = 0;
    bool mouseClicked  = false;  // true the frame button was pressed
    bool mouseReleased = false;  // true the frame button was released
    bool mouseDown     = false;  // held
    bool touchActive   = false;  // a finger is touching

    // Last input device
    bool usingGamepad = false;

    // Delta time
    float dt = 0;

    // Per-item animation (up to 64 items per screen)
    static constexpr int MAX_ANIM_ITEMS = 64;
    float itemAnim[MAX_ANIM_ITEMS] = {};

    // Hit-test result for this frame
    int  hoveredItem = -1;
    int  prevHoveredItem = -1; // from previous frame's render
    int  clickedItem = -1;  // set when mouse clicked on a hovered item

    void init(SDL_Renderer* r);
    void beginFrame(float dt, bool gamepad);
    void endFrame();
    void shutdown();

    // ── Drawing Helpers ─────────────────────────────────────────────────────

    // Cached text rendering (fast)
    void drawText(const char* text, int x, int y, int size, SDL_Color color);
    void drawTextCentered(const char* text, int y, int size, SDL_Color color);
    void drawTextRight(const char* text, int x, int y, int size, SDL_Color color);
    int  textWidth(const char* text, int size);
    int  textHeight(int size);

    // Panels
    void drawPanel(int x, int y, int w, int h,
                   SDL_Color bg = Color::PanelBg,
                   SDL_Color border = {0, 180, 160, 80});
    void drawDarkOverlay(uint8_t alpha = 200,
                         uint8_t r = 4, uint8_t g = 6, uint8_t b = 14);

    // Separator line
    void drawSeparator(int cx, int y, int halfWidth,
                       SDL_Color color = {0, 180, 160, 60});

    // ── Interactive Elements ────────────────────────────────────────────────

    // Menu item with automatic hit-test, hover animation, selection indicator.
    // Returns true if the item was activated (click or confirmInput).
    // `idx` = item index; `sel` = currently focused by keyboard/gamepad;
    // `accent` = highlight color.
    bool menuItem(int idx, const char* label, int cx, int y, int w, int h,
                  SDL_Color accent, bool sel, int fontSize = 20, int selFontSize = 24);

    // Slider row: label + "< value >" with left/right adjustable.
    // Returns delta: -1, 0, or +1 if the user clicked the arrows or used keys.
    int  sliderRow(int idx, const char* label, const char* value,
                   int cx, int y, int w, int h,
                   SDL_Color accent, bool sel, bool leftKey, bool rightKey);

    // Hint bar at bottom of screen with automatic glyphs
    void drawHintBar(const HintPair* pairs, int count, int y = SCREEN_H - 36);

    // ── Touch helpers ───────────────────────────────────────────────────────
    bool pointInRect(int px, int py, int rx, int ry, int rw, int rh) const;
};

} // namespace UI
