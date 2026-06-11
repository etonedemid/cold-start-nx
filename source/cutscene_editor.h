#pragma once
#include "cutscene.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

// Height of the bottom editor panel
static constexpr int CS_EDITOR_PANEL_H = 310;

// Column widths within the panel
static constexpr int CS_LIST_W  = 140;  // cutscene list (far left)
static constexpr int CS_ACTOR_W = 155;  // actor list
static constexpr int CS_PROPS_W = 330;  // properties (far right)

// Which text field is currently being edited
enum class CsEditField : int {
    None = -1,
    CsId = 0, CsChainOnEnd,
    ActorName, ActorSprite,
    ActorStartX, ActorStartY, ActorStartRot,
    ActorStartAlpha, ActorStartScaleX, ActorStartScaleY,
    EvStartTime, EvDuration,
    EvFromX, EvFromY, EvToX, EvToY,
    EvFromRot, EvToRot,
    EvFromSX, EvFromSY, EvToSX, EvToSY,
    EvFromAlpha, EvToAlpha,
    EvFlashR, EvFlashG, EvFlashB,
    EvFromZoom, EvToZoom,
    EvShake, EvExplX, EvExplY,
    EvDialogId, EvSfxPath,
    EvFlagName, EvChainId,
    EvSpawnX, EvSpawnY,
    DlgSeqId,
    DlgLineChr, DlgLinePortrait, DlgLineText, DlgLineSfx,
    ChoiceText0, ChoiceText1, ChoiceText2, ChoiceText3,
    ChoiceNext0, ChoiceNext1, ChoiceNext2, ChoiceNext3,
    ChoiceFlag0, ChoiceFlag1, ChoiceFlag2, ChoiceFlag3,
    COUNT
};

// What the props panel is showing
enum class CsPropsMode { Scene, Actor, Event, DialogSeq, DialogLine };

class CutsceneEditor {
public:
    void init(SDL_Renderer* r, int screenW, int screenH, UI::Context* ui);
    void shutdown();

    void handleEvent(SDL_Event& e, float editorZoom, float camWorldX, float camWorldY);
    void update(float dt);
    void render(SDL_Renderer* r, int screenW, int panelY);

    void setLibrary(CutsceneLibrary* lib) { lib_ = lib; }
    CutsceneLibrary* library() const { return lib_; }

    bool isActive() const { return active_; }
    void setActive(bool v) { active_ = v; }

    float scrubTime() const { return scrubTime_; }
    const Cutscene* currentCutscene() const;
    const CsActorState* actorStateAt(int actorIdx) const;
    int actorCount() const;

private:
    bool active_    = false;
    SDL_Renderer*  r_  = nullptr;
    UI::Context*   ui_ = nullptr;
    int screenW_    = 1280;

    CutsceneLibrary* lib_ = nullptr;

    // --- Selection state ---
    int selectedCutscene_    = -1;
    int selectedActor_       = -1;
    int selectedEvent_       = -1;
    int selectedDialogSeq_   = -1;
    int selectedDialogLine_  = -1;
    int selectedChoice_      = -1;
    CsPropsMode propsMode_   = CsPropsMode::Scene;

    // Timeline
    float timelineStart_  = 0;
    float timelineScale_  = 80.0f; // pixels per second
    float scrubTime_      = 0;
    bool  playing_        = false;

    // Preview actor states at scrubTime_
    std::vector<CsActorState> previewStates_;

    // Event drag/resize
    bool  draggingEvent_  = false;
    float dragEventOrigT_ = 0;
    int   dragStartPx_    = 0;
    bool  resizingEvent_  = false;
    float resizeOrigDur_  = 0;
    int   resizeStartPx_  = 0;

    // Scrubber drag
    bool  draggingScrub_  = false;

    // Text field editing
    CsEditField activeField_ = CsEditField::None;
    char  editBuf_[512]      = {};
    float editBlinkT_        = 0;
    bool  editActive_        = false;

    // Popup menus
    bool showActorMenu_      = false;  // "add actor" submenu
    bool showEnemyTypeMenu_  = false;  // enemy kind submenu
    bool showEventMenu_      = false;  // "add event" submenu
    bool showEaseMenu_       = false;  // ease selector

    // Event clipboard
    bool    hasEventClipboard_ = false;
    CsEvent eventClipboard_;

    // Counters
    uint32_t nextActorId_ = 1;
    uint32_t nextCsId_    = 1;

    // Scroll
    int csListScrollY_   = 0;
    int actorScrollY_    = 0;
    int propsScrollY_    = 0;
    int dlgScrollY_      = 0;

    // Layout (computed)
    int listX_     = 0, listW_     = CS_LIST_W;
    int actorX_    = 0, actorW_    = CS_ACTOR_W;
    int timelineX_ = 0, timelineW_ = 0;
    int propsX_    = 0, propsW_    = CS_PROPS_W;
    int panelY_    = 0, panelH_    = CS_EDITOR_PANEL_H;

    void computeLayout(int screenW, int panelY);

    // ---- Drawing helpers ----
    void fill(int x, int y, int w, int h, SDL_Color c);
    void fillBlend(int x, int y, int w, int h, SDL_Color c);
    void outline(int x, int y, int w, int h, SDL_Color c);
    void hline(int x0, int x1, int y, SDL_Color c);
    void vline(int x, int y0, int y1, SDL_Color c);
    void txt(const char* s, int x, int y, SDL_Color c, int sz = 11);
    void txtR(const char* s, int rx, int y, SDL_Color c, int sz = 11);

    // Win98-style helpers that delegate to ui_
    void panelBg(int x, int y, int w, int h);              // silver fill + bevel
    void sectionHeader(int x, int y, int w, const char* title); // navy bar + white text
    // Draw an editable text field; returns true if clicked (caller starts edit)
    bool fieldRow(const char* label, const char* value, CsEditField fid,
                  int x, int& cy, int w, int labelW = 72);
    // Draw a static label+value row
    void labelRow(const char* label, const char* value,
                  int x, int& cy, int w, int labelW = 72,
                  SDL_Color vc = {230,230,230,255});
    // Thin separator line
    void sepLine(int x, int& cy, int w);
    // Small Win98 button; returns true if clicked
    bool btn(int idx, const char* label, int x, int y, int bw, int bh);
    // Bool toggle row; returns true if toggled
    bool boolRow(const char* label, bool val, int x, int& cy, int w, int labelW = 72);

    // ---- Sub-panels ----
    void renderCutsceneList(int x, int y, int w, int h);
    void renderActorList(int x, int y, int w, int h);
    void renderTimeline(int x, int y, int w, int h);
    void renderPropsPanel(int x, int y, int w, int h);

    void renderProps_Scene(int x, int& cy, int w, int maxY);
    void renderProps_Actor(int x, int& cy, int w, int maxY);
    void renderProps_Event(int x, int& cy, int w, int maxY);
    void renderProps_DialogSeq(int x, int& cy, int w, int maxY);
    void renderProps_DialogLine(int x, int& cy, int w, int maxY);

    // Submenus (drawn on top)
    void renderActorTypeMenu(int anchorX, int anchorY);
    void renderEnemyTypeMenu(int anchorX, int anchorY);
    void renderEventTypeMenu(int anchorX, int anchorY);
    void renderEaseMenu(int anchorX, int anchorY, CsEase current);

    // Timeline helpers
    float pxToTime(int px, int timelineX) const;
    int   timeToPx(float t, int timelineX) const;
    int   actorRowY(int actorIdx, int contentY) const;
    SDL_Color eventColor(CsEventType t) const;

    // Preview
    void recomputePreview();

    // Data helpers
    Cutscene* current();

    void addCutscene();
    void deleteCutscene(int idx);
    void addActor(CsActorType type, CsEnemyType enemyType = CsEnemyType::Melee);
    void deleteActor(int idx);
    void addEvent(CsEventType type, uint32_t actorId, float atTime);
    void deleteEvent(int idx);
    void addDialogSeq();
    void deleteDialogSeq(int idx);
    void addDialogLine(int seqIdx);
    void deleteDialogLine(int seqIdx, int lineIdx);
    void addChoice(int seqIdx, int lineIdx);
    void deleteChoice(int seqIdx, int lineIdx, int choiceIdx);

    // Text field activation / commit
    void startEdit(CsEditField fid, const char* current);
    void commitEdit();
    void cancelEdit();
    void applyEditToField(CsEditField fid, const char* val);

    // Input routing
    void handleTimelineClick(int mx, int my, bool right);
    void handleTimelineMotion(int mx, int my);
    void handleTimelineRelease();
    void handleListClick(int mx, int my);
    void handlePropsPanelClick(int mx, int my);
    void handleTextInput(const char* text);
    void handleKeyDown(SDL_Keycode sym, SDL_Keymod mod);
};
