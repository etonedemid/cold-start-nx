#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <string>
#include <vector>
#include <unordered_map>
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
    SpawnActor    = 16,  // make actor appear at position (uses actorId)
    DespawnActor  = 17,  // hide/remove actor (uses actorId)
    SetFlag       = 18,  // set a named script flag
    ChainCutscene = 19,  // trigger another cutscene by ID
    EndCutscene   = 20,  // immediately end this cutscene
    AdjustSignal  = 21,  // change the campaign SIGNAL meter by signalDelta
    BranchCutscene= 22,  // compare SIGNAL/route to a threshold, chain true/false
    SpawnEnemy    = 23,  // spawn an enemy at a world position
    SpawnPickup   = 24,  // spawn an upgrade pickup at a world position
    CameraRotate  = 25,  // rotate camera viewport (uses fromRot/toRot degrees)
    COUNT         = 26,
};

static inline const char* csEventTypeName(CsEventType t) {
    static const char* names[(int)CsEventType::COUNT] = {
        "Move","Rotate","Scale","Alpha","Flash","Wait",
        "Dialog","Play SFX","Explosion","Cam Move","Cam Zoom",
        "Shake","Screen Fade","Cine Bars","Set Visible","Set Frame",
        "Spawn Actor","Despawn","Set Flag","Chain CS","End CS",
        "Adj SIGNAL","Branch CS","Spawn Enemy","Spawn Pickup",
        "Cam Rotate",
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

    // SpawnActor: override position (uses actorId)
    float spawnX = 0, spawnY = 0;
    bool  spawnOverridePos = false;  // if true, move actor to spawnX/Y on spawn

    // SpawnEnemy: type (maps to EnemyType 0-5 = Melee/Shooter/Brute/Scout/Sniper/Gunner)
    // position reuses explX/explY
    uint8_t spawnEnemyTypeId  = 0;
    // SpawnPickup: type (maps to UpgradeType)
    // position reuses explX/explY
    uint8_t spawnPickupTypeId = 0;

    // SetFlag
    std::string flagName;
    bool        flagValue = true;

    // ChainCutscene (also reused as the "true" branch of BranchCutscene)
    std::string chainCsId;

    // AdjustSignal
    int signalDelta = 0;

    // BranchCutscene: compare branchVar to branchThreshold, chain chainCsId if
    // the comparison holds, else chainFalseId. branchVar 0=SIGNAL, 1=route.
    // branchCmp 0=">=", 1="<", 2="==".
    uint8_t     branchVar       = 0;
    uint8_t     branchCmp       = 0;
    int         branchThreshold = 50;
    std::string chainFalseId;
};

// Dialog branching choice
struct CsDialogChoice {
    std::string text;           // label shown to player
    std::string nextSeqId;      // dialog seq to jump to (empty = end dialog)
    std::string setFlag;        // flag name to set when chosen (empty = none)
    bool        setFlagValue = true;
};

struct CsDialogLine {
    std::string character;
    std::string portrait;   // sprite path, empty = no portrait
    std::string text;
    bool portraitLeft = true;
    std::string sfxPath;
    std::vector<CsDialogChoice> choices;  // if non-empty, show choice list after typing
};

struct CsDialogSeq {
    std::string id;
    std::vector<CsDialogLine> lines;
};

struct Cutscene {
    std::string id;
    bool blockInput = true;
    std::string chainOnEnd;  // cutscene ID to auto-chain when this one ends
    std::vector<CsActor>     actors;
    std::vector<CsEvent>     events;
    std::vector<CsDialogSeq> dialogs;

    float totalDuration() const;
    const CsActor*     findActor(uint32_t id) const;
    const CsDialogSeq* findDialog(const std::string& id) const;
};

// Renders the in-game dialog box (bar + portrait + speaker + typed text +
// choices) into the given screen rect. Shared by runtime playback and the
// cutscene editor's WYSIWYG preview so the two always match.
//   visibleChars < 0  -> show the whole line (no typewriter)
//   lineComplete      -> show choices / advance arrow
//   hoveredChoice     -> highlight that choice index (-1 = none)
// The box is drawn at the bottom of the rect [x, y, w, h].
void cutsceneRenderDialogBox(SDL_Renderer* r, int x, int y, int w, int h,
                             const CsDialogLine& line, int visibleChars,
                             bool lineComplete, int hoveredChoice);

struct CutsceneLibrary {
    std::vector<Cutscene> cutscenes;
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    void clear() { cutscenes.clear(); }
    const Cutscene* findById(const std::string& id) const {
        for (const auto& cs : cutscenes) if (cs.id == id) return &cs;
        return nullptr;
    }
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
    // Leg animation (Player actor only)
    float legAnimTimer = 0;
    int   legAnimFrame = 0;
    float legRotation  = 0;  // radians, movement direction
};

struct CsCamState {
    float x = 0, y = 0;
    float zoom     = 1.0f;
    float rotation = 0.0f;  // degrees, clockwise
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
    int   hoveredChoice  = -1;    // mouse-hovered choice index
};

// Full runtime state
struct CutscenePlayback {
    bool  active   = false;
    float time     = 0;
    const Cutscene* cutscene = nullptr;

    std::vector<CsActorState> actorStates;  // parallel to cutscene->actors
    std::vector<SDL_Texture*> actorTex;     // loaded textures (parallel)
    CsCamState        cam;
    // True once a CameraMove/CameraZoom event has driven the camera.
    // Until then the game anchors actor rendering to the live game camera,
    // so actors placed in the map editor line up with the world.
    bool camDriven = false;
    CsDialogPlayback  dialog;
    float cinematicBarsAmt = 0; // 0..1, target driven by events
    float screenFadeAlpha  = 0;
    bool  screenFadeToBlack = false;

    // Script flags set by SetFlag events or dialog choices
    std::unordered_map<std::string, bool> scriptFlags;

    // Chain request: set by ChainCutscene/EndCutscene, consumed by caller
    std::string pendingChainId;
    bool        pendingEnd = false;

    // Pending one-shot spawns; drained by the game each frame.
    struct PendingExplosion { float x, y; };
    struct PendingEnemy     { float x, y; uint8_t typeId; };
    struct PendingPickup    { float x, y; uint8_t typeId; };
    std::vector<PendingExplosion> pendingExplosions;
    std::vector<PendingEnemy>     pendingEnemies;
    std::vector<PendingPickup>    pendingPickups;

    // SIGNAL delta accumulated by AdjustSignal events; drained by the game.
    int         pendingSignalDelta = 0;
    // Campaign values the game pushes in before update() so BranchCutscene can
    // evaluate against live SIGNAL / route.
    int         extSignal = 50;
    int         extRoute  = 0;

    void start(const Cutscene* c, SDL_Renderer* r);
    void stop();
    bool isDone() const;

    // Advance time and evaluate all events
    void update(float dt, const CutsceneLibrary& lib);

    // Advance dialog (called when confirm is pressed); returns true if a choice was accepted
    bool advanceDialog();

    // Select a dialog choice (0-3); call when choices are shown
    void selectDialogChoice(int idx, const CutsceneLibrary& lib);

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
