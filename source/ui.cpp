// ─── ui.cpp ─── Immediate-mode UI implementation ────────────────────────────
#include "ui.h"
#include "assets.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace UI {

// ═════════════════════════════════════════════════════════════════════════════
//  Text Cache
// ═════════════════════════════════════════════════════════════════════════════

const TextCache::Entry& TextCache::get(const char* text, int size) {
    static const Entry empty{};
    if (!renderer_ || !text || text[0] == '\0') return empty;

    Key key{text, size};
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.frameUsed = frame_;
        return it->second;
    }

    // Evict if cache is too large
    if (cache_.size() >= MAX_ENTRIES) evict(300);

    TTF_Font* f = Assets::instance().font(size);
    if (!f) return empty;

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderText_Blended(f, text, white);
    if (!surf) return empty;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    if (!tex) { SDL_FreeSurface(surf); return empty; }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    Entry entry;
    entry.texture   = tex;
    entry.width     = surf->w;
    entry.height    = surf->h;
    entry.frameUsed = frame_;
    SDL_FreeSurface(surf);

    auto [ins, _] = cache_.emplace(key, entry);
    return ins->second;
}

void TextCache::evict(uint32_t maxAge) {
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (frame_ - it->second.frameUsed > maxAge) {
            SDL_DestroyTexture(it->second.texture);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void TextCache::clear() {
    for (auto& [k, e] : cache_) {
        if (e.texture) SDL_DestroyTexture(e.texture);
    }
    cache_.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Input Glyphs
// ═════════════════════════════════════════════════════════════════════════════

const char* glyphLabel(Action action, bool gamepad) {
#ifdef __SWITCH__
    // Nintendo Switch glyphs (matches Joy-Con / Pro Controller layout)
    switch (action) {
        case Action::Confirm:  return "A";
        case Action::Back:     return "B";
        case Action::Left:     return "\xE2\x97\x80";   // ◀
        case Action::Right:    return "\xE2\x96\xB6";   // ▶
        case Action::Navigate: return "D-Pad";
        case Action::Pause:    return "+";
        case Action::Tab:      return "Y";
        case Action::Bomb:     return "X";
    }
#else
    if (gamepad) {
        switch (action) {
            case Action::Confirm:  return "A";
            case Action::Back:     return "B";
            case Action::Left:     return "LB";
            case Action::Right:    return "RB";
            case Action::Navigate: return "D-Pad";
            case Action::Pause:    return "Start";
            case Action::Tab:      return "Y";
            case Action::Bomb:     return "X";
        }
    } else {
        switch (action) {
            case Action::Confirm:  return "Enter";
            case Action::Back:     return "Esc";
            case Action::Left:     return "<-";
            case Action::Right:    return "->";
            case Action::Navigate: return "Arrows";
            case Action::Pause:    return "Esc";
            case Action::Tab:      return "Tab";
            case Action::Bomb:     return "Q";
        }
    }
#endif
    return "?";
}

std::string buildHintBar(const HintPair* pairs, int count, bool gamepad) {
    std::string result;
    for (int i = 0; i < count; i++) {
        if (i > 0) result += "     ";
        result += "[";
        result += glyphLabel(pairs[i].action, gamepad);
        result += "] ";
        result += pairs[i].desc;
    }
    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Context
// ═════════════════════════════════════════════════════════════════════════════

void Context::init(SDL_Renderer* r) {
    renderer = r;
    textCache.init(r);
    std::memset(itemAnim, 0, sizeof(itemAnim));
}

void Context::beginFrame(float frameDt, bool gamepad) {
    dt = frameDt;
    usingGamepad = gamepad;
    textCache.beginFrame();

    // Save previous frame hover for click-through in handleInput
    prevHoveredItem = hoveredItem;

    // Reset per-frame hit-test
    hoveredItem = -1;
    clickedItem = -1;

    // Get mouse state (SDL logical coordinates thanks to RenderSetLogicalSize)
    int rawX, rawY;
    Uint32 buttons = SDL_GetMouseState(&rawX, &rawY);
    // Convert to logical coords via SDL renderer mapping
    float fx, fy;
    SDL_RenderWindowToLogical(renderer, rawX, rawY, &fx, &fy);
    mouseX = (int)fx;
    mouseY = (int)fy;

    bool wasDown = mouseDown;
    mouseDown = (buttons & SDL_BUTTON_LMASK) != 0;
    mouseClicked  = mouseDown && !wasDown;
    mouseReleased = !mouseDown && wasDown;

    // Evict old text cache entries occasionally (every ~10 seconds)
    static int evictCounter = 0;
    if (++evictCounter > 600) { textCache.evict(600); evictCounter = 0; }
}

void Context::endFrame() {
    // nothing for now
}

void Context::shutdown() {
    textCache.clear();
}

// ─── Drawing Helpers ────────────────────────────────────────────────────────

void Context::drawText(const char* text, int x, int y, int size, SDL_Color color) {
    const auto& e = textCache.get(text, size);
    if (!e.texture) return;
    SDL_SetTextureColorMod(e.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(e.texture, color.a);
    SDL_Rect dst = {x, y, e.width, e.height};
    SDL_RenderCopy(renderer, e.texture, nullptr, &dst);
}

void Context::drawTextCentered(const char* text, int y, int size, SDL_Color color) {
    const auto& e = textCache.get(text, size);
    if (!e.texture) return;
    SDL_SetTextureColorMod(e.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(e.texture, color.a);
    SDL_Rect dst = {SCREEN_W / 2 - e.width / 2, y, e.width, e.height};
    SDL_RenderCopy(renderer, e.texture, nullptr, &dst);
}

void Context::drawTextRight(const char* text, int x, int y, int size, SDL_Color color) {
    const auto& e = textCache.get(text, size);
    if (!e.texture) return;
    SDL_SetTextureColorMod(e.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(e.texture, color.a);
    SDL_Rect dst = {x - e.width, y, e.width, e.height};
    SDL_RenderCopy(renderer, e.texture, nullptr, &dst);
}

int Context::textWidth(const char* text, int size) {
    const auto& e = textCache.get(text, size);
    return e.width;
}

int Context::textHeight(int size) {
    // Approximate using a reference character
    const auto& e = textCache.get("Ag", size);
    return e.height;
}

void Context::drawPanel(int x, int y, int w, int h, SDL_Color bg, SDL_Color border) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_Rect panel = {x, y, w, h};
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &panel);
}

void Context::drawDarkOverlay(uint8_t alpha, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer, &full);
}

void Context::drawSeparator(int cx, int y, int halfWidth, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect sep = {cx - halfWidth, y, halfWidth * 2, 1};
    SDL_RenderFillRect(renderer, &sep);
}

// ─── Interactive Elements ───────────────────────────────────────────────────

static float smoothstep(float t) {
    t = std::max(0.f, std::min(1.f, t));
    return t * t * (3.f - 2.f * t);
}

bool Context::menuItem(int idx, const char* label, int cx, int y, int w, int h,
                       SDL_Color accent, bool sel, int fontSize, int selFontSize) {
    // Bounds for hit testing
    int rx = cx - w / 2;
    int ry = y - 4;
    int rw = w;
    int rh = h;

    // Mouse hover detection
    bool hovered = pointInRect(mouseX, mouseY, rx, ry, rw, rh);
    if (hovered) hoveredItem = idx;

    bool activated = false;
    if (hovered && mouseClicked) {
        clickedItem = idx;
        activated = true;
    }

    // Animate focus (smooth interpolation)
    bool focused = sel || hovered;
    float& anim = (idx >= 0 && idx < MAX_ANIM_ITEMS) ? itemAnim[idx] : itemAnim[0];
    float target = focused ? 1.0f : 0.0f;
    float speed = 12.0f; // animation speed
    anim += (target - anim) * std::min(1.0f, speed * dt);
    if (fabsf(anim - target) < 0.01f) anim = target;

    float a = smoothstep(anim);

    // Background highlight
    if (a > 0.01f) {
        Uint8 bgAlpha = (Uint8)(a * 30);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, bgAlpha);
        SDL_Rect bg = {rx, ry, rw, rh};
        SDL_RenderFillRect(renderer, &bg);

        // Left accent bar (slides in)
        int barW = (int)(3.0f * a);
        if (barW > 0) {
            Uint8 barAlpha = (Uint8)(180 * a);
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, barAlpha);
            SDL_Rect bar = {rx, ry, barW, rh};
            SDL_RenderFillRect(renderer, &bar);
        }
    }

    // Text color: interpolate between gray and accent
    SDL_Color c;
    c.r = (Uint8)(Color::Gray.r + (accent.r - Color::Gray.r) * a);
    c.g = (Uint8)(Color::Gray.g + (accent.g - Color::Gray.g) * a);
    c.b = (Uint8)(Color::Gray.b + (accent.b - Color::Gray.b) * a);
    c.a = 255;

    // Font size: interpolate
    int fs = fontSize + (int)((selFontSize - fontSize) * a);

    // Slight indent when focused
    int indent = (int)(8.0f * a);

    // Draw label
    const auto& entry = textCache.get(label, fs);
    if (entry.texture) {
        SDL_SetTextureColorMod(entry.texture, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(entry.texture, c.a);
        SDL_Rect dst = {cx - entry.width / 2 + indent, y + (h - entry.height) / 2,
                        entry.width, entry.height};
        SDL_RenderCopy(renderer, entry.texture, nullptr, &dst);
    }

    return activated;
}

int Context::sliderRow(int idx, const char* label, const char* value,
                       int cx, int y, int w, int h,
                       SDL_Color accent, bool sel, bool leftKey, bool rightKey) {
    int rx = cx - w / 2;
    int ry = y - 4;
    int rw = w;
    int rh = h;

    // Mouse hover
    bool hovered = pointInRect(mouseX, mouseY, rx, ry, rw, rh);
    if (hovered) hoveredItem = idx;

    // Animate
    bool focused = sel || hovered;
    float& anim = (idx >= 0 && idx < MAX_ANIM_ITEMS) ? itemAnim[idx] : itemAnim[0];
    float target = focused ? 1.0f : 0.0f;
    anim += (target - anim) * std::min(1.0f, 12.0f * dt);
    if (fabsf(anim - target) < 0.01f) anim = target;
    float a = smoothstep(anim);

    // Background
    if (a > 0.01f) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, (Uint8)(a * 25));
        SDL_Rect bg = {rx, ry, rw, rh};
        SDL_RenderFillRect(renderer, &bg);

        int barW = (int)(3.0f * a);
        if (barW > 0) {
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, (Uint8)(180 * a));
            SDL_Rect bar = {rx, ry, barW, rh};
            SDL_RenderFillRect(renderer, &bar);
        }
    }

    // Color
    SDL_Color c;
    c.r = (Uint8)(Color::Gray.r + (accent.r - Color::Gray.r) * a);
    c.g = (Uint8)(Color::Gray.g + (accent.g - Color::Gray.g) * a);
    c.b = (Uint8)(Color::Gray.b + (accent.b - Color::Gray.b) * a);
    c.a = 255;

    int fs = 20 + (int)(2 * a);

    // Build display text
    char display[128];
    if (focused) {
        snprintf(display, sizeof(display), "<  %s  %s  >", label, value);
    } else {
        snprintf(display, sizeof(display), "%s  %s", label, value);
    }

    int indent = (int)(6.0f * a);
    drawTextCentered(display, y + (h - textHeight(fs)) / 2 + indent / 3, fs, c);

    // Click on left/right arrows
    int delta = 0;
    if (leftKey)  delta = -1;
    if (rightKey) delta = +1;

    if (hovered && mouseClicked) {
        // Left half = decrease, right half = increase
        if (mouseX < cx) delta = -1;
        else delta = +1;
    }

    return delta;
}

void Context::drawHintBar(const HintPair* pairs, int count, int y) {
    std::string text = buildHintBar(pairs, count, usingGamepad);
    drawTextCentered(text.c_str(), y, 13, Color::HintGray);
}

bool Context::pointInRect(int px, int py, int rx, int ry, int rw, int rh) const {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

} // namespace UI
