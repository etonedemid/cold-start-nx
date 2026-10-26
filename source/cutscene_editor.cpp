// Cutscene editor - bottom panel docked in the map editor.
//
// Layout:  [cutscene list][actor list][timeline...............][inspector]
// The top edge of the panel can be dragged to resize it.  All text fields
// are click-to-edit (type, Enter to commit, Esc to cancel).  Numeric fields
// also have -/+ spinners.  Events are dragged on the timeline (1ms snap,
// hold Shift for free placement) and resized by their right edge.
// Move/explosion/spawn positions and actor start positions can be picked by
// clicking directly on the map canvas, and actor markers can be dragged.
#include "cutscene_editor.h"
#include "assets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- Widget id ranges (must not collide with the map editor's 100-599) ----
// Buttons: 600-799.  Text/number fields: 1000+ (internal focus ids only).

namespace {

// Win98 gray theme - matches the map editor's toolbar / panels.
const SDL_Color ThFace   = {192, 192, 192, 255};  // panel face (silver)
const SDL_Color ThSunk   = {255, 255, 255, 255};  // sunken content (white)
const SDL_Color ThSunkAlt= {223, 223, 223, 255};  // alt row
const SDL_Color ThText   = {  0,   0,   0, 255};  // black
const SDL_Color ThDim    = { 96,  96, 104, 255};  // shadow gray
const SDL_Color ThHead   = {  0,   0, 128, 255};  // navy headers
const SDL_Color ThTitle  = {  0,   0, 128, 255};  // navy title bar
const SDL_Color ThTitleTx= {255, 255, 255, 255};  // white on navy
const SDL_Color ThSel    = {  0,   0, 128, 255};  // navy selection
const SDL_Color ThSelTx  = {255, 255, 255, 255};
const SDL_Color ThHover  = {180, 190, 215, 255};  // light blue hover
const SDL_Color ThGrid   = {150, 150, 156, 255};  // gridlines on light
const SDL_Color ThShadow = {128, 128, 128, 255};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int   clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }

// Does this event type animate/affect a specific actor?
bool eventNeedsActor(CsEventType t) {
    switch (t) {
        case CsEventType::Move:
        case CsEventType::Rotate:
        case CsEventType::Scale:
        case CsEventType::Alpha:
        case CsEventType::Flash:
        case CsEventType::SetVisible:
        case CsEventType::SetFrame:
        case CsEventType::SpawnActor:
        case CsEventType::DespawnActor:
            return true;
        default:
            return false;
    }
}

const char* easeName(CsEase e) {
    switch (e) {
        case CsEase::Linear:    return "Linear";
        case CsEase::EaseIn:    return "Ease In";
        case CsEase::EaseOut:   return "Ease Out";
        case CsEase::EaseInOut: return "Ease InOut";
        case CsEase::Instant:   return "Instant";
        default:                return "?";
    }
}

const char* enemyKindName(CsEnemyType t) {
    static const char* names[] = {"Melee","Shooter","Brute","Scout","Sniper","Gunner"};
    int i = (int)t;
    return (i >= 0 && i < 6) ? names[i] : "?";
}

} // namespace

// ---- Init / shutdown ----

void CutsceneEditor::init(SDL_Renderer* r, int screenW, int screenH, UI::Context* ui) {
    r_       = r;
    ui_      = ui;
    screenW_ = screenW;
    screenH_ = screenH;
    panelH_  = CS_EDITOR_PANEL_H;
}

void CutsceneEditor::shutdown() {
    releaseFocus(false);
}

void CutsceneEditor::setActive(bool v) {
    if (active_ == v) return;
    active_ = v;
    if (!v) {
        releaseFocus(false);
        playing_       = false;
        pickMode_      = CsPickMode::None;
        showEventMenu_ = false;
        showActorMenu_ = false;
        dragActorIdx_  = -1;
        return;
    }
    // Activating: pick a default cutscene and seed unique id counters
    if (lib_) {
        if (selectedCutscene_ < 0 && !lib_->cutscenes.empty()) selectedCutscene_ = 0;
        for (auto& cs : lib_->cutscenes)
            for (auto& a : cs.actors)
                if (a.id >= nextActorId_) nextActorId_ = a.id + 1;
        nextCsId_ = (uint32_t)lib_->cutscenes.size() + 1;
    }
    clampSelection();
    recomputePreview();
}

void CutsceneEditor::setStatus(const char* msg) {
    statusMsg_  = msg ? msg : "";
    statusMsgT_ = 2.5f;
}

bool CutsceneEditor::rotationHandle(float& cx, float& cy, float& a0, float& a1, bool& two) const {
    const Cutscene* cs = currentCutscene();
    if (!cs) return false;
    // Only shown for a selected Rotate event - centered on its actor's current
    // previewed position so it tracks the actor through Move events.
    if (selectedEvent_ < 0 || selectedEvent_ >= (int)cs->events.size()) return false;
    const CsEvent& ev = cs->events[selectedEvent_];
    if (ev.type != CsEventType::Rotate) return false;
    int idx = -1;
    for (int i = 0; i < (int)cs->actors.size(); i++)
        if (cs->actors[i].id == ev.actorId) { idx = i; break; }
    if (idx < 0 || idx >= (int)previewStates_.size()) return false;
    cx = previewStates_[idx].x; cy = previewStates_[idx].y;
    a0 = ev.fromRot; a1 = ev.toRot; two = true;
    return true;
}

// Scale gizmo for a selected Scale event. Box half-size in world units is
// CS_SCALE_REF * scale, so a corner at CS_SCALE_REF from center is scale 1.0.
bool CutsceneEditor::scaleHandle(float& cx, float& cy, float& fsx, float& fsy,
                                 float& tsx, float& tsy) const {
    const Cutscene* cs = currentCutscene();
    if (!cs) return false;
    if (selectedEvent_ < 0 || selectedEvent_ >= (int)cs->events.size()) return false;
    const CsEvent& ev = cs->events[selectedEvent_];
    if (ev.type != CsEventType::Scale) return false;
    int idx = -1;
    for (int i = 0; i < (int)cs->actors.size(); i++)
        if (cs->actors[i].id == ev.actorId) { idx = i; break; }
    if (idx < 0 || idx >= (int)previewStates_.size()) return false;
    cx = previewStates_[idx].x; cy = previewStates_[idx].y;
    fsx = ev.fromScaleX; fsy = ev.fromScaleY;
    tsx = ev.toScaleX;   tsy = ev.toScaleY;
    return true;
}

const char* CutsceneEditor::pickHint() const {
    switch (pickMode_) {
        case CsPickMode::ActorStart:     return "Click on the map to set the actor start position (Esc cancels)";
        case CsPickMode::EventFrom:      return "Click on the map to set the FROM position (Esc cancels)";
        case CsPickMode::EventTo:        return "Click on the map to set the TO position (Esc cancels)";
        case CsPickMode::EventExplosion: return "Click on the map to place the explosion (Esc cancels)";
        case CsPickMode::EventSpawnPos:  return "Click on the map to set the spawn position (Esc cancels)";
        default: return "";
    }
}

// ---- Selection / preview ----

Cutscene* CutsceneEditor::current() {
    if (!lib_ || selectedCutscene_ < 0 || selectedCutscene_ >= (int)lib_->cutscenes.size())
        return nullptr;
    return &lib_->cutscenes[selectedCutscene_];
}

const Cutscene* CutsceneEditor::currentCutscene() const {
    if (!lib_ || selectedCutscene_ < 0 || selectedCutscene_ >= (int)lib_->cutscenes.size())
        return nullptr;
    return &lib_->cutscenes[selectedCutscene_];
}

void CutsceneEditor::clampSelection() {
    int nCs = lib_ ? (int)lib_->cutscenes.size() : 0;
    if (selectedCutscene_ >= nCs) selectedCutscene_ = nCs - 1;
    const Cutscene* cs = currentCutscene();
    int nA = cs ? (int)cs->actors.size() : 0;
    int nE = cs ? (int)cs->events.size() : 0;
    int nD = cs ? (int)cs->dialogs.size() : 0;
    if (selectedActor_ >= nA)     selectedActor_ = nA - 1;
    if (selectedEvent_ >= nE)     selectedEvent_ = nE - 1;
    if (selectedDialogSeq_ >= nD) selectedDialogSeq_ = nD - 1;
    if (selectedDialogSeq_ >= 0 && cs) {
        int nL = (int)cs->dialogs[selectedDialogSeq_].lines.size();
        if (selectedDialogLine_ >= nL) selectedDialogLine_ = nL - 1;
    } else {
        selectedDialogLine_ = -1;
    }
}

const CsActorState* CutsceneEditor::actorStateAt(int idx) const {
    if (idx < 0 || idx >= (int)previewStates_.size()) return nullptr;
    return &previewStates_[idx];
}

int CutsceneEditor::actorCount() const {
    const Cutscene* cs = currentCutscene();
    return cs ? (int)cs->actors.size() : 0;
}

void CutsceneEditor::recomputePreview() {
    const Cutscene* cs = currentCutscene();
    if (!cs) { previewStates_.clear(); return; }

    previewStates_.resize(cs->actors.size());
    for (int i = 0; i < (int)cs->actors.size(); i++) {
        const auto& a = cs->actors[i];
        auto& s   = previewStates_[i];
        s.x       = a.startX;       s.y      = a.startY;
        s.rot     = a.startRot;     s.scaleX = a.startScaleX;
        s.scaleY  = a.startScaleY;  s.alpha  = a.startAlpha;
        s.visible = a.startVisible; s.frame  = 0;
        s.flashAmt = 0;
    }

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    auto ease = [](float t, CsEase e) -> float {
        t = clampf(t, 0.0f, 1.0f);
        switch (e) {
            case CsEase::EaseIn:    return t * t;
            case CsEase::EaseOut:   return 1 - (1 - t) * (1 - t);
            case CsEase::EaseInOut: return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
            case CsEase::Instant:   return t >= 1.0f ? 1.0f : 0.0f;
            default:                return t;
        }
    };

    for (const auto& ev : cs->events) {
        if (scrubTime_ < ev.startTime) continue;
        float end = ev.startTime + std::max(ev.duration, 0.001f);
        float localT = (scrubTime_ >= end) ? 1.0f
                     : (scrubTime_ - ev.startTime) / (end - ev.startTime);
        float t = ease(localT, ev.ease);

        int idx = -1;
        for (int i = 0; i < (int)cs->actors.size(); i++)
            if (cs->actors[i].id == ev.actorId) { idx = i; break; }
        CsActorState* s = (idx >= 0) ? &previewStates_[idx] : nullptr;

        switch (ev.type) {
            case CsEventType::Move:
                if (s) { s->x = lerp(ev.fromX, ev.toX, t); s->y = lerp(ev.fromY, ev.toY, t); }
                break;
            case CsEventType::Rotate:
                if (s) s->rot = lerp(ev.fromRot, ev.toRot, t);
                break;
            case CsEventType::Scale:
                if (s) { s->scaleX = lerp(ev.fromScaleX, ev.toScaleX, t);
                         s->scaleY = lerp(ev.fromScaleY, ev.toScaleY, t); }
                break;
            case CsEventType::Alpha:
                if (s) s->alpha = lerp(ev.fromAlpha, ev.toAlpha, t);
                break;
            case CsEventType::Flash:
                if (s && scrubTime_ < end) s->flashAmt = 1.0f - localT;
                break;
            case CsEventType::SetVisible:
                if (s && localT >= 1.0f) s->visible = ev.visible;
                break;
            case CsEventType::SetFrame:
                if (s && localT >= 1.0f) s->frame = ev.frame;
                break;
            case CsEventType::SpawnActor:
                if (s) {
                    s->visible = true;
                    if (ev.spawnOverridePos) { s->x = ev.spawnX; s->y = ev.spawnY; }
                }
                break;
            case CsEventType::DespawnActor:
                if (s) s->visible = false;
                break;
            default: break;
        }
    }
}

float CutsceneEditor::snapTime(float t) const {
    bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
    bool snap  = snapOn_ != shift;  // Shift temporarily inverts the toggle
    if (snap) t = roundf(t * 1000.0f) / 1000.0f;
    return std::max(0.0f, t);
}

// ---- Mutations ----

void CutsceneEditor::addCutscene() {
    if (!lib_) return;
    Cutscene cs;
    for (;;) {
        cs.id = "cutscene_" + std::to_string(nextCsId_++);
        if (!lib_->findById(cs.id)) break;
    }
    cs.blockInput = true;
    // Every cutscene gets an implicit Player actor as the first actor.
    CsActor pa;
    pa.id   = nextActorId_++;
    pa.type = CsActorType::Player;
    pa.name = "Player";
    pa.startX = 320; pa.startY = 240;
    cs.actors.push_back(pa);
    lib_->cutscenes.push_back(std::move(cs));
    selectedCutscene_ = (int)lib_->cutscenes.size() - 1;
    selectedActor_ = 0;
    selectedEvent_ = -1;
    selectedDialogSeq_ = selectedDialogLine_ = -1;
    scrubTime_ = 0;
    recomputePreview();
}

void CutsceneEditor::deleteCutscene(int idx) {
    if (!lib_ || idx < 0 || idx >= (int)lib_->cutscenes.size()) return;
    lib_->cutscenes.erase(lib_->cutscenes.begin() + idx);
    if (selectedCutscene_ >= (int)lib_->cutscenes.size())
        selectedCutscene_ = (int)lib_->cutscenes.size() - 1;
    selectedActor_ = selectedEvent_ = -1;
    selectedDialogSeq_ = selectedDialogLine_ = -1;
    recomputePreview();
}

void CutsceneEditor::addActor(CsActorType type, CsEnemyType enemyType) {
    Cutscene* cs = current();
    if (!cs) return;
    CsActor a;
    a.id        = nextActorId_++;
    a.type      = type;
    a.enemyType = enemyType;
    switch (type) {
        case CsActorType::Player:     a.name = "Player"; break;
        case CsActorType::Enemy:      a.name = enemyKindName(enemyType); break;
        case CsActorType::FreeSprite: a.name = "Sprite"; break;
    }
    // Distinct numbered name
    int n = 2;
    std::string base = a.name;
    for (;;) {
        bool taken = false;
        for (auto& other : cs->actors) if (other.name == a.name) { taken = true; break; }
        if (!taken) break;
        a.name = base + " " + std::to_string(n++);
    }
    a.startX = 320; a.startY = 240;
    cs->actors.push_back(a);
    selectedActor_ = (int)cs->actors.size() - 1;
    selectedEvent_ = -1;
    setStatus("Actor added - drag its marker on the map to position it");
    recomputePreview();
}

void CutsceneEditor::deleteActor(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->actors.size()) return;
    uint32_t rmId = cs->actors[idx].id;
    cs->actors.erase(cs->actors.begin() + idx);
    cs->events.erase(std::remove_if(cs->events.begin(), cs->events.end(),
        [rmId](const CsEvent& e) { return e.actorId == rmId; }), cs->events.end());
    if (selectedActor_ >= (int)cs->actors.size())
        selectedActor_ = (int)cs->actors.size() - 1;
    selectedEvent_ = -1;
    recomputePreview();
}

void CutsceneEditor::addEvent(CsEventType type) {
    Cutscene* cs = current();
    if (!cs) return;

    uint32_t actorId = 0;
    int actorIdx = -1;
    if (eventNeedsActor(type)) {
        actorIdx = selectedActor_;
        if (actorIdx < 0 && !cs->actors.empty()) actorIdx = 0;
        if (actorIdx < 0) {
            setStatus("This event needs an actor - add one first");
            return;
        }
        actorId = cs->actors[actorIdx].id;
    }

    // Current preview state of the target actor: used to seed sensible defaults
    const CsActorState* st = (actorIdx >= 0 && actorIdx < (int)previewStates_.size())
                           ? &previewStates_[actorIdx] : nullptr;

    CsEvent ev;
    ev.actorId   = actorId;
    ev.type      = type;
    ev.startTime = snapTime(scrubTime_);
    ev.duration  = 1.0f;
    ev.ease      = CsEase::EaseInOut;

    switch (type) {
        case CsEventType::Move:
            ev.fromX = st ? st->x : 0;  ev.fromY = st ? st->y : 0;
            ev.toX   = ev.fromX + 128;  ev.toY   = ev.fromY;
            break;
        case CsEventType::Rotate:
            ev.fromRot = st ? st->rot : 0;
            ev.toRot   = ev.fromRot + 90;
            break;
        case CsEventType::Scale:
            ev.fromScaleX = st ? st->scaleX : 1; ev.fromScaleY = st ? st->scaleY : 1;
            ev.toScaleX   = ev.fromScaleX;       ev.toScaleY   = ev.fromScaleY;
            break;
        case CsEventType::Alpha:
            ev.fromAlpha = st ? st->alpha : 1;  ev.toAlpha = (ev.fromAlpha > 0.5f) ? 0.0f : 1.0f;
            break;
        case CsEventType::Flash:         ev.duration = 0.4f; break;
        case CsEventType::Wait:          ev.duration = 1.0f; break;
        case CsEventType::Dialog:        ev.duration = 0.0f; break;
        case CsEventType::PlaySFX:       ev.duration = 0.0f; break;
        case CsEventType::SpawnExplosion:
            ev.duration = 0.0f;
            ev.explX = st ? st->x : 0;  ev.explY = st ? st->y : 0;
            break;
        case CsEventType::CameraMove:
            ev.fromX = st ? st->x : 0;  ev.fromY = st ? st->y : 0;
            ev.toX = ev.fromX;          ev.toY   = ev.fromY;
            break;
        case CsEventType::CameraZoom:    ev.fromZoom = 1.0f; ev.toZoom = 1.5f; break;
        case CsEventType::CameraShake:   ev.shakeStrength = 8.0f; ev.duration = 0.5f; break;
        case CsEventType::CameraRotate:  ev.fromRot = 0.0f; ev.toRot = 15.0f; break;
        case CsEventType::ScreenFade:    ev.fadeToBlack = true; break;
        case CsEventType::CinematicBars: ev.showBars = true; ev.duration = 0.5f; break;
        case CsEventType::SetVisible:    ev.duration = 0.0f; ev.visible = true; break;
        case CsEventType::SetFrame:      ev.duration = 0.0f; break;
        case CsEventType::SpawnActor:
            ev.duration = 0.0f;
            ev.spawnX = st ? st->x : 0; ev.spawnY = st ? st->y : 0;
            break;
        case CsEventType::DespawnActor:  ev.duration = 0.0f; break;
        case CsEventType::SetFlag:       ev.duration = 0.0f; ev.flagName = "flag"; break;
        case CsEventType::SetVariable:   ev.duration = 0.0f; ev.varName = "var"; ev.varValue = 1; break;
        case CsEventType::DeathScreen:   ev.duration = 0.0f; break;
        case CsEventType::ChainCutscene: ev.duration = 0.0f; break;
        case CsEventType::PostFXAcid:    ev.duration = 0.0f; ev.flagValue = true; break;
        case CsEventType::ConsoleCmd:    ev.duration = 0.0f; break;
        case CsEventType::EndCutscene:   ev.duration = 0.0f; break;
        case CsEventType::AdjustSignal:  ev.duration = 0.0f; ev.signalDelta = 5; break;
        case CsEventType::BranchCutscene:ev.duration = 0.0f; break;
        case CsEventType::LoadMap:       ev.duration = 0.0f; break;
        default: break;
    }
    cs->events.push_back(ev);
    selectedEvent_ = (int)cs->events.size() - 1;
    if (actorIdx >= 0) selectedActor_ = actorIdx;
    recomputePreview();
}

void CutsceneEditor::deleteEvent(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->events.size()) return;
    cs->events.erase(cs->events.begin() + idx);
    selectedEvent_ = -1;
    recomputePreview();
}

void CutsceneEditor::addDialogSeq() {
    Cutscene* cs = current();
    if (!cs) return;
    CsDialogSeq seq;
    int n = (int)cs->dialogs.size();
    for (;;) {
        seq.id = "dialog_" + std::to_string(n++);
        if (!cs->findDialog(seq.id)) break;
    }
    CsDialogLine line;
    line.character = "Character";
    line.text      = "Enter dialog text here.";
    seq.lines.push_back(line);
    cs->dialogs.push_back(seq);
    selectedDialogSeq_  = (int)cs->dialogs.size() - 1;
    selectedDialogLine_ = 0;
}

void CutsceneEditor::deleteDialogSeq(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->dialogs.size()) return;
    cs->dialogs.erase(cs->dialogs.begin() + idx);
    if (selectedDialogSeq_ >= (int)cs->dialogs.size())
        selectedDialogSeq_ = (int)cs->dialogs.size() - 1;
    selectedDialogLine_ = -1;
    clampSelection();
}

// ---- Layout ----

void CutsceneEditor::computeLayout(int screenW, int screenH, int panelY) {
    screenW_ = screenW;
    screenH_ = screenH;
    panelY_  = panelY;

    listW_  = 150;
    actorW_ = 150;
    propsW_ = (screenW >= 1100) ? 290 : 240;

    listX_  = 0;
    actorX_ = listX_ + listW_ + 1;
    propsX_ = screenW - propsW_;
    tlX_    = actorX_ + actorW_ + 1;
    tlW_    = propsX_ - tlX_ - 1;

    colY_ = panelY_ + HEADER_H;
    colH_ = screenH - HINT_H - colY_;
}

SDL_Color CutsceneEditor::eventColor(CsEventType t) const {
    switch (t) {
        case CsEventType::Move:           return {90,  170, 255, 255};
        case CsEventType::Rotate:         return {150, 100, 255, 255};
        case CsEventType::Scale:          return {100, 220, 150, 255};
        case CsEventType::Alpha:          return {200, 200, 80,  255};
        case CsEventType::Flash:          return {255, 150, 80,  255};
        case CsEventType::Wait:           return {130, 130, 130, 255};
        case CsEventType::Dialog:         return {255, 210, 80,  255};
        case CsEventType::PlaySFX:        return {80,  220, 220, 255};
        case CsEventType::SpawnExplosion: return {255, 80,  80,  255};
        case CsEventType::CameraMove:     return {255, 160, 200, 255};
        case CsEventType::CameraZoom:     return {255, 130, 160, 255};
        case CsEventType::CameraShake:    return {255, 80,  160, 255};
        case CsEventType::CameraRotate:   return {255, 100, 180, 255};
        case CsEventType::ScreenFade:     return {90,  90,  90,  255};
        case CsEventType::CinematicBars:  return {120, 120, 120, 255};
        case CsEventType::SetVisible:     return {180, 255, 180, 255};
        case CsEventType::SetFrame:       return {180, 200, 255, 255};
        case CsEventType::SpawnActor:     return {120, 255, 200, 255};
        case CsEventType::DespawnActor:   return {255, 160, 120, 255};
        case CsEventType::SetFlag:        return {220, 180, 255, 255};
        case CsEventType::ChainCutscene:  return {140, 200, 255, 255};
        case CsEventType::EndCutscene:    return {255, 120, 120, 255};
        case CsEventType::AdjustSignal:   return {120, 255, 120, 255};
        case CsEventType::BranchCutscene: return {255, 220, 140, 255};
        case CsEventType::SetVariable:    return {180, 240, 180, 255};
        case CsEventType::DeathScreen:    return {255,  80,  80, 255};
        case CsEventType::LoadMap:        return {140, 220, 255, 255};
        case CsEventType::PostFXAcid:     return { 80, 255, 120, 255};
        case CsEventType::ConsoleCmd:     return {255, 210,  80, 255};
        case CsEventType::SpawnEnemy:    return {255, 100, 60,  255};
        case CsEventType::SpawnPickup:   return {60,  255, 140, 255};
        default:                          return {150, 150, 150, 255};
    }
}

// ---- Drawing primitives ----

void CutsceneEditor::fillRect(int x, int y, int w, int h, SDL_Color c) {
    if (!r_) return;
    if (clipActive_ && (y + h <= clipY0_ || y >= clipY1_)) return;
    SDL_SetRenderDrawBlendMode(r_, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r_, &rect);
}

void CutsceneEditor::drawRect(int x, int y, int w, int h, SDL_Color c) {
    if (!r_) return;
    if (clipActive_ && (y + h <= clipY0_ || y >= clipY1_)) return;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(r_, &rect);
}

void CutsceneEditor::drawLine(int x0, int y0, int x1, int y1, SDL_Color c) {
    if (!r_) return;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r_, x0, y0, x1, y1);
}

void CutsceneEditor::drawText(const char* text, int x, int y, SDL_Color c, int size) {
    if (!ui_ || !text || !text[0]) return;
    if (clipActive_ && (y + size + 4 <= clipY0_ || y >= clipY1_)) return;
    ui_->drawText(text, x, y, size, c);
}

void CutsceneEditor::drawBevel(int x, int y, int w, int h, bool raised) {
    if (!ui_) return;
    ui_->drawWin98Bevel(x, y, w, h, raised);
}

bool CutsceneEditor::button(int id, const char* label, int x, int y, int w, int h, bool sel) {
    if (!ui_) return false;
    if (clipActive_ && (y < clipY0_ || y + h > clipY1_)) return false;  // skip clipped widgets
    return ui_->win98Button(id, label, x, y, w, h, sel);
}

// ---- Inline editable fields ----

void CutsceneEditor::focusField(int id, const std::string& current, bool numeric) {
    if (focusedField_ >= 0 && focusedField_ != id)
        releaseFocus(true);  // commit whatever was being edited
    focusedField_ = id;
    editBuf_      = current;
    numericField_ = numeric;
#ifndef __SWITCH__
    SDL_StartTextInput();
#endif
}

void CutsceneEditor::releaseFocus(bool commit) {
    if (focusedField_ < 0) return;
    if (commit && focusedCommit_) focusedCommit_(editBuf_);
    focusedField_  = -1;
    focusedCommit_ = nullptr;
    editBuf_.clear();
#ifndef __SWITCH__
    SDL_StopTextInput();
#endif
}

void CutsceneEditor::textField(int id, int x, int y, int w, int h, const std::string& value,
                               std::function<void(const std::string&)> commit) {
    if (!ui_) return;
    if (clipActive_ && (y < clipY0_ || y + h > clipY1_)) {
        if (focusedField_ == id) focusedCommit_ = commit;  // keep binding alive
        return;
    }
    bool focused = (focusedField_ == id);
    if (focused) focusedCommit_ = commit;

    float blink = focused ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
    ui_->drawWin98TextField(x, y, w, h, focused ? editBuf_.c_str() : value.c_str(),
                            focused, false, blink);

    if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, x, y, w, h)) {
        ui_->mouseClicked = false;
        ui_->clickCooldownFrames = 2;
        focusField(id, value, false);
        focusedCommit_ = commit;
    }
}

void CutsceneEditor::floatField(int id, int x, int y, int w, int h, float* v,
                                float step, float lo, float hi, const char* fmt) {
    if (!ui_) return;
    const int bw = 15;
    if (button(40000 + id, "-", x, y, bw, h)) {
        *v = clampf(*v - step, lo, hi);
        recomputePreview();
    }
    if (button(50000 + id, "+", x + w - bw, y, bw, h)) {
        *v = clampf(*v + step, lo, hi);
        recomputePreview();
    }
    char buf[48];
    snprintf(buf, sizeof(buf), fmt, (double)*v);
    int fx = x + bw + 1, fw = w - (bw + 1) * 2;

    if (clipActive_ && (y < clipY0_ || y + h > clipY1_)) {
        if (focusedField_ == id) {
            focusedCommit_ = [this, v, lo, hi](const std::string& s) {
                char* end = nullptr;
                float f = strtof(s.c_str(), &end);
                if (end != s.c_str()) { *v = clampf(f, lo, hi); recomputePreview(); }
            };
        }
        return;
    }

    bool focused = (focusedField_ == id);
    auto commit = [this, v, lo, hi](const std::string& s) {
        char* end = nullptr;
        float f = strtof(s.c_str(), &end);
        if (end != s.c_str()) { *v = clampf(f, lo, hi); recomputePreview(); }
    };
    if (focused) focusedCommit_ = commit;

    float blink = focused ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
    ui_->drawWin98TextField(fx, y, fw, h, focused ? editBuf_.c_str() : buf, focused, false, blink);

    if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, fx, y, fw, h)) {
        ui_->mouseClicked = false;
        ui_->clickCooldownFrames = 2;
        focusField(id, buf, true);
        focusedCommit_ = commit;
    }
}

void CutsceneEditor::intField(int id, int x, int y, int w, int h, int* v, int step, int lo, int hi) {
    const int bw = 15;
    if (button(40000 + id, "-", x, y, bw, h)) { *v = clampi(*v - step, lo, hi); recomputePreview(); }
    if (button(50000 + id, "+", x + w - bw, y, bw, h)) { *v = clampi(*v + step, lo, hi); recomputePreview(); }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", *v);
    int fx = x + bw + 1, fw = w - (bw + 1) * 2;

    auto commit = [this, v, lo, hi](const std::string& s) {
        char* end = nullptr;
        long n = strtol(s.c_str(), &end, 10);
        if (end != s.c_str()) { *v = clampi((int)n, lo, hi); recomputePreview(); }
    };

    if (clipActive_ && (y < clipY0_ || y + h > clipY1_)) {
        if (focusedField_ == id) focusedCommit_ = commit;
        return;
    }
    bool focused = (focusedField_ == id);
    if (focused) focusedCommit_ = commit;

    float blink = focused ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
    ui_->drawWin98TextField(fx, y, fw, h, focused ? editBuf_.c_str() : buf, focused, false, blink);

    if (ui_->mouseClicked && ui_->pointInRect(ui_->mouseX, ui_->mouseY, fx, y, fw, h)) {
        ui_->mouseClicked = false;
        ui_->clickCooldownFrames = 2;
        focusField(id, buf, true);
        focusedCommit_ = commit;
    }
}

// ---- Update ----

void CutsceneEditor::update(float dt) {
    if (!active_) return;
    if (statusMsgT_ > 0) statusMsgT_ -= dt;

    // Safety net: if the mouse button is no longer held (e.g. the up event
    // was suppressed by a click cooldown), end any drag in progress.
    bool mousePhysDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    if (!mousePhysDown && !(ui_ && ui_->touchActive) &&
        (draggingEvent_ || resizingEvent_ || draggingScrub_ || resizingPanel_ ||
         dragActorIdx_ >= 0)) {
        handleRelease();
    }

    if (playing_) {
        scrubTime_ += dt;
        const Cutscene* cs = currentCutscene();
        float dur = cs ? cs->totalDuration() : 0.0f;
        if (dur < 0.05f) dur = 0.05f;
        if (scrubTime_ >= dur) {
            scrubTime_ = dur;
            playing_   = false;
        }
        // Keep the scrubber in view while playing
        if (scrubTime_ > timelineStart_ + viewDuration() - 0.5f)
            timelineStart_ = std::max(0.0f, scrubTime_ - viewDuration() * 0.5f);
        recomputePreview();
    }

    if (dialogModal_) updateDialogModal(dt);
}

// ---- Rendering ----

void CutsceneEditor::render(SDL_Renderer* r, int screenW, int screenH, int panelY) {
    r_ = r;
    if (!ui_) return;
    computeLayout(screenW, screenH, panelY);
    clipActive_ = false;

    // The dialog editor takes over the whole screen; the map editor hides its
    // own panels while it is open so nothing behind it can steal clicks.
    if (dialogModal_) { renderDialogModal(); return; }

    // Panel background (silver, raised like a Win98 toolbar)
    fillRect(0, panelY_, screenW, screenH - panelY_, ThFace);

    renderHeader(screenW);
    renderCutsceneList();
    renderActorList();
    renderTimeline();
    renderInspector();
    renderHintBar();
    renderMenus();
}

// ---- Dialog editor modal ----

void CutsceneEditor::openDialogModal(int seqIdx) {
    Cutscene* cs = current();
    if (!cs || seqIdx < 0 || seqIdx >= (int)cs->dialogs.size()) return;
    releaseFocus(true);
    selectedDialogSeq_  = seqIdx;
    if (selectedDialogLine_ < 0 || selectedDialogLine_ >= (int)cs->dialogs[seqIdx].lines.size())
        selectedDialogLine_ = cs->dialogs[seqIdx].lines.empty() ? -1 : 0;
    dialogModal_ = true;
    dlgPlaying_  = false;
    dlgPreviewT_ = 0;
}

void CutsceneEditor::closeDialogModal() {
    releaseFocus(true);
    dialogModal_ = false;
    dlgPlaying_  = false;
}

void CutsceneEditor::updateDialogModal(float dt) {
    if (dlgPlaying_) dlgPreviewT_ += dt;
}

bool CutsceneEditor::handleDialogModalEvent(SDL_Event& e) {
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: closeDialogModal(); return true;
            case SDLK_SPACE:
                dlgPlaying_ = !dlgPlaying_;
                if (dlgPlaying_) dlgPreviewT_ = 0;
                return true;
            default: break;
        }
    }
    // Consume everything else so the timeline / map never react underneath.
    return true;
}

void CutsceneEditor::renderDialogModal() {
    Cutscene* cs = current();
    const int W = screenW_, H = screenH_;

    // Dim the whole screen
    fillRect(0, 0, W, H, {0, 0, 0, 200});

    // Centered window
    const int winW = std::min(960, W - 40);
    const int winH = std::min(640, H - 40);
    const int winX = (W - winW) / 2;
    const int winY = (H - winH) / 2;
    ui_->drawWin98Window(winX, winY, winW, winH, "Dialog Editor");
    if (button(900, "Close", winX + winW - 64, winY + 3, 56, 16, false)) { closeDialogModal(); return; }

    if (!cs || selectedDialogSeq_ < 0 || selectedDialogSeq_ >= (int)cs->dialogs.size()) {
        drawText("No dialog sequence selected.", winX + 16, winY + 40, ThText, 13);
        return;
    }
    CsDialogSeq& seq = cs->dialogs[selectedDialogSeq_];

    int x = winX + 14;
    int y = winY + UI::W98::TitleH + 10;
    int innerW = winW - 28;

    // -- Sequence id + line strip --
    drawText("Sequence:", x, y + 4, ThText, 12);
    textField(2000, x + 76, y, 200, 20, seq.id, [&seq](const std::string& s) {
        if (!s.empty()) seq.id = s;
    });
    drawText("(Dialog events reference this id)", x + 286, y + 4, ThDim, 11);
    y += 28;

    // Line tabs
    int nLines = (int)seq.lines.size();
    drawText("Lines:", x, y + 4, ThText, 12);
    int lx = x + 54;
    for (int i = 0; i < nLines && lx < winX + winW - 150; i++) {
        char lb[8]; snprintf(lb, sizeof(lb), "%d", i + 1);
        if (button(2100 + i, lb, lx, y, 26, 20, i == selectedDialogLine_))
            selectedDialogLine_ = i;
        lx += 28;
    }
    if (button(2090, "+ Add", winX + winW - 150, y, 56, 20, false)) {
        CsDialogLine ln; ln.character = "Character"; ln.text = "...";
        seq.lines.push_back(ln);
        selectedDialogLine_ = (int)seq.lines.size() - 1;
        nLines = (int)seq.lines.size();
    }
    if (selectedDialogLine_ >= 0 &&
        button(2091, "Del", winX + winW - 90, y, 40, 20, false)) {
        seq.lines.erase(seq.lines.begin() + selectedDialogLine_);
        if (selectedDialogLine_ >= (int)seq.lines.size())
            selectedDialogLine_ = (int)seq.lines.size() - 1;
    }
    y += 28;

    if (selectedDialogLine_ < 0 || selectedDialogLine_ >= (int)seq.lines.size()) {
        drawText("This sequence has no lines. Click '+ Add'.", x, y + 8, ThDim, 12);
        return;
    }
    CsDialogLine& line = seq.lines[selectedDialogLine_];

    // -- Live preview --
    int previewH = 210;
    int previewY = y;
    fillRect(x, previewY, innerW, previewH, {20, 22, 30, 255});
    drawBevel(x, previewY, innerW, previewH, false);
    // Hover-test choices in the preview for highlighting
    dlgHoverChoice_ = -1;
    // Typewriter: when playing, reveal chars over time; otherwise full text.
    int vis = -1;
    bool complete = true;
    if (dlgPlaying_) {
        int total = (int)line.text.size();
        vis = std::min(total, (int)(dlgPreviewT_ * 40.0f));
        complete = (vis >= total);
    }
    cutsceneRenderDialogBox(r_, x + 1, previewY + 1, innerW - 2, previewH - 2,
                            line, vis, complete, dlgHoverChoice_);
    // Preview controls
    if (button(2010, dlgPlaying_ ? "Stop" : "Write", x, previewY + previewH + 4, 96, 18, dlgPlaying_)) {
        dlgPlaying_ = !dlgPlaying_;
        dlgPreviewT_ = 0;
    }
    drawText("Space: play/pause typing", x + 104, previewY + previewH + 7, ThDim, 11);
    y = previewY + previewH + 28;

    // -- Fields (two columns) --
    int colW = (innerW - 16) / 2;
    int cxL = x, cxR = x + colW + 16;
    int fh = 20, step = 26;
    int fxOff = 70;

    // Left column: speaker, portrait, side, sfx
    int yl = y;
    drawText("Speaker:", cxL, yl + 4, ThText, 12);
    textField(2020, cxL + fxOff, yl, colW - fxOff, fh, line.character,
              [&line](const std::string& s){ line.character = s; });
    yl += step;
    drawText("Portrait:", cxL, yl + 4, ThText, 12);
    textField(2021, cxL + fxOff, yl, colW - fxOff, fh, line.portrait,
              [&line](const std::string& s){ line.portrait = s; });
    yl += step;
    drawText("Side:", cxL, yl + 4, ThText, 12);
    if (button(2022, line.portraitLeft ? "Left" : "Right", cxL + fxOff, yl, 70, fh, false))
        line.portraitLeft = !line.portraitLeft;
    yl += step;
    drawText("SFX:", cxL, yl + 4, ThText, 12);
    textField(2023, cxL + fxOff, yl, colW - fxOff, fh, line.sfxPath,
              [&line](const std::string& s){ line.sfxPath = s; });
    yl += step;

    // Right column: text + choices
    int yr = y;
    drawText("Text:", cxR, yr + 4, ThText, 12);
    textField(2024, cxR + 44, yr, colW - 44, fh, line.text,
              [&line](const std::string& s){ line.text = s; });
    yr += step;
    drawText("Choices (branch):", cxR, yr + 4, ThHead, 12);
    if ((int)line.choices.size() < 4 &&
        button(2030, "+ Choice", cxR + colW - 70, yr, 64, 16, false)) {
        CsDialogChoice c; c.text = "Choice"; line.choices.push_back(c);
    }
    yr += 22;
    for (int ci = 0; ci < (int)line.choices.size(); ci++) {
        CsDialogChoice& ch = line.choices[ci];
        char cl[8]; snprintf(cl, sizeof(cl), "%d", ci + 1);
        drawText(cl, cxR, yr + 3, ThText, 11);
        textField(2040 + ci * 3, cxR + 16, yr, colW - 16 - 24, fh, ch.text,
                  [&ch](const std::string& s){ ch.text = s; });
        if (button(2041 + ci * 3, "X", cxR + colW - 22, yr, 20, fh, false)) {
            line.choices.erase(line.choices.begin() + ci);
            break;
        }
        yr += 22;
        drawText("->seq", cxR + 16, yr + 3, ThDim, 10);
        textField(2042 + ci * 3, cxR + 56, yr, colW - 56, fh, ch.nextSeqId,
                  [&ch](const std::string& s){ ch.nextSeqId = s; });
        yr += 22;
        // Visibility condition: only show this choice when <var> <cmp> <value>
        // (blank var = always). Lets dialog branch on a game variable.
        drawText("if", cxR + 16, yr + 3, ThDim, 10);
        textField(2200 + ci * 3, cxR + 32, yr, 64, fh, ch.condVar,
                  [&ch](const std::string& s){ ch.condVar = s; });
        static const char* cmpSym[] = {"==","!=",">","<",">=","<="};
        if (button(2201 + ci * 3, cmpSym[ch.condCmp % 6], cxR + 98, yr, 28, fh, false))
            ch.condCmp = (uint8_t)((ch.condCmp + 1) % 6);
        intField(2202 + ci * 3, cxR + 128, yr, colW - 128, fh, &ch.condValue, 1, -999999, 999999);
        yr += step;
    }
    if (line.choices.empty())
        drawText("none", cxR, yr + 2, ThDim, 10);
}

void CutsceneEditor::renderHeader(int screenW) {
    // Navy title bar (like a Win98 window) with a drag grip; the top edge of
    // the whole panel is the resize handle.
    fillRect(0, panelY_, screenW, HEADER_H, ThTitle);
    drawLine(0, panelY_, screenW, panelY_, ThText);

    // Grip dashes in the center
    for (int i = -2; i <= 2; i++)
        fillRect(screenW / 2 + i * 14, panelY_ + 4, 8, 2, {120, 130, 200, 200});

    drawText("CUTSCENE EDITOR", 8, panelY_ + 4, ThTitleTx, 12);

    const Cutscene* cs = currentCutscene();
    char info[160];
    if (cs) {
        snprintf(info, sizeof(info), "%s   %.2fs   %d actors   %d events",
                 cs->id.c_str(), (double)cs->totalDuration(),
                 (int)cs->actors.size(), (int)cs->events.size());
    } else {
        snprintf(info, sizeof(info), "no cutscene selected");
    }
    drawText(info, 150, panelY_ + 4, {200, 205, 230, 255}, 12);

    drawText("saved with the map (Ctrl+S)", screenW - 240, panelY_ + 4, {170, 175, 210, 255}, 11);

    if (button(698, "X", screenW - 22, panelY_ + 2, 18, 16, false))
        wantsClose_ = true;
}

void CutsceneEditor::renderCutsceneList() {
    int x = listX_, y = colY_, w = listW_, h = colH_;
    fillRect(x, y, w, 16, ThFace);
    drawText("CUTSCENES", x + 6, y + 3, ThHead, 11);
    int listTopArea = y + 16;
    fillRect(x, listTopArea, w, h - 16, ThSunk);
    drawBevel(x, listTopArea, w, h - 16, false);

    const int btnRowH = 22;
    int listTop = listTopArea + 2;
    int listH   = h - 16 - 2 - btnRowH;

    if (lib_) {
        int n = (int)lib_->cutscenes.size();
        int maxScroll = std::max(0, n * ROW_H - listH);
        listScroll_ = clampi(listScroll_, 0, maxScroll);

        for (int i = 0; i < n; i++) {
            int ry = listTop + i * ROW_H - listScroll_;
            if (ry + ROW_H <= listTop || ry >= listTop + listH) continue;
            bool sel = (i == selectedCutscene_);
            bool hov = ui_->pointInRect(ui_->mouseX, ui_->mouseY, x + 1, ry, w - 2, ROW_H);
            if (sel)      fillRect(x + 1, ry, w - 2, ROW_H, ThSel);
            else if (hov) fillRect(x + 1, ry, w - 2, ROW_H, ThHover);
            drawText(lib_->cutscenes[i].id.c_str(), x + 6, ry + 4,
                     sel ? ThSelTx : ThText, 11);
            if (hov && ui_->mouseClicked) {
                ui_->mouseClicked = false;
                ui_->clickCooldownFrames = 2;
                releaseFocus(true);
                selectedCutscene_ = i;
                selectedActor_ = selectedEvent_ = -1;
                selectedDialogSeq_ = selectedDialogLine_ = -1;
                scrubTime_ = 0;
                playing_ = false;
                recomputePreview();
            }
        }
    }

    // Bottom buttons: New / Del (Del needs a confirm click)
    int by = y + h - btnRowH + 2;
    if (button(600, "+ New", x + 3, by, 52, 18, false)) {
        releaseFocus(true);
        addCutscene();
    }
    if (selectedCutscene_ >= 0) {
        bool armed = (deleteArmT_ > 0);
        if (button(601, armed ? "Sure?" : "Del", x + 58, by, 44, 18, armed)) {
            if (armed) {
                releaseFocus(false);
                deleteCutscene(selectedCutscene_);
                deleteArmT_ = 0;
            } else {
                deleteArmT_ = 2.0f;
                setStatus("Click Del again to delete the cutscene");
            }
        }
        if (deleteArmT_ > 0) deleteArmT_ -= ui_->dt;
    }
}

void CutsceneEditor::renderActorList() {
    int x = actorX_, y = colY_, w = actorW_, h = colH_;
    fillRect(x, y, w, 16, ThFace);
    drawText("ACTORS", x + 6, y + 3, ThHead, 11);
    int listTopArea = y + 16;
    fillRect(x, listTopArea, w, h - 16, ThSunk);
    drawBevel(x, listTopArea, w, h - 16, false);

    const int btnRowH = 22;
    int listTop = listTopArea + 2;
    int listH   = h - 16 - 2 - btnRowH;

    const Cutscene* cs = currentCutscene();
    if (cs) {
        int n = (int)cs->actors.size();
        int maxScroll = std::max(0, n * ROW_H - listH);
        actorScroll_ = clampi(actorScroll_, 0, maxScroll);

        static const char* typeTag[] = {"P", "E", "S"};
        static const SDL_Color tagCol[] = {
            {0, 80, 160, 255}, {200, 60, 20, 255}, {30, 130, 30, 255}};

        bool clickedRow = false;
        for (int i = 0; i < n; i++) {
            int ry = listTop + i * ROW_H - actorScroll_;
            if (ry + ROW_H <= listTop || ry >= listTop + listH) continue;
            bool sel = (i == selectedActor_);
            bool hov = ui_->pointInRect(ui_->mouseX, ui_->mouseY, x + 1, ry, w - 2, ROW_H);
            if (sel)      fillRect(x + 1, ry, w - 2, ROW_H, ThSel);
            else if (hov) fillRect(x + 1, ry, w - 2, ROW_H, ThHover);
            int ti = (int)cs->actors[i].type;
            if (ti < 0 || ti > 2) ti = 2;
            drawText(typeTag[ti], x + 6, ry + 4, sel ? ThSelTx : tagCol[ti], 11);
            drawText(cs->actors[i].name.c_str(), x + 22, ry + 4,
                     sel ? ThSelTx : ThText, 11);
            if (cs->actors[i].layer != 0) {
                char lb[8]; snprintf(lb, sizeof(lb), "L%d", cs->actors[i].layer);
                drawText(lb, x + w - 24, ry + 4, sel ? ThSelTx : SDL_Color{120,120,180,255}, 11);
            }
            if (hov && ui_->mouseClicked) {
                ui_->mouseClicked = false;
                ui_->clickCooldownFrames = 2;
                releaseFocus(true);
                selectedActor_ = i;
                selectedEvent_ = -1;
                clickedRow = true;
            }
        }
        // Click in empty list space deselects (back to scene view)
        if (!clickedRow && ui_->mouseClicked &&
            ui_->pointInRect(ui_->mouseX, ui_->mouseY, x + 1, listTop, w - 2, listH)) {
            int below = listTop + n * ROW_H - actorScroll_;
            if (ui_->mouseY >= below) {
                ui_->mouseClicked = false;
                ui_->clickCooldownFrames = 2;
                releaseFocus(true);
                selectedActor_ = -1;
                selectedEvent_ = -1;
            }
        }
    }

    int by = y + h - btnRowH + 2;
    if (button(610, "+ Actor", x + 3, by, 68, 18, showActorMenu_)) {
        showActorMenu_ = !showActorMenu_;
        showEventMenu_ = false;
    }
    if (selectedActor_ >= 0) {
        bool isPlayer = cs && selectedActor_ < (int)cs->actors.size() &&
                        cs->actors[selectedActor_].type == CsActorType::Player;
        if (!isPlayer && button(611, "Del", x + 74, by, 40, 18, false)) {
            releaseFocus(false);
            deleteActor(selectedActor_);
        }
    }
}

void CutsceneEditor::renderTimeline() {
    int x = tlX_, y = colY_, w = tlW_, h = colH_;

    const Cutscene* cs = currentCutscene();

    // -- Toolbar (silver strip) --
    int tby = y + 3;
    fillRect(x, y, w, TL_TOOL_H, ThFace);
    if (button(620, playing_ ? "||" : ">", x + 4, tby, 22, 18, false)) {
        playing_ = !playing_;
        const Cutscene* c2 = currentCutscene();
        if (playing_ && c2 && scrubTime_ >= c2->totalDuration() - 0.01f) scrubTime_ = 0;
    }
    if (button(621, "|<", x + 28, tby, 22, 18, false)) {
        scrubTime_ = 0;
        playing_ = false;
        timelineStart_ = 0;
        recomputePreview();
    }
    char timeBuf[48];
    float total = cs ? cs->totalDuration() : 0.0f;
    snprintf(timeBuf, sizeof(timeBuf), "%.2f / %.2fs", (double)scrubTime_, (double)total);
    drawText(timeBuf, x + 56, tby + 3, ThText, 11);

    if (button(622, "Snap", x + w - 170, tby, 50, 18, snapOn_))
        snapOn_ = !snapOn_;
    if (button(623, "-", x + w - 116, tby, 18, 18, false))
        timelineScale_ = std::max(10.0f, timelineScale_ * 0.8f);
    if (button(624, "+", x + w - 96, tby, 18, 18, false))
        timelineScale_ = std::min(400.0f, timelineScale_ * 1.25f);
    if (button(625, "+ Event", x + w - 74, tby, 72, 18, showEventMenu_)) {
        if (cs) {
            showEventMenu_ = !showEventMenu_;
            showActorMenu_ = false;
        } else {
            setStatus("Create a cutscene first (+ New on the left)");
        }
    }

    // -- Ruler --
    int rulerY = y + TL_TOOL_H;
    fillRect(x, rulerY, w, TL_RULER_H, ThSunkAlt);
    float secStep = 1.0f;
    if (timelineScale_ < 18) secStep = 5.0f;
    if (timelineScale_ < 7)  secStep = 10.0f;
    if (timelineScale_ > 180) secStep = 0.5f;
    float firstTick = ceilf(timelineStart_ / secStep) * secStep;
    for (float t = firstTick; t <= timelineStart_ + viewDuration() + 0.001f; t += secStep) {
        int px = timeToPx(t);
        if (px < x + 1 || px > x + w - 2) continue;
        drawLine(px, rulerY, px, rulerY + TL_RULER_H, ThShadow);
        char tb[16];
        if (secStep < 1.0f) snprintf(tb, sizeof(tb), "%.1f", (double)t);
        else                snprintf(tb, sizeof(tb), "%.0fs", (double)t);
        drawText(tb, px + 3, rulerY + 2, ThDim, 10);
    }

    // -- Rows --
    // Global (camera) row = TL_ROW_H; each actor row = TL_ROW_H*2 (two lanes).
    const int TL_ACTOR_H = TL_ROW_H * 2;
    int rowsY = rulerY + TL_RULER_H;
    int rowsH = h - TL_TOOL_H - TL_RULER_H - 1;
    int nActors = cs ? (int)cs->actors.size() : 0;
    int totalRowH = TL_ROW_H + nActors * TL_ACTOR_H;
    fillRect(x, rowsY, w, rowsH, ThSunk);
    drawBevel(x, rulerY, w, h - TL_TOOL_H, false);
    int maxRowScroll = std::max(0, totalRowH - rowsH);
    tlRowScroll_ = clampi(tlRowScroll_, 0, maxRowScroll);

    SDL_Rect clip = {x + 1, rowsY, w - 2, rowsH};
    SDL_RenderSetClipRect(r_, &clip);

    // rowY(0) = global; rowY(1+i) = top of actor i's double-height row.
    auto rowY = [&](int row) -> int {
        if (row == 0) return rowsY - tlRowScroll_;
        return rowsY + TL_ROW_H + (row - 1) * TL_ACTOR_H - tlRowScroll_;
    };

    // Global row
    {
        int ry = rowY(0);
        fillRect(x + 1, ry, w - 2, TL_ROW_H, {210, 214, 220, 255});
        drawText("[camera/global]", x + 5, ry + 5, ThDim, 10);
        drawLine(x + 1, ry + TL_ROW_H - 1, x + w - 1, ry + TL_ROW_H - 1, ThGrid);
    }
    if (cs) {
        for (int i = 0; i < nActors; i++) {
            int ry = rowY(1 + i);
            bool selRow = (i == selectedActor_);
            SDL_Color rowCol = selRow ? SDL_Color{200, 212, 235, 255}
                                      : (i % 2 ? ThSunk : ThSunkAlt);
            fillRect(x + 1, ry, w - 2, TL_ACTOR_H, rowCol);
            drawText(cs->actors[i].name.c_str(), x + 5, ry + 4,
                     selRow ? ThHead : ThText, 10);
            // Lane divider
            drawLine(x + 1, ry + TL_ROW_H, x + w - 1, ry + TL_ROW_H,
                     {160, 164, 172, 180});
            drawLine(x + 1, ry + TL_ACTOR_H - 1, x + w - 1, ry + TL_ACTOR_H - 1, ThGrid);
        }

        // Vertical second gridlines
        for (float t = firstTick; t <= timelineStart_ + viewDuration() + 0.001f; t += secStep) {
            int px = timeToPx(t);
            if (px >= x + 1 && px <= x + w - 2)
                drawLine(px, rowsY, px, rowsY + rowsH, {170, 170, 176, 120});
        }

        // Total duration marker
        if (total > 0.01f) {
            int dpx = timeToPx(total);
            if (dpx >= x + 1 && dpx <= x + w - 2)
                for (int yy = rowsY; yy < rowsY + rowsH; yy += 6)
                    drawLine(dpx, yy, dpx, yy + 3, {200, 60, 60, 200});
        }

        // Greedy lane assignment per actor (by event-vector order).
        // lane[ei]: 0 = top sub-row, 1 = bottom sub-row.
        std::vector<int> lane(cs->events.size(), 0);
        for (int ai = -1; ai < nActors; ai++) {
            uint32_t actorId = (ai < 0) ? 0 : cs->actors[ai].id;
            float laneEnd[2] = {-1e9f, -1e9f};
            for (int ei = 0; ei < (int)cs->events.size(); ei++) {
                if (cs->events[ei].actorId != actorId) continue;
                const auto& ev2 = cs->events[ei];
                float evEnd = ev2.startTime + std::max(ev2.duration, 0.06f);
                if (ev2.startTime >= laneEnd[0] - 0.001f) {
                    lane[ei] = 0; laneEnd[0] = evEnd;
                } else {
                    lane[ei] = 1; laneEnd[1] = evEnd;
                }
            }
        }

        // Event blocks
        for (int ei = 0; ei < (int)cs->events.size(); ei++) {
            const auto& ev = cs->events[ei];
            int row = 0;
            for (int i = 0; i < nActors; i++)
                if (cs->actors[i].id == ev.actorId) { row = 1 + i; break; }

            int laneOff = (row > 0) ? lane[ei] * TL_ROW_H : 0;
            int ry  = rowY(row) + laneOff + 2;
            int rh  = TL_ROW_H - 5;
            int px0 = timeToPx(ev.startTime);
            int px1 = timeToPx(ev.startTime + std::max(ev.duration, 0.06f));
            if (px1 - px0 < 6) px1 = px0 + 6;
            if (px0 > x + w || px1 < x) continue;

            SDL_Color bc = eventColor(ev.type);
            bool isSel = (ei == selectedEvent_);
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r_, bc.r, bc.g, bc.b, isSel ? 255 : 185);
            SDL_Rect er = {px0, ry, px1 - px0, rh};
            SDL_RenderFillRect(r_, &er);
            if (isSel) {
                SDL_SetRenderDrawColor(r_, 255, 255, 255, 255);
                SDL_RenderDrawRect(r_, &er);
            }
            SDL_SetRenderDrawColor(r_, 0, 0, 0, 90);
            SDL_Rect grip = {px1 - 4, ry, 4, rh};
            SDL_RenderFillRect(r_, &grip);
            if (px1 - px0 > 30)
                drawText(csEventTypeName(ev.type), px0 + 3, ry + 3, {10, 10, 10, 255}, 10);
        }
    }

    // Scrubber (red playhead reads well on the light timeline)
    const SDL_Color scrubCol = {200, 30, 30, 255};
    int spx = timeToPx(scrubTime_);
    if (spx >= x + 1 && spx <= x + w - 2) {
        drawLine(spx, rulerY, spx, rowsY + rowsH, scrubCol);
        SDL_RenderSetClipRect(r_, nullptr);
        fillRect(spx - 4, rulerY, 9, 7, scrubCol);
    }
    SDL_RenderSetClipRect(r_, nullptr);

    if (!cs) {
        drawText("No cutscene. Click '+ New' on the left to create one.",
                 x + 12, rowsY + 12, ThDim, 12);
    }
}

// ---- Inspector ----

void CutsceneEditor::renderInspector() {
    int x = propsX_, y = colY_, w = propsW_, h = colH_;
    fillRect(x, y, w, 18, ThFace);

    Cutscene* cs = current();

    // Header with context tabs
    const char* title = "SCENE";
    if (cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) title = "EVENT";
    else if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size()) title = "ACTOR";
    drawText(title, x + 6, y + 4, ThHead, 11);
    if ((selectedEvent_ >= 0 || selectedActor_ >= 0) &&
        button(640, "Scene", x + w - 58, y + 2, 54, 16, false)) {
        releaseFocus(true);
        selectedEvent_ = -1;
        selectedActor_ = -1;
    }

    // Sunken white content area
    int contentTop = y + 18;
    int contentH   = h - 18;
    fillRect(x, contentTop, w, contentH, ThSunk);
    drawBevel(x, contentTop, w, contentH, false);

    if (!cs) {
        if (lib_) {
            int cy2 = contentTop + 8;
            const int lx2 = x + 6, fx2 = x + 78, fw2 = w - 78 - 10, fh2 = 18;
            drawText("LIBRARY", lx2, cy2 + 2, ThHead, 11);
            cy2 += 20;
            drawText("On Death CS:", lx2, cy2 + 3, ThText, 11);
            textField(1260, fx2, cy2, fw2, fh2, lib_->onDeathId, [this](const std::string& s) {
                lib_->onDeathId = s;
            });
            cy2 += 22;
            drawText("Cutscene ID to play when player dies.", lx2, cy2 + 2, ThDim, 10);
            cy2 += 14;
            drawText("Add a Death Screen event inside it to", lx2, cy2 + 2, ThDim, 10);
            cy2 += 14;
            drawText("show the retry/quit screen.", lx2, cy2 + 2, ThDim, 10);
        } else {
            drawText("No cutscene selected.", x + 6, contentTop + 8, ThDim, 11);
        }
        return;
    }
    contentTop += 2;
    contentH   -= 2;
    clipActive_ = true;
    clipY0_ = contentTop;
    clipY1_ = contentTop + contentH;
    SDL_Rect clip = {x, contentTop, w, contentH};
    SDL_RenderSetClipRect(r_, &clip);

    int maxScroll = std::max(0, propsContentH_ - contentH);
    propsScroll_  = clampi(propsScroll_, 0, maxScroll);
    int cy = contentTop + 4 - propsScroll_;
    int startCy = cy;

    if (selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
        cy = inspectEvent(x, cy, w, cs->events[selectedEvent_]);
    } else if (selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size()) {
        cy = inspectActor(x, cy, w, cs->actors[selectedActor_]);
    } else {
        cy = inspectScene(x, cy, w);
        cy = inspectDialogs(x, cy, w);
    }

    propsContentH_ = cy - startCy + 8;
    SDL_RenderSetClipRect(r_, nullptr);
    clipActive_ = false;

    // Scrollbar hint
    if (propsContentH_ > contentH) {
        float ratio = (float)contentH / propsContentH_;
        int barH = std::max(16, (int)(contentH * ratio));
        int barY = contentTop + (int)((contentH - barH) *
                   (maxScroll > 0 ? (float)propsScroll_ / maxScroll : 0));
        fillRect(x + w - 5, barY, 3, barH, ThShadow);
    }
}

int CutsceneEditor::inspectScene(int x, int cy, int w) {
    Cutscene* cs = current();
    if (!cs) return cy;
    const int lx = x + 6, fx = x + 78, fw = w - 78 - 10, fh = 18, step = 23;

    drawText("ID:", lx, cy + 3, ThText, 11);
    textField(1000, fx, cy, fw, fh, cs->id, [this, cs](const std::string& s) {
        if (!s.empty()) cs->id = s;
    });
    cy += step;

    drawText("Block input:", lx, cy + 3, ThText, 11);
    if (button(641, cs->blockInput ? "Yes" : "No", fx, cy, 56, fh, cs->blockInput))
        cs->blockInput = !cs->blockInput;
    cy += step;

    drawText("Chain next:", lx, cy + 3, ThText, 11);
    textField(1001, fx, cy, fw, fh, cs->chainOnEnd, [cs](const std::string& s) {
        cs->chainOnEnd = s;
    });
    cy += step;

    drawText("Tip: select an actor or event to edit it;", lx, cy + 2, ThDim, 10);
    cy += 13;
    drawText("drag actor markers directly on the map.", lx, cy + 2, ThDim, 10);
    cy += 18;
    drawLine(lx, cy, x + w - 8, cy, ThShadow);
    cy += 6;
    return cy;
}

int CutsceneEditor::inspectDialogs(int x, int cy, int w) {
    Cutscene* cs = current();
    if (!cs) return cy;
    const int lx = x + 6, fx = x + 78, fw = w - 78 - 10, fh = 18, step = 23;

    drawText("DIALOGS", lx, cy + 2, ThHead, 11);
    if (button(642, "+ Seq", x + w - 60, cy, 52, 16, false)) {
        addDialogSeq();
        openDialogModal(selectedDialogSeq_);   // jump straight into the editor
    }
    cy += 20;
    (void)fx; (void)fw; (void)step;

    // Each sequence is a row with an "Edit..." button that opens the
    // full-screen dialog editor (with live preview).
    for (int i = 0; i < (int)cs->dialogs.size(); i++) {
        char db[96];
        snprintf(db, sizeof(db), "%s  (%d)", cs->dialogs[i].id.c_str(),
                 (int)cs->dialogs[i].lines.size());
        drawText(db, lx, cy + 3, ThText, 11);
        if (button(660 + i * 2, "Edit", x + w - 86, cy, 44, fh, false))
            openDialogModal(i);
        if (button(661 + i * 2, "Del", x + w - 40, cy, 34, fh, false)) {
            releaseFocus(false);
            deleteDialogSeq(i);
            return cy + 22;
        }
        cy += 22;
    }
    if (cs->dialogs.empty()) {
        drawText("(none - used by Dialog events)", lx, cy + 2, ThDim, 10);
        cy += 16;
    }
    cy += 4;
    drawText(":)", lx, cy + 2, ThDim, 10);
    cy += 14;
    return cy;
}

int CutsceneEditor::inspectActor(int x, int cy, int w, CsActor& a) {
    const int lx = x + 6, fx = x + 78, fw = w - 78 - 10, fh = 18, step = 23;
    const SDL_Color lab = ThText;

    drawText("Name:", lx, cy + 3, lab, 11);
    textField(1100, fx, cy, fw, fh, a.name, [&a](const std::string& s) {
        if (!s.empty()) a.name = s;
    });
    cy += step;

    static const char* typeNames[] = {"Player", "Enemy", "Sprite"};
    drawText("Type:", lx, cy + 3, lab, 11);
    if (button(660, typeNames[(int)a.type < 3 ? (int)a.type : 2], fx, cy, 70, fh, false)) {
        a.type = (CsActorType)(((int)a.type + 1) % 3);
        recomputePreview();
    }
    if (a.type == CsActorType::Enemy) {
        if (button(661, enemyKindName(a.enemyType), fx + 76, cy, 70, fh, false)) {
            a.enemyType = (CsEnemyType)(((int)a.enemyType + 1) % 6);
        }
    }
    cy += step;

    if (a.type == CsActorType::FreeSprite) {
        drawText("Sprite:", lx, cy + 3, lab, 11);
        textField(1101, fx, cy, fw, fh, a.spritePath, [&a](const std::string& s) {
            a.spritePath = s;
        });
        cy += step;
        drawText("Flip H:", lx, cy + 3, lab, 11);
        if (button(662, a.flipH ? "Yes" : "No", fx, cy, 56, fh, a.flipH))
            a.flipH = !a.flipH;
        cy += step;
    }

    drawText("Start X/Y:", lx, cy + 3, lab, 11);
    int half = (fw - 4) / 2;
    floatField(1102, fx, cy, half, fh, &a.startX, 16, -100000, 100000, "%.0f");
    floatField(1103, fx + half + 4, cy, half, fh, &a.startY, 16, -100000, 100000, "%.0f");
    cy += step;

    bool pickThis = (pickMode_ == CsPickMode::ActorStart);
    if (button(663, pickThis ? "Picking..." : "Pick on map", fx, cy, 110, fh, pickThis)) {
        pickMode_ = pickThis ? CsPickMode::None : CsPickMode::ActorStart;
    }
    cy += step;

    drawText("Rotation:", lx, cy + 3, lab, 11);
    floatField(1104, fx, cy, fw, fh, &a.startRot, 15, -360, 360, "%.0f");
    cy += step;

    drawText("Scale X/Y:", lx, cy + 3, lab, 11);
    floatField(1105, fx, cy, half, fh, &a.startScaleX, 0.1f, 0.05f, 20, "%.2f");
    floatField(1106, fx + half + 4, cy, half, fh, &a.startScaleY, 0.1f, 0.05f, 20, "%.2f");
    cy += step;

    drawText("Alpha:", lx, cy + 3, lab, 11);
    floatField(1107, fx, cy, fw, fh, &a.startAlpha, 0.1f, 0, 1, "%.2f");
    cy += step;

    drawText("Visible:", lx, cy + 3, lab, 11);
    if (button(664, a.startVisible ? "Yes" : "No", fx, cy, 56, fh, a.startVisible)) {
        a.startVisible = !a.startVisible;
        recomputePreview();
    }
    cy += step;

    drawText("Layer:", lx, cy + 3, lab, 11);
    if (button(670, "-", fx, cy, 20, fh, false) && a.layer > -99) a.layer--;
    char layBuf[8]; snprintf(layBuf, sizeof(layBuf), "%d", a.layer);
    drawText(layBuf, fx + 26, cy + 3, lab, 11);
    if (button(671, "+", fx + 26 + 20, cy, 20, fh, false) && a.layer < 99) a.layer++;
    cy += step + 4;

    if (button(665, "Delete Actor", lx, cy, w - 16, 20, false)) {
        releaseFocus(false);
        deleteActor(selectedActor_);
    }
    cy += 26;
    return cy;
}

int CutsceneEditor::inspectEvent(int x, int cy, int w, CsEvent& ev) {
    Cutscene* cs = current();
    const int lx = x + 6, fx = x + 78, fw = w - 78 - 10, fh = 18, step = 23;
    const SDL_Color lab = ThText;
    int half = (fw - 4) / 2;

    // Type + owning actor
    {
        SDL_Color ec = eventColor(ev.type);
        fillRect(lx, cy + 2, 8, 12, ec);
        drawText(csEventTypeName(ev.type), lx + 14, cy + 3, ThText, 12);
        const char* who = "[global]";
        if (cs && ev.actorId != 0) {
            for (auto& a : cs->actors) if (a.id == ev.actorId) { who = a.name.c_str(); break; }
        }
        drawText(who, fx + 60, cy + 4, {0, 110, 40, 255}, 10);
        cy += step;
    }

    drawText("Start (s):", lx, cy + 3, lab, 11);
    floatField(1200, fx, cy, fw, fh, &ev.startTime, 0.1f, 0, 6000, "%.2f");
    cy += step;

    drawText("Length (s):", lx, cy + 3, lab, 11);
    floatField(1201, fx, cy, fw, fh, &ev.duration, 0.1f, 0, 6000, "%.2f");
    cy += step;

    drawText("Ease:", lx, cy + 3, lab, 11);
    if (button(670, easeName(ev.ease), fx, cy, 110, fh, false)) {
        ev.ease = (CsEase)(((int)ev.ease + 1) % 5);
        recomputePreview();
    }
    cy += step;
    drawLine(lx, cy, x + w - 8, cy, ThShadow);
    cy += 5;

    auto pickBtn = [&](int id, CsPickMode mode, const char* idleLabel) {
        bool on = (pickMode_ == mode);
        if (button(id, on ? "Picking..." : idleLabel, fx, cy, 110, fh, on))
            pickMode_ = on ? CsPickMode::None : mode;
        cy += step;
    };

    switch (ev.type) {
        case CsEventType::Move:
        case CsEventType::CameraMove:
            drawText("From X/Y:", lx, cy + 3, lab, 11);
            floatField(1210, fx, cy, half, fh, &ev.fromX, 16, -100000, 100000, "%.0f");
            floatField(1211, fx + half + 4, cy, half, fh, &ev.fromY, 16, -100000, 100000, "%.0f");
            cy += step;
            pickBtn(671, CsPickMode::EventFrom, "Pick FROM");
            drawText("To X/Y:", lx, cy + 3, lab, 11);
            floatField(1212, fx, cy, half, fh, &ev.toX, 16, -100000, 100000, "%.0f");
            floatField(1213, fx + half + 4, cy, half, fh, &ev.toY, 16, -100000, 100000, "%.0f");
            cy += step;
            pickBtn(672, CsPickMode::EventTo, "Pick TO");
            break;
        case CsEventType::Rotate:
            drawText("From deg:", lx, cy + 3, lab, 11);
            floatField(1214, fx, cy, fw, fh, &ev.fromRot, 15, -3600, 3600, "%.0f");
            cy += step;
            drawText("To deg:", lx, cy + 3, lab, 11);
            floatField(1215, fx, cy, fw, fh, &ev.toRot, 15, -3600, 3600, "%.0f");
            cy += step;
            break;
        case CsEventType::Scale:
            drawText("From X/Y:", lx, cy + 3, lab, 11);
            floatField(1216, fx, cy, half, fh, &ev.fromScaleX, 0.1f, 0.05f, 20, "%.2f");
            floatField(1217, fx + half + 4, cy, half, fh, &ev.fromScaleY, 0.1f, 0.05f, 20, "%.2f");
            cy += step;
            drawText("To X/Y:", lx, cy + 3, lab, 11);
            floatField(1218, fx, cy, half, fh, &ev.toScaleX, 0.1f, 0.05f, 20, "%.2f");
            floatField(1219, fx + half + 4, cy, half, fh, &ev.toScaleY, 0.1f, 0.05f, 20, "%.2f");
            cy += step;
            break;
        case CsEventType::Alpha:
            drawText("From:", lx, cy + 3, lab, 11);
            floatField(1220, fx, cy, fw, fh, &ev.fromAlpha, 0.1f, 0, 1, "%.2f");
            cy += step;
            drawText("To:", lx, cy + 3, lab, 11);
            floatField(1221, fx, cy, fw, fh, &ev.toAlpha, 0.1f, 0, 1, "%.2f");
            cy += step;
            break;
        case CsEventType::Flash:
            drawText("R / G / B:", lx, cy + 3, lab, 11);
            {
                int third = (fw - 8) / 3;
                floatField(1222, fx, cy, third, fh, &ev.flashR, 15, 0, 255, "%.0f");
                floatField(1223, fx + third + 4, cy, third, fh, &ev.flashG, 15, 0, 255, "%.0f");
                floatField(1224, fx + (third + 4) * 2, cy, third, fh, &ev.flashB, 15, 0, 255, "%.0f");
            }
            cy += step;
            break;
        case CsEventType::CameraZoom:
            drawText("From zoom:", lx, cy + 3, lab, 11);
            floatField(1225, fx, cy, fw, fh, &ev.fromZoom, 0.1f, 0.1f, 8, "%.2f");
            cy += step;
            drawText("To zoom:", lx, cy + 3, lab, 11);
            floatField(1226, fx, cy, fw, fh, &ev.toZoom, 0.1f, 0.1f, 8, "%.2f");
            cy += step;
            break;
        case CsEventType::CameraShake:
            drawText("Strength:", lx, cy + 3, lab, 11);
            floatField(1227, fx, cy, fw, fh, &ev.shakeStrength, 1, 0, 100, "%.1f");
            cy += step;
            break;
        case CsEventType::CameraRotate:
            drawText("From deg:", lx, cy + 3, lab, 11);
            floatField(1228, fx, cy, fw, fh, &ev.fromRot, 5, -360, 360, "%.0f");
            cy += step;
            drawText("To deg:", lx, cy + 3, lab, 11);
            floatField(1229, fx, cy, fw, fh, &ev.toRot, 5, -360, 360, "%.0f");
            cy += step;
            break;
        case CsEventType::ScreenFade:
            drawText("Direction:", lx, cy + 3, lab, 11);
            if (button(673, ev.fadeToBlack ? "To black" : "From black", fx, cy, 100, fh, false))
                ev.fadeToBlack = !ev.fadeToBlack;
            cy += step;
            break;
        case CsEventType::CinematicBars:
            drawText("Bars:", lx, cy + 3, lab, 11);
            if (button(673, ev.showBars ? "Show" : "Hide", fx, cy, 70, fh, false))
                ev.showBars = !ev.showBars;
            cy += step;
            break;
        case CsEventType::SetVisible:
            drawText("Visible:", lx, cy + 3, lab, 11);
            if (button(673, ev.visible ? "Yes" : "No", fx, cy, 56, fh, ev.visible)) {
                ev.visible = !ev.visible;
                recomputePreview();
            }
            cy += step;
            break;
        case CsEventType::SetFrame:
            drawText("Frame:", lx, cy + 3, lab, 11);
            intField(1228, fx, cy, fw, fh, &ev.frame, 1, 0, 255);
            cy += step;
            break;
        case CsEventType::SpawnExplosion:
            drawText("At X/Y:", lx, cy + 3, lab, 11);
            floatField(1229, fx, cy, half, fh, &ev.explX, 16, -100000, 100000, "%.0f");
            floatField(1230, fx + half + 4, cy, half, fh, &ev.explY, 16, -100000, 100000, "%.0f");
            cy += step;
            pickBtn(671, CsPickMode::EventExplosion, "Pick on map");
            break;
        case CsEventType::Dialog:
            drawText("Dialog ID:", lx, cy + 3, lab, 11);
            textField(1231, fx, cy, fw, fh, ev.dialogId, [&ev](const std::string& s) {
                ev.dialogId = s;
            });
            cy += step;
            drawText("(create sequences in the Scene view)", lx, cy + 2, ThDim, 10);
            cy += 16;
            break;
        case CsEventType::PlaySFX:
            drawText("SFX path:", lx, cy + 3, lab, 11);
            textField(1232, fx, cy, fw, fh, ev.sfxPath, [&ev](const std::string& s) {
                ev.sfxPath = s;
            });
            cy += step;
            break;
        case CsEventType::SpawnActor:
            drawText("Override:", lx, cy + 3, lab, 11);
            if (button(673, ev.spawnOverridePos ? "Position set" : "Keep start pos",
                       fx, cy, 110, fh, ev.spawnOverridePos)) {
                ev.spawnOverridePos = !ev.spawnOverridePos;
                recomputePreview();
            }
            cy += step;
            if (ev.spawnOverridePos) {
                drawText("Spawn X/Y:", lx, cy + 3, lab, 11);
                floatField(1233, fx, cy, half, fh, &ev.spawnX, 16, -100000, 100000, "%.0f");
                floatField(1234, fx + half + 4, cy, half, fh, &ev.spawnY, 16, -100000, 100000, "%.0f");
                cy += step;
                pickBtn(671, CsPickMode::EventSpawnPos, "Pick on map");
            }
            break;
        case CsEventType::SpawnEnemy: {
            static const char* enemyNames[] = {
                "Melee","Shooter","Brute","Scout","Sniper","Gunner"};
            drawText("At X/Y:", lx, cy + 3, lab, 11);
            floatField(1241, fx, cy, half, fh, &ev.explX, 16, -100000, 100000, "%.0f");
            floatField(1242, fx + half + 4, cy, half, fh, &ev.explY, 16, -100000, 100000, "%.0f");
            cy += step;
            pickBtn(671, CsPickMode::EventExplosion, "Pick on map");
            drawText("Type:", lx, cy + 3, lab, 11);
            int et = (int)ev.spawnEnemyTypeId;
            if (button(680, et > 0 ? "<" : " ", fx, cy, 16, fh, false) && et > 0) {
                ev.spawnEnemyTypeId--; }
            drawText(enemyNames[et], fx + 18, cy + 3, ThText, 11);
            if (button(681, et < 5 ? ">" : " ", fx + 18 + 48, cy, 16, fh, false) && et < 5) {
                ev.spawnEnemyTypeId++; }
            cy += step;
            break;
        }
        case CsEventType::SpawnPickup: {
            drawText("At X/Y:", lx, cy + 3, lab, 11);
            floatField(1243, fx, cy, half, fh, &ev.explX, 16, -100000, 100000, "%.0f");
            floatField(1244, fx + half + 4, cy, half, fh, &ev.explY, 16, -100000, 100000, "%.0f");
            cy += step;
            pickBtn(671, CsPickMode::EventExplosion, "Pick on map");
            drawText("Type ID:", lx, cy + 3, lab, 11);
            int pt = (int)ev.spawnPickupTypeId;
            if (button(682, pt > 0 ? "<" : " ", fx, cy, 16, fh, false) && pt > 0)
                ev.spawnPickupTypeId--;
            char ptbuf[16]; snprintf(ptbuf, sizeof(ptbuf), "%d", pt);
            drawText(ptbuf, fx + 18, cy + 3, ThText, 11);
            if (button(683, pt < 53 ? ">" : " ", fx + 18 + 28, cy, 16, fh, false) && pt < 53)
                ev.spawnPickupTypeId++;
            cy += step;
            break;
        }
        case CsEventType::DespawnActor:
        case CsEventType::Wait:
        case CsEventType::EndCutscene:
            break;
        case CsEventType::SetFlag:
            drawText("Flag:", lx, cy + 3, lab, 11);
            textField(1235, fx, cy, fw, fh, ev.flagName, [&ev](const std::string& s) {
                ev.flagName = s;
            });
            cy += step;
            drawText("Value:", lx, cy + 3, lab, 11);
            if (button(673, ev.flagValue ? "true" : "false", fx, cy, 60, fh, ev.flagValue))
                ev.flagValue = !ev.flagValue;
            cy += step;
            break;
        case CsEventType::ChainCutscene:
            drawText("Chain to:", lx, cy + 3, lab, 11);
            textField(1236, fx, cy, fw, fh, ev.chainCsId, [&ev](const std::string& s) {
                ev.chainCsId = s;
            });
            cy += step;
            break;
        case CsEventType::AdjustSignal:
            drawText("Delta:", lx, cy + 3, lab, 11);
            intField(1237, fx, cy, fw, fh, &ev.signalDelta, 5, -100, 100);
            cy += step;
            break;
        case CsEventType::BranchCutscene: {
            static const char* varNames3[] = {"SIGNAL", "Route", "Var"};
            drawText("Variable:", lx, cy + 3, lab, 11);
            if (button(673, varNames3[ev.branchVar % 3], fx, cy, 70, fh, false))
                ev.branchVar = (ev.branchVar + 1) % 3;
            cy += step;
            if (ev.branchVar == 2) {
                drawText("Var name:", lx, cy + 3, lab, 11);
                textField(676, fx, cy, fw, fh, ev.flagName, [&ev](const std::string& s) {
                    ev.flagName = s;
                });
                cy += step;
            }
            drawText("Compare:", lx, cy + 3, lab, 11);
            static const char* cmpNames6[] = {">","!=","==","<",">=","<="};
            if (button(674, cmpNames6[ev.branchCmp % 6], fx, cy, 44, fh, false))
                ev.branchCmp = (uint8_t)(((int)ev.branchCmp + 1) % 6);
            cy += step;
            drawText("Threshold:", lx, cy + 3, lab, 11);
            intField(1238, fx, cy, fw, fh, &ev.branchThreshold, 5, -1000, 1000);
            cy += step;
            drawText("If true:", lx, cy + 3, lab, 11);
            textField(1239, fx, cy, fw, fh, ev.chainCsId, [&ev](const std::string& s) {
                ev.chainCsId = s;
            });
            cy += step;
            drawText("If false:", lx, cy + 3, lab, 11);
            textField(1240, fx, cy, fw, fh, ev.chainFalseId, [&ev](const std::string& s) {
                ev.chainFalseId = s;
            });
            cy += step;
            break;
        }
        case CsEventType::SetVariable: {
            static const char* opNames[] = {"Set", "Add", "Sub"};
            static const char* scopeNames[] = {"Local", "Pack"};
            drawText("Var name:", lx, cy + 3, ThText, 11);
            textField(1250, fx, cy, fw, fh, ev.varName, [&ev](const std::string& s) {
                if (!s.empty()) ev.varName = s;
            });
            cy += step;
            drawText("Value:", lx, cy + 3, ThText, 11);
            intField(1251, fx, cy, half, fh, &ev.varValue, 1, -999999, 999999);
            cy += step;
            drawText("Op:", lx, cy + 3, ThText, 11);
            if (button(1252, opNames[ev.varOp % 3], fx, cy, 46, fh, false))
                ev.varOp = (ev.varOp + 1) % 3;
            drawText("Scope:", fx + 54, cy + 3, ThText, 11);
            if (button(1253, scopeNames[ev.varScope % 2], fx + 104, cy, 50, fh, ev.varScope == 1))
                ev.varScope = ev.varScope ? 0 : 1;
            cy += step;
            break;
        }
        case CsEventType::DeathScreen:
            drawText("Triggers death screen.", lx, cy + 3, {255, 100, 100, 255}, 11);
            cy += step;
            drawText("Only active in the ondeath cutscene.", lx, cy + 3, ThDim, 10);
            cy += 14;
            break;
        case CsEventType::LoadMap:
            drawText("Map path:", lx, cy + 3, lab, 11);
            textField(1261, fx, cy, fw, fh, ev.mapPath, [&ev](const std::string& s) {
                ev.mapPath = s;
            });
            cy += step;
            drawText("Relative to romfs/ or absolute.", lx, cy + 3, ThDim, 10);
            cy += 14;
            break;
        case CsEventType::PostFXAcid: {
            drawText("Enable:", lx, cy + 3, lab, 11);
            if (button(1270, ev.flagValue ? "ON" : "OFF", fx, cy, 50, fh, ev.flagValue))
                ev.flagValue = !ev.flagValue;
            cy += step;
            drawText("Duration:", lx, cy + 3, lab, 11);
            floatField(1271, fx, cy, fw, fh, &ev.duration, 0.5f, 0.0f, 9999.0f, "%.1f");
            cy += step;
            drawText("(0 = permanent until disabled)", lx, cy + 3, ThDim, 10);
            cy += 14;
            drawText("Color 1:", lx, cy + 3, lab, 11);
            floatField(1272, fx,            cy, fw/3 - 2, fh, &ev.acidColor1R, 1, 0, 255, "%.0f");
            floatField(1273, fx + fw/3,     cy, fw/3 - 2, fh, &ev.acidColor1G, 1, 0, 255, "%.0f");
            floatField(1274, fx + fw/3*2,   cy, fw/3 - 2, fh, &ev.acidColor1B, 1, 0, 255, "%.0f");
            cy += step;
            drawText("Color 2:", lx, cy + 3, lab, 11);
            floatField(1275, fx,            cy, fw/3 - 2, fh, &ev.acidColor2R, 1, 0, 255, "%.0f");
            floatField(1276, fx + fw/3,     cy, fw/3 - 2, fh, &ev.acidColor2G, 1, 0, 255, "%.0f");
            floatField(1277, fx + fw/3*2,   cy, fw/3 - 2, fh, &ev.acidColor2B, 1, 0, 255, "%.0f");
            cy += step;
            break;
        }
        case CsEventType::ConsoleCmd:
            drawText("Cmd:", lx, cy + 3, lab, 11);
            textField(1280, fx, cy, fw, fh, ev.consoleCmd, [&ev](const std::string& s) {
                ev.consoleCmd = s;
            });
            cy += step;
            drawText("Use {varname} for var substitution.", lx, cy + 3, ThDim, 10);
            cy += 14;
            break;
        default: break;
    }

    cy += 4;
    drawLine(lx, cy, x + w - 8, cy, ThShadow);
    cy += 5;
    if (button(675, "Copy", lx, cy, 60, 20, false)) {
        clipboard_ = ev;
        hasClipboard_ = true;
        setStatus("Event copied (Ctrl+V on the timeline to paste)");
    }
    if (button(676, "Delete Event", lx + 66, cy, w - 66 - 16, 20, false)) {
        releaseFocus(false);
        deleteEvent(selectedEvent_);
    }
    cy += 26;
    return cy;
}

void CutsceneEditor::renderHintBar() {
    int y = screenH_ - HINT_H;
    fillRect(0, y, screenW_, HINT_H, ThFace);
    drawLine(0, y, screenW_, y, ThShadow);
    const char* hint;
    if (pickMode_ != CsPickMode::None) {
        hint = pickHint();
    } else if (statusMsgT_ > 0 && !statusMsg_.empty()) {
        hint = statusMsg_.c_str();
    } else {
        hint = "Space: play/pause   Drag blocks to move, right edge to resize   "
               "Right-click block: delete   Shift: toggle snap   Ctrl+C/V: copy/paste   "
               "Drag actor markers on the map";
    }
    SDL_Color c = (pickMode_ != CsPickMode::None) ? SDL_Color{160, 0, 0, 255}
                : (statusMsgT_ > 0 ? SDL_Color{0, 100, 0, 255} : ThDim);
    drawText(hint, 8, y + 2, c, 11);
}

void CutsceneEditor::renderMenus() {
    if (showEventMenu_) {
        // Two-column menu listing every event type, anchored above "+ Event"
        const int itemH = 18, cols = 2;
        int n    = (int)CsEventType::COUNT;
        int rows = (n + cols - 1) / cols;
        menuW_ = 220;
        menuH_ = rows * itemH + 4;
        menuX_ = tlX_ + tlW_ - menuW_ - 4;
        menuY_ = colY_ + TL_TOOL_H;
        if (menuY_ + menuH_ > screenH_ - HINT_H) menuY_ = screenH_ - HINT_H - menuH_;

        fillRect(menuX_, menuY_, menuW_, menuH_, ThFace);
        drawBevel(menuX_, menuY_, menuW_, menuH_, true);
        for (int i = 0; i < n; i++) {
            int col = i / rows, row = i % rows;
            int ix = menuX_ + 2 + col * (menuW_ / cols);
            int iy = menuY_ + 2 + row * itemH;
            bool hov = ui_->pointInRect(ui_->mouseX, ui_->mouseY, ix, iy, menuW_ / cols - 4, itemH);
            if (hov) fillRect(ix, iy, menuW_ / cols - 4, itemH, ThHover);
            fillRect(ix + 2, iy + 3, 6, 12, eventColor((CsEventType)i));
            drawText(csEventTypeName((CsEventType)i), ix + 12, iy + 3,
                     hov ? ThText : ThText, 11);
        }
    }

    if (showActorMenu_) {
        static const char* opts[] = {
            "Enemy: Melee", "Enemy: Shooter", "Enemy: Brute",
            "Enemy: Scout", "Enemy: Sniper", "Enemy: Gunner", "Free Sprite"};
        const int n = 7, itemH = 18;
        menuW_ = 130;
        menuH_ = n * itemH + 4;
        menuX_ = actorX_ + 3;
        menuY_ = colY_ + colH_ - 22 - menuH_;

        fillRect(menuX_, menuY_, menuW_, menuH_, ThFace);
        drawBevel(menuX_, menuY_, menuW_, menuH_, true);
        for (int i = 0; i < n; i++) {
            int iy = menuY_ + 2 + i * itemH;
            bool hov = ui_->pointInRect(ui_->mouseX, ui_->mouseY, menuX_ + 2, iy, menuW_ - 4, itemH);
            if (hov) fillRect(menuX_ + 2, iy, menuW_ - 4, itemH, ThHover);
            drawText(opts[i], menuX_ + 8, iy + 3,
                     hov ? ThText : ThText, 11);
        }
    }
}

// ---- Input ----

bool CutsceneEditor::handleEvent(SDL_Event& e, float mouseWorldX, float mouseWorldY, float zoom) {
    if (!active_) return false;

    // Text editing has top priority for keyboard events
    if (focusedField_ >= 0) {
        if (e.type == SDL_TEXTINPUT) {
            for (const char* t = e.text.text; *t; t++) {
                char c = *t;
                if (numericField_) {
                    if ((c >= '0' && c <= '9') || c == '-' || c == '.' || c == '+')
                        editBuf_ += c;
                } else if ((unsigned char)c >= 32 && (unsigned char)c < 127) {
                    editBuf_ += c;
                }
            }
            return true;
        }
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_BACKSPACE:
                    if (!editBuf_.empty()) editBuf_.pop_back();
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    releaseFocus(true);
                    break;
                case SDLK_ESCAPE:
                    releaseFocus(false);
                    break;
                default: break;
            }
            return true;  // swallow all keys while editing
        }
    }

    // The dialog modal owns all input while open. Its buttons/fields are
    // immediate-mode (handled in renderDialogModal via ui_->mouseClicked), so
    // here we only consume events and handle ESC; clicks are left intact.
    if (dialogModal_) return handleDialogModalEvent(e);

    switch (e.type) {
        case SDL_MOUSEBUTTONDOWN: {
            int mx = e.button.x, my = e.button.y;
            bool right = (e.button.button == SDL_BUTTON_RIGHT);
            bool left  = (e.button.button == SDL_BUTTON_LEFT);

            // A click anywhere commits the field being edited. If it lands on
            // another field, that field re-focuses during this frame's render.
            if (focusedField_ >= 0) releaseFocus(true);

            // Open menus capture every click first
            if (showEventMenu_ || showActorMenu_) {
                swallowUiClick_ = true;
                bool inMenu = (mx >= menuX_ && mx < menuX_ + menuW_ &&
                               my >= menuY_ && my < menuY_ + menuH_);
                if (inMenu && left) {
                    if (showEventMenu_) {
                        const int itemH = 18, cols = 2;
                        int n    = (int)CsEventType::COUNT;
                        int rows = (n + cols - 1) / cols;
                        int col  = (mx - menuX_ - 2) / (menuW_ / cols);
                        int row  = (my - menuY_ - 2) / itemH;
                        int idx  = clampi(col, 0, cols - 1) * rows + row;
                        if (row >= 0 && row < rows && idx >= 0 && idx < n)
                            addEvent((CsEventType)idx);
                    } else {
                        const int itemH = 18;
                        int idx = (my - menuY_ - 2) / itemH;
                        switch (idx) {
                            case 0: addActor(CsActorType::Enemy, CsEnemyType::Melee); break;
                            case 1: addActor(CsActorType::Enemy, CsEnemyType::Shooter); break;
                            case 2: addActor(CsActorType::Enemy, CsEnemyType::Brute); break;
                            case 3: addActor(CsActorType::Enemy, CsEnemyType::Scout); break;
                            case 4: addActor(CsActorType::Enemy, CsEnemyType::Sniper); break;
                            case 5: addActor(CsActorType::Enemy, CsEnemyType::Gunner); break;
                            case 6: addActor(CsActorType::FreeSprite); break;
                            default: break;
                        }
                    }
                }
                showEventMenu_ = false;
                showActorMenu_ = false;
                return true;
            }

            // Panel resize band along the top edge
            if (left && my >= panelY_ - 3 && my <= panelY_ + 6) {
                resizingPanel_ = true;
                resizeGrabDY_  = my - panelY_;
                swallowUiClick_ = true;
                return true;
            }

            if (my >= panelY_) {
                // Click inside the panel: timeline interactions are handled
                // here; everything else is immediate-mode widgets in render().
                if (handleTimelineClick(mx, my, right)) swallowUiClick_ = true;
                return true;
            }

            // Click on the map canvas above the panel
            if (left && handleCanvasClick(mouseWorldX, mouseWorldY, zoom)) {
                swallowUiClick_ = true;
                return true;
            }
            // Clicking the canvas commits any text edit but is not consumed
            if (focusedField_ >= 0) releaseFocus(true);
            return false;
        }

        case SDL_MOUSEBUTTONUP: {
            bool wasDragging = draggingEvent_ || resizingEvent_ || draggingScrub_ ||
                               resizingPanel_ || dragActorIdx_ >= 0 ||
                               rotTarget_ != RotTarget::None || scaleTarget_ != ScaleTarget::None;
            handleRelease();
            return wasDragging;  // consume if we owned the drag
        }

        case SDL_MOUSEMOTION: {
            int mx = e.motion.x, my = e.motion.y;
            if (resizingPanel_) {
                int newY = my - resizeGrabDY_;
                int maxH = (screenH_ * 2) / 3;
                panelH_  = clampi(screenH_ - newY, CS_PANEL_MIN_H, maxH);
                return true;
            }
            if (rotTarget_ != RotTarget::None) {
                Cutscene* cs = current();
                float cx, cy, a0, a1; bool two;
                if (cs && rotationHandle(cx, cy, a0, a1, two)) {
                    float deg = atan2f(mouseWorldY - cy, mouseWorldX - cx) * 180.0f / 3.14159265f;
                    if (!(SDL_GetModState() & KMOD_SHIFT))
                        deg = roundf(deg / 15.0f) * 15.0f;  // 15-deg snap unless Shift
                    if (rotTarget_ == RotTarget::ActorStart &&
                        selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                        cs->actors[selectedActor_].startRot = deg;
                    else if (selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
                        if (rotTarget_ == RotTarget::EventTo) cs->events[selectedEvent_].toRot = deg;
                        else                                  cs->events[selectedEvent_].fromRot = deg;
                    }
                    recomputePreview();
                }
                return true;
            }
            if (scaleTarget_ != ScaleTarget::None) {
                Cutscene* cs = current();
                float cx, cy, fsx, fsy, tsx, tsy;
                if (cs && scaleHandle(cx, cy, fsx, fsy, tsx, tsy) &&
                    selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
                    float sxv = clampf(fabsf(mouseWorldX - cx) / CS_SCALE_REF, 0.05f, 20.0f);
                    float syv = clampf(fabsf(mouseWorldY - cy) / CS_SCALE_REF, 0.05f, 20.0f);
                    if (!(SDL_GetModState() & KMOD_SHIFT)) {  // uniform unless Shift
                        float u = std::max(sxv, syv); sxv = syv = u;
                    }
                    CsEvent& ev = cs->events[selectedEvent_];
                    if (scaleTarget_ == ScaleTarget::To) { ev.toScaleX = sxv; ev.toScaleY = syv; }
                    else                                 { ev.fromScaleX = sxv; ev.fromScaleY = syv; }
                    recomputePreview();
                }
                return true;
            }
            if (dragActorIdx_ >= 0) {
                Cutscene* cs = current();
                if (cs && dragActorIdx_ < (int)cs->actors.size()) {
                    cs->actors[dragActorIdx_].startX =
                        dragActorOrigX_ + (mouseWorldX - dragWorldStartX_);
                    cs->actors[dragActorIdx_].startY =
                        dragActorOrigY_ + (mouseWorldY - dragWorldStartY_);
                    recomputePreview();
                }
                return true;
            }
            handleMotion(mx, my);
            return draggingEvent_ || resizingEvent_ || draggingScrub_;
        }

        case SDL_MOUSEWHEEL: {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            if (ui_) { mx = ui_->mouseX; my = ui_->mouseY; }
            if (my < panelY_) return false;
            int dy = e.wheel.y;
            if (mx >= tlX_ && mx < tlX_ + tlW_) {
                if (SDL_GetModState() & KMOD_CTRL) {
                    float mt = pxToTime(mx);
                    timelineScale_ = clampf(timelineScale_ * (dy > 0 ? 1.2f : 1.0f / 1.2f), 10, 400);
                    timelineStart_ = std::max(0.0f, mt - (float)(mx - tlX_) / timelineScale_);
                } else if (SDL_GetModState() & KMOD_SHIFT) {
                    tlRowScroll_ -= dy * TL_ROW_H;
                } else {
                    timelineStart_ = std::max(0.0f, timelineStart_ - dy * 30.0f / timelineScale_);
                }
            } else if (mx < listX_ + listW_) {
                listScroll_ -= dy * ROW_H;
            } else if (mx < actorX_ + actorW_) {
                actorScroll_ -= dy * ROW_H;
            } else if (mx >= propsX_) {
                propsScroll_ -= dy * 24;
            }
            return true;
        }

        case SDL_KEYDOWN:
            return handleKey(e);

        default:
            return false;
    }
}

bool CutsceneEditor::handleTimelineClick(int mx, int my, bool rightClick) {
    if (mx < tlX_ || mx >= tlX_ + tlW_) return false;
    int rulerY = colY_ + TL_TOOL_H;
    int rowsY  = rulerY + TL_RULER_H;
    int rowsH  = colH_ - TL_TOOL_H - TL_RULER_H - 1;
    if (my < rulerY || my >= rowsY + rowsH) return false;

    if (focusedField_ >= 0) releaseFocus(true);

    Cutscene* cs = current();

    // Ruler: start scrubbing
    if (my < rowsY) {
        scrubTime_ = snapTime(pxToTime(mx));
        draggingScrub_ = true;
        playing_ = false;
        recomputePreview();
        return true;
    }

    if (!cs) return true;

    const int TL_ACTOR_H = TL_ROW_H * 2;
    int nActors = (int)cs->actors.size();

    // Convert y to row index (global=0, actors=1+i).
    auto yToRow = [&](int py) -> int {
        int rel = py - rowsY + tlRowScroll_;
        if (rel < TL_ROW_H) return 0;
        int ai = (rel - TL_ROW_H) / TL_ACTOR_H;
        return 1 + std::min(ai, nActors - 1 + (nActors == 0 ? 1 : 0));
    };

    auto rowTopY = [&](int row) -> int {
        if (row == 0) return rowsY - tlRowScroll_;
        return rowsY + TL_ROW_H + (row - 1) * TL_ACTOR_H - tlRowScroll_;
    };

    auto rowOf = [&](uint32_t actorId) -> int {
        for (int i = 0; i < nActors; i++)
            if (cs->actors[i].id == actorId) return 1 + i;
        return 0;
    };

    // Greedy lane assignment (same algorithm as renderTimeline).
    std::vector<int> lane(cs->events.size(), 0);
    for (int ai = -1; ai < nActors; ai++) {
        uint32_t actorId = (ai < 0) ? 0 : cs->actors[ai].id;
        float laneEnd[2] = {-1e9f, -1e9f};
        for (int ei = 0; ei < (int)cs->events.size(); ei++) {
            if (cs->events[ei].actorId != actorId) continue;
            const auto& ev2 = cs->events[ei];
            float evEnd = ev2.startTime + std::max(ev2.duration, 0.06f);
            if (ev2.startTime >= laneEnd[0] - 0.001f) {
                lane[ei] = 0; laneEnd[0] = evEnd;
            } else {
                lane[ei] = 1; laneEnd[1] = evEnd;
            }
        }
    }

    // Hit-test event blocks (reverse order = topmost drawn last wins).
    for (int ei = (int)cs->events.size() - 1; ei >= 0; ei--) {
        const auto& ev = cs->events[ei];
        int row     = rowOf(ev.actorId);
        int laneOff = (row > 0) ? lane[ei] * TL_ROW_H : 0;
        int ry  = rowTopY(row) + laneOff + 2;
        int rh  = TL_ROW_H - 5;
        int px0 = timeToPx(ev.startTime);
        int px1 = timeToPx(ev.startTime + std::max(ev.duration, 0.06f));
        if (px1 - px0 < 6) px1 = px0 + 6;
        if (mx >= px0 && mx <= px1 && my >= ry && my <= ry + rh) {
            if (rightClick) { deleteEvent(ei); return true; }
            selectedEvent_ = ei;
            if (row > 0) selectedActor_ = row - 1;
            if (mx >= px1 - 5 && ev.duration > 0.0f) {
                resizingEvent_ = true;
                resizeOrigDur_ = ev.duration;
                dragStartPx_   = mx;
            } else {
                draggingEvent_  = true;
                dragEventOrigT_ = ev.startTime;
                dragStartPx_    = mx;
            }
            return true;
        }
    }

    // Empty area: deselect event, update actor selection from the clicked row, scrub.
    selectedEvent_ = -1;
    { int r = yToRow(my); if (r > 0) selectedActor_ = r - 1; }
    scrubTime_ = snapTime(pxToTime(mx));
    draggingScrub_ = true;
    playing_ = false;
    recomputePreview();
    return true;
}

void CutsceneEditor::handleMotion(int mx, int my) {
    Cutscene* cs = current();

    if (draggingScrub_) {
        scrubTime_ = snapTime(pxToTime(mx));
        recomputePreview();
        return;
    }
    if (draggingEvent_ && cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
        CsEvent& ev = cs->events[selectedEvent_];
        float dt = (float)(mx - dragStartPx_) / timelineScale_;
        ev.startTime = snapTime(dragEventOrigT_ + dt);
        // Vertical drag = reassign actor (two-lane rows, global=TL_ROW_H, actors=TL_ROW_H*2).
        const int TL_ACTOR_H = TL_ROW_H * 2;
        int rowsY = colY_ + TL_TOOL_H + TL_RULER_H;
        int nActors = (int)cs->actors.size();
        int rel = (my - rowsY + tlRowScroll_);
        int row = 0;
        if (rel >= TL_ROW_H && nActors > 0) {
            int ai = (rel - TL_ROW_H) / TL_ACTOR_H;
            row = 1 + clampi(ai, 0, nActors - 1);
        }
        ev.actorId = (row == 0) ? 0u : cs->actors[row - 1].id;
        if (row > 0) selectedActor_ = row - 1;
        recomputePreview();
        return;
    }
    if (resizingEvent_ && cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
        float dt = (float)(mx - dragStartPx_) / timelineScale_;
        float d  = snapTime(resizeOrigDur_ + dt);
        cs->events[selectedEvent_].duration = std::max(0.05f, d);
        recomputePreview();
        return;
    }
}

void CutsceneEditor::handleRelease() {
    draggingEvent_ = false;
    resizingEvent_ = false;
    draggingScrub_ = false;
    resizingPanel_ = false;
    dragActorIdx_  = -1;
    rotTarget_     = RotTarget::None;
    scaleTarget_   = ScaleTarget::None;
}

bool CutsceneEditor::handleKey(SDL_Event& e) {
    SDL_Keycode sym = e.key.keysym.sym;
    bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;

    // Esc always cancels an armed pick, wherever the mouse is
    if (sym == SDLK_ESCAPE && pickMode_ != CsPickMode::None) {
        pickMode_ = CsPickMode::None;
        return true;
    }

    // Other shortcuts only apply while the mouse is over the panel
    int my = ui_ ? ui_->mouseY : 0;
    if (my < panelY_) return false;

    Cutscene* cs = current();
    switch (sym) {
        case SDLK_SPACE:
            playing_ = !playing_;
            if (playing_ && cs && scrubTime_ >= cs->totalDuration() - 0.01f) scrubTime_ = 0;
            return true;
        case SDLK_HOME:
            scrubTime_ = 0;
            timelineStart_ = 0;
            recomputePreview();
            return true;
        case SDLK_DELETE:
            if (selectedEvent_ >= 0) { deleteEvent(selectedEvent_); return true; }
            return false;
        case SDLK_LEFT:
        case SDLK_RIGHT: {
            if (!cs || selectedEvent_ < 0 || selectedEvent_ >= (int)cs->events.size()) return false;
            float d = (sym == SDLK_LEFT ? -0.1f : 0.1f);
            if (e.key.keysym.mod & KMOD_SHIFT) d *= 10.0f;
            auto& ev = cs->events[selectedEvent_];
            ev.startTime = std::max(0.0f, ev.startTime + d);
            recomputePreview();
            return true;
        }
        case SDLK_c:
            if (ctrl && cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
                clipboard_ = cs->events[selectedEvent_];
                hasClipboard_ = true;
                setStatus("Event copied");
                return true;
            }
            return false;
        case SDLK_v:
            if (ctrl && cs && hasClipboard_) {
                CsEvent ev = clipboard_;
                ev.startTime = snapTime(scrubTime_);
                // Paste onto the selected actor when the source actor is gone
                bool actorExists = (ev.actorId == 0);
                for (auto& a : cs->actors) if (a.id == ev.actorId) { actorExists = true; break; }
                if (!actorExists && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                    ev.actorId = cs->actors[selectedActor_].id;
                cs->events.push_back(ev);
                selectedEvent_ = (int)cs->events.size() - 1;
                recomputePreview();
                setStatus("Event pasted at the scrub time");
                return true;
            }
            return false;
        case SDLK_ESCAPE:
            selectedEvent_ = -1;
            selectedActor_ = -1;
            return true;
        default:
            return false;
    }
}

bool CutsceneEditor::handleCanvasClick(float wx, float wy, float zoom) {
    Cutscene* cs = current();
    if (!cs) return false;

    // Armed position pick
    if (pickMode_ != CsPickMode::None) {
        switch (pickMode_) {
            case CsPickMode::ActorStart:
                if (selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size()) {
                    cs->actors[selectedActor_].startX = wx;
                    cs->actors[selectedActor_].startY = wy;
                }
                break;
            case CsPickMode::EventFrom:
                if (selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
                    cs->events[selectedEvent_].fromX = wx;
                    cs->events[selectedEvent_].fromY = wy;
                }
                break;
            case CsPickMode::EventTo:
                if (selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
                    cs->events[selectedEvent_].toX = wx;
                    cs->events[selectedEvent_].toY = wy;
                }
                break;
            case CsPickMode::EventExplosion:
                if (selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
                    cs->events[selectedEvent_].explX = wx;
                    cs->events[selectedEvent_].explY = wy;
                }
                break;
            case CsPickMode::EventSpawnPos:
                if (selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
                    cs->events[selectedEvent_].spawnX = wx;
                    cs->events[selectedEvent_].spawnY = wy;
                    cs->events[selectedEvent_].spawnOverridePos = true;
                }
                break;
            default: break;
        }
        pickMode_ = CsPickMode::None;
        setStatus("Position set");
        recomputePreview();
        return true;
    }

    // Grab a rotation knob first (it sits outside the actor sprite)
    {
        float cx, cy, a0, a1; bool two;
        if (rotationHandle(cx, cy, a0, a1, two)) {
            float ktol = 14.0f / std::max(zoom, 0.05f);
            const float D2R = 3.14159265f / 180.0f;
            auto hit = [&](float deg) {
                float kx = cx + CS_ROT_R * cosf(deg * D2R);
                float ky = cy + CS_ROT_R * sinf(deg * D2R);
                return fabsf(wx - kx) < ktol && fabsf(wy - ky) < ktol;
            };
            if (two) {
                if (hit(a1)) { rotTarget_ = RotTarget::EventTo;   return true; }
                if (hit(a0)) { rotTarget_ = RotTarget::EventFrom; return true; }
            } else if (hit(a0)) {
                rotTarget_ = RotTarget::ActorStart; return true;
            }
        }
    }

    // Grab a scale-gizmo corner (bottom-right corner of each box)
    {
        float cx, cy, fsx, fsy, tsx, tsy;
        if (scaleHandle(cx, cy, fsx, fsy, tsx, tsy)) {
            float ktol = 14.0f / std::max(zoom, 0.05f);
            auto hitCorner = [&](float sxv, float syv) {
                float hx = cx + CS_SCALE_REF * sxv;
                float hy = cy + CS_SCALE_REF * syv;
                return fabsf(wx - hx) < ktol && fabsf(wy - hy) < ktol;
            };
            if (hitCorner(tsx, tsy)) { scaleTarget_ = ScaleTarget::To;   return true; }
            if (hitCorner(fsx, fsy)) { scaleTarget_ = ScaleTarget::From; return true; }
        }
    }

    // Start dragging an actor marker
    float tol = 18.0f / std::max(zoom, 0.05f);
    for (int i = (int)cs->actors.size() - 1; i >= 0; i--) {
        const CsActorState* s = actorStateAt(i);
        float ax = s ? s->x : cs->actors[i].startX;
        float ay = s ? s->y : cs->actors[i].startY;
        if (fabsf(wx - ax) < tol && fabsf(wy - ay) < tol) {
            selectedActor_   = i;
            selectedEvent_   = -1;
            dragActorIdx_    = i;
            dragActorOrigX_  = cs->actors[i].startX;
            dragActorOrigY_  = cs->actors[i].startY;
            dragWorldStartX_ = wx;
            dragWorldStartY_ = wy;
            return true;
        }
    }
    return false;
}
