#pragma once
// ─── touchcontrols.h ─── On-screen virtual gamepad ──────────────────────────
#include "vec2.h"
#include <SDL2/SDL.h>

struct TouchControls {
    // ── Visibility ──────────────────────────────────────────────────────────
    bool visible_ = false;   // toggled by the top-left button; auto-set true in gameplay

    // ── Output (read by input.cpp after compute()) ───────────────────────────
    Vec2 moveStick  = {0, 0};   // normalized −1..1
    Vec2 aimStick   = {0, 0};   // normalized −1..1
    bool fire       = false;
    bool melee      = false;
    bool bomb       = false;
    bool parry      = false;
    bool pauseBtn   = false;
    bool togglePressed = false; // true for one frame when toggle tapped; caller clears it
    bool anyActive  = false;    // true when any game-control zone is being held

    // Recompute all output fields from the current finger state.
    // Call this after processing all SDL events each frame (replaces beginFrame).
    void compute();

    // Feed a raw SDL finger event.  Returns true if the event was consumed.
    // uiScale should match config_.uiScale so hit zones scale correctly.
    bool handleEvent(const SDL_Event& e, float uiScale);

    // Render the overlay (toggle button always; full controls only when visible_).
    void render(SDL_Renderer* r, float uiScale) const;

private:
    enum Zone : int8_t {
        ZONE_NONE = -1,
        ZONE_LEFT,      // movement joystick
        ZONE_RIGHT,     // aim joystick
        ZONE_FIRE,
        ZONE_MELEE,
        ZONE_BOMB,
        ZONE_PARRY,
        ZONE_PAUSE,
    };

    struct Finger {
        SDL_FingerID id     = 0;
        bool         active = false;
        float        ox     = 0, oy = 0;   // origin (logical px, clamped to zone)
        float        cx     = 0, cy = 0;   // current position
        Zone         zone   = ZONE_NONE;
    };

    static constexpr int MAX_FINGERS = 10;
    Finger fingers_[MAX_FINGERS] = {};

    int leftSlot_  = -1;
    int rightSlot_ = -1;

    int  findSlot(SDL_FingerID id) const;
    int  allocSlot(SDL_FingerID id, float lx, float ly);
    Zone hitZone(float lx, float ly, float uiScale) const;

    // Button geometry helpers (logical px, scaled by uiScale)
    struct BtnPos { float x, y, r; };
    BtnPos btnToggle(float s) const { return {  32*s,  32*s, 22*s}; }
    BtnPos btnFire  (float s) const { return {1190*s, 480*s, 42*s}; }
    BtnPos btnParry (float s) const { return {1100*s, 480*s, 38*s}; }
    BtnPos btnMelee (float s) const { return {1190*s, 570*s, 38*s}; }
    BtnPos btnBomb  (float s) const { return {1100*s, 570*s, 38*s}; }
    BtnPos btnPause (float s) const { return { 640*s,  32*s, 26*s}; }

    static bool inCircle(float px, float py, float cx, float cy, float r) {
        float dx = px-cx, dy = py-cy;
        return dx*dx+dy*dy <= r*r;
    }

    mutable float lastScale_ = 1.0f;
};
