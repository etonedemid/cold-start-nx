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
    for (const auto& e : events) {
        // PostFXAcid duration is an effect lifespan, not a cutscene hold time.
        float dur = (e.type == CsEventType::PostFXAcid) ? 0.0f : e.duration;
        t = std::max(t, e.startTime + dur);
    }
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
    if (!onDeathId.empty()) {
        fprintf(f, "\n[config]\n");
        writeStr(f, "ondeath", onDeathId);
    }
    for (const auto& va : triggerVarActions) {
        fprintf(f, "\n[trigger_var]\n");
        writeI(f, "trigger", va.triggerIndex);
        writeStr(f, "key", va.key);
        writeI(f, "value", va.value);
        writeI(f, "op", (int)va.op);
        writeI(f, "scope", (int)va.scope);
    }
    for (const auto& tc : triggerConditions) {
        fprintf(f, "\n[trigger_cond]\n");
        writeI(f, "trigger", tc.triggerIndex);
        writeStr(f, "var", tc.varName);
        writeI(f, "value", tc.value);
        writeI(f, "cmp", (int)tc.cmp);
    }
    for (const auto& tm : triggerMapLoads) {
        fprintf(f, "\n[trigger_map]\n");
        writeI(f, "trigger", tm.triggerIndex);
        writeStr(f, "path", tm.mapPath);
    }
    for (const auto& mc : triggerMultiConfigs) {
        fprintf(f, "\n[trigger_multi]\n");
        writeI(f, "trigger", mc.triggerIndex);
        writeF(f, "cooldown", mc.cooldown);
    }
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
            writeI(f, "layer", a.layer);
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
            writeI(f, "spawn_enemy_type", (int)e.spawnEnemyTypeId);
            writeI(f, "spawn_pickup_type", (int)e.spawnPickupTypeId);
            writeStr(f, "var_name", e.varName);
            writeI(f, "var_value", e.varValue);
            writeI(f, "var_op", (int)e.varOp);
            writeI(f, "var_scope", (int)e.varScope);
            writeStr(f, "map_path", e.mapPath);
            writeF(f, "acid_c1r", e.acidColor1R);
            writeF(f, "acid_c1g", e.acidColor1G);
            writeF(f, "acid_c1b", e.acidColor1B);
            writeF(f, "acid_c2r", e.acidColor2R);
            writeF(f, "acid_c2g", e.acidColor2G);
            writeF(f, "acid_c2b", e.acidColor2B);
            writeStr(f, "console_cmd", e.consoleCmd);
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
                    writeStr(f, "cond_var",       line.choices[ci].condVar);
                    writeI(f,   "cond_cmp",       line.choices[ci].condCmp);
                    writeI(f,   "cond_value",     line.choices[ci].condValue);
                }
            }
        }
    }
    for (const auto& kv : varDefaults) {
        fprintf(f, "\n[var_default]\n");
        writeStr(f, "name", kv.first);
        writeI(f, "value", kv.second);
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
    onDeathId.clear();
    triggerVarActions.clear();

    char buf[1024];
    Cutscene*     curCs     = nullptr;
    CsActor*      curActor  = nullptr;
    CsEvent*      curEvent  = nullptr;
    CsDialogSeq*  curSeq    = nullptr;
    CsDialogLine* curLine   = nullptr;
    CsDialogChoice* curChoice = nullptr;
    bool          inConfig    = false;
    TriggerVarAction*  curTrigVar   = nullptr;
    TriggerCondition*  curTrigCond  = nullptr;
    TriggerMapLoad*    curTrigMap   = nullptr;
    TriggerMultiConfig* curTrigMulti = nullptr;
    bool          inVarDefault = false;
    std::string   varDefaultName;

    while (fgets(buf, sizeof(buf), f)) {
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[') {
            std::string tag = line.substr(1, line.size() - 2);
            curActor = nullptr; curEvent = nullptr;
            curChoice = nullptr; inConfig = false; inVarDefault = false;
            curTrigVar = nullptr; curTrigCond = nullptr; curTrigMap = nullptr; curTrigMulti = nullptr;
            if (tag != "line") curLine = nullptr;
            if (tag == "config") {
                inConfig = true;
            } else if (tag == "trigger_var") {
                triggerVarActions.push_back(TriggerVarAction{});
                curTrigVar = &triggerVarActions.back();
            } else if (tag == "trigger_cond") {
                triggerConditions.push_back(TriggerCondition{});
                curTrigCond = &triggerConditions.back();
            } else if (tag == "trigger_map") {
                triggerMapLoads.push_back(TriggerMapLoad{});
                curTrigMap = &triggerMapLoads.back();
            } else if (tag == "trigger_multi") {
                triggerMultiConfigs.push_back(TriggerMultiConfig{});
                curTrigMulti = &triggerMultiConfigs.back();
            } else if (tag == "var_default") {
                inVarDefault = true;
                varDefaultName.clear();
            } else if (tag == "cutscene") {
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

        if (inVarDefault) {
            if      (key == "name")  varDefaultName = sv();
            else if (key == "value" && !varDefaultName.empty())
                varDefaults[varDefaultName] = iv();
        } else if (inConfig) {
            if (key == "ondeath") onDeathId = sv();
        } else if (curTrigVar) {
            if      (key == "trigger") curTrigVar->triggerIndex = iv();
            else if (key == "key")     curTrigVar->key   = sv();
            else if (key == "value")   curTrigVar->value = iv();
            else if (key == "op")      curTrigVar->op    = (uint8_t)iv();
            else if (key == "scope")   curTrigVar->scope = (uint8_t)iv();
        } else if (curTrigCond) {
            if      (key == "trigger") curTrigCond->triggerIndex = iv();
            else if (key == "var")     curTrigCond->varName = sv();
            else if (key == "value")   curTrigCond->value = iv();
            else if (key == "cmp")     curTrigCond->cmp   = (uint8_t)iv();
        } else if (curTrigMap) {
            if      (key == "trigger") curTrigMap->triggerIndex = iv();
            else if (key == "path")    curTrigMap->mapPath = sv();
        } else if (curTrigMulti) {
            if      (key == "trigger")  curTrigMulti->triggerIndex = iv();
            else if (key == "cooldown") curTrigMulti->cooldown     = fv();
        } else if (curChoice) {
            if      (key == "text")          curChoice->text         = sv();
            else if (key == "next_seq")      curChoice->nextSeqId    = sv();
            else if (key == "set_flag")      curChoice->setFlag      = sv();
            else if (key == "set_flag_value") curChoice->setFlagValue = bv();
            else if (key == "cond_var")      curChoice->condVar      = sv();
            else if (key == "cond_cmp")      curChoice->condCmp      = (uint8_t)iv();
            else if (key == "cond_value")    curChoice->condValue    = iv();
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
            else if (key == "layer")     curActor->layer        = iv();
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
            else if (key == "var_name")           curEvent->varName           = sv();
            else if (key == "var_value")          curEvent->varValue          = iv();
            else if (key == "var_op")             curEvent->varOp             = (uint8_t)iv();
            else if (key == "var_scope")          curEvent->varScope          = (uint8_t)iv();
            else if (key == "map_path")           curEvent->mapPath           = sv();
            else if (key == "spawn_enemy_type")   curEvent->spawnEnemyTypeId  = (uint8_t)iv();
            else if (key == "spawn_pickup_type")  curEvent->spawnPickupTypeId = (uint8_t)iv();
            else if (key == "acid_c1r")           curEvent->acidColor1R       = fv();
            else if (key == "acid_c1g")           curEvent->acidColor1G       = fv();
            else if (key == "acid_c1b")           curEvent->acidColor1B       = fv();
            else if (key == "acid_c2r")           curEvent->acidColor2R       = fv();
            else if (key == "acid_c2g")           curEvent->acidColor2G       = fv();
            else if (key == "acid_c2b")           curEvent->acidColor2B       = fv();
            else if (key == "console_cmd")        curEvent->consoleCmd        = sv();
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
        if (a.type == CsActorType::FreeSprite && !a.spritePath.empty()) {
            std::string full = Assets::instance().prefix() + a.spritePath;
            actorTex[i] = IMG_LoadTexture(r, full.c_str());
        }
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
    camDriven = false;
    dialog = CsDialogPlayback{};
    cinematicBarsAmt = 0;
    screenFadeAlpha  = 0;
    screenFadeToBlack = false;
    scriptFlags.clear();
    pendingChainId.clear();
    pendingEnd = false;
    pendingVarSets.clear();
    pendingDeathScreen = false;
    pendingLoadMap.clear();
    pendingSignalDelta = 0;
    pendingAcidFX.clear();
    pendingConsoleCmds.clear();
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
            camDriven = true;
            break;
        case CsEventType::CameraZoom:
            cam.zoom = lerp(ev.fromZoom, ev.toZoom, t);
            camDriven = true;
            break;
        case CsEventType::CameraRotate:
            cam.rotation = lerp(ev.fromRot, ev.toRot, t);
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

    // Snapshot positions for velocity-based leg animation
    std::vector<float> prevX(actorStates.size()), prevY(actorStates.size());
    for (int i = 0; i < (int)actorStates.size(); i++) {
        prevX[i] = actorStates[i].x;
        prevY[i] = actorStates[i].y;
    }

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
                if (justStarted) pendingExplosions.push_back({ev.explX, ev.explY});
                continue;
            case CsEventType::SpawnEnemy:
                if (justStarted) pendingEnemies.push_back({ev.explX, ev.explY, ev.spawnEnemyTypeId});
                continue;
            case CsEventType::SpawnPickup:
                if (justStarted) pendingPickups.push_back({ev.explX, ev.explY, ev.spawnPickupTypeId});
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
            case CsEventType::SetVariable:
                if (justStarted && !ev.varName.empty())
                    pendingVarSets.push_back({ev.varName, ev.varValue, ev.varOp, ev.varScope});
                continue;
            case CsEventType::DeathScreen:
                if (justStarted) pendingDeathScreen = true;
                continue;
            case CsEventType::LoadMap:
                if (justStarted && !ev.mapPath.empty()) pendingLoadMap = ev.mapPath;
                continue;
            case CsEventType::PostFXAcid:
                if (justStarted) {
                    PendingAcidFX req;
                    req.enable   = ev.flagValue;
                    req.duration = ev.duration;
                    req.c1r = ev.acidColor1R; req.c1g = ev.acidColor1G; req.c1b = ev.acidColor1B;
                    req.c2r = ev.acidColor2R; req.c2g = ev.acidColor2G; req.c2b = ev.acidColor2B;
                    pendingAcidFX.push_back(req);
                }
                continue;
            case CsEventType::ConsoleCmd:
                if (justStarted && !ev.consoleCmd.empty())
                    pendingConsoleCmds.push_back(ev.consoleCmd);
                continue;
            case CsEventType::BranchCutscene:
                if (justStarted) {
                    // branchVar: 0=SIGNAL, 1=route, 2=named var (extVars[flagName])
                    int lhs;
                    if (ev.branchVar == 1)       lhs = extRoute;
                    else if (ev.branchVar == 2)  lhs = (extVarGet ? extVarGet(ev.flagName) : 0);
                    else                         lhs = extSignal;
                    bool cond;
                    switch (ev.branchCmp) {
                        case 1:  cond = (lhs != ev.branchThreshold); break;
                        case 2:  cond = (lhs == ev.branchThreshold); break;
                        case 3:  cond = (lhs <  ev.branchThreshold); break;
                        case 4:  cond = (lhs >= ev.branchThreshold); break;
                        case 5:  cond = (lhs <= ev.branchThreshold); break;
                        default: cond = (lhs >  ev.branchThreshold); break;
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

    // Leg animation for Player actors (driven by position delta)
    if (dt > 0.0001f) {
        for (int i = 0; i < (int)cutscene->actors.size() &&
                        i < (int)actorStates.size(); i++) {
            if (cutscene->actors[i].type != CsActorType::Player) continue;
            auto& s = actorStates[i];
            float vx = (s.x - prevX[i]) / dt;
            float vy = (s.y - prevY[i]) / dt;
            float spd = sqrtf(vx * vx + vy * vy);
            if (spd > 5.0f) {
                s.legRotation = atan2f(vy, vx);
                float legAnimSpeed = spd / 520.0f;
                s.legAnimTimer += dt * legAnimSpeed;
                if (s.legAnimTimer > 0.07f) {
                    s.legAnimTimer = 0;
                    s.legAnimFrame++;
                }
            } else {
                s.legAnimTimer = 0;
                s.legAnimFrame = 0;
            }
        }
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
    dialog.visibleChoices.clear();

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
            dialog.visibleChoices.clear();
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

    // Build draw order sorted by layer (stable: same layer keeps array order)
    std::vector<int> order;
    order.reserve(cutscene->actors.size());
    for (int i = 0; i < (int)cutscene->actors.size(); i++) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return cutscene->actors[a].layer < cutscene->actors[b].layer;
    });

    for (int ii : order) {
        int i = ii;
        if (i >= (int)actorStates.size()) continue;
        const auto& a = cutscene->actors[i];
        const auto& s = actorStates[i];
        if (!s.visible) continue;

        SDL_Texture* tex = nullptr;
        if (a.type == CsActorType::FreeSprite) {
            tex = (i < (int)actorTex.size()) ? actorTex[i] : nullptr;
        } else if (a.type == CsActorType::Player) {
            continue;  // driven by the real player; see game.cpp updateStoryCutscene
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
}

// Small text helper for the dialog box: renders via the shared Assets font.
// Returns the rendered height (0 if nothing drawn).
static int dlgText(SDL_Renderer* r, const char* text, int x, int y, int size,
                   SDL_Color c, int wrapW = 0) {
    TTF_Font* f = Assets::instance().font(size);
    if (!f || !text || !text[0]) return 0;
    SDL_Surface* surf = (wrapW > 0)
        ? TTF_RenderText_Blended_Wrapped(f, text, c, wrapW)
        : TTF_RenderText_Blended(f, text, c);
    if (!surf) return 0;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surf);
    int h = surf->h;
    if (t) {
        SDL_Rect dst = { x, y, surf->w, surf->h };
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(surf);
    return h;
}

void cutsceneRenderDialogBox(SDL_Renderer* r, int rx, int ry, int rw, int rh,
                             const CsDialogLine& line, int visibleChars,
                             bool lineComplete, int hoveredChoice) {
    const int PAD     = 14;
    const int PORT_SZ = (rh < 360) ? 96 : 128;
    const int BAR_H   = PORT_SZ + 2 * 16;
    int barY = ry + rh - BAR_H;

    // Bar background + accent
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 6, 8, 14, 225);
    SDL_Rect bar = { rx, barY, rw, BAR_H };
    SDL_RenderFillRect(r, &bar);
    SDL_SetRenderDrawColor(r, 0, 200, 180, 200);
    SDL_RenderDrawLine(r, rx, barY, rx + rw, barY);
    SDL_SetRenderDrawColor(r, 0, 90, 80, 160);
    SDL_RenderDrawLine(r, rx, barY + 1, rx + rw, barY + 1);

    // Portrait (or placeholder so the layout is always visible in the editor)
    int textX = rx + PAD;
    int textRight = rx + rw - PAD;
    bool hasPort = !line.portrait.empty();
    if (hasPort) {
        int px = line.portraitLeft ? (rx + PAD) : (rx + rw - PAD - PORT_SZ);
        SDL_Rect pr = { px, barY + (BAR_H - PORT_SZ) / 2, PORT_SZ, PORT_SZ };
        SDL_Texture* port = Assets::instance().loadRelTex(line.portrait);
        if (port) {
            SDL_SetTextureBlendMode(port, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(port, 255, 255, 255);
            SDL_SetTextureAlphaMod(port, 255);
            SDL_RenderCopy(r, port, nullptr, &pr);
        } else {
            SDL_SetRenderDrawColor(r, 30, 34, 46, 255);
            SDL_RenderFillRect(r, &pr);
            dlgText(r, "(no portrait)", pr.x + 8, pr.y + PORT_SZ / 2 - 8, 12, {130, 135, 150, 255});
        }
        SDL_SetRenderDrawColor(r, 0, 200, 180, 220);
        SDL_RenderDrawRect(r, &pr);
        if (line.portraitLeft) textX     = px + PORT_SZ + PAD;
        else                   textRight = px - PAD;
    }
    int textW = textRight - textX;
    if (textW < 40) textW = 40;

    // Speaker name
    int ty = barY + 12;
    if (!line.character.empty())
        ty += dlgText(r, line.character.c_str(), textX, ty, 20, {0, 255, 228, 255}) + 4;
    else
        ty += 6;

    // Typed text (typewriter when visibleChars >= 0)
    std::string shown = line.text;
    if (visibleChars >= 0 && visibleChars < (int)shown.size())
        shown = shown.substr(0, visibleChars);
    if (!shown.empty())
        dlgText(r, shown.c_str(), textX, ty, 18, {235, 235, 242, 255}, textW);

    // Choices (after the line finishes typing)
    if (lineComplete && !line.choices.empty()) {
        const int CH_H = 26, CH_W = std::min(420, rw - 2 * PAD);
        int n = (int)line.choices.size();
        int cx = rx + (rw - CH_W) / 2;
        int cy = barY - n * (CH_H + 4) - 8;
        for (int i = 0; i < n; i++) {
            SDL_Rect cr = { cx, cy + i * (CH_H + 4), CH_W, CH_H };
            bool hov = (i == hoveredChoice);
            SDL_SetRenderDrawColor(r, hov ? 60 : 18, hov ? 80 : 22, hov ? 120 : 34, 235);
            SDL_RenderFillRect(r, &cr);
            SDL_SetRenderDrawColor(r, 90, 110, 170, 255);
            SDL_RenderDrawRect(r, &cr);
            dlgText(r, line.choices[i].text.c_str(), cr.x + 10, cr.y + 5, 16,
                    hov ? SDL_Color{255, 255, 255, 255} : SDL_Color{200, 205, 220, 255});
        }
    } else if (lineComplete) {
        // Blinking advance arrow
        if ((SDL_GetTicks() / 400) % 2 == 0) {
            SDL_SetRenderDrawColor(r, 220, 220, 80, 255);
            int ix = rx + rw - 28, iy = barY + BAR_H - 18;
            SDL_RenderDrawLine(r, ix, iy - 8, ix + 14, iy);
            SDL_RenderDrawLine(r, ix, iy + 8, ix + 14, iy);
            SDL_RenderDrawLine(r, ix, iy - 8, ix, iy + 8);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void CutscenePlayback::renderDialogLine(SDL_Renderer* r,
                                         const CsDialogLine& line,
                                         int visibleChars,
                                         int screenW, int screenH,
                                         const CutsceneLibrary& /*lib*/) const {
    // Render only the choices the game flagged visible (variable-conditional
    // branching). hoveredChoice indexes the visible list. Empty list => show all.
    if (!line.choices.empty() && !dialog.visibleChoices.empty()) {
        CsDialogLine filtered = line;
        filtered.choices.clear();
        for (int idx : dialog.visibleChoices)
            if (idx >= 0 && idx < (int)line.choices.size())
                filtered.choices.push_back(line.choices[idx]);
        cutsceneRenderDialogBox(r, 0, 0, screenW, screenH, filtered, visibleChars,
                                dialog.lineComplete, dialog.hoveredChoice);
        return;
    }
    cutsceneRenderDialogBox(r, 0, 0, screenW, screenH, line, visibleChars,
                            dialog.lineComplete, dialog.hoveredChoice);
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

    // Dialog (the shared box renderer draws the bar, portrait, speaker, typed
    // text, choices and advance arrow).
    if (dialog.active && dialog.seq &&
        dialog.lineIdx < (int)dialog.seq->lines.size()) {
        const auto& line = dialog.seq->lines[dialog.lineIdx];
        renderDialogLine(r, line, dialog.visibleChars, screenW, screenH, lib);
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
