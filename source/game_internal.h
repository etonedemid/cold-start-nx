#pragma once
// Included only by game implementation files, not external consumers.
#include "game.h"
#include <SDL2/SDL_mixer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
inline int mkdir(const char* path, int /*mode*/) { return _mkdir(path); }
#elif defined(__SWITCH__)
#include <switch.h>
#endif

// On Nintendo hardware, A/B and X/Y are physically swapped vs Xbox layout.
// Switch (libnx SDL) reports position-based buttons, so we swap. Wii U's
// wiiu-sdl2 already maps by Nintendo label (SDL_GAMECONTROLLER_USE_BUTTON_LABELS
// defaults on: a:b0 = the A-labeled button), so swapping there double-flips and
// puts confirm on B - do NOT swap on Wii U. (matches editor.cpp/texeditor.cpp)
inline Uint8 remapButton(Uint8 btn) {
#if defined(__SWITCH__)
    switch (btn) {
        case SDL_CONTROLLER_BUTTON_A: return SDL_CONTROLLER_BUTTON_B;
        case SDL_CONTROLLER_BUTTON_B: return SDL_CONTROLLER_BUTTON_A;
        case SDL_CONTROLLER_BUTTON_X: return SDL_CONTROLLER_BUTTON_Y;
        case SDL_CONTROLLER_BUTTON_Y: return SDL_CONTROLLER_BUTTON_X;
        default: return btn;
    }
#else
    return btn;
#endif
}

inline bool hasSuffix(const std::string& value, const char* suffix) {
    size_t suffixLen = strlen(suffix);
    return value.size() >= suffixLen &&
           value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

inline bool isAllowedSyncedCharacterFile(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return false;
    if (name == "." || name == "..") return false;
    return hasSuffix(name, ".png") || hasSuffix(name, ".cfg") || hasSuffix(name, ".cschar");
}

inline std::string sanitizeNetCharacterName(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              c == '_' || c == '-')) {
            c = '_';
        }
    }
    if (out.empty()) out = "character";
    return out;
}

#ifdef __SWITCH__
inline HidVibrationValue makeSwitchVibrationValue(float strength, float lowBandScale, float highBandScale) {
    strength       = std::clamp(strength,       0.0f, 1.0f);
    lowBandScale   = std::clamp(lowBandScale,   0.2f, 1.8f);
    highBandScale  = std::clamp(highBandScale,  0.2f, 1.8f);

    HidVibrationValue value{};
    value.amp_low   = std::clamp((0.16f + strength * 0.74f) * lowBandScale,   0.0f, 1.0f);
    value.freq_low  = std::clamp(85.0f  + strength * 65.0f  - (lowBandScale  - 1.0f) * 20.0f, 40.0f, 160.0f);
    value.amp_high  = std::clamp((0.08f + strength * 0.88f) * highBandScale,  0.0f, 1.0f);
    value.freq_high = std::clamp(180.0f + strength * 135.0f + (highBandScale - 1.0f) * 80.0f, 120.0f, 640.0f);
    return value;
}

inline void appendSwitchVibrationHandles(std::vector<HidVibrationDeviceHandle>& handles,
                                         HidNpadIdType id, HidNpadStyleTag style, s32 count) {
    HidVibrationDeviceHandle tmp[2] = {};
    if (R_SUCCEEDED(hidInitializeVibrationDevices(tmp, count, id, style)))
        handles.insert(handles.end(), tmp, tmp + count);
}

inline void sendSwitchVibrationNow(const HidVibrationValue& value) {
    static const HidNpadIdType ids[] = {
        HidNpadIdType_Handheld, HidNpadIdType_No1, HidNpadIdType_No2,
        HidNpadIdType_No3, HidNpadIdType_No4,
    };
    std::vector<HidVibrationDeviceHandle> handles;
    handles.reserve(10);
    for (HidNpadIdType id : ids) {
        u32 styles = hidGetNpadStyleSet(id);
        if (styles & HidNpadStyleTag_NpadHandheld) appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadHandheld, 2);
        if (styles & HidNpadStyleTag_NpadJoyDual)  appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadJoyDual,  2);
        if (styles & HidNpadStyleTag_NpadFullKey)  appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadFullKey,  2);
        if (styles & HidNpadStyleTag_NpadJoyLeft)  appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadJoyLeft,  1);
        if (styles & HidNpadStyleTag_NpadJoyRight) appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadJoyRight, 1);
    }
    if (handles.empty()) return;
    std::vector<HidVibrationValue> values(handles.size(), value);
    hidPermitVibration(true);
    hidSendVibrationValues(handles.data(), values.data(), (s32)handles.size());
}
#endif // __SWITCH__

inline bool isMeleeEnemyType(EnemyType type) {
    return type == EnemyType::Melee || type == EnemyType::Brute || type == EnemyType::Scout
        || type == EnemyType::BossBrute;
}

inline bool isShooterEnemyType(EnemyType type) {
    return type == EnemyType::Shooter || type == EnemyType::Sniper || type == EnemyType::Gunner
        || type == EnemyType::BossSniper || type == EnemyType::BossGunner;
}

inline bool isBossType(EnemyType type) {
    return type == EnemyType::BossBrute || type == EnemyType::BossSniper || type == EnemyType::BossGunner;
}

// Play a sound with a random ±8% pitch shift. Cleans up after itself.
void playSFX(Mix_Chunk* chunk, int volume);
void initPitchSFX();  // call once after Mix_AllocateChannels

inline bool isCrateSpawnType(uint8_t type) {
    return type == ENTITY_CRATE || type == ENTITY_UPGRADE_CRATE;
}

// Story bystander classification (these ride the EnemySpawn array but are not enemies)
inline bool isCivilianSpawn(uint8_t type)   { return type == ENTITY_CIVILIAN; }
inline bool isResponderSpawn(uint8_t type)  { return type == ENTITY_RESPONDER; }
inline bool isInfrastructureSpawn(uint8_t type) {
    return type == ENTITY_INFRA_MEDRELAY || type == ENTITY_INFRA_POWER ||
           type == ENTITY_INFRA_WATER    || type == ENTITY_INFRA_ANTENNA;
}
inline bool isBystanderSpawn(uint8_t type) {
    return isCivilianSpawn(type) || isResponderSpawn(type) || isInfrastructureSpawn(type);
}

constexpr int CONFIG_RESOLUTION_INDEX        = -1; // removed - window is now freely resizable
constexpr int CONFIG_SHADER_CRT_INDEX        = 7;
constexpr int CONFIG_SHADER_CHROMATIC_INDEX  = 8;
constexpr int CONFIG_SHADER_SCANLINES_INDEX  = 9;
constexpr int CONFIG_SHADER_GLOW_INDEX       = 10;
constexpr int CONFIG_SHADER_GLITCH_INDEX     = 11;
constexpr int CONFIG_SHADER_NEON_INDEX       = 12;
constexpr int CONFIG_SAVE_INCOMING_MODS_INDEX = 13;
constexpr int CONFIG_USERNAME_INDEX          = 14;
constexpr int CONFIG_UI_SCALE_INDEX          = 15;
constexpr int CONFIG_SHAKE_INDEX             = 17;
constexpr int CONFIG_BACK_INDEX              = 16;

inline EnemyType enemyTypeFromSpawnId(uint8_t type) {
    switch (type) {
        case ENTITY_SHOOTER: return EnemyType::Shooter;
        case ENTITY_BRUTE:   return EnemyType::Brute;
        case ENTITY_SCOUT:   return EnemyType::Scout;
        case ENTITY_SNIPER:  return EnemyType::Sniper;
        case ENTITY_GUNNER:  return EnemyType::Gunner;
        case ENTITY_MELEE:
        default:             return EnemyType::Melee;
    }
}

inline SDL_Color enemyBaseTint(EnemyType type) {
    switch (type) {
        case EnemyType::Brute:      return {210, 110, 110, 255};
        case EnemyType::Scout:      return {255, 150, 200, 255};
        case EnemyType::Sniper:     return {190, 160, 255, 255};
        case EnemyType::Gunner:     return {255, 225, 140, 255};
        case EnemyType::Shooter:    return {255, 235, 210, 255};
        case EnemyType::BossBrute:  return {255,  40,  40, 255};
        case EnemyType::BossSniper: return { 40, 220, 255, 255};
        case EnemyType::BossGunner: return {255, 190,  20, 255};
        case EnemyType::Melee:
        default:                    return {255, 255, 255, 255};
    }
}

inline EnemyType rollWaveEnemyType() {
    const int totalWeight =
        WAVE_MELEE_WEIGHT + WAVE_SHOOTER_WEIGHT + WAVE_BRUTE_WEIGHT +
        WAVE_SCOUT_WEIGHT + WAVE_SNIPER_WEIGHT  + WAVE_GUNNER_WEIGHT;
    int roll = rand() % totalWeight;
    if ((roll -= WAVE_MELEE_WEIGHT)   < 0) return EnemyType::Melee;
    if ((roll -= WAVE_SHOOTER_WEIGHT) < 0) return EnemyType::Shooter;
    if ((roll -= WAVE_BRUTE_WEIGHT)   < 0) return EnemyType::Brute;
    if ((roll -= WAVE_SCOUT_WEIGHT)   < 0) return EnemyType::Scout;
    if ((roll -= WAVE_SNIPER_WEIGHT)  < 0) return EnemyType::Sniper;
    return EnemyType::Gunner;
}

inline bool sweptCircleOverlap(Vec2 curPos, Vec2 vel, float backtrackSec, Vec2 center, float radius) {
    Vec2  start    = curPos - vel * backtrackSec;
    Vec2  seg      = curPos - start;
    float segLenSq = seg.x * seg.x + seg.y * seg.y;
    if (segLenSq <= 0.0001f) return circleOverlap(curPos, 0.0f, center, radius);
    Vec2  toCenter = center - start;
    float t        = std::clamp((toCenter.x * seg.x + toCenter.y * seg.y) / segLenSq, 0.0f, 1.0f);
    Vec2  closest  = start + seg * t;
    float dx = closest.x - center.x, dy = closest.y - center.y;
    return (dx * dx + dy * dy) <= (radius * radius);
}

inline float getMeleeRange(const Player& p, const PlayerUpgrades& u) {
    float range = MELEE_RANGE + u.meleeRangeBonus;
    if (u.speedBonus > 0.0f) range += std::min(18.0f, u.speedBonus * 0.08f);
    if (p.speed > PLAYER_SPEED) range += std::min(14.0f, (p.speed - PLAYER_SPEED) * 0.04f);
    return range;
}

inline float getMeleeArc(const PlayerUpgrades& u) {
    return std::min(2.30f, MELEE_ARC + u.meleeArcBonus);
}

inline int getMeleePlayerDamage(const PlayerUpgrades& u) {
    int dmg = MELEE_PLAYER_DAMAGE + u.meleeDamageBonus;
    dmg += std::max(0, (int)floorf((u.damageMulti - 1.0f) * 20.0f));
    return std::min(80, std::max(1, dmg));
}

inline float getMeleeCooldownTime(const PlayerUpgrades& u) {
    return std::max(0.16f, MELEE_COOLDOWN_TIME * u.meleeCooldownMulti);
}
