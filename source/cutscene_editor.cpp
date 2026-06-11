#include "cutscene_editor.h"
#include "assets.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

void CutsceneEditor::init(SDL_Renderer* r, int screenW, int screenH, UI::Context* ui) {
    r_       = r;
    ui_      = ui;
    screenW_ = screenW;
    (void)screenH;
}

// ---- Layout ----

void CutsceneEditor::computeLayout(int screenW, int panelY) {
    panelY_   = panelY;
    panelH_   = CS_EDITOR_PANEL_H;
    listX_    = 0;
    listW_    = CS_LIST_W;
    actorX_   = listX_ + listW_ + 1;
    actorW_   = CS_ACTOR_W;
    propsX_   = screenW - CS_PROPS_W;
    propsW_   = CS_PROPS_W;
    timelineX_ = actorX_ + actorW_ + 1;
    timelineW_ = propsX_ - timelineX_ - 1;
}

// ---- Drawing primitives (delegated to UI::Context) ----

void CutsceneEditor::fillRect(int x, int y, int w, int h, SDL_Color c) {
    if (!ui_) return;
    ui_->drawPanel(x, y, w, h, c, {0,0,0,0});
}

void CutsceneEditor::drawRect(int x, int y, int w, int h, SDL_Color c) {
    if (!ui_) return;
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(r_, &rect);
}

void CutsceneEditor::drawLine(int x0, int y0, int x1, int y1, SDL_Color c) {
    if (!ui_) return;
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r_, x0, y0, x1, y1);
}

void CutsceneEditor::drawText(const char* text, int x, int y, SDL_Color c, int size) {
    if (!ui_) return;
    ui_->drawText(text, x, y, size, c);
}

void CutsceneEditor::drawTextRight(const char* text, int x, int y, int w, SDL_Color c, int size) {
    if (!ui_) return;
    ui_->drawTextRight(text, x, y + (size < 13 ? 2 : 0), size, c);
    (void)w;
}

void CutsceneEditor::drawBevel(int x, int y, int w, int h, bool raised) {
    if (!ui_) return;
    ui_->drawWin98Bevel(x, y, w, h, raised);
}

// ---- Layout helpers ----

float CutsceneEditor::pxToTime(int px, int timelineX) const {
    return timelineStart_ + (float)(px - timelineX) / timelineScale_;
}

int CutsceneEditor::timeToPx(float t, int timelineX) const {
    return timelineX + (int)((t - timelineStart_) * timelineScale_);
}

static constexpr int TL_HEADER_H = 22;
static constexpr int TL_ROW_H    = 22;

int CutsceneEditor::actorRowY(int actorIdx, int timelineY) const {
    return timelineY + TL_HEADER_H + actorIdx * TL_ROW_H;
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
        default:                          return {150, 150, 150, 255};
    }
}

// ---- Helpers ----

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

// ---- Preview computation ----

void CutsceneEditor::recomputePreview() {
    const Cutscene* cs = currentCutscene();
    if (!cs) { previewStates_.clear(); return; }

    previewStates_.resize(cs->actors.size());
    for (int i = 0; i < (int)cs->actors.size(); i++) {
        const auto& a = cs->actors[i];
        auto& s = previewStates_[i];
        s.x       = a.startX;     s.y       = a.startY;
        s.rot     = a.startRot;   s.scaleX  = a.startScaleX;
        s.scaleY  = a.startScaleY; s.alpha  = a.startAlpha;
        s.visible = a.startVisible; s.frame = 0;
        s.flashAmt = 0;
    }

    auto lerp = [](float a, float b, float t){ return a + (b-a)*t; };
    auto ease  = [](float t, CsEase e) -> float {
        t = std::max(0.0f, std::min(1.0f, t));
        switch (e) {
            case CsEase::EaseIn:    return t*t;
            case CsEase::EaseOut:   return 1-(1-t)*(1-t);
            case CsEase::EaseInOut: return t<0.5f ? 2*t*t : 1-2*(1-t)*(1-t);
            case CsEase::Instant:   return t>=1.f?1.f:0.f;
            default:                return t;
        }
    };

    for (const auto& ev : cs->events) {
        float end = ev.startTime + std::max(ev.duration, 0.001f);
        if (scrubTime_ < ev.startTime) continue;

        float localT = (scrubTime_ >= end) ? 1.0f :
                       (scrubTime_ - ev.startTime) / (end - ev.startTime);
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
            default: break;
        }
    }
}

// ---- Add/Delete operations ----

void CutsceneEditor::addCutscene() {
    if (!lib_) return;
    Cutscene cs;
    cs.id = "cutscene_" + std::to_string(nextCsId_++);
    cs.blockInput = true;
    lib_->cutscenes.push_back(std::move(cs));
    selectedCutscene_ = (int)lib_->cutscenes.size() - 1;
    selectedActor_ = selectedEvent_ = -1;
}

void CutsceneEditor::deleteCutscene(int idx) {
    if (!lib_ || idx < 0 || idx >= (int)lib_->cutscenes.size()) return;
    lib_->cutscenes.erase(lib_->cutscenes.begin() + idx);
    selectedCutscene_ = std::min(selectedCutscene_, (int)lib_->cutscenes.size()-1);
    selectedActor_ = selectedEvent_ = -1;
}

void CutsceneEditor::addActor(CsActorType type, CsEnemyType enemyType) {
    Cutscene* cs = current();
    if (!cs) return;
    CsActor a;
    a.id   = nextActorId_++;
    a.type = type;
    a.enemyType = enemyType;
    switch (type) {
        case CsActorType::Player:     a.name = "Player";   break;
        case CsActorType::Enemy:      a.name = "Enemy";    break;
        case CsActorType::FreeSprite: a.name = "Sprite";   break;
    }
    a.startX = 320; a.startY = 240;
    cs->actors.push_back(a);
    selectedActor_ = (int)cs->actors.size() - 1;
    recomputePreview();
}

void CutsceneEditor::deleteActor(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->actors.size()) return;
    uint32_t rmId = cs->actors[idx].id;
    cs->actors.erase(cs->actors.begin() + idx);
    // Remove all events for this actor
    cs->events.erase(std::remove_if(cs->events.begin(), cs->events.end(),
        [rmId](const CsEvent& e){ return e.actorId == rmId; }), cs->events.end());
    selectedActor_ = std::min(selectedActor_, (int)cs->actors.size()-1);
    selectedEvent_ = -1;
    recomputePreview();
}

void CutsceneEditor::addEvent(CsEventType type, uint32_t actorId, float atTime) {
    Cutscene* cs = current();
    if (!cs) return;
    CsEvent ev;
    ev.actorId   = actorId;
    ev.type      = type;
    ev.startTime = atTime;
    ev.duration  = (type == CsEventType::Wait || type == CsEventType::PlaySFX
                    || type == CsEventType::SpawnExplosion) ? 0.0f : 1.0f;
    ev.ease      = CsEase::EaseInOut;
    // Set sensible defaults
    switch (type) {
        case CsEventType::Alpha:      ev.fromAlpha = 0; ev.toAlpha = 1; break;
        case CsEventType::CameraZoom: ev.fromZoom = 1; ev.toZoom = 1.5f; break;
        case CsEventType::CameraShake: ev.shakeStrength = 8; ev.duration = 0.5f; break;
        case CsEventType::ScreenFade:  ev.fadeToBlack = true; break;
        case CsEventType::CinematicBars: ev.showBars = true; break;
        default: break;
    }
    cs->events.push_back(ev);
    selectedEvent_ = (int)cs->events.size() - 1;
}

void CutsceneEditor::deleteEvent(int idx) {
    Cutscene* cs = current();
    if (!cs || idx < 0 || idx >= (int)cs->events.size()) return;
    cs->events.erase(cs->events.begin() + idx);
    selectedEvent_ = std::min(selectedEvent_, (int)cs->events.size()-1);
    recomputePreview();
}

void CutsceneEditor::addDialogSeq() {
    Cutscene* cs = current();
    if (!cs) return;
    CsDialogSeq seq;
    seq.id = "dialog_" + std::to_string(cs->dialogs.size());
    cs->dialogs.push_back(seq);
    selectedDialogSeq_ = (int)cs->dialogs.size()-1;
}

void CutsceneEditor::addDialogLine(int seqIdx) {
    Cutscene* cs = current();
    if (!cs || seqIdx < 0 || seqIdx >= (int)cs->dialogs.size()) return;
    CsDialogLine line;
    line.character = "Character";
    line.text      = "Enter dialog here.";
    line.portraitLeft = true;
    cs->dialogs[seqIdx].lines.push_back(line);
    selectedDialogLine_ = (int)cs->dialogs[seqIdx].lines.size()-1;
}

// ---- Sub-panel rendering ----

void CutsceneEditor::renderCutsceneList(int x, int y, int w, int h) {
    const SDL_Color bgDark   = UI::Color::BgDark;
    const SDL_Color bgSel    = UI::Color::DimCyan;
    const SDL_Color colText  = UI::Color::Gray;
    const SDL_Color colHead  = UI::Color::Cyan;

    fillRect(x, y, w, h, bgDark);
    drawBevel(x, y, w, h, false);

    drawText("CUTSCENES", x+4, y+5, colHead, 11);

    int itemY = y + headerH_;
    if (lib_) {
        for (int i = 0; i < (int)lib_->cutscenes.size(); i++) {
            bool sel = (i == selectedCutscene_);
            if (sel) fillRect(x+1, itemY, w-2, rowH_-1, bgSel);
            drawText(lib_->cutscenes[i].id.c_str(), x+4, itemY+4, colText, 11);
            itemY += rowH_;
        }
    }

    // + button at bottom
    int btnY = y + h - 20;
    if (ui_) {
        ui_->win98Button(100, "+ Add", x+2, btnY, 40, 16, false);
        if (selectedCutscene_ >= 0) {
            ui_->win98Button(101, "Del", x+46, btnY, 40, 16, false);
        }
    }
}

void CutsceneEditor::renderActorList(int x, int y, int w, int h) {
    const SDL_Color bgDark   = UI::Color::BgDark;
    const SDL_Color bgSel    = UI::Color::Green;
    const SDL_Color colText  = UI::Color::Gray;
    const SDL_Color colHead  = UI::Color::Cyan;
    const SDL_Color colType  = UI::Color::Blue;

    fillRect(x, y, w, h, bgDark);
    drawBevel(x, y, w, h, false);

    drawText("ACTORS", x+4, y+5, colHead, 11);

    static const char* typeShort[] = { "PLR", "ENM", "SPR" };

    const Cutscene* cs = currentCutscene();
    int itemY = y + headerH_;
    if (cs) {
        for (int i = 0; i < (int)cs->actors.size(); i++) {
            bool sel = (i == selectedActor_);
            if (sel) fillRect(x+1, itemY, w-2, rowH_-1, bgSel);
            int ti = (int)cs->actors[i].type;
            drawText(typeShort[ti < 3 ? ti : 2], x+3, itemY+4, colType, 11);
            drawText(cs->actors[i].name.c_str(), x+32, itemY+4, colText, 11);
            itemY += rowH_;
        }
    }

    // + button at bottom
    int btnY = y + h - 20;
    if (ui_) {
        ui_->win98Button(102, "+ Actor", x+2, btnY, 60, 16, false);
        if (selectedActor_ >= 0) {
            ui_->win98Button(103, "Del", x+66, btnY, 40, 16, false);
        }
    }

    // Actor type submenu
    if (showActorMenu_) {
        int mx = x+2, my = btnY - 72;
        fillRect(mx, my, 90, 70, UI::Color::PanelBg);
        drawBevel(mx, my, 90, 70, true);
        const char* opts[] = {"Player","Enemy","Sprite"};
        for (int i = 0; i < 3; i++)
            drawText(opts[i], mx+6, my+4+i*22, colText, 11);
    }
}

void CutsceneEditor::renderTimeline(int x, int y, int w, int h) {
    const SDL_Color bgMain  = UI::Color::BgDark;
    const SDL_Color bgRow   = {28, 28, 28, 255};
    const SDL_Color bgRowAlt= {24, 24, 24, 255};
    const SDL_Color colGrid = {45, 45, 45, 255};
    const SDL_Color colText = UI::Color::Gray;
    const SDL_Color colScrub= UI::Color::Yellow;
    const SDL_Color colSel  = UI::Color::White;

    fillRect(x, y, w, h, bgMain);
    drawBevel(x, y, w, h, false);

    // Toolbar row: play button, time display, zoom
    int toolY = y + 2;
    fillRect(x, toolY, w, TL_HEADER_H-2, {35,35,35,255});

    // Play/pause
    SDL_Color playCol = playing_ ? UI::Color::Green : UI::Color::Gray;
    if (ui_) {
        ui_->win98Button(104, playing_ ? "||" : "|>", x+4, toolY+3, 16, 16, false);
    }

    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%.2fs", scrubTime_);
    drawText(timeBuf, x+24, toolY+4, colText, 11);

    // Zoom -/+
    if (ui_) {
        ui_->win98Button(105, "-", x+w-38, toolY+3, 16, 16, false);
        ui_->win98Button(106, "+", x+w-20, toolY+3, 16, 16, false);
    }

    // Content area (below header)
    int contentY = y + TL_HEADER_H;
    int contentH = h - TL_HEADER_H;

    // Time ruler
    fillRect(x, contentY, w, 16, {40,40,40,255});
    float secStep = 1.0f;
    if (timelineScale_ < 20) secStep = 5.0f;
    if (timelineScale_ < 8)  secStep = 10.0f;
    float firstTick = ceilf(timelineStart_ / secStep) * secStep;
    for (float t = firstTick; t < timelineStart_ + w/timelineScale_; t += secStep) {
        int px = timeToPx(t, x);
        if (px < x || px > x+w) continue;
        drawLine(px, contentY, px, contentY+16, colGrid);
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%.0fs", (double)t);
        drawText(tbuf, px+2, contentY+2, {100,100,100,255}, 11);
    }
    contentY += 16;
    contentH  -= 16;

    // Actor rows
    const Cutscene* cs = currentCutscene();
    if (cs) {
        int nActors = (int)cs->actors.size();
        // Global row (actor id=0)
        fillRect(x, contentY, w, TL_ROW_H, {22,22,22,255});
        drawText("[global]", x+2, contentY+4, {100,120,100,255}, 11);
        // Grid line
        drawLine(x, contentY+TL_ROW_H-1, x+w, contentY+TL_ROW_H-1, colGrid);

        for (int i = 0; i < nActors; i++) {
            int ry = contentY + TL_ROW_H + i * TL_ROW_H;
            SDL_Color rowBg = (i % 2 == 0) ? bgRow : bgRowAlt;
            fillRect(x, ry, w, TL_ROW_H, rowBg);
            drawLine(x, ry+TL_ROW_H-1, x+w, ry+TL_ROW_H-1, colGrid);
        }

        // Event blocks
        for (int ei = 0; ei < (int)cs->events.size(); ei++) {
            const auto& ev = cs->events[ei];
            // Find row
            int row = -1; // -1 = global row
            for (int i = 0; i < nActors; i++)
                if (cs->actors[i].id == ev.actorId) { row = i; break; }

            int ry = (row < 0)
                ? contentY + 2
                : contentY + TL_ROW_H + row * TL_ROW_H + 2;
            int rh = TL_ROW_H - 4;

            int px0 = timeToPx(ev.startTime, x);
            int px1 = timeToPx(ev.startTime + std::max(ev.duration, 0.04f), x);
            int bw  = std::max(px1 - px0, 4);
            if (px0 > x+w || px1 < x) continue;
            px0 = std::max(px0, x);
            bw  = std::min(bw, x+w-px0);

            SDL_Color bc = eventColor(ev.type);
            bool isSel = (ei == selectedEvent_);
            SDL_Color fill = isSel ? SDL_Color{(Uint8)std::min(255,bc.r+50),
                                               (Uint8)std::min(255,bc.g+50),
                                               (Uint8)std::min(255,bc.b+50),255} : bc;
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r_, fill.r, fill.g, fill.b, 200);
            SDL_Rect er = {px0, ry, bw, rh};
            SDL_RenderFillRect(r_, &er);
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
            if (isSel) { drawRect(px0, ry, bw, rh, colSel); }

            // Label
            if (bw > 24) {
                SDL_Color tc = {0,0,0,255};
                drawText(csEventTypeName(ev.type), px0+2, ry+3, tc, 11);
            }

            // Resize handle (right edge)
            fillRect(px0+bw-4, ry, 4, rh, {255,255,255,80});
        }
    }

    // Vertical grid lines (seconds)
    for (float t = firstTick; t < timelineStart_ + w/timelineScale_; t += secStep) {
        int px = timeToPx(t, x);
        if (px >= x && px <= x+w)
            drawLine(px, contentY, px, contentY+contentH, colGrid);
    }

    // Scrubber line
    int spx = timeToPx(scrubTime_, x);
    if (spx >= x && spx <= x+w) {
        drawLine(spx, y+TL_HEADER_H, spx, y+h, colScrub);
        // Scrubber head
        fillRect(spx-5, y+TL_HEADER_H, 10, 8, colScrub);
    }

    // "Add event" button. Always available: the new event attaches to the
    // selected actor, or to the global (camera/screen) row if none is selected.
    if (ui_) {
        int addBtnX = x+w-80, addBtnY = y+h-20;
        ui_->win98Button(107, "+ Event", addBtnX, addBtnY, 76, 16, false);
    }
}

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

void CutsceneEditor::renderPropsPanel(int x, int y, int w, int h) {
    const SDL_Color bgPanel  = UI::Color::PanelBg;
    const SDL_Color colHead  = UI::Color::Blue;
    const SDL_Color colLabel = UI::Color::Gray;
    const SDL_Color colValue = UI::Color::White;
    const SDL_Color colField = {20, 20, 20, 255};

    fillRect(x, y, w, h, bgPanel);
    drawBevel(x, y, w, h, false);

    int cy = y + 4;
    auto label = [&](const char* k, const char* v){
        drawText(k, x+4, cy, colLabel, 11);
        drawText(v, x+90, cy, colValue, 11);
        cy += 16;
    };
    auto sep = [&](){ drawLine(x+4, cy+2, x+w-4, cy+2, {60,60,60,255}); cy += 6; };
    auto field = [&](const char* k, const char* v){
        drawText(k, x+4, cy, colLabel, 11);
        fillRect(x+90, cy-1, w-94, 14, colField);
        drawBevel(x+90, cy-1, w-94, 14, false);
        drawText(v, x+93, cy, colValue, 11);
        cy += 17;
    };

    const Cutscene* cs = currentCutscene();

    if (!cs) {
        drawText("No cutscene selected", x+4, cy, colLabel, 11);
        return;
    }

    // Cutscene properties
    drawText("SCENE", x+4, cy, colHead, 11);
    char idBuf[128]; snprintf(idBuf, sizeof(idBuf), "%s", cs->id.c_str());
    field("ID:", idBuf);
    char durBuf[32]; snprintf(durBuf, sizeof(durBuf), "%.2fs", cs->totalDuration());
    label("Duration:", durBuf);
    label("Block:", cs->blockInput ? "Yes" : "No");
    sep();

    // Selected event properties
    if (selectedEvent_ >= 0 && selectedEvent_ < (int)cs->events.size()) {
        const CsEvent& ev = cs->events[selectedEvent_];
        drawText("EVENT", x+4, cy, colHead, 11); cy+=16;

        label("Type:", csEventTypeName(ev.type));
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "%.3f", (double)ev.startTime);
        field("Start (s):", tbuf);
        snprintf(tbuf, sizeof(tbuf), "%.3f", (double)ev.duration);
        field("Dur (s):", tbuf);
        label("Ease:", easeName(ev.ease));
        sep();

        // Type-specific fields
        switch (ev.type) {
            case CsEventType::Move:
            case CsEventType::CameraMove: {
                char buf[64];
                snprintf(buf,sizeof(buf),"%.0f, %.0f", (double)ev.fromX, (double)ev.fromY);
                field("From:", buf);
                snprintf(buf,sizeof(buf),"%.0f, %.0f", (double)ev.toX, (double)ev.toY);
                field("To:", buf);
                break;
            }
            case CsEventType::Rotate: {
                char buf[32];
                snprintf(buf,sizeof(buf),"%.1f deg", (double)ev.fromRot); field("From:", buf);
                snprintf(buf,sizeof(buf),"%.1f deg", (double)ev.toRot);   field("To:", buf);
                break;
            }
            case CsEventType::Scale: {
                char buf[32];
                snprintf(buf,sizeof(buf),"%.2f, %.2f",
                         (double)ev.fromScaleX, (double)ev.fromScaleY); field("From:", buf);
                snprintf(buf,sizeof(buf),"%.2f, %.2f",
                         (double)ev.toScaleX, (double)ev.toScaleY); field("To:", buf);
                break;
            }
            case CsEventType::Alpha: {
                char buf[16];
                snprintf(buf,sizeof(buf),"%.2f", (double)ev.fromAlpha); field("From:", buf);
                snprintf(buf,sizeof(buf),"%.2f", (double)ev.toAlpha);   field("To:", buf);
                break;
            }
            case CsEventType::Flash: {
                char buf[32];
                snprintf(buf,sizeof(buf),"%.0f,%.0f,%.0f",
                         (double)ev.flashR, (double)ev.flashG, (double)ev.flashB);
                field("RGB:", buf);
                break;
            }
            case CsEventType::CameraZoom: {
                char buf[16];
                snprintf(buf,sizeof(buf),"%.2f", (double)ev.fromZoom); field("From:", buf);
                snprintf(buf,sizeof(buf),"%.2f", (double)ev.toZoom);   field("To:", buf);
                break;
            }
            case CsEventType::CameraShake: {
                char buf[16];
                snprintf(buf,sizeof(buf),"%.1f", (double)ev.shakeStrength);
                field("Strength:", buf);
                break;
            }
            case CsEventType::ScreenFade:
                label("Direction:", ev.fadeToBlack ? "To Black" : "From Black");
                break;
            case CsEventType::CinematicBars:
                label("Show:", ev.showBars ? "Show" : "Hide");
                break;
            case CsEventType::Dialog:
                field("Dialog ID:", ev.dialogId.c_str());
                break;
            case CsEventType::PlaySFX:
                field("SFX Path:", ev.sfxPath.c_str());
                break;
            case CsEventType::SpawnExplosion: {
                char buf[32];
                snprintf(buf,sizeof(buf),"%.0f, %.0f", (double)ev.explX, (double)ev.explY);
                field("At:", buf);
                break;
            }
            default: break;
        }
        sep();
        // Delete button
        int delY = y + h - 20;
        if (ui_) {
            ui_->win98Button(108, "Del Evt", x+4, delY, 50, 16, false);
        }
        return;
    }

    // Selected actor properties
    if (selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size()) {
        const CsActor& a = cs->actors[selectedActor_];
        drawText("ACTOR", x+4, cy, colHead, 11); cy+=16;
        field("Name:", a.name.c_str());
        static const char* typeNames[] = {"Player","Enemy","Sprite"};
        label("Type:", typeNames[(int)a.type < 3 ? (int)a.type : 0]);
        if (a.type == CsActorType::Enemy) {
            static const char* enames[] = {"Melee","Shooter","Brute","Scout","Sniper","Gunner"};
            label("Kind:", enames[(int)a.enemyType < 6 ? (int)a.enemyType : 0]);
        }
        if (a.type == CsActorType::FreeSprite)
            field("Sprite:", a.spritePath.empty() ? "(none)" : a.spritePath.c_str());
        char buf[32];
        snprintf(buf,sizeof(buf),"%.0f, %.0f", (double)a.startX, (double)a.startY);
        field("Start Pos:", buf);
        snprintf(buf,sizeof(buf),"%.1f", (double)a.startRot);
        field("Start Rot:", buf);
        snprintf(buf,sizeof(buf),"%.2f", (double)a.startAlpha);
        field("Start Alpha:", buf);
        sep();
        int delY = y + h - 20;
        if (ui_) {
            ui_->win98Button(109, "Del Actor", x+4, delY, 60, 16, false);
        }
        return;
    }

    // Dialogs listing
    if (cs->dialogs.empty()) {
        drawText("No dialogs yet.", x+4, cy, colLabel, 11); cy+=16;
    } else {
        drawText("DIALOGS", x+4, cy, colHead, 11); cy+=16;
        for (int i = 0; i < (int)cs->dialogs.size(); i++) {
            bool sel = (i == selectedDialogSeq_);
            if (sel) fillRect(x+2, cy-1, w-4, 14, {60,80,120,255});
            char dbuf[64];
            snprintf(dbuf, sizeof(dbuf), "%s (%d lines)",
                     cs->dialogs[i].id.c_str(), (int)cs->dialogs[i].lines.size());
            drawText(dbuf, x+4, cy, colValue, 11);
            cy += 15;
        }
    }
    int addDlgY = y + h - 40;
    if (ui_) {
        ui_->win98Button(110, "+ Dialog", x+4, addDlgY, 80, 16, false);
        if (selectedDialogSeq_ >= 0) {
            ui_->win98Button(111, "+ Line", x+4, addDlgY+20, 80, 16, false);
        }
    }
}

// ---- Event add submenu ----

static const char* s_eventMenuOpts[] = {
    "Move","Rotate","Scale","Alpha","Flash",
    "Wait","Dialog","SFX","Explosion",
    "Cam Move","Cam Zoom","Cam Shake",
    "Screen Fade","Cine Bars","Set Visible","Set Frame",
    nullptr
};

// ---- Main render ----

void CutsceneEditor::render(SDL_Renderer* r, int screenW, int panelY) {
    r_ = r;
    computeLayout(screenW, panelY);

    // Background
    fillRect(0, panelY_, screenW, panelH_, {18,18,18,255});
    drawLine(0, panelY_, screenW, panelY_, {80,80,80,255});

    renderCutsceneList(listX_, panelY_+1, listW_, panelH_-1);
    renderActorList(actorX_, panelY_+1, actorW_, panelH_-1);
    renderTimeline(timelineX_, panelY_+1, timelineW_, panelH_-1);
    renderPropsPanel(propsX_, panelY_+1, propsW_, panelH_-1);

    // Event add submenu (shown above + Event button)
    if (showEventMenu_) {
        int menuX = timelineX_ + timelineW_ - 110;
        int menuItemH = 18;
        int numOpts = (int)CsEventType::COUNT;
        int menuY = panelY_ - numOpts * menuItemH - 4;
        fillRect(menuX, menuY, 106, numOpts * menuItemH + 4, {40,40,40,255});
        drawBevel(menuX, menuY, 106, numOpts * menuItemH + 4, true);
        for (int i = 0; i < numOpts; i++) {
            SDL_Color ec = eventColor((CsEventType)i);
            fillRect(menuX+2, menuY+2+i*menuItemH, 6, 14, ec);
            drawText(s_eventMenuOpts[i], menuX+12, menuY+4+i*menuItemH,
                     {200,200,200,255}, 11);
        }
    }
}

// ---- Update ----

void CutsceneEditor::update(float dt) {
    if (!active_) return;
    if (playing_) {
        scrubTime_ += dt;
        const Cutscene* cs = currentCutscene();
        float dur = cs ? cs->totalDuration() : 10.0f;
        if (dur < 0.1f) dur = 10.0f;
        if (scrubTime_ > dur) { scrubTime_ = 0; playing_ = false; }
        // Keep scrubber visible
        if (scrubTime_ > timelineStart_ + timelineW_/timelineScale_ - 1.0f)
            timelineStart_ = scrubTime_ - 1.0f;
        recomputePreview();
    }
}

// ---- Input ----

void CutsceneEditor::handleTimelineClick(int mx, int my, bool rightClick) {
    if (!lib_) return;
    computeLayout(screenW_, panelY_);

    int tx = timelineX_, th = panelH_ - 1;
    if (mx < tx || mx > tx + timelineW_) return;
    if (my < panelY_ || my > panelY_ + th) return;

    // Play/pause button
    int toolY = panelY_ + 2;
    SDL_Rect playBtn = { tx+4, toolY+3, 16, 16 };
    if (mx >= playBtn.x && mx <= playBtn.x+playBtn.w &&
        my >= playBtn.y && my <= playBtn.y+playBtn.h) {
        playing_ = !playing_;
        if (playing_ && scrubTime_ < 0) scrubTime_ = 0;
        return;
    }

    // Zoom buttons
    SDL_Rect zoomOut = { tx+timelineW_-38, toolY+3, 16, 16 };
    SDL_Rect zoomIn  = { tx+timelineW_-20, toolY+3, 16, 16 };
    if (mx >= zoomOut.x && mx <= zoomOut.x+16 && my >= zoomOut.y && my <= zoomOut.y+16) {
        timelineScale_ = std::max(10.0f, timelineScale_ * 0.8f); recomputePreview(); return;
    }
    if (mx >= zoomIn.x && mx <= zoomIn.x+16 && my >= zoomIn.y && my <= zoomIn.y+16) {
        timelineScale_ = std::min(400.0f, timelineScale_ * 1.25f); recomputePreview(); return;
    }

    // Add event button
    int addBtnY = panelY_ + th - 20;
    SDL_Rect addBtn = { tx+timelineW_-80, addBtnY, 76, 16 };
    if (mx >= addBtn.x && mx <= addBtn.x+addBtn.w &&
        my >= addBtn.y && my <= addBtn.y+addBtn.h) {
        showEventMenu_ = !showEventMenu_;
        return;
    }

    // Scrubber header area
    int contentY = panelY_ + TL_HEADER_H + 16 + 1;
    int rulerY   = panelY_ + TL_HEADER_H;
    if (my >= rulerY && my < contentY) {
        scrubTime_ = std::max(0.0f, pxToTime(mx, tx));
        draggingScrub_ = true;
        playing_ = false;
        recomputePreview();
        return;
    }

    const Cutscene* cs = currentCutscene();
    if (!cs) return;

    // Click on event block?
    for (int ei = (int)cs->events.size()-1; ei >= 0; ei--) {
        const auto& ev = cs->events[ei];
        int row = -1;
        for (int i = 0; i < (int)cs->actors.size(); i++)
            if (cs->actors[i].id == ev.actorId) { row = i; break; }

        int ry = (row < 0)
            ? contentY + 2
            : contentY + TL_ROW_H + row * TL_ROW_H + 2;
        int rh = TL_ROW_H - 4;

        int px0 = timeToPx(ev.startTime, tx);
        int px1 = timeToPx(ev.startTime + std::max(ev.duration, 0.04f), tx);

        if (mx >= px0 && mx <= px1 && my >= ry && my <= ry+rh) {
            if (rightClick) {
                deleteEvent(ei);
                return;
            }
            selectedEvent_ = ei;
            // Resize handle?
            if (mx >= px1-4) {
                resizingEvent_  = true;
                resizeOrigDur_  = cs->events[ei].duration;
                resizeStartPx_  = mx;
            } else {
                draggingEvent_  = true;
                dragEventOrigT_ = cs->events[ei].startTime;
                dragStartPx_    = mx;
            }
            return;
        }
    }

    // Click in empty area = move scrubber
    scrubTime_ = std::max(0.0f, pxToTime(mx, tx));
    playing_ = false;
    recomputePreview();
}

void CutsceneEditor::handleTimelineMotion(int mx, int /*my*/) {
    if (!lib_) return;
    Cutscene* cs = current();

    if (draggingScrub_) {
        scrubTime_ = std::max(0.0f, pxToTime(mx, timelineX_));
        recomputePreview();
        return;
    }
    if (draggingEvent_ && cs && selectedEvent_ >= 0 &&
        selectedEvent_ < (int)cs->events.size()) {
        float dt = (mx - dragStartPx_) / timelineScale_;
        cs->events[selectedEvent_].startTime =
            std::max(0.0f, dragEventOrigT_ + dt);
        recomputePreview();
        return;
    }
    if (resizingEvent_ && cs && selectedEvent_ >= 0 &&
        selectedEvent_ < (int)cs->events.size()) {
        float ddt = (mx - resizeStartPx_) / timelineScale_;
        cs->events[selectedEvent_].duration =
            std::max(0.001f, resizeOrigDur_ + ddt);
        recomputePreview();
        return;
    }
}

void CutsceneEditor::handleTimelineRelease() {
    draggingEvent_  = false;
    resizingEvent_  = false;
    draggingScrub_  = false;
}

void CutsceneEditor::handleListClick(int mx, int my,
                                      int panelX, int panelY,
                                      int panelW, int panelH) {
    if (!lib_) return;

    int btnY = panelY + panelH - 20;

    // Cutscene list
    if (mx >= listX_ && mx < listX_+listW_) {
        // Add button
        if (my >= btnY && my < btnY+16 && mx < listX_+44) {
            addCutscene(); return;
        }
        if (my >= btnY && my < btnY+16 && mx >= listX_+46) {
            deleteCutscene(selectedCutscene_); return;
        }
        int itemY = panelY + headerH_;
        for (int i = 0; i < (int)lib_->cutscenes.size(); i++) {
            if (my >= itemY && my < itemY + rowH_) {
                selectedCutscene_ = i;
                selectedActor_ = selectedEvent_ = -1;
                recomputePreview();
                return;
            }
            itemY += rowH_;
        }
        return;
    }

    // Actor list
    if (mx >= actorX_ && mx < actorX_+actorW_) {
        if (my >= btnY && my < btnY+16 && mx < actorX_+64) {
            showActorMenu_ = !showActorMenu_; return;
        }
        if (my >= btnY && my < btnY+16 && mx >= actorX_+66) {
            deleteActor(selectedActor_); return;
        }
        // Actor type submenu
        if (showActorMenu_) {
            int menuY = btnY - 72;
            if (mx >= actorX_+2 && mx <= actorX_+92 &&
                my >= menuY && my < menuY+70) {
                int idx = (my - menuY) / 22;
                if (idx == 0) addActor(CsActorType::Player);
                else if (idx == 1) addActor(CsActorType::Enemy);
                else addActor(CsActorType::FreeSprite);
                showActorMenu_ = false;
                return;
            }
            showActorMenu_ = false;
        }
        const Cutscene* cs = currentCutscene();
        if (!cs) return;
        int itemY = panelY + headerH_;
        for (int i = 0; i < (int)cs->actors.size(); i++) {
            if (my >= itemY && my < itemY+rowH_) {
                selectedActor_ = i;
                selectedEvent_ = -1;
                return;
            }
            itemY += rowH_;
        }
        return;
    }

    // Props panel - dialog section
    if (mx >= propsX_) {
        Cutscene* cs = current();
        if (!cs) return;
        int addDlgY = panelY + panelH - 40;
        if (my >= addDlgY && my < addDlgY+16) {
            addDialogSeq(); return;
        }
        if (my >= addDlgY+20 && my < addDlgY+36 && selectedDialogSeq_ >= 0) {
            addDialogLine(selectedDialogSeq_); return;
        }
        // Delete event / actor buttons
        int delY = panelY + panelH - 20;
        if (selectedEvent_ >= 0 && my >= delY && my < delY+16 && mx < propsX_+58) {
            deleteEvent(selectedEvent_); return;
        }
        if (selectedActor_ >= 0 && my >= delY && my < delY+16 && mx < propsX_+68) {
            deleteActor(selectedActor_); return;
        }
        // Dialog seq selection. The dialog list is only drawn when neither an
        // event nor an actor is selected, so mirror that guard here and match
        // the exact row layout used by renderPropsPanel.
        if (selectedEvent_ < 0 && selectedActor_ < 0) {
            const Cutscene* ccs = currentCutscene();
            if (ccs && !ccs->dialogs.empty()) {
                int cy = panelY + 1 + 4;    // renderPropsPanel is drawn at y = panelY+1
                cy += 17 + 16 + 16 + 6;     // SCENE: ID field, Duration, Block, separator
                cy += 16;                   // "DIALOGS" header
                for (int i = 0; i < (int)ccs->dialogs.size(); i++) {
                    if (my >= cy-1 && my < cy+14) {
                        selectedDialogSeq_ = i; return;
                    }
                    cy += 15;
                }
            }
        }
    }
}

void CutsceneEditor::handlePropsPanelEvent(SDL_Event& e) {
    // Keyboard shortcuts for editing event fields
    // Full inline editing is complex; for now, arrow keys tweak numeric fields
    if (e.type != SDL_KEYDOWN) return;
    Cutscene* cs = current();
    if (!cs || selectedEvent_ < 0 || selectedEvent_ >= (int)cs->events.size()) return;
    CsEvent& ev = cs->events[selectedEvent_];

    float step = (e.key.keysym.mod & KMOD_SHIFT) ? 10.0f : 1.0f;
    if (e.key.keysym.mod & KMOD_CTRL) step = 0.1f;

    switch (e.key.keysym.sym) {
        case SDLK_LEFT:
            ev.startTime = std::max(0.0f, ev.startTime - 0.1f * step);
            recomputePreview(); break;
        case SDLK_RIGHT:
            ev.startTime += 0.1f * step;
            recomputePreview(); break;
        case SDLK_UP:
            if (ev.type == CsEventType::Move || ev.type == CsEventType::CameraMove)
                ev.toY -= step;
            else if (ev.type == CsEventType::Rotate)
                ev.toRot -= step * 5.0f;
            else if (ev.type == CsEventType::Alpha)
                ev.toAlpha = std::min(1.0f, ev.toAlpha + 0.05f);
            else if (ev.type == CsEventType::CameraZoom)
                ev.toZoom += 0.05f;
            else if (ev.type == CsEventType::CameraShake)
                ev.shakeStrength += step;
            recomputePreview(); break;
        case SDLK_DOWN:
            if (ev.type == CsEventType::Move || ev.type == CsEventType::CameraMove)
                ev.toY += step;
            else if (ev.type == CsEventType::Rotate)
                ev.toRot += step * 5.0f;
            else if (ev.type == CsEventType::Alpha)
                ev.toAlpha = std::max(0.0f, ev.toAlpha - 0.05f);
            else if (ev.type == CsEventType::CameraZoom)
                ev.toZoom = std::max(0.1f, ev.toZoom - 0.05f);
            else if (ev.type == CsEventType::CameraShake)
                ev.shakeStrength = std::max(0.0f, ev.shakeStrength - step);
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

void CutsceneEditor::handleEvent(SDL_Event& e,
                                  float editorZoom, float camWorldX, float camWorldY) {
    if (!active_) return;
    (void)editorZoom; (void)camWorldX; (void)camWorldY;

    computeLayout(screenW_, panelY_);

    if (e.type == SDL_MOUSEBUTTONDOWN) {
        int mx = e.button.x, my = e.button.y;
        bool right = (e.button.button == SDL_BUTTON_RIGHT);

        // Dismiss menus on click outside
        if (showEventMenu_) {
            int numOpts = (int)CsEventType::COUNT;
            int menuX   = timelineX_ + timelineW_ - 110;
            int menuItemH = 18;
            int menuY   = panelY_ - numOpts * menuItemH - 4;
            if (mx >= menuX && mx <= menuX+106 &&
                my >= menuY && my <= menuY + numOpts*menuItemH+4) {
                int idx = (my - menuY - 2) / menuItemH;
                if (idx >= 0 && idx < numOpts) {
                    uint32_t actorId = 0;
                    const Cutscene* cs = currentCutscene();
                    if (cs && selectedActor_ >= 0 && selectedActor_ < (int)cs->actors.size())
                        actorId = cs->actors[selectedActor_].id;
                    addEvent((CsEventType)idx, actorId, scrubTime_);
                }
                showEventMenu_ = false;
                return;
            }
            showEventMenu_ = false;
        }
        if (showActorMenu_ && (mx < actorX_ || mx >= actorX_+actorW_)) {
            showActorMenu_ = false;
        }

        if (my < panelY_) return; // click above panel = editor handles it

        handleTimelineClick(mx, my, right);
        handleListClick(mx, my, 0, panelY_, screenW_, panelH_);
    }
    if (e.type == SDL_MOUSEBUTTONUP) {
        handleTimelineRelease();
    }
    if (e.type == SDL_MOUSEMOTION) {
        handleTimelineMotion(e.motion.x, e.motion.y);
    }
    if (e.type == SDL_MOUSEWHEEL && active_) {
        // Horizontal scroll on timeline
        SDL_Point mp;
        SDL_GetMouseState(&mp.x, &mp.y);
        if (mp.x >= timelineX_ && mp.x < timelineX_+timelineW_ &&
            mp.y >= panelY_) {
            timelineStart_ = std::max(0.0f,
                timelineStart_ - e.wheel.y * 0.5f);
        }
    }
    if (e.type == SDL_KEYDOWN) {
        int mx2, my2;
        SDL_GetMouseState(&mx2, &my2);
        if (my2 >= panelY_) handlePropsPanelEvent(e);
    }
}