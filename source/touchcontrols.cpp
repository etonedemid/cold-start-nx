// ─── touchcontrols.cpp ─── On-screen virtual gamepad ────────────────────────
#include "touchcontrols.h"
#include "constants.h"
#include <cmath>
#include <algorithm>

// ── Geometry helpers ──────────────────────────────────────────────────────────

static void fillCircle(SDL_Renderer* r, int cx, int cy, int rad) {
    for (int dy = -rad; dy <= rad; dy++) {
        int hw = (int)sqrtf((float)(rad*rad - dy*dy));
        SDL_RenderDrawLine(r, cx-hw, cy+dy, cx+hw, cy+dy);
    }
}

static void drawCircleOutline(SDL_Renderer* r, int cx, int cy, int rad, int thick) {
    for (int t = 0; t < thick; t++) {
        int R = rad - t;
        if (R <= 0) break;
        for (int dy = -R; dy <= R; dy++) {
            int hw = (int)sqrtf((float)(R*R - dy*dy));
            SDL_RenderDrawPoint(r, cx-hw, cy+dy);
            SDL_RenderDrawPoint(r, cx+hw, cy+dy);
        }
    }
}

// ── TouchControls ─────────────────────────────────────────────────────────────

void TouchControls::compute() {
    moveStick = {0, 0};
    aimStick  = {0, 0};
    fire = melee = bomb = parry = pauseBtn = false;
    anyActive = false;
    // togglePressed is NOT cleared here - caller clears it after reading

    const float us     = lastScale_;
    const float stickR = 75.f * us;
    const float aimR   = 65.f * us;

    for (int i = 0; i < MAX_FINGERS; i++) {
        const auto& f = fingers_[i];
        if (!f.active) continue;
        anyActive = true;
        switch (f.zone) {
            case ZONE_LEFT: {
                float dx = f.cx - f.ox, dy = f.cy - f.oy;
                float len = sqrtf(dx*dx + dy*dy);
                if (len > 1.f) {
                    float norm = std::min(len, stickR) / stickR;
                    moveStick = {dx/len*norm, dy/len*norm};
                }
                break;
            }
            case ZONE_RIGHT: {
                float dx = f.cx - f.ox, dy = f.cy - f.oy;
                float len = sqrtf(dx*dx + dy*dy);
                if (len > 1.f) {
                    float norm = std::min(len, aimR) / aimR;
                    aimStick = {dx/len*norm, dy/len*norm};
                }
                break;
            }
            case ZONE_FIRE:  fire     = true; break;
            case ZONE_MELEE: melee    = true; break;
            case ZONE_BOMB:  bomb     = true; break;
            case ZONE_PARRY: parry    = true; break;
            case ZONE_PAUSE: pauseBtn = true; break;
            default: break;
        }
    }
}

int TouchControls::findSlot(SDL_FingerID id) const {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (fingers_[i].active && fingers_[i].id == id) return i;
    return -1;
}

int TouchControls::allocSlot(SDL_FingerID id, float lx, float ly) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (!fingers_[i].active) {
            fingers_[i] = {id, true, lx, ly, lx, ly, ZONE_NONE};
            return i;
        }
    }
    return -1;
}

TouchControls::Zone TouchControls::hitZone(float lx, float ly, float us) const {
    if (inCircle(lx,ly, btnPause(us).x,btnPause(us).y,btnPause(us).r)) return ZONE_PAUSE;
    if (inCircle(lx,ly, btnFire (us).x,btnFire (us).y,btnFire (us).r)) return ZONE_FIRE;
    if (inCircle(lx,ly, btnParry(us).x,btnParry(us).y,btnParry(us).r)) return ZONE_PARRY;
    if (inCircle(lx,ly, btnMelee(us).x,btnMelee(us).y,btnMelee(us).r)) return ZONE_MELEE;
    if (inCircle(lx,ly, btnBomb (us).x,btnBomb (us).y,btnBomb (us).r)) return ZONE_BOMB;
    // joystick zones: lower 70% of screen, split left / right
    if (ly > SCREEN_H * 0.3f) {
        if (lx < SCREEN_W * 0.45f) return ZONE_LEFT;
        if (lx < SCREEN_W * 0.88f) return ZONE_RIGHT;
    }
    return ZONE_NONE;
}

bool TouchControls::handleEvent(const SDL_Event& e, float uiScale) {
    if (e.type != SDL_FINGERDOWN && e.type != SDL_FINGERMOTION && e.type != SDL_FINGERUP)
        return false;

    lastScale_ = uiScale;
    const float us = uiScale;
    const float lx = e.tfinger.x * SCREEN_W;
    const float ly = e.tfinger.y * SCREEN_H;

    if (e.type == SDL_FINGERDOWN) {
        // Toggle button is always active regardless of visible_
        if (inCircle(lx, ly, btnToggle(us).x, btnToggle(us).y, btnToggle(us).r)) {
            togglePressed = true;
            return true;
        }
        // If overlay is hidden, pass all other touches through to the UI
        if (!visible_) return false;

        int slot = allocSlot(e.tfinger.fingerId, lx, ly);
        if (slot < 0) return true;  // no free slot - consume to avoid ghost inputs

        Zone z = hitZone(lx, ly, us);
        fingers_[slot].zone = z;

        if (z == ZONE_NONE) {
            // Not in any control zone - release slot and let the UI handle it
            fingers_[slot].active = false;
            return false;
        }

        if (z == ZONE_LEFT && leftSlot_ < 0) {
            leftSlot_ = slot;
            fingers_[slot].ox = std::clamp(lx, 60.f, SCREEN_W * 0.42f);
            fingers_[slot].oy = std::clamp(ly, SCREEN_H * 0.35f, SCREEN_H - 40.f);
        } else if (z == ZONE_RIGHT && rightSlot_ < 0) {
            rightSlot_ = slot;
            fingers_[slot].ox = std::clamp(lx, SCREEN_W * 0.50f, SCREEN_W * 0.90f);
            fingers_[slot].oy = std::clamp(ly, SCREEN_H * 0.35f, SCREEN_H - 40.f);
        } else if (z == ZONE_LEFT && leftSlot_ >= 0 && rightSlot_ < 0) {
            // Second finger in left zone → treat as right stick
            fingers_[slot].zone = ZONE_RIGHT;
            rightSlot_ = slot;
            fingers_[slot].ox = std::clamp(lx, SCREEN_W * 0.50f, SCREEN_W * 0.90f);
            fingers_[slot].oy = std::clamp(ly, SCREEN_H * 0.35f, SCREEN_H - 40.f);
        }
        return true;
    }

    if (e.type == SDL_FINGERMOTION) {
        int slot = findSlot(e.tfinger.fingerId);
        if (slot < 0) return false;
        fingers_[slot].cx = lx;
        fingers_[slot].cy = ly;
        return true;
    }

    if (e.type == SDL_FINGERUP) {
        int slot = findSlot(e.tfinger.fingerId);
        if (slot < 0) return false;
        if (slot == leftSlot_)  leftSlot_  = -1;
        if (slot == rightSlot_) rightSlot_ = -1;
        fingers_[slot].active = false;
        fingers_[slot].zone   = ZONE_NONE;
        return true;
    }
    return false;
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void TouchControls::render(SDL_Renderer* r, float uiScale) const {
    lastScale_ = uiScale;
    const float us = uiScale;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // ── Toggle button (always visible, green = on, amber = off) ──────────────
    {
        auto b = btnToggle(us);
        int cx = (int)b.x, cy = (int)b.y, cr = (int)b.r;
        // Background fill
        if (visible_) SDL_SetRenderDrawColor(r,  30, 60, 30, 110);
        else          SDL_SetRenderDrawColor(r,  60, 40, 20, 110);
        fillCircle(r, cx, cy, cr);
        // Outline
        if (visible_) SDL_SetRenderDrawColor(r,  80, 200,  80, 180);
        else          SDL_SetRenderDrawColor(r, 200, 140,  40, 180);
        drawCircleOutline(r, cx, cy, cr, 2);
        // Hamburger icon (3 bars)
        int bw = (int)(cr * 0.62f);
        int step = (int)(cr * 0.35f);
        SDL_SetRenderDrawColor(r, 240, 240, 240, 210);
        SDL_RenderDrawLine(r, cx - bw, cy - step, cx + bw, cy - step);
        SDL_RenderDrawLine(r, cx - bw, cy,        cx + bw, cy);
        SDL_RenderDrawLine(r, cx - bw, cy + step, cx + bw, cy + step);
    }

    if (!visible_) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        return;
    }

    // ── Floating joysticks ──────────────────────────────────────────────────
    auto drawStick = [&](int slot, float maxR, float knobR) {
        if (slot < 0) return;
        const auto& f = fingers_[slot];
        if (!f.active) return;
        int ox = (int)f.ox, oy = (int)f.oy;
        float dx = f.cx-f.ox, dy = f.cy-f.oy;
        float len = sqrtf(dx*dx+dy*dy);
        float clamp = std::min(len, maxR);
        int kx = ox + (len > 0 ? (int)(dx/len*clamp) : 0);
        int ky = oy + (len > 0 ? (int)(dy/len*clamp) : 0);
        SDL_SetRenderDrawColor(r, 255,255,255,35);
        fillCircle(r, ox, oy, (int)maxR);
        SDL_SetRenderDrawColor(r, 255,255,255,80);
        drawCircleOutline(r, ox, oy, (int)maxR, 2);
        SDL_SetRenderDrawColor(r, 220,220,255,160);
        fillCircle(r, kx, ky, (int)knobR);
        SDL_SetRenderDrawColor(r, 255,255,255,200);
        drawCircleOutline(r, kx, ky, (int)knobR, 2);
    };
    drawStick(leftSlot_,  75.f*us, 30.f*us);
    drawStick(rightSlot_, 65.f*us, 25.f*us);

    // Idle stick hints (when no finger on that zone)
    auto drawIdleStick = [&](float hx, float hy, float maxR) {
        SDL_SetRenderDrawColor(r, 255,255,255,18);
        fillCircle(r, (int)hx, (int)hy, (int)maxR);
        SDL_SetRenderDrawColor(r, 255,255,255,45);
        drawCircleOutline(r, (int)hx, (int)hy, (int)maxR, 2);
    };
    if (leftSlot_  < 0) drawIdleStick(180.f*us, 570.f*us, 75.f*us);
    if (rightSlot_ < 0) drawIdleStick(850.f*us, 570.f*us, 65.f*us);

    // ── Action buttons ──────────────────────────────────────────────────────
    // Labels adapt to context: in gameplay they fire actions; in menus they
    // act as gamepad face buttons (FIRE=A/Confirm, BOMB=B/Back, MELEE=Tab).
    struct { BtnPos pos; bool pressed; SDL_Color c; const char* lbl; } btns[] = {
        { btnFire (us), fire,  {255,120, 80,255}, "A"    },
        { btnParry(us), parry, { 80,180,255,255}, "LB"   },
        { btnMelee(us), melee, {255,200, 40,255}, "E"    },
        { btnBomb (us), bomb,  {100,200,255,255}, "Bmb"  },
        { btnPause(us), pauseBtn, {160,160,160,255}, "||"},
    };

    for (auto& b : btns) {
        int cx = (int)b.pos.x, cy = (int)b.pos.y, cr = (int)b.pos.r;
        Uint8 alpha = b.pressed ? 200 : 100;
        SDL_SetRenderDrawColor(r, b.c.r/3, b.c.g/3, b.c.b/3, alpha);
        fillCircle(r, cx, cy, cr);
        SDL_SetRenderDrawColor(r, b.c.r, b.c.g, b.c.b, b.pressed ? 255 : 160);
        drawCircleOutline(r, cx, cy, cr, 2);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
