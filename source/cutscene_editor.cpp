#include "cutscene_editor.h"
#include "assets.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ============================================================
//  Init / Shutdown
// ============================================================

void CutsceneEditor::init(SDL_Renderer* r, int screenW, int screenH, UI::Context* ui) {
    r_       = r;
    ui_      = ui;
    screenW_ = screenW;
    (void)screenH;
}

void CutsceneEditor::shutdown() {}

// ============================================================
//  Layout
// ============================================================

void CutsceneEditor::computeLayout(int screenW, int panelY) {
    panelY_    = panelY;
    panelH_    = CS_EDITOR_PANEL_H;
    listX_     = 0;
    listW_     = CS_LIST_W;
    actorX_    = listX_ + listW_ + 1;
    actorW_    = CS_ACTOR_W;
    propsX_    = screenW - CS_PROPS_W;
    propsW_    = CS_PROPS_W;
    timelineX_ = actorX_ + actorW_ + 1;
    timelineW_ = propsX_ - timelineX_ - 1;
}

// ============================================================
//  Low-level drawing helpers
// ============================================================

void CutsceneEditor::fill(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r_, &rc);
}

void CutsceneEditor::fillBlend(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r_, &rc);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
}

void CutsceneEditor::outline(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderDrawRect(r_, &rc);
}

void CutsceneEditor::hline(int x0, int x1, int y, SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r_, x0, y, x1, y);
}

void CutsceneEditor::vline(int x, int y0, int y1, SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r_, x, y0, x, y1);
}

void CutsceneEditor::txt(const char* s, int x, int y, SDL_Color c, int sz) {
    if (ui_) ui_->drawText(s, x, y, sz, c);
}

void CutsceneEditor::txtR(const char* s, int rx, int y, SDL_Color c, int sz) {
    if (ui_) ui_->drawTextRight(s, rx, y, sz, c);
}

// ============================================================
//  Win98-style higher-level helpers
// ============================================================

// Silver inset panel with bevel
void CutsceneEditor::panelBg(int x, int y, int w, int h) {
    fill(x, y, w, h, UI::W98::Silver);
    if (ui_) ui_->drawWin98Bevel(x, y, w, h, false);
}

// Navy section header with white title
void CutsceneEditor::sectionHeader(int x, int y, int w, const char* title) {
    fill(x, y, w, 16, UI::W98::Navy);
    txt(title, x + 4, y + 2, UI::W98::White, 11);
}

// Editable text field row: label + sunken text field
// Returns true if the field was clicked (caller should call startEdit)
bool CutsceneEditor::fieldRow(const char* label, const char* value, CsEditField fid,
                               int x, int& cy, int w, int labelW) {
    bool isActive = (activeField_ == fid);
    txt(label, x + 2, cy + 2, {100, 100, 100, 255}, 11);

    int fx = x + labelW, fw = w - labelW - 4;
    const char* display = isActive ? editBuf_ : value;

    // Show cursor in active field
    if (isActive) {
        char withCursor[520];
        bool showCursor = (int)(editBlinkT_ * 2.0f) % 2 == 0;
        snprintf(withCursor, sizeof(withCursor), "%s%s", display, showCursor ? "|" : "");
        if (ui_) ui_->drawWin98TextField(fx, cy, fw, 15, withCursor, true, false, editBlinkT_);
    } else {
        if (ui_) ui_->drawWin98TextField(fx, cy, fw, 15, display, false);
    }

    // Hit test
    bool clicked = false;
    if (ui_ && ui_->mouseClicked) {
        int mx = ui_->mouseX, my = ui_->mouseY;
        if (mx >= fx && mx <= fx+fw && my >= cy && my <= cy+15) {
            clicked = true;
            ui_->mouseClicked = false;
        }
    }
    cy += 18;
    return clicked;
}

// Static label + value row
void CutsceneEditor::labelRow(const char* label, const char* value,
                               int x, int& cy, int w, int labelW, SDL_Color vc) {
    txt(label, x + 2, cy + 1, {100, 100, 100, 255}, 11);
    txt(value, x + labelW, cy + 1, vc, 11);
    (void)w;
    cy += 15;
}

// Thin horizontal separator
void CutsceneEditor::sepLine(int x, int& cy, int w) {
    hline(x, x + w - 4, cy + 2, {128, 128, 128, 255});
    cy += 6;
}

// Small Win98 button; returns true if clicked this frame
bool CutsceneEditor::btn(int idx, const char* label, int x, int y, int bw, int bh) {
    if (ui_) return ui_->win98Button(idx, label, x, y, bw, bh, false);
    return false;
}

// Bool toggle row: shows "[Y]" or "[N]" and returns true if toggled
bool CutsceneEditor::boolRow(const char* label, bool val, int x, int& cy, int w, int labelW) {
    txt(label, x + 2, cy + 1, {100, 100, 100, 255}, 11);
    const char* s = val ? "Yes" : "No";
    SDL_Color vc = val ? SDL_Color{80, 220, 120, 255} : SDL_Color{180, 80, 80, 255};
    txt(s, x + labelW, cy + 1, vc, 11);

    // Small toggle button
    bool clicked = btn(48 + (int)val, val ? "Disable" : "Enable",
                       x + labelW + 30, cy - 1, 48, 14);
    (void)w;
    cy += 15;
    return clicked;
}

// ============================================================
//  Timeline math
// ============================================================

static constexpr int TL_TOOLBAR_H = 24;
static constexpr int TL_RULER_H   = 16;
static constexpr int TL_ROW_H     = 22;

float CutsceneEditor::pxToTime(int px, int timelineX) const {
    return timelineStart_ + (float)(px - timelineX) / timelineScale_;
}

int CutsceneEditor::timeToPx(float t, int timelineX) const {
    return timelineX + (int)((t - timelineStart_) * timelineScale_);
}

int CutsceneEditor::actorRowY(int ai, int contentY) const {
    return contentY + TL_ROW_H + ai * TL_ROW_H; // row 0 = global
}

SDL_Color CutsceneEditor::eventColor(CsEventType t) const {
    switch (t) {
        case CsEventType::Move:          return {90,  170, 255, 255};
        case CsEventType::Rotate:        return {150, 100, 255, 255};
        case CsEventType::Scale:         return {100, 220, 150, 255};
        case CsEventType::Alpha:         return {200, 200, 80,  255};
        case CsEventType::Flash:         return {255, 150, 80,  255};
        case CsEventType::Wait:          return {130, 130, 130, 255};
        case CsEventType::Dialog:        return {255, 210, 80,  255};
        case CsEventType::PlaySFX:       return {80,  220, 220, 255};
        case CsEventType::SpawnExplosion:return {255, 80,  80,  255};
        case CsEventType::CameraMove:    return {255, 160, 200, 255};
        case CsEventType::CameraZoom:    return {255, 130, 160, 255};
        case CsEventType::CameraShake:   return {255, 80,  160, 255};
        case CsEventType::ScreenFade:    return {40,  40,  40,  255};
        case CsEventType::CinematicBars: return {80,  80,  80,  255};
        case CsEventType::SetVisible:    return {180, 255, 180, 255};
        case CsEventType::SetFrame:      return {180, 200, 255, 255};
        case CsEventType::SpawnActor:    return {120, 255, 120, 255};
        case CsEventType::DespawnActor:  return {255, 120, 120, 255};
        case CsEventType::SetFlag:       return {255, 200, 100, 255};
        case CsEventType::ChainCutscene: return {200, 100, 255, 255};
        case CsEventType::EndCutscene:   return {255, 60,  60,  255};
        default:                          return {150, 150, 150, 255};
    }
}

// ============================================================
//  Helpers
// ============================================================

Cutscene* CutsceneEditor::current() {
    if (!lib_) return nullptr;
    if (selectedCutscene_ < 0 || selectedCutscene_ >= (int)lib_->cutscenes.size())
        return nullptr;
    return &lib_->cutscenes[selectedCutscene_];
}

const Cutscene* CutsceneEditor::currentCutscene() const {
    if (!lib_) return nullptr;
    if (selectedCutscene_ < 0 || selectedCutscene_ >= (int)lib_->cutscenes.size())
        return nullptr;
    return &lib_->cutscenes[selectedCutscene_];
}

const CsActorState* CutsceneEditor::actorStateAt(int idx) const {
    if (idx < 0 || idx >= (int)previewStates_.size()) return nullptr;
    return &previewStates_[idx];
}

int CutsceneEditor::actorCount() const {
    const Cutscene* cs = currentCutscene();
    return cs ? (int)cs->actors.size() : 0;
}

// ============================================================
//  Preview computation
// ============================================================

void CutsceneEditor::recomputePreview() {
    const Cutscene* cs = currentCutscene();
    if (!cs) { previewStates_.clear(); return; }

    previewStates_.resize(cs->actors.size());
    for (int i = 0; i < (int)cs->actors.size(); i++) {
        const auto& a = cs->actors[i];
        auto& s = previewStates_[i];
        s.x = a.startX; s.y = a.startY;
        s.rot = a.startRot;
        s.scaleX = a.startScaleX; s.scaleY = a.startScaleY;
        s.alpha = a.startAlpha;
        s.visible = a.startVisible;
        s.frame = 0; s.flashAmt = 0;
    }

    auto lerp = [](float a, float b, float t){ return a + (b-a)*t; };
    auto ease = [](float t, CsEase e) -> float {
        t = std::max(0.0f, std::min(1.0f, t));
        switch (e) {
            case CsEase::EaseIn:    return t*t;
            case CsEase::EaseOut:   return 1-(1-t)*(1-t);
            case CsEase::EaseInOut: return t<0.5f ? 2*t*t : 1-2*(1-t)*(1-t);
            case CsEase::Instant:   return t>=1.f?1.f:0.f;
            default: return t;
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
        if (idx < 0 && ev.actorId != 0) continue;
        CsActorState* s = (idx >= 0) ? &previewStates_[idx] : nullptr;

        switch (ev.type) {
            case CsEventType::Move:
                if (s) { s->x = lerp(ev.fromX, ev.toX, t); s->y = lerp(ev.fromY, ev.toY, t); } break;
            case CsEventType::Rotate:
                if (s) s->rot = lerp(ev.fromRot, ev.toRot, t); break;
            case CsEventType::Scale:
                if (s) { s->scaleX = lerp(ev.fromScaleX, ev.toScaleX, t);
                         s->scaleY = lerp(ev.fromScaleY, ev.toScaleY, t); } break;
            case CsEventType::Alpha:
                if (s) s->alpha = lerp(ev.fromAlpha, ev.toAlpha, t); break;
            case CsEventType::SetVisible:
                if (s && localT >= 1.f) s->visible = ev.visible; break;
            case CsEventType::SetFrame:
                if (s && localT >= 1.f) s->frame = ev.frame; break;
            case CsEventType::SpawnActor:
                if (s && localT >= 1.f) {
                    s->visible = true;
                    if (ev.spawnOverridePos) { s->x = ev.spawnX; s->y = ev.spawnY; }
                } break;
            case CsEventType::DespawnActor:
                if (s && localT >= 1.f) s->visible = false; break;
            default: break;
        }
    }
}

// ============================================================
//  Add/Delete operations
// ============================================================

void CutsceneEditor::addCutscene() {
    if (!lib_) return;
    Cutscene cs;
    cs.id = "cutscene_" + std::to_string(nextCsId_++);
    cs.blockInput = true;
    lib_->cutscenes.push_back(std::move(cs));
    selectedCutscene_ = (int)lib_->cutscenes.size() - 1;
    selectedActor_ = selectedEvent_ = selectedDialogSeq_ = selectedDialogLine_ = -1;
    propsMode_ = CsPropsMode::Scene;
}

void CutsceneEditor::deleteCutscene(int idx) {
    if (!lib_ || idx < 0 || idx >= (int)lib_->cutscenes.size()) return;
    lib_->cutscenes.erase(lib_->cutscenes.begin() + idx);
    selectedCutscene_ = std::min(selectedCutscene_, (int)lib_->cutscenes.size()-1);
    selectedActor_ = selectedEvent_ = selectedDialogSeq_ = selectedDialogLine_ = -1;
    propsMode_ = CsPropsMode::Scene;
}

void CutsceneEditor::addActor(CsActorType type, CsEnemyType enemyType) {
    Cutscene* cs = current();
    if (!cs) return;
    CsActor a;
    a.id = nextActorId_++;
    a.type = type;
    a.enemyType = enemyType;
    switch (type) {
        case CsActorType::Player:     a.name = "Player";  break;
        case CsActorType::Enemy:      a.name = "Enemy";   break;
        case CsActorType::FreeSprite: a.name = "Sprite";  break;
    }
    a.startX = 320; a.startY = 240;
    cs->actors.push_back(a);
    selectedActor_ = (int)cs->actors.size() - 1;
    selectedEvent_ = -1;
    propsMode_ = CsPropsMode::Actor;
    recomputePreview();
}

void CutsceneEditor::deleteActor(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->actors.size()) return;
    uint32_t rmId = cs->actors[idx].id;
    cs->actors.erase(cs->actors.begin() + idx);
    cs->events.erase(std::remove_if(cs->events.begin(), cs->events.end(),
        [rmId](const CsEvent& e){ return e.actorId == rmId; }), cs->events.end());
    selectedActor_ = std::min(selectedActor_, (int)cs->actors.size()-1);
    selectedEvent_ = -1;
    propsMode_ = selectedActor_ >= 0 ? CsPropsMode::Actor : CsPropsMode::Scene;
    recomputePreview();
}

void CutsceneEditor::addEvent(CsEventType type, uint32_t actorId, float atTime) {
    Cutscene* cs = current();
    if (!cs) return;
    CsEvent ev;
    ev.actorId   = actorId;
    ev.type      = type;
    ev.startTime = atTime;
    ev.ease      = CsEase::EaseInOut;

    // Zero-duration event types
    bool zerodur = (type == CsEventType::Wait
                 || type == CsEventType::PlaySFX
                 || type == CsEventType::SpawnExplosion
                 || type == CsEventType::SpawnActor
                 || type == CsEventType::DespawnActor
                 || type == CsEventType::SetFlag
                 || type == CsEventType::ChainCutscene
                 || type == CsEventType::EndCutscene);
    ev.duration = zerodur ? 0.0f : 1.0f;

    switch (type) {
        case CsEventType::Alpha:         ev.fromAlpha = 0; ev.toAlpha = 1; break;
        case CsEventType::CameraZoom:    ev.fromZoom = 1; ev.toZoom = 1.5f; break;
        case CsEventType::CameraShake:   ev.shakeStrength = 8; ev.duration = 0.5f; break;
        case CsEventType::ScreenFade:    ev.fadeToBlack = true; break;
        case CsEventType::CinematicBars: ev.showBars = true; break;
        case CsEventType::SetVisible:    ev.visible = true; break;
        case CsEventType::SetFlag:       ev.flagValue = true; break;
        case CsEventType::SpawnActor: {
            // Default spawn pos from actor start position
            const Cutscene* ccs = currentCutscene();
            if (ccs) {
                const CsActor* a = ccs->findActor(actorId);
                if (a) { ev.spawnX = a->startX; ev.spawnY = a->startY; }
            }
            break;
        }
        default: break;
    }
    cs->events.push_back(ev);
    selectedEvent_ = (int)cs->events.size() - 1;
    propsMode_ = CsPropsMode::Event;
}

void CutsceneEditor::deleteEvent(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->events.size()) return;
    cs->events.erase(cs->events.begin() + idx);
    selectedEvent_ = std::min(selectedEvent_, (int)cs->events.size()-1);
    propsMode_ = selectedEvent_ >= 0 ? CsPropsMode::Event
               : (selectedActor_ >= 0 ? CsPropsMode::Actor : CsPropsMode::Scene);
    recomputePreview();
}

void CutsceneEditor::addDialogSeq() {
    Cutscene* cs = current();
    if (!cs) return;
    CsDialogSeq seq;
    seq.id = "dialog_" + std::to_string(cs->dialogs.size() + 1);
    cs->dialogs.push_back(seq);
    selectedDialogSeq_  = (int)cs->dialogs.size()-1;
    selectedDialogLine_ = -1;
    propsMode_ = CsPropsMode::DialogSeq;
}

void CutsceneEditor::deleteDialogSeq(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->dialogs.size()) return;
    cs->dialogs.erase(cs->dialogs.begin() + idx);
    selectedDialogSeq_  = std::min(selectedDialogSeq_, (int)cs->dialogs.size()-1);
    selectedDialogLine_ = -1;
    propsMode_ = selectedDialogSeq_ >= 0 ? CsPropsMode::DialogSeq : CsPropsMode::Scene;
}

void CutsceneEditor::addDialogLine(int seqIdx) {
    Cutscene* cs = current();
    if (!cs || seqIdx < 0 || seqIdx >= (int)cs->dialogs.size()) return;
    CsDialogLine line;
    line.character   = "Character";
    line.text        = "Enter dialog text here.";
    line.portraitLeft = true;
    cs->dialogs[seqIdx].lines.push_back(line);
    selectedDialogLine_ = (int)cs->dialogs[seqIdx].lines.size()-1;
    propsMode_ = CsPropsMode::DialogLine;
}

void CutsceneEditor::deleteDialogLine(int seqIdx, int lineIdx) {
    Cutscene* cs = current();
    if (!cs || seqIdx < 0 || seqIdx >= (int)cs->dialogs.size()) return;
    auto& lines = cs->dialogs[seqIdx].lines;
    if (lineIdx < 0 || lineIdx >= (int)lines.size()) return;
    lines.erase(lines.begin() + lineIdx);
    selectedDialogLine_ = std::min(selectedDialogLine_, (int)lines.size()-1);
    propsMode_ = selectedDialogLine_ >= 0 ? CsPropsMode::DialogLine : CsPropsMode::DialogSeq;
}

void CutsceneEditor::addChoice(int seqIdx, int lineIdx) {
    Cutscene* cs = current();
    if (!cs || seqIdx < 0 || seqIdx >= (int)cs->dialogs.size()) return;
    auto& lines = cs->dialogs[seqIdx].lines;
    if (lineIdx < 0 || lineIdx >= (int)lines.size()) return;
    if ((int)lines[lineIdx].choices.size() >= 4) return;
    CsDialogChoice c;
    c.text = "Choice";
    lines[lineIdx].choices.push_back(c);
    selectedChoice_ = (int)lines[lineIdx].choices.size()-1;
}

void CutsceneEditor::deleteChoice(int seqIdx, int lineIdx, int choiceIdx) {
    Cutscene* cs = current();
    if (!cs) return;
    auto& lines = cs->dialogs[seqIdx].lines;
    if (lineIdx < 0 || lineIdx >= (int)lines.size()) return;
    auto& choices = lines[lineIdx].choices;
    if (choiceIdx < 0 || choiceIdx >= (int)choices.size()) return;
    choices.erase(choices.begin() + choiceIdx);
    selectedChoice_ = std::min(selectedChoice_, (int)choices.size()-1);
}

// ============================================================
//  Text field editing
// ============================================================

void CutsceneEditor::startEdit(CsEditField fid, const char* current) {
    if (editActive_) commitEdit();
    activeField_ = fid;
    strncpy(editBuf_, current ? current : "", sizeof(editBuf_)-1);
    editBuf_[sizeof(editBuf_)-1] = '\0';
    editBlinkT_ = 0;
    editActive_ = true;
    SDL_StartTextInput();
}

void CutsceneEditor::commitEdit() {
    if (!editActive_) return;
    applyEditToField(activeField_, editBuf_);
    activeField_ = CsEditField::None;
    editActive_  = false;
    SDL_StopTextInput();
    recomputePreview();
}

void CutsceneEditor::cancelEdit() {
    activeField_ = CsEditField::None;
    editActive_  = false;
    SDL_StopTextInput();
}

void CutsceneEditor::applyEditToField(CsEditField fid, const char* val) {
    if (!val) return;
    float fval = (float)atof(val);
    Cutscene* cs = current();

    auto setA = [&](auto& x){ x = std::string(val); };
    auto setF = [&](float& x){ x = fval; };

    switch (fid) {
        case CsEditField::CsId:         if (cs) setA(cs->id); break;
        case CsEditField::CsChainOnEnd: if (cs) setA(cs->chainOnEnd); break;
        case CsEditField::ActorName:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                setA(cs->actors[selectedActor_].name); break;
        case CsEditField::ActorSprite:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                setA(cs->actors[selectedActor_].spritePath); break;
        case CsEditField::ActorStartX:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                setF(cs->actors[selectedActor_].startX); break;
        case CsEditField::ActorStartY:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                setF(cs->actors[selectedActor_].startY); break;
        case CsEditField::ActorStartRot:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                setF(cs->actors[selectedActor_].startRot); break;
        case CsEditField::ActorStartAlpha:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                { cs->actors[selectedActor_].startAlpha = std::max(0.f,std::min(1.f,fval)); } break;
        case CsEditField::ActorStartScaleX:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                setF(cs->actors[selectedActor_].startScaleX); break;
        case CsEditField::ActorStartScaleY:
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                setF(cs->actors[selectedActor_].startScaleY); break;

        // Event fields
        case CsEditField::EvStartTime:
            if (cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size())
                { cs->events[selectedEvent_].startTime = std::max(0.f, fval); } break;
        case CsEditField::EvDuration:
            if (cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size())
                { cs->events[selectedEvent_].duration = std::max(0.001f, fval); } break;
        case CsEditField::EvFromX:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].fromX); break;
        case CsEditField::EvFromY:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].fromY); break;
        case CsEditField::EvToX:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].toX); break;
        case CsEditField::EvToY:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].toY); break;
        case CsEditField::EvFromRot:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].fromRot); break;
        case CsEditField::EvToRot:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].toRot); break;
        case CsEditField::EvFromSX:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].fromScaleX); break;
        case CsEditField::EvFromSY:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].fromScaleY); break;
        case CsEditField::EvToSX:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].toScaleX); break;
        case CsEditField::EvToSY:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].toScaleY); break;
        case CsEditField::EvFromAlpha:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].fromAlpha); break;
        case CsEditField::EvToAlpha:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].toAlpha); break;
        case CsEditField::EvFlashR:
            if (cs && selectedEvent_ >= 0) { cs->events[selectedEvent_].flashR = std::max(0.f,std::min(255.f,fval)); } break;
        case CsEditField::EvFlashG:
            if (cs && selectedEvent_ >= 0) { cs->events[selectedEvent_].flashG = std::max(0.f,std::min(255.f,fval)); } break;
        case CsEditField::EvFlashB:
            if (cs && selectedEvent_ >= 0) { cs->events[selectedEvent_].flashB = std::max(0.f,std::min(255.f,fval)); } break;
        case CsEditField::EvFromZoom:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].fromZoom); break;
        case CsEditField::EvToZoom:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].toZoom); break;
        case CsEditField::EvShake:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].shakeStrength); break;
        case CsEditField::EvExplX:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].explX); break;
        case CsEditField::EvExplY:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].explY); break;
        case CsEditField::EvDialogId:
            if (cs && selectedEvent_ >= 0) setA(cs->events[selectedEvent_].dialogId); break;
        case CsEditField::EvSfxPath:
            if (cs && selectedEvent_ >= 0) setA(cs->events[selectedEvent_].sfxPath); break;
        case CsEditField::EvFlagName:
            if (cs && selectedEvent_ >= 0) setA(cs->events[selectedEvent_].flagName); break;
        case CsEditField::EvChainId:
            if (cs && selectedEvent_ >= 0) setA(cs->events[selectedEvent_].chainCsId); break;
        case CsEditField::EvSpawnX:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].spawnX); break;
        case CsEditField::EvSpawnY:
            if (cs && selectedEvent_ >= 0) setF(cs->events[selectedEvent_].spawnY); break;

        // Dialog fields
        case CsEditField::DlgSeqId:
            if (cs && selectedDialogSeq_ >= 0 && selectedDialogSeq_ < (int)cs->dialogs.size())
                setA(cs->dialogs[selectedDialogSeq_].id); break;
        case CsEditField::DlgLineChr:
            if (cs && selectedDialogSeq_ >= 0 && selectedDialogLine_ >= 0) {
                auto& lines = cs->dialogs[selectedDialogSeq_].lines;
                if (selectedDialogLine_ < (int)lines.size()) setA(lines[selectedDialogLine_].character);
            } break;
        case CsEditField::DlgLinePortrait:
            if (cs && selectedDialogSeq_ >= 0 && selectedDialogLine_ >= 0) {
                auto& lines = cs->dialogs[selectedDialogSeq_].lines;
                if (selectedDialogLine_ < (int)lines.size()) setA(lines[selectedDialogLine_].portrait);
            } break;
        case CsEditField::DlgLineText:
            if (cs && selectedDialogSeq_ >= 0 && selectedDialogLine_ >= 0) {
                auto& lines = cs->dialogs[selectedDialogSeq_].lines;
                if (selectedDialogLine_ < (int)lines.size()) setA(lines[selectedDialogLine_].text);
            } break;
        case CsEditField::DlgLineSfx:
            if (cs && selectedDialogSeq_ >= 0 && selectedDialogLine_ >= 0) {
                auto& lines = cs->dialogs[selectedDialogSeq_].lines;
                if (selectedDialogLine_ < (int)lines.size()) setA(lines[selectedDialogLine_].sfxPath);
            } break;

        default: {
            // Choices: ChoiceText0-3, ChoiceNext0-3, ChoiceFlag0-3
            int fi = (int)fid;
            int choiceTextBase = (int)CsEditField::ChoiceText0;
            int choiceNextBase = (int)CsEditField::ChoiceNext0;
            int choiceFlagBase = (int)CsEditField::ChoiceFlag0;
            if (fi >= choiceTextBase && fi < choiceTextBase + 4) {
                int ci = fi - choiceTextBase;
                if (cs && selectedDialogSeq_ >= 0 && selectedDialogLine_ >= 0) {
                    auto& lines = cs->dialogs[selectedDialogSeq_].lines;
                    if (selectedDialogLine_ < (int)lines.size() && ci < (int)lines[selectedDialogLine_].choices.size())
                        setA(lines[selectedDialogLine_].choices[ci].text);
                }
            } else if (fi >= choiceNextBase && fi < choiceNextBase + 4) {
                int ci = fi - choiceNextBase;
                if (cs && selectedDialogSeq_ >= 0 && selectedDialogLine_ >= 0) {
                    auto& lines = cs->dialogs[selectedDialogSeq_].lines;
                    if (selectedDialogLine_ < (int)lines.size() && ci < (int)lines[selectedDialogLine_].choices.size())
                        setA(lines[selectedDialogLine_].choices[ci].nextSeqId);
                }
            } else if (fi >= choiceFlagBase && fi < choiceFlagBase + 4) {
                int ci = fi - choiceFlagBase;
                if (cs && selectedDialogSeq_ >= 0 && selectedDialogLine_ >= 0) {
                    auto& lines = cs->dialogs[selectedDialogSeq_].lines;
                    if (selectedDialogLine_ < (int)lines.size() && ci < (int)lines[selectedDialogLine_].choices.size())
                        setA(lines[selectedDialogLine_].choices[ci].setFlag);
                }
            }
            break;
        }
    }
}

// ============================================================
//  Sub-panel: Cutscene list
// ============================================================

void CutsceneEditor::renderCutsceneList(int x, int y, int w, int h) {
    // Background
    fill(x, y, w, h, {192, 192, 192, 255});
    if (ui_) ui_->drawWin98Bevel(x, y, w, h, false);

    // Header
    sectionHeader(x+2, y+2, w-4, "Cutscenes");

    int rowH = 18;
    int listTop = y + 20;
    int listBot = y + h - 22;
    int listH   = listBot - listTop;

    // Scrollable list
    SDL_Rect clip = {x, listTop, w, listH};
    SDL_RenderSetClipRect(r_, &clip);

    if (lib_) {
        int iy = listTop - csListScrollY_;
        for (int i = 0; i < (int)lib_->cutscenes.size(); i++) {
            if (iy + rowH > listTop && iy < listBot) {
                bool sel = (i == selectedCutscene_);
                if (sel) fill(x+2, iy, w-4, rowH-1, UI::W98::Navy);
                else if (ui_ && ui_->mouseX >= x+2 && ui_->mouseX < x+w-2
                             && ui_->mouseY >= iy && ui_->mouseY < iy+rowH)
                    fill(x+2, iy, w-4, rowH-1, {160, 160, 200, 255});
                txt(lib_->cutscenes[i].id.c_str(), x+6, iy+3,
                    sel ? UI::W98::White : UI::W98::Black, 11);
            }
            iy += rowH;
        }
    }
    SDL_RenderSetClipRect(r_, nullptr);

    // Bottom buttons — offset by 18 extra px to clear the Win98 status bar
    int btnY = y + h - 38;
    if (btn(40, "+New", x+2, btnY, 44, 18)) addCutscene();
    if (selectedCutscene_ >= 0 && btn(41, "Del", x+50, btnY, 36, 18))
        deleteCutscene(selectedCutscene_);
}

// ============================================================
//  Sub-panel: Actor list
// ============================================================

void CutsceneEditor::renderActorList(int x, int y, int w, int h) {
    fill(x, y, w, h, {192, 192, 192, 255});
    if (ui_) ui_->drawWin98Bevel(x, y, w, h, false);

    sectionHeader(x+2, y+2, w-4, "Actors");

    static const char* typeShort[] = {"PLR","ENM","SPR"};
    static const SDL_Color typeCol[] = {{80,180,255,255},{255,120,80,255},{180,220,100,255}};

    const Cutscene* cs = currentCutscene();
    int rowH = 18, listTop = y+20, listBot = y+h-22;
    SDL_Rect clip = {x, listTop, w, listBot - listTop};
    SDL_RenderSetClipRect(r_, &clip);

    if (cs) {
        int iy = listTop - actorScrollY_;
        for (int i = 0; i < (int)cs->actors.size(); i++) {
            if (iy + rowH > listTop && iy < listBot) {
                bool sel = (i == selectedActor_);
                if (sel) fill(x+2, iy, w-4, rowH-1, UI::W98::Navy);
                int ti = (int)cs->actors[i].type;
                if (ti < 0 || ti > 2) ti = 2;
                fill(x+3, iy+3, 22, 12, typeCol[ti]);
                txt(typeShort[ti], x+4, iy+3, UI::W98::Black, 10);
                txt(cs->actors[i].name.c_str(), x+28, iy+3,
                    sel ? UI::W98::White : UI::W98::Black, 11);
            }
            iy += rowH;
        }
    }
    SDL_RenderSetClipRect(r_, nullptr);

    int btnY = y + h - 38;
    if (btn(42, "+Actor", x+2, btnY, 52, 18)) {
        showActorMenu_ = !showActorMenu_;
        showEnemyTypeMenu_ = false;
    }
    if (selectedActor_ >= 0 && btn(43, "Del", x+58, btnY, 36, 18))
        deleteActor(selectedActor_);
    if (selectedActor_ >= 0) {
        if (btn(44, "Props", x+98, btnY, 44, 18)) {
            propsMode_ = CsPropsMode::Actor;
            selectedEvent_ = -1;
        }
    }
}

// ============================================================
//  Sub-panel: Timeline
// ============================================================

void CutsceneEditor::renderTimeline(int x, int y, int w, int h) {
    // Black background
    fill(x, y, w, h, {18, 18, 18, 255});
    if (ui_) ui_->drawWin98Bevel(x, y, w, h, false);

    // Toolbar
    int ty = y + 2;
    fill(x, ty, w, TL_TOOLBAR_H, {36, 36, 36, 255});

    SDL_Color playCol = playing_ ? SDL_Color{80,220,80,255} : SDL_Color{200,200,200,255};
    if (btn(45, playing_ ? "||" : "|>", x+2, ty+3, 22, 18)) {
        playing_ = !playing_;
        if (playing_ && scrubTime_ < 0) scrubTime_ = 0;
    }

    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%.2fs", (double)scrubTime_);
    txt(tbuf, x+28, ty+5, playCol, 11);

    if (btn(46, "-", x+w-42, ty+3, 18, 18))
        timelineScale_ = std::max(10.0f, timelineScale_ * 0.8f);
    if (btn(47, "+", x+w-22, ty+3, 18, 18))
        timelineScale_ = std::min(400.0f, timelineScale_ * 1.25f);

    // Time ruler
    int rulerY = y + TL_TOOLBAR_H + 2;
    fill(x, rulerY, w, TL_RULER_H, {40, 40, 40, 255});

    float secStep = 1.0f;
    if (timelineScale_ < 20) secStep = 5.0f;
    if (timelineScale_ < 8)  secStep = 10.0f;

    float firstTick = ceilf(timelineStart_ / secStep) * secStep;
    for (float t = firstTick; t < timelineStart_ + w/timelineScale_; t += secStep) {
        int px = timeToPx(t, x);
        if (px < x || px > x+w) continue;
        vline(px, rulerY, rulerY+TL_RULER_H, {80, 80, 80, 255});
        char tbuf2[12];
        snprintf(tbuf2, sizeof(tbuf2), "%.0fs", (double)t);
        txt(tbuf2, px+2, rulerY+2, {120, 120, 120, 255}, 10);
    }

    // Content rows
    int contentY = rulerY + TL_RULER_H;
    int contentH = h - TL_TOOLBAR_H - 2 - TL_RULER_H;
    SDL_Rect clip = {x, contentY, w, contentH};
    SDL_RenderSetClipRect(r_, &clip);

    const Cutscene* cs = currentCutscene();

    // Global row
    fill(x, contentY, w, TL_ROW_H, {24, 24, 24, 255});
    txt("[global]", x+2, contentY+5, {90, 120, 90, 255}, 10);
    hline(x, x+w, contentY+TL_ROW_H-1, {45, 45, 45, 255});

    if (cs) {
        for (int i = 0; i < (int)cs->actors.size(); i++) {
            int ry = actorRowY(i, contentY);
            SDL_Color rowBg = (i % 2 == 0) ? SDL_Color{26,26,26,255} : SDL_Color{22,22,22,255};
            fill(x, ry, w, TL_ROW_H, rowBg);
            hline(x, x+w, ry+TL_ROW_H-1, {45, 45, 45, 255});
        }

        // Event blocks
        for (int ei = 0; ei < (int)cs->events.size(); ei++) {
            const auto& ev = cs->events[ei];
            int row = -1;
            for (int i = 0; i < (int)cs->actors.size(); i++)
                if (cs->actors[i].id == ev.actorId) { row = i; break; }

            int ry = (row < 0)
                ? contentY + 2
                : actorRowY(row, contentY) + 2;
            int rh = TL_ROW_H - 4;

            int px0 = timeToPx(ev.startTime, x);
            int px1 = timeToPx(ev.startTime + std::max(ev.duration, 0.04f), x);
            int bw  = std::max(px1 - px0, 4);
            if (px0 > x+w || px1 < x) continue;
            int clampedX = std::max(px0, x);
            int clampedW = std::min(bw, x+w - clampedX);

            SDL_Color bc = eventColor(ev.type);
            bool isSel = (ei == selectedEvent_);
            if (isSel) {
                bc.r = (Uint8)std::min(255, bc.r + 60);
                bc.g = (Uint8)std::min(255, bc.g + 60);
                bc.b = (Uint8)std::min(255, bc.b + 60);
            }
            fillBlend(clampedX, ry, clampedW, rh, {bc.r, bc.g, bc.b, 200});
            if (isSel) outline(clampedX, ry, clampedW, rh, {255, 255, 255, 255});
            if (bw > 30) txt(csEventTypeName(ev.type), clampedX+2, ry+3, {0,0,0,255}, 10);

            // Resize handle
            fill(clampedX + clampedW - 4, ry, 4, rh, {255, 255, 255, 60});
        }

        // Vertical grid
        for (float t = firstTick; t < timelineStart_ + w/timelineScale_; t += secStep) {
            int px = timeToPx(t, x);
            if (px >= x && px <= x+w)
                vline(px, contentY, contentY+contentH, {45, 45, 45, 255});
        }
    }

    SDL_RenderSetClipRect(r_, nullptr);

    // Scrubber
    int spx = timeToPx(scrubTime_, x);
    if (spx >= x && spx <= x+w) {
        vline(spx, rulerY, y+h, {255, 200, 0, 255});
        fill(spx-5, rulerY, 10, 8, {255, 200, 0, 255});
    }

    // Add event button
    if (btn(48, "+Event", x+w-72, y+h-20, 68, 18))
        showEventMenu_ = !showEventMenu_;
}

// ============================================================
//  Props panel sections
// ============================================================

static const char* easeName(CsEase e) {
    switch (e) {
        case CsEase::Linear:    return "Linear";
        case CsEase::EaseIn:    return "Ease In";
        case CsEase::EaseOut:   return "Ease Out";
        case CsEase::EaseInOut: return "Ease InOut";
        case CsEase::Instant:   return "Instant";
        default:                return "?";
    }
}

static const char* actorTypeName(CsActorType t) {
    switch (t) {
        case CsActorType::Player:     return "Player";
        case CsActorType::Enemy:      return "Enemy";
        case CsActorType::FreeSprite: return "Free Sprite";
        default: return "?";
    }
}

static const char* enemyTypeName(CsEnemyType t) {
    switch (t) {
        case CsEnemyType::Melee:   return "Melee";
        case CsEnemyType::Shooter: return "Shooter";
        case CsEnemyType::Brute:   return "Brute";
        case CsEnemyType::Scout:   return "Scout";
        case CsEnemyType::Sniper:  return "Sniper";
        case CsEnemyType::Gunner:  return "Gunner";
        default: return "?";
    }
}

void CutsceneEditor::renderProps_Scene(int x, int& cy, int w, int maxY) {
    const Cutscene* cs = currentCutscene();
    if (!cs) {
        txt("No cutscene selected.", x+4, cy+2, {120,120,120,255}, 11);
        cy += 16;
        return;
    }

    sectionHeader(x, cy, w, "Scene Properties");
    cy += 18;

    char buf[256];
    if (fieldRow("ID:", cs->id.c_str(), CsEditField::CsId, x, cy, w))
        startEdit(CsEditField::CsId, cs->id.c_str());

    snprintf(buf, sizeof(buf), "%.2fs", (double)cs->totalDuration());
    labelRow("Duration:", buf, x, cy, w);

    if (boolRow("Block Input:", cs->blockInput, x, cy, w))
        current()->blockInput = !current()->blockInput;

    if (fieldRow("Chain On End:", cs->chainOnEnd.c_str(), CsEditField::CsChainOnEnd, x, cy, w))
        startEdit(CsEditField::CsChainOnEnd, cs->chainOnEnd.c_str());

    sepLine(x, cy, w);

    // Dialogs section
    sectionHeader(x, cy, w, "Dialog Sequences");
    cy += 18;

    for (int i = 0; i < (int)cs->dialogs.size() && cy < maxY - 20; i++) {
        bool sel = (i == selectedDialogSeq_);
        if (sel) fill(x, cy, w, 16, UI::W98::Navy);
        snprintf(buf, sizeof(buf), "%s  (%d lines)",
                 cs->dialogs[i].id.c_str(), (int)cs->dialogs[i].lines.size());
        txt(buf, x+4, cy+2, sel ? UI::W98::White : UI::W98::Black, 11);

        if (ui_ && ui_->mouseClicked && ui_->mouseX >= x && ui_->mouseX < x+w
                && ui_->mouseY >= cy && ui_->mouseY < cy+16) {
            selectedDialogSeq_   = i;
            selectedDialogLine_  = -1;
            propsMode_  = CsPropsMode::DialogSeq;
            ui_->mouseClicked = false;
        }
        cy += 17;
    }

    if (cy < maxY - 22) {
        if (btn(56, "+Seq", x, cy, 44, 18)) addDialogSeq();
        if (selectedDialogSeq_ >= 0 && btn(57, "Del", x+48, cy, 36, 18))
            deleteDialogSeq(selectedDialogSeq_);
        if (selectedDialogSeq_ >= 0 && btn(58, "Edit", x+88, cy, 36, 18)) {
            propsMode_ = CsPropsMode::DialogSeq;
        }
        cy += 22;
    }
}

void CutsceneEditor::renderProps_Actor(int x, int& cy, int w, int maxY) {
    Cutscene* cs = current();
    if (!cs || selectedActor_ < 0 || selectedActor_ >= (int)cs->actors.size()) {
        txt("No actor selected.", x+4, cy+2, {120,120,120,255}, 11);
        cy += 16; return;
    }
    CsActor& a = cs->actors[selectedActor_];
    char buf[64];

    sectionHeader(x, cy, w, "Actor Properties");
    cy += 18;

    if (fieldRow("Name:", a.name.c_str(), CsEditField::ActorName, x, cy, w))
        startEdit(CsEditField::ActorName, a.name.c_str());

    labelRow("Type:", actorTypeName(a.type), x, cy, w);

    if (a.type == CsActorType::Enemy) {
        labelRow("Kind:", enemyTypeName(a.enemyType), x, cy, w);
        if (btn(49, "Change", x + w - 58, cy - 14, 56, 14)) {
            showEnemyTypeMenu_ = !showEnemyTypeMenu_;
        }
    }

    if (a.type == CsActorType::FreeSprite) {
        const char* sp = a.spritePath.empty() ? "(none)" : a.spritePath.c_str();
        if (fieldRow("Sprite:", sp, CsEditField::ActorSprite, x, cy, w))
            startEdit(CsEditField::ActorSprite, a.spritePath.c_str());
    }

    sepLine(x, cy, w);
    sectionHeader(x, cy, w, "Start State");
    cy += 18;

    snprintf(buf, sizeof(buf), "%.1f", (double)a.startX);
    if (fieldRow("Start X:", buf, CsEditField::ActorStartX, x, cy, w))
        startEdit(CsEditField::ActorStartX, buf);

    snprintf(buf, sizeof(buf), "%.1f", (double)a.startY);
    if (fieldRow("Start Y:", buf, CsEditField::ActorStartY, x, cy, w))
        startEdit(CsEditField::ActorStartY, buf);

    snprintf(buf, sizeof(buf), "%.1f", (double)a.startRot);
    if (fieldRow("Rotation:", buf, CsEditField::ActorStartRot, x, cy, w))
        startEdit(CsEditField::ActorStartRot, buf);

    snprintf(buf, sizeof(buf), "%.2f", (double)a.startAlpha);
    if (fieldRow("Alpha:", buf, CsEditField::ActorStartAlpha, x, cy, w))
        startEdit(CsEditField::ActorStartAlpha, buf);

    snprintf(buf, sizeof(buf), "%.2f", (double)a.startScaleX);
    if (fieldRow("Scale X:", buf, CsEditField::ActorStartScaleX, x, cy, w))
        startEdit(CsEditField::ActorStartScaleX, buf);

    snprintf(buf, sizeof(buf), "%.2f", (double)a.startScaleY);
    if (fieldRow("Scale Y:", buf, CsEditField::ActorStartScaleY, x, cy, w))
        startEdit(CsEditField::ActorStartScaleY, buf);

    if (boolRow("Visible:", a.startVisible, x, cy, w)) a.startVisible = !a.startVisible;
    if (boolRow("Flip H:", a.flipH, x, cy, w)) a.flipH = !a.flipH;

    if (cy < maxY - 22) {
        sepLine(x, cy, w);
        if (btn(50, "Delete Actor", x, cy, 90, 18)) deleteActor(selectedActor_);
    }
}

void CutsceneEditor::renderProps_Event(int x, int& cy, int w, int maxY) {
    Cutscene* cs = current();
    if (!cs || selectedEvent_ < 0 || selectedEvent_ >= (int)cs->events.size()) {
        txt("No event selected.", x+4, cy+2, {120,120,120,255}, 11);
        cy += 16; return;
    }
    CsEvent& ev = cs->events[selectedEvent_];
    char buf[64];

    sectionHeader(x, cy, w, "Event Properties");
    cy += 18;

    labelRow("Type:", csEventTypeName(ev.type), x, cy, w, 72, {80, 180, 255, 255});

    snprintf(buf, sizeof(buf), "%.3f", (double)ev.startTime);
    if (fieldRow("Start (s):", buf, CsEditField::EvStartTime, x, cy, w))
        startEdit(CsEditField::EvStartTime, buf);

    // Duration (hide for zero-dur types)
    bool zerodup = (ev.type == CsEventType::Wait || ev.type == CsEventType::PlaySFX
                 || ev.type == CsEventType::SpawnExplosion || ev.type == CsEventType::SpawnActor
                 || ev.type == CsEventType::DespawnActor   || ev.type == CsEventType::SetFlag
                 || ev.type == CsEventType::ChainCutscene  || ev.type == CsEventType::EndCutscene);
    if (!zerodup) {
        snprintf(buf, sizeof(buf), "%.3f", (double)ev.duration);
        if (fieldRow("Dur (s):", buf, CsEditField::EvDuration, x, cy, w))
            startEdit(CsEditField::EvDuration, buf);

        labelRow("Ease:", easeName(ev.ease), x, cy, w);
        if (btn(51, "Change", x + w - 58, cy - 14, 56, 14)) {
            showEaseMenu_ = !showEaseMenu_;
        }
    }

    // Actor selector (for actor-specific events)
    bool actorBound = (ev.type != CsEventType::CameraMove
                    && ev.type != CsEventType::CameraZoom
                    && ev.type != CsEventType::CameraShake
                    && ev.type != CsEventType::ScreenFade
                    && ev.type != CsEventType::CinematicBars
                    && ev.type != CsEventType::Wait
                    && ev.type != CsEventType::Dialog
                    && ev.type != CsEventType::PlaySFX
                    && ev.type != CsEventType::SpawnExplosion
                    && ev.type != CsEventType::SetFlag
                    && ev.type != CsEventType::ChainCutscene
                    && ev.type != CsEventType::EndCutscene);
    if (actorBound && cs) {
        const CsActor* a = cs->findActor(ev.actorId);
        const char* aname = a ? a->name.c_str() : "(global)";
        labelRow("Actor:", aname, x, cy, w, 72, {160, 220, 100, 255});
    }

    sepLine(x, cy, w);

    // Type-specific fields
    switch (ev.type) {
        case CsEventType::Move:
        case CsEventType::CameraMove:
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.fromX);
            if (fieldRow("From X:", buf, CsEditField::EvFromX, x, cy, w)) startEdit(CsEditField::EvFromX, buf);
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.fromY);
            if (fieldRow("From Y:", buf, CsEditField::EvFromY, x, cy, w)) startEdit(CsEditField::EvFromY, buf);
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.toX);
            if (fieldRow("To X:", buf, CsEditField::EvToX, x, cy, w)) startEdit(CsEditField::EvToX, buf);
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.toY);
            if (fieldRow("To Y:", buf, CsEditField::EvToY, x, cy, w)) startEdit(CsEditField::EvToY, buf);
            break;

        case CsEventType::Rotate:
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.fromRot);
            if (fieldRow("From Rot:", buf, CsEditField::EvFromRot, x, cy, w)) startEdit(CsEditField::EvFromRot, buf);
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.toRot);
            if (fieldRow("To Rot:", buf, CsEditField::EvToRot, x, cy, w)) startEdit(CsEditField::EvToRot, buf);
            break;

        case CsEventType::Scale:
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.fromScaleX);
            if (fieldRow("From SX:", buf, CsEditField::EvFromSX, x, cy, w)) startEdit(CsEditField::EvFromSX, buf);
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.fromScaleY);
            if (fieldRow("From SY:", buf, CsEditField::EvFromSY, x, cy, w)) startEdit(CsEditField::EvFromSY, buf);
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.toScaleX);
            if (fieldRow("To SX:", buf, CsEditField::EvToSX, x, cy, w)) startEdit(CsEditField::EvToSX, buf);
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.toScaleY);
            if (fieldRow("To SY:", buf, CsEditField::EvToSY, x, cy, w)) startEdit(CsEditField::EvToSY, buf);
            break;

        case CsEventType::Alpha:
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.fromAlpha);
            if (fieldRow("From:", buf, CsEditField::EvFromAlpha, x, cy, w)) startEdit(CsEditField::EvFromAlpha, buf);
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.toAlpha);
            if (fieldRow("To:", buf, CsEditField::EvToAlpha, x, cy, w)) startEdit(CsEditField::EvToAlpha, buf);
            break;

        case CsEventType::Flash: {
            snprintf(buf, sizeof(buf), "%.0f", (double)ev.flashR);
            if (fieldRow("R (0-255):", buf, CsEditField::EvFlashR, x, cy, w)) startEdit(CsEditField::EvFlashR, buf);
            snprintf(buf, sizeof(buf), "%.0f", (double)ev.flashG);
            if (fieldRow("G (0-255):", buf, CsEditField::EvFlashG, x, cy, w)) startEdit(CsEditField::EvFlashG, buf);
            snprintf(buf, sizeof(buf), "%.0f", (double)ev.flashB);
            if (fieldRow("B (0-255):", buf, CsEditField::EvFlashB, x, cy, w)) startEdit(CsEditField::EvFlashB, buf);
            // Color preview
            SDL_Color preview = {(Uint8)ev.flashR, (Uint8)ev.flashG, (Uint8)ev.flashB, 255};
            fill(x + w - 30, cy - 12, 26, 10, preview);
            outline(x + w - 30, cy - 12, 26, 10, UI::W98::Shadow);
            break;
        }

        case CsEventType::CameraZoom:
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.fromZoom);
            if (fieldRow("From:", buf, CsEditField::EvFromZoom, x, cy, w)) startEdit(CsEditField::EvFromZoom, buf);
            snprintf(buf, sizeof(buf), "%.2f", (double)ev.toZoom);
            if (fieldRow("To:", buf, CsEditField::EvToZoom, x, cy, w)) startEdit(CsEditField::EvToZoom, buf);
            break;

        case CsEventType::CameraShake:
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.shakeStrength);
            if (fieldRow("Strength:", buf, CsEditField::EvShake, x, cy, w)) startEdit(CsEditField::EvShake, buf);
            break;

        case CsEventType::ScreenFade:
            if (boolRow("To Black:", ev.fadeToBlack, x, cy, w)) ev.fadeToBlack = !ev.fadeToBlack;
            break;

        case CsEventType::CinematicBars:
            if (boolRow("Show:", ev.showBars, x, cy, w)) ev.showBars = !ev.showBars;
            break;

        case CsEventType::SetVisible:
            if (boolRow("Visible:", ev.visible, x, cy, w)) ev.visible = !ev.visible;
            break;

        case CsEventType::SetFrame:
            snprintf(buf, sizeof(buf), "%d", ev.frame);
            if (fieldRow("Frame:", buf, CsEditField::EvFromX, x, cy, w)) {
                snprintf(buf, sizeof(buf), "%d", ev.frame);
                startEdit(CsEditField::EvFromX, buf);
            }
            break;

        case CsEventType::SpawnExplosion:
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.explX);
            if (fieldRow("World X:", buf, CsEditField::EvExplX, x, cy, w)) startEdit(CsEditField::EvExplX, buf);
            snprintf(buf, sizeof(buf), "%.1f", (double)ev.explY);
            if (fieldRow("World Y:", buf, CsEditField::EvExplY, x, cy, w)) startEdit(CsEditField::EvExplY, buf);
            break;

        case CsEventType::Dialog: {
            if (fieldRow("Dialog ID:", ev.dialogId.c_str(), CsEditField::EvDialogId, x, cy, w))
                startEdit(CsEditField::EvDialogId, ev.dialogId.c_str());
            // Show matching dialog seq if found
            const Cutscene* ccs = currentCutscene();
            if (ccs && !ev.dialogId.empty()) {
                const CsDialogSeq* seq = ccs->findDialog(ev.dialogId);
                if (seq) {
                    snprintf(buf, sizeof(buf), "-> %d lines", (int)seq->lines.size());
                    txt(buf, x+4, cy, {80, 200, 100, 255}, 10);
                } else {
                    txt("(sequence not found)", x+4, cy, {200, 80, 80, 255}, 10);
                }
                cy += 14;
            }
            break;
        }

        case CsEventType::PlaySFX:
            if (fieldRow("SFX Path:", ev.sfxPath.c_str(), CsEditField::EvSfxPath, x, cy, w))
                startEdit(CsEditField::EvSfxPath, ev.sfxPath.c_str());
            break;

        case CsEventType::SpawnActor:
            if (boolRow("Override Pos:", ev.spawnOverridePos, x, cy, w)) ev.spawnOverridePos = !ev.spawnOverridePos;
            if (ev.spawnOverridePos) {
                snprintf(buf, sizeof(buf), "%.1f", (double)ev.spawnX);
                if (fieldRow("Spawn X:", buf, CsEditField::EvSpawnX, x, cy, w)) startEdit(CsEditField::EvSpawnX, buf);
                snprintf(buf, sizeof(buf), "%.1f", (double)ev.spawnY);
                if (fieldRow("Spawn Y:", buf, CsEditField::EvSpawnY, x, cy, w)) startEdit(CsEditField::EvSpawnY, buf);
            }
            break;

        case CsEventType::DespawnActor:
            // Actor is set via actorId; shown in actor row above
            txt("Hides the bound actor.", x+4, cy, {130,130,130,255}, 10);
            cy += 14;
            break;

        case CsEventType::SetFlag:
            if (fieldRow("Flag Name:", ev.flagName.c_str(), CsEditField::EvFlagName, x, cy, w))
                startEdit(CsEditField::EvFlagName, ev.flagName.c_str());
            if (boolRow("Set True:", ev.flagValue, x, cy, w)) ev.flagValue = !ev.flagValue;
            break;

        case CsEventType::ChainCutscene:
            if (fieldRow("Target CS:", ev.chainCsId.c_str(), CsEditField::EvChainId, x, cy, w))
                startEdit(CsEditField::EvChainId, ev.chainCsId.c_str());
            break;

        case CsEventType::EndCutscene:
            txt("Immediately ends this cutscene.", x+4, cy, {130,130,130,255}, 10);
            cy += 14;
            break;

        default: break;
    }

    if (cy < maxY - 22) {
        sepLine(x, cy, w);
        if (btn(52, "Delete Event", x, cy, 90, 18)) deleteEvent(selectedEvent_);
        // Copy/Paste
        if (btn(53, "Copy", x+94, cy, 40, 18)) {
            if (cs && selectedEvent_ >= 0) { hasEventClipboard_ = true; eventClipboard_ = cs->events[selectedEvent_]; }
        }
        if (hasEventClipboard_ && btn(54, "Paste", x+138, cy, 44, 18)) {
            if (cs) { cs->events.push_back(eventClipboard_); cs->events.back().startTime = scrubTime_; selectedEvent_ = (int)cs->events.size()-1; propsMode_ = CsPropsMode::Event; }
        }
    }
}

void CutsceneEditor::renderProps_DialogSeq(int x, int& cy, int w, int maxY) {
    Cutscene* cs = current();
    if (!cs || selectedDialogSeq_ < 0 || selectedDialogSeq_ >= (int)cs->dialogs.size()) {
        txt("No dialog sequence selected.", x+4, cy+2, {120,120,120,255}, 11);
        cy += 16; return;
    }
    CsDialogSeq& seq = cs->dialogs[selectedDialogSeq_];
    char buf[256];

    sectionHeader(x, cy, w, "Dialog Sequence");
    cy += 18;

    if (fieldRow("Seq ID:", seq.id.c_str(), CsEditField::DlgSeqId, x, cy, w))
        startEdit(CsEditField::DlgSeqId, seq.id.c_str());

    snprintf(buf, sizeof(buf), "%d lines", (int)seq.lines.size());
    labelRow("Lines:", buf, x, cy, w);

    sepLine(x, cy, w);
    sectionHeader(x, cy, w, "Lines");
    cy += 18;

    for (int li = 0; li < (int)seq.lines.size() && cy < maxY - 22; li++) {
        bool sel = (li == selectedDialogLine_);
        if (sel) fill(x, cy, w, 16, UI::W98::Navy);
        snprintf(buf, sizeof(buf), "%d. [%s] %s",
                 li+1,
                 seq.lines[li].character.empty() ? "?" : seq.lines[li].character.c_str(),
                 seq.lines[li].text.c_str());
        buf[62] = '\0'; // truncate
        txt(buf, x+4, cy+2, sel ? UI::W98::White : UI::W98::Black, 10);

        if (!seq.lines[li].choices.empty()) {
            snprintf(buf, sizeof(buf), "[%d choices]", (int)seq.lines[li].choices.size());
            txtR(buf, x+w-2, cy+2, {100,160,255,255}, 10);
        }

        if (ui_ && ui_->mouseClicked && ui_->mouseX >= x && ui_->mouseX < x+w
                && ui_->mouseY >= cy && ui_->mouseY < cy+16) {
            selectedDialogLine_ = li;
            propsMode_ = CsPropsMode::DialogLine;
            ui_->mouseClicked = false;
        }
        cy += 17;
    }

    if (cy < maxY - 44) {
        if (btn(59, "+Line", x, cy, 44, 18)) addDialogLine(selectedDialogSeq_);
        if (selectedDialogLine_ >= 0 && btn(60, "Del Line", x+48, cy, 60, 18))
            deleteDialogLine(selectedDialogSeq_, selectedDialogLine_);
        if (selectedDialogLine_ >= 0 && btn(61, "Edit", x+112, cy, 36, 18))
            propsMode_ = CsPropsMode::DialogLine;
        cy += 22;
        if (btn(62, "Back to Scene", x, cy, 100, 18)) propsMode_ = CsPropsMode::Scene;
    }
}

void CutsceneEditor::renderProps_DialogLine(int x, int& cy, int w, int maxY) {
    Cutscene* cs = current();
    if (!cs || selectedDialogSeq_ < 0 || selectedDialogLine_ < 0) {
        txt("No line selected.", x+4, cy+2, {120,120,120,255}, 11);
        cy += 16; return;
    }
    auto& lines = cs->dialogs[selectedDialogSeq_].lines;
    if (selectedDialogLine_ >= (int)lines.size()) { selectedDialogLine_ = -1; propsMode_ = CsPropsMode::DialogSeq; return; }
    CsDialogLine& line = lines[selectedDialogLine_];

    char buf[256];
    sectionHeader(x, cy, w, "Dialog Line");
    cy += 18;

    snprintf(buf, sizeof(buf), "%d / %d", selectedDialogLine_+1, (int)lines.size());
    labelRow("Line:", buf, x, cy, w);

    if (fieldRow("Character:", line.character.c_str(), CsEditField::DlgLineChr, x, cy, w))
        startEdit(CsEditField::DlgLineChr, line.character.c_str());

    if (fieldRow("Portrait:", line.portrait.c_str(), CsEditField::DlgLinePortrait, x, cy, w))
        startEdit(CsEditField::DlgLinePortrait, line.portrait.c_str());

    if (boolRow("Portrait L:", line.portraitLeft, x, cy, w)) line.portraitLeft = !line.portraitLeft;

    if (fieldRow("SFX:", line.sfxPath.c_str(), CsEditField::DlgLineSfx, x, cy, w))
        startEdit(CsEditField::DlgLineSfx, line.sfxPath.c_str());

    // Text field (tall)
    txt("Text:", x+2, cy+2, {100,100,100,255}, 11);
    cy += 14;
    bool textFocused = (activeField_ == CsEditField::DlgLineText);
    const char* tdisplay = textFocused ? editBuf_ : line.text.c_str();
    fill(x, cy, w-2, 28, textFocused ? UI::W98::FieldBg : SDL_Color{240,240,240,255});
    if (ui_) ui_->drawWin98Bevel(x, cy, w-2, 28, false);
    if (ui_) {
        char wrapped[520] = {};
        if (textFocused) {
            snprintf(wrapped, sizeof(wrapped), "%s%s", tdisplay, (int)(editBlinkT_*2)%2==0 ? "|" : "");
        } else {
            strncpy(wrapped, tdisplay, sizeof(wrapped)-1);
        }
        // Clip text display inside field
        SDL_Rect tclip = {x+2, cy+2, w-6, 24};
        SDL_RenderSetClipRect(r_, &tclip);
        ui_->drawTextWrapped(wrapped, x+2, cy+2, 11, w-6, {0,0,0,255});
        SDL_RenderSetClipRect(r_, nullptr);
    }
    if (ui_ && ui_->mouseClicked && ui_->mouseX >= x && ui_->mouseX < x+w-2
            && ui_->mouseY >= cy && ui_->mouseY < cy+28) {
        startEdit(CsEditField::DlgLineText, line.text.c_str());
        ui_->mouseClicked = false;
    }
    cy += 30;

    // Choices section
    sectionHeader(x, cy, w, "Choices (Branching)");
    cy += 18;

    static const CsEditField choiceTextFields[] = {CsEditField::ChoiceText0,CsEditField::ChoiceText1,CsEditField::ChoiceText2,CsEditField::ChoiceText3};
    static const CsEditField choiceNextFields[] = {CsEditField::ChoiceNext0,CsEditField::ChoiceNext1,CsEditField::ChoiceNext2,CsEditField::ChoiceNext3};
    static const CsEditField choiceFlagFields[] = {CsEditField::ChoiceFlag0,CsEditField::ChoiceFlag1,CsEditField::ChoiceFlag2,CsEditField::ChoiceFlag3};

    for (int ci = 0; ci < (int)line.choices.size() && cy < maxY - 40; ci++) {
        bool csel = (ci == selectedChoice_);
        snprintf(buf, sizeof(buf), "Choice %d", ci+1);
        sectionHeader(x+2, cy, w-4, buf);
        if (csel) fill(x+2, cy, w-4, 14, {0, 60, 120, 255});
        cy += 15;

        if (fieldRow("Text:", line.choices[ci].text.c_str(), choiceTextFields[ci], x, cy, w))
            startEdit(choiceTextFields[ci], line.choices[ci].text.c_str());
        if (fieldRow("Next Seq:", line.choices[ci].nextSeqId.c_str(), choiceNextFields[ci], x, cy, w))
            startEdit(choiceNextFields[ci], line.choices[ci].nextSeqId.c_str());
        if (fieldRow("Set Flag:", line.choices[ci].setFlag.c_str(), choiceFlagFields[ci], x, cy, w))
            startEdit(choiceFlagFields[ci], line.choices[ci].setFlag.c_str());
        if (!line.choices[ci].setFlag.empty()) {
            if (boolRow("Flag Val:", line.choices[ci].setFlagValue, x, cy, w))
                line.choices[ci].setFlagValue = !line.choices[ci].setFlagValue;
        }

        if (btn(55 + ci, "Del Choice", x+w-80, cy-14, 78, 14))
            deleteChoice(selectedDialogSeq_, selectedDialogLine_, ci);
        cy += 4;
    }

    if ((int)line.choices.size() < 4 && cy < maxY - 22) {
        if (btn(55, "+Choice", x, cy, 60, 18)) addChoice(selectedDialogSeq_, selectedDialogLine_);
        cy += 22;
    }

    if (cy < maxY - 22) {
        if (btn(63, "< Back", x, cy, 54, 18)) propsMode_ = CsPropsMode::DialogSeq;
        if (selectedDialogLine_ > 0 && btn(56, "Prev", x+58, cy, 40, 18)) {
            selectedDialogLine_--;
            cancelEdit();
        }
        if (selectedDialogLine_ < (int)lines.size()-1 && btn(57, "Next", x+102, cy, 40, 18)) {
            selectedDialogLine_++;
            cancelEdit();
        }
        if (btn(58, "Del Line", x+w-70, cy, 68, 18))
            deleteDialogLine(selectedDialogSeq_, selectedDialogLine_);
        cy += 22;
    }
}

// ============================================================
//  Props panel (router)
// ============================================================

void CutsceneEditor::renderPropsPanel(int x, int y, int w, int h) {
    fill(x, y, w, h, {192, 192, 192, 255});
    if (ui_) ui_->drawWin98Bevel(x, y, w, h, false);

    // Breadcrumb / mode tabs
    fill(x, y+1, w, 18, {160, 160, 160, 255});
    static const char* modeLabels[] = {"Scene","Actor","Event","Dialog Seq","Line Editor"};
    txt(modeLabels[(int)propsMode_], x+6, y+4, {0,0,128,255}, 11);

    int cy = y + 21;
    int maxY = y + h - 2;

    // Clipping
    SDL_Rect clip = {x, y+20, w, h-20};
    SDL_RenderSetClipRect(r_, &clip);
    cy -= propsScrollY_;

    switch (propsMode_) {
        case CsPropsMode::Scene:      renderProps_Scene(x+2, cy, w-4, maxY); break;
        case CsPropsMode::Actor:      renderProps_Actor(x+2, cy, w-4, maxY); break;
        case CsPropsMode::Event:      renderProps_Event(x+2, cy, w-4, maxY); break;
        case CsPropsMode::DialogSeq:  renderProps_DialogSeq(x+2, cy, w-4, maxY); break;
        case CsPropsMode::DialogLine: renderProps_DialogLine(x+2, cy, w-4, maxY); break;
    }

    SDL_RenderSetClipRect(r_, nullptr);
}

// ============================================================
//  Submenus (drawn on top of everything)
// ============================================================

void CutsceneEditor::renderActorTypeMenu(int anchorX, int anchorY) {
    static const char* labels[] = {"Player","Enemy","Free Sprite"};
    int mw = 90, mh = 18;
    int my = anchorY - 3 * mh - 4;
    fill(anchorX, my, mw, 3*mh+4, {192,192,192,255});
    if (ui_) ui_->drawWin98Bevel(anchorX, my, mw, 3*mh+4, true);
    for (int i = 0; i < 3; i++) {
        int iy = my + 2 + i * mh;
        bool hov = ui_ && ui_->mouseX >= anchorX && ui_->mouseX < anchorX+mw
                       && ui_->mouseY >= iy && ui_->mouseY < iy+mh-1;
        if (hov) fill(anchorX+1, iy, mw-2, mh-1, UI::W98::Navy);
        txt(labels[i], anchorX+6, iy+3, hov ? UI::W98::White : UI::W98::Black, 11);
    }
}

void CutsceneEditor::renderEnemyTypeMenu(int anchorX, int anchorY) {
    static const char* labels[] = {"Melee","Shooter","Brute","Scout","Sniper","Gunner"};
    int mw = 80, mh = 18, n = 6;
    int my = anchorY - n * mh - 4;
    fill(anchorX, my, mw, n*mh+4, {192,192,192,255});
    if (ui_) ui_->drawWin98Bevel(anchorX, my, mw, n*mh+4, true);
    for (int i = 0; i < n; i++) {
        int iy = my + 2 + i * mh;
        bool hov = ui_ && ui_->mouseX >= anchorX && ui_->mouseX < anchorX+mw
                       && ui_->mouseY >= iy && ui_->mouseY < iy+mh-1;
        if (hov) fill(anchorX+1, iy, mw-2, mh-1, UI::W98::Navy);
        txt(labels[i], anchorX+6, iy+3, hov ? UI::W98::White : UI::W98::Black, 11);
    }
}

void CutsceneEditor::renderEventTypeMenu(int anchorX, int anchorY) {
    int n = (int)CsEventType::COUNT;
    int mh = 17, mw = 110;
    int my = anchorY - n * mh - 4;
    if (my < 0) my = 0;
    fill(anchorX, my, mw, n*mh+4, {30,30,30,255});
    if (ui_) ui_->drawWin98Bevel(anchorX, my, mw, n*mh+4, true);
    for (int i = 0; i < n; i++) {
        int iy = my + 2 + i * mh;
        SDL_Color ec = eventColor((CsEventType)i);
        fill(anchorX+2, iy+2, 8, 12, ec);
        txt(csEventTypeName((CsEventType)i), anchorX+14, iy+3, {220,220,220,255}, 11);
    }
}

void CutsceneEditor::renderEaseMenu(int anchorX, int anchorY, CsEase current) {
    static const char* labels[] = {"Linear","Ease In","Ease Out","Ease InOut","Instant"};
    int n = 5, mh = 18, mw = 90;
    int my = anchorY - n * mh - 4;
    fill(anchorX, my, mw, n*mh+4, {192,192,192,255});
    if (ui_) ui_->drawWin98Bevel(anchorX, my, mw, n*mh+4, true);
    for (int i = 0; i < n; i++) {
        int iy = my + 2 + i * mh;
        bool sel = (i == (int)current);
        if (sel) fill(anchorX+1, iy, mw-2, mh-1, UI::W98::Navy);
        txt(labels[i], anchorX+6, iy+3, sel ? UI::W98::White : UI::W98::Black, 11);
    }
}

// ============================================================
//  Main render
// ============================================================

void CutsceneEditor::render(SDL_Renderer* r, int screenW, int panelY) {
    r_ = r;
    computeLayout(screenW, panelY);

    // Panel background
    fill(0, panelY_, screenW, panelH_, {192, 192, 192, 255});
    hline(0, screenW, panelY_, UI::W98::Shadow);

    renderCutsceneList(listX_, panelY_+1, listW_, panelH_-2);
    renderActorList(actorX_, panelY_+1, actorW_, panelH_-2);
    renderTimeline(timelineX_, panelY_+1, timelineW_, panelH_-2);
    renderPropsPanel(propsX_, panelY_+1, propsW_, panelH_-2);

    // Column separators
    vline(listX_+listW_, panelY_, panelY_+panelH_, UI::W98::Shadow);
    vline(actorX_+actorW_, panelY_, panelY_+panelH_, UI::W98::Shadow);
    vline(propsX_, panelY_, panelY_+panelH_, UI::W98::Shadow);

    // Status bar hint
    if (ui_) {
        ui_->drawWin98StatusBar(panelY_ + panelH_ - 16,
            editActive_ ? "Type value, Enter to confirm, Esc to cancel" :
            "Click timeline to scrub | Drag events | Right-click event to delete | +Event to add");
    }

    // Event type submenu (drawn on top)
    if (showEventMenu_) {
        int mx = timelineX_ + timelineW_ - 116;
        renderEventTypeMenu(mx, panelY_);
    }

    // Actor type submenu
    if (showActorMenu_) {
        renderActorTypeMenu(actorX_ + 2, panelY_);
    }

    // Enemy type submenu
    if (showEnemyTypeMenu_ && propsMode_ == CsPropsMode::Actor) {
        renderEnemyTypeMenu(propsX_ + propsW_ - 84, panelY_);
    }

    // Ease selector submenu
    if (showEaseMenu_) {
        const Cutscene* cs = currentCutscene();
        CsEase ce = CsEase::Linear;
        if (cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size())
            ce = cs->events[selectedEvent_].ease;
        renderEaseMenu(propsX_ + propsW_ - 96, panelY_, ce);
    }
}

// ============================================================
//  Update
// ============================================================

void CutsceneEditor::update(float dt) {
    if (!active_) return;
    editBlinkT_ += dt;
    if (editBlinkT_ > 2.0f) editBlinkT_ = 0;

    if (playing_) {
        scrubTime_ += dt;
        const Cutscene* cs = currentCutscene();
        float dur = cs ? cs->totalDuration() : 10.0f;
        if (dur < 0.1f) dur = 10.0f;
        if (scrubTime_ > dur) { scrubTime_ = 0; playing_ = false; }
        if (scrubTime_ > timelineStart_ + timelineW_/timelineScale_ - 1.0f)
            timelineStart_ = scrubTime_ - 1.0f;
        recomputePreview();
    }
}

// ============================================================
//  Input
// ============================================================

void CutsceneEditor::handleTimelineClick(int mx, int my, bool right) {
    computeLayout(screenW_, panelY_);
    int tx = timelineX_;
    if (mx < tx || mx > tx + timelineW_) return;
    if (my < panelY_ || my > panelY_ + panelH_) return;

    // Toolbar buttons are handled by win98Button in render; skip here
    int toolY = panelY_ + 2;
    if (my >= toolY && my < toolY + TL_TOOLBAR_H) return;

    int rulerY = panelY_ + TL_TOOLBAR_H + 2;
    int contentY = rulerY + TL_RULER_H;

    // Ruler: scrub
    if (my >= rulerY && my < contentY) {
        scrubTime_ = std::max(0.0f, pxToTime(mx, tx));
        draggingScrub_ = true;
        playing_ = false;
        recomputePreview();
        return;
    }

    const Cutscene* cs = currentCutscene();
    if (!cs) {
        // Empty click = scrub
        scrubTime_ = std::max(0.0f, pxToTime(mx, tx));
        playing_ = false; recomputePreview();
        return;
    }

    // Click on event?
    for (int ei = (int)cs->events.size()-1; ei >= 0; ei--) {
        const auto& ev = cs->events[ei];
        int row = -1;
        for (int i = 0; i < (int)cs->actors.size(); i++)
            if (cs->actors[i].id == ev.actorId) { row = i; break; }
        int ry = (row < 0)
            ? contentY + 2
            : actorRowY(row, contentY) + 2;
        int rh = TL_ROW_H - 4;
        int px0 = timeToPx(ev.startTime, tx);
        int px1 = timeToPx(ev.startTime + std::max(ev.duration, 0.04f), tx);

        if (mx >= px0 && mx <= px1 && my >= ry && my <= ry+rh) {
            if (right) { deleteEvent(ei); return; }
            selectedEvent_ = ei;
            propsMode_ = CsPropsMode::Event;
            selectedActor_ = -1;
            if (mx >= px1-4) {
                resizingEvent_ = true; resizeOrigDur_ = cs->events[ei].duration; resizeStartPx_ = mx;
            } else {
                draggingEvent_ = true; dragEventOrigT_ = cs->events[ei].startTime; dragStartPx_ = mx;
            }
            return;
        }
    }

    // Click on actor row label = select actor
    for (int i = 0; i < (int)cs->actors.size(); i++) {
        int ry = actorRowY(i, contentY);
        if (my >= ry && my < ry+TL_ROW_H && mx >= tx && mx < tx+80) {
            selectedActor_ = i; selectedEvent_ = -1; propsMode_ = CsPropsMode::Actor;
            return;
        }
    }

    // Empty area = scrub
    scrubTime_ = std::max(0.0f, pxToTime(mx, tx));
    playing_ = false; recomputePreview();
}

void CutsceneEditor::handleTimelineMotion(int mx, int /*my*/) {
    if (!lib_) return;
    Cutscene* cs = current();
    if (draggingScrub_) {
        scrubTime_ = std::max(0.0f, pxToTime(mx, timelineX_));
        recomputePreview(); return;
    }
    if (draggingEvent_ && cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
        float dt = (mx - dragStartPx_) / timelineScale_;
        cs->events[selectedEvent_].startTime = std::max(0.0f, dragEventOrigT_ + dt);
        recomputePreview(); return;
    }
    if (resizingEvent_ && cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
        float ddt = (mx - resizeStartPx_) / timelineScale_;
        cs->events[selectedEvent_].duration = std::max(0.001f, resizeOrigDur_ + ddt);
        recomputePreview(); return;
    }
}

void CutsceneEditor::handleTimelineRelease() {
    draggingEvent_ = resizingEvent_ = draggingScrub_ = false;
}

void CutsceneEditor::handleListClick(int mx, int my) {
    if (!lib_) return;
    int btnY = panelY_ + panelH_ - 20;

    // Cutscene list
    if (mx >= listX_ && mx < listX_+listW_) {
        int listTop = panelY_ + 20;
        int iy = listTop - csListScrollY_;
        for (int i = 0; i < (int)lib_->cutscenes.size(); i++) {
            if (my >= iy && my < iy+18) {
                if (selectedCutscene_ != i) {
                    selectedCutscene_ = i;
                    selectedActor_ = selectedEvent_ = selectedDialogSeq_ = selectedDialogLine_ = -1;
                    propsMode_ = CsPropsMode::Scene;
                    recomputePreview();
                }
                return;
            }
            iy += 18;
        }
        return;
    }

    // Actor list
    if (mx >= actorX_ && mx < actorX_+actorW_) {
        // Actor type submenu click
        if (showActorMenu_) {
            int mw = 90, mh = 18;
            int my0 = panelY_ - 3*mh - 4;
            if (mx >= actorX_+2 && mx < actorX_+2+mw && my >= my0 && my < my0+3*mh+4) {
                int idx = (my - my0 - 2) / mh;
                if (idx == 0) addActor(CsActorType::Player);
                else if (idx == 1) {
                    addActor(CsActorType::Enemy);
                    showEnemyTypeMenu_ = false;
                } else addActor(CsActorType::FreeSprite);
                showActorMenu_ = false;
                return;
            }
            showActorMenu_ = false;
        }

        const Cutscene* cs = currentCutscene();
        if (!cs) return;
        int listTop = panelY_ + 20;
        int iy = listTop - actorScrollY_;
        for (int i = 0; i < (int)cs->actors.size(); i++) {
            if (my >= iy && my < iy+18) {
                selectedActor_ = i; selectedEvent_ = -1; propsMode_ = CsPropsMode::Actor;
                return;
            }
            iy += 18;
        }
        return;
    }
}

void CutsceneEditor::handlePropsPanelClick(int mx, int my) {
    // Enemy type submenu
    if (showEnemyTypeMenu_) {
        int n = 6, mh = 18, mw = 80;
        int ax = propsX_ + propsW_ - 84;
        int ay = panelY_ - n*mh - 4;
        if (mx >= ax && mx < ax+mw && my >= ay && my < ay+n*mh+4) {
            int ci = (my - ay - 2) / mh;
            Cutscene* cs = current();
            if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size()) {
                cs->actors[selectedActor_].enemyType = (CsEnemyType)ci;
            }
            showEnemyTypeMenu_ = false; return;
        }
        showEnemyTypeMenu_ = false;
    }

    // Ease submenu
    if (showEaseMenu_) {
        int n = 5, mh = 18, mw = 90;
        int ax = propsX_ + propsW_ - 96;
        int ay = panelY_ - n*mh - 4;
        if (mx >= ax && mx < ax+mw && my >= ay && my < ay+n*mh+4) {
            int ci = (my - ay - 2) / mh;
            Cutscene* cs = current();
            if (cs && selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size())
                cs->events[selectedEvent_].ease = (CsEase)ci;
            showEaseMenu_ = false; return;
        }
        showEaseMenu_ = false;
    }
}

void CutsceneEditor::handleTextInput(const char* text) {
    if (!editActive_) return;
    int len = (int)strlen(editBuf_);
    int tlen = (int)strlen(text);
    if (len + tlen < (int)sizeof(editBuf_)-1) {
        strcat(editBuf_, text);
    }
    editBlinkT_ = 0;
}

void CutsceneEditor::handleKeyDown(SDL_Keycode sym, SDL_Keymod mod) {
    if (editActive_) {
        switch (sym) {
            case SDLK_BACKSPACE: {
                int len = (int)strlen(editBuf_);
                if (len > 0) editBuf_[len-1] = '\0';
                editBlinkT_ = 0;
                break;
            }
            case SDLK_RETURN: case SDLK_KP_ENTER: commitEdit(); break;
            case SDLK_ESCAPE: cancelEdit(); break;
            default: break;
        }
        return;
    }

    // Non-editing shortcuts
    if (selectedEvent_ < 0) return;
    Cutscene* cs = current();
    if (!cs || selectedEvent_ >= (int)cs->events.size()) return;
    CsEvent& ev = cs->events[selectedEvent_];

    float step = (mod & KMOD_SHIFT) ? 10.0f : 1.0f;
    if (mod & KMOD_CTRL) step = 0.1f;

    switch (sym) {
        case SDLK_LEFT:
            ev.startTime = std::max(0.0f, ev.startTime - 0.1f * step);
            recomputePreview(); break;
        case SDLK_RIGHT:
            ev.startTime += 0.1f * step;
            recomputePreview(); break;
        case SDLK_UP:
            if (ev.type == CsEventType::Move || ev.type == CsEventType::CameraMove) ev.toY -= step;
            else if (ev.type == CsEventType::Rotate) ev.toRot -= step * 5.0f;
            else if (ev.type == CsEventType::Alpha) ev.toAlpha = std::min(1.f, ev.toAlpha + 0.05f);
            else if (ev.type == CsEventType::CameraZoom) ev.toZoom += 0.05f;
            else if (ev.type == CsEventType::CameraShake) ev.shakeStrength += step;
            recomputePreview(); break;
        case SDLK_DOWN:
            if (ev.type == CsEventType::Move || ev.type == CsEventType::CameraMove) ev.toY += step;
            else if (ev.type == CsEventType::Rotate) ev.toRot += step * 5.0f;
            else if (ev.type == CsEventType::Alpha) ev.toAlpha = std::max(0.f, ev.toAlpha - 0.05f);
            else if (ev.type == CsEventType::CameraZoom) ev.toZoom = std::max(0.1f, ev.toZoom - 0.05f);
            else if (ev.type == CsEventType::CameraShake) ev.shakeStrength = std::max(0.f, ev.shakeStrength - step);
            recomputePreview(); break;
        case SDLK_PAGEUP:
            ev.ease = (CsEase)(((int)ev.ease + 1) % 5); break;
        case SDLK_PAGEDOWN:
            ev.ease = (CsEase)(((int)ev.ease + 4) % 5); break;
        case SDLK_DELETE:
            deleteEvent(selectedEvent_); break;
        default: break;
    }
}

void CutsceneEditor::handleEvent(SDL_Event& e, float /*editorZoom*/, float /*camWorldX*/, float /*camWorldY*/) {
    if (!active_) return;
    computeLayout(screenW_, panelY_);

    if (e.type == SDL_MOUSEBUTTONDOWN) {
        int mx = e.button.x, my = e.button.y;
        bool right = (e.button.button == SDL_BUTTON_RIGHT);

        // Commit edit on outside click
        if (editActive_) {
            commitEdit();
        }

        // Dismiss menus on outside click
        if (showEventMenu_) {
            int n = (int)CsEventType::COUNT;
            int mh = 17, mw = 110;
            int ax = timelineX_ + timelineW_ - 116;
            int ay = panelY_ - n*mh - 4;
            if (ay < 0) ay = 0;
            if (mx >= ax && mx < ax+mw && my >= ay && my < ay+n*mh+4) {
                int ci = (my - ay - 2) / mh;
                if (ci >= 0 && ci < n) {
                    uint32_t actorId = 0;
                    const Cutscene* cs = currentCutscene();
                    if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                        actorId = cs->actors[selectedActor_].id;
                    addEvent((CsEventType)ci, actorId, scrubTime_);
                }
                showEventMenu_ = false;
                return;
            }
            showEventMenu_ = false;
        }

        if (my < panelY_) return;

        handleListClick(mx, my);
        handlePropsPanelClick(mx, my);
        if (mx >= timelineX_ && mx < timelineX_+timelineW_)
            handleTimelineClick(mx, my, right);
    }

    if (e.type == SDL_MOUSEBUTTONUP) handleTimelineRelease();
    if (e.type == SDL_MOUSEMOTION) handleTimelineMotion(e.motion.x, e.motion.y);

    if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point mp; SDL_GetMouseState(&mp.x, &mp.y);
        if (mp.x >= timelineX_ && mp.x < timelineX_+timelineW_ && mp.y >= panelY_)
            timelineStart_ = std::max(0.0f, timelineStart_ - e.wheel.y * 0.5f);
        if (mp.x >= propsX_ && mp.x < propsX_+propsW_ && mp.y >= panelY_) {
            propsScrollY_ = std::max(0, propsScrollY_ - e.wheel.y * 20);
        }
    }

    if (e.type == SDL_TEXTINPUT) handleTextInput(e.text.text);

    if (e.type == SDL_KEYDOWN) {
        int mx2, my2; SDL_GetMouseState(&mx2, &my2);
        handleKeyDown(e.key.keysym.sym, (SDL_Keymod)e.key.keysym.mod);
    }
}
