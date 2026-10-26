#pragma once
#include "cutscene.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <functional>
#include <string>
#include <vector>

// Cutscene editor bottom panel. Default height; the top edge can be dragged
// to resize between CS_PANEL_MIN_H and roughly 2/3 of the screen.
static constexpr int CS_EDITOR_PANEL_H = 300;
static constexpr int CS_PANEL_MIN_H    = 220;

// World radius of the on-canvas rotation handle ring
static constexpr float CS_ROT_R = 56.0f;
// World half-size that corresponds to scale 1.0 for the scale gizmo box
static constexpr float CS_SCALE_REF = 48.0f;

// What a canvas click is currently armed to set (world-space position pick)
enum class CsPickMode : uint8_t {
    None = 0,
    ActorStart,     // selected actor startX/Y
    EventFrom,      // selected event fromX/Y
    EventTo,        // selected event toX/Y
    EventExplosion, // selected event explX/Y
    EventSpawnPos,  // selected event spawnX/Y
};

class CutsceneEditor {
public:
    void init(SDL_Renderer* r, int screenW, int screenH, UI::Context* ui);
    void shutdown();

    // Handle one SDL event. mouseWorldX/Y are the map-world coordinates of
    // the mouse for this event (computed by the map editor, which owns the
    // camera), zoom is the map editor zoom (for hit tolerances on canvas).
    // Returns true when the event was fully consumed by the cutscene editor
    // and the map editor must ignore it.
    bool handleEvent(SDL_Event& e, float mouseWorldX, float mouseWorldY, float zoom);
    void update(float dt);
    // Render the bottom panel. panelY is the top edge of the panel.
    void render(SDL_Renderer* r, int screenW, int screenH, int panelY);

    void setLibrary(CutsceneLibrary* lib) { lib_ = lib; clampSelection(); }
    CutsceneLibrary* library() const { return lib_; }

    bool isActive() const { return active_; }
    void setActive(bool v);

    // Current panel height (user-resizable)
    int  panelHeight() const { return panelH_; }

    // Close request (X button in the panel header); consumed by the map editor
    bool wantsClose() const { return wantsClose_; }
    void clearWantsClose()  { wantsClose_ = false; }

    // Scrub time for the map-canvas preview overlay
    float scrubTime() const { return scrubTime_; }
    const Cutscene* currentCutscene() const;

    // Preview actor states at the scrub time (parallel to cutscene actors)
    const CsActorState* actorStateAt(int actorIdx) const;
    int  actorCount() const;
    int  selectedActor() const { return selectedActor_; }
    int  selectedEvent() const { return selectedEvent_; }
    // True while the full-screen dialog editor is open (the map editor hides
    // its own toolbar/palette/panels so they cannot eat the modal's clicks).
    bool dialogModalOpen() const { return dialogModal_; }
    bool pickArmed() const { return pickMode_ != CsPickMode::None; }
    const char* pickHint() const;
    bool textEditing() const { return focusedField_ >= 0; }

    // On-canvas rotation handle. Returns true if one should be shown for the
    // current selection and fills the world center plus the angle(s) in
    // degrees. twoKnobs=true for a selected Rotate event (from + to angles);
    // otherwise it is the selected actor's start rotation (single knob).
    bool rotationHandle(float& cx, float& cy, float& a0, float& a1, bool& twoKnobs) const;

    // Scale gizmo for a selected Scale event. Fills world center plus the
    // from/to scale (x,y). Returns false if no Scale event is selected.
    bool scaleHandle(float& cx, float& cy, float& fsx, float& fsy,
                     float& tsx, float& tsy) const;

    // True when the last consumed click must also be hidden from all
    // immediate-mode UI widgets this frame (menu clicks, canvas picks).
    // The map editor reads and clears this right after handleEvent().
    bool takeUiClickSwallow() {
        bool v = swallowUiClick_;
        swallowUiClick_ = false;
        return v;
    }
    // True if a drag is currently active (motion events must not be suppressed).
    bool isDragging() const {
        return draggingEvent_ || resizingEvent_ || draggingScrub_ ||
               resizingPanel_ || dragActorIdx_ >= 0 ||
               rotTarget_ != RotTarget::None || scaleTarget_ != ScaleTarget::None;
    }

private:
    bool active_ = false;
    bool wantsClose_ = false;
    SDL_Renderer* r_  = nullptr;
    UI::Context*  ui_ = nullptr;
    int screenW_ = 1280, screenH_ = 720;

    CutsceneLibrary* lib_ = nullptr;

    // Selection
    int selectedCutscene_   = -1;
    int selectedActor_      = -1;
    int selectedEvent_      = -1;
    int selectedDialogSeq_  = -1;
    int selectedDialogLine_ = -1;

    // Timeline view
    float timelineStart_ = 0;       // left edge time (seconds)
    float timelineScale_ = 80.0f;   // pixels per second
    float scrubTime_     = 0;
    bool  playing_       = false;

    // Preview states at scrubTime_ (parallel to current cutscene actors)
    std::vector<CsActorState> previewStates_;

    // Panel geometry
    int panelY_ = 0;
    int panelH_ = CS_EDITOR_PANEL_H;
    bool resizingPanel_ = false;
    int  resizeGrabDY_  = 0;

    // Column layout (recomputed every frame)
    int listX_ = 0,  listW_ = 150;
    int actorX_ = 0, actorW_ = 150;
    int tlX_ = 0,    tlW_ = 0;
    int propsX_ = 0, propsW_ = 290;
    int colY_ = 0,   colH_ = 0;     // content area inside panel
    static constexpr int HEADER_H = 20;  // panel title strip
    static constexpr int HINT_H   = 16;  // bottom hint strip
    static constexpr int ROW_H    = 20;  // list rows
    static constexpr int TL_TOOL_H  = 24; // timeline toolbar
    static constexpr int TL_RULER_H = 16;
    static constexpr int TL_ROW_H   = 22;

    // List scrolling
    int listScroll_  = 0;
    int actorScroll_ = 0;
    int propsScroll_ = 0;
    int propsContentH_ = 0;  // measured during render
    int tlRowScroll_ = 0;    // timeline/actor row scroll (rows)

    // Timeline drag state
    bool  draggingEvent_  = false;
    bool  resizingEvent_  = false;
    bool  draggingScrub_  = false;
    float dragEventOrigT_ = 0;
    float resizeOrigDur_  = 0;
    int   dragStartPx_    = 0;

    // Canvas interaction
    CsPickMode pickMode_ = CsPickMode::None;
    int   dragActorIdx_  = -1;   // actor marker being dragged on the canvas
    float dragActorOrigX_ = 0, dragActorOrigY_ = 0;
    float dragWorldStartX_ = 0, dragWorldStartY_ = 0;
    bool  swallowUiClick_ = false;

    // Rotation-handle drag target (set when a knob is grabbed on the canvas)
    enum class RotTarget { None, ActorStart, EventFrom, EventTo };
    RotTarget rotTarget_ = RotTarget::None;
    // Scale-gizmo drag target
    enum class ScaleTarget { None, From, To };
    ScaleTarget scaleTarget_ = ScaleTarget::None;

    // Timeline snapping (Shift temporarily inverts)
    bool snapOn_ = true;

    // Transient status message shown in the hint bar
    std::string statusMsg_;
    float       statusMsgT_ = 0;

    // Two-step delete confirmation for the cutscene Del button
    float deleteArmT_ = 0;

    // Vertical widget clipping while drawing the scrollable inspector
    bool clipActive_ = false;
    int  clipY0_ = 0, clipY1_ = 0;

    // Inline text-field editing (immediate mode)
    int         focusedField_ = -1;   // field id, -1 = none
    std::string editBuf_;
    bool        commitPending_ = false;
    bool        cancelPending_ = false;
    bool        numericField_  = false;
    std::function<void(const std::string&)> focusedCommit_; // re-registered each frame

    // Full-screen dialog editor with live preview
    bool  dialogModal_   = false;
    bool  dlgPlaying_    = false;  // animate the typewriter in the preview
    float dlgPreviewT_   = 0;      // typewriter time
    int   dlgHoverChoice_= -1;

    // Popup menus
    bool showEventMenu_ = false;
    bool showActorMenu_ = false;
    int  menuX_ = 0, menuY_ = 0, menuW_ = 0, menuH_ = 0; // computed at render

    // Event clipboard
    bool    hasClipboard_ = false;
    CsEvent clipboard_;

    // Unique id counters
    uint32_t nextActorId_ = 1;
    uint32_t nextCsId_    = 1;

    // ---- helpers ----
    Cutscene* current();
    void clampSelection();
    void setStatus(const char* msg);
    void recomputePreview();
    float snapTime(float t) const;
    float viewDuration() const { return tlW_ > 0 ? tlW_ / timelineScale_ : 1.0f; }

    float pxToTime(int px) const { return timelineStart_ + (float)(px - tlX_) / timelineScale_; }
    int   timeToPx(float t) const { return tlX_ + (int)((t - timelineStart_) * timelineScale_); }

    void computeLayout(int screenW, int screenH, int panelY);
    SDL_Color eventColor(CsEventType t) const;

    // Mutations
    void addCutscene();
    void deleteCutscene(int idx);
    void addActor(CsActorType type, CsEnemyType enemyType = CsEnemyType::Melee);
    void deleteActor(int idx);
    void addEvent(CsEventType type);
    void deleteEvent(int idx);
    void addDialogSeq();
    void deleteDialogSeq(int idx);

    // Drawing primitives
    void fillRect(int x, int y, int w, int h, SDL_Color c);
    void drawRect(int x, int y, int w, int h, SDL_Color c);
    void drawLine(int x0, int y0, int x1, int y1, SDL_Color c);
    void drawText(const char* text, int x, int y, SDL_Color c, int size = 12);
    void drawBevel(int x, int y, int w, int h, bool raised = true);
    bool button(int id, const char* label, int x, int y, int w, int h, bool sel = false);

    // Immediate-mode editable fields. Each id must be unique and stable.
    void textField(int id, int x, int y, int w, int h, const std::string& value,
                   std::function<void(const std::string&)> commit);
    void floatField(int id, int x, int y, int w, int h, float* v,
                    float step, float lo, float hi, const char* fmt = "%.2f");
    void intField(int id, int x, int y, int w, int h, int* v, int step, int lo, int hi);
    bool fieldFocused(int id) const { return focusedField_ == id; }
    void focusField(int id, const std::string& current, bool numeric);
    void releaseFocus(bool commit);

    // Dialog editor modal
    void openDialogModal(int seqIdx);
    void closeDialogModal();
    void updateDialogModal(float dt);
    void renderDialogModal();
    bool handleDialogModalEvent(SDL_Event& e);

    // Sub-panels
    void renderHeader(int screenW);
    void renderCutsceneList();
    void renderActorList();
    void renderTimeline();
    void renderInspector();
    void renderHintBar();
    void renderMenus();

    // Inspector sections (cy = running y cursor, returns updated cy)
    int  inspectScene(int x, int y, int w);
    int  inspectActor(int x, int y, int w, CsActor& a);
    int  inspectEvent(int x, int y, int w, CsEvent& ev);
    int  inspectDialogs(int x, int y, int w);

    // Input sub-handlers (return true if consumed)
    bool handleTimelineClick(int mx, int my, bool rightClick);
    void handleMotion(int mx, int my);
    void handleRelease();
    bool handleKey(SDL_Event& e);
    bool handleCanvasClick(float wx, float wy, float zoom);
};
