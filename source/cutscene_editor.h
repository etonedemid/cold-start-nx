#pragma once
#include "cutscene.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

// Height occupied at the bottom of the screen when the cutscene editor is open
static constexpr int CS_EDITOR_PANEL_H = 230;

// Panel column widths within the cutscene editor strip
static constexpr int CS_LIST_W  = 130; // cutscene list on the far left
static constexpr int CS_ACTOR_W = 140; // actor list next to it
static constexpr int CS_PROPS_W = 240; // event/dialog properties on the right

class CutsceneEditor {
public:
    void init(SDL_Renderer* r, int screenW, int screenH, UI::Context* ui);
    void shutdown();

    void handleEvent(SDL_Event& e, float editorZoom, float camWorldX, float camWorldY);
    void update(float dt);
    // Render the bottom panel. timelineAreaY is the Y of the map canvas bottom edge.
    void render(SDL_Renderer* r, int screenW, int panelY);

    void setLibrary(CutsceneLibrary* lib) { lib_ = lib; }
    CutsceneLibrary* library() const { return lib_; }

    bool isActive() const { return active_; }
    void setActive(bool v) { active_ = v; }

    // Get scrub time (for the map editor preview overlay)
    float scrubTime() const { return scrubTime_; }
    // Currently selected cutscene (null if none)
    const Cutscene* currentCutscene() const;

    // For rendering actor positions on the map canvas during scrub
    const CsActorState* actorStateAt(int actorIdx) const;
    int actorCount() const;

private:
    bool active_    = false;
    SDL_Renderer*  r_  = nullptr;
    UI::Context*   ui_ = nullptr;
    int screenW_    = 1280;

    CutsceneLibrary* lib_ = nullptr;

    // --- State ---
    int selectedCutscene_ = -1;
    int selectedActor_    = -1;
    int selectedEvent_    = -1;
    int selectedDialogSeq_  = -1;
    int selectedDialogLine_ = -1;

    // Timeline view
    float timelineStart_  = 0;
    float timelineScale_  = 80.0f; // pixels per second
    float scrubTime_      = 0;
    bool  playing_        = false;

    // Scrub preview: computed actor states at scrubTime_
    std::vector<CsActorState> previewStates_;

    // Drag state for moving events on timeline
    bool  draggingEvent_  = false;
    float dragEventOrigT_ = 0;
    int   dragStartPx_    = 0;

    // Drag state for resizing events (right edge)
    bool  resizingEvent_   = false;
    float resizeOrigDur_   = 0;
    int   resizeStartPx_   = 0;

    // Drag state for scrubber
    bool  draggingScrub_   = false;

    // Text editing
    bool  editingField_    = false;
    char  editBuf_[512]    = {};
    int   editTarget_      = 0; // 0=cs name, 1=actor name, 2=event fields, 3=dialog text

    // Actor add menu popup
    bool showActorMenu_ = false;

    // Event add menu popup
    bool showEventMenu_  = false;
    int  eventMenuActor_ = -1; // actor to attach new event to

    // Pending clipboard
    bool hasEventClipboard_ = false;
    CsEvent eventClipboard_;

    // Next unique IDs
    uint32_t nextActorId_  = 1;
    uint32_t nextCsId_     = 1;

    // Layout helpers (computed from screenW_ and panel position)
    int listX_    = 0, listW_    = CS_LIST_W;
    int actorX_   = 0, actorW_   = CS_ACTOR_W;
    int timelineX_= 0, timelineW_= 0;
    int propsX_   = 0, propsW_   = CS_PROPS_W;
    int panelY_   = 0, panelH_   = CS_EDITOR_PANEL_H;
    int headerH_  = 28;
    int rowH_     = 22;

    void computeLayout(int screenW, int panelY);

    // Drawing (delegated to UI::Context)
    void fillRect(int x, int y, int w, int h, SDL_Color c);
    void drawRect(int x, int y, int w, int h, SDL_Color c);
    void drawLine(int x0, int y0, int x1, int y1, SDL_Color c);
    void drawText(const char* text, int x, int y, SDL_Color c, int size = 13);
    void drawTextRight(const char* text, int x, int y, int w, SDL_Color c, int size = 13);
    void drawBevel(int x, int y, int w, int h, bool raised = true);

    // Sub-panels
    void renderCutsceneList(int x, int y, int w, int h);
    void renderActorList(int x, int y, int w, int h);
    void renderTimeline(int x, int y, int w, int h);
    void renderPropsPanel(int x, int y, int w, int h);
    void renderToolbar(int x, int y, int w);

    // Timeline helpers
    float pxToTime(int px, int timelineX) const;
    int   timeToPx(float t, int timelineX) const;
    int   actorRowY(int actorIdx, int timelineY) const;
    SDL_Color eventColor(CsEventType t) const;

    // Preview computation
    void recomputePreview();

    // Editing helpers
    Cutscene* current();
    void addCutscene();
    void deleteCutscene(int idx);
    void addActor(CsActorType type, CsEnemyType enemyType = CsEnemyType::Melee);
    void deleteActor(int idx);
    void addEvent(CsEventType type, uint32_t actorId, float atTime);
    void deleteEvent(int idx);
    void addDialogSeq();
    void addDialogLine(int seqIdx);

    // Input handling by sub-region
    void handleTimelineClick(int mx, int my, bool rightClick);
    void handleTimelineMotion(int mx, int my);
    void handleTimelineRelease();
    void handlePropsPanelEvent(SDL_Event& e);
    void handleListClick(int mx, int my, int panelX, int panelY, int panelW, int panelH);
};