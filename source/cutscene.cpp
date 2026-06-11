#include "cutscene.h"
#include "assets.h"
#include <SDL2/SDL_image.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>

// ---- Cutscene helpers ----

float Cutscene::totalDuration() const {
    float t = 0;
    for (const auto& e : events)
        t = std::max(t, e.startTime + e.duration);
    return t;
}

const CsActor* Cutscene::findActor(uint32_t id) const {
    for (const auto& a : actors)
        if (a.id == id) return &a;
    return nullptr;
}

const CsDialogSeq* Cutscene::findDialog(const std::string& id) const {
    for (const auto& d : dialogs)
        if (d.id == id) return &d;
    return nullptr;
}

// ---- File I/O ----

static void writeStr(FILE* f, const char* key, const std::string& val) {
    fprintf(f, "%s=%s\n", key, val.c_str());
}
static void writeF(FILE* f, const char* key, float val) {
    fprintf(f, "%s=%.4f\n", key, (double)val);
}
static void writeI(FILE* f, const char* key, int val) {
    fprintf(f, "%s=%d\n", key, val);
}
static void writeB(FILE* f, const char* key, bool val) {
    fprintf(f, "%s=%d\n", key, val ? 1 : 0);
}

bool CutsceneLibrary::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "# Cold Start Cutscene Library v2\n");
    fprintf(f, "[library]\nversion=2\n");
    for (const auto& cs : cutscenes) {
        fprintf(f, "\n[cutscene]\n");
        writeStr(f, "id", cs.id);
        writeB(f, "block_input", cs.blockInput);
        writeStr(f, "chain_on_end", cs.chainOnEnd);
        // actors
        for (const auto& a : cs.actors) {
            fprintf(f, "[actor]\n");
            writeI(f, "id",   (int)a.id);
            writeStr(f, "name", a.name);
            writeI(f, "type",  (int)a.type);
            writeI(f, "enemy_type", (int)a.enemyType);
            writeStr(f, "sprite", a.spritePath);
            writeF(f, "sx",  a.startX);
            writeF(f, "sy",  a.startY);
            writeF(f, "srot", a.startRot);
            writeF(f, "ssx", a.startScaleX);
            writeF(f, "ssy", a.startScaleY);
            writeF(f, "salpha", a.startAlpha);
            writeB(f, "svis",  a.startVisible);
            writeB(f, "fliph", a.flipH);
        }
        // events
        for (const auto& e : cs.events) {
            fprintf(f, "[event]\n");
            writeI(f, "actor_id", (int)e.actorId);
            writeI(f, "type",     (int)e.type);
            writeF(f, "t0",       e.startTime);
            writeF(f, "dur",      e.duration);
            writeI(f, "ease",     (int)e.ease);
            writeF(f, "fx", e.fromX); writeF(f, "fy", e.fromY);
            writeF(f, "tx", e.toX);   writeF(f, "ty", e.toY);
            writeF(f, "fr", e.fromRot); writeF(f, "tr", e.toRot);
            writeF(f, "fsx", e.fromScaleX); writeF(f, "fsy", e.fromScaleY);
            writeF(f, "tsx", e.toScaleX);   writeF(f, "tsy", e.toScaleY);
            writeF(f, "fa", e.fromAlpha); writeF(f, "ta", e.toAlpha);
            writeF(f, "flr", e.flashR); writeF(f, "flg", e.flashG); writeF(f, "flb", e.flashB);
            writeF(f, "fz", e.fromZoom); writeF(f, "tz", e.toZoom);
            writeF(f, "shake", e.shakeStrength);
            writeB(f, "fade_black", e.fadeToBlack);
            writeB(f, "show_bars", e.showBars);
            writeB(f, "vis",  e.visible);
            writeI(f, "frame", e.frame);
            writeF(f, "expl_x", e.explX); writeF(f, "expl_y", e.explY);
            writeStr(f, "dialog_id", e.dialogId);
            writeStr(f, "sfx", e.sfxPath);
            writeF(f, "spawn_x", e.spawnX); writeF(f, "spawn_y", e.spawnY);
            writeB(f, "spawn_override_pos", e.spawnOverridePos);
            writeStr(f, "flag_name", e.flagName);
            writeB(f, "flag_value", e.flagValue);
            writeStr(f, "chain_cs_id", e.chainCsId);
            writeI(f, "signal_delta", e.signalDelta);
            writeI(f, "branch_var", (int)e.branchVar);
            writeI(f, "branch_cmp", (int)e.branchCmp);
            writeI(f, "branch_threshold", e.branchThreshold);
            writeStr(f, "chain_false_id", e.chainFalseId);
        }
        // dialogs
        for (const auto& seq : cs.dialogs) {
            fprintf(f, "[dialog]\n");
            writeStr(f, "id", seq.id);
            for (const auto& line : seq.lines) {
                fprintf(f, "[line]\n");
                writeStr(f, "char",     line.character);
                writeStr(f, "portrait", line.portrait);
                writeStr(f, "text",     line.text);
                writeB(f,   "pleft",    line.portraitLeft);
                writeStr(f, "sfx",      line.sfxPath);
                writeI(f, "num_choices", (int)line.choices.size());
                for (int ci = 0; ci < (int)line.choices.size(); ci++) {
                    fprintf(f, "[choice]\n");
                    writeStr(f, "text",          line.choices[ci].text);
                    writeStr(f, "next_seq",       line.choices[ci].nextSeqId);
                    writeStr(f, "set_flag",       line.choices[ci].setFlag);
                    writeB(f,   "set_flag_value", line.choices[ci].setFlagValue);
                }
            }
        }
    }
    fclose(f);
    return true;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool CutsceneLibrary::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    cutscenes.clear();

    char buf[1024];
    Cutscene*     curCs     = nullptr;
    CsActor*      curActor  = nullptr;
    CsEvent*      curEvent  = nullptr;
    CsDialogSeq*  curSeq    = nullptr;
    CsDialogLine* curLine   = nullptr;
    CsDialogChoice* curChoice = nullptr;

    while (fgets(buf, sizeof(buf), f)) {
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[') {
            std::string tag = line.substr(1, line.size() - 2);
            curActor = nullptr; curEvent = nullptr;
            curChoice = nullptr;
            if (tag != "line") curLine = nullptr;
            if (tag == "cutscene") {
                cutscenes.push_back(Cutscene{});
                curCs = &cutscenes.back();
                curSeq = nullptr;
            } else if (tag == "actor" && curCs) {
                curCs->actors.push_back(CsActor{});
                curActor = &curCs->actors.back();
            } else if (tag == "event" && curCs) {
                curCs->events.push_back(CsEvent{});
                curEvent = &curCs->events.back();
            } else if (tag == "dialog" && curCs) {
                curCs->dialogs.push_back(CsDialogSeq{});
                curSeq = &curCs->dialogs.back();
                curLine = nullptr;
            } else if (tag == "line" && curSeq) {
                curSeq->lines.push_back(CsDialogLine{});
                curLine = &curSeq->lines.back();
            } else if (tag == "choice" && curLine) {
                curLine->choices.push_back(CsDialogChoice{});
                curChoice = &curLine->choices.back();
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = line.substr(eq + 1);

        auto fv = [&](){ return (float)atof(val.c_str()); };
        auto iv = [&](){ return atoi(val.c_str()); };
        auto bv = [&](){ return atoi(val.c_str()) != 0; };
        auto sv = [&](){ return trim(val); };

        if (curChoice) {
            if      (key == "text")          curChoice->text         = sv();
            else if (key == "next_seq")      curChoice->nextSeqId    = sv();
            else if (key == "set_flag")      curChoice->setFlag      = sv();
            else if (key == "set_flag_value") curChoice->setFlagValue = bv();
        } else if (curLine) {
            if      (key == "char")     curLine->character   = sv();
            else if (key == "portrait") curLine->portrait    = sv();
            else if (key == "text")     curLine->text        = sv();
            else if (key == "pleft")    curLine->portraitLeft = bv();
            else if (key == "sfx")      curLine->sfxPath     = sv();
            // num_choices is informational, choices are loaded via [choice] sections
        } else if (curSeq) {
            if (key == "id") curSeq->id = sv();
        } else if (curActor) {
            if      (key == "id")         curActor->id          = (uint32_t)iv();
            else if (key == "name")       curActor->name        = sv();
            else if (key == "type")       curActor->type        = (CsActorType)iv();
            else if (key == "enemy_type") curActor->enemyType   = (CsEnemyType)iv();
            else if (key == "sprite")     curActor->spritePath  = sv();
            else if (key == "sx")         curActor->startX      = fv();
            else if (key == "sy")         curActor->startY      = fv();
            else if (key == "srot")       curActor->startRot    = fv();
            else if (key == "ssx")        curActor->startScaleX = fv();
            else if (key == "ssy")        curActor->startScaleY = fv();
            else if (key == "salpha")     curActor->startAlpha  = fv();
            else if (key == "svis")       curActor->startVisible = bv();
            else if (key == "fliph")      curActor->flipH        = bv();
        } else if (curEvent) {
            if      (key == "actor_id") curEvent->actorId   = (uint32_t)iv();
            else if (key == "type")     curEvent->type      = (CsEventType)iv();
            else if (key == "t0")       curEvent->startTime = fv();
            else if (key == "dur")      curEvent->duration  = fv();
            else if (key == "ease")     curEvent->ease      = (CsEase)iv();
            else if (key == "fx")  curEvent->fromX      = fv();
            else if (key == "fy")  curEvent->fromY      = fv();
            else if (key == "tx")  curEvent->toX        = fv();
            else if (key == "ty")  curEvent->toY        = fv();
            else if (key == "fr")  curEvent->fromRot    = fv();
            else if (key == "tr")  curEvent->toRot      = fv();
            else if (key == "fsx") curEvent->fromScaleX = fv();
            else if (key == "fsy") curEvent->fromScaleY = fv();
            else if (key == "tsx") curEvent->toScaleX   = fv();
            else if (key == "tsy") curEvent->toScaleY   = fv();
            else if (key == "fa")  curEvent->fromAlpha  = fv();
            else if (key == "ta")  curEvent->toAlpha    = fv();
            else if (key == "flr") curEvent->flashR     = fv();
            else if (key == "flg") curEvent->flashG     = fv();
            else if (key == "flb") curEvent->flashB     = fv();
            else if (key == "fz")  curEvent->fromZoom   = fv();
            else if (key == "tz")  curEvent->toZoom     = fv();
            else if (key == "shake")              curEvent->shakeStrength     = fv();
            else if (key == "fade_black")         curEvent->fadeToBlack       = bv();
            else if (key == "show_bars")          curEvent->showBars          = bv();
            else if (key == "vis")                curEvent->visible           = bv();
            else if (key == "frame")              curEvent->frame             = iv();
            else if (key == "expl_x")             curEvent->explX             = fv();
            else if (key == "expl_y")             curEvent->explY             = fv();
            else if (key == "dialog_id")          curEvent->dialogId          = sv();
            else if (key == "sfx")                curEvent->sfxPath           = sv();
            else if (key == "spawn_x")            curEvent->spawnX            = fv();
            else if (key == "spawn_y")            curEvent->spawnY            = fv();
            else if (key == "spawn_override_pos") curEvent->spawnOverridePos  = bv();
            else if (key == "flag_name")          curEvent->flagName          = sv();
            else if (key == "flag_value")         curEvent->flagValue         = bv();
            else if (key == "chain_cs_id")        curEvent->chainCsId         = sv();
            else if (key == "signal_delta")       curEvent->signalDelta       = iv();
            else if (key == "branch_var")         curEvent->branchVar         = (uint8_t)iv();
            else if (key == "branch_cmp")         curEvent->branchCmp         = (uint8_t)iv();
            else if (key == "branch_threshold")   curEvent->branchThreshold   = iv();
            else if (key == "chain_false_id")     curEvent->chainFalseId      = sv();
        } else if (curCs) {
            if      (key == "id")           curCs->id           = sv();
            else if (key == "block_input")  curCs->blockInput   = bv();
            else if (key == "chain_on_end") curCs->chainOnEnd   = sv();
        }
    }
    fclose(f);
    return true;
}

// ---- Playback ----

float CutscenePlayback::applyEase(float t, CsEase ease) const {
    t = std::max(0.0f, std::min(1.0f, t));
    switch (ease) {
        case CsEase::EaseIn:    return t * t;
        case CsEase::EaseOut:   return 1.0f - (1.0f - t) * (1.0f - t);
        case CsEase::EaseInOut: return t < 0.5f ? 2*t*t : 1.0f - 2*(1-t)*(1-t);
        case CsEase::Instant:   return t >= 1.0f ? 1.0f : 0.0f;
        default:                return t; // Linear
    }
}

int CutscenePlayback::actorIdx(uint32_t id) const {
    if (!cutscene) return -1;
    for (int i = 0; i < (int)cutscene->actors.size(); i++)
        if (cutscene->actors[i].id == id) return i;
    return -1;
}

CsActorState& CutscenePlayback::stateFor(uint32_t id) {
    int i = actorIdx(id);
    static CsActorState dummy;
    if (i < 0 || i >= (int)actorStates.size()) return dummy;
    return actorStates[i];
}

void CutscenePlayback::freeTextures() {
    for (auto* t : actorTex)
        if (t) SDL_DestroyTexture(t);
    actorTex.clear();
}

void CutscenePlayback::loadTextures(SDL_Renderer* r) {
    freeTextures();
    if (!cutscene) return;
    actorTex.resize(cutscene->actors.size(), nullptr);
    for (int i = 0; i < (int)cutscene->actors.size(); i++) {
        const auto& a = cutscene->actors[i];
        if (a.type == CsActorType::FreeSprite && !a.spritePath.empty())
            actorTex[i] = IMG_LoadTexture(r, a.spritePath.c_str());
    }
}

void CutscenePlayback::start(const Cutscene* c, SDL_Renderer* r) {
    stop();
    if (!c) return;
    cutscene = c;
    time     = 0;
    active   = true;

    actorStates.resize(c->actors.size());
    for (int i = 0; i < (int)c->actors.size(); i++) {
        const auto& a = c->actors[i];
        auto& s       = actorStates[i];
        s.x       = a.startX;
        s.y       = a.startY;
        s.rot     = a.startRot;
        s.scaleX  = a.startScaleX;
        s.scaleY  = a.startScaleY;
        s.alpha   = a.startAlpha;
        s.visible = a.startVisible;
        s.frame   = 0;
        s.flashAmt = 0;
    }
    cam = CsCamState{};
    dialog = CsDialogPlayback{};
    cinematicBarsAmt = 0;
    screenFadeAlpha  = 0;
    screenFadeToBlack = false;
    scriptFlags.clear();
    pendingChainId.clear();
    pendingEnd = false;
    pendingSignalDelta = 0;
    loadTextures(r);
}

void CutscenePlayback::stop() {
    active   = false;
    cutscene = nullptr;
    time     = 0;
    freeTextures();
    actorStates.clear();
    dialog = CsDialogPlayback{};
    scriptFlags.clear();
    pendingChainId.clear();
    pendingEnd = false;
}

bool CutscenePlayback::isDone() const {
    if (!active || !cutscene) return true;
    if (pendingEnd) return true;
    return time >= cutscene->totalDuration() && !dialog.active;
}

void CutscenePlayback::applyEvent(const CsEvent& ev, float localT,
                                   const CutsceneLibrary& /*lib*/) {
    float t = applyEase(localT, ev.ease);
    auto lerp = [](float a, float b, float t){ return a + (b-a)*t; };

    switch (ev.type) {
        case CsEventType::Move: {
            auto& s = stateFor(ev.actorId);
            s.x = lerp(ev.fromX, ev.toX, t);
            s.y = lerp(ev.fromY, ev.toY, t);
            break;
        }
        case CsEventType::Rotate: {
            auto& s = stateFor(ev.actorId);
            s.rot = lerp(ev.fromRot, ev.toRot, t);
            break;
        }
        case CsEventType::Scale: {
            auto& s  = stateFor(ev.actorId);
            s.scaleX = lerp(ev.fromScaleX, ev.toScaleX, t);
            s.scaleY = lerp(ev.fromScaleY, ev.toScaleY, t);
            break;
        }
        case CsEventType::Alpha: {
            auto& s = stateFor(ev.actorId);
            s.alpha = lerp(ev.fromAlpha, ev.toAlpha, t);
            break;
        }
        case CsEventType::Flash: {
            auto& s   = stateFor(ev.actorId);
            s.flashR  = ev.flashR;
            s.flashG  = ev.flashG;
            s.flashB  = ev.flashB;
            float ft  = (t < 0.5f) ? t * 2.0f : (1.0f - t) * 2.0f;
            s.flashAmt = ft;
            break;
        }
        case CsEventType::SetVisible: {
            auto& s = stateFor(ev.actorId);
            if (localT >= 1.0f) s.visible = ev.visible;
            break;
        }
        case CsEventType::SetFrame: {
            auto& s = stateFor(ev.actorId);
            if (localT >= 1.0f) s.frame = ev.frame;
            break;
        }
        case CsEventType::CameraMove:
            cam.x = lerp(ev.fromX, ev.toX, t);
            cam.y = lerp(ev.fromY, ev.toY, t);
            break;
        case CsEventType::CameraZoom:
            cam.zoom = lerp(ev.fromZoom, ev.toZoom, t);
            break;
        case CsEventType::ScreenFade: {
            float alpha = ev.fadeToBlack ? lerp(0, 1, t) : lerp(1, 0, t);
            screenFadeAlpha  = alpha;
            screenFadeToBlack = ev.fadeToBlack;
            break;
        }
        case CsEventType::CinematicBars:
            cinematicBarsAmt = ev.showBars ? lerp(0, 1, t) : lerp(1, 0, t);
            break;
        default:
            break;
    }
}

void CutscenePlayback::update(float dt, const CutsceneLibrary& lib) {
    if (!active || !cutscene) return;

    // If dialog is waiting for input, don't advance time
    if (dialog.active && !dialog.done) {
        if (dialog.lineComplete) return; // waiting for advance/choice
        dialog.typeTimer += dt;
        const float CHARS_PER_SEC = 40.0f;
        if (dialog.seq && dialog.lineIdx < (int)dialog.seq->lines.size()) {
            int total = (int)dialog.seq->lines[dialog.lineIdx].text.size();
            dialog.visibleChars = std::min(total,
                (int)(dialog.typeTimer * CHARS_PER_SEC));
            if (dialog.visibleChars >= total)
                dialog.lineComplete = true;
        }
        return;
    }

    // Decay camera shake
    if (cam.shakeTimer > 0) {
        cam.shakeTimer -= dt;
        if (cam.shakeTimer <= 0) { cam.shakeX = cam.shakeY = 0; }
        else {
            float a = cam.shakeTimer / 0.5f;
            cam.shakeX = ((rand() % 100) / 50.0f - 1.0f) * cam.shakeStrength * a;
            cam.shakeY = ((rand() % 100) / 50.0f - 1.0f) * cam.shakeStrength * a;
        }
    }

    // Decay actor flash
    for (auto& s : actorStates)
        if (s.flashAmt > 0) s.flashAmt = std::max(0.0f, s.flashAmt - dt * 4.0f);

    time += dt;

    // Apply all active events at current time
    for (const auto& ev : cutscene->events) {
        if (time < ev.startTime) continue;
        bool justStarted = (time - dt < ev.startTime);

        switch (ev.type) {
            case CsEventType::Dialog:
                if (justStarted && !dialog.active) {
                    const CsDialogSeq* seq = cutscene->findDialog(ev.dialogId);
                    if (seq && !seq->lines.empty()) {
                        dialog.active       = true;
                        dialog.seq          = seq;
                        dialog.lineIdx      = 0;
                        dialog.typeTimer    = 0;
                        dialog.visibleChars = 0;
                        dialog.lineComplete = false;
                        dialog.done         = false;
                        dialog.hoveredChoice = -1;
                    }
                }
                continue;
            case CsEventType::CameraShake:
                if (justStarted) {
                    cam.shakeStrength = ev.shakeStrength;
                    cam.shakeTimer    = std::max(ev.duration, 0.3f);
                }
                continue;
            case CsEventType::PlaySFX:
                if (justStarted && !ev.sfxPath.empty()) {
                    static std::unordered_map<std::string, Mix_Chunk*> s_sfxCache;
                    auto it = s_sfxCache.find(ev.sfxPath);
                    Mix_Chunk* sfx;
                    if (it != s_sfxCache.end()) {
                        sfx = it->second;
                    } else {
                        sfx = Mix_LoadWAV(ev.sfxPath.c_str());
                        s_sfxCache[ev.sfxPath] = sfx;
                    }
                    if (sfx) Mix_PlayChannel(-1, sfx, 0);
                }
                continue;
            case CsEventType::SpawnExplosion:
                continue;
            case CsEventType::Wait:
                continue;
            case CsEventType::SpawnActor:
                if (justStarted) {
                    auto& s = stateFor(ev.actorId);
                    s.visible = true;
                    if (ev.spawnOverridePos) {
                        s.x = ev.spawnX;
                        s.y = ev.spawnY;
                    }
                }
                continue;
            case CsEventType::DespawnActor:
                if (justStarted) {
                    auto& s = stateFor(ev.actorId);
                    s.visible = false;
                }
                continue;
            case CsEventType::SetFlag:
                if (justStarted && !ev.flagName.empty())
                    scriptFlags[ev.flagName] = ev.flagValue;
                continue;
            case CsEventType::ChainCutscene:
                if (justStarted && !ev.chainCsId.empty()) {
                    pendingChainId = ev.chainCsId;
                }
                continue;
            case CsEventType::EndCutscene:
                if (justStarted) pendingEnd = true;
                continue;
            case CsEventType::AdjustSignal:
                if (justStarted) pendingSignalDelta += ev.signalDelta;
                continue;
            case CsEventType::BranchCutscene:
                if (justStarted) {
                    int  lhs = (ev.branchVar == 1) ? extRoute : extSignal;
                    bool cond;
                    switch (ev.branchCmp) {
                        case 1:  cond = (lhs <  ev.branchThreshold); break;
                        case 2:  cond = (lhs == ev.branchThreshold); break;
                        default: cond = (lhs >= ev.branchThreshold); break;
                    }
                    const std::string& target = cond ? ev.chainCsId : ev.chainFalseId;
                    if (!target.empty()) pendingChainId = target;
                    else pendingEnd = true;
                }
                continue;
            default:
                break;
        }

        // Continuous events
        float end    = ev.startTime + std::max(ev.duration, 0.001f);
        float localT = (time >= end) ? 1.0f : (time - ev.startTime) / (end - ev.startTime);
        applyEvent(ev, localT, lib);
    }

    // Auto-chain at end of cutscene
    if (!cutscene->chainOnEnd.empty() && pendingChainId.empty()) {
        if (time >= cutscene->totalDuration() && !dialog.active)
            pendingChainId = cutscene->chainOnEnd;
    }
}

bool CutscenePlayback::advanceDialog() {
    if (!dialog.active) return false;
    if (!dialog.seq) { dialog.active = false; return false; }

    // If choices are being shown, don't advance - player must choose
    if (dialog.lineComplete && dialog.lineIdx < (int)dialog.seq->lines.size()) {
        const auto& line = dialog.seq->lines[dialog.lineIdx];
        if (!line.choices.empty()) {
            return false; // must use selectDialogChoice
        }
    }

    if (!dialog.lineComplete) {
        // Skip typewriter
        if (dialog.lineIdx < (int)dialog.seq->lines.size())
            dialog.visibleChars = (int)dialog.seq->lines[dialog.lineIdx].text.size();
        dialog.lineComplete = true;
        return true;
    }

    dialog.lineIdx++;
    dialog.typeTimer    = 0;
    dialog.visibleChars = 0;
    dialog.lineComplete = false;
    dialog.hoveredChoice = -1;

    if (dialog.lineIdx >= (int)dialog.seq->lines.size()) {
        dialog.active = false;
        dialog.done   = true;
    }
    return true;
}

void CutscenePlayback::selectDialogChoice(int idx, const CutsceneLibrary& lib) {
    if (!dialog.active || !dialog.seq) return;
    if (dialog.lineIdx >= (int)dialog.seq->lines.size()) return;

    const auto& line = dialog.seq->lines[dialog.lineIdx];
    if (idx < 0 || idx >= (int)line.choices.size()) return;

    const auto& choice = line.choices[idx];

    // Set flag if specified
    if (!choice.setFlag.empty())
        scriptFlags[choice.setFlag] = choice.setFlagValue;

    // Jump or end
    if (choice.nextSeqId.empty()) {
        dialog.active = false;
        dialog.done   = true;
    } else {
        // Try to find in current cutscene first, then library
        const CsDialogSeq* nextSeq = cutscene ? cutscene->findDialog(choice.nextSeqId) : nullptr;
        if (!nextSeq) {
            // Search all cutscenes in library
            for (const auto& cs : lib.cutscenes) {
                nextSeq = cs.findDialog(choice.nextSeqId);
                if (nextSeq) break;
            }
        }
        if (nextSeq && !nextSeq->lines.empty()) {
            dialog.seq          = nextSeq;
            dialog.lineIdx      = 0;
            dialog.typeTimer    = 0;
            dialog.visibleChars = 0;
            dialog.lineComplete = false;
            dialog.done         = false;
            dialog.hoveredChoice = -1;
        } else {
            dialog.active = false;
            dialog.done   = true;
        }
    }
}

// ---- Rendering ----

void CutscenePlayback::renderActors(SDL_Renderer* r,
                                     float camX, float camY, float camZoom,
                                     SDL_Texture* playerBody,
                                     SDL_Texture* playerLegs,
                                     SDL_Texture* enemyTex) const {
    if (!active || !cutscene) return;
    for (int i = 0; i < (int)cutscene->actors.size(); i++) {
        if (i >= (int)actorStates.size()) break;
        const auto& a = cutscene->actors[i];
        const auto& s = actorStates[i];
        if (!s.visible) continue;

        SDL_Texture* tex = nullptr;
        if (a.type == CsActorType::FreeSprite) {
            tex = (i < (int)actorTex.size()) ? actorTex[i] : nullptr;
        } else if (a.type == CsActorType::Player) {
            tex = playerBody;
        } else if (a.type == CsActorType::Enemy) {
            tex = enemyTex;
        }
        if (!tex) continue;

        int tw, th;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        int dw = (int)(tw * camZoom * s.scaleX);
        int dh = (int)(th * camZoom * s.scaleY);

        float sx = (s.x - camX) * camZoom;
        float sy = (s.y - camY) * camZoom;
        SDL_Rect dst = { (int)sx - dw/2, (int)sy - dh/2, dw, dh };

        SDL_SetTextureAlphaMod(tex, (Uint8)(s.alpha * 255));
        if (s.flashAmt > 0.01f) {
            SDL_SetTextureColorMod(tex,
                (Uint8)(255 * (1-s.flashAmt) + s.flashR * s.flashAmt),
                (Uint8)(255 * (1-s.flashAmt) + s.flashG * s.flashAmt),
                (Uint8)(255 * (1-s.flashAmt) + s.flashB * s.flashAmt));
        } else {
            SDL_SetTextureColorMod(tex, 255, 255, 255);
        }
        SDL_Point center = { dw/2, dh/2 };
        SDL_RenderCopyEx(r, tex, nullptr, &dst,
                         (double)s.rot, &center,
                         a.flipH ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    }
    (void)playerLegs;
}

void CutscenePlayback::renderDialogLine(SDL_Renderer* r,
                                         const CsDialogLine& line,
                                         int visibleChars,
                                         int screenW, int screenH,
                                         const CutsceneLibrary& /*lib*/) const {
    const int BAR_H   = 160;
    const int PAD     = 12;
    const int PORT_SZ = 128;
    int barY = screenH - BAR_H;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 210);
    SDL_Rect barRect = { 0, barY, screenW, BAR_H };
    SDL_RenderFillRect(r, &barRect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    if (!line.portrait.empty()) {
        SDL_Texture* port = IMG_LoadTexture(r, line.portrait.c_str());
        if (port) {
            int px = line.portraitLeft ? PAD : (screenW - PAD - PORT_SZ);
            SDL_Rect portRect = { px, barY + (BAR_H - PORT_SZ)/2, PORT_SZ, PORT_SZ };
            SDL_RenderCopy(r, port, nullptr, &portRect);
            SDL_DestroyTexture(port);
        }
    }
    (void)visibleChars; (void)PAD;
}

void CutscenePlayback::renderOverlay(SDL_Renderer* r,
                                      int screenW, int screenH,
                                      const CutsceneLibrary& lib) const {
    if (!active) return;

    const int BAR_SZ = (int)(screenH * 0.11f);

    // Cinematic bars
    if (cinematicBarsAmt > 0.001f) {
        int bh = (int)(BAR_SZ * cinematicBarsAmt);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_Rect top    = { 0, 0,          screenW, bh };
        SDL_Rect bottom = { 0, screenH-bh, screenW, bh };
        SDL_RenderFillRect(r, &top);
        SDL_RenderFillRect(r, &bottom);
    }

    // Dialog
    if (dialog.active && dialog.seq &&
        dialog.lineIdx < (int)dialog.seq->lines.size()) {
        const auto& line = dialog.seq->lines[dialog.lineIdx];
        renderDialogLine(r, line, dialog.visibleChars, screenW, screenH, lib);

        // Choices (shown after typewriter completes)
        if (dialog.lineComplete && !line.choices.empty()) {
            const int CHOICE_H = 28;
            const int CHOICE_W = 400;
            int totalH = (int)line.choices.size() * CHOICE_H + 8;
            int choiceX = (screenW - CHOICE_W) / 2;
            int choiceY = screenH - 160 - totalH - 8;

            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
            SDL_Rect bg = {choiceX-4, choiceY-4, CHOICE_W+8, totalH+8};
            SDL_RenderFillRect(r, &bg);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

            int mx, my;
            SDL_GetMouseState(&mx, &my);

            for (int ci = 0; ci < (int)line.choices.size(); ci++) {
                int cy = choiceY + ci * CHOICE_H + 4;
                SDL_Rect cr = {choiceX, cy, CHOICE_W, CHOICE_H - 2};
                bool hov = (mx >= cr.x && mx <= cr.x+cr.w && my >= cr.y && my <= cr.y+cr.h);
                SDL_Color bg2 = hov ? SDL_Color{60,80,120,255} : SDL_Color{20,24,36,255};
                SDL_SetRenderDrawColor(r, bg2.r, bg2.g, bg2.b, 255);
                SDL_RenderFillRect(r, &cr);
                SDL_SetRenderDrawColor(r, 80, 100, 160, 255);
                SDL_RenderDrawRect(r, &cr);
            }
        } else if (dialog.lineComplete && line.choices.empty()) {
            // Blinking advance indicator
            Uint32 ticks = SDL_GetTicks();
            if ((ticks / 400) % 2 == 0) {
                SDL_SetRenderDrawColor(r, 220, 220, 80, 255);
                int ix = screenW - 30;
                int iy = screenH - 24;
                SDL_RenderDrawLine(r, ix,   iy-8, ix+14, iy);
                SDL_RenderDrawLine(r, ix,   iy+8, ix+14, iy);
                SDL_RenderDrawLine(r, ix,   iy-8, ix,    iy+8);
            }
        }
    }

    // Screen fade
    if (screenFadeAlpha > 0.01f) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(screenFadeAlpha * 255));
        SDL_Rect fr = { 0, 0, screenW, screenH };
        SDL_RenderFillRect(r, &fr);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
}
