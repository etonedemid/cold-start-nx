#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <string>
#include <vector>
#include <cstdint>

// ---- Enums ----

enum class CsActorType : uint8_t {
    Player      = 0,
    Enemy       = 1,
    FreeSprite  = 2,
};

enum class CsEnemyType : uint8_t {
    Melee   = 0,
    Shooter = 1,
    Brute   = 2,
    Scout   = 3,
    Sniper  = 4,
    Gunner  = 5,
};

enum class CsEventType : uint8_t {
    Move          = 0,
    Rotate        = 1,
    Scale         = 2,
    Alpha         = 3,
    Flash         = 4,
    Wait          = 5,
    Dialog        = 6,
    PlaySFX       = 7,
    SpawnExplosion= 8,
    CameraMove    = 9,
    CameraZoom    = 10,
    CameraShake   = 11,
    ScreenFade    = 12,
    CinematicBars = 13,
    SetVisible    = 14,
    SetFrame      = 15,
    COUNT         = 16,
};

static inline const char* csEventTypeName(CsEventType t) {
    static const char* names[(int)CsEventType::COUNT] = {
        "Move","Rotate","Scale","Alpha","Flash","Wait",
        "Dialog","PlaySFX","Explosion","Cam Move","Cam Zoom",
        "Cam Shake","Screen Fade","Cine Bars","Set Visible","Set Frame",
    };
    int i = (int)t;
    if (i < 0 || i >= (int)CsEventType::COUNT) return "?";
    return names[i];
}

enum class CsEase : uint8_t {
    Linear    = 0,
    EaseIn    = 1,
    EaseOut   = 2,
    EaseInOut = 3,
    Instant   = 4,
};

// ---- Data structures ----

struct CsActor {
    uint32_t    id          = 0;
    std::string name;
    CsActorType type        = CsActorType::FreeSprite;
    CsEnemyType enemyType   = CsEnemyType::Melee;
    std::string spritePath; // used by FreeSprite
    float startX            = 320, startY = 240;
    float startRot          = 0;
    float startScaleX       = 1, startScaleY = 1;
    float startAlpha        = 1.0f;
    bool  startVisible      = true;
    bool  flipH             = false;
};

struct CsEvent {
    uint32_t    actorId   = 0;      // 0 = global/camera events
    CsEventType type      = CsEventType::Wait;
    float       startTime = 0.0f;
    float       duration  = 1.0f;
    CsEase      ease      = CsEase::Linear;

    // Move / CameraMove
    float fromX = 0, fromY = 0;
    float toX   = 0, toY   = 0;
    // Rotate
    float fromRot = 0, toRot = 0;
    // Scale
    float fromScaleX = 1, fromScaleY = 1;
    float toScaleX   = 1, toScaleY   = 1;
    // Alpha
    float fromAlpha = 1.0f, toAlpha = 1.0f;
    // Flash color (r/g/b 0-255 as floats)
    float flashR = 255, flashG = 255, flashB = 255;
    // Camera zoom
    float fromZoom = 1.0f, toZoom = 1.0f;
    // Camera shake
    float shakeStrength = 8.0f;
    // ScreenFade: true=fade to black, false=fade from black
    bool fadeToBlack = true;
    // CinematicBars: true=show, false=hide
    bool showBars = true;
    // SetVisible
    bool visible = true;
    // SetFrame
    int frame = 0;
    // SpawnExplosion position (world space)
    float explX = 0, explY = 0;
    // Dialog sequence id
    std::string dialogId;
    // SFX path
    std::string sfxPath;
};

struct CsDialogLine {
    std::string character;
    std::string portrait;   // sprite path, empty = no portrait
    std::string text;
    bool portraitLeft = true;
    std::string sfxPath;
};

struct CsDialogSeq {
    std::string id;
    std::vector<CsDialogLine> lines;
};

struct Cutscene {
    std::string id;
    bool blockInput = true;
    std::vector<CsActor>     actors;
    std::vector<CsEvent>     events;
    std::vector<CsDialogSeq> dialogs;

    float totalDuration() const;
    const CsActor*     findActor(uint32_t id) const;
    const CsDialogSeq* findDialog(const std::string& id) const;
};

struct CutsceneLibrary {
    std::vector<Cutscene> cutscenes;
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    void clear() { cutscenes.clear(); }
};

// ---- Runtime playback ----

struct CsActorState {
    float x = 0, y = 0;
    float rot = 0;
    float scaleX = 1, scaleY = 1;
    float alpha = 1.0f;
    bool  visible = true;
    int   frame   = 0;
    // Flash overlay
    float flashR = 255, flashG = 255, flashB = 255;
    float flashAmt = 0;  // 0=none, 1=full flash color
};

struct CsCamState {
    float x = 0, y = 0;
    float zoom   = 1.0f;
    float shakeX = 0, shakeY = 0;
    float shakeTimer    = 0;
    float shakeStrength = 0;
};

struct CsDialogPlayback {
    bool active          = false;
    const CsDialogSeq*  seq       = nullptr;
    int   lineIdx        = 0;
    float typeTimer      = 0;
    int   visibleChars   = 0;
    bool  lineComplete   = false; // all chars shown
    bool  done           = false; // all lines done
};

// Full runtime state
struct CutscenePlayback {
    bool  active   = false;
    float time     = 0;
    const Cutscene* cutscene = nullptr;

    std::vector<CsActorState> actorStates;  // parallel to cutscene->actors
    std::vector<SDL_Texture*> actorTex;     // loaded textures (parallel)
    CsCamState        cam;
    CsDialogPlayback  dialog;
    float cinematicBarsAmt = 0; // 0..1, target driven by events
    float screenFadeAlpha  = 0;
    bool  screenFadeToBlack = false;

    void start(const Cutscene* c, SDL_Renderer* r);
    void stop();
    bool isDone() const;

    // Advance time and evaluate all events
    void update(float dt, const CutsceneLibrary& lib);

    // Advance dialog (called when confirm is pressed)
    void advanceDialog();

    // Render world-space actors (call between background and UI render)
    void renderActors(SDL_Renderer* r,
                      float camX, float camY, float camZoom,
                      SDL_Texture* playerBody, SDL_Texture* playerLegs,
                      SDL_Texture* enemyTex) const;

    // Render full-screen overlays: cinematic bars, dialog, screen fade
    void renderOverlay(SDL_Renderer* r, int screenW, int screenH,
                       const CutsceneLibrary& lib) const;

private:
    void freeTextures();
    void loadTextures(SDL_Renderer* r);
    int  actorIdx(uint32_t id) const;
    CsActorState& stateFor(uint32_t id);
    float applyEase(float t, CsEase ease) const;
    void  applyEvent(const CsEvent& ev, float localT,
                     const CutsceneLibrary& lib);
    void  renderDialogLine(SDL_Renderer* r, const CsDialogLine& line,
                           int visibleChars, int screenW, int screenH,
                           const CutsceneLibrary& lib) const;
};
