// ─── game.cpp ─── Main game implementation ──────────────────────────────────
#include "game.h"
#include "update_checker.h"
#include "discord_rpc.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <set>
#include <dirent.h>
#include <sys/stat.h>
#include <thread>
#ifdef _WIN32
#  include <direct.h>
#  define mkdir(p, m) _mkdir(p)
#endif

#ifdef __SWITCH__
#include <switch.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace {
// On Switch, A/B and X/Y are physically swapped compared to Xbox layout
inline Uint8 remapButton(Uint8 btn) {
#ifdef __SWITCH__
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

static bool hasSuffix(const std::string& value, const char* suffix) {
    size_t suffixLen = strlen(suffix);
    return value.size() >= suffixLen &&
           value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

static bool isAllowedSyncedCharacterFile(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return false;
    if (name == "." || name == "..") return false;
    return hasSuffix(name, ".png") || hasSuffix(name, ".cfg") || hasSuffix(name, ".cschar");
}

static std::string sanitizeNetCharacterName(const std::string& name) {
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
static HidVibrationValue makeSwitchVibrationValue(float strength, float lowBandScale, float highBandScale) {
    strength = std::clamp(strength, 0.0f, 1.0f);
    lowBandScale = std::clamp(lowBandScale, 0.2f, 1.8f);
    highBandScale = std::clamp(highBandScale, 0.2f, 1.8f);

    HidVibrationValue value{};
    value.amp_low = std::clamp((0.16f + strength * 0.74f) * lowBandScale, 0.0f, 1.0f);
    value.freq_low = std::clamp(85.0f + strength * 65.0f - (lowBandScale - 1.0f) * 20.0f, 40.0f, 160.0f);
    value.amp_high = std::clamp((0.08f + strength * 0.88f) * highBandScale, 0.0f, 1.0f);
    value.freq_high = std::clamp(180.0f + strength * 135.0f + (highBandScale - 1.0f) * 80.0f, 120.0f, 640.0f);
    return value;
}

static void appendSwitchVibrationHandles(std::vector<HidVibrationDeviceHandle>& handles,
                                         HidNpadIdType id, HidNpadStyleTag style, s32 count) {
    HidVibrationDeviceHandle tmp[2] = {};
    if (R_SUCCEEDED(hidInitializeVibrationDevices(tmp, count, id, style))) {
        handles.insert(handles.end(), tmp, tmp + count);
    }
}

static void sendSwitchVibrationNow(const HidVibrationValue& value) {
    static const HidNpadIdType ids[] = {
        HidNpadIdType_Handheld,
        HidNpadIdType_No1,
        HidNpadIdType_No2,
        HidNpadIdType_No3,
        HidNpadIdType_No4,
    };

    std::vector<HidVibrationDeviceHandle> handles;
    handles.reserve(10);

    for (HidNpadIdType id : ids) {
        u32 styles = hidGetNpadStyleSet(id);
        if (styles & HidNpadStyleTag_NpadHandheld)  appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadHandheld, 2);
        if (styles & HidNpadStyleTag_NpadJoyDual)   appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadJoyDual, 2);
        if (styles & HidNpadStyleTag_NpadFullKey)   appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadFullKey, 2);
        if (styles & HidNpadStyleTag_NpadJoyLeft)   appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadJoyLeft, 1);
        if (styles & HidNpadStyleTag_NpadJoyRight)  appendSwitchVibrationHandles(handles, id, HidNpadStyleTag_NpadJoyRight, 1);
    }

    if (handles.empty()) return;

    std::vector<HidVibrationValue> values(handles.size(), value);
    hidPermitVibration(true);
    hidSendVibrationValues(handles.data(), values.data(), (s32)handles.size());
}
#endif

bool isMeleeEnemyType(EnemyType type) {
    return type == EnemyType::Melee || type == EnemyType::Brute || type == EnemyType::Scout;
}

bool isShooterEnemyType(EnemyType type) {
    return type == EnemyType::Shooter || type == EnemyType::Sniper || type == EnemyType::Gunner;
}

bool isCrateSpawnType(uint8_t type) {
    return type == ENTITY_CRATE || type == ENTITY_UPGRADE_CRATE;
}

struct ResolutionPreset {
    int w;
    int h;
    const char* label;
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {1280,  720,  "1280x720"},
    {1600,  900,  "1600x900"},
    {1920, 1080,  "1920x1080"},
    {2560, 1440,  "2560x1440"},
};

int findResolutionPresetIndex(int w, int h) {
    for (int i = 0; i < (int)(sizeof(kResolutionPresets) / sizeof(kResolutionPresets[0])); ++i) {
        if (kResolutionPresets[i].w == w && kResolutionPresets[i].h == h) return i;
    }

    int bestIdx = 0;
    int bestScore = 1 << 30;
    for (int i = 0; i < (int)(sizeof(kResolutionPresets) / sizeof(kResolutionPresets[0])); ++i) {
        int score = std::abs(kResolutionPresets[i].w - w) + std::abs(kResolutionPresets[i].h - h);
        if (score < bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }
    return bestIdx;
}

void clampResolutionConfig(GameConfig& config) {
    int idx = findResolutionPresetIndex(config.windowWidth, config.windowHeight);
    config.windowWidth = kResolutionPresets[idx].w;
    config.windowHeight = kResolutionPresets[idx].h;
}

constexpr int CONFIG_RESOLUTION_INDEX = 6;
constexpr int CONFIG_SHADER_CRT_INDEX = 7;
constexpr int CONFIG_SHADER_CHROMATIC_INDEX = 8;
constexpr int CONFIG_SHADER_SCANLINES_INDEX = 9;
constexpr int CONFIG_SHADER_GLOW_INDEX = 10;
constexpr int CONFIG_SHADER_GLITCH_INDEX = 11;
constexpr int CONFIG_SHADER_NEON_INDEX = 12;
constexpr int CONFIG_SAVE_INCOMING_MODS_INDEX = 13;
constexpr int CONFIG_USERNAME_INDEX = 14;
constexpr int CONFIG_BACK_INDEX = 15;

EnemyType enemyTypeFromSpawnId(uint8_t type) {
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

SDL_Color enemyBaseTint(EnemyType type) {
    switch (type) {
        case EnemyType::Brute:   return {210, 110, 110, 255};
        case EnemyType::Scout:   return {255, 150, 200, 255};
        case EnemyType::Sniper:  return {190, 160, 255, 255};
        case EnemyType::Gunner:  return {255, 225, 140, 255};
        case EnemyType::Shooter: return {255, 235, 210, 255};
        case EnemyType::Melee:
        default:                 return {255, 255, 255, 255};
    }
}

EnemyType rollWaveEnemyType() {
    const int totalWeight =
        WAVE_MELEE_WEIGHT + WAVE_SHOOTER_WEIGHT + WAVE_BRUTE_WEIGHT +
        WAVE_SCOUT_WEIGHT + WAVE_SNIPER_WEIGHT + WAVE_GUNNER_WEIGHT;

    int roll = rand() % totalWeight;
    if ((roll -= WAVE_MELEE_WEIGHT) < 0)   return EnemyType::Melee;
    if ((roll -= WAVE_SHOOTER_WEIGHT) < 0) return EnemyType::Shooter;
    if ((roll -= WAVE_BRUTE_WEIGHT) < 0)   return EnemyType::Brute;
    if ((roll -= WAVE_SCOUT_WEIGHT) < 0)   return EnemyType::Scout;
    if ((roll -= WAVE_SNIPER_WEIGHT) < 0)  return EnemyType::Sniper;
    return EnemyType::Gunner;
}

bool sweptCircleOverlap(Vec2 curPos, Vec2 vel, float backtrackSec, Vec2 center, float radius) {
    Vec2 start = curPos - vel * backtrackSec;
    Vec2 end = curPos;
    Vec2 seg = end - start;
    float segLenSq = seg.x * seg.x + seg.y * seg.y;
    if (segLenSq <= 0.0001f) return circleOverlap(curPos, 0.0f, center, radius);

    Vec2 toCenter = center - start;
    float t = (toCenter.x * seg.x + toCenter.y * seg.y) / segLenSq;
    t = std::clamp(t, 0.0f, 1.0f);
    Vec2 closest = start + seg * t;
    float dx = closest.x - center.x;
    float dy = closest.y - center.y;
    return (dx * dx + dy * dy) <= (radius * radius);
}

float getMeleeRange(const Player& p, const PlayerUpgrades& u) {
    float range = MELEE_RANGE + u.meleeRangeBonus;
    if (u.speedBonus > 0.0f) range += std::min(18.0f, u.speedBonus * 0.08f);
    if (p.speed > PLAYER_SPEED) range += std::min(14.0f, (p.speed - PLAYER_SPEED) * 0.04f);
    return range;
}

float getMeleeArc(const PlayerUpgrades& u) {
    return std::min(2.30f, MELEE_ARC + u.meleeArcBonus);
}

int getMeleePlayerDamage(const PlayerUpgrades& u) {
    int dmg = MELEE_PLAYER_DAMAGE + u.meleeDamageBonus;
    dmg += std::max(0, (int)floorf((u.damageMulti - 1.0f) * 2.0f));
    return std::min(8, std::max(1, dmg));
}

float getMeleeCooldownTime(const PlayerUpgrades& u) {
    return std::max(0.16f, MELEE_COOLDOWN_TIME * u.meleeCooldownMulti);
}
}

bool Game::rebuildScreenTextures() {
    if (!renderer_) return false;

    if (sceneTarget_) {
        SDL_DestroyTexture(sceneTarget_);
        sceneTarget_ = nullptr;
    }
    if (vignetteTex_) {
        SDL_DestroyTexture(vignetteTex_);
        vignetteTex_ = nullptr;
    }
    if (minimapCacheTex_) {
        SDL_DestroyTexture(minimapCacheTex_);
        minimapCacheTex_ = nullptr;
    }
    minimapCacheDirty_ = true;
    minimapCacheMapW_ = 0;
    minimapCacheMapH_ = 0;
    minimapCacheTilePx_ = 0;

    // Use base viewport dimensions for render targets
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_RGBA8888);
    if (surf) {
        SDL_LockSurface(surf);
        Uint32* pixels = (Uint32*)surf->pixels;
        float cx = SCREEN_W / 2.0f;
        float cy = SCREEN_H / 2.0f;
        for (int py = 0; py < SCREEN_H; py++) {
            for (int px = 0; px < SCREEN_W; px++) {
                float dx = (px - cx) / cx;
                float dy = (py - cy) / cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float t = (dist - 0.4f) / 0.9f;
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                t = t * t;
                Uint8 alpha = (Uint8)(t * 120);
                pixels[py * (surf->pitch / 4) + px] = SDL_MapRGBA(surf->format, 0, 0, 0, alpha);
            }
        }
        SDL_UnlockSurface(surf);
        vignetteTex_ = SDL_CreateTextureFromSurface(renderer_, surf);
        if (vignetteTex_) SDL_SetTextureBlendMode(vignetteTex_, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(surf);
    }

    sceneTarget_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    if (sceneTarget_) SDL_SetTextureBlendMode(sceneTarget_, SDL_BLENDMODE_BLEND);

    return sceneTarget_ != nullptr;
}

void Game::applyResolutionSettings(bool rebuildTargets) {
#ifdef __SWITCH__
    // On Switch: use fixed 720p resolution
    config_.windowWidth = 1280;
    config_.windowHeight = 720;
#else
    clampResolutionConfig(config_);
#endif

    // Camera viewport is ALWAYS the base resolution (SCREEN_W x SCREEN_H) for consistent gameplay
    camera_.viewW = SCREEN_W;
    camera_.viewH = SCREEN_H;
    editor_.setScreenSize(SCREEN_W, SCREEN_H);

    if (window_ && !config_.fullscreen) {
        SDL_SetWindowSize(window_, config_.windowWidth, config_.windowHeight);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    if (renderer_) {
        // Logical size is always SCREEN_W x SCREEN_H - SDL scales to window size
        SDL_RenderSetLogicalSize(renderer_, SCREEN_W, SCREEN_H);
        if (rebuildTargets) rebuildScreenTextures();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Init / Shutdown
// ═════════════════════════════════════════════════════════════════════════════

void Game::configureDedicatedServer(uint16_t port, int maxPlayers,
                                    const std::string& password,
                                    const std::string& serverName) {
    dedicatedMode_ = true;
    dedicatedPort_ = port;
    dedicatedMaxPlayers_ = std::max(2, std::min(128, maxPlayers));
    dedicatedPassword_ = password;
    dedicatedServerName_ = serverName.empty() ? "DedicatedServer" : serverName;
}

bool Game::init() {
    srand((unsigned)time(nullptr));

#ifdef __SWITCH__
    romfsInit();
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() < 0) {
        printf("TTF_Init: %s\n", TTF_GetError());
        return false;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        printf("Mix_OpenAudio: %s\n", Mix_GetError());
        // Non-fatal: continue without audio
    }
    Mix_AllocateChannels(32);

    loadConfig();
    applyResolutionSettings(false);

    char windowTitle[64];
    snprintf(windowTitle, sizeof(windowTitle), "COLD START v%s", GAME_VERSION);

    window_ = SDL_CreateWindow(windowTitle,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
#ifdef __SWITCH__
        0
#else
        0
#endif
    );
    if (!window_) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return false; }

#ifdef __SWITCH__
    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#else
    // PC: respect SDL_RENDER_DRIVER from environment when provided.
    // Otherwise let SDL auto-select an accelerated backend.
    // FIX: Set scale quality BEFORE renderer creation to ensure nearest-neighbor
    //       filtering is used for ALL textures including rotated ones.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    // RENDER_BATCHING is disabled in main() before SDL_Init; do NOT re-enable it here.
    // Batching causes vertex collapse on Linux OpenGL drivers with SDL_RenderCopyExF.

    const char* envDriver = getenv("SDL_RENDER_DRIVER");
    if (envDriver && envDriver[0]) {
        printf("PC renderer request from env: %s\n", envDriver);
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_) {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer_, &info) == 0)
            printf("PC renderer: %s (accelerated)\n", info.name);
        else
            printf("PC renderer: accelerated\n");
    } else {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (renderer_) {
            SDL_RendererInfo info;
            if (SDL_GetRendererInfo(renderer_, &info) == 0)
                printf("PC renderer: %s (software fallback)\n", info.name);
            else
                printf("PC renderer: software fallback\n");
        }
    }

    // Re-assert nearest-neighbor after renderer creation for texture loads
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
#endif
    if (!renderer_) { printf("SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

#ifndef __SWITCH__
    SDL_ShowCursor(SDL_DISABLE);
#endif
    // Cursor is re-enabled per-state in render() for editors that need it

    // Open all connected game controllers
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* gc = SDL_GameControllerOpen(i);
            if (gc && !activeController_) {
                activeController_ = gc;  // Cache first controller for rumble
            }
        }
    }

    Assets::instance().init(renderer_);
    ui_.init(renderer_);
    loadAssets();
    applyResolutionSettings(true);
#ifndef __SWITCH__
    if (config_.fullscreen)
        SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
    loadSavedServers();
    loadServerPresets();

    // Initialize map editor
    editor_.init(renderer_, SCREEN_W, SCREEN_H, &ui_);

    // Scan for custom characters and maps
    scanCharacters();

    // Initialize mod system
    initMods();

    // Scan map files after mods are loaded so mod maps are included
    scanMapFiles();

    // Initialize multiplayer
    initMultiplayer();

    // Check for updates in background (skip on Switch - network conflicts with nxlink)
#ifndef __SWITCH__
    checkForUpdates();
#endif

    // Start main menu music
    if (menuMusic_) {
        Mix_PlayMusic(menuMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
    }

    DiscordRPC::instance().init();
    return true;
}

void Game::playMapMusic(const std::string& folder, const std::string& trackPath) {
    Mix_HaltMusic();
    if (customMapMusic_) { Mix_FreeMusic(customMapMusic_); customMapMusic_ = nullptr; }
    if (!trackPath.empty()) {
        std::string resolved = (!folder.empty() && trackPath[0] != '/') ? folder + trackPath : trackPath;
        customMapMusic_ = Mix_LoadMUS(resolved.c_str());
        if (customMapMusic_) {
            Mix_PlayMusic(customMapMusic_, -1);
            Mix_VolumeMusic(config_.musicVolume);
            return;
        }
        printf("Warning: could not load map music: %s\n", resolved.c_str());
    }
    if (bgMusic_) { Mix_PlayMusic(bgMusic_, -1); Mix_VolumeMusic(config_.musicVolume); }
}

void Game::playMenuMusic() {
    Mix_HaltMusic();
    if (menuMusic_) {
        Mix_PlayMusic(menuMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  SoftKeyboard — centralized on-screen keyboard for all text input
// ═════════════════════════════════════════════════════════════════════════════
void Game::SoftKeyboard::open(const char* pal, int c, std::string* tgt, int max,
                              std::function<void(bool confirmed)> done) {
    active = true; palette = pal; cols = c; charIdx = 0;
    heldButton = -1; repeatAt = 0; target = tgt; maxLen = max;
    onDone = done;
    renderX = renderY = cellW = cellH = rows = 0;
    delRect = {0, 0, 0, 0};
    okRect = {0, 0, 0, 0};
    cancelRect = {0, 0, 0, 0};
#ifndef __SWITCH__
    SDL_StartTextInput();
#endif
}

void Game::SoftKeyboard::close(bool confirmed) {
    active = false;
#ifndef __SWITCH__
    SDL_StopTextInput();
#endif
    auto cb = onDone;
    target = nullptr;
    onDone = nullptr;
    heldButton = -1;
    if (cb) cb(confirmed);
}

void Game::updateSoftKBRepeat() {
    auto& kb = softKB_;
    if (!kb.active || !kb.palette || kb.heldButton < 0) return;
    if (SDL_GetTicks() < kb.repeatAt) return;
    int palLen = (int)strlen(kb.palette);
    int delta = 0;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_LEFT)  delta = -1;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) delta = +1;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_UP)    delta = -kb.cols;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_DOWN)  delta = +kb.cols;
    if (delta != 0) kb.charIdx = (kb.charIdx + delta + palLen * 4) % palLen;
    kb.repeatAt = SDL_GetTicks() + 80;
}

bool Game::handleSoftKBEvent(SDL_Event& e) {
    auto& kb = softKB_;
    if (!kb.active || !kb.target || !kb.palette) return false;
    int palLen = (int)strlen(kb.palette);

    auto logicalPointFromMouse = [&](int px, int py, int& lx, int& ly) {
        float fx = (float)px;
        float fy = (float)py;
        SDL_RenderWindowToLogical(renderer_, px, py, &fx, &fy);
        lx = (int)fx;
        ly = (int)fy;
    };

    auto handlePointer = [&](int x, int y) -> bool {
        if (kb.delRect.w > 0 && ui_.pointInRect(x, y, kb.delRect.x, kb.delRect.y, kb.delRect.w, kb.delRect.h)) {
            if (!kb.target->empty()) kb.target->pop_back();
            return true;
        }
        if (kb.okRect.w > 0 && ui_.pointInRect(x, y, kb.okRect.x, kb.okRect.y, kb.okRect.w, kb.okRect.h)) {
            kb.close(true);
            return true;
        }
        if (kb.cancelRect.w > 0 && ui_.pointInRect(x, y, kb.cancelRect.x, kb.cancelRect.y, kb.cancelRect.w, kb.cancelRect.h)) {
            kb.close(false);
            return true;
        }
        if (kb.cellW <= 0 || kb.cellH <= 0 || kb.rows <= 0) return false;
        int totalW = kb.cols * kb.cellW;
        int totalH = kb.rows * kb.cellH;
        if (!ui_.pointInRect(x, y, kb.renderX, kb.renderY, totalW, totalH)) return false;
        int col = (x - kb.renderX) / kb.cellW;
        int row = (y - kb.renderY) / kb.cellH;
        int idx = row * kb.cols + col;
        if (idx < 0 || idx >= palLen) return true;
        kb.charIdx = idx;
        if ((int)kb.target->size() < kb.maxLen) *kb.target += kb.palette[idx];
        return true;
    };

    if (e.type == SDL_TEXTINPUT) {
        for (const char* p = e.text.text; *p; p++) {
            if (*p >= ' ' && *p <= '~' && *p != '=' && *p != '\n')
                if ((int)kb.target->size() < kb.maxLen) *kb.target += *p;
        }
        return true;
    }
    if (e.type == SDL_KEYDOWN) {
        auto sym = e.key.keysym.sym;
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) { kb.close(true); return true; }
        if (sym == SDLK_ESCAPE) { kb.close(false); return true; }
        if (sym == SDLK_BACKSPACE && !kb.target->empty()) { kb.target->pop_back(); return true; }
        // Ctrl+V paste
        if (sym == SDLK_v && (e.key.keysym.mod & KMOD_CTRL)) {
            if (SDL_HasClipboardText()) {
                char* clip = SDL_GetClipboardText();
                if (clip) {
                    for (const char* p = clip; *p; p++) {
                        if (*p >= ' ' && *p <= '~' && *p != '=' && *p != '\n')
                            if ((int)kb.target->size() < kb.maxLen) *kb.target += *p;
                    }
                    SDL_free(clip);
                }
            }
            return true;
        }
        return true; // consume all keydowns while keyboard active
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int x = 0, y = 0;
        logicalPointFromMouse(e.button.x, e.button.y, x, y);
        return handlePointer(x, y);
    }
    if (e.type == SDL_FINGERDOWN) {
        int x = (int)(e.tfinger.x * SCREEN_W);
        int y = (int)(e.tfinger.y * SCREEN_H);
        return handlePointer(x, y);
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = remapButton(e.cbutton.button);
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                kb.charIdx = (kb.charIdx - 1 + palLen) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                kb.charIdx = (kb.charIdx + 1) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                kb.charIdx = (kb.charIdx - kb.cols + palLen) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                kb.charIdx = (kb.charIdx + kb.cols) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_A:
                if ((int)kb.target->size() < kb.maxLen) *kb.target += kb.palette[kb.charIdx];
                break;
            case SDL_CONTROLLER_BUTTON_Y:
                if (!kb.target->empty()) kb.target->pop_back();
                break;
            case SDL_CONTROLLER_BUTTON_X:
            case SDL_CONTROLLER_BUTTON_START:
                kb.close(true);
                break;
            case SDL_CONTROLLER_BUTTON_B:
                kb.close(false);
                break;
        }
        return true;
    }
    if (e.type == SDL_CONTROLLERBUTTONUP) {
        Uint8 btn = remapButton(e.cbutton.button);
        if (btn == (Uint8)kb.heldButton) kb.heldButton = -1;
        return true;
    }
    return false;
}

void Game::renderSoftKB(int centerY) {
#ifndef __SWITCH__
    if (!usingGamepad_ && !ui_.touchActive) return;
#endif
    auto& kb = softKB_;
    if (!kb.active || !kb.palette) return;
    int palLen = (int)strlen(kb.palette);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {120, 120, 130, 255};

    bool singleRow = (palLen <= kb.cols);
    int cellW = singleRow ? 36 : 20;
    int cellH = singleRow ? 36 : 26;
    int cols = kb.cols;
    int rows = (palLen + cols - 1) / cols;
    int totalW = cols * cellW;
    int startX = (SCREEN_W - totalW) / 2;
    int palY = centerY;
    kb.renderX = startX;
    kb.renderY = palY;
    kb.cellW = cellW;
    kb.cellH = cellH;
    kb.rows = rows;

    // Opaque background
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 15, 16, 28, 255);
    SDL_Rect palBg = {startX - 8, palY - 8, totalW + 16, rows * cellH + 80};
    SDL_RenderFillRect(renderer_, &palBg);
    SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 180);
    SDL_RenderDrawRect(renderer_, &palBg);

    for (int i = 0; i < palLen; i++) {
        int col = i % cols, row = i / cols;
        int cx = startX + col * cellW;
        int cy = palY + row * cellH;
        bool sel = (i == kb.charIdx);
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 255);
            SDL_Rect bg = {cx, cy, cellW - 2, cellH - 2};
            SDL_RenderFillRect(renderer_, &bg);
        }
        char ch[2] = { kb.palette[i], 0 };
        int fontSize = singleRow ? (sel ? 22 : 18) : (sel ? 18 : 14);
        int ox = singleRow ? 10 : 4;
        int oy = singleRow ? 6 : 3;
        drawText(ch, cx + ox, cy + oy, fontSize, sel ? white : gray);
    }

    int btnY = palY + rows * cellH + 12;
    auto drawKbButton = [&](SDL_Rect rect, const char* label, SDL_Color color) {
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, rect.x, rect.y, rect.w, rect.h);
        SDL_SetRenderDrawColor(renderer_, 18, 24, 34, 255);
        SDL_RenderFillRect(renderer_, &rect);
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, hovered ? 220 : 170);
        SDL_RenderDrawRect(renderer_, &rect);
        drawText(label, rect.x + 12, rect.y + 6, 14, color);
    };
    kb.delRect = {startX, btnY, 88, 28};
    kb.okRect = {startX + totalW / 2 - 44, btnY, 88, 28};
    kb.cancelRect = {startX + totalW - 88, btnY, 88, 28};
    drawKbButton(kb.delRect, "DEL", UI::Color::Yellow);
    drawKbButton(kb.okRect, "OK", UI::Color::Green);
    drawKbButton(kb.cancelRect, "CANCEL", {255, 120, 120, 255});

    { UI::HintPair hints[] = { {UI::Action::Navigate, "Navigate"}, {UI::Action::Confirm, "Insert"}, {UI::Action::Tab, "Delete"}, {UI::Action::Back, "Close"}, {UI::Action::Bomb, "Confirm"} };
      ui_.drawHintBar(hints, 5, palY + rows * cellH + 8); }
}

void Game::shutdown() {
    shutdownMultiplayer();
    editor_.shutdown();
    for (auto& cd : availableChars_) cd.unload();
    clearSyncedCharacters();
    ui_.shutdown();
    Assets::instance().shutdown();
    if (sceneTarget_) SDL_DestroyTexture(sceneTarget_);
    if (vignetteTex_) SDL_DestroyTexture(vignetteTex_);
    if (minimapCacheTex_) SDL_DestroyTexture(minimapCacheTex_);
    Mix_HaltMusic();
    if (customMapMusic_) { Mix_FreeMusic(customMapMusic_); customMapMusic_ = nullptr; }
    Mix_CloseAudio();
    TTF_Quit();
    DiscordRPC::instance().shutdown();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
#ifdef __SWITCH__
    romfsExit();
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
//  Controller Rumble
// ═════════════════════════════════════════════════════════════════════════════

void Game::rumble(float strength, int durationMs) {
    rumbleForSlot(activeLocalPlayerSlot_, strength, durationMs, 1.0f, 1.0f);
}

void Game::rumble(float strength, int durationMs, float lowBandScale, float highBandScale) {
    rumbleForSlot(activeLocalPlayerSlot_, strength, durationMs, lowBandScale, highBandScale);
}

SDL_GameController* Game::getRumbleControllerForSlot(int slot) const {
    if (slot > 0 && slot < 4 && coopSlots_[slot].joined && coopSlots_[slot].joyInstanceId >= 0) {
        if (SDL_GameController* gc = SDL_GameControllerFromInstanceID(coopSlots_[slot].joyInstanceId)) {
            return gc;
        }
    }

    if (slot == 0) {
        if (SDL_GameController* gc = getPrimaryGameplayController()) return gc;
        if (activeController_) return activeController_;
    }

    return nullptr;
}

void Game::rumbleForSlot(int slot, float strength, int durationMs, float lowBandScale, float highBandScale) {
    strength = std::clamp(strength, 0.0f, 1.0f);
    lowBandScale = std::clamp(lowBandScale, 0.2f, 1.8f);
    highBandScale = std::clamp(highBandScale, 0.2f, 1.8f);
    if (strength <= 0.0f || durationMs <= 0) return;

    SDL_GameController* targetController = getRumbleControllerForSlot(slot);

    // Find a fallback active controller if we don't have a slot-specific one cached
    if (!targetController && !activeController_) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(i);
                activeController_ = SDL_GameControllerFromInstanceID(jid);
                if (activeController_) break;
            }
        }
    }

    if (!targetController) targetController = activeController_;

#ifdef __SWITCH__
    if (!targetController) {
        HidVibrationValue value = makeSwitchVibrationValue(strength, lowBandScale, highBandScale);
        sendSwitchVibrationNow(value);
        switchRumbleStopTick_ = SDL_GetTicks() + (Uint32)std::max(16, durationMs);
        switchRumbleActive_ = true;
    }
#endif

    if (targetController) {
        if (slot == 0) activeController_ = targetController;
        Uint16 lowIntensity  = (Uint16)(std::clamp((0.18f + strength * 0.72f) * lowBandScale, 0.0f, 1.0f) * 65535.0f);
        Uint16 highIntensity = (Uint16)(std::clamp((0.08f + strength * 0.92f) * highBandScale, 0.0f, 1.0f) * 65535.0f);
        SDL_GameControllerRumble(targetController, lowIntensity, highIntensity, durationMs);
        SDL_GameControllerRumbleTriggers(targetController, highIntensity / 2, lowIntensity / 3, durationMs);
    }
}

float Game::localFeedbackFalloff(Vec2 pos, float maxDistance) const {
    if (maxDistance <= 0.0f) return 0.0f;
    float dist = Vec2::dist(pos, player_.pos);
    return std::clamp(1.0f - dist / maxDistance, 0.0f, 1.0f);
}

void Game::playExplosionFeedback(Vec2 pos, float maxDistance, float minStrength, float maxStrength,
                                 int minDurationMs, int maxDurationMs, float lowBandScale,
                                 float highBandScale, int maxVolume, int minVolume) {
    float falloff = localFeedbackFalloff(pos, maxDistance);
    if (falloff <= 0.01f) return;

    float strength = minStrength + (maxStrength - minStrength) * falloff;
    int durationMs = minDurationMs + (int)((maxDurationMs - minDurationMs) * falloff);
    rumble(strength, durationMs, lowBandScale, highBandScale);

    if (sfxExplosion_) {
        int volume = minVolume + (int)((maxVolume - minVolume) * falloff);
        volume = std::clamp(volume, 0, MIX_MAX_VOLUME);
        if (volume > 0) {
            int ch = Mix_PlayChannel(-1, sfxExplosion_, 0);
            if (ch >= 0) Mix_Volume(ch, volume);
        }
    }
}

#ifdef __SWITCH__
void Game::updateSwitchRumble() {
    if (!switchRumbleActive_) return;

    Sint32 remaining = (Sint32)(switchRumbleStopTick_ - SDL_GetTicks());
    if (remaining > 0) return;

    sendSwitchVibrationNow(HidVibrationValue{});
    switchRumbleActive_ = false;
    switchRumbleStopTick_ = 0;
}
#endif

// ═════════════════════════════════════════════════════════════════════════════
//  Update Checker
// ═════════════════════════════════════════════════════════════════════════════

void Game::checkForUpdates() {
    if (updateChecked_) return;
    updateChecked_ = true;

#ifdef __SWITCH__
    return;
#endif
    
    // Run in background thread to avoid blocking startup
    std::thread([this]() {
        std::string latest = UpdateChecker::fetchLatestVersion("etonedemid", "cold-start-nx");
        if (!latest.empty()) {
            latestVersion_ = latest;
            updateAvailable_ = UpdateChecker::isNewerVersion(GAME_VERSION, latest.c_str());
            if (updateAvailable_) {
                printf("Update available: v%s (current: v%s)\n", latest.c_str(), GAME_VERSION);
            }
        }
    }).detach();
}

bool Game::isNewerVersion(const char* current, const char* latest) {
    return UpdateChecker::isNewerVersion(current, latest);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Asset Loading
// ═════════════════════════════════════════════════════════════════════════════

void Game::loadAssets() {
    auto& a = Assets::instance();

    // Player body frames (body-01..10)
    defaultPlayerSprites_ = a.loadAnim("sprites/player/body-", 10, 1);
    playerSprites_ = defaultPlayerSprites_;

    // Player death frames
    char buf[128];
    defaultPlayerDeathSprites_.clear();
    for (int i = 1; i <= 12; i++) {
        snprintf(buf, sizeof(buf), "sprites/player/death-%d.png", i);
        auto* t = a.tex(buf);
        if (t) defaultPlayerDeathSprites_.push_back(t);
    }
    playerDeathSprites_ = defaultPlayerDeathSprites_;

    // Leg frames (legs-01..08)
    defaultLegSprites_ = a.loadAnim("sprites/player/legs-", 8, 1);
    legSprites_ = defaultLegSprites_;

    // Bomb anim
    bombSprites_.clear();
    for (int i = 1; i <= 13; i++) {
        snprintf(buf, sizeof(buf), "sprites/bomb/bomb%d.png", i);
        auto* t = a.tex(buf);
        if (t) bombSprites_.push_back(t);
    }

    // Single sprites
    enemySprite_   = a.tex("sprites/enemy/melee.png");
    shooterSprite_ = a.tex("sprites/enemy/shooter.png");
    bruteSprite_   = a.tex("sprites/enemy/heavy.png");
    scoutSprite_   = a.tex("sprites/enemy/scout.png");
    sniperSprite_  = a.tex("sprites/enemy/sniper.png");
    gunnerSprite_  = a.tex("sprites/enemy/gunner.png");  // falls back to nullptr → uses shooterSprite_
    bulletSprite_  = a.tex("sprites/projectiles/bullet-player.png");
    // Red-tinted copy for enemy bullets – reuse same texture with color mod at draw time
    enemyBulletSprite_ = bulletSprite_;
    shieldSprite_  = a.tex("sprites/effects/shield.png");
    mainmenuBg_   = a.tex("sprites/ui/mainmenu.png");
    bloodTex_     = a.tex("sprites/effects/blood.png");
    scorchTex_    = a.tex("sprites/effects/scorch.png");

    // Map tiles
    floorTex_  = a.tex("tiles/walls/floor.png");
    grassTex_  = a.tex("tiles/ground/grass.png");
    gravelTex_ = a.tex("tiles/ground/gravel.png");
    woodTex_   = a.tex("sprites/tiles/wood.png");  // not yet moved
    sandTex_   = a.tex("sprites/tiles/sand.png");  // not yet moved
    wallTex_   = a.tex("tiles/walls/floor.png");
    glassTex_  = a.tex("sprites/tiles/glass.png"); // not yet moved
    deskTex_   = a.tex("sprites/tiles/desk.png");  // not yet moved
    boxTex_    = a.tex("tiles/props/box.png");
    gravelGrass1Tex_ = a.tex("tiles/ground/gravel-grass1.png");
    gravelGrass2Tex_ = a.tex("tiles/ground/gravel-grass2.png");
    gravelGrass3Tex_ = a.tex("tiles/ground/gravel-grass3.png");
    glassTileTex_   = a.tex("tiles/ceiling/glasstile.png");

    // Sound effects
    sfxShoot_    = a.sfx("shootfx.wav");
    sfxEnemyShoot_ = a.sfx("laserShoot.wav");
    sfxReload_   = a.sfx("reload.mp3");
    sfxHurt_     = a.sfx("hurt.mp3");
    sfxDeath_    = a.sfx("death.mp3");
    sfxExplosion_= a.sfx("explosion.mp3");
    sfxParry_    = a.sfx("parry.mp3");
    sfxSwoosh_   = a.sfx("swing.mp3");
    sfxBreak_    = a.sfx("break.mp3");
    sfxBeep_     = a.sfx("beep.mp3");
    sfxPress_        = a.sfx("press.mp3");
    sfxEnemyExplode_ = a.sfx("enemyexplode.mp3");
    bgMusic_         = a.music("cybergrind.mp3");
    menuMusic_   = a.music("mainmenu.mp3");
}

// ═════════════════════════════════════════════════════════════════════════════
//  Game State Management
// ═════════════════════════════════════════════════════════════════════════════

void Game::startGame() {
    state_ = GameState::Playing;
    gameTime_ = 0;
    discordSessionStart_ = (int64_t)time(nullptr);
    // Reset lobby flags that would suppress wave spawning if carried over from a
    // previous multiplayer PvP session
    lobbySettings_.isPvp = false;
    // Wave spawning state
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;

    // Reset world
    enemies_.clear();
    bullets_.clear();
    enemyBullets_.clear();
    bombs_.clear();
    explosions_.clear();
    debris_.clear();
    blood_.clear();
    boxFragments_.clear();
    crates_.clear();
    pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    sandboxMode_ = false;  // regular play is always arena
    map_.generate(config_.mapWidth, config_.mapHeight);
    invalidateMinimapCache();

    // Reset player
    player_ = Player{};
    player_.maxHp = config_.playerMaxHp;
    player_.hp = config_.playerMaxHp;
    player_.pos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
    player_.bombCount = 1; // Start with one bomb
    applyCharacterStatsToPlayer(player_);

    // Camera
    camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
    camera_.worldW = map_.worldWidth();
    camera_.worldH = map_.worldHeight();

    // Start music
    if (bgMusic_) {
        Mix_PlayMusic(bgMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Main Loop
// ═════════════════════════════════════════════════════════════════════════════

void Game::run() {
    Uint64 lastTime = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();

    while (running_) {
        if (dedicatedMode_ && !dedicatedBootstrapped_) {
            dedicatedBootstrapped_ = true;
            hostPort_ = dedicatedPort_;
            hostMaxPlayers_ = dedicatedMaxPlayers_;
            lobbyPassword_ = dedicatedPassword_;
            config_.username = dedicatedServerName_;
            NetworkManager::instance().setUsername(config_.username);
            currentRules_ = createCoopArenaRules(hostMaxPlayers_);
            NetworkManager::instance().setGamemode("coop_arena");
            printf("Dedicated server: starting on UDP %d (maxPlayers=%d)\n",
                   hostPort_, hostMaxPlayers_);
            hostGame();
        }

        Uint64 now = SDL_GetPerformanceCounter();
        dt_ = (float)(now - lastTime) / (float)freq;
        lastTime = now;
        if (dt_ > 0.05f) dt_ = 0.05f; // cap at 20fps min

        // Update UI system at the start of each frame so both handleInput()
        // and render() see consistent mouse/touch state.
        ui_.beginFrame(dt_, usingGamepad_);

    #ifdef __SWITCH__
        updateSwitchRumble();
    #endif

        handleInput();

        // Always update the network (for lobby, connecting, in-game, etc.)
        {
            auto& net = NetworkManager::instance();
            if (net.isOnline()) net.update(dt_);
        }

        // Discord Rich Presence: tick IPC connection; refresh activity every 5 s
        DiscordRPC::instance().tick(dt_);
        discordTimer_ -= dt_;
        if (discordTimer_ <= 0.f) {
            discordTimer_ = 5.0f;
            updateDiscordPresence();
        }

        if (state_ == GameState::Playing || state_ == GameState::PlayingCustom
            || state_ == GameState::PlayingPack
            || state_ == GameState::MultiplayerGame
            || state_ == GameState::MultiplayerPaused
            || state_ == GameState::MultiplayerDead
            || state_ == GameState::LocalCoopGame) {
            update();
            if (state_ == GameState::PlayingCustom) {
                updateCustomMapGoal();
            }
            if (state_ == GameState::PlayingPack) {
                // Reuse custom map goal logic for pack levels
                updateCustomMapGoal();
                // Check if player won (goal reached)
                if (state_ == GameState::CustomWin) {
                    state_ = GameState::PackLevelWin;
                    menuSelection_ = 0;
                }
                // Check if player died
                if (player_.dead && state_ == GameState::PlayingPack) {
                    state_ = GameState::PackDead;
                    menuSelection_ = 0;
                }
            }
        }
        else if (state_ == GameState::EditorConfig) {
            // Editor config screen handles its own input via events
            // Check if config is done
            if (!editor_.isShowingConfig()) {
                if (editor_.wantsBack()) {
                    editor_.clearWantsBack();
                    editor_.setActive(false);
                    state_ = GameState::MainMenu;
                    menuSelection_ = 0;
                } else {
                    state_ = GameState::Editor;
                }
            }
        }
        else if (state_ == GameState::Editor) {
            editor_.update(dt_);
            // Check if editor wants back → return to main menu and rescan maps
            if (editor_.wantsBack()) {
                editor_.clearWantsBack();
                editor_.setActive(false);
                scanMapFiles();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
            // ── Handle save dialog from editor (runs here, not in update(), because
            //    update() is only called for gameplay states) ──
            if (!modSaveDialog_.isOpen()) {
                if (editor_.wantsModSave()) {
                    editor_.clearWantsModSave();
                    if (editor_.hasExplicitSavePath()) {
                        editor_.saveMap(editor_.savePath());
                    } else {
                        openModSaveDialog(ModSaveDialogState::AssetMap);
                    }
                }
            }
            if (modSaveDialog_.confirmed) {
                modSaveDialog_.confirmed = false;
                if (modSaveDialog_.asset == ModSaveDialogState::AssetMap) {
                    editor_.performModSave(modSaveDialog_.confirmedModFolder);
                    ModManager::instance().scanMods();
                    modSaveDialog_.close();
                }
                // other asset types handled by update() block below
            }
            // Check if editor wants to test play
            if (editor_.wantsTestPlay()) {
                editor_.clearTestPlay();
                editor_.clearWantsModSave();  // don’t let pending save open dialog mid-game
                // Copy the editor's map directly into customMap_
                customMap_ = editor_.getMap();
                // Sync custom tile paths (normalised) into the map copy
                // so startCustomMap-style texture loading works
                for (int _i = 0; _i < 8; _i++) customTileTextures_[_i] = nullptr;
                editor_.getCustomTileTextures(customTileTextures_);
                // Apply the map's saved game mode for test-play
                sandboxMode_ = (customMap_.gameMode == 1);
                // Start playing it
                state_ = GameState::PlayingCustom;
                playingCustomMap_ = true;
                customGoalOpen_ = false;
                gameTime_ = 0;

                map_.width  = customMap_.width;
                map_.height = customMap_.height;
                map_.tiles  = customMap_.tiles;
                map_.ceiling = customMap_.ceiling;

                enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
                bombs_.clear(); explosions_.clear(); debris_.clear();
                blood_.clear(); boxFragments_.clear();
                waveNumber_ = 0; waveEnemiesLeft_ = 0; waveActive_ = false;

                player_ = Player{};
                player_.maxHp = config_.playerMaxHp;
                player_.hp = config_.playerMaxHp;
                player_.bombCount = 1;

                MapTrigger* startT = customMap_.findStartTrigger();
                if (startT) player_.pos = {startT->x, startT->y};
                else player_.pos = {map_.worldWidth()/2.f, map_.worldHeight()/2.f};

                camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
                camera_.worldW = map_.worldWidth();
                camera_.worldH = map_.worldHeight();

                customEnemiesTotal_ = 0;
                for (auto& es : customMap_.enemySpawns) {
                    if (isCrateSpawnType(es.enemyType)) {
                        // Spawn as a breakable crate
                        PickupCrate crate;
                        crate.pos = {es.x, es.y};
                        crate.contents = rollRandomUpgrade();
                        crates_.push_back(crate);
                    } else {
                        spawnEnemy({es.x, es.y}, enemyTypeFromSpawnId(es.enemyType));
                        customEnemiesTotal_++;
                    }
                }
                map_.findSpawnPoints();
                testPlayFromEditor_ = true;
            }
        }

        if (!dedicatedMode_) {
            render();
        } else {
            SDL_Delay(5);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Input
// ═════════════════════════════════════════════════════════════════════════════

void Game::handleInput() {
    // Reset per-frame triggers
    bombInput_ = false;
    bombLaunchInput_ = false;
    parryInput_ = false;
    meleeInput_ = false;
    weaponSwitchDelta_ = 0;
    pauseInput_ = false;
    confirmInput_ = false;
    backInput_ = false;
    leftInput_ = false;
    rightInput_ = false;
    tabInput_ = false;

    // Soft keyboard hold-repeat
    updateSoftKBRepeat();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { running_ = false; return; }

        // Mod-save dialog gets first pick of all events when open
        if (modSaveDialog_.isOpen()) {
            if (modSaveDialog_.phase == ModSaveDialogState::NameNewMod && e.type == SDL_KEYDOWN) {
                SDL_Keycode sym = e.key.keysym.sym;
                if ((sym == SDLK_RETURN || sym == SDLK_KP_ENTER) && !modSaveDialog_.newModId.empty()) {
                    modSaveDialog_.confirmedModFolder = modBuildFolder(modSaveDialog_.newModId, modSaveDialog_.newModId);
                    modSaveDialog_.confirmed = true;
                    if (softKB_.active) softKB_.close(false);
                    continue;
                }
            }
            if (softKB_.active && handleSoftKBEvent(e)) continue;
            handleModSaveDialogEvent(e);
            continue;
        }

        // Clear D-pad hold-repeat when button released
        if (e.type == SDL_CONTROLLERBUTTONUP) {
            Uint8 btn = remapButton(e.cbutton.button);
            if (btn == (Uint8)kbNavHeldButton_)
                kbNavHeldButton_ = -1;
            if (btn == (Uint8)menuNavHeldBtn_)
                menuNavHeldBtn_ = -1;
        }

        // Pass events to editor if active
        if ((state_ == GameState::Editor || state_ == GameState::EditorConfig) && editor_.isActive()) {
            editor_.handleInput(e);
        }

        // Centralized soft keyboard handles ALL text input
        if (softKB_.active) {
            if (handleSoftKBEvent(e)) continue;
            continue;
        }

        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            lastGamepadInputId_ = e.cbutton.which;
            // ── Local join/leave handling in lobby screens ──
            if (state_ == GameState::LocalCoopLobby || state_ == GameState::Lobby) {
                bool isMultiplayerLobby = (state_ == GameState::Lobby);
                SDL_JoystickID iid = e.cbutton.which;
                // Check if this gamepad is already assigned to a slot
                int existingSlot = -1;
                for (int s = 0; s < 4; s++) {
                    if (coopSlots_[s].joined && coopSlots_[s].joyInstanceId == iid)
                        { existingSlot = s; break; }
                }
                if (btn == SDL_CONTROLLER_BUTTON_A) {
                    bool isLobbyPrimaryPad = (isMultiplayerLobby && iid == lobbyPrimaryPadId_);
                    if (existingSlot < 0) {
                        if (isLobbyPrimaryPad) {
                            // This pad opened host/join flow; allow A to act as normal confirm/ready,
                            // but never treat it as a local sub-player join.
                        } else {
#ifdef __SWITCH__
                            // On Switch, gamepads can join slot 0-3
                            int startSlot = 0;
#else
                            // On PC, slot 0 is keyboard+mouse, gamepads join 1-3
                            int startSlot = 1;
#endif
                            for (int s = startSlot; s < 4; s++) {
                                if (!coopSlots_[s].joined) {
                                    int gpNum = 0;
                                    for (int k = startSlot; k < 4; k++) if (coopSlots_[k].joined) gpNum++;
                                    coopSlots_[s].joined = true;
                                    coopSlots_[s].joyInstanceId = iid;
#ifdef __SWITCH__
                                    char uname[16];
                                    if (s == 0) {
                                        snprintf(uname, sizeof(uname), "%s", config_.username.c_str());
                                    } else {
                                        snprintf(uname, sizeof(uname), "nx-%d", gpNum);
                                    }
#else
                                    char uname[16]; snprintf(uname, sizeof(uname), "pc-%d", gpNum + 1);
#endif
                                    coopSlots_[s].username = uname;
                                    if (isMultiplayerLobby) {
                                        int localSubPlayers = 0;
                                        for (int si = startSlot; si < 4; si++) if (coopSlots_[si].joined && si > 0) localSubPlayers++;
                                        NetworkManager::instance().setLocalSubPlayers((uint8_t)localSubPlayers);
                                        lobbySubPlayersSent_ = localSubPlayers;
                                    }
                                    if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                                    break;
                                }
                            }
                            continue;
                        }
                    }
                    // In multiplayer lobby, let already joined gamepads still use A as confirm/ready.
                    if (!isMultiplayerLobby || existingSlot < 0) continue;
                } else if (btn == SDL_CONTROLLER_BUTTON_B) {
#ifdef __SWITCH__
                    int minSlot = 0; // On Switch, slot 0 can leave
#else
                    int minSlot = 1; // On PC, slot 0 is keyboard+mouse
#endif
                    if (existingSlot >= minSlot) {
                        coopSlots_[existingSlot] = CoopSlot{};
                        // Renumber remaining gamepad usernames
                        int gpNum = 1;
#ifdef __SWITCH__
                        // On Switch: slot 0 gets config name, others get nx-N
                        for (int s = 0; s < 4; s++) {
                            if (!coopSlots_[s].joined) continue;
                            if (s == 0) {
                                coopSlots_[s].username = config_.username;
                            } else {
                                char uname[16]; snprintf(uname, sizeof(uname), "nx-%d", gpNum++);
                                coopSlots_[s].username = uname;
                            }
                        }
#else
                        // On PC: all gamepads get pc-N
                        for (int s = 1; s < 4; s++) {
                            if (!coopSlots_[s].joined) continue;
                            char uname[16]; snprintf(uname, sizeof(uname), "pc-%d", gpNum++);
                            coopSlots_[s].username = uname;
                        }
#endif
                        if (isMultiplayerLobby) {
                            int localSubPlayers = 0;
                            for (int s = 1; s < 4; s++) if (coopSlots_[s].joined) localSubPlayers++;
                            NetworkManager::instance().setLocalSubPlayers((uint8_t)localSubPlayers);
                            lobbySubPlayersSent_ = localSubPlayers;
                        }
                    } else if (existingSlot < 0 && !isMultiplayerLobby) {
                        // Unassigned gamepad pressed B — treat as global back
                        backInput_ = true;
                    }
#ifdef __SWITCH__
                    if (existingSlot >= 0 || !isMultiplayerLobby) continue;
#else
                    if (existingSlot > 0 || !isMultiplayerLobby) continue;
#endif
                } else if (btn == SDL_CONTROLLER_BUTTON_START) {
#ifdef __SWITCH__
                    // On Switch, any joined gamepad can start if at least one player is joined
                    bool anyJoined = false;
                    for (int s = 0; s < 4; s++) if (coopSlots_[s].joined) { anyJoined = true; break; }
                    if (existingSlot >= 0 || anyJoined) {
#else
                    // On PC, any joined gamepad can start if slot 0 (keyboard+mouse) is joined
                    if (existingSlot >= 0 || coopSlots_[0].joined) {
#endif
                        confirmInput_ = true;
                        if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    }
                    continue; // consumed by lobby
                }
                // Other buttons in lobby — fall through to normal handling
            }
            usingGamepad_ = true;
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_START:    pauseInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_A:        confirmInput_ = true; if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); } break;
                case SDL_CONTROLLER_BUTTON_B:        backInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: parryInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: weaponSwitchDelta_ += 1; break;
                case SDL_CONTROLLER_BUTTON_X:        bombInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_Y:        tabInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_UP;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    menuSelection_--;
                    if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    menuSelection_++;
                    if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    leftInput_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    rightInput_ = true;
                    break;
            }
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            usingGamepad_ = false;
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: pauseInput_ = true; break;
                case SDLK_RETURN: confirmInput_ = true; break;
                case SDLK_BACKSPACE: backInput_ = true; break;
                case SDLK_SPACE:  parryInput_ = true; break;
                case SDLK_e:     meleeInput_ = true; break;
                case SDLK_1:     player_.activeWeapon = 0; break;
                case SDLK_2:     player_.activeWeapon = 1; break;
                case SDLK_q:     bombInput_ = true; break;
                case SDLK_TAB:   tabInput_ = true; break;
                case SDLK_F11:
                    config_.fullscreen = !config_.fullscreen;
                    SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    break;
            }
        }
        // Arrow keys with OS key-repeat (fires on both initial press and held repeats)
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:    menuSelection_--; break;
                case SDLK_DOWN:  menuSelection_++; break;
                case SDLK_LEFT:  leftInput_  = true; break;
                case SDLK_RIGHT: rightInput_ = true; break;
            }
        }

        if (e.type == SDL_MOUSEWHEEL) {
            if (state_ == GameState::HostSetup)
                hostSetupScrollY_ = std::max(0, std::min(206, hostSetupScrollY_ - e.wheel.y * 30));
            else if (state_ == GameState::Lobby)
                lobbySettingsScrollY_ = std::max(0, lobbySettingsScrollY_ - e.wheel.y * 34);
            else
                weaponSwitchDelta_ += (e.wheel.y > 0) ? 1 : (e.wheel.y < 0 ? -1 : 0);
        }

        // Mouse movement switches to mouse/keyboard mode
        if (e.type == SDL_MOUSEMOTION) {
            usingGamepad_ = false;
        }

        // Mouse click — set confirmInput_ if clicking over a known UI item
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            usingGamepad_ = false;
            if (state_ != GameState::CharCreator && ui_.prevHoveredItem >= 0) {
                confirmInput_ = true;
                if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
            }
        }

        // Touch events — convert to mouse-like behaviour for menu navigation
        if (e.type == SDL_FINGERDOWN) {
            usingGamepad_ = false;
            ui_.touchActive = true;
            ui_.mouseX = (int)(e.tfinger.x * SCREEN_W);
            ui_.mouseY = (int)(e.tfinger.y * SCREEN_H);
            ui_.mouseDown = true;
            ui_.mouseClicked = true;
            // Touch tap over a UI item = confirm
            if (state_ != GameState::CharCreator && ui_.prevHoveredItem >= 0) {
                confirmInput_ = true;
                if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
            }
        }
        if (e.type == SDL_FINGERMOTION) {
            ui_.touchActive = true;
            ui_.mouseX = (int)(e.tfinger.x * SCREEN_W);
            ui_.mouseY = (int)(e.tfinger.y * SCREEN_H);
        }
        if (e.type == SDL_FINGERUP) {
            ui_.touchActive = false;
            ui_.mouseX = (int)(e.tfinger.x * SCREEN_W);
            ui_.mouseY = (int)(e.tfinger.y * SCREEN_H);
            ui_.mouseDown = false;
            ui_.mouseReleased = true;
        }
    }

    // ── D-pad hold-repeat for menu navigation ──
    {
        Uint32 now = SDL_GetTicks();
        if (menuNavHeldBtn_ >= 0 && now >= menuNavRepeatAt_) {
            menuNavRepeatAt_ = now + 110;
            switch (menuNavHeldBtn_) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                menuSelection_--;
                if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                menuSelection_++;
                if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  leftInput_  = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: rightInput_ = true; break;
            }
        }
    }

    // Movement: left stick or WASD
    moveInput_ = {0, 0};
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_W]) moveInput_.y -= 1;
    if (keys[SDL_SCANCODE_S]) moveInput_.y += 1;
    if (keys[SDL_SCANCODE_A]) moveInput_.x -= 1;
    if (keys[SDL_SCANCODE_D]) moveInput_.x += 1;

    // Controller left stick — use the primary gameplay pad and avoid pads owned by local sub-players.
    SDL_GameController* gc = getPrimaryGameplayController();

    // Gamepad stick input (movement + aim)
    bool gcAimActive = false;
    bool gcFireActive = false;
    if (gc) {
        float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        if (fabsf(lx) > 0.15f || fabsf(ly) > 0.15f) {
            moveInput_ = {lx, ly};
            usingGamepad_ = true;
        }

        // Right stick for aiming
        float rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
        if (fabsf(rx) > 0.2f || fabsf(ry) > 0.2f) {
            gcAimActive = true;
            usingGamepad_ = true;
            aimInput_ = {rx, ry};

            // Aim-assist: slight lock-on to closest enemy within a cone
            constexpr float AIM_ASSIST_CONE  = 0.86f;  // cos(~30°) half-angle
            constexpr float AIM_ASSIST_RANGE = 500.0f;  // max lock-on distance
            constexpr float AIM_ASSIST_STRENGTH = 0.35f; // blend factor

            Vec2 aimDir = aimInput_.normalized();
            float bestDist = AIM_ASSIST_RANGE;
            Vec2  bestDir  = {0, 0};
            bool  found    = false;

            for (const auto& en : enemies_) {
                if (!en.alive) continue;
                Vec2 toEnemy = en.pos - player_.pos;
                float dist = toEnemy.length();
                if (dist < 1.0f || dist > AIM_ASSIST_RANGE) continue;
                Vec2 dirToEnemy = toEnemy * (1.0f / dist);
                float dot = aimDir.x * dirToEnemy.x + aimDir.y * dirToEnemy.y;
                if (dot > AIM_ASSIST_CONE && dist < bestDist) {
                    bestDist = dist;
                    bestDir  = dirToEnemy;
                    found    = true;
                }
            }
            if (found) {
                float len = aimInput_.length();
                Vec2 blended = {
                    aimDir.x * (1.0f - AIM_ASSIST_STRENGTH) + bestDir.x * AIM_ASSIST_STRENGTH,
                    aimDir.y * (1.0f - AIM_ASSIST_STRENGTH) + bestDir.y * AIM_ASSIST_STRENGTH
                };
                float blen = blended.length();
                if (blen > 0.001f) aimInput_ = blended * (len / blen);
            }
        }

        // ZR = fire
        gcFireActive = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        // ZL = launch bomb (one-shot)
        bool zlDown = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        if (zlDown && !bombLaunchHeld_) bombLaunchInput_ = true;
        bombLaunchHeld_ = zlDown;
    }

    // ── Analog-stick menu navigation (non-gameplay states) ──
    {
        bool inGameplay = (state_ == GameState::Playing  || state_ == GameState::Paused ||
            state_ == GameState::Dead            || state_ == GameState::LocalCoopGame  ||
            state_ == GameState::LocalCoopPaused || state_ == GameState::LocalCoopDead ||
            state_ == GameState::MultiplayerGame  || state_ == GameState::MultiplayerPaused ||
            state_ == GameState::MultiplayerDead  || state_ == GameState::MultiplayerSpectator ||
            state_ == GameState::PlayingPack      || state_ == GameState::PackPaused ||
            state_ == GameState::PackDead         || state_ == GameState::PackLevelWin ||
            state_ == GameState::Editor           || state_ == GameState::EditorConfig);
        if (!inGameplay && gc) {
            Uint32 now = SDL_GetTicks();
            float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            const float THRESH = 0.35f;
            int snY = (ly < -THRESH) ? -1 : (ly > THRESH) ? 1 : 0;
            int snX = (lx < -THRESH) ? -1 : (lx > THRESH) ? 1 : 0;
            int snCur = (snY != 0) ? snY : snX;
            if (snCur == 0) {
                menuNavStickPrev_ = 0;
            } else if (menuNavHeldBtn_ < 0) {
                bool isNew = (menuNavStickPrev_ == 0);
                if (isNew || (snCur == menuNavStickPrev_ && now >= menuNavRepeatAt_)) {
                    menuNavRepeatAt_ = now + (isNew ? 300U : 110U);
                    if (snY != 0) {
                        menuSelection_ += snY;
                        if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    } else {
                        leftInput_  = snX < 0;
                        rightInput_ = snX > 0;
                    }
                }
                menuNavStickPrev_ = snCur;
                usingGamepad_ = true;
            }
        }
    }

    // Mouse aiming — always active as fallback when gamepad right stick is idle
    {
        int mx, my;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        bool allowMouseAim = (!gc || !usingGamepad_);
    #ifdef __SWITCH__
        allowMouseAim = false;
    #endif
        if (!gcAimActive && allowMouseAim) {
            Vec2 mouseWorld = camera_.screenToWorld({(float)mx, (float)my});
            Vec2 diff = mouseWorld - player_.pos;
            if (diff.length() > 5.0f) {
                aimInput_ = diff.normalized();
            } else {
                aimInput_ = moveInput_;
            }
        }
        // Mouse/keyboard fire — always available alongside gamepad
        fireInput_ = gcFireActive || (mb & SDL_BUTTON_LMASK) || keys[SDL_SCANCODE_J] || keys[SDL_SCANCODE_Z];
        bool rmbDown = (mb & SDL_BUTTON_RMASK) != 0;
        if (rmbDown && !bombLaunchHeld_) bombLaunchInput_ = true;
        if (!gc) bombLaunchHeld_ = rmbDown;  // only track RMB hold when no gamepad (ZL tracks its own)
    }

    // Normalize move
    if (moveInput_.length() > 1.0f) moveInput_ = moveInput_.normalized();

    // Handle menu state transitions
    if (state_ == GameState::MainMenu) {
        if (menuSelection_ < 0) menuSelection_ = 0;
#ifdef __SWITCH__
        if (menuSelection_ > 9) menuSelection_ = 9;  // 0-9: 10 items (PLAY through QUIT)
#else
        if (menuSelection_ > 10) menuSelection_ = 10;  // 0-10: 11 items (PLAY through QUIT)
#endif
        if (confirmInput_) {
            if (menuSelection_ == 0) {
                state_ = GameState::PlayModeMenu;
                playModeSelection_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 1) {
                // Multiplayer
                state_ = GameState::MultiplayerMenu;
                multiMenuSelection_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 2) {
                // Open editor config screen first
                state_ = GameState::EditorConfig;
                editor_.setActive(true);
                editor_.showConfig();
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 3) {
                scanMapFiles();
                prevMenuState_ = GameState::MainMenu;
                state_ = GameState::MapSelect;
                mapSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 4) {
                scanMapPacks();
                prevMenuState_ = GameState::MainMenu;
                state_ = GameState::PackSelect;
                packSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 5) {
                scanCharacters();
                state_ = GameState::CharSelect;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 6) {
                // Create Character — scan first so we can edit existing ones
                scanCharacters();
                charCreator_ = CharCreatorState{};
                state_ = GameState::CharCreator;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 7) {
                // Mods
                ModManager::instance().scanMods();
                state_ = GameState::ModMenu;
                modMenuSelection_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 8) {
                state_ = GameState::ConfigMenu;
                configSelection_ = 0;
                menuSelection_ = 0;
            }
#ifndef __SWITCH__
            else if (menuSelection_ == 9) {
                // UPDATE — open release page
                #if defined(_WIN32)
                    system("start https://github.com/etonedemid/cold-start-nx/releases/latest");
                #else
                    system("xdg-open https://github.com/etonedemid/cold-start-nx/releases/latest 2>/dev/null || "
                           "firefox https://github.com/etonedemid/cold-start-nx/releases/latest 2>/dev/null || "
                           "google-chrome https://github.com/etonedemid/cold-start-nx/releases/latest 2>/dev/null");
                #endif
            }
            else if (menuSelection_ == 10) {
                running_ = false;  // QUIT
            }
#else
            else if (menuSelection_ == 9) {
                running_ = false;  // QUIT
            }
#endif
            else running_ = false;
        }
    }
    else if (state_ == GameState::PlayModeMenu) {
        playModeSelection_ = menuSelection_;   // propagate DPad nav
        if (playModeSelection_ < 0) playModeSelection_ = 0;
        if (playModeSelection_ > 9) playModeSelection_ = 9;
        menuSelection_ = playModeSelection_;

        auto adjustIntPM = [&](int& value, int minV, int maxV, int step) {
            if (leftInput_)  value = std::max(minV, value - step);
            if (rightInput_) value = std::min(maxV, value + step);
        };
        auto adjustFloatPM = [&](float& value, float minV, float maxV, float step) {
            if (leftInput_)  value = std::max(minV, value - step);
            if (rightInput_) value = std::min(maxV, value + step);
        };

        if      (playModeSelection_ == 3) adjustIntPM  (config_.mapWidth,        20,   120, 2);
        else if (playModeSelection_ == 4) adjustIntPM  (config_.mapHeight,        14,    80, 2);
        else if (playModeSelection_ == 5) adjustIntPM  (config_.playerMaxHp,       1,    20, 1);
        else if (playModeSelection_ == 6) adjustFloatPM(config_.spawnRateScale,  0.3f,  3.0f, 0.1f);
        else if (playModeSelection_ == 7) adjustFloatPM(config_.enemyHpScale,    0.3f,  3.0f, 0.1f);
        else if (playModeSelection_ == 8) adjustFloatPM(config_.enemySpeedScale, 0.5f,  2.5f, 0.1f);

        if (confirmInput_) {
            if (playModeSelection_ == 0) {
                startGame();
            } else if (playModeSelection_ == 1) {
                scanMapFiles();
                prevMenuState_ = GameState::PlayModeMenu;
                state_ = GameState::MapSelect;
                mapSelectIdx_ = 0;
                menuSelection_ = 0;
            } else if (playModeSelection_ == 2) {
                scanMapPacks();
                prevMenuState_ = GameState::PlayModeMenu;
                state_ = GameState::PackSelect;
                packSelectIdx_ = 0;
                menuSelection_ = 0;
            } else if (playModeSelection_ == 9) {
                saveConfig();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }
        if (backInput_ || pauseInput_) {
            saveConfig();
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::LocalCoopLobby) {
#ifndef __SWITCH__
        // P1 (keyboard+mouse) is always joined on PC
        if (!coopSlots_[0].joined) {
            coopSlots_[0].joined = true;
            coopSlots_[0].joyInstanceId = -1;
            coopSlots_[0].username = config_.username;
        }
#endif
        // Toggle mode with TAB
        if (tabInput_) {
            lobbySettings_.isPvp = !lobbySettings_.isPvp;
            lobbySettings_.pvpEnabled = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
        }
        if (confirmInput_) {
            // Only P1 (Enter on keyboard, START on P1 gamepad) starts the game
            coopPlayerCount_ = 0;
            for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) coopPlayerCount_++;
            startLocalCoopGame();
        }
        if (backInput_ || pauseInput_) {
            for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
            coopPlayerCount_ = 0;
            state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
        }
        // Allow gamepad join/unjoin
        for (int ci = 0; ci < 4; ci++) {
            auto& slot = coopSlots_[ci];
            (void)slot; // handled by joy events
        }
    }
    else if (state_ == GameState::LocalCoopGame) {
        if (pauseInput_) { state_ = GameState::LocalCoopPaused; menuSelection_ = 0; }
    }
    else if (state_ == GameState::LocalCoopPaused) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 1) menuSelection_ = 1;
        if (pauseInput_ || backInput_) state_ = GameState::LocalCoopGame;
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::LocalCoopGame;
            else {
                for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
                coopPlayerCount_ = 0;
                state_ = GameState::MainMenu; menuSelection_ = 0;
                playMenuMusic();
            }
        }
    }
    else if (state_ == GameState::LocalCoopDead) {
        if (confirmInput_ || backInput_) {
            for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
            coopPlayerCount_ = 0;
            state_ = GameState::MainMenu; menuSelection_ = 0;
            playMenuMusic();
        }
    }
    else if (state_ == GameState::ConfigMenu) {
        if (configSelection_ < 0) configSelection_ = 0;
        if (configSelection_ > CONFIG_BACK_INDEX) configSelection_ = CONFIG_BACK_INDEX;

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > CONFIG_BACK_INDEX) menuSelection_ = CONFIG_BACK_INDEX;

        // Username text input handling
        if (usernameTyping_) {
            // Input is consumed in SDL event loop, we just check for confirm/cancel
            // (handled via event loop below — this prevents other config actions)
        } else {
            configSelection_ = menuSelection_;

            auto adjustInt = [&](int& value, int minV, int maxV, int step) {
                if (leftInput_) value = std::max(minV, value - step);
                if (rightInput_) value = std::min(maxV, value + step);
            };
            auto adjustFloat = [&](float& value, float minV, float maxV, float step) {
                if (leftInput_) value = std::max(minV, value - step);
                if (rightInput_) value = std::min(maxV, value + step);
            };
            auto toggleBool = [&](bool& value) {
                if (confirmInput_ || leftInput_ || rightInput_) value = !value;
            };
#ifndef __SWITCH__
            auto adjustResolution = [&](int dir) {
                int idx = findResolutionPresetIndex(config_.windowWidth, config_.windowHeight);
                idx = std::max(0, std::min((int)(sizeof(kResolutionPresets) / sizeof(kResolutionPresets[0])) - 1, idx + dir));
                config_.windowWidth = kResolutionPresets[idx].w;
                config_.windowHeight = kResolutionPresets[idx].h;
                applyResolutionSettings(true);
            };
#endif

            if      (configSelection_ == 0) adjustInt  (config_.playerMaxHp,    1,    20, 1);
            else if (configSelection_ == 1) adjustFloat(config_.spawnRateScale, 0.3f, 3.0f, 0.1f);
            else if (configSelection_ == 2) adjustFloat(config_.enemyHpScale,   0.3f, 3.0f, 0.1f);
            else if (configSelection_ == 3) adjustFloat(config_.enemySpeedScale,0.5f, 2.5f, 0.1f);
            else if (configSelection_ == 4) { adjustInt(config_.musicVolume, 0, 128, 8); Mix_VolumeMusic(config_.musicVolume); }
            else if (configSelection_ == 5) { adjustInt(config_.sfxVolume, 0, 128, 8); }
#ifndef __SWITCH__
            else if (configSelection_ == CONFIG_RESOLUTION_INDEX) {
                if (leftInput_) adjustResolution(-1);
                if (rightInput_) adjustResolution(1);
            }
#endif
            else if (configSelection_ == CONFIG_SHADER_CRT_INDEX) toggleBool(config_.shaderCRT);
            else if (configSelection_ == CONFIG_SHADER_CHROMATIC_INDEX) toggleBool(config_.shaderChromatic);
            else if (configSelection_ == CONFIG_SHADER_SCANLINES_INDEX) toggleBool(config_.shaderScanlines);
            else if (configSelection_ == CONFIG_SHADER_GLOW_INDEX) toggleBool(config_.shaderGlow);
            else if (configSelection_ == CONFIG_SHADER_GLITCH_INDEX) toggleBool(config_.shaderGlitch);
            else if (configSelection_ == CONFIG_SHADER_NEON_INDEX) toggleBool(config_.shaderNeonEdge);
            else if (configSelection_ == CONFIG_SAVE_INCOMING_MODS_INDEX) toggleBool(config_.saveIncomingModsPermanently);
            else if (configSelection_ == CONFIG_USERNAME_INDEX && confirmInput_) {
                // Edit username
                usernameTyping_ = true;
                softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", 16,
                             &config_.username, 32, [this](bool) {
                    usernameTyping_ = false;
                    if (config_.username.empty()) config_.username = "Player";
                    NetworkManager::instance().setUsername(config_.username);
                });
            }
            else if (configSelection_ == CONFIG_BACK_INDEX && (confirmInput_ || backInput_ || pauseInput_)) {
                saveConfig();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
            if (backInput_ || pauseInput_) {
                saveConfig();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }
    }
    else if (state_ == GameState::Paused) {
        if (menuSelection_ < 0) menuSelection_ = 0;
#ifndef __SWITCH__
        if (menuSelection_ > 4) menuSelection_ = 4;
#else
        if (menuSelection_ > 3) menuSelection_ = 3;
#endif
        if (pauseInput_) { state_ = GameState::Playing; }
        // Volume adjustment with left/right
        if (menuSelection_ == 1) {
            if (leftInput_) config_.musicVolume = std::max(0, config_.musicVolume - 8);
            if (rightInput_) config_.musicVolume = std::min(128, config_.musicVolume + 8);
            Mix_VolumeMusic(config_.musicVolume);
        }
        if (menuSelection_ == 2) {
            if (leftInput_) config_.sfxVolume = std::max(0, config_.sfxVolume - 8);
            if (rightInput_) config_.sfxVolume = std::min(128, config_.sfxVolume + 8);
        }
#ifndef __SWITCH__
        if (menuSelection_ == 3 && (confirmInput_ || leftInput_ || rightInput_)) {
            config_.fullscreen = !config_.fullscreen;
            SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            saveConfig();
        }
#endif
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::Playing;
#ifndef __SWITCH__
            else if (menuSelection_ == 4) {
                if (testPlayFromEditor_) {
                    testPlayFromEditor_ = false;
                    state_ = GameState::CharCreator;
                    playMenuMusic();
                    menuSelection_ = 0;
                } else {
                    state_ = GameState::MainMenu; playMenuMusic();
                }
            }
#else
            else if (menuSelection_ == 3) {
                if (testPlayFromEditor_) {
                    testPlayFromEditor_ = false;
                    state_ = GameState::CharCreator;
                    playMenuMusic();
                    menuSelection_ = 0;
                } else {
                    state_ = GameState::MainMenu; playMenuMusic();
                }
            }
#endif
        }
    }
    else if (state_ == GameState::Playing) {
        if (pauseInput_) { state_ = GameState::Paused; menuSelection_ = 0; }
    }
    else if (state_ == GameState::Dead) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 1) menuSelection_ = 1;
        if (confirmInput_) {
            if (menuSelection_ == 0) {
                if (testPlayFromEditor_) {
                    // Restart test with same character
                    testCharacter();
                } else {
                    startGame();
                }
            } else {
                if (testPlayFromEditor_) {
                    testPlayFromEditor_ = false;
                    state_ = GameState::CharCreator;
                    playMenuMusic();
                    menuSelection_ = 0;
                } else {
                    state_ = GameState::MainMenu; playMenuMusic(); menuSelection_ = 0;
                }
            }
        }
    }
    else if (state_ == GameState::EditorConfig) {
        // Config screen handles its own input; transitions checked in run loop
    }
    else if (state_ == GameState::MapConfig) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 2) menuSelection_ = 2;  // 0=Arena 1=Sandbox 2=BACK
        if (leftInput_ || rightInput_) {
            // Toggle mode with left/right too
            if (menuSelection_ < 2) mapConfigMode_ = 1 - mapConfigMode_;
        }
        if (confirmInput_) {
            if (menuSelection_ == 0) {
                mapConfigMode_ = 0; // Arena
                startCustomMap(mapFiles_[mapSelectIdx_]);
            } else if (menuSelection_ == 1) {
                mapConfigMode_ = 1; // Sandbox
                startCustomMap(mapFiles_[mapSelectIdx_]);
            } else {
                state_ = GameState::MapSelect; menuSelection_ = mapSelectIdx_;
            }
        }
        if (backInput_) { state_ = GameState::MapSelect; menuSelection_ = mapSelectIdx_; }
    }
    else if (state_ == GameState::Editor) {
        // Editor handles its own input via SDL events above
        if (pauseInput_ || backInput_) {
            editor_.setActive(false);
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::MapSelect) {
        int maxIdx = std::max(0, (int)mapFiles_.size() - 1);
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > maxIdx + 1) menuSelection_ = maxIdx + 1; // +1 for BACK
        mapSelectIdx_ = menuSelection_;
        if (backInput_) {
            if (prevMenuState_ == GameState::PlayModeMenu) {
                state_ = GameState::PlayModeMenu; playModeSelection_ = 1; menuSelection_ = 1;
            } else {
                state_ = GameState::MainMenu; menuSelection_ = 0;
            }
        }
        if (confirmInput_) {
            if (mapSelectIdx_ <= maxIdx && !mapFiles_.empty()) {
                // Peek at the map header to pre-select its saved game mode
                mapConfigMode_ = 0;
                {
                    FILE* fh = fopen(mapFiles_[mapSelectIdx_].c_str(), "rb");
                    if (fh) {
                        CSM_Header hdr;
                        if (fread(&hdr, sizeof(CSM_Header), 1, fh) == 1 && hdr.magic == CSM_MAGIC)
                            mapConfigMode_ = hdr.reserved[0];
                        fclose(fh);
                    }
                }
                state_ = GameState::MapConfig;
                menuSelection_ = 0;
            } else {
                if (prevMenuState_ == GameState::PlayModeMenu) {
                    state_ = GameState::PlayModeMenu; playModeSelection_ = 1; menuSelection_ = 1;
                } else {
                    state_ = GameState::MainMenu; menuSelection_ = 0;
                }
            }
        }
    }
    else if (state_ == GameState::CharSelect) {
        int maxIdx = (int)availableChars_.size(); // includes "Default" option
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > maxIdx + 1) menuSelection_ = maxIdx + 1; // +1 BACK
        if (backInput_) { state_ = GameState::MainMenu; menuSelection_ = 0; }
        if (confirmInput_) {
            if (menuSelection_ == 0) {
                resetToDefaultCharacter();
            } else if (menuSelection_ <= (int)availableChars_.size()) {
                selectedChar_ = menuSelection_ - 1;
                applyCharacter(availableChars_[selectedChar_]);
            } else {
                // BACK
            }
            state_ = GameState::MainMenu; menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::PlayingCustom) {
        if (pauseInput_) { state_ = GameState::CustomPaused; menuSelection_ = 0; }
    }
    else if (state_ == GameState::CustomPaused) {
        if (menuSelection_ < 0) menuSelection_ = 0;
#ifndef __SWITCH__
        if (menuSelection_ > 4) menuSelection_ = 4;
#else
        if (menuSelection_ > 3) menuSelection_ = 3;
#endif
        if (pauseInput_) state_ = GameState::PlayingCustom;
        if (menuSelection_ == 1) {
            if (leftInput_) config_.musicVolume = std::max(0, config_.musicVolume - 8);
            if (rightInput_) config_.musicVolume = std::min(128, config_.musicVolume + 8);
            Mix_VolumeMusic(config_.musicVolume);
        }
        if (menuSelection_ == 2) {
            if (leftInput_) config_.sfxVolume = std::max(0, config_.sfxVolume - 8);
            if (rightInput_) config_.sfxVolume = std::min(128, config_.sfxVolume + 8);
        }
#ifndef __SWITCH__
        if (menuSelection_ == 3 && (confirmInput_ || leftInput_ || rightInput_)) {
            config_.fullscreen = !config_.fullscreen;
            SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            saveConfig();
        }
#endif
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::PlayingCustom;
#ifndef __SWITCH__
            else if (menuSelection_ == 4) {
#else
            else if (menuSelection_ == 3) {
#endif
                playMenuMusic(); menuSelection_ = 0; playingCustomMap_ = false;
                if (testPlayFromEditor_) { state_ = GameState::Editor; testPlayFromEditor_ = false; }
                else { state_ = GameState::MainMenu; }
            }
        }
    }
    else if (state_ == GameState::CustomDead) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 1) menuSelection_ = 1;
        if (confirmInput_) {
            if (menuSelection_ == 0 && !customMap_.name.empty() && !testPlayFromEditor_) {
                startCustomMap("maps/" + customMap_.name + ".csm");
            } else {
                playMenuMusic(); menuSelection_ = 0; playingCustomMap_ = false;
                if (testPlayFromEditor_) { state_ = GameState::Editor; testPlayFromEditor_ = false; }
                else { state_ = GameState::MainMenu; }
            }
        }
    }
    else if (state_ == GameState::CustomWin) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 1) menuSelection_ = 1;
        if (confirmInput_) {
            playMenuMusic(); menuSelection_ = 0; playingCustomMap_ = false;
            if (testPlayFromEditor_) { state_ = GameState::Editor; testPlayFromEditor_ = false; }
            else { state_ = GameState::MainMenu; }
        }
    }
    else if (state_ == GameState::CharCreator) {
        auto& cc = charCreator_;
        if (modSaveDialog_.isOpen()) {
            if (cc.statusTimer > 0) cc.statusTimer -= dt_;
        }
        else if (cc.textEditing) {
            // Text input handled via SDL_TEXTINPUT events already processed
        } else {
            cc.field = menuSelection_;
            if (cc.field < 0) cc.field = 0;
            if (cc.field > 10) cc.field = 10;
            menuSelection_ = cc.field;
            
            // Tab/Shift+Tab for preview sections
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (cc.field < 1 || cc.field > 5) {
                static bool tabWasPressed = false;
                static bool shiftTabWasPressed = false;
                bool tabPressed = keys[SDL_SCANCODE_TAB] && !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                bool shiftTabPressed = keys[SDL_SCANCODE_TAB] && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                
                if (tabPressed && !tabWasPressed) {
                    cc.previewSection++;
                    if (cc.previewSection > 4) cc.previewSection = 0;
                    cc.previewFrame = 0; cc.animTime = 0.0f;
                }
                if (shiftTabPressed && !shiftTabWasPressed) {
                    cc.previewSection--;
                    if (cc.previewSection < 0) cc.previewSection = 4;
                    cc.previewFrame = 0; cc.animTime = 0.0f;
                }
                tabWasPressed = tabPressed;
                shiftTabWasPressed = shiftTabPressed;
                
                // P key to toggle play/pause, R key to hot-reload
                static bool pWasPressed = false, rWasPressed = false;
                bool pPressed = keys[SDL_SCANCODE_P];
                bool rPressed = keys[SDL_SCANCODE_R];
                if (pPressed && !pWasPressed) cc.playAnimation = !cc.playAnimation;
                if (rPressed && !rWasPressed && cc.loaded) {
                    // Hot-reload sprites
                    cc.charDef.reloadSprites(renderer_);
                    cc.warnings = cc.charDef.validate();
                    cc.statusMsg = "Sprites reloaded!";
                    cc.statusTimer = 2.0f;
                }
                pWasPressed = pPressed;
                rWasPressed = rPressed;
            }

            // Adjust values with left/right (fields 1-5 = stats)
            if (cc.field == 1 && (leftInput_ || rightInput_)) {
                cc.speed += rightInput_ ? 20.0f : -20.0f;
                if (cc.speed < 100.0f) cc.speed = 100.0f;
                if (cc.speed > 1200.0f) cc.speed = 1200.0f;
            }
            if (cc.field == 2 && (leftInput_ || rightInput_)) {
                cc.hp += rightInput_ ? 1 : -1;
                if (cc.hp < 1) cc.hp = 1;
                if (cc.hp > 50) cc.hp = 50;
            }
            if (cc.field == 3 && (leftInput_ || rightInput_)) {
                cc.ammo += rightInput_ ? 1 : -1;
                if (cc.ammo < 1) cc.ammo = 1;
                if (cc.ammo > 50) cc.ammo = 50;
            }
            if (cc.field == 4 && (leftInput_ || rightInput_)) {
                cc.fireRate += rightInput_ ? 1.0f : -1.0f;
                if (cc.fireRate < 1.0f) cc.fireRate = 1.0f;
                if (cc.fireRate > 30.0f) cc.fireRate = 30.0f;
            }
            if (cc.field == 5 && (leftInput_ || rightInput_)) {
                cc.reloadTime += rightInput_ ? 0.1f : -0.1f;
                if (cc.reloadTime < 0.1f) cc.reloadTime = 0.1f;
                if (cc.reloadTime > 5.0f) cc.reloadTime = 5.0f;
            }

            auto previewFrameCount = [&]() -> int {
                switch (cc.previewSection) {
                    case 1: return cc.loaded ? (int)cc.charDef.bodySprites.size() : (int)playerSprites_.size();
                    case 2: return cc.loaded ? (int)cc.charDef.legSprites.size() : (int)legSprites_.size();
                    case 3: return cc.loaded ? (int)cc.charDef.deathSprites.size() : (int)playerDeathSprites_.size();
                    default: return 1;
                }
            };
            if ((cc.field == 0 || cc.field >= 6) && (leftInput_ || rightInput_)) {
                int frameCount = previewFrameCount();
                if (frameCount > 1) {
                    cc.playAnimation = false;
                    cc.previewFrame += rightInput_ ? 1 : -1;
                    if (cc.previewFrame < 0) cc.previewFrame = frameCount - 1;
                    if (cc.previewFrame >= frameCount) cc.previewFrame = 0;
                }
            }

            // Mouse click handling for buttons (must happen before keyboard confirmInput_ check)
            if (ui_.mouseClicked && !usingGamepad_) {
                // Check if any button was clicked
                if (ui_.prevHoveredItem == 6) { cc.field = 6; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 7) { cc.field = 7; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 8) { cc.field = 8; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 9) { cc.field = 9; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 10) { cc.field = 10; confirmInput_ = true; }
            }

            // Name field: start text editing on confirm
            if (cc.field == 0 && confirmInput_) {
                cc.textEditing = true;
                cc.textBuf = cc.name;
                softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.", 16,
                             &cc.textBuf, 16, [this](bool confirmed) {
                    charCreator_.textEditing = false;
                    if (confirmed) charCreator_.name = charCreator_.textBuf;
                });
            }
            // Reload button
            if (cc.field == 6 && confirmInput_) {
                if (cc.loaded) {
                    cc.charDef.reloadSprites(renderer_);
                    cc.warnings = cc.charDef.validate();
                    cc.statusMsg = "Sprites reloaded!";
                    cc.statusTimer = 2.0f;
                } else if (!cc.folderPath.empty()) {
                    loadCharacterIntoCreator(cc.folderPath);
                } else {
                    // Try characters/<name>
                    std::string tryPath = "characters/" + cc.name;
                    loadCharacterIntoCreator(tryPath);
                }
            }
            // Test Play button
            if (cc.field == 7 && confirmInput_) {
                testCharacter();
            }
            // Save button
            if (cc.field == 8 && confirmInput_) {
                if (!modSaveDialog_.isOpen()) {
                    openModSaveDialog(ModSaveDialogState::AssetCharacter);
                }
            }
            // Save local button
            if (cc.field == 9 && confirmInput_) {
                std::string folder = cc.folderPath.empty() ? 
                    "characters/" + cc.name : cc.folderPath;
                saveCharacterToFolder(folder);
                cc.statusMsg = "Saved to " + folder + "/";
                cc.statusTimer = 3.0f;
            }
            // Back button
            if (cc.field == 10 && confirmInput_) {
                cc.clearPreviews(renderer_);
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }
        // Status timer
        if (cc.statusTimer > 0) cc.statusTimer -= dt_;

        if (backInput_ && !cc.textEditing) {
            cc.clearPreviews(renderer_);
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
    }
    // ── Map Pack states ──
    else if (state_ == GameState::PackSelect) {
        int maxIdx = std::max(0, (int)availablePacks_.size() - 1);
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > maxIdx + 1) menuSelection_ = maxIdx + 1; // +1 for BACK
        packSelectIdx_ = menuSelection_;
        if (backInput_) {
            if (prevMenuState_ == GameState::PlayModeMenu) {
                state_ = GameState::PlayModeMenu; playModeSelection_ = 2; menuSelection_ = 2;
            } else {
                state_ = GameState::MainMenu; menuSelection_ = 0;
            }
        }
        if (confirmInput_) {
            if (packSelectIdx_ <= maxIdx && !availablePacks_.empty()) {
                currentPack_ = availablePacks_[packSelectIdx_];
                currentPack_.reset();
                playingPack_ = true;
                // Load pack character if specified
                if (!currentPack_.characterPaths.empty()) {
                    packCharDef_ = CharacterDef{};
                    if (packCharDef_.loadFromFile(currentPack_.characterPaths[0], renderer_)) {
                        applyCharacter(packCharDef_);
                    }
                }
                startPackLevel();
            } else {
                if (prevMenuState_ == GameState::PlayModeMenu) {
                    state_ = GameState::PlayModeMenu; playModeSelection_ = 2; menuSelection_ = 2;
                } else {
                    state_ = GameState::MainMenu; menuSelection_ = 0;
                }
            }
        }
    }
    else if (state_ == GameState::PlayingPack) {
        if (pauseInput_) { state_ = GameState::PackPaused; menuSelection_ = 0; }
    }
    else if (state_ == GameState::PackPaused) {
        if (menuSelection_ < 0) menuSelection_ = 0;
#ifndef __SWITCH__
        if (menuSelection_ > 4) menuSelection_ = 4;
#else
        if (menuSelection_ > 3) menuSelection_ = 3;
#endif
        if (pauseInput_) state_ = GameState::PlayingPack;
        if (menuSelection_ == 1) {
            if (leftInput_) config_.musicVolume = std::max(0, config_.musicVolume - 8);
            if (rightInput_) config_.musicVolume = std::min(128, config_.musicVolume + 8);
            Mix_VolumeMusic(config_.musicVolume);
        }
        if (menuSelection_ == 2) {
            if (leftInput_) config_.sfxVolume = std::max(0, config_.sfxVolume - 8);
            if (rightInput_) config_.sfxVolume = std::min(128, config_.sfxVolume + 8);
        }
#ifndef __SWITCH__
        if (menuSelection_ == 3 && (confirmInput_ || leftInput_ || rightInput_)) {
            config_.fullscreen = !config_.fullscreen;
            SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            saveConfig();
        }
#endif
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::PlayingPack;
#ifndef __SWITCH__
            else if (menuSelection_ == 4) {
#else
            else if (menuSelection_ == 3) {
#endif
                playMenuMusic(); menuSelection_ = 0; playingPack_ = false;
                packCharDef_.unload();
                state_ = GameState::MainMenu;
            }
        }
    }
    else if (state_ == GameState::PackDead) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 1) menuSelection_ = 1;
        if (confirmInput_) {
            if (menuSelection_ == 0) {
                startPackLevel(); // Retry current level
            } else {
                playMenuMusic(); menuSelection_ = 0; playingPack_ = false;
                packCharDef_.unload();
                state_ = GameState::MainMenu;
            }
        }
    }
    else if (state_ == GameState::PackLevelWin) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 1) menuSelection_ = 1;
        if (confirmInput_) {
            if (menuSelection_ == 0) {
                advancePackLevel();
            } else {
                playMenuMusic(); menuSelection_ = 0; playingPack_ = false;
                packCharDef_.unload();
                state_ = GameState::MainMenu;
            }
        }
    }
    else if (state_ == GameState::PackComplete) {
        if (confirmInput_) {
            playMenuMusic(); menuSelection_ = 0; playingPack_ = false;
            packCharDef_.unload();
            state_ = GameState::MainMenu;
        }
    }
    // ── Multiplayer state input handling ──
    else if (state_ == GameState::MultiplayerMenu) {
        int totalItems = 3 + (int)savedServers_.size(); // HOST, JOIN, BACK + saved servers
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ >= totalItems) menuSelection_ = totalItems - 1;
        multiMenuSelection_ = menuSelection_;
        if (multiMenuSelection_ >= 3) serverListSelection_ = multiMenuSelection_ - 3;
        if (backInput_ || pauseInput_) { state_ = GameState::MainMenu; menuSelection_ = 0; }
        if (confirmInput_) {
            if (multiMenuSelection_ == 0) {
                // Host game
                scanMapFiles();
                state_ = GameState::HostSetup;
                hostSetupSelection_ = 0;
                hostSetupScrollY_   = 0;
                gamemodeSelectIdx_ = 0;
                hostMapSelectIdx_ = 0;
                lobbySettings_.isPvp            = false;
                lobbySettings_.friendlyFire     = false;
                lobbySettings_.pvpEnabled       = false;
                lobbySettings_.teamCount        = 0;
                lobbySettings_.upgradesShared   = false;
                lobbySettings_.mapWidth         = config_.mapWidth;
                lobbySettings_.mapHeight        = config_.mapHeight;
                lobbySettings_.enemyHpScale     = config_.enemyHpScale;
                lobbySettings_.enemySpeedScale  = config_.enemySpeedScale;
                lobbySettings_.spawnRateScale   = config_.spawnRateScale;
                lobbySettings_.playerMaxHp      = config_.playerMaxHp;
                lobbySettings_.livesPerPlayer   = 0;
                lobbySettings_.livesShared      = false;
                lobbySettings_.crateInterval    = 25.0f;
                lobbySettings_.pvpMatchDuration = 300.0f;
                lobbySettings_.waveCount        = 0;
                lobbySettings_.maxPlayers       = hostMaxPlayers_;
                menuSelection_ = 0;
            }
            else if (multiMenuSelection_ == 1) {
                // Quick connect (join)
                state_ = GameState::JoinMenu;
                joinMenuSelection_ = 0;
                menuSelection_ = 0;
                connectStatus_.clear();
            }
            else if (multiMenuSelection_ == 2) {
                // Back
                state_ = GameState::MainMenu; menuSelection_ = 0;
            }
            else {
                // Saved server — connect directly
                int sIdx = multiMenuSelection_ - 3;
                if (sIdx >= 0 && sIdx < (int)savedServers_.size()) {
                    joinAddress_ = savedServers_[sIdx].address;
                    joinPort_ = savedServers_[sIdx].port;
                    joinGame();
                }
            }
        }
        // X button / Delete key to remove saved server
        if (bombInput_ && multiMenuSelection_ >= 3) {
            int sIdx = multiMenuSelection_ - 3;
            removeSavedServer(sIdx);
            if (menuSelection_ >= totalItems - 1) menuSelection_ = totalItems - 2;
            if (menuSelection_ < 0) menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::HostSetup) {
        if (mpUsernameTyping_ || portTyping_ || hostPasswordTyping_) {
            // Editing consumes events; nothing else to do
        } else {
        constexpr int HOST_SETUP_ITEMS = 14;
        auto syncHostSetupRules = [this]() {
            lobbySettings_.maxPlayers = hostMaxPlayers_;
            if (lobbySettings_.isPvp) {
                currentRules_ = (lobbySettings_.teamCount >= 2)
                    ? createTeamDeathmatchRules(lobbySettings_.teamCount, 20, hostMaxPlayers_)
                    : createDeathmatchRules(20, hostMaxPlayers_);
            } else {
                currentRules_ = createCoopArenaRules(hostMaxPlayers_);
            }
            currentRules_.friendlyFire   = lobbySettings_.friendlyFire;
            currentRules_.pvpEnabled     = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
            currentRules_.upgradesShared = lobbySettings_.upgradesShared;
            currentRules_.teamCount      = lobbySettings_.teamCount;
            currentRules_.lives          = lobbySettings_.livesPerPlayer;
            currentRules_.sharedLives    = lobbySettings_.livesShared;

            auto& net = NetworkManager::instance();
            if (lobbySettings_.isPvp) {
                net.setGamemode(lobbySettings_.teamCount >= 2 ? "team_deathmatch" : "deathmatch");
            } else {
                net.setGamemode("coop_arena");
            }
        };

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ >= HOST_SETUP_ITEMS) menuSelection_ = HOST_SETUP_ITEMS - 1;
        hostSetupSelection_ = menuSelection_;

        if (backInput_ || pauseInput_) { state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0; }

        if (hostSetupSelection_ == 0) {
            if (leftInput_)  hostMaxPlayers_ = std::max(2, hostMaxPlayers_ - 1);
            if (rightInput_) hostMaxPlayers_ = std::min(128, hostMaxPlayers_ + 1);
        }
        else if (hostSetupSelection_ == 1 && confirmInput_) {
            portStr_ = std::to_string(hostPort_);
            portTyping_ = true;
            softKB_.open("0123456789", 10, &portStr_, 5, [this](bool) {
                portTyping_ = false;
                int v = portStr_.empty() ? 7777 : std::stoi(portStr_);
                hostPort_ = std::max(1024, std::min(65535, v));
                portStr_ = std::to_string(hostPort_);
            });
        }
        else if (hostSetupSelection_ == 2 && confirmInput_) {
            mpUsernameTyping_ = true;
            softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", 16,
                         &config_.username, 32, [this](bool) {
                mpUsernameTyping_ = false;
                if (config_.username.empty()) config_.username = "Player";
                NetworkManager::instance().setUsername(config_.username);
            });
        }
        else if (hostSetupSelection_ == 3 && confirmInput_) {
            hostPasswordTyping_ = true;
            softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-!@#$", 16,
                         &lobbyPassword_, 32, [this](bool) {
                hostPasswordTyping_ = false;
            });
        }

        if (hostSetupSelection_ == 4 && (leftInput_ || rightInput_ || confirmInput_)) {
            lobbySettings_.isPvp = !lobbySettings_.isPvp;
        }
        if (hostSetupSelection_ == 5 && (leftInput_ || rightInput_ || confirmInput_)) {
            int count = (int)mapFiles_.size();
            if (count > 0) {
                int dir = rightInput_ ? 1 : -1;
                if (confirmInput_ && !leftInput_ && !rightInput_) dir = 1;
                hostMapSelectIdx_ += dir;
                if (hostMapSelectIdx_ < 0) hostMapSelectIdx_ = count;
                if (hostMapSelectIdx_ > count) hostMapSelectIdx_ = 0;
            }
        }
        if (hostSetupSelection_ == 6 && (leftInput_ || rightInput_ || confirmInput_)) {
            int dir = rightInput_ ? 1 : -1;
            if (confirmInput_ && !leftInput_ && !rightInput_) dir = 1;
            int tc = lobbySettings_.teamCount;
            if (dir > 0) tc = (tc == 0) ? 2 : (tc == 2) ? 4 : 0;
            else         tc = (tc == 0) ? 4 : (tc == 4) ? 2 : 0;
            lobbySettings_.teamCount = tc;
        }
        if (hostSetupSelection_ == 7) {
            if (leftInput_)  lobbySettings_.playerMaxHp = std::max(1, lobbySettings_.playerMaxHp - 1);
            if (rightInput_) lobbySettings_.playerMaxHp = std::min(100, lobbySettings_.playerMaxHp + 1);
        }
        if (hostSetupSelection_ == 8) {
            if (leftInput_)  lobbySettings_.livesPerPlayer = std::max(0, lobbySettings_.livesPerPlayer - 1);
            if (rightInput_) lobbySettings_.livesPerPlayer = std::min(100, lobbySettings_.livesPerPlayer + 1);
            if (lobbySettings_.livesPerPlayer == 0) lobbySettings_.livesShared = false;
        }
        if (hostSetupSelection_ == 9) {
            int dir = rightInput_ ? 1 : -1;
            if (lobbySettings_.isPvp) {
                if (leftInput_ || rightInput_)
                    lobbySettings_.pvpMatchDuration = std::max(0.0f, std::min(3600.0f, lobbySettings_.pvpMatchDuration + dir * 30.0f));
            } else {
                if (leftInput_ || rightInput_) {
                    int step = (lobbySettings_.waveCount >= 10) ? 10 : 1;
                    lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * step));
                }
            }
        }
        if (hostSetupSelection_ == 10 && (leftInput_ || rightInput_ || confirmInput_)) {
            bool forceOn = lobbySettings_.isPvp && lobbySettings_.teamCount == 0;
            if (!forceOn) {
                lobbySettings_.friendlyFire = !lobbySettings_.friendlyFire;
            }
        }
        if (hostSetupSelection_ == 11) {
            if ((leftInput_ || rightInput_) && !serverPresets_.empty()) {
                int n = (int)serverPresets_.size();
                if (leftInput_)  presetSelection_ = (presetSelection_ - 1 + n) % n;
                if (rightInput_) presetSelection_ = (presetSelection_ + 1) % n;
            }
            if (confirmInput_ && !serverPresets_.empty()) {
                applyServerPreset(presetSelection_);
            }
        }

        syncHostSetupRules();

        if (confirmInput_) {
            if (hostSetupSelection_ == 12) {
                hostGame();
            }
            else if (hostSetupSelection_ == 13) {
                state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
            }
        }
        } // end !mpUsernameTyping_
    }
    else if (state_ == GameState::JoinMenu) {
        if (!ipTyping_ && !mpUsernameTyping_ && !joinPortTyping_ && !joinPasswordTyping_) {
            if (menuSelection_ < 0) menuSelection_ = 0;
            if (menuSelection_ > 6) menuSelection_ = 6;
            joinMenuSelection_ = menuSelection_;

            if (backInput_ || pauseInput_) {
                connectStatus_.clear();
                state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
            }
            if (confirmInput_) {
                if (joinMenuSelection_ == 0) {
                    // Edit address
                    ipTyping_ = true;
                    softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-:", 11, &joinAddress_, 64, [this](bool confirmed) {
                        ipTyping_ = false;
                        if (confirmed && !joinAddress_.empty()) joinGame();
                    });
                } else if (joinMenuSelection_ == 1) {
                    // Edit port
                    joinPortStr_ = std::to_string(joinPort_);
                    joinPortTyping_ = true;
                    softKB_.open("0123456789", 10, &joinPortStr_, 5, [this](bool) {
                        joinPortTyping_ = false;
                        int v = joinPortStr_.empty() ? 7777 : std::stoi(joinPortStr_);
                        joinPort_ = std::max(1024, std::min(65535, v));
                        joinPortStr_ = std::to_string(joinPort_);
                    });
                } else if (joinMenuSelection_ == 2) {
                    // Edit username
                    mpUsernameTyping_ = true;
                    softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", 16,
                                 &config_.username, 32, [this](bool) {
                        mpUsernameTyping_ = false;
                        if (config_.username.empty()) config_.username = "Player";
                        NetworkManager::instance().setUsername(config_.username);
                    });
                } else if (joinMenuSelection_ == 3) {
                    // Edit password
                    joinPasswordTyping_ = true;
                    softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-!@#$", 16,
                                 &joinPassword_, 32, [this](bool) {
                        joinPasswordTyping_ = false;
                    });
                } else if (joinMenuSelection_ == 4) {
                    // Connect
                    joinGame();
                } else if (joinMenuSelection_ == 5) {
                    // Save server
                    addSavedServer(joinAddress_, joinAddress_, joinPort_);
                    connectStatus_ = "Server saved!";
                } else if (joinMenuSelection_ == 6) {
                    // Back
                    connectStatus_.clear();
                    state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
                }
            }
        }
        // (while ipTyping_, joinPortTyping_, mpUsernameTyping_, or joinPasswordTyping_, events are consumed above)
    }
    else if (state_ == GameState::Lobby) {
        auto& net = NetworkManager::instance();

#ifndef __SWITCH__
        // On PC, auto-join keyboard+mouse as P1
        if (!coopSlots_[0].joined) {
            coopSlots_[0].joined = true;
            coopSlots_[0].joyInstanceId = -1;
        }
        coopSlots_[0].username = config_.username;
#endif
        int localSubPlayers = 0;
        for (int i = 1; i < 4; i++) if (coopSlots_[i].joined) localSubPlayers++;
        if (localSubPlayers != lobbySubPlayersSent_) {
            net.setLocalSubPlayers((uint8_t)localSubPlayers);
            lobbySubPlayersSent_ = localSubPlayers;
        }

        bool canManageLobby = net.isLobbyHost();

        // Check if the connection was lost or timed out
        if (!net.isHost()) {
            if (net.state() == NetState::Connecting) {
                connectTimer_ -= dt_;
                if (connectTimer_ <= 0) {
                    connectStatus_ = "Connection timed out";
                    net.disconnect();
                    state_ = GameState::JoinMenu;
                    menuSelection_ = 0;
                    ipTyping_ = false;
                }
            } else if (net.state() == NetState::Offline) {
                connectStatus_ = "Connection failed";
                state_ = GameState::JoinMenu;
                menuSelection_ = 0;
                ipTyping_ = false;
            }
        }

        // ── Lobby kick mode (host only: TAB/Y toggles, LEFT/RIGHT selects, A kicks) ─────
        if (canManageLobby && tabInput_) {
            lobbyKickCursor_ = (lobbyKickCursor_ < 0) ? 0 : -1;
        }
        if (lobbyKickCursor_ >= 0) {
            // Navigate the vertical player list with UP/DOWN (menuSelection_ delta)
            int pCount = (int)net.players().size();
            if (pCount > 0) {
                int menuDelta = menuSelection_ - lobbySettingsSel_;
                if (menuDelta != 0) {
                    lobbyKickCursor_ = ((lobbyKickCursor_ + menuDelta) % pCount + pCount) % pCount;
                }
                lobbyKickCursor_ = std::max(0, std::min(pCount - 1, lobbyKickCursor_));
            }
            // Restore menuSelection_ so the lobby settings cursor doesn't move
            menuSelection_ = lobbySettingsSel_;

            if (confirmInput_ && pCount > 0) {
                // A button = kick
                uint8_t tid = net.players()[lobbyKickCursor_].id;
                if (tid != net.lobbyHostId())
                    net.sendAdminKick(tid);
                lobbyKickCursor_ = -1;
                confirmInput_ = false;
            }
            if (bombInput_ && pCount > 0) {
                // X button = transfer lobby host permissions
                uint8_t tid = net.players()[lobbyKickCursor_].id;
                if (tid != net.lobbyHostId()) net.sendLobbyHostTransfer(tid);
                lobbyKickCursor_ = -1;
                bombInput_ = false;
            }
            if (backInput_) { lobbyKickCursor_ = -1; backInput_ = false; }
            // Consume left/right so settings don't change while in kick mode
            leftInput_ = false; rightInput_ = false;
        }

        if (backInput_ || pauseInput_) {
            net.disconnect();
            connectStatus_.clear();
            lobbyKickCursor_ = -1;
            bannedPlayerIds_.clear();
            for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
            lobbySubPlayersSent_ = -1;
            state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
        }
        // Compute setting count here so we can check preset indices before the start-game confirm
        if (canManageLobby) {
            int _SC = 13;
            if (lobbySettings_.livesPerPlayer == 0) _SC = 12;
            _SC++; // CrateInterval or WaveCount
            if (lobbySettings_.isPvp) _SC++; // Match Time (PVP only)
            int _PSave = _SC;      // "Save as Preset" row index
            int _PLoad = _SC + 1;  // "Load Preset" row index
            // Intercept confirm when cursor is on a preset row (prevents starting the game)
            if (confirmInput_ && lobbyKickCursor_ < 0 && lobbySettingsSel_ == _PSave) {
                presetNameBuf_ = "My Preset";
                softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-", 16,
                             &presetNameBuf_, 32, [this](bool confirmed) {
                    if (confirmed && !presetNameBuf_.empty())
                        addServerPreset(presetNameBuf_, "arena", lobbySettings_.maxPlayers, hostPort_, 0, lobbySettings_);
                });
                confirmInput_ = false;
            } else if (confirmInput_ && lobbyKickCursor_ < 0 && lobbySettingsSel_ == _PLoad) {
                if (!serverPresets_.empty()) {
                    applyServerPreset(presetSelection_);
                    net.sendConfigSync(lobbySettings_);
                }
                confirmInput_ = false;
            }
        }
        if (confirmInput_ && lobbyKickCursor_ < 0) {
            if (canManageLobby) {
                if (net.isHost()) startMultiplayerGame();
                else net.requestStartGame();
            } else {
                // Only allow ready-up once actually connected to lobby
                if (net.state() == NetState::InLobby) {
                    lobbyReady_ = !lobbyReady_;
                    net.setReady(lobbyReady_);
                }
            }
        }
        // Host can adjust lobby settings with UP/DOWN/LEFT/RIGHT
        if (canManageLobby) {
            // Settings row list:
            //   0=Gamemode, 1=FriendlyFire, 2=Upgrades, 3=Map, 4=MapWidth, 5=MapHeight,
            //   6=EnemyHP, 7=EnemySpeed, 8=SpawnRate, 9=PlayerHP,
            //   10=TeamCount, 11=Lives, 12=LivesMode, 13=CrateInterval(PVP)/WaveCount(PVE)
            // Compute effective setting count based on current mode
            int SETTING_COUNT = 13; // base: 0-12 (includes Map selector)
            if (lobbySettings_.livesPerPlayer == 0) SETTING_COUNT = 12; // hide LivesMode
            // Add conditional setting
            if (lobbySettings_.isPvp) SETTING_COUNT++; // CrateInterval
            else SETTING_COUNT++; // WaveCount
            // Match Time: PVP only
            if (lobbySettings_.isPvp) SETTING_COUNT++; // pvpMatchDuration
            // Add preset rows (always at end)
            int presetSaveIdx = SETTING_COUNT;     // save current settings as preset
            int presetLoadIdx = SETTING_COUNT + 1; // load / apply a saved preset
            SETTING_COUNT += 2;

            if (menuSelection_ < 0) menuSelection_ = SETTING_COUNT - 1;
            if (menuSelection_ >= SETTING_COUNT) menuSelection_ = 0;
            lobbySettingsSel_ = menuSelection_;

            if (leftInput_ || rightInput_) {
                int dir = rightInput_ ? 1 : -1;
                switch (lobbySettingsSel_) {
                    case 0: // Gamemode (PVP vs PVE)
                        lobbySettings_.isPvp = !lobbySettings_.isPvp;
                        lobbySettings_.pvpEnabled   = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
                        currentRules_.pvpEnabled    = lobbySettings_.pvpEnabled;
                        break;
                    case 1: // PvP/Friendly fire — disabled when PvP-no-teams (always-on)
                        if (!(lobbySettings_.isPvp && lobbySettings_.teamCount == 0)) {
                            lobbySettings_.friendlyFire = !lobbySettings_.friendlyFire;
                            lobbySettings_.pvpEnabled   = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
                            currentRules_.friendlyFire  = lobbySettings_.friendlyFire;
                            currentRules_.pvpEnabled    = lobbySettings_.pvpEnabled;
                        }
                        break;
                    case 2: // Upgrades shared
                        lobbySettings_.upgradesShared = !lobbySettings_.upgradesShared;
                        break;
                    case 3: { // Map selection (0=Generated, 1+=custom map files)
                        int nm = (int)mapFiles_.size();
                        if (nm > 0) {
                            int newIdx = lobbyMapIdx_ + dir;
                            if (newIdx < 0) newIdx = nm;   // wrap to last custom
                            if (newIdx > nm) newIdx = 0;   // wrap to Generated
                            lobbyMapIdx_ = newIdx;
                            if (lobbyMapIdx_ == 0) {
                                net.setMap("", "");
                            } else {
                                const std::string& mf = mapFiles_[lobbyMapIdx_ - 1];
                                size_t sl = mf.rfind('/'); if (sl == std::string::npos) sl = mf.rfind('\\');
                                std::string mname = (sl != std::string::npos) ? mf.substr(sl + 1) : mf;
                                size_t dot = mname.rfind('.'); if (dot != std::string::npos) mname = mname.substr(0, dot);
                                net.setMap(mf, mname);
                            }
                        }
                        break;
                    }
                    case 4: // Map width (only when generated map)
                        if (lobbyMapIdx_ == 0)
                            lobbySettings_.mapWidth = std::max(10, std::min(200, lobbySettings_.mapWidth + dir * 10));
                        break;
                    case 5: // Map height (only when generated map)
                        if (lobbyMapIdx_ == 0)
                            lobbySettings_.mapHeight = std::max(10, std::min(200, lobbySettings_.mapHeight + dir * 10));
                        break;
                    case 6: // Enemy HP (disabled in PVP)
                        if (!lobbySettings_.isPvp)
                            lobbySettings_.enemyHpScale = std::max(0.1f, std::min(5.0f, lobbySettings_.enemyHpScale + dir * 0.1f));
                        break;
                    case 7: // Enemy speed (disabled in PVP)
                        if (!lobbySettings_.isPvp)
                            lobbySettings_.enemySpeedScale = std::max(0.1f, std::min(5.0f, lobbySettings_.enemySpeedScale + dir * 0.1f));
                        break;
                    case 8: // Spawn rate (disabled in PVP)
                        if (!lobbySettings_.isPvp)
                            lobbySettings_.spawnRateScale = std::max(0.1f, std::min(5.0f, lobbySettings_.spawnRateScale + dir * 0.1f));
                        break;
                    case 9: // Player HP
                        lobbySettings_.playerMaxHp = std::max(1, std::min(100, lobbySettings_.playerMaxHp + dir));
                        break;
                    case 10: { // Team count
                        int tc = lobbySettings_.teamCount;
                        if (dir > 0) tc = (tc == 0) ? 2 : (tc == 2) ? 4 : 0;
                        else         tc = (tc == 0) ? 4 : (tc == 4) ? 2 : 0;
                        lobbySettings_.teamCount = tc;
                        currentRules_.teamCount = tc;
                        break;
                    }
                    case 11: // Lives per player (0-100)
                        lobbySettings_.livesPerPlayer = std::max(0, std::min(100, lobbySettings_.livesPerPlayer + dir));
                        break;
                    case 12: // Lives mode OR conditional setting
                        if (lobbySettings_.livesPerPlayer > 0) {
                            // LivesMode
                            lobbySettings_.livesShared = !lobbySettings_.livesShared;
                        } else if (lobbySettings_.isPvp) {
                            // CrateInterval (no livesMode visible, so 12 = crateInterval)
                            lobbySettings_.crateInterval = std::max(5.0f, std::min(120.0f, lobbySettings_.crateInterval + dir * 5.0f));
                        } else {
                            // WaveCount (no livesMode visible, so 12 = waveCount)
                            lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * (lobbySettings_.waveCount >= 10 ? 10 : 1)));
                        }
                        break;
                    case 13: // CrateInterval/WaveCount (when lives>0) OR MatchTime (when lives==0, isPvp)
                        if (lobbySettings_.livesPerPlayer > 0) {
                            // condIdx=13: CrateInterval or WaveCount
                            if (lobbySettings_.isPvp) {
                                lobbySettings_.crateInterval = std::max(5.0f, std::min(120.0f, lobbySettings_.crateInterval + dir * 5.0f));
                            } else {
                                lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * (lobbySettings_.waveCount >= 10 ? 10 : 1)));
                            }
                        } else if (lobbySettings_.isPvp) {
                            // lives==0, isPvp → matchTimeIdx=13
                            lobbySettings_.pvpMatchDuration = std::max(0.0f, std::min(3600.0f, lobbySettings_.pvpMatchDuration + dir * 30.0f));
                        }
                        // else lives==0, !isPvp → presetSaveIdx=13 (no left/right effect)
                        break;
                    default:
                        // lives>0, isPvp → matchTimeIdx=14
                        if (lobbySettings_.isPvp && lobbySettings_.livesPerPlayer > 0 && lobbySettingsSel_ == 14) {
                            lobbySettings_.pvpMatchDuration = std::max(0.0f, std::min(3600.0f, lobbySettings_.pvpMatchDuration + dir * 30.0f));
                        }
                        // presetLoadIdx: cycle through presets with left/right
                        if (lobbySettingsSel_ == presetLoadIdx && !serverPresets_.empty()) {
                            int n = (int)serverPresets_.size();
                            if (leftInput_)  presetSelection_ = (presetSelection_ - 1 + n) % n;
                            if (rightInput_) presetSelection_ = (presetSelection_ + 1) % n;
                        }
                        break;
                }
                // Sync to all clients after any change
                net.sendConfigSync(lobbySettings_);
            }
        }
    }
    else if (state_ == GameState::MultiplayerGame) {
        if (pauseInput_) { state_ = GameState::MultiplayerPaused; menuSelection_ = 0; pauseMenuSub_ = 0; }
    }
    else if (state_ == GameState::MultiplayerSpectator) {
        if (pauseInput_) { state_ = GameState::MultiplayerPaused; menuSelection_ = 0; pauseMenuSub_ = 0; }
    }
    else if (state_ == GameState::MultiplayerPaused) {
        auto& net2 = NetworkManager::instance();
        bool hasTeams    = currentRules_.teamCount >= 2;
        bool isHostPlayer = net2.isLobbyHost();

        // ── Admin overlay ──────────────────────────────────────────────
        if (adminMenuOpen_) {
            const auto& players = net2.players();
            int pCount = (int)players.size();
            // Admin row selection mirrors global menuSelection_ (DPAD up/down)
            if (pCount > 0) {
                if (menuSelection_ < 0) menuSelection_ = 0;
                if (menuSelection_ >= pCount) menuSelection_ = pCount - 1;
                adminMenuSel_ = menuSelection_;
            }
            if (leftInput_)  adminMenuAction_ = (adminMenuAction_ + 3) % 4;
            if (rightInput_) adminMenuAction_ = (adminMenuAction_ + 1) % 4;
            if (confirmInput_ && pCount > 0) {
                uint8_t tid = players[adminMenuSel_].id;
                switch (adminMenuAction_) {
                    case 0: // Kick
                        if (tid != net2.localPlayerId())
                            net2.sendAdminKick(tid);
                        break;
                    case 1: // Respawn
                        net2.sendAdminRespawn(tid);
                        break;
                    case 2: // Team −
                        if (hasTeams) {
                            int8_t nt = (int8_t)((players[adminMenuSel_].team - 1 + currentRules_.teamCount) % currentRules_.teamCount);
                            net2.sendAdminTeamMove(tid, nt);
                        }
                        break;
                    case 3: // Team +
                        if (hasTeams) {
                            int8_t nt = (int8_t)((players[adminMenuSel_].team + 1) % currentRules_.teamCount);
                            net2.sendAdminTeamMove(tid, nt);
                        }
                        break;
                }
            }
            if (backInput_ || pauseInput_) { adminMenuOpen_ = false; menuSelection_ = 0; }
            return;
        }

        // ── Team-pick sub-state ────────────────────────────────────────
        if (pauseMenuSub_ == 1) {
            int tc = currentRules_.teamCount; if (tc < 2) tc = 2;
            if (leftInput_)  pauseTeamCursor_ = (pauseTeamCursor_ - 1 + tc) % tc;
            if (rightInput_) pauseTeamCursor_ = (pauseTeamCursor_ + 1) % tc;
            if (confirmInput_) {
                localTeam_ = (int8_t)pauseTeamCursor_;
                net2.sendTeamAssignment(net2.localPlayerId(), localTeam_);
                player_.dead = false;
                player_.hp   = player_.maxHp;
                spectatorMode_ = false;
                localLives_    = (currentRules_.lives > 0) ? currentRules_.lives : -1;
                state_         = GameState::MultiplayerGame;
                pauseMenuSub_  = 0;
            }
            if (backInput_) pauseMenuSub_ = 0;
            return;
        }

        // ── Main pause list ────────────────────────────────────────────
        //  0 = Resume
        //  1 = Music Volume
        //  2 = SFX Volume
        //  3 = Change Team   (if hasTeams)
        //  next = Admin Menu  (if isHostPlayer)
        //  next = End Game   (if isHostPlayer)
        //  last = Disconnect / Back to Lobby
        int resumeIdx = 0;
        int musicIdx  = 1;
        int sfxIdx    = 2;
        int teamIdx   = hasTeams ? 3 : -1;
        int adminIdx  = isHostPlayer ? (hasTeams ? 4 : 3) : -1;
        int endGameIdx = -1;
        int nextIdx   = 3 + (hasTeams ? 1 : 0) + (isHostPlayer ? 1 : 0);
        if (isHostPlayer) { endGameIdx = nextIdx; nextIdx++; }
        // Host cannot disconnect mid-game (must use End Game instead)
        int dcIdx     = isHostPlayer ? -1 : nextIdx;
        int maxSel    = isHostPlayer ? (nextIdx - 1) : nextIdx;

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > maxSel) menuSelection_ = maxSel;

        // Volume adjustment with left/right
        if (menuSelection_ == musicIdx) {
            if (leftInput_) config_.musicVolume = std::max(0, config_.musicVolume - 8);
            if (rightInput_) config_.musicVolume = std::min(128, config_.musicVolume + 8);
            Mix_VolumeMusic(config_.musicVolume);
        }
        if (menuSelection_ == sfxIdx) {
            if (leftInput_) config_.sfxVolume = std::max(0, config_.sfxVolume - 8);
            if (rightInput_) config_.sfxVolume = std::min(128, config_.sfxVolume + 8);
        }

        if (pauseInput_) {
            state_ = spectatorMode_ ? GameState::MultiplayerSpectator : GameState::MultiplayerGame;
        }
        if (confirmInput_) {
            if (menuSelection_ == resumeIdx) {
                state_ = spectatorMode_ ? GameState::MultiplayerSpectator : GameState::MultiplayerGame;
            } else if (hasTeams && menuSelection_ == teamIdx) {
                pauseMenuSub_   = 1;
                pauseTeamCursor_ = (localTeam_ >= 0) ? (int)localTeam_ : 0;
                menuSelection_  = 0;
            } else if (isHostPlayer && menuSelection_ == adminIdx) {
                adminMenuOpen_   = true;
                adminMenuSel_    = 0;
                adminMenuAction_ = 0;
            } else if (isHostPlayer && menuSelection_ == endGameIdx) {
                // End game — return everyone to lobby
                net2.sendGameEnd((uint8_t)MatchEndReason::HostEnded);
            } else if (menuSelection_ == dcIdx) {
                net2.disconnect();
                playMenuMusic();
                state_         = GameState::MainMenu;
                menuSelection_ = 0;
                spectatorMode_ = false;
            }
        }
    }
    else if (state_ == GameState::MultiplayerDead) {
        // Wait for respawn (automatic from updateMultiplayer)
        if (respawnTimer_ <= 0) respawnTimer_ = currentRules_.respawnTime;
    }
    else if (state_ == GameState::WinLoss) {
        // Any confirm or back → proceed to full scoreboard
        if (confirmInput_ || backInput_) {
            state_ = GameState::Scoreboard;
            menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::Scoreboard) {
        if (confirmInput_ || backInput_) {
            // Return to lobby instead of main menu
            state_ = GameState::Lobby;
            menuSelection_ = 0;
            lobbySettingsScrollY_ = 0;
        }
    }
    else if (state_ == GameState::TeamSelect) {
        auto& net = NetworkManager::instance();
        int tc = lobbySettings_.teamCount;
        if (tc < 2) tc = 2;

        if (!teamLocked_) {
            if (leftInput_)  teamSelectCursor_ = (teamSelectCursor_ - 1 + tc) % tc;
            if (rightInput_) teamSelectCursor_ = (teamSelectCursor_ + 1) % tc;
            if (confirmInput_) {
                localTeam_ = (int8_t)teamSelectCursor_;
                teamLocked_ = true;
                net.sendTeamAssignment(net.localPlayerId(), localTeam_);
                // If host, check if all players have teams assigned and start
                if (net.isHost()) {
                    bool allAssigned = true;
                    for (auto& p : net.players()) {
                        if (p.team < 0) { allAssigned = false; break; }
                    }
                    if (allAssigned) {
                        // Actually launch the game now
                        std::string customMapFile = net.lobbyInfo().mapFile;
                        std::vector<uint8_t> customMapData;
                        if (!customMapFile.empty()) {
                            FILE* f = fopen(customMapFile.c_str(), "rb");
                            if (f) {
                                fseek(f, 0, SEEK_END);
                                long sz = ftell(f);
                                fseek(f, 0, SEEK_SET);
                                customMapData.resize(sz);
                                fread(customMapData.data(), 1, sz, f);
                                fclose(f);
                                startCustomMapMultiplayer(customMapFile);
                                if (lobbySettings_.isPvp) player_.invulnDuration = 0.0f;
                                state_ = GameState::MultiplayerGame;
                                net.startGame(0, map_.width, map_.height, customMapData);
                                respawnTimer_ = currentRules_.respawnTime;
                            }
                        }
                        if (state_ != GameState::MultiplayerGame) {
                            uint32_t mapSeed = (uint32_t)time(nullptr) ^ (uint32_t)rand();
                            mapSrand(mapSeed);
                            startGame();
                            player_.pos = pickSpawnPos(); // team corner or random spawn
                            if (lobbySettings_.isPvp) player_.invulnDuration = 0.0f;
                            state_ = GameState::MultiplayerGame;
                            net.startGame(mapSeed, config_.mapWidth, config_.mapHeight);
                            respawnTimer_ = currentRules_.respawnTime;
                        }
                    }
                }
            }
        }
        if (backInput_) {
            // Go back to lobby
            teamLocked_ = false;
            localTeam_ = -1;
            // Reset all team assignments
            for (auto& p : const_cast<std::vector<NetPlayer>&>(net.players())) {
                p.team = -1;
            }
            state_ = GameState::Lobby;
            menuSelection_ = 0;
            lobbySettingsScrollY_ = 0;
            lobbyKickCursor_ = -1;
        }
    }
    else if (state_ == GameState::ModMenu) {
        auto& mm = ModManager::instance();

        // Tab switch with left/right
        if (leftInput_)  { modMenuTab_ = (modMenuTab_ + 3) % 4; modMenuSelection_ = 0; menuSelection_ = 0; }
        if (rightInput_) { modMenuTab_ = (modMenuTab_ + 1) % 4; modMenuSelection_ = 0; menuSelection_ = 0; }

        if (backInput_ || pauseInput_) {
            mm.saveModConfig();
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }

        if (modMenuTab_ == 0) {
            // Mods tab: enable/disable
            int maxIdx = (int)mm.mods().size(); // includes BACK option
            if (modMenuSelection_ < 0) modMenuSelection_ = 0;
            if (modMenuSelection_ > maxIdx) modMenuSelection_ = maxIdx;
            if (menuSelection_ != modMenuSelection_) modMenuSelection_ = menuSelection_;

            if (confirmInput_) {
                if (modMenuSelection_ < (int)mm.mods().size()) {
                    auto& mods = mm.mods();
                    std::string id = mods[modMenuSelection_].id;
                    mm.setEnabled(id, !mods[modMenuSelection_].enabled);
                    applyModOverrides();
                } else {
                    mm.saveModConfig();
                    state_ = GameState::MainMenu;
                    menuSelection_ = 0;
                }
            }
        } else {
            // Content tabs (Characters/Maps/Playlists): selection only, no action yet
            std::vector<std::string>* paths = nullptr;
            std::vector<std::string> chars, maps, packs;
            if (modMenuTab_ == 1) { chars = mm.allCharacterPaths(); paths = &chars; }
            else if (modMenuTab_ == 2) { maps = mm.allMapPaths();    paths = &maps; }
            else if (modMenuTab_ == 3) { packs = mm.allPackPaths();  paths = &packs; }

            if (paths) {
                int maxIdx = (int)paths->size();
                if (modMenuSelection_ < 0) modMenuSelection_ = 0;
                if (modMenuSelection_ > maxIdx) modMenuSelection_ = maxIdx;
                if (menuSelection_ != modMenuSelection_) modMenuSelection_ = menuSelection_;
            }

            if (confirmInput_ && modMenuSelection_ == (int)(paths ? paths->size() : 0)) {
                mm.saveModConfig();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Update
// ═════════════════════════════════════════════════════════════════════════════

void Game::update() {
    float dt = dt_;
    gameTime_ += dt;

    screenFlashTimer_  = std::max(0.0f, screenFlashTimer_ - dt);
    muzzleFlashTimer_  = std::max(0.0f, muzzleFlashTimer_ - dt);
    pickupPopupTimer_  = std::max(0.0f, pickupPopupTimer_ - dt);
    waveAnnounceTimer_ = std::max(0.0f, waveAnnounceTimer_ - dt);
    cratePopupTimer_   = std::max(0.0f, cratePopupTimer_ - dt);

    // Only run gameplay logic in active playing states
    bool isPlayingState =
        state_ == GameState::Playing || state_ == GameState::Paused || state_ == GameState::Dead ||
        state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused ||
        state_ == GameState::CustomDead || state_ == GameState::CustomWin ||
        state_ == GameState::PlayingPack || state_ == GameState::PackPaused ||
        state_ == GameState::PackDead || state_ == GameState::PackLevelWin ||
        state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
        state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator ||
        state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused;

    if (isPlayingState) {
        bool isCoopState = (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused);
        bool isMPSplitscreen = !isCoopState && coopPlayerCount_ > 1 &&
            (state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
             state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);

        if (isCoopState || isMPSplitscreen) {
            updateLocalCoopPlayers(dt);
        } else {
            activeLocalPlayerSlot_ = 0;
            updatePlayer(dt);
        }
        updateEnemies(dt);
        updateBullets(dt);
        updateBombs(dt);
        updateExplosions(dt);
        updateBoxFragments(dt);
        updateSpawning(dt);
        updateCrates(dt);
        updatePickups(dt);
        bool coopSlot0Alive = (isCoopState || isMPSplitscreen) && !player_.dead;
        resolveCollisions();

        // Sync slot 0 back after resolveCollisions (damage, death, etc.)
        if (isCoopState || isMPSplitscreen) {
            coopSlots_[0].player   = player_;
            coopSlots_[0].upgrades = upgrades_;
            if (coopSlot0Alive && player_.dead) coopSlots_[0].deaths++;
        }

        // Camera — co-op/splitscreen cameras are updated inside updateLocalCoopPlayers
        if (!isCoopState && !isMPSplitscreen) {
            Vec2 aimDir = {0,0};
            if (aimInput_.lengthSq() > 0.04f) aimDir = aimInput_.normalized();
            else if (player_.moving && player_.vel.lengthSq() > 1.0f) aimDir = player_.vel.normalized();
            camera_.update(player_.pos, aimDir, dt);
        }

        // Clean up dead entities
        auto removeDeadEntities = [](auto& vec) {
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [](const auto& e) { return !e.alive; }), vec.end());
        };
        removeDeadEntities(enemies_);
        removeDeadEntities(bullets_);
        removeDeadEntities(enemyBullets_);
        removeDeadEntities(bombs_);
        removeDeadEntities(explosions_);
        removeDeadEntities(debris_);
    }

    // Always update multiplayer (even in lobby/menus for network events)
    updateMultiplayer(dt);

    // Character Creator animation update
    if (state_ == GameState::CharCreator) {
        auto& cc = charCreator_;
        if (cc.playAnimation) {
            cc.animTime += dt;
            float frameTime = 0.1f;
            if (cc.animTime >= frameTime) {
                cc.animTime -= frameTime;
                cc.previewFrame++;
                
                int maxFrames = 0;
                switch (cc.previewSection) {
                    case 0: maxFrames = 1; break;
                    case 1: maxFrames = cc.loaded ? (int)cc.charDef.bodySprites.size() : (int)playerSprites_.size(); break;
                    case 2: maxFrames = cc.loaded ? (int)cc.charDef.legSprites.size() : (int)legSprites_.size(); break;
                    case 3: maxFrames = cc.loaded ? (int)cc.charDef.deathSprites.size() : (int)playerDeathSprites_.size(); break;
                    case 4: maxFrames = 1; break;
                }
                if (maxFrames > 0 && cc.previewFrame >= maxFrames) cc.previewFrame = 0;
            }
        }
    }

    // Mod-save for editor maps (character creator no longer uses mod save dialog)
    if (!modSaveDialog_.isOpen()) {
        if (charCreatorWantsModSave_) {
            charCreatorWantsModSave_ = false;
            openModSaveDialog(ModSaveDialogState::AssetCharacter);
        }
    }
    // ── When dialog confirmed, execute the actual save ───────────────────────
    if (modSaveDialog_.confirmed) {
        modSaveDialog_.confirmed = false;
        const std::string& folder = modSaveDialog_.confirmedModFolder;
        switch (modSaveDialog_.asset) {
            case ModSaveDialogState::AssetMap:
                editor_.performModSave(folder);
                break;
            case ModSaveDialogState::AssetCharacter:
                if (!folder.empty()) {
                    auto& cc = charCreator_;
                    std::string safeName = cc.name;
                    for (char& ch : safeName) {
                        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
                            continue;
                        }
                        if (ch == ' ') ch = '_';
                        else ch = '_';
                    }
                    if (safeName.empty()) safeName = "NewChar";
                    std::string charFolder = folder + "/characters/" + safeName;
                    saveCharacterToFolder(charFolder);
                    cc.folderPath = charFolder;
                    cc.isEditing = true;
                    cc.statusMsg = "Saved to " + charFolder;
                    cc.statusTimer = 3.0f;
                }
                break;
        }
        ModManager::instance().scanMods();
        scanCharacters();
        modSaveDialog_.close();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Player Update
// ═════════════════════════════════════════════════════════════════════════════

void Game::updatePlayer(float dt) {
    Player& p = player_;
    if (p.dead) {
        bool isSecondaryLocalViewport = activeLocalPlayerSlot_ > 0 &&
            (state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
             state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);
        p.deathTimer += dt;
        // Death animation
        if (!playerDeathSprites_.empty()) {
            float frameTime = 0.06f;
            p.animTimer += dt;
            if (p.animTimer >= frameTime) {
                p.animTimer -= frameTime;
                p.animFrame++;
                if (p.animFrame >= (int)playerDeathSprites_.size())
                    p.animFrame = (int)playerDeathSprites_.size() - 1;
            }
        }
        if (isSecondaryLocalViewport) {
            return;
        }
        if (p.deathTimer > 1.5f && state_ != GameState::Dead
            && state_ != GameState::MultiplayerDead
            && state_ != GameState::CustomDead
            && state_ != GameState::PackDead
            && state_ != GameState::LocalCoopGame
            && state_ != GameState::LocalCoopPaused) {
            // Transition to the appropriate death state
            if (state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused) {
                // Check individual lives
                if (currentRules_.lives > 0 && !currentRules_.sharedLives) {
                    if (localLives_ > 0) localLives_--;
                    if (localLives_ <= 0) {
                        // Out of lives — become a spectator ghost
                        spectatorMode_ = true;
                        player_.dead   = false;
                        player_.hp     = 1;
                        state_ = GameState::MultiplayerSpectator;
                        menuSelection_ = 0;
                    } else {
                        state_ = GameState::MultiplayerDead;
                        respawnTimer_ = currentRules_.respawnTime;
                    }
                } else {
                    state_ = GameState::MultiplayerDead;
                    respawnTimer_ = currentRules_.respawnTime;
                }
            } else if (state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused) {
                state_ = GameState::CustomDead;
            } else if (state_ == GameState::PlayingPack || state_ == GameState::PackPaused) {
                state_ = GameState::PackDead;
            } else {
                state_ = GameState::Dead;
            }
            menuSelection_ = 0;
        }
        return;
    }

    // ── Weapon switch ──
    constexpr int NUM_WEAPONS = 2;
    if (weaponSwitchDelta_ != 0) {
        p.activeWeapon = ((p.activeWeapon + weaponSwitchDelta_) % NUM_WEAPONS + NUM_WEAPONS) % NUM_WEAPONS;
        weaponSwitchDelta_ = 0;
        if (p.activeWeapon == 0) {
            // Switching back to gun: clear axe-pose so gun animation resumes
            p.hadMeleeSwing = false;
        }
    }

    // ── Movement ──
    Vec2 targetVel = {0, 0};
    p.moving = moveInput_.lengthSq() > 0.01f;
    if (p.moving) {
        const float lastStandSpeedMult = (upgrades_.hasLastStand && p.hp <= 1) ? 1.3f : 1.0f;
        targetVel = moveInput_.normalized() * p.speed * moveInput_.length() * lastStandSpeedMult;
    }

    // Smooth velocity (like Unity's Lerp)
    p.vel = Vec2::lerp(p.vel, targetVel, dt * PLAYER_SMOOTHING);

    // Parry dash override (matches scout enemy dash)
    if (p.isParrying && p.parryDashTimer > 0) {
        p.vel = p.parryDir * PARRY_DASH_SPEED;
    }

    // Apply velocity
    Vec2 newPos = p.pos + p.vel * dt;

    // Tile collision (slide) — spectators clip through walls
    if (!spectatorMode_) {
        if (!map_.worldCollides(newPos.x, p.pos.y, PLAYER_SIZE * 0.4f))
            p.pos.x = newPos.x;
        if (!map_.worldCollides(p.pos.x, newPos.y, PLAYER_SIZE * 0.4f))
            p.pos.y = newPos.y;
    } else {
        p.pos = newPos; // noclip
    }

    // Clamp to world
    p.pos.x = fmaxf(PLAYER_SIZE, fminf(map_.worldWidth() - PLAYER_SIZE, p.pos.x));
    p.pos.y = fmaxf(PLAYER_SIZE, fminf(map_.worldHeight() - PLAYER_SIZE, p.pos.y));

    // ── Rotation (aim) ──
    if (aimInput_.lengthSq() > 0.04f) {
        p.rotation = atan2f(aimInput_.y, aimInput_.x);
    } else if (p.moving) {
        p.rotation = atan2f(moveInput_.y, moveInput_.x);
    }

    // ── Leg rotation (movement direction) ──
    if (p.moving) {
        p.legRotation = atan2f(p.vel.y, p.vel.x);
    }

    // ── Body animation: melee takes priority, then shooting anim ──
    if (p.isMeleeSwinging) {
        // Axe swing: sprites 0004–0010 (frames 3–9)
        float prog = std::min(1.0f, p.meleeTimer / MELEE_DURATION);
        int range  = MELEE_ANIM_LAST - MELEE_ANIM_FIRST;             // 6
        int offset = std::min(range, (int)(prog * (range + 1)));      // 0..6
        p.animFrame = p.meleeSwingReverse
                      ? (MELEE_ANIM_LAST  - offset)   // reverse: 9→3
                      : (MELEE_ANIM_FIRST + offset);  // forward: 3→9
    } else if (p.hadMeleeSwing) {
        // Hold end-of-swing pose between swings (meleeSwingReverse was toggled
        // when swing completed, so: next=reverse → last was forward → idle at LAST=9;
        //                            next=forward → last was reverse → idle at FIRST=3)
        p.animFrame = p.meleeSwingReverse ? MELEE_ANIM_LAST : MELEE_ANIM_FIRST;
    } else if (p.activeWeapon == 1) {
        // Axe equipped but never swung yet — hold the axe-ready pose (sprite 0004)
        p.animFrame = MELEE_ANIM_FIRST;
    } else {
        // Normal shooting animation
        p.shootAnimTimer -= dt;
        if (p.shootAnimTimer > 0) {
            p.animFrame = 2; // Sprite-0003 (shooting/recoil)
        } else if (p.hasFiredOnce) {
            p.animFrame = 1; // Sprite-0002 (gun ready, default after first shot)
        } else {
            p.animFrame = 0; // Sprite-0001 (idle, never fired)
        }
    }

    // Leg anim speed follows actual movement speed.
    // 0.0 = standing still, 1.0 = moving at default PLAYER_SPEED.
    if (p.moving && !legSprites_.empty()) {
        float legAnimSpeed = std::max(0.0f, p.vel.length() / PLAYER_SPEED);
        p.legAnimTimer += dt * legAnimSpeed;
        const float legFrameInterval = 0.07f;
        if (p.legAnimTimer > legFrameInterval) {
            p.legAnimTimer = 0;
            p.legAnimFrame = (p.legAnimFrame + 1) % (int)legSprites_.size();
        }
    } else {
        p.legAnimTimer = 0;
        p.legAnimFrame = 0;
    }

    // ── Invulnerability ──
    if (p.invulnerable) {
        p.invulnTimer -= dt;
        if (p.invulnTimer <= 0) p.invulnerable = false;
    }

    // ── Shooting ──
    p.fireCooldown -= dt;
    if (!spectatorMode_ && p.activeWeapon == 0) {  // gun slot only
    if (p.reloading) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0) {
            p.ammo = p.maxAmmo;
            p.reloading = false;
        }
    } else if (fireInput_ && p.fireCooldown <= 0 && p.ammo > 0) {
        spawnBullet(p.pos, p.rotation);
        // Triple shot – two extra bullets at ±15°, costs 3 ammo, halves fire rate
        if (upgrades_.hasTripleShot) {
            const float spread = 0.26f; // ~15 degrees
            spawnBullet(p.pos, p.rotation + spread);
            spawnBullet(p.pos, p.rotation - spread);
            p.ammo = std::max(0, p.ammo - 3);
            p.fireCooldown = 2.0f / p.fireRate; // half fire rate
        } else {
            p.ammo--;
            p.fireCooldown = 1.0f / p.fireRate;
        }
        p.hasFiredOnce = true;
        p.shootAnimTimer = 0.12f;
        camera_.addShake(1.2f);
        rumble(0.05f, 16, 0.22f, 0.92f);  // Even softer gun kick
        if (sfxShoot_) { int ch = Mix_PlayChannel(-1, sfxShoot_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    } else if ((fireInput_ || upgrades_.hasAutoReload) && p.ammo <= 0 && !p.reloading) {
        // Auto-reload (fireInput or hasAutoReload flag)
        p.reloading = true;
        p.reloadTimer = p.reloadTime;
        if (sfxReload_) { int ch = Mix_PlayChannel(-1, sfxReload_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    }
    } // gun slot only (!spectatorMode_ && activeWeapon==0)

    // ── Parry ──
    if (!spectatorMode_) {
    if (parryInput_ && p.canParry && !p.isParrying) {
        playerParry();
    }
    if (p.isParrying) {
        p.parryTimer -= dt;
        p.parryDashTimer -= dt;
        if (p.parryTimer <= 0) {
            p.isParrying = false;
        }
    }
    if (!p.canParry) {
        p.parryCdTimer -= dt;
        if (p.parryCdTimer <= 0) p.canParry = true;
    }
    } // !spectatorMode_

    // ── Melee (axe swing) ──
    const float meleeRange = getMeleeRange(p, upgrades_);
    const float meleeArc = getMeleeArc(upgrades_);
    const int meleePlayerDamage = getMeleePlayerDamage(upgrades_);
    const float meleeCooldownTime = getMeleeCooldownTime(upgrades_);
    if (p.meleeCooldown > 0) p.meleeCooldown -= dt;
    if (!spectatorMode_) {
        // Trigger: dedicated E key OR fire trigger when axe is equipped
        bool doMelee = meleeInput_ || (p.activeWeapon == 1 && fireInput_);
        if (doMelee && !p.isMeleeSwinging && p.meleeCooldown <= 0) {
            p.isMeleeSwinging = true;
            p.meleeTimer      = 0.0f;
            p.meleeHit        = false;
            p.hadMeleeSwing   = true;
            p.meleeBloodlustProc = false;
            p.vel = p.vel + Vec2::fromAngle(p.rotation) * (140.0f + std::min(60.0f, upgrades_.speedBonus * 0.12f));
            camera_.addShake(0.45f);
            if (sfxSwoosh_) { int ch = Mix_PlayChannel(-1, sfxSwoosh_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
        }
    }
    if (p.isMeleeSwinging) {
        p.meleeTimer += dt;

        // Apply damage at the midpoint of the swing
        if (!p.meleeHit && p.meleeTimer >= MELEE_DURATION * 0.5f) {
            p.meleeHit = true;
            auto& net = NetworkManager::instance();
            const bool simAuth = !net.isOnline() || net.isHost() || (net.isConnectedToDedicated() && net.isLobbyHost());

            auto angleDiffOk = [&](Vec2 toTarget) {
                float ang  = atan2f(toTarget.y, toTarget.x);
                float diff = ang - p.rotation;
                while (diff >  (float)M_PI) diff -= 2.0f * (float)M_PI;
                while (diff < -(float)M_PI) diff += 2.0f * (float)M_PI;
                return fabsf(diff) <= meleeArc;
            };
            auto spawnMeleeImpact = [&](Vec2 pos, SDL_Color color, int count, float shake) {
                for (int i = 0; i < count; i++) {
                    BoxFragment f;
                    f.pos = {pos.x + (float)(rand() % 14 - 7), pos.y + (float)(rand() % 14 - 7)};
                    float angle = p.rotation + (((float)(rand() % 90) - 45.0f) * (float)M_PI / 180.0f);
                    float spd = 120.0f + (float)(rand() % 260);
                    f.vel = {cosf(angle) * spd, sinf(angle) * spd};
                    f.size = 2.5f + (float)(rand() % 6);
                    f.lifetime = 0.20f + (float)(rand() % 25) / 100.0f;
                    f.age = 0.0f;
                    f.alive = true;
                    f.rotation = (float)(rand() % 360);
                    f.rotSpeed = (float)(rand() % 700 - 350);
                    int vr = rand() % 40 - 20;
                    f.color = {
                        (Uint8)std::clamp((int)color.r + vr, 0, 255),
                        (Uint8)std::clamp((int)color.g + vr, 0, 255),
                        (Uint8)std::clamp((int)color.b + vr, 0, 255),
                        255
                    };
                    boxFragments_.push_back(f);
                }
                camera_.addShake(shake);
            };
            float meleeImpactRumbleStrength = 0.0f;
            int meleeImpactRumbleDurationMs = 0;
            float meleeImpactRumbleLowBand = 0.0f;
            float meleeImpactRumbleHighBand = 0.0f;
            auto queueMeleeImpactRumble = [&](float strength, int durationMs, float lowBandScale, float highBandScale) {
                if (strength <= 0.0f || durationMs <= 0) return;
                if (strength > meleeImpactRumbleStrength) meleeImpactRumbleStrength = strength;
                if (durationMs > meleeImpactRumbleDurationMs) meleeImpactRumbleDurationMs = durationMs;
                if (lowBandScale > meleeImpactRumbleLowBand) meleeImpactRumbleLowBand = lowBandScale;
                if (highBandScale > meleeImpactRumbleHighBand) meleeImpactRumbleHighBand = highBandScale;
            };
            auto breakCrateAt = [&](PickupCrate& c, bool heavyBreak) {
                c.takeDamage(99.0f);
                if (!c.alive) {
                    Pickup pu;
                    pu.pos = c.pos;
                    pu.type = c.contents;
                    pickups_.push_back(pu);
                    spawnMeleeImpact(c.pos, {170, 110, 55, 255}, heavyBreak ? 18 : 14, heavyBreak ? 1.35f : 1.05f);
                    queueMeleeImpactRumble(heavyBreak ? 0.34f : 0.28f,
                                           heavyBreak ? 105 : 82,
                                           heavyBreak ? 1.35f : 1.05f,
                                           heavyBreak ? 0.70f : 0.60f);
                    screenFlashTimer_ = 0.06f;
                    screenFlashR_ = 255; screenFlashG_ = 200; screenFlashB_ = 50;
                    if (sfxBreak_) { int ch = Mix_PlayChannel(-1, sfxBreak_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    if (upgrades_.hasBloodlust) {
                        p.meleeBloodlustProc = true;
                        p.ammo = std::min(p.maxAmmo, p.ammo + 1);
                    }
                }
            };
            auto emitShockPulse = [&](Vec2 pos) {
                if (!(upgrades_.hasShockEdge || upgrades_.hasStunRounds)) return;
                float shockRadius = upgrades_.hasShockEdge ? 110.0f : 80.0f;
                for (int i = 0; i < (upgrades_.hasShockEdge ? 10 : 6); i++) {
                    BoxFragment f;
                    f.pos = pos;
                    float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
                    float spd = 120.0f + (float)(rand() % 160);
                    f.vel = {cosf(angle) * spd, sinf(angle) * spd};
                    f.size = 2.0f + (float)(rand() % 4);
                    f.lifetime = 0.16f + (float)(rand() % 14) / 100.0f;
                    f.age = 0.0f;
                    f.alive = true;
                    f.rotation = 0.0f;
                    f.rotSpeed = 0.0f;
                    f.color = {120, 230, 255, 255};
                    boxFragments_.push_back(f);
                }
                for (auto& e : enemies_) {
                    if (!e.alive) continue;
                    if (Vec2::dist(pos, e.pos) > shockRadius) continue;
                    e.damageFlash = 1.0f;
                    if (simAuth) e.stunTimer = std::max(e.stunTimer, upgrades_.hasShockEdge ? 1.1f : 0.7f);
                }
                if (simAuth && upgrades_.hasShockEdge) {
                    int px = TileMap::toTile(pos.x);
                    int py = TileMap::toTile(pos.y);
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            int tx = px + dx, ty = py + dy;
                            if (map_.get(tx, ty) == TILE_BOX) {
                                Vec2 boxPos = {TileMap::toWorld(tx), TileMap::toWorld(ty)};
                                if (Vec2::dist(pos, boxPos) <= shockRadius * 0.65f) destroyBox(tx, ty);
                            }
                        }
                    }
                    for (auto& c : crates_) {
                        if (!c.alive) continue;
                        if (Vec2::dist(pos, c.pos) <= shockRadius * 0.55f) breakCrateAt(c, false);
                    }
                }
            };

            // Hit enemies in a cone in front of the player
            for (auto& e : enemies_) {
                if (!e.alive) continue;
                Vec2 toEnemy = {e.pos.x - p.pos.x, e.pos.y - p.pos.y};
                float dist = toEnemy.length();
                if (dist > meleeRange + e.size) continue;
                if (!angleDiffOk(toEnemy)) continue;
                e.damageFlash = 1.0f;
                spawnMeleeImpact(e.pos, {170, 15, 15, 255}, 10 + std::max(0, meleePlayerDamage - MELEE_PLAYER_DAMAGE) * 2, 1.15f);
                queueMeleeImpactRumble(0.32f, 74, 0.46f, 1.18f);
                if (upgrades_.hasShockEdge || upgrades_.hasStunRounds) emitShockPulse(e.pos);
                if (simAuth) {
                    e.hp -= meleePlayerDamage;
                    if (e.hp <= 0) {
                        killEnemy(e);
                        queueMeleeImpactRumble(0.64f, 130, 1.35f, 0.70f);  // extra kill thud
                    }
                    if (upgrades_.hasBloodlust) {
                        p.meleeBloodlustProc = true;
                        p.ammo = std::min(p.maxAmmo, p.ammo + 1 + (upgrades_.hasScavenger ? 1 : 0));
                    }
                    if (net.isInGame()) {
                        uint32_t eIdx = (uint32_t)(&e - &enemies_[0]);
                        net.sendEnemyKilled(eIdx, net.localPlayerId());
                        enemyStatesNeedUpdate_ = true;
                    }
                }
            }
            // Hit destroyable boxes in the swing arc
            {
                int px = TileMap::toTile(p.pos.x);
                int py = TileMap::toTile(p.pos.y);
                int sr = (int)(meleeRange / TILE_SIZE) + 1;
                for (int dy = -sr; dy <= sr; dy++) {
                    for (int dx = -sr; dx <= sr; dx++) {
                        int btx = px + dx, bty = py + dy;
                        if (map_.get(btx, bty) != TILE_BOX) continue;
                        float wx = TileMap::toWorld(btx);
                        float wy = TileMap::toWorld(bty);
                        Vec2 toBox = {wx - p.pos.x, wy - p.pos.y};
                        float dist = toBox.length();
                        if (dist > meleeRange + TILE_SIZE * 0.5f) continue;
                        if (!angleDiffOk(toBox)) continue;
                        spawnMeleeImpact({wx, wy}, {165, 110, 60, 255}, upgrades_.hasShockEdge ? 16 : 12, upgrades_.hasShockEdge ? 1.2f : 0.95f);
                        queueMeleeImpactRumble(upgrades_.hasShockEdge ? 0.30f : 0.24f,
                                               upgrades_.hasShockEdge ? 92 : 70,
                                               upgrades_.hasShockEdge ? 1.20f : 0.92f,
                                               upgrades_.hasShockEdge ? 0.74f : 0.56f);
                        destroyBox(btx, bty);
                        if (upgrades_.hasBloodlust) p.meleeBloodlustProc = true;
                        if (upgrades_.hasShockEdge) emitShockPulse({wx, wy});
                    }
                }
            }
            // Hitting solid walls — clank feedback
            {
                int wpx = TileMap::toTile(p.pos.x);
                int wpy = TileMap::toTile(p.pos.y);
                int wsr = (int)(meleeRange / TILE_SIZE) + 1;
                for (int dy = -wsr; dy <= wsr; dy++) {
                    for (int dx = -wsr; dx <= wsr; dx++) {
                        int tx = wpx + dx, ty = wpy + dy;
                        if (map_.get(tx, ty) != TILE_WALL) continue;
                        float wx = TileMap::toWorld(tx);
                        float wy = TileMap::toWorld(ty);
                        Vec2 toWall = {wx - p.pos.x, wy - p.pos.y};
                        if (toWall.length() > meleeRange + TILE_SIZE * 0.6f) continue;
                        if (!angleDiffOk(toWall)) continue;
                        spawnMeleeImpact({wx, wy}, {110, 110, 130, 255}, 6, 0.60f);
                        queueMeleeImpactRumble(0.18f, 50, 0.88f, 0.36f);
                    }
                }
            }
            // Melee breaks upgrade crates
            for (auto& c : crates_) {
                if (!c.alive) continue;
                Vec2 toCrate = {c.pos.x - p.pos.x, c.pos.y - p.pos.y};
                float dist = toCrate.length();
                if (dist > meleeRange + 20.0f) continue;
                if (!angleDiffOk(toCrate)) continue;
                breakCrateAt(c, true);
                if (upgrades_.hasShockEdge) emitShockPulse(c.pos);
            }
            // PvP / local co-op: hit other players
            bool pvpActive = lobbySettings_.isPvp || currentRules_.pvpEnabled;
            auto doPvpMeleeHit = [&](Player& target, uint8_t targetId) {
                if (target.dead || target.invulnerable) return;
                Vec2 toTarget = {target.pos.x - p.pos.x, target.pos.y - p.pos.y};
                float dist = toTarget.length();
                if (dist > meleeRange + PLAYER_SIZE) return;
                if (!angleDiffOk(toTarget)) return;
                target.takeDamage(meleePlayerDamage);
                spawnMeleeImpact(target.pos, {255, 60, 60, 255}, 10, 1.1f);
                queueMeleeImpactRumble(target.dead ? 0.46f : 0.36f,
                                       target.dead ? 138 : 92,
                                       target.dead ? 0.92f : 0.58f,
                                       target.dead ? 1.00f : 1.26f);
                if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                if (target.dead) {
                    if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
                    if (net.isInGame()) net.sendPlayerDied(targetId, net.localPlayerId());
                }
            };
            if ((state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) || pvpActive) {
                for (int ci = 0; ci < 4; ci++) {
                    if (!coopSlots_[ci].joined) continue;
                    if (&coopSlots_[ci].player == &player_) continue; // don't hit self
                    doPvpMeleeHit(coopSlots_[ci].player, (uint8_t)ci);
                }
            }
            // Online PvP: send melee hit request to host for each remote player in range
            if (net.isInGame() && pvpActive) {
                uint8_t localId = net.localPlayerId();
                for (auto& rp : net.players()) {
                    if (rp.id == localId || !rp.alive) continue;
                    Vec2 toTarget = {rp.targetPos.x - p.pos.x, rp.targetPos.y - p.pos.y};
                    float dist = toTarget.length();
                    if (dist > meleeRange + PLAYER_SIZE) continue;
                    if (!angleDiffOk(toTarget)) continue;
                    if (localTeam_ >= 0) {
                        NetPlayer* rpFull = net.findPlayer(rp.id);
                        if (rpFull && rpFull->team == localTeam_) continue;
                    }
                    if (net.isHost()) {
                        NetPlayer* rpM = net.findPlayer(rp.id);
                        if (rpM) {
                            rpM->hp -= meleePlayerDamage;
                            if (rpM->hp <= 0) {
                                rpM->hp = 0; rpM->alive = false;
                                net.sendPlayerDied(rp.id, localId);
                            } else {
                                net.sendPlayerHpSync(rp.id, rpM->hp, rpM->maxHp, localId);
                            }
                        }
                    } else {
                        net.sendMeleeHitRequest(rp.id, meleePlayerDamage, 0);
                    }
                    for (int spi = 0; spi < (int)rp.subPlayers.size(); spi++) {
                        auto& sp = rp.subPlayers[spi];
                        if (!sp.alive) continue;
                        Vec2 toSub = {sp.targetPos.x - p.pos.x, sp.targetPos.y - p.pos.y};
                        float distSub = toSub.length();
                        if (distSub > meleeRange + PLAYER_SIZE) continue;
                        if (!angleDiffOk(toSub)) continue;
                        if (net.isHost()) {
                            NetPlayer* rpM = net.findPlayer(rp.id);
                            if (!rpM || spi >= (int)rpM->subPlayers.size()) continue;
                            auto& target = rpM->subPlayers[spi];
                            target.hp -= meleePlayerDamage;
                            if (target.hp <= 0) {
                                target.hp = 0;
                                target.alive = false;
                                net.sendSubPlayerDied(rp.id, (uint8_t)(spi + 1), localId);
                            } else {
                                net.sendSubPlayerHpSync(rp.id, (uint8_t)(spi + 1), target.hp, target.maxHp, localId);
                            }
                        } else {
                            net.sendMeleeHitRequest(rp.id, meleePlayerDamage, (uint8_t)(spi + 1));
                        }
                    }
                }
            }
            if (meleeImpactRumbleStrength > 0.0f) {
                rumble(meleeImpactRumbleStrength,
                       meleeImpactRumbleDurationMs,
                       meleeImpactRumbleLowBand,
                       meleeImpactRumbleHighBand);
            }
        }

        if (p.meleeTimer >= MELEE_DURATION) {
            p.isMeleeSwinging   = false;
            p.meleeSwingReverse = !p.meleeSwingReverse; // alternate next swing direction
            p.meleeCooldown     = p.meleeBloodlustProc ? std::max(0.08f, meleeCooldownTime * 0.45f)
                                                       : meleeCooldownTime;
            p.meleeBloodlustProc = false;
        }
    }

    // ── Bombs ── spawn one queued bomb per frame so orbit angles spread naturally
    if (!spectatorMode_ && p.bombCount > 0) {
        spawnBomb();
        p.bombCount--;
    }
    // ZL / RMB = Launch the nearest orbiting bomb
    if (!spectatorMode_ && bombLaunchInput_) {
        uint8_t localBombOwner2     = NetworkManager::instance().isInGame() ? NetworkManager::instance().localPlayerId() : (uint8_t)255;
        uint8_t localBombOwnerSlot2 = (uint8_t)std::clamp(activeLocalPlayerSlot_, 0, 3);
        Bomb* toFire = nullptr;
        for (auto& b : bombs_) {
            if (b.alive && !b.hasDashed && b.ownerId == localBombOwner2 &&
                (!NetworkManager::instance().isInGame() || b.ownerSubSlot == localBombOwnerSlot2)) {
                toFire = &b; break;
            }
        }
        if (toFire) {
            Vec2 aimDir = (aimInput_.lengthSq() > 0.04f) ? aimInput_.normalized()
                : Vec2::fromAngle(p.rotation);

            // Look for the closest alive enemy within a 45° cone of aim direction
            float bestDist = 600.0f; // max targeting range
            int bestIdx = -1;
            for (int i = 0; i < (int)enemies_.size(); i++) {
                auto& e = enemies_[i];
                if (!e.alive) continue;
                Vec2 toE = e.pos - p.pos;
                float dist = toE.length();
                if (dist < 1.0f || dist > bestDist) continue;
                Vec2 dirE = toE * (1.0f / dist);
                float dot = aimDir.x * dirE.x + aimDir.y * dirE.y;
                if (dot > 0.707f) { // ~45° cone
                    bestDist = dist;
                    bestIdx = i;
                }
            }

            // In PvP, also search remote players for the closest enemy in cone
            uint8_t bestPlayerId = 255;
            uint8_t bestPlayerSlot = 0;
            bool pvpActive = lobbySettings_.isPvp || currentRules_.pvpEnabled;
            if (pvpActive) {
                auto& net = NetworkManager::instance();
                uint8_t localId = net.localPlayerId();
                for (auto& rp : net.players()) {
                    if (rp.id == localId || !rp.alive || rp.spectating) continue;
                    // Skip teammates
                    if (localTeam_ >= 0 && rp.team == localTeam_) continue;
                    Vec2 toRp = rp.targetPos - p.pos;
                    float dist = toRp.length();
                    if (dist < 1.0f || dist > bestDist) continue;
                    Vec2 dirRp = toRp * (1.0f / dist);
                    float dot = aimDir.x * dirRp.x + aimDir.y * dirRp.y;
                    if (dot > 0.707f) { // ~45° cone
                        bestDist = dist;
                        bestPlayerId = rp.id;
                        bestPlayerSlot = 0;
                        bestIdx = -1; // player target wins over AI enemy
                    }
                    for (int spi = 0; spi < (int)rp.subPlayers.size(); spi++) {
                        auto& sp = rp.subPlayers[spi];
                        if (!sp.alive) continue;
                        Vec2 toSp = sp.targetPos - p.pos;
                        float spDist = toSp.length();
                        if (spDist < 1.0f || spDist > bestDist) continue;
                        Vec2 dirSp = toSp * (1.0f / spDist);
                        float spDot = aimDir.x * dirSp.x + aimDir.y * dirSp.y;
                        if (spDot > 0.707f) {
                            bestDist = spDist;
                            bestPlayerId = rp.id;
                            bestPlayerSlot = (uint8_t)(spi + 1);
                            bestIdx = -1;
                        }
                    }
                }
            }

            Vec2 launchDir;
            // Reset both homing fields before assigning
            toFire->homingTarget   = -1;
            toFire->homingPlayerId = 255;
            toFire->homingPlayerSlot = 0;
            toFire->homingStr      = 0;
            if (bestPlayerId != 255) {
                // Home toward closest enemy player
                auto& net2 = NetworkManager::instance();
                NetPlayer* tp = net2.findPlayer(bestPlayerId);
                Vec2 targetPos = tp ? tp->targetPos : p.pos;
                if (tp && bestPlayerSlot > 0) {
                    size_t subIdx = (size_t)(bestPlayerSlot - 1);
                    if (subIdx < tp->subPlayers.size() && tp->subPlayers[subIdx].alive)
                        targetPos = tp->subPlayers[subIdx].targetPos;
                }
                launchDir = (targetPos - toFire->pos).lengthSq() > 1.0f ? (targetPos - toFire->pos).normalized() : aimDir;
                toFire->homingPlayerId = bestPlayerId;
                toFire->homingPlayerSlot = bestPlayerSlot;
                toFire->homingStr = 3.5f;
            } else if (bestIdx >= 0) {
                // Launch toward that AI enemy
                launchDir = (enemies_[bestIdx].pos - toFire->pos).normalized();
                toFire->homingTarget = bestIdx;
                toFire->homingStr = 3.5f; // homing strength (rad/s turn rate)
            } else {
                // Raycast: launch in aim direction
                launchDir = aimDir;
            }
            toFire->activate(launchDir);
            // ── Sync bomb launch to other players ──
            auto& net = NetworkManager::instance();
            if (net.isInGame()) {
                net.sendBombSpawn(toFire->pos, toFire->vel, net.localPlayerId(), toFire->ownerSubSlot);
            }
        }
    }
}

void Player::takeDamage(int dmg) {
    if (invulnerable || dead) return;
    hp -= dmg;
    if (invulnDuration > 0.0f) {
        invulnerable = true;
        invulnTimer  = invulnDuration;
    }
    if (hp <= 0) die();
}

void Player::die() {
    dead = true;
    deathTimer = 0;
    animFrame = 0;
    animTimer = 0;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Enemy Update
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateEnemies(float dt) {
    auto& net = NetworkManager::instance();

    // Clients: smoothly interpolate enemy positions from last host snapshot.
    // Exception: the lobby-host client on a dedicated server IS the simulation authority
    // and must run full AI, not the interpolation path.
    const bool simDelegate = net.isConnectedToDedicated() && net.isLobbyHost();
    if (net.isOnline() && !net.isHost() && !simDelegate) {
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            float lerpRate = (e.netIsDashing) ? 25.0f : 14.0f;
            e.pos = Vec2::lerp(e.pos, e.netTargetPos, std::min(1.0f, dt * lerpRate));
            if (e.damageFlash > 0) e.damageFlash -= dt * 2.5f;
        }
        return;
    }

    for (auto& e : enemies_) {
        if (!e.alive) continue;

        // Stun
        if (e.stunTimer > 0) { e.stunTimer -= dt; continue; }

        // Damage flash decay
        if (e.damageFlash > 0) e.damageFlash -= dt * 2.5f;

        // Vision check — sets e.targetPlayerId if a player is seen
        e.canSeePlayer = enemyCanSeeAnyPlayer(e);
        if (e.canSeePlayer) {
            e.state = EnemyState::Chase;
            e.lastSeenTime = gameTime_;
            e.idleTimer = 0;
        } else if (e.state == EnemyState::Chase && gameTime_ - e.lastSeenTime > LOSE_PLAYER_DELAY) {
            // Migrate to the nearest player within 1.5× vision range instead of wandering
            const float extRange = ENEMY_VISION_DIST * 1.5f;
            float migBest = 1e9f;
            uint8_t migId = 255, migSlot = 0;
            auto tryMigrate = [&](uint8_t pid, uint8_t pslot, Vec2 ppos) {
                float d = (ppos - e.pos).length();
                if (d < extRange && d < migBest) { migBest = d; migId = pid; migSlot = pslot; }
            };
            if (net.isOnline()) {
                if (!player_.dead) tryMigrate(net.localPlayerId(), 0, player_.pos);
                for (int ci = 1; ci < 4; ci++) {
                    if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
                    tryMigrate(net.localPlayerId(), (uint8_t)ci, coopSlots_[ci].player.pos);
                }
                for (auto& p : net.players()) {
                    if (p.id == net.localPlayerId()) continue;
                    if (p.alive && !p.spectating) tryMigrate(p.id, 0, p.pos);
                    for (int spi = 0; spi < (int)p.subPlayers.size(); spi++) {
                        if (!p.subPlayers[spi].alive) continue;
                        tryMigrate(p.id, (uint8_t)(spi + 1), p.subPlayers[spi].targetPos);
                    }
                }
            } else if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
                for (int ci = 0; ci < 4; ci++) {
                    if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
                    tryMigrate((uint8_t)ci, (uint8_t)ci, coopSlots_[ci].player.pos);
                }
            } else if (!player_.dead) {
                tryMigrate(0, 0, player_.pos);
            }
            if (migId != 255) {
                e.targetPlayerId = migId;
                e.targetPlayerSlot = migSlot;
                e.state = EnemyState::Chase;
                e.lastSeenTime = gameTime_;
            } else {
                e.state = EnemyState::Wander;
            }
        }

        // After 30s of wandering with no sight of any player, hunt down the nearest one
        if (e.state == EnemyState::Wander) {
            e.idleTimer += dt;
            if (e.idleTimer > 30.0f) {
                e.idleTimer = 0;
                // Find nearest alive non-spectating player
                float best = 1e9f;
                uint8_t bestId = 255;
                uint8_t bestSlot = 0;
                auto testClose = [&](uint8_t pid, uint8_t pslot, Vec2 ppos) {
                    float d = (ppos - e.pos).length();
                    if (d < best) { best = d; bestId = pid; bestSlot = pslot; }
                };
                if (net.isOnline()) {
                    if (!player_.dead) testClose(net.localPlayerId(), 0, player_.pos);
                    for (int ci = 1; ci < 4; ci++) {
                        if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
                        testClose(net.localPlayerId(), (uint8_t)ci, coopSlots_[ci].player.pos);
                    }
                    for (auto& p : net.players()) {
                        if (p.id == net.localPlayerId()) continue;
                        if (p.alive && !p.spectating) testClose(p.id, 0, p.pos);
                        for (int spi = 0; spi < (int)p.subPlayers.size(); spi++) {
                            if (!p.subPlayers[spi].alive) continue;
                            testClose(p.id, (uint8_t)(spi + 1), p.subPlayers[spi].targetPos);
                        }
                    }
                } else {
                    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
                        for (int ci = 0; ci < 4; ci++) {
                            if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
                            testClose((uint8_t)ci, (uint8_t)ci, coopSlots_[ci].player.pos);
                        }
                    } else if (!player_.dead) {
                        testClose(0, 0, player_.pos);
                    }
                }
                if (bestId != 255) {
                    e.targetPlayerId = bestId;
                    e.targetPlayerSlot = bestSlot;
                    e.state = EnemyState::Chase;
                    e.lastSeenTime = gameTime_;
                }
            }
        } else {
            e.idleTimer = 0;
        }

        // Behavior
        if (e.isDashing) {
            enemyDash(e, dt);
        } else if (e.dashCharging) {
            e.dashDelayTimer -= dt;
            e.flashTimer -= dt;
            if (e.dashDelayTimer <= 0) {
                // Initiate actual dash (direction was locked when charging began)
                e.isDashing = true;
                e.dashTimer = e.dashDuration;
                e.dashCharging = false;
                if (sfxSwoosh_) { int ch = Mix_PlayChannel(-1, sfxSwoosh_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
            }
        } else if (e.state == EnemyState::Chase) {
            enemyChase(e, dt);
        } else {
            enemyWander(e, dt);
        }

        // Cooldowns
        if (e.dashOnCd) {
            e.dashCdTimer -= dt;
            if (e.dashCdTimer <= 0) e.dashOnCd = false;
        }

        // Rotation toward movement (shooter enemies in chase face their target, set in enemyChase)
        bool shooterChasing = isShooterEnemyType(e.type) && e.state == EnemyState::Chase;
        if (!shooterChasing && e.vel.lengthSq() > 1.0f && !e.isDashing && !e.dashCharging) {
            e.rotation = atan2f(e.vel.y, e.vel.x);
        }

        // Wall collision (slide)
        Vec2 newPos = e.pos + e.vel * dt;
        if (!map_.worldCollides(newPos.x, e.pos.y, e.size * 0.4f))
            e.pos.x = newPos.x;
        else
            e.vel.x = 0;
        if (!map_.worldCollides(e.pos.x, newPos.y, e.size * 0.4f))
            e.pos.y = newPos.y;
        else
            e.vel.y = 0;

        // Clamp to world
        e.pos.x = fmaxf(e.size, fminf(map_.worldWidth() - e.size, e.pos.x));
        e.pos.y = fmaxf(e.size, fminf(map_.worldHeight() - e.size, e.pos.y));
    }
}

bool Game::enemyCanSeeAnyPlayer(Enemy& e) {
    auto& net = NetworkManager::instance();

    float bestDist  = ENEMY_VISION_DIST + 1.0f;
    uint8_t newTarget = 255;
    uint8_t newTargetSlot = 0;
    bool sawCurrentTarget = false;

    // Helper: LOS check for one candidate player
    auto testVis = [&](uint8_t pid, uint8_t pslot, Vec2 ppos) {
        Vec2 toPlayer = ppos - e.pos;
        float dist = toPlayer.length();
        if (dist >= ENEMY_VISION_DIST) return;
        // Raycast (no angle limit — 360° awareness)
        Vec2 dir = toPlayer.normalized();
        float step = TILE_SIZE * 0.4f;
        for (float d = step; d < dist; d += step) {
            Vec2 pt = e.pos + dir * d;
            if (map_.isSolid(TileMap::toTile(pt.x), TileMap::toTile(pt.y))) return;
        }
        if (pid == e.targetPlayerId && pslot == e.targetPlayerSlot) sawCurrentTarget = true;
        if (dist < bestDist) { bestDist = dist; newTarget = pid; newTargetSlot = pslot; }
    };

    if (net.isOnline()) {
        // Use authoritative local position (net.players() entry for self may be stale)
        if (!player_.dead) testVis(net.localPlayerId(), 0, player_.pos);
        for (int ci = 1; ci < 4; ci++) {
            if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
            testVis(net.localPlayerId(), (uint8_t)ci, coopSlots_[ci].player.pos);
        }
        for (auto& p : net.players()) {
            if (p.id == net.localPlayerId()) continue; // already tested above
            if (p.alive && !p.spectating) testVis(p.id, 0, p.pos);
            for (int spi = 0; spi < (int)p.subPlayers.size(); spi++) {
                if (!p.subPlayers[spi].alive) continue;
                testVis(p.id, (uint8_t)(spi + 1), p.subPlayers[spi].targetPos);
            }
        }
    } else if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
        // Local co-op: test visibility against all joined players
        for (int ci = 0; ci < 4; ci++) {
            if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
            testVis((uint8_t)ci, (uint8_t)ci, coopSlots_[ci].player.pos);
        }
    } else {
        if (!player_.dead) testVis(0, 0, player_.pos);
    }

    if (newTarget != 255) {
        // Keep chasing current target while they're still visible;
        // switch to closest visible if target gone or not yet set
        if (!sawCurrentTarget || e.targetPlayerId == 255) {
            e.targetPlayerId = newTarget;
            e.targetPlayerSlot = newTargetSlot;
        }
        return true;
    }
    return false;
}

Vec2 Game::getEnemyTargetPos(const Enemy& e) const {
    auto& net = NetworkManager::instance();
    if (net.isOnline() && e.targetPlayerId != 255) {
        // Use authoritative local position for own player
        if (e.targetPlayerId == net.localPlayerId()) {
            if (e.targetPlayerSlot == 0) return player_.pos;
            if (e.targetPlayerSlot < 4 && coopSlots_[e.targetPlayerSlot].joined && !coopSlots_[e.targetPlayerSlot].player.dead)
                return coopSlots_[e.targetPlayerSlot].player.pos;
        }
        for (auto& p : net.players()) {
            if (p.id != e.targetPlayerId) continue;
            if (e.targetPlayerSlot == 0) {
                if (p.alive && !p.spectating) return p.pos;
            } else {
                size_t subIdx = (size_t)(e.targetPlayerSlot - 1);
                if (subIdx < p.subPlayers.size() && p.subPlayers[subIdx].alive)
                    return p.subPlayers[subIdx].targetPos;
            }
        }
    }
    // Local co-op: prefer the tracked target slot if it's valid, otherwise target nearest alive player.
    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
        if (e.targetPlayerId < 4 && coopSlots_[e.targetPlayerId].joined && !coopSlots_[e.targetPlayerId].player.dead)
            return coopSlots_[e.targetPlayerId].player.pos;
        const Player* nearest = nullptr;
        float nearestDist = 1e9f;
        for (int i = 0; i < 4; i++) {
            if (!coopSlots_[i].joined || coopSlots_[i].player.dead) continue;
            float d = (coopSlots_[i].player.pos - e.pos).length();
            if (d < nearestDist) { nearestDist = d; nearest = &coopSlots_[i].player; }
        }
        if (nearest) return nearest->pos;
    }
    return player_.pos; // single-player fallback or host's own position
}

void Game::enemyWander(Enemy& e, float dt) {
    if (gameTime_ >= e.nextWanderTime) {
        // Pick random nearby point
        float angle = (float)(rand() % 360) * M_PI / 180.0f;
        float dist = 100.0f + (rand() % 200);
        e.wanderTarget = e.pos + Vec2::fromAngle(angle) * dist;
        // Clamp to map
        e.wanderTarget.x = fmaxf(TILE_SIZE * 2, fminf(map_.worldWidth() - TILE_SIZE * 2, e.wanderTarget.x));
        e.wanderTarget.y = fmaxf(TILE_SIZE * 2, fminf(map_.worldHeight() - TILE_SIZE * 2, e.wanderTarget.y));
        e.nextWanderTime = gameTime_ + WANDER_INTERVAL;
    }
    Vec2 desired = steerToward(e.pos, e.wanderTarget, e.speed * 0.5f, dt);
    // Melee enemies: smooth velocity with inertia
    if (isMeleeEnemyType(e.type)) {
        float inertia = MELEE_INERTIA;
        e.vel = Vec2::lerp(e.vel, desired, dt * inertia);
    } else if (isShooterEnemyType(e.type)) {
        e.vel = Vec2::lerp(e.vel, desired, dt * SHOOTER_INERTIA);
    } else {
        e.vel = desired;
    }
}

void Game::enemyChase(Enemy& e, float dt) {
    Vec2 targetPos = getEnemyTargetPos(e);
    Vec2 toPlayer  = targetPos - e.pos;
    float dist     = toPlayer.length();

    // Shooter: keep range and strafe while shooting
    if (isShooterEnemyType(e.type)) {
        if (dist < e.preferredMinRange) {
            // Back away and strafe sideways
            Vec2 away  = (e.pos - targetPos).normalized();
            Vec2 perp  = Vec2{-away.y, away.x}; // perpendicular
            float sideSign = (sinf(gameTime_ * 1.1f) > 0) ? 1.0f : -1.0f;
            Vec2 retreat = (away + perp * sideSign * 0.6f).normalized();
            Vec2 desired = steerToward(e.pos, e.pos + retreat * 200.0f, e.speed * 0.7f, dt);
            e.vel = Vec2::lerp(e.vel, desired, dt * SHOOTER_INERTIA);
        } else if (dist > e.preferredMaxRange) {
            Vec2 desired = steerToward(e.pos, targetPos, e.speed, dt);
            e.vel = Vec2::lerp(e.vel, desired, dt * SHOOTER_INERTIA);
        } else {
            // Orbit / strafe
            Vec2 perp = Vec2{-toPlayer.normalized().y, toPlayer.normalized().x};
            float sideSign = (sinf(gameTime_ * 0.9f) > 0) ? 1.0f : -1.0f;
            e.vel = Vec2::lerp(e.vel, perp * sideSign * e.speed * 0.5f, dt * 4.0f);
        }
        enemyShoot(e, dt);
        e.rotation = atan2f(toPlayer.y, toPlayer.x);
        return;
    }

    // Melee: charge toward target with inertia
    Vec2 desired = steerToward(e.pos, targetPos, e.speed, dt);
    e.vel = Vec2::lerp(e.vel, desired, dt * MELEE_INERTIA);

    // Start dash when close — lock direction immediately
    if (dist < e.dashDistance && !e.dashOnCd && !e.dashCharging) {
        e.dashCharging   = true;
        e.dashDelayTimer = e.dashDelay;
        e.flashTimer     = e.dashDelay;
        e.dashDir        = toPlayer.normalized(); // lock dash direction now
        e.vel = {0, 0}; // stop for wind-up
    }
}

void Game::enemyDash(Enemy& e, float dt) {
    e.vel = e.dashDir * e.dashForce;
    e.dashTimer -= dt;
    if (e.dashTimer <= 0) {
        e.isDashing = false;
        e.dashOnCd = true;
        e.dashCdTimer = e.dashCooldown;
        // Keep some momentum after dash instead of hard stop
        e.vel = e.dashDir * e.speed * 0.5f;
    }
}

void Game::enemyShoot(Enemy& e, float dt) {
    e.shootCooldown -= dt;
    if (e.burstGapTimer > 0) e.burstGapTimer -= dt;

    if (!e.canSeePlayer) return;

    if (e.burstShotsLeft <= 0 && e.shootCooldown <= 0) {
        e.burstShotsLeft = std::max(1, e.shotsPerBurst);
        e.burstGapTimer = 0.0f;
    }

    if (e.burstShotsLeft > 0 && e.burstGapTimer <= 0) {
        float spread = 0.0f;
        if (e.shootSpread > 0.001f) {
            float t = (float)(rand() % 1000) / 999.0f;
            spread = (t - 0.5f) * e.shootSpread;
        }
        // Muzzle position: offset relative to the enemy's own facing direction
        // so the bullet originates from where the gun is on the sprite.
        Vec2 eFwd   = Vec2::fromAngle(e.rotation);
        Vec2 eRight = {-eFwd.y, eFwd.x};
        Vec2 muzzle = e.pos + eFwd * 14.0f + eRight * 14.0f;
        float bulletSpeed = ENEMY_BULLET_SPEED;
        if (e.type == EnemyType::Sniper) {
            bulletSpeed *= SNIPER_BULLET_SPEED_MULTI;
        }
        spawnEnemyBullet(muzzle, getEnemyTargetPos(e), spread, bulletSpeed);
        e.burstShotsLeft--;
        if (e.burstShotsLeft > 0) {
            e.burstGapTimer = e.burstGap;
        } else {
            e.shootCooldown = e.shootCooldownBase;
        }
    }
}

Vec2 Game::steerToward(Vec2 from, Vec2 to, float spd, float dt) const {
    Vec2 dir = (to - from);
    if (dir.lengthSq() < 4.0f) return {0, 0};
    dir = dir.normalized();

    // Check if direct path is blocked by a wall
    float lookAhead = TILE_SIZE * 1.5f;
    Vec2 ahead = from + dir * lookAhead;
    int tx = TileMap::toTile(ahead.x);
    int ty = TileMap::toTile(ahead.y);

    if (map_.isSolid(tx, ty)) {
        // Try 45-degree left and right feelers to find an open direction
        float baseAngle = atan2f(dir.y, dir.x);
        // Try increasingly wider angles
        for (float offset = 0.5f; offset <= 2.5f; offset += 0.5f) {
            // Try right
            float aR = baseAngle + offset;
            Vec2 rDir = {cosf(aR), sinf(aR)};
            Vec2 rAhead = from + rDir * lookAhead;
            int rxT = TileMap::toTile(rAhead.x);
            int ryT = TileMap::toTile(rAhead.y);
            if (!map_.isSolid(rxT, ryT)) {
                return rDir * spd;
            }
            // Try left
            float aL = baseAngle - offset;
            Vec2 lDir = {cosf(aL), sinf(aL)};
            Vec2 lAhead = from + lDir * lookAhead;
            int lxT = TileMap::toTile(lAhead.x);
            int lyT = TileMap::toTile(lAhead.y);
            if (!map_.isSolid(lxT, lyT)) {
                return lDir * spd;
            }
        }
        // All feelers blocked: try perpendicular
        return Vec2{-dir.y, dir.x} * spd * 0.5f;
    }

    return dir * spd;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Bullets
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateBullets(float dt) {
    const bool bulletSimAuth = !NetworkManager::instance().isOnline() || NetworkManager::instance().isHost() ||
        (NetworkManager::instance().isConnectedToDedicated() && NetworkManager::instance().isLobbyHost());
    for (auto& b : bullets_) {
        b.tick(dt);
        // Magnet upgrade — gently steer bullets toward the nearest alive enemy
        if (upgrades_.hasMagnet && b.alive) {
            float bestDist = 420.0f;
            Enemy* target = nullptr;
            for (auto& e : enemies_) {
                if (!e.alive) continue;
                float d = (e.pos - b.pos).length();
                if (d < bestDist) { bestDist = d; target = &e; }
            }
            if (target) {
                Vec2 toTarget = (target->pos - b.pos).normalized();
                float speed = b.vel.length();
                if (speed > 0.001f) {
                    float currentAngle = b.vel.angle();
                    float targetAngle  = toTarget.angle();
                    float angleDiff = targetAngle - currentAngle;
                    // Wrap to [-π, π]
                    while (angleDiff >  (float)M_PI) angleDiff -= 2.0f * (float)M_PI;
                    while (angleDiff < -(float)M_PI) angleDiff += 2.0f * (float)M_PI;
                    const float turnRate = 3.5f; // rad/s
                    currentAngle += std::clamp(angleDiff, -turnRate * dt, turnRate * dt);
                    b.vel = Vec2::fromAngle(currentAngle) * speed;
                }
            }
        }
        // Wall collision
        int tx = TileMap::toTile(b.pos.x);
        int ty = TileMap::toTile(b.pos.y);
        if (map_.isSolid(tx, ty)) {
            // Check if it's a breakable box
            if (map_.get(tx, ty) == TILE_BOX) {
                destroyBox(tx, ty);
                if (b.explosive) spawnBulletExplosion(b.pos, b.damage, b.ownerId, b.ownerSubSlot, -1, bulletSimAuth);
                b.alive = false;
            } else if (upgrades_.hasRicochet && b.bounces < 3) {
                // Determine which axis caused the collision
                int prevTx = TileMap::toTile(b.pos.x - b.vel.x * dt);
                int prevTy = TileMap::toTile(b.pos.y - b.vel.y * dt);
                bool hitX = map_.isSolid(tx, prevTy);
                bool hitY = map_.isSolid(prevTx, ty);
                if (hitX) b.vel.x = -b.vel.x;
                if (hitY) b.vel.y = -b.vel.y;
                if (!hitX && !hitY) { b.vel.x = -b.vel.x; b.vel.y = -b.vel.y; }
                // Step back out of wall (vel is already flipped, so += moves away)
                b.pos.x += b.vel.x * dt;
                b.pos.y += b.vel.y * dt;
                b.bounces++;
                // Ricochet spark
                for (int i = 0; i < 3; i++) {
                    BoxFragment f;
                    f.pos = b.pos;
                    float baseAngle = atan2f(b.vel.y, b.vel.x);
                    float spread = ((float)(rand() % 90) - 45.0f) * (float)M_PI / 180.0f;
                    float spd = 120.0f + (float)(rand() % 80);
                    f.vel = {cosf(baseAngle + spread) * spd, sinf(baseAngle + spread) * spd};
                    f.rotation = (float)(rand() % 360);
                    f.rotSpeed = (float)(rand() % 600 - 300);
                    f.size = 1.5f;
                    f.lifetime = 0.15f;
                    f.age = 0;
                    f.alive = true;
                    f.color = {150, 255, 150, 255}; // green sparks for ricochet
                    boxFragments_.push_back(f);
                }
            } else {
                if (b.explosive) spawnBulletExplosion(b.pos, b.damage, b.ownerId, b.ownerSubSlot, -1, bulletSimAuth);
                // Bullet shatter sparks on wall hit
                int numSparks = 4 + rand() % 4;
                for (int i = 0; i < numSparks; i++) {
                    BoxFragment f;
                    f.pos = b.pos;
                    // Reflect roughly back from travel direction
                    float baseAngle = atan2f(-b.vel.y, -b.vel.x);
                    float spread = ((float)(rand() % 120) - 60.0f) * M_PI / 180.0f;
                    float angle = baseAngle + spread;
                    float spd = 80.0f + (float)(rand() % 180);
                    f.vel = {cosf(angle) * spd, sinf(angle) * spd};
                    f.rotation = (float)(rand() % 360);
                    f.rotSpeed = (float)(rand() % 800 - 400);
                    f.size = 1.5f + (float)(rand() % 3);
                    f.lifetime = 0.2f + (float)(rand() % 20) / 100.0f;
                    f.age = 0;
                    f.alive = true;
                    // Yellow/white spark colors
                    int bright = 200 + rand() % 56;
                    f.color = {(Uint8)bright, (Uint8)(bright - 40 - rand() % 60), (Uint8)(50 + rand() % 60), 255};
                    boxFragments_.push_back(f);
                }
                b.alive = false;
            }
        }
    }
    for (auto& b : enemyBullets_) {
        b.tick(dt);
        int tx = TileMap::toTile(b.pos.x);
        int ty = TileMap::toTile(b.pos.y);
        if (map_.isSolid(tx, ty)) {
            if (map_.get(tx, ty) == TILE_BOX) {
                destroyBox(tx, ty);
            } else {
                // Enemy bullet spark on wall hit
                int numSparks = 3 + rand() % 3;
                for (int i = 0; i < numSparks; i++) {
                    BoxFragment f;
                    f.pos = b.pos;
                    float baseAngle = atan2f(-b.vel.y, -b.vel.x);
                    float spread = ((float)(rand() % 120) - 60.0f) * M_PI / 180.0f;
                    float angle = baseAngle + spread;
                    float spd = 60.0f + (float)(rand() % 140);
                    f.vel = {cosf(angle) * spd, sinf(angle) * spd};
                    f.rotation = (float)(rand() % 360);
                    f.rotSpeed = (float)(rand() % 600 - 300);
                    f.size = 1.5f + (float)(rand() % 2);
                    f.lifetime = 0.15f + (float)(rand() % 15) / 100.0f;
                    f.age = 0;
                    f.alive = true;
                    int bright = 200 + rand() % 56;
                    f.color = {(Uint8)(bright - 20), (Uint8)(bright - 80), (Uint8)(50 + rand() % 40), 255};
                    boxFragments_.push_back(f);
                }
            }
            b.alive = false;
        }
    }
}

void Game::spawnBullet(Vec2 pos, float angle) {
    Entity b;
    // Offset forward + slightly right (gun side)
    Vec2 fwd = Vec2::fromAngle(angle);
    Vec2 right = {-fwd.y, fwd.x}; // perpendicular right
    float shootOffsetX = hasActiveChar_ ? activeCharDef_.shootOffsetX : GUN_OFFSET_RIGHT;
    float shootOffsetY = hasActiveChar_ ? activeCharDef_.shootOffsetY : -30.0f;
    b.pos = pos + right * shootOffsetX + fwd * (-shootOffsetY);
    b.vel = Vec2::fromAngle(angle) * (BULLET_SPEED * upgrades_.bulletSpeedMulti);
    b.rotation = angle;
    b.size = BULLET_SIZE + upgrades_.bulletSizeBonus;
    b.lifetime = BULLET_LIFETIME * std::max(1.0f, upgrades_.bulletSpeedMulti * 0.92f);
    b.tag = TAG_BULLET;
    b.sprite = bulletSprite_;
    b.damage = std::max(1, (int)roundf(upgrades_.damageMulti *
        (upgrades_.hasLastStand && player_.hp <= 1 ? 2.0f : 1.0f)));
    b.piercing = upgrades_.hasPiercing;
    b.explosive = upgrades_.hasExplosiveTips;
    b.chainLightning = upgrades_.hasChainLightning;

    // Assign a stable network ID so remote peers can remove this bullet on hit
    auto& net = NetworkManager::instance();
    if (net.isInGame()) {
        b.netId = nextBulletNetId_++;
        if (nextBulletNetId_ == 0) nextBulletNetId_ = 1; // skip 0 (means "not networked")
        b.ownerId = net.localPlayerId();
        b.ownerSubSlot = (uint8_t)std::clamp(activeLocalPlayerSlot_, 0, 3);
    }
    // In local co-op, tag bullet with the slot index of the player who fired it
    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
        b.ownerId = (uint8_t)std::clamp(activeLocalPlayerSlot_, 0, 3);
        b.ownerSubSlot = b.ownerId;
    }

    bullets_.push_back(b);

    // ── Visual polish: muzzle flash ──
    muzzleFlashTimer_ = 0.06f;
    muzzleFlashPos_ = b.pos; // store exact bullet spawn point
    camera_.addShake(1.5f);  // subtle recoil shake

    // ── Sync bullet to other players ──
    if (net.isInGame()) {
        net.sendBulletSpawn(b.pos, angle, net.localPlayerId(), b.netId, b.ownerSubSlot);
    }
}

void Game::spawnEnemyBullet(Vec2 pos, Vec2 target, float angleOffset, float speed) {
    Entity b;
    Vec2 dir = (target - pos).normalized();
    if (angleOffset != 0.0f) dir = Vec2::fromAngle(dir.angle() + angleOffset);
    b.pos = pos + dir * 20.0f;  // push forward out of the enemy body; lateral already baked into pos
    b.vel = dir * speed;
    b.rotation = atan2f(dir.y, dir.x);
    b.size = BULLET_SIZE;
    b.lifetime = ENEMY_BULLET_LIFETIME;
    b.tag = TAG_ENEMY_BULLET;
    b.sprite = enemyBulletSprite_;
    b.damage = 1;
    enemyBullets_.push_back(b);

    // Enemy shoot SFX (quieter)
    if (sfxEnemyShoot_) {
        int ch = Mix_PlayChannel(-1, sfxEnemyShoot_, 0);
        if (ch >= 0) Mix_Volume(ch, config_.sfxVolume * 2 / 5);
    }

    // Host broadcasts bullet spawn to clients
    auto& net = NetworkManager::instance();
    if (net.isHost() && net.isInGame()) {
        net.sendEnemyBulletSpawn(b.pos, b.vel);
    }
}

void Game::spawnBulletExplosion(Vec2 pos, int damage, uint8_t ownerId, uint8_t ownerSlot, int skipEnemyIdx, bool applyDamage) {
    auto& net = NetworkManager::instance();
    const float radius = 72.0f;
    const int splashDamage = std::max(1, damage / 2);

    for (int i = 0; i < 14; i++) {
        BoxFragment f;
        f.pos = {pos.x + (float)(rand() % 18 - 9), pos.y + (float)(rand() % 18 - 9)};
        float ang = (float)(rand() % 360) * (float)M_PI / 180.0f;
        float spd = 90.0f + (float)(rand() % 240);
        f.vel = {cosf(ang) * spd, sinf(ang) * spd};
        f.size = 2.5f + (float)(rand() % 5);
        f.lifetime = 0.22f + (float)(rand() % 22) / 100.0f;
        f.age = 0.0f;
        f.alive = true;
        f.rotation = (float)(rand() % 360);
        f.rotSpeed = (float)(rand() % 560 - 280);
        f.color = {255, (Uint8)(145 + rand() % 70), (Uint8)(35 + rand() % 35), 255};
        boxFragments_.push_back(f);
    }
    camera_.addShake(1.6f);
    screenFlashTimer_ = std::max(screenFlashTimer_, 0.035f);
    screenFlashR_ = 255; screenFlashG_ = 150; screenFlashB_ = 50;
    playExplosionFeedback(pos, radius * 6.8f, 0.10f, 0.34f, 75, 180, 1.10f, 0.72f,
                          config_.sfxVolume / 2, config_.sfxVolume / 10);

    if (!applyDamage) return;

    for (size_t i = 0; i < enemies_.size(); ++i) {
        if ((int)i == skipEnemyIdx) continue;
        auto& e = enemies_[i];
        if (!e.alive) continue;
        if (Vec2::dist(pos, e.pos) > radius + e.size) continue;
        e.damageFlash = 1.0f;
        e.hp -= splashDamage;
        if (upgrades_.hasStunRounds) e.stunTimer = std::max(e.stunTimer, 0.4f);
        if (e.hp <= 0) {
            uint32_t eIdx = (uint32_t)i;
            bool trackKill = !net.isOnline() || ownerId == net.localPlayerId();
            killEnemy(e, trackKill);
            // Credit kill to the firing slot's scoreboard counter in any splitscreen mode
            if (ownerId < 4 && coopSlots_[ownerId].joined)
                coopSlots_[ownerId].kills++;
            if (net.isInGame()) {
                net.sendEnemyKilled(eIdx, ownerId);
                enemyStatesNeedUpdate_ = true;
            }
        }
    }

    int minTx = TileMap::toTile(pos.x - radius);
    int maxTx = TileMap::toTile(pos.x + radius);
    int minTy = TileMap::toTile(pos.y - radius);
    int maxTy = TileMap::toTile(pos.y + radius);
    for (int ty = minTy; ty <= maxTy; ty++) {
        for (int tx = minTx; tx <= maxTx; tx++) {
            if (map_.get(tx, ty) != TILE_BOX) continue;
            Vec2 bp = {TileMap::toWorld(tx), TileMap::toWorld(ty)};
            if (Vec2::dist(pos, bp) <= radius) destroyBox(tx, ty);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Bombs & Explosions
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateBombs(float dt) {
    for (auto& b : bombs_) {
        if (!b.alive) continue;
        b.age += dt;

        // Animation — blink between bomb1 and bomb2
        if (!bombSprites_.empty()) {
            b.animTimer += dt;
            float blinkRate = 0.15f;
            // Blink faster as bomb ages (urgency)
            if (b.age > b.lifetime * 0.5f) blinkRate = 0.08f;
            if (b.age > b.lifetime * 0.8f) blinkRate = 0.04f;
            if (b.animTimer > blinkRate) {
                b.animTimer = 0;
                b.animFrame = (b.animFrame == 0) ? 1 : 0; // toggle 0 and 1
            }
        }

        if (b.hasDashed) {
            // Homing toward target AI enemy if alive
            if (b.homingTarget >= 0 && b.homingTarget < (int)enemies_.size()) {
                auto& target = enemies_[b.homingTarget];
                if (target.alive) {
                    Vec2 toTarget = target.pos - b.pos;
                    float dist = toTarget.length();
                    if (dist > 1.0f) {
                        Vec2 desired = toTarget * (1.0f / dist);
                        float speed = b.vel.length();
                        if (speed > 1.0f) {
                            Vec2 curDir = b.vel * (1.0f / speed);
                            // Blend current direction toward target
                            Vec2 newDir = {
                                curDir.x + (desired.x - curDir.x) * b.homingStr * dt,
                                curDir.y + (desired.y - curDir.y) * b.homingStr * dt
                            };
                            float nlen = newDir.length();
                            if (nlen > 0.001f) {
                                b.vel = newDir * (speed / nlen);
                            }
                        }
                    }
                } else {
                    b.homingTarget = -1; // target died
                }
            }
            // Homing toward a remote enemy player (PvP)
            if (b.homingPlayerId != 255) {
                auto& netH = NetworkManager::instance();
                NetPlayer* tp = netH.findPlayer(b.homingPlayerId);
                if (tp && tp->alive && !tp->spectating) {
                    Vec2 targetPos = tp->targetPos;
                    if (b.homingPlayerSlot > 0) {
                        size_t subIdx = (size_t)(b.homingPlayerSlot - 1);
                        if (subIdx < tp->subPlayers.size() && tp->subPlayers[subIdx].alive) {
                            targetPos = tp->subPlayers[subIdx].targetPos;
                        } else {
                            b.homingPlayerId = 255;
                            b.homingPlayerSlot = 0;
                            goto end_remote_bomb_homing;
                        }
                    }
                    Vec2 toTarget = targetPos - b.pos;
                    float dist = toTarget.length();
                    if (dist > 1.0f) {
                        Vec2 desired = toTarget * (1.0f / dist);
                        float speed = b.vel.length();
                        if (speed > 1.0f) {
                            Vec2 curDir = b.vel * (1.0f / speed);
                            Vec2 newDir = {
                                curDir.x + (desired.x - curDir.x) * b.homingStr * dt,
                                curDir.y + (desired.y - curDir.y) * b.homingStr * dt
                            };
                            float nlen = newDir.length();
                            if (nlen > 0.001f) {
                                b.vel = newDir * (speed / nlen);
                            }
                        }
                    }
                } else {
                    b.homingPlayerId = 255; // target gone
                    b.homingPlayerSlot = 0;
                }
            }
end_remote_bomb_homing:

            b.pos += b.vel * dt;
            // Very light friction so bomb keeps momentum
            b.vel = b.vel * (1.0f - dt * 0.5f);
            // Wall collision => explode (but smash through boxes)
            if (map_.worldCollides(b.pos.x, b.pos.y, BOMB_SIZE * 0.3f)) {
                int btx = TileMap::toTile(b.pos.x);
                int bty = TileMap::toTile(b.pos.y);
                if (map_.get(btx, bty) == TILE_BOX) {
                    destroyBox(btx, bty);
                    // Bomb continues through boxes
                } else {
                    spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
                    b.alive = false;
                }
            }
            // Explode if speed drops too low
            if (b.vel.length() < 30.0f) {
                spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
                b.alive = false;
            }
            // Proximity: explode on contact with any enemy
            if (b.alive) {
                for (auto& e : enemies_) {
                    if (!e.alive) continue;
                    if (Vec2::dist(b.pos, e.pos) < BOMB_SIZE + 20.0f) {
                        spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
                        b.alive = false;
                        break;
                    }
                }
            }
            // Proximity: explode on contact with any remote enemy player (PvP)
            if (b.alive && (lobbySettings_.isPvp || currentRules_.pvpEnabled)) {
                auto& netP = NetworkManager::instance();
                uint8_t localId = netP.localPlayerId();
                for (auto& rp : netP.players()) {
                    if (rp.id == localId || !rp.alive || rp.spectating) continue;
                    if (localTeam_ >= 0 && rp.team == localTeam_) continue;
                    if (Vec2::dist(b.pos, rp.targetPos) < BOMB_SIZE + 20.0f) {
                        spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
                        b.alive = false;
                        break;
                    }
                }
            }
        } else {
            // Orbit around the owner (local player or a remote player)
            b.orbitAngle += b.orbitSpeed * dt;
            Vec2 center = player_.pos;
            if (b.ownerId != 255) {
                auto& netU = NetworkManager::instance();
                if (b.ownerId == netU.localPlayerId()) {
                    if (b.ownerSubSlot == 0) {
                        center = player_.pos;
                    } else if (b.ownerSubSlot < 4 && coopSlots_[b.ownerSubSlot].joined) {
                        center = coopSlots_[b.ownerSubSlot].player.pos;
                    }
                } else {
                    NetPlayer* owner = netU.findPlayer(b.ownerId);
                    if (owner && owner->alive) {
                        center = owner->targetPos;
                        if (b.ownerSubSlot > 0) {
                            size_t subIdx = (size_t)(b.ownerSubSlot - 1);
                            if (subIdx < owner->subPlayers.size() && owner->subPlayers[subIdx].alive)
                                center = owner->subPlayers[subIdx].targetPos;
                        }
                    }
                }
            }
            b.pos.x = center.x + cosf(b.orbitAngle) * b.orbitRadius;
            b.pos.y = center.y + sinf(b.orbitAngle) * b.orbitRadius;
        }
    }
}

void Bomb::activate(Vec2 direction) {
    if (hasDashed) return;
    vel = direction * dashSpeed;
    hasDashed = true;
}

void Game::updateExplosions(float dt) {
    for (auto& ex : explosions_) {
        if (!ex.alive) continue;
        ex.timer += dt;
        if (ex.timer >= ex.duration) { ex.alive = false; continue; }

        // Deal damage to all enemies in radius (once)
        if (!ex.dealtDmg) {
            auto& netAuth = NetworkManager::instance();
            bool isAuthoritative = !netAuth.isOnline() || netAuth.isHost() || (netAuth.isConnectedToDedicated() && netAuth.isLobbyHost());
            for (auto& e : enemies_) {
                if (!e.alive) continue;
                if (Vec2::dist(ex.pos, e.pos) < ex.radius) {
                    if (isAuthoritative) {
                        e.hp -= ex.damage;
                        e.damageFlash = 1.0f;
                        if (e.hp <= 0) {
                            uint32_t eIdx = (uint32_t)(&e - &enemies_[0]);
                            killEnemy(e);
                            if (netAuth.isInGame()) {
                                netAuth.sendEnemyKilled(eIdx, ex.ownerId);
                                enemyStatesNeedUpdate_ = true;
                            }
                        }
                    } else {
                        e.damageFlash = 1.0f; // visual-only on clients
                    }
                }
            }
            // In PvP, explosions deal 3 HP damage to the local player — host-validated
            if ((lobbySettings_.isPvp || currentRules_.pvpEnabled) &&
                !player_.dead &&
                Vec2::dist(ex.pos, player_.pos) < ex.radius) {
                auto& netEx = NetworkManager::instance();
                uint8_t localId = netEx.localPlayerId();
                bool selfFire = (ex.ownerId == localId && ex.ownerSubSlot == 0);
                bool teamFire = false;
                if (!selfFire && ex.ownerId != 255 && localTeam_ >= 0) {
                    if (ex.ownerId == localId) {
                        teamFire = true;
                    } else {
                        NetPlayer* exOwner = netEx.findPlayer(ex.ownerId);
                        if (exOwner && exOwner->team == localTeam_) teamFire = true;
                    }
                }
                if (!selfFire && !teamFire && (netEx.isHost() || netEx.isConnectedToDedicated())) {
                    // P2P host or dedicated-server client: process own explosion damage locally
                    int newHp = std::max(0, player_.hp - 3);
                    player_.hp = newHp;
                    NetPlayer* localNetP = netEx.localPlayer();
                    if (localNetP) localNetP->hp = newHp;
                    if (newHp <= 0) {
                        player_.die();
                        netEx.sendPlayerDied(localId, 255);
                    } else {
                        netEx.sendPlayerHpSync(localId, player_.hp, player_.maxHp, 255);
                    }
                }
            }
            // In PvP, host (or dedicated-server lobby-host) also applies explosion damage to remote players
            if ((lobbySettings_.isPvp || currentRules_.pvpEnabled)) {
                auto& netEx2 = NetworkManager::instance();
                if (netEx2.isHost() || (netEx2.isConnectedToDedicated() && netEx2.isLobbyHost())) {
                    // Determine the owner's team for friendly-fire checks
                    int8_t exOwnerTeam = -1;
                    if (ex.ownerId == netEx2.localPlayerId()) {
                        exOwnerTeam = (int8_t)localTeam_;
                    } else if (ex.ownerId != 255) {
                        NetPlayer* exOwnerP = netEx2.findPlayer(ex.ownerId);
                        if (exOwnerP) exOwnerTeam = exOwnerP->team;
                    }
                    for (const auto& rp : netEx2.players()) {
                        if (rp.id == netEx2.localPlayerId() || !rp.alive) continue;
                        if (rp.id == ex.ownerId && ex.ownerSubSlot == 0) continue;           // can't hurt yourself
                        if (exOwnerTeam >= 0 && rp.team == exOwnerTeam) continue;  // same team
                        if (Vec2::dist(ex.pos, rp.targetPos) < ex.radius) {
                            NetPlayer* rpM = netEx2.findPlayer(rp.id);
                            if (!rpM) continue;
                            rpM->hp -= (int)ex.damage;
                            if (rpM->hp <= 0) {
                                rpM->hp = 0;
                                rpM->alive = false;
                                netEx2.sendPlayerDied(rp.id, 255);
                            } else {
                                netEx2.sendPlayerHpSync(rp.id, rpM->hp, rpM->maxHp, 255);
                            }
                        }
                        NetPlayer* rpM = netEx2.findPlayer(rp.id);
                        if (!rpM) continue;
                        for (int spi = 0; spi < (int)rp.subPlayers.size(); spi++) {
                            if (!rp.subPlayers[spi].alive) continue;
                            if (rp.id == ex.ownerId && ex.ownerSubSlot == (uint8_t)(spi + 1)) continue;
                            if (Vec2::dist(ex.pos, rp.subPlayers[spi].targetPos) >= ex.radius) continue;
                            auto& sub = rpM->subPlayers[spi];
                            sub.hp -= (int)ex.damage;
                            if (sub.hp <= 0) {
                                sub.hp = 0;
                                sub.alive = false;
                                netEx2.sendSubPlayerDied(rp.id, (uint8_t)(spi + 1), ex.ownerId);
                            } else {
                                netEx2.sendSubPlayerHpSync(rp.id, (uint8_t)(spi + 1), sub.hp, sub.maxHp, ex.ownerId);
                            }
                        }
                    }
                }
            }
            bool mpSplitscreen = coopPlayerCount_ > 1 &&
                (state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
                 state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);
            if ((lobbySettings_.isPvp || currentRules_.pvpEnabled) && mpSplitscreen) {
                auto& netEx3 = NetworkManager::instance();
                for (int ci = 1; ci < 4; ci++) {
                    if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
                    if (Vec2::dist(ex.pos, coopSlots_[ci].player.pos) >= ex.radius) continue;
                    bool selfFire = (ex.ownerId == netEx3.localPlayerId() && ex.ownerSubSlot == ci);
                    bool teamFire = false;
                    if (!selfFire && ex.ownerId != 255 && localTeam_ >= 0) {
                        if (ex.ownerId == netEx3.localPlayerId()) {
                            teamFire = true;
                        } else {
                            NetPlayer* exOwner = netEx3.findPlayer(ex.ownerId);
                            if (exOwner && exOwner->team == localTeam_) teamFire = true;
                        }
                    }
                    if (!selfFire && !teamFire && (netEx3.isHost() || netEx3.isConnectedToDedicated())) {
                        Player& target = coopSlots_[ci].player;
                        target.hp = std::max(0, target.hp - 3);
                        if (target.hp <= 0) {
                            target.dead = true;
                            netEx3.sendSubPlayerDied(netEx3.localPlayerId(), (uint8_t)ci, ex.ownerId);
                        } else {
                            netEx3.sendSubPlayerHpSync(netEx3.localPlayerId(), (uint8_t)ci, target.hp, target.maxHp, ex.ownerId);
                        }
                    }
                }
            }
            // Local co-op: explosions damage co-op players (skip owner)
            if ((state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) &&
                currentRules_.pvpEnabled) {
                for (int ci = 0; ci < 4; ci++) {
                    if (!coopSlots_[ci].joined) continue;
                    CoopSlot& slot = coopSlots_[ci];
                    if (slot.player.dead) continue;
                    if (ex.ownerId == ci && ex.ownerSubSlot == ci) continue;  // can't hurt yourself with your own bomb
                    if (Vec2::dist(ex.pos, slot.player.pos) < ex.radius) {
                        slot.player.takeDamage(3);  // 3 damage from explosion
                    }
                }
            }
            ex.dealtDmg = true;
        }
    }
}

void Game::spawnExplosion(Vec2 pos, uint8_t ownerId, uint8_t ownerSlot) {
    Explosion ex;
    ex.pos = pos;
    ex.ownerId = ownerId;
    ex.ownerSubSlot = ownerSlot;
    explosions_.push_back(ex);
    camera_.addShake(6.0f);
    playExplosionFeedback(pos, ex.radius * 8.4f, 0.18f, 0.62f, 130, 320, 1.45f, 0.74f,
                          config_.sfxVolume, config_.sfxVolume / 12);

    // ── Sync explosion to other players (guard prevents echo-loop from network callback) ──
    auto& net = NetworkManager::instance();
    if (net.isOnline() && !suppressNetExplosion_) {
        net.sendExplosion(pos, ownerId, ownerSlot);
    }

    // ── Visual polish: screen flash on explosion ──
    screenFlashTimer_ = 0.15f;
    screenFlashR_ = 255; screenFlashG_ = 200; screenFlashB_ = 80;

    // ── Spawn fire/debris particles ──
#ifdef __SWITCH__
    int numSparks = 10 + rand() % 6;
#else
    int numSparks = 20 + rand() % 12;
#endif
    for (int i = 0; i < numSparks; i++) {
        BoxFragment f;
        f.pos = {pos.x + (float)(rand() % 20 - 10), pos.y + (float)(rand() % 20 - 10)};
        float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
        float spd = 150.0f + (float)(rand() % 400);
        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
        f.rotation = (float)(rand() % 360);
        f.rotSpeed = (float)(rand() % 600 - 300);
        f.size = 4.0f + (float)(rand() % 10);
        f.lifetime = 0.4f + (float)(rand() % 40) / 100.0f;
        // Orange-yellow-red fire colors
        int r = 200 + rand() % 56;
        int g = 80 + rand() % 120;
        int b = rand() % 60;
        f.color = {(Uint8)r, (Uint8)g, (Uint8)b, 255};
        boxFragments_.push_back(f);
    }
    // Smoke particles (darker, larger, slower)
#ifdef __SWITCH__
    int numSmoke = 4 + rand() % 3;
#else
    int numSmoke = 8 + rand() % 6;
#endif
    for (int i = 0; i < numSmoke; i++) {
        BoxFragment f;
        f.pos = {pos.x + (float)(rand() % 30 - 15), pos.y + (float)(rand() % 30 - 15)};
        float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
        float spd = 40.0f + (float)(rand() % 100);
        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
        f.rotation = 0;
        f.rotSpeed = 0;
        f.size = 8.0f + (float)(rand() % 12);
        f.lifetime = 0.6f + (float)(rand() % 40) / 100.0f;
        int v = 40 + rand() % 40;
        f.color = {(Uint8)v, (Uint8)v, (Uint8)v, 200};
        boxFragments_.push_back(f);
    }
    // Cap total fragments to prevent runaway performance loss
#ifdef __SWITCH__
    constexpr size_t MAX_FRAGMENTS = 150;
#else
    constexpr size_t MAX_FRAGMENTS = 400;
#endif
    if (boxFragments_.size() > MAX_FRAGMENTS)
        boxFragments_.erase(boxFragments_.begin(),
                            boxFragments_.begin() + (int)(boxFragments_.size() - MAX_FRAGMENTS));

    // ── Scorch marks on ground ──
    {
        BloodDecal scorch;
        scorch.pos = pos;
        scorch.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
        scorch.scale = 1.5f + (float)(rand() % 50) / 100.0f;
        scorch.type = DecalType::Scorch;
        blood_.push_back(scorch);
        // Smaller surrounding scorch splatters
#ifdef __SWITCH__
        int numScorch = 1 + rand() % 2;
#else
        int numScorch = 3 + rand() % 3;
#endif
        for (int i = 0; i < numScorch; i++) {
            BloodDecal s;
            float dist = 40.0f + (float)(rand() % 80);
            float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
            s.pos = {pos.x + cosf(angle) * dist, pos.y + sinf(angle) * dist};
            s.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
            s.scale = 0.4f + (float)(rand() % 60) / 100.0f;
            s.type = DecalType::Scorch;
            blood_.push_back(s);
        }
        // Cap decals to avoid unbounded growth and renderDecals() cost
#ifdef __SWITCH__
        constexpr size_t MAX_DECALS = 80;
#else
        constexpr size_t MAX_DECALS = 200;
#endif
        if (blood_.size() > MAX_DECALS)
            blood_.erase(blood_.begin(),
                         blood_.begin() + (int)(blood_.size() - MAX_DECALS));
    }

    // ── Destroy boxes caught in explosion radius ──
    float er = ex.radius;
    int minTx = TileMap::toTile(pos.x - er);
    int maxTx = TileMap::toTile(pos.x + er);
    int minTy = TileMap::toTile(pos.y - er);
    int maxTy = TileMap::toTile(pos.y + er);
    for (int ty = minTy; ty <= maxTy; ty++) {
        for (int tx = minTx; tx <= maxTx; tx++) {
            if (map_.get(tx, ty) == TILE_BOX) {
                float bx = TileMap::toWorld(tx);
                float by = TileMap::toWorld(ty);
                if (Vec2::dist(pos, {bx, by}) < er) {
                    destroyBox(tx, ty);
                }
            }
        }
    }
}

Vec2 Game::pickSpawnPos() {
    float ww = map_.worldWidth();
    float wh = map_.worldHeight();

    // Team mode: place player at their team's corner of the map
    if (localTeam_ >= 0 && currentRules_.teamCount >= 2) {
        float margin = 96.0f;
        Vec2 corners[4] = {
            {margin,       margin},
            {ww - margin,  wh - margin},
            {ww - margin,  margin},
            {margin,       wh - margin}
        };
        Vec2 target = corners[localTeam_ % 4];
        for (int attempt = 0; attempt < 50; attempt++) {
            float rx = target.x + (float)(rand() % 200 - 100);
            float ry = target.y + (float)(rand() % 200 - 100);
            rx = std::max(64.0f, std::min(ww - 64.0f, rx));
            ry = std::max(64.0f, std::min(wh - 64.0f, ry));
            if (!map_.worldCollides(rx, ry, PLAYER_SIZE * 0.5f))
                return {rx, ry};
        }
        return corners[localTeam_ % 4]; // fallback: corner even if colliding
    }

    // No teams: pick a random open tile
    for (int attempt = 0; attempt < 80; attempt++) {
        float rx = (float)(64 + rand() % (map_.width  * 64 - 128));
        float ry = (float)(64 + rand() % (map_.height * 64 - 128));
        if (!map_.worldCollides(rx, ry, PLAYER_SIZE * 0.5f))
            return {rx, ry};
    }
    // Ultimate fallback: try map center, then scan outward
    Vec2 center = {ww / 2.0f, wh / 2.0f};
    if (!map_.worldCollides(center.x, center.y, PLAYER_SIZE * 0.5f))
        return center;
    // Systematic scan: search tiles for any non-solid position
    for (int ty = 1; ty < map_.height - 1; ty++) {
        for (int tx = 1; tx < map_.width - 1; tx++) {
            if (!map_.isSolid(tx, ty)) {
                float wx = TileMap::toWorld(tx);
                float wy = TileMap::toWorld(ty);
                if (!map_.worldCollides(wx, wy, PLAYER_SIZE * 0.5f))
                    return {wx, wy};
            }
        }
    }
    return center; // absolute last resort
}

bool Game::isEnemySpawnVisibleToAnyPlayer(Vec2 pos) const {
    auto hasSightLine = [&](Vec2 playerPos, float viewW, float viewH) -> bool {
        float viewPadX = viewW * 0.60f + TILE_SIZE * 1.5f;
        float viewPadY = viewH * 0.60f + TILE_SIZE * 1.5f;
        if (fabsf(pos.x - playerPos.x) <= viewPadX && fabsf(pos.y - playerPos.y) <= viewPadY) {
            return true;
        }

        Vec2 toSpawn = pos - playerPos;
        float dist = toSpawn.length();
        if (dist >= ENEMY_VISION_DIST) return false;

        Vec2 dir = toSpawn.normalized();
        float step = TILE_SIZE * 0.4f;
        for (float d = step; d < dist; d += step) {
            Vec2 pt = playerPos + dir * d;
            if (map_.isSolid(TileMap::toTile(pt.x), TileMap::toTile(pt.y))) return false;
        }
        return true;
    };

    auto& net = NetworkManager::instance();
    if (net.isOnline()) {
        if (!player_.dead && hasSightLine(player_.pos, camera_.viewW, camera_.viewH)) return true;

        for (int ci = 1; ci < 4; ++ci) {
            if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
            if (hasSightLine(coopSlots_[ci].player.pos, coopSlots_[ci].camera.viewW, coopSlots_[ci].camera.viewH)) return true;
        }

        for (const auto& p : net.players()) {
            if (p.id == net.localPlayerId() || !p.alive || p.spectating) continue;
            if (hasSightLine(p.pos, (float)SCREEN_W, (float)SCREEN_H)) return true;
        }
        return false;
    }

    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
        for (int ci = 0; ci < 4; ++ci) {
            if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
            if (hasSightLine(coopSlots_[ci].player.pos, coopSlots_[ci].camera.viewW, coopSlots_[ci].camera.viewH)) return true;
        }
        return false;
    }

    if (!player_.dead && hasSightLine(player_.pos, camera_.viewW, camera_.viewH)) return true;
    return false;
}

Vec2 Game::pickEnemySpawnPos(bool* foundHidden) {
    if (foundHidden) *foundHidden = false;

    auto isValidSpawn = [&](Vec2 sp, bool requireHidden) -> bool {
        if (map_.worldCollides(sp.x, sp.y, ENEMY_SIZE * 0.5f)) return false;
        if (requireHidden && isEnemySpawnVisibleToAnyPlayer(sp)) return false;
        return true;
    };

    bool tryHidden = true;
    Vec2 candidate = {map_.worldWidth() * 0.5f, map_.worldHeight() * 0.5f};

    if (!map_.spawnPoints.empty()) {
        int hiddenAttempts = std::min((int)map_.spawnPoints.size() * 2, 48);
        for (int attempt = 0; attempt < hiddenAttempts; ++attempt) {
            Vec2 sp = map_.spawnPoints[rand() % map_.spawnPoints.size()];
            if (!isValidSpawn(sp, true)) continue;
            if (foundHidden) *foundHidden = true;
            return sp;
        }

        for (int attempt = 0; attempt < 24; ++attempt) {
            Vec2 sp = map_.spawnPoints[rand() % map_.spawnPoints.size()];
            if (!isValidSpawn(sp, false)) continue;
            return sp;
        }
        tryHidden = false;
    }

    int hiddenAttempts = tryHidden ? 80 : 0;
    for (int attempt = 0; attempt < hiddenAttempts; ++attempt) {
        float rx = (float)(64 + rand() % std::max(1, map_.width * 64 - 128));
        float ry = (float)(64 + rand() % std::max(1, map_.height * 64 - 128));
        Vec2 sp = {rx, ry};
        if (!isValidSpawn(sp, true)) continue;
        if (foundHidden) *foundHidden = true;
        return sp;
    }

    for (int attempt = 0; attempt < 120; ++attempt) {
        float rx = (float)(64 + rand() % std::max(1, map_.width * 64 - 128));
        float ry = (float)(64 + rand() % std::max(1, map_.height * 64 - 128));
        Vec2 sp = {rx, ry};
        if (!isValidSpawn(sp, false)) continue;
        return sp;
    }

    for (int ty = 1; ty < map_.height - 1; ++ty) {
        for (int tx = 1; tx < map_.width - 1; ++tx) {
            Vec2 sp = {TileMap::toWorld(tx), TileMap::toWorld(ty)};
            if (!isValidSpawn(sp, false)) continue;
            return sp;
        }
    }

    return candidate;
}

void Game::spawnBomb() {
    auto& net = NetworkManager::instance();
    Bomb b;
    b.pos = player_.pos;
    // Start at a random angle around the player
    b.orbitAngle = (float)(rand() % 360) * M_PI / 180.0f;
    b.orbitRadius = 55.0f + (float)(rand() % 20);
    b.orbitSpeed = 3.0f + (float)(rand() % 100) / 100.0f; // radians/sec
    b.dashSpeed *= upgrades_.bombDashSpeedMulti;
    b.ownerId = net.isInGame() ? net.localPlayerId() : 255;
    b.ownerSubSlot = (uint8_t)std::clamp(activeLocalPlayerSlot_, 0, 3);
    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
        b.ownerId = b.ownerSubSlot;
    }
    bombs_.push_back(b);
    // Notify other clients so they can render the orbiting bomb
    if (net.isInGame()) net.sendBombOrbit(b.ownerId, b.ownerSubSlot);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Spawning
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateSpawning(float dt) {
    // In multiplayer the host is authoritative — clients receive enemy spawns via state packets.
    // Exception: when connected to a headless dedicated server the server has no game simulation;
    // the lobby-host client takes on the simulation role in that case.
    auto& net0 = NetworkManager::instance();
    {
        const bool simulationDelegate = net0.isConnectedToDedicated() && net0.isLobbyHost();
        if (net0.isOnline() && !net0.isHost() && !simulationDelegate) return;
    }
    // True when this instance should send network events (wave start, game end).
    // Offline single-player never sends; ENet host always does; dedicated-server lobby-host does too.
    const bool sendNetEvents = net0.isHost() || (net0.isConnectedToDedicated() && net0.isLobbyHost());
    // In sandbox mode or pure PvP mode skip all wave spawning.
    // Note: pvpActive here means "PvP arena gamemode" only — friendly fire in PvE does NOT skip waves.
    bool pvpActive = lobbySettings_.isPvp;
    if (sandboxMode_ || pvpActive) return;
    // Wave-based spawning system
    if (waveActive_) {
        // Spawning enemies within current wave
        waveSpawnTimer_ -= dt;
        if (waveSpawnTimer_ <= 0 && waveEnemiesLeft_ > 0) {
            waveSpawnTimer_ = WAVE_SPAWN_INTERVAL;
            Vec2 sp = pickEnemySpawnPos();
            if (!map_.worldCollides(sp.x, sp.y, ENEMY_SIZE * 0.5f)) {
                spawnEnemy(sp, rollWaveEnemyType());
                waveEnemiesLeft_--;
            }
        }
        if (waveEnemiesLeft_ <= 0) {
            waveActive_ = false;

            // ── PVE victory check: all enemies dead + reached wave count ──
            if (!pvpActive && lobbySettings_.waveCount > 0 &&
                waveNumber_ >= lobbySettings_.waveCount) {
                // Check if all enemies are dead
                bool allDead = true;
                for (auto& e : enemies_) {
                    if (e.alive) { allDead = false; break; }
                }
                if (allDead) {
                    if (sendNetEvents) {
                        auto& net = NetworkManager::instance();
                        net.sendGameEnd((uint8_t)MatchEndReason::WavesCleared);
                    }
                    return;
                }
            }

            // Pause before next wave, gets shorter over time
            float pauseScale = fmaxf(0.3f, 1.0f - waveNumber_ * 0.05f);
            wavePauseTimer_ = WAVE_PAUSE_BASE * pauseScale * config_.spawnRateScale;
        }
    } else {
        wavePauseTimer_ -= dt;
        if (wavePauseTimer_ <= 0) {
            // PVE: don't start new wave if we've reached the wave count
            if (!pvpActive && lobbySettings_.waveCount > 0 &&
                waveNumber_ >= lobbySettings_.waveCount) {
                // Check if all enemies are dead for victory
                bool allDead = true;
                for (auto& e : enemies_) {
                    if (e.alive) { allDead = false; break; }
                }
                if (allDead && sendNetEvents) {
                    auto& net = NetworkManager::instance();
                    net.sendGameEnd((uint8_t)MatchEndReason::WavesCleared);
                }
                return;
            }

            // Start new wave
            waveNumber_++;
            int waveSize = WAVE_SIZE_BASE + waveNumber_ * WAVE_SIZE_GROWTH;
            if (waveSize > WAVE_MAX_SIZE) waveSize = WAVE_MAX_SIZE;
            waveEnemiesLeft_ = waveSize;
            waveActive_ = true;
            waveSpawnTimer_ = 0;

            // ── Sync wave start to clients ──
            if (sendNetEvents) {
                auto& net = NetworkManager::instance();
                net.sendWaveStart(waveNumber_);
            }

            // ── Visual polish: wave announcement banner ──
            waveAnnounceTimer_ = 2.5f;
            waveAnnounceNum_ = waveNumber_;
        }
    }
}

void Game::spawnEnemy(Vec2 pos, EnemyType type) {
    Enemy e;
    e.pos = pos;
    e.type = type;
    e.wanderTarget = pos;
    e.nextWanderTime = gameTime_ + 1.0f;
    e.renderScale = 3.0f;
    switch (type) {
        case EnemyType::Shooter:
            e.hp = SHOOTER_HP * config_.enemyHpScale;
            e.maxHp = SHOOTER_HP * config_.enemyHpScale;
            e.speed = SHOOTER_SPEED * config_.enemySpeedScale;
            e.size = SHOOTER_SIZE;
            e.shootCooldown = SHOOTER_SHOOT_CD;
            e.shootCooldownBase = SHOOTER_SHOOT_CD;
            e.renderScale = SHOOTER_RENDER_SCALE;
            break;
        case EnemyType::Brute:
            e.hp = BRUTE_HP * config_.enemyHpScale;
            e.maxHp = BRUTE_HP * config_.enemyHpScale;
            e.speed = BRUTE_SPEED * config_.enemySpeedScale;
            e.size = BRUTE_SIZE;
            e.dashDistance = BRUTE_DASH_DIST;
            e.dashForce = BRUTE_DASH_FORCE;
            e.dashDelay = BRUTE_DASH_DELAY;
            e.dashDuration = BRUTE_DASH_DUR;
            e.dashCooldown = BRUTE_DASH_CD;
            e.contactDamage = BRUTE_DASH_DMG;
            e.renderScale = BRUTE_RENDER_SCALE;
            break;
        case EnemyType::Scout:
            e.hp = SCOUT_HP * config_.enemyHpScale;
            e.maxHp = SCOUT_HP * config_.enemyHpScale;
            e.speed = SCOUT_SPEED * config_.enemySpeedScale;
            e.size = SCOUT_SIZE;
            e.dashDistance = SCOUT_DASH_DIST;
            e.dashForce = SCOUT_DASH_FORCE;
            e.dashDelay = SCOUT_DASH_DELAY;
            e.dashDuration = SCOUT_DASH_DUR;
            e.dashCooldown = SCOUT_DASH_CD;
            e.contactDamage = SCOUT_DASH_DMG;
            e.renderScale = SCOUT_RENDER_SCALE;
            break;
        case EnemyType::Sniper:
            e.hp = SNIPER_HP * config_.enemyHpScale;
            e.maxHp = SNIPER_HP * config_.enemyHpScale;
            e.speed = SNIPER_SPEED * config_.enemySpeedScale;
            e.size = SNIPER_SIZE;
            e.shootCooldown = SNIPER_SHOOT_CD;
            e.shootCooldownBase = SNIPER_SHOOT_CD;
            e.preferredMinRange = 460.0f;
            e.preferredMaxRange = 700.0f;
            e.renderScale = SNIPER_RENDER_SCALE;
            break;
        case EnemyType::Gunner:
            e.hp = GUNNER_HP * config_.enemyHpScale;
            e.maxHp = GUNNER_HP * config_.enemyHpScale;
            e.speed = GUNNER_SPEED * config_.enemySpeedScale;
            e.size = GUNNER_SIZE;
            e.shootCooldown = GUNNER_SHOOT_CD;
            e.shootCooldownBase = GUNNER_SHOOT_CD;
            e.preferredMinRange = 220.0f;
            e.preferredMaxRange = 360.0f;
            e.shotsPerBurst = 3;
            e.burstGap = GUNNER_BURST_GAP;
            e.shootSpread = 0.18f;
            e.renderScale = GUNNER_RENDER_SCALE;
            break;
        case EnemyType::Melee:
        default:
            e.hp = ENEMY_HP * config_.enemyHpScale;
            e.maxHp = ENEMY_HP * config_.enemyHpScale;
            e.speed = ENEMY_SPEED * config_.enemySpeedScale;
            e.renderScale = 3.0f;
            break;
    }
    enemies_.push_back(e);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Collisions
// ═════════════════════════════════════════════════════════════════════════════

void Game::resolveCollisions() {
    Player& p = player_;
    auto& net = NetworkManager::instance();
    const bool simAuth = !net.isOnline() || net.isHost() || (net.isConnectedToDedicated() && net.isLobbyHost());

    auto creditEnemyKill = [&](Enemy& enemy, uint8_t ownerId) {
        uint32_t eIdx = (uint32_t)(&enemy - &enemies_[0]);
        bool trackKill = !net.isOnline() || ownerId == net.localPlayerId();
        killEnemy(enemy, trackKill);
        // Credit kill to the firing slot's scoreboard counter in any splitscreen mode
        if (ownerId < 4 && coopSlots_[ownerId].joined)
            coopSlots_[ownerId].kills++;
        if (net.isInGame()) {
            net.sendEnemyKilled(eIdx, ownerId);
            enemyStatesNeedUpdate_ = true;
        }
    };

    auto spawnBulletBurstFX = [&](Vec2 pos, SDL_Color color, int count, float shake) {
        for (int i = 0; i < count; ++i) {
            BoxFragment f;
            f.pos = {pos.x + (float)(rand() % 14 - 7), pos.y + (float)(rand() % 14 - 7)};
            float ang = (float)(rand() % 360) * (float)M_PI / 180.0f;
            float spd = 90.0f + (float)(rand() % 210);
            f.vel = {cosf(ang) * spd, sinf(ang) * spd};
            f.size = 2.0f + (float)(rand() % 4);
            f.lifetime = 0.18f + (float)(rand() % 20) / 100.0f;
            f.age = 0.0f;
            f.alive = true;
            f.rotation = (float)(rand() % 360);
            f.rotSpeed = (float)(rand() % 500 - 250);
            int v = rand() % 40 - 20;
            f.color = {
                (Uint8)std::clamp((int)color.r + v, 0, 255),
                (Uint8)std::clamp((int)color.g + v, 0, 255),
                (Uint8)std::clamp((int)color.b + v, 0, 255),
                255
            };
            boxFragments_.push_back(f);
        }
        camera_.addShake(shake);
    };

    auto triggerExplosiveTip = [&](Entity& b, Vec2 pos, int skipEnemy) {
        if (!b.explosive) return;
        spawnBulletExplosion(pos, b.damage, b.ownerId, b.ownerSubSlot, skipEnemy, simAuth);
    };

    auto triggerChainLightning = [&](Entity& b, Vec2 pos, int primaryEnemy) {
        if (!b.chainLightning) return;
        int jumps = 0;
        const int maxJumps = 4;  // Increased from 2 to 4 for better chain effect
        const float chainRange = 200.0f;  // Increased from 170 to 200 for better reach
        
        for (int pass = 0; pass < maxJumps; ++pass) {
            float bestDist = chainRange;
            int bestIdx = -1;
            for (size_t i = 0; i < enemies_.size(); ++i) {
                if ((int)i == primaryEnemy) continue;
                auto& ex = enemies_[i];
                if (!ex.alive) continue;
                // Skip enemies already hit by this chain
                if (std::find(b.hitEnemies.begin(), b.hitEnemies.end(), (uint32_t)i) != b.hitEnemies.end()) continue;
                float d = Vec2::dist(pos, ex.pos);
                if (d < bestDist) { bestDist = d; bestIdx = (int)i; }
            }
            if (bestIdx < 0) break;
            
            auto& ex = enemies_[(size_t)bestIdx];
            b.hitEnemies.push_back((uint32_t)bestIdx);
            
            // Enhanced visual feedback
            spawnBulletBurstFX(ex.pos, {120, 230, 255, 255}, 9, 1.0f);
            
            // Lightning arc line effect
            int numArcs = 3;
            for (int arc = 0; arc < numArcs; arc++) {
                for (int seg = 0; seg < 8; seg++) {
                    BoxFragment f;
                    float t = seg / 7.0f;
                    Vec2 start = pos;
                    Vec2 end = ex.pos;
                    f.pos = Vec2::lerp(start, end, t);
                    // Add perpendicular offset for arc effect
                    Vec2 dir = (end - start).normalized();
                    Vec2 perp = {-dir.y, dir.x};
                    float arcHeight = sinf(t * (float)M_PI) * (15.0f + (float)(rand() % 10));
                    f.pos += perp * arcHeight;
                    f.vel = {0, 0};
                    f.size = 2.0f + (float)(rand() % 3);
                    f.lifetime = 0.12f + (float)(rand() % 8) / 100.0f;
                    f.age = 0.0f;
                    f.rotation = 0;
                    f.rotSpeed = 0;
                    int bright = 150 + rand() % 105;
                    f.color = {(Uint8)(bright * 0.5f), (Uint8)bright, (Uint8)255, 255};
                    boxFragments_.push_back(f);
                }
            }
            
            ex.damageFlash = 1.0f;
            if (simAuth) {
                // Chain lightning deals 50% of bullet damage per jump
                ex.hp -= std::max(1, b.damage / 2);
                ex.stunTimer = std::max(ex.stunTimer, 0.65f);
                if (ex.hp <= 0) creditEnemyKill(ex, b.ownerId);
            }
            pos = ex.pos;  // Continue chain from this enemy
            primaryEnemy = bestIdx;  // Update primary to avoid re-hitting
            jumps++;
        }
        if (jumps > 0 && sfxParry_) {
            int ch = Mix_PlayChannel(-1, sfxParry_, 0);
            if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 3);
        }
    };

    // Player bullets vs enemies — all peers resolve locally for instant feedback;
    // the killer also sends EnemyKilled so the host/others stay in sync.
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (size_t ei = 0; ei < enemies_.size(); ++ei) {
            auto& e = enemies_[ei];
            if (!e.alive) continue;
            if (circleOverlap(b.pos, b.size, e.pos, e.size * 0.7f)) {
                // Piercing: skip enemies already struck by this bullet
                if (b.piercing) {
                    if (std::find(b.hitEnemies.begin(), b.hitEnemies.end(), (uint32_t)ei) != b.hitEnemies.end())
                        continue;
                    b.hitEnemies.push_back((uint32_t)ei);
                }
                // Visual feedback on all peers
                e.damageFlash = 1.0f;
                // Blood splatter — visual only, runs on all peers
                {
                    Vec2 hitDir = b.vel.normalized();
                    // Directional spray in bullet direction
#ifdef __SWITCH__
                    int numDirSparks = 4 + rand() % 3;
#else
                    int numDirSparks = 7 + rand() % 5;
#endif
                    for (int i = 0; i < numDirSparks; i++) {
                        BoxFragment f;
                        f.pos = {b.pos.x + (float)(rand() % 14 - 7), b.pos.y + (float)(rand() % 14 - 7)};
                        float spread = ((float)(rand() % 160) - 80.0f) * (float)M_PI / 180.0f;
                        float spd = 120.0f + (float)(rand() % 280);
                        float ang = atan2f(hitDir.y, hitDir.x) + spread;
                        f.vel = {cosf(ang) * spd, sinf(ang) * spd};
                        f.size = 3.0f + (float)(rand() % 6);
                        f.lifetime = 0.35f + (float)(rand() % 25) / 100.0f;
                        f.age = 0.0f;
                        f.rotation = (float)(rand() % 360);
                        f.rotSpeed = (float)(rand() % 500 - 250);
                        int shade = 80 + rand() % 130;
                        f.color = {(Uint8)shade, 0, 0, 255};
                        boxFragments_.push_back(f);
                    }
                    // Omnidirectional burst (mini death explosion)
#ifdef __SWITCH__
                    int numBurst = 3 + rand() % 3;
#else
                    int numBurst = 5 + rand() % 5;
#endif
                    for (int i = 0; i < numBurst; i++) {
                        BoxFragment f;
                        f.pos = {b.pos.x + (float)(rand() % 16 - 8), b.pos.y + (float)(rand() % 16 - 8)};
                        float ang = (float)(rand() % 360) * (float)M_PI / 180.0f;
                        float spd = 80.0f + (float)(rand() % 220);
                        f.vel = {cosf(ang) * spd, sinf(ang) * spd};
                        f.size = 2.0f + (float)(rand() % 5);
                        f.lifetime = 0.3f + (float)(rand() % 30) / 100.0f;
                        f.age = 0.0f;
                        f.rotation = (float)(rand() % 360);
                        f.rotSpeed = (float)(rand() % 400 - 200);
                        int shade = 90 + rand() % 120;
                        f.color = {(Uint8)shade, 0, 0, 255};
                        boxFragments_.push_back(f);
                    }
                    // Central blood decal (bigger)
                    BloodDecal bd;
                    bd.pos = b.pos;
                    bd.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
                    bd.scale = 0.5f + (float)(rand() % 50) / 100.0f;
                    bd.type = DecalType::Blood;
                    blood_.push_back(bd);
                    // Extra scattered splat decals
#ifdef __SWITCH__
                    int numExtra = 1 + rand() % 2;
#else
                    int numExtra = 2 + rand() % 2;
#endif
                    for (int i = 0; i < numExtra; i++) {
                        BloodDecal extra;
                        float dist = 10.0f + (float)(rand() % 30);
                        float ang = (float)(rand() % 360) * (float)M_PI / 180.0f;
                        extra.pos = {b.pos.x + cosf(ang) * dist, b.pos.y + sinf(ang) * dist};
                        extra.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
                        extra.scale = 0.25f + (float)(rand() % 40) / 100.0f;
                        extra.type = DecalType::Blood;
                        blood_.push_back(extra);
                    }
                }
                if (!b.piercing) {
                    b.alive = false;
                    // Notify remote peers that this bullet is gone
                    if (net.isInGame() && b.netId != 0) {
                        net.sendBulletHit(b.netId);
                    }
                }
                triggerExplosiveTip(b, b.pos, (int)ei);
                triggerChainLightning(b, b.pos, (int)ei);
                // State changes are authoritative on: offline, P2P host, or dedicated-server lobby-host
                if (simAuth) {
                    e.hp -= b.damage;
                    if (upgrades_.hasStunRounds) e.stunTimer = std::max(e.stunTimer, 0.75f);
                    // Aggro — target the player who shot this bullet
                    e.state = EnemyState::Chase;
                    e.lastSeenTime = gameTime_;
                    e.idleTimer    = 0;
                    if (b.ownerId != 255) {
                        e.targetPlayerId = b.ownerId;
                        e.targetPlayerSlot = b.ownerSubSlot;
                    }
                    if (e.hp <= 0) {
                        creditEnemyKill(e, b.ownerId);
                    }
                }
                if (!b.piercing) break;
            }
        }
    }

    // Enemy bullets vs player
    for (auto& b : enemyBullets_) {
        if (!b.alive || p.dead) continue;
        if (circleOverlap(b.pos, b.size, p.pos, PLAYER_SIZE * 0.5f)) {
            if (p.isParrying) {
                // Parry reflects!
                b.vel = b.vel * -1.0f;
                b.tag = TAG_BULLET;
                bullets_.push_back(b);
                b.alive = false;
                camera_.addShake(2.2f);
                rumble(0.40f, 72, 0.50f, 1.55f);  // Sharp parry reflect rumble
                if (sfxParry_) { int ch = Mix_PlayChannel(-1, sfxParry_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
            } else {
                p.takeDamage(b.damage);
                b.alive = false;
                camera_.addShake(1.8f);
                rumble(0.48f, 150, 1.25f, 0.78f);  // Heavier damage thud
                if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                if (p.dead) {
                    if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
                    camera_.addShake(4.0f);
                    rumble(0.92f, 340, 1.50f, 0.82f);  // Heavy death rumble
                    auto& net = NetworkManager::instance();
                    if (!net.isInGame()) spawnPlayerDeathEffect(p.pos);
                    if (net.isInGame()) net.sendPlayerDied(net.localPlayerId(), 0);
                }
            }
        }
    }

    // Melee enemies vs player (dash collision)
    if (!p.dead) for (auto& e : enemies_) {
        if (!e.alive || !(e.isDashing || e.netIsDashing)) continue;
        if (circleOverlap(e.pos, e.size * 0.6f, p.pos, PLAYER_SIZE * 0.5f)) {
            if (p.isParrying) {
                // Parry counter!
                e.hp -= PARRY_DMG;
                e.stunTimer = 1.0f;
                e.isDashing = false;
                e.vel = (e.pos - p.pos).normalized() * 500.0f;
                e.damageFlash = 1.0f;
                camera_.addShake(2.2f);
                rumble(0.52f, 84, 0.65f, 1.45f);  // Sharp parry counter rumble
                if (sfxParry_) { int ch = Mix_PlayChannel(-1, sfxParry_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                if (e.hp <= 0) {
                    killEnemy(e);
                    // Broadcast parry kill over network so clients see the enemy die
                    auto& net = NetworkManager::instance();
                    const bool simAuth = net.isHost() || (net.isConnectedToDedicated() && net.isLobbyHost());
                    if (net.isInGame() && simAuth) {
                        uint32_t eIdx = (uint32_t)(&e - &enemies_[0]);
                        net.sendEnemyKilled(eIdx, net.localPlayerId());
                        enemyStatesNeedUpdate_ = true;
                    }
                }
            } else {
                p.takeDamage(e.contactDamage);
                rumble(0.54f, 150, 1.30f, 0.72f);  // Heavy contact impact rumble
                camera_.addShake(1.8f);
                if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                if (p.dead) {
                    if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
                    camera_.addShake(4.0f);
                    rumble(0.92f, 340, 1.50f, 0.82f);  // Heavy death rumble
                    auto& net2 = NetworkManager::instance();
                    if (!net2.isInGame()) spawnPlayerDeathEffect(p.pos);
                    if (net2.isInGame()) net2.sendPlayerDied(net2.localPlayerId(), 0);
                }
            }
        }
    }

    // ── Local co-op / MP-splitscreen: damage for extra players (slots 1–3) ──
    bool anyLocalSplitscreen = coopPlayerCount_ > 1 &&
        (state_ == GameState::LocalCoopGame  || state_ == GameState::LocalCoopPaused ||
         state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
         state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);
    if (anyLocalSplitscreen) {
        for (int ci = 1; ci < 4; ci++) {
            if (!coopSlots_[ci].joined) continue;
            Player& cp = coopSlots_[ci].player;
            if (cp.dead) continue;
            for (auto& b : enemyBullets_) {
                if (!b.alive) continue;
                if (cp.invulnerable) continue;
                float eHitR = b.size + PLAYER_SIZE * 0.5f;
                bool ePoint = circleOverlap(b.pos, b.size, cp.pos, PLAYER_SIZE * 0.5f);
                bool eSwept = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, cp.pos, eHitR);
                if (ePoint || eSwept) {
                    if (cp.isParrying) {
                        b.vel = b.vel * -1.0f; b.tag = TAG_BULLET;
                        bullets_.push_back(b); b.alive = false;
                        coopSlots_[ci].camera.addShake(2.2f);
                        rumbleForSlot(ci, 0.40f, 72, 0.50f, 1.55f);
                        if (sfxParry_) { int ch = Mix_PlayChannel(-1, sfxParry_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    } else {
                        cp.takeDamage(b.damage); b.alive = false;
                        coopSlots_[ci].camera.addShake(1.8f);
                        rumbleForSlot(ci, cp.dead ? 0.92f : 0.48f, cp.dead ? 340 : 150,
                                      cp.dead ? 1.50f : 1.25f, cp.dead ? 0.82f : 0.78f);
                        if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                    }
                }
            }
            for (auto& e : enemies_) {
                if (!e.alive || !(e.isDashing || e.netIsDashing)) continue;
                if (circleOverlap(e.pos, e.size * 0.6f, cp.pos, PLAYER_SIZE * 0.5f)) {
                    if (cp.isParrying) {
                        e.hp -= PARRY_DMG; e.stunTimer = 1.0f; e.isDashing = false;
                        e.vel = (e.pos - cp.pos).normalized() * 500.0f; e.damageFlash = 1.0f;
                        coopSlots_[ci].camera.addShake(2.2f);
                        rumbleForSlot(ci, 0.52f, 84, 0.65f, 1.45f);
                        if (sfxParry_) { int ch = Mix_PlayChannel(-1, sfxParry_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                        if (e.hp <= 0) killEnemy(e);
                    } else {
                        cp.takeDamage(e.contactDamage);
                        coopSlots_[ci].camera.addShake(1.8f);
                        rumbleForSlot(ci, cp.dead ? 0.92f : 0.54f, cp.dead ? 340 : 150,
                                      cp.dead ? 1.50f : 1.30f, cp.dead ? 0.82f : 0.72f);
                        if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                    }
                }
            }
            // Player bullets vs other co-op players (PvP)
            if (currentRules_.pvpEnabled) {
                for (auto& b : bullets_) {
                    if (!b.alive) continue;
                    if (b.ownerSubSlot == (uint8_t)ci) continue;  // can't hurt yourself
                    float hitRadius = b.size + PLAYER_SIZE * 0.6f;
                    bool pointHit = circleOverlap(b.pos, b.size, cp.pos, PLAYER_SIZE * 0.5f);
                    bool sweptHit = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, cp.pos, hitRadius);
                    if (pointHit || sweptHit) {
                        cp.takeDamage(b.damage);
                        b.alive = false;
                        coopSlots_[ci].camera.addShake(1.8f);
                        rumbleForSlot(ci, cp.dead ? 0.92f : 0.48f, cp.dead ? 340 : 150,
                                      cp.dead ? 1.50f : 1.25f, cp.dead ? 0.82f : 0.78f);
                        if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                    }
                }
            }
            // Track co-op death (cp was alive at loop start)
            if (cp.dead) coopSlots_[ci].deaths++;
        }

        // ── PvP bullets vs slot 0 (P1) in offline local coop ──
        // (The ci=1..3 loop above covers sub-players; slot 0 is player_ and
        //  needs a separate check since it's outside that loop.)
        if (!net.isInGame() && currentRules_.pvpEnabled &&
            coopSlots_[0].joined && !player_.dead) {
            bool p1WasAlive = !player_.dead;
            for (auto& b : bullets_) {
                if (!b.alive) continue;
                if (b.ownerSubSlot == 0) continue;  // slot 0 can't hurt themselves
                float hitRadius = b.size + PLAYER_SIZE * 0.6f;
                bool pointHit = circleOverlap(b.pos, b.size, player_.pos, PLAYER_SIZE * 0.5f);
                bool sweptHit = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, player_.pos, hitRadius);
                if (pointHit || sweptHit) {
                    player_.takeDamage(b.damage);
                    b.alive = false;
                    camera_.addShake(player_.dead ? 4.0f : 1.8f);
                    rumbleForSlot(0, player_.dead ? 0.92f : 0.48f, player_.dead ? 340 : 150,
                                  player_.dead ? 1.50f : 1.25f, player_.dead ? 0.82f : 0.78f);
                    if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                }
            }
            if (p1WasAlive && player_.dead) coopSlots_[0].deaths++;
        }
    }

    // ── PVP: Player bullets vs remote players (when friendlyFire/pvp is enabled) ──
    if (net.isInGame() && currentRules_.pvpEnabled) {
        auto& players = net.players();
        bool mpSplitscreenLocal = coopPlayerCount_ > 1 &&
            (state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
             state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);

        if (mpSplitscreenLocal) {
            for (int targetSlot = 0; targetSlot < 4; targetSlot++) {
                if (!coopSlots_[targetSlot].joined) continue;
                Player& target = coopSlots_[targetSlot].player;
                if (target.dead) continue;
                for (auto& b : bullets_) {
                    if (!b.alive) continue;
                    if (b.ownerId != net.localPlayerId()) continue;
                    if (b.ownerSubSlot == (uint8_t)targetSlot) continue;
                    if (currentRules_.teamCount >= 2 && !currentRules_.friendlyFire && localTeam_ >= 0) continue;
                    float hitRadius = b.size + PLAYER_SIZE * 0.6f;
                    bool pointHit = circleOverlap(b.pos, b.size, target.pos, PLAYER_SIZE * 0.6f);
                    bool sweptHit = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, target.pos, hitRadius);
                    if (!pointHit && !sweptHit) continue;

                    b.alive = false;
                    if (net.isHost() || net.isConnectedToDedicated()) {
                        target.hp = std::max(0, target.hp - b.damage);
                        if (targetSlot == 0) {
                            player_.hp = target.hp;
                            if (net.localPlayer()) net.localPlayer()->hp = target.hp;
                        }
                        if (target.hp <= 0) {
                            target.dead = true;
                            if (targetSlot == 0) player_.dead = true;
                            if (targetSlot == 0) {
                                net.sendPlayerDied(net.localPlayerId(), b.ownerId);
                            } else {
                                net.sendSubPlayerDied(net.localPlayerId(), (uint8_t)targetSlot, b.ownerId);
                            }
                        } else {
                            if (targetSlot == 0) {
                                net.sendPlayerHpSync(net.localPlayerId(), target.hp, target.maxHp, b.ownerId);
                            } else {
                                net.sendSubPlayerHpSync(net.localPlayerId(), (uint8_t)targetSlot, target.hp, target.maxHp, b.ownerId);
                            }
                        }
                    } else {
                        net.sendHitRequest(b.netId, b.damage, b.ownerId, (uint8_t)targetSlot);
                    }

                    if (targetSlot == 0) {
                        camera_.addShake(target.hp <= 0 ? 4.0f : 2.0f);
                        rumbleForSlot(0, target.hp <= 0 ? 0.92f : 0.48f, target.hp <= 0 ? 340 : 150,
                                      target.hp <= 0 ? 1.50f : 1.25f, target.hp <= 0 ? 0.82f : 0.78f);
                    } else {
                        coopSlots_[targetSlot].camera.addShake(target.hp <= 0 ? 4.0f : 2.0f);
                        rumbleForSlot(targetSlot, target.hp <= 0 ? 0.92f : 0.48f, target.hp <= 0 ? 340 : 150,
                                      target.hp <= 0 ? 1.50f : 1.25f, target.hp <= 0 ? 0.82f : 0.78f);
                    }
                    if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                    if (target.hp <= 0 && sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
                    break;
                }
            }
        }

        for (auto& b : bullets_) {
            if (!b.alive) continue;
            // Only check OUR bullets against remote players (not remote bullets)
            if (b.ownerId != net.localPlayerId()) continue;
            for (auto& rp : players) {
                if (rp.id == net.localPlayerId()) continue;  // don't hit self
                if (!rp.alive) continue;
                // Team mode: skip same-team players unless friendly fire is on
                if (currentRules_.teamCount >= 2 && !currentRules_.friendlyFire) {
                    if (localTeam_ >= 0 && rp.team == localTeam_) continue; // same team — no damage
                }
                // Use interpolated position for hit detection
                Vec2 rpPos = {
                    rp.prevPos.x + (rp.targetPos.x - rp.prevPos.x) * rp.interpT,
                    rp.prevPos.y + (rp.targetPos.y - rp.prevPos.y) * rp.interpT
                };
                float hitRadius = b.size + PLAYER_SIZE * 0.6f;
                bool pointHit = circleOverlap(b.pos, b.size, rpPos, PLAYER_SIZE * 0.5f);
                bool sweptHit = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, rpPos, hitRadius);
                if (pointHit || sweptHit) {
                    b.alive = false;
                    // Visual feedback on shooter side — damage/kill is handled victim-side
                    // (the bullet is now in bullets_[] on the victim's machine and resolveCollisions
                    // there will call p.takeDamage() and send PlayerDied when HP reaches 0)
                    camera_.addShake(1.5f);
                    break;
                }
                for (auto& sp : rp.subPlayers) {
                    if (!b.alive || !sp.alive) continue;
                    Vec2 spPos = {
                        sp.prevPos.x + (sp.targetPos.x - sp.prevPos.x) * sp.interpT,
                        sp.prevPos.y + (sp.targetPos.y - sp.prevPos.y) * sp.interpT
                    };
                    float hitRadius = b.size + PLAYER_SIZE * 0.6f;
                    bool pointHit = circleOverlap(b.pos, b.size, spPos, PLAYER_SIZE * 0.5f);
                    bool sweptHit = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, spPos, hitRadius);
                    if (pointHit || sweptHit) {
                        b.alive = false;
                        camera_.addShake(1.5f);
                        break;
                    }
                }
                if (!b.alive) break;
            }
        }

        // Remote player bullets vs LOCAL player
        if (!p.dead) {
            for (auto& b : bullets_) {
                if (!b.alive) continue;
                if (b.ownerId == 255) continue; // skip unowned
                if (b.ownerId == net.localPlayerId() && b.ownerSubSlot == 0) continue; // skip self only
                // Team check
                if (currentRules_.teamCount >= 2 && !currentRules_.friendlyFire) {
                    if (b.ownerId == net.localPlayerId()) {
                        if (localTeam_ >= 0) continue;
                    } else {
                        NetPlayer* shooter = net.findPlayer(b.ownerId);
                        if (shooter && shooter->team == localTeam_ && localTeam_ >= 0) continue;
                    }
                }
                float hitRadius = b.size + PLAYER_SIZE * 0.6f;
                bool pointHit = circleOverlap(b.pos, b.size, p.pos, PLAYER_SIZE * 0.6f);
                bool sweptHit = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, p.pos, hitRadius);
                if (pointHit || sweptHit) {
                    b.alive = false;
                    if (net.isHost() || net.isConnectedToDedicated()) {
                        // P2P host OR dedicated-server client: each client processes its own incoming damage
                        // and broadcasts the result (dedicated server relays to all peers).
                        NetPlayer* localNetP = net.localPlayer();
                        int newHp = std::max(0, player_.hp - b.damage);
                        player_.hp = newHp;
                        if (localNetP) localNetP->hp = newHp;
                        if (newHp <= 0) {
                            p.die();
                            camera_.addShake(4.0f);
                            if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
                            net.sendPlayerDied(net.localPlayerId(), b.ownerId);
                        } else {
                            camera_.addShake(2.0f);
                            if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                            net.sendPlayerHpSync(net.localPlayerId(), player_.hp, player_.maxHp, b.ownerId);
                        }
                    } else {
                        // P2P non-host: report hit to host for validation — host will send back HP/death
                        net.sendHitRequest(b.netId, b.damage, b.ownerId);
                        // Optimistic visual feedback only (no HP deducted yet)
                        camera_.addShake(1.5f);
                        if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                    }
                    break;
                }
            }
        }

        bool mpSplitscreen = coopPlayerCount_ > 1 &&
            (state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
             state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);
        if (mpSplitscreen) {
            for (int ci = 1; ci < 4; ci++) {
                if (!coopSlots_[ci].joined) continue;
                Player& cp = coopSlots_[ci].player;
                if (cp.dead) continue;
                for (auto& b : bullets_) {
                    if (!b.alive) continue;
                    if (b.ownerId == 255) continue;
                    if (b.ownerId == net.localPlayerId() && b.ownerSubSlot == ci) continue;
                    if (currentRules_.teamCount >= 2 && !currentRules_.friendlyFire) {
                        if (b.ownerId == net.localPlayerId()) {
                            if (localTeam_ >= 0) continue;
                        } else {
                            NetPlayer* shooter = net.findPlayer(b.ownerId);
                            if (shooter && shooter->team == localTeam_ && localTeam_ >= 0) continue;
                        }
                    }
                    float hitRadius = b.size + PLAYER_SIZE * 0.6f;
                    bool pointHit = circleOverlap(b.pos, b.size, cp.pos, PLAYER_SIZE * 0.6f);
                    bool sweptHit = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, cp.pos, hitRadius);
                    if (!pointHit && !sweptHit) continue;

                    b.alive = false;
                    if (net.isHost() || net.isConnectedToDedicated()) {
                        cp.hp = std::max(0, cp.hp - b.damage);
                        coopSlots_[ci].camera.addShake(cp.hp <= 0 ? 4.0f : 2.0f);
                        rumbleForSlot(ci, cp.hp <= 0 ? 0.92f : 0.48f, cp.hp <= 0 ? 340 : 150,
                                      cp.hp <= 0 ? 1.50f : 1.25f, cp.hp <= 0 ? 0.82f : 0.78f);
                        if (cp.hp <= 0) {
                            cp.dead = true;
                            if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
                            net.sendSubPlayerDied(net.localPlayerId(), (uint8_t)ci, b.ownerId);
                        } else {
                            if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                            net.sendSubPlayerHpSync(net.localPlayerId(), (uint8_t)ci, cp.hp, cp.maxHp, b.ownerId);
                        }
                    } else {
                        net.sendHitRequest(b.netId, b.damage, b.ownerId, (uint8_t)ci);
                        coopSlots_[ci].camera.addShake(1.5f);
                        if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                    }
                    break;
                }
            }
        }
    }

    // Bombs vs enemies (dashed bombs)
    for (auto& b : bombs_) {
        if (!b.alive || !b.hasDashed) continue;
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            if (circleOverlap(b.pos, BOMB_SIZE, e.pos, e.size * 0.7f)) {
                spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
                b.alive = false;
                break;
            }
        }
    }

    // Bombs vs remote players (PvP, dashed bombs)
    if (lobbySettings_.isPvp || currentRules_.pvpEnabled) {
        auto& netBvP = NetworkManager::instance();
        uint8_t localId = netBvP.localPlayerId();
        for (auto& b : bombs_) {
            if (!b.alive || !b.hasDashed) continue;
            for (const auto& rp : netBvP.players()) {
                if (rp.id == localId || !rp.alive || rp.spectating) continue;
                if (rp.id == b.ownerId) continue;  // can't detonate on own player
                if (localTeam_ >= 0 && rp.team == localTeam_) continue;
                if (circleOverlap(b.pos, BOMB_SIZE, rp.targetPos, 18.0f)) {
                    spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
                    b.alive = false;
                    break;
                }
            }
        }
    }
}

void Game::killEnemy(Enemy& e, bool trackKill) {
    if (!e.alive) return;  // already dead — avoid double gib/score effects
    e.alive = false;
    // Central blood decal (large)
    BloodDecal bd;
    bd.pos = e.pos;
    bd.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
    bd.scale = 1.2f + (float)(rand() % 40) / 100.0f;
    bd.type = DecalType::Blood;
    blood_.push_back(bd);

    // Bloody explosion — big burst of red particles
#ifdef __SWITCH__
    int numGibs = 10 + rand() % 6;
#else
    int numGibs = 22 + rand() % 12;
#endif
    for (int i = 0; i < numGibs; i++) {
        BoxFragment f;
        f.pos = {e.pos.x + (float)(rand() % 20 - 10), e.pos.y + (float)(rand() % 20 - 10)};
        float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
        float spd = 150.0f + (float)(rand() % 350);
        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
        f.size = 3.0f + (float)(rand() % 7);
        f.lifetime = 0.7f + (float)(rand() % 100) / 200.0f;
        f.age = 0;
        f.alive = true;
        f.rotation = (float)(rand() % 360);
        f.rotSpeed = (float)(rand() % 600 - 300);
        // Red/dark-red blood colors
        int shade = 80 + rand() % 140;
        f.color = {(Uint8)shade, 0, 0, 255};
        boxFragments_.push_back(f);
    }
    // Extra blood splatter decals — varied sizes, spread out
#ifdef __SWITCH__
    int numExtra = 2 + rand() % 2;
#else
    int numExtra = 4 + rand() % 4;
#endif
    for (int i = 0; i < numExtra; i++) {
        BloodDecal extra;
        float dist = 20.0f + (float)(rand() % 60);
        float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
        extra.pos = {e.pos.x + cosf(angle) * dist, e.pos.y + sinf(angle) * dist};
        extra.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
        extra.scale = 0.3f + (float)(rand() % 80) / 100.0f;
        extra.type = DecalType::Blood;
        blood_.push_back(extra);
    }
    // Cap both pools
#ifdef __SWITCH__
    constexpr size_t KILL_MAX_FRAGS = 150;
    constexpr size_t KILL_MAX_DECALS = 80;
#else
    constexpr size_t KILL_MAX_FRAGS = 400;
    constexpr size_t KILL_MAX_DECALS = 200;
#endif
    if (boxFragments_.size() > KILL_MAX_FRAGS)
        boxFragments_.erase(boxFragments_.begin(),
                            boxFragments_.begin() + (int)(boxFragments_.size() - KILL_MAX_FRAGS));
    if (blood_.size() > KILL_MAX_DECALS)
        blood_.erase(blood_.begin(),
                     blood_.begin() + (int)(blood_.size() - KILL_MAX_DECALS));

    if (sfxEnemyExplode_) {
        int ch = Mix_PlayChannel(-1, sfxEnemyExplode_, 0);
        if (ch >= 0) Mix_Volume(ch, config_.sfxVolume * 3 / 4);
    }

    // Brief red flash
    screenFlashTimer_ = 0.05f;
    screenFlashR_ = 180; screenFlashG_ = 30; screenFlashB_ = 30;

    // Player kill registration (only for locally-originated kills)
    if (trackKill) {
        player_.killCounter++;
        if (upgrades_.hasScavenger && player_.ammo < player_.maxAmmo) {
            player_.ammo = std::min(player_.maxAmmo, player_.ammo + 1);
        }
        if (upgrades_.hasVampire) {
            player_.hp = std::min(player_.maxHp, player_.hp + 1);
        }
        if (player_.killCounter >= upgrades_.killsPerBomb) {
            player_.killCounter = 0;
            player_.bombCount = std::min(MAX_BOMBS, player_.bombCount + 1);
        }
    }
    // Local co-op kills tracking: credit the slot that owns this kill
    // (During resolveCollisions, player_ = slot 0; use ownerId if available)
    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
        // The kill is credited to slot 0 via player_ above; coopSlots_[0].kills
        // will be synced after resolveCollisions. Nothing else needed here — the
        // per-slot kill count is incremented in resolveCollisions via b.ownerId.
    }
}

void Game::playerParry() {
    Player& p = player_;
    p.canParry = false;
    p.isParrying = true;
    p.parryTimer = PARRY_WINDOW;           // Reflect/counter window
    p.parryDashTimer = PARRY_DASH_DURATION; // Dash movement duration (scout speed)
    p.parryCdTimer = PARRY_COOLDOWN;
    rumble(0.16f, 38, 0.32f, 1.20f);  // Softer parry dash startup

    // Dash direction = aim direction
    if (aimInput_.lengthSq() > 0.04f)
        p.parryDir = aimInput_.normalized();
    else
        p.parryDir = Vec2::fromAngle(p.rotation);

    if (sfxSwoosh_) { int ch = Mix_PlayChannel(-1, sfxSwoosh_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
}

void Game::destroyBox(int tx, int ty) {
    // Replace the box tile with grass
    map_.set(tx, ty, TILE_GRASS);
    invalidateMinimapCache();
    // Spawn wood-colored fragment particles — explosive burst
    float wx = TileMap::toWorld(tx);
    float wy = TileMap::toWorld(ty);
    int numFrags = 14 + rand() % 8;
    for (int i = 0; i < numFrags; i++) {
        BoxFragment f;
        f.pos = {wx + (float)(rand() % 24 - 12), wy + (float)(rand() % 24 - 12)};
        float angle = (float)(rand() % 360) * M_PI / 180.0f;
        float spd = 120.0f + (float)(rand() % 300);
        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
        f.rotation = (float)(rand() % 360);
        f.rotSpeed = (float)(rand() % 800 - 400);
        f.size = 3.0f + (float)(rand() % 8);
        f.lifetime = 0.5f + (float)(rand() % 50) / 100.0f;
        // Brown/wood color variations
        f.color = {(Uint8)(140 + rand() % 60), (Uint8)(90 + rand() % 50), (Uint8)(40 + rand() % 30), 255};
        boxFragments_.push_back(f);
    }
    // Orange flash particles (fire/explosion)
    for (int i = 0; i < 6; i++) {
        BoxFragment f;
        f.pos = {wx + (float)(rand() % 16 - 8), wy + (float)(rand() % 16 - 8)};
        float angle = (float)(rand() % 360) * M_PI / 180.0f;
        float spd = 60.0f + (float)(rand() % 120);
        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
        f.rotation = 0;
        f.rotSpeed = 0;
        f.size = 5.0f + (float)(rand() % 6);
        f.lifetime = 0.25f + (float)(rand() % 20) / 100.0f;
        int r = 220 + rand() % 36;
        f.color = {(Uint8)r, (Uint8)(140 + rand() % 60), (Uint8)(20 + rand() % 30), 255};
        boxFragments_.push_back(f);
    }
    screenFlashTimer_ = 0.08f;
    screenFlashR_ = 220; screenFlashG_ = 170; screenFlashB_ = 60;
    camera_.addShake(2.5f);
    if (sfxBreak_) { int ch = Mix_PlayChannel(-1, sfxBreak_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
}

void Game::spawnPlayerDeathEffect(Vec2 pos) {
    auto& net = NetworkManager::instance();
    const bool isOnline = net.isOnline();

    // Camera shake — big in singleplayer, moderate in multiplayer
    camera_.addShake(isOnline ? 5.0f : 12.0f);
    screenFlashTimer_ = isOnline ? 0.08f : 0.22f;
    screenFlashR_ = 255; screenFlashG_ = 55; screenFlashB_ = 20;

    if (!isOnline) {
        playExplosionFeedback(pos, EXPLOSION_RADIUS * 9.0f, 0.24f, 0.72f, 140, 340, 1.50f, 0.76f,
                              config_.sfxVolume, config_.sfxVolume / 14);
    }

    // Fire/gore burst
#ifdef __SWITCH__
    int numFire = isOnline ? 12 : 24;
#else
    int numFire = isOnline ? 20 : 52;
#endif
    for (int i = 0; i < numFire; i++) {
        BoxFragment f;
        f.pos = {pos.x + (float)(rand() % 20 - 10), pos.y + (float)(rand() % 20 - 10)};
        float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
        float spd = 160.0f + (float)(rand() % 450);
        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
        f.rotation = (float)(rand() % 360);
        f.rotSpeed = (float)(rand() % 800 - 400);
        f.size = 4.0f + (float)(rand() % 10);
        f.lifetime = 0.45f + (float)(rand() % 50) / 100.0f;
        f.age = 0; f.alive = true;
        int r = 190 + rand() % 65, g = 35 + rand() % 120, b = rand() % 50;
        f.color = {(Uint8)r, (Uint8)g, (Uint8)b, 255};
        boxFragments_.push_back(f);
    }
    // Smoke
#ifdef __SWITCH__
    int numSmoke = isOnline ? 3 : 8;
#else
    int numSmoke = isOnline ? 6 : 18;
#endif
    for (int i = 0; i < numSmoke; i++) {
        BoxFragment f;
        f.pos = {pos.x + (float)(rand() % 30 - 15), pos.y + (float)(rand() % 30 - 15)};
        float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
        float spd = 30.0f + (float)(rand() % 80);
        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
        f.rotation = 0; f.rotSpeed = 0;
        f.size = 10.0f + (float)(rand() % 14);
        f.lifetime = 0.65f + (float)(rand() % 40) / 100.0f;
        f.age = 0; f.alive = true;
        int v = 25 + rand() % 40;
        f.color = {(Uint8)v, (Uint8)v, (Uint8)v, 200};
        boxFragments_.push_back(f);
    }
    // Scorch marks — many in singleplayer, few in multiplayer
    {
        BloodDecal scorch;
        scorch.pos = pos;
        scorch.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
        scorch.scale = isOnline ? 1.2f : 2.2f;
        scorch.type = DecalType::Scorch;
        blood_.push_back(scorch);
#ifdef __SWITCH__
        int numScorch = isOnline ? 1 : 5;
#else
        int numScorch = isOnline ? 3 : 14;
#endif
        for (int i = 0; i < numScorch; i++) {
            BloodDecal s;
            float dist = 20.0f + (float)(rand() % (isOnline ? 60 : 130));
            float ang  = (float)(rand() % 360) * (float)M_PI / 180.0f;
            s.pos = {pos.x + cosf(ang) * dist, pos.y + sinf(ang) * dist};
            s.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
            s.scale = isOnline ? (0.3f + (float)(rand() % 40) / 100.0f)
                               : (0.55f + (float)(rand() % 110) / 100.0f);
            s.type = DecalType::Scorch;
            blood_.push_back(s);
        }
    }
}

void Game::updateBoxFragments(float dt) {
    for (auto& f : boxFragments_) {
        if (!f.alive) continue;
        f.age += dt;
        if (f.age >= f.lifetime) { f.alive = false; continue; }
        f.pos += f.vel * dt;
        f.vel = f.vel * (1.0f - dt * 4.0f); // friction
        f.rotation += f.rotSpeed * dt;
    }
    // Remove dead
    boxFragments_.erase(std::remove_if(boxFragments_.begin(), boxFragments_.end(),
        [](const BoxFragment& f) { return !f.alive; }), boxFragments_.end());
}

// ═════════════════════════════════════════════════════════════════════════════
//  Rendering
// ═════════════════════════════════════════════════════════════════════════════

void Game::render() {
    bool gameplayView =
        state_ == GameState::Playing || state_ == GameState::Paused || state_ == GameState::Dead ||
        state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused || state_ == GameState::CustomDead || state_ == GameState::CustomWin ||
        state_ == GameState::PlayingPack || state_ == GameState::PackPaused || state_ == GameState::PackDead || state_ == GameState::PackLevelWin ||
        state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused || state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator ||
        state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused || state_ == GameState::LocalCoopDead;

    bool usePostFX = sceneTarget_ && !isSplitscreenActive();

    if (usePostFX) SDL_SetRenderTarget(renderer_, sceneTarget_);
    SDL_SetRenderDrawColor(renderer_, 20, 20, 25, 255);
    SDL_RenderClear(renderer_);

#ifndef __SWITCH__
    // Hide OS cursor only during active gameplay; show everywhere else (menus, editors)
    if (state_ == GameState::Playing || state_ == GameState::Paused ||
        state_ == GameState::Dead ||
        state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused ||
        state_ == GameState::CustomDead ||
        state_ == GameState::PlayingPack || state_ == GameState::PackPaused ||
        state_ == GameState::PackDead ||
        state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
        state_ == GameState::MultiplayerDead ||
        state_ == GameState::LocalCoopGame)
        SDL_ShowCursor(SDL_DISABLE);
    else
        SDL_ShowCursor(SDL_ENABLE);
#endif

    switch (state_) {
    case GameState::MainMenu:
        renderMainMenu();
        break;
    case GameState::PlayModeMenu:
        renderPlayModeMenu();
        break;
    case GameState::ConfigMenu:
        renderConfigMenu();
        break;
    case GameState::Playing:
    case GameState::Paused:
    case GameState::Dead:
        renderMap();
        renderDecals();

        // Bombs
        for (auto& b : bombs_) {
            if (!b.alive) continue;
            SDL_Texture* tex = (!bombSprites_.empty()) ?
                bombSprites_[b.animFrame % bombSprites_.size()] : nullptr;
            if (tex) renderSprite(tex, b.pos, 0, 2.0f);
        }

        // Explosions (pixel-art style)
        for (auto& ex : explosions_) {
            if (!ex.alive) continue;
            renderExplosionPixelated(ex);
        }

        // Enemies
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            SDL_Texture* tex = enemySpriteTex(e.type);
            float drawScale = e.renderScale;
            if (tex) {
                // Dash state (works both on host and clients via netIsDashing)
                bool showDash    = e.isDashing    || e.netIsDashing;
                bool showCharge  = e.dashCharging || e.netDashCharging;

                // Red ghost trail during dash — subtle/transparent
                if (showDash && isMeleeEnemyType(e.type)) {
                    Vec2 trailDir = (e.dashDir.lengthSq() > 0.001f) ? e.dashDir : Vec2{1,0};
                    for (int t = 1; t <= 4; t++) {
                        Vec2 trailPos = e.pos - trailDir * e.size * 0.65f * (float)t;
                        Uint8 alpha = (Uint8)std::max(0, 70 - t * 16);
                        SDL_Color trailCol = {220, 60, 60, alpha};
                        renderSpriteEx(tex, trailPos, e.rotation + M_PI/2,
                                       drawScale * (1.0f - t * 0.10f), trailCol);
                    }
                }

                // Determine tint for the main sprite
                if (showDash) {
                    // Solid bright red during dash
                    SDL_Color tint = {255, 30, 30, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, tint);
                } else if (showCharge) {
                    // Pulsing red during wind-up
                    Uint8 pulse = (Uint8)(120 + (int)(sinf(gameTime_ * 28.0f) * 100.0f + 100.0f) / 2);
                    SDL_Color tint = {255, pulse, pulse, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, tint);
                } else if (e.damageFlash > 0 || e.flashTimer > 0) {
                    Uint8 redness = (Uint8)(255.0f * std::min(1.0f, e.damageFlash + e.flashTimer));
                    Uint8 other   = (Uint8)(15.0f * (1.0f - std::min(1.0f, e.damageFlash + e.flashTimer)));
                    SDL_Color tint = {255, (Uint8)(other + redness/12), (Uint8)(other + redness/12), 255};
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, tint);
                } else {
                    // Health-based tint
                    float hpRatio = (e.maxHp > 0.0f) ? (e.hp / e.maxHp) : 1.0f;
                    hpRatio = std::max(0.0f, std::min(1.0f, hpRatio));
                    SDL_Color base = enemyBaseTint(e.type);
                    Uint8 r = base.r;
                    Uint8 g = (Uint8)(base.g * hpRatio);
                    Uint8 b = (Uint8)(base.b * hpRatio);
                    SDL_Color tint = {r, g, b, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, tint);
                }
            }
        }

        // Player legs
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + M_PI/2, 1.5f);
        }

        // Player body
        if (!player_.dead) {
            if (!playerSprites_.empty()) {
                int idx = player_.animFrame % (int)playerSprites_.size();
                Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
                // Flash white when invulnerable
                if (player_.invulnerable && ((int)(player_.invulnTimer * 10) % 2 == 0)) {
                    SDL_Color tint = {255, 255, 255, 128};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + M_PI/2, 1.5f, tint);
                } else if (player_.isParrying) {
                    SDL_Color tint = {128, 200, 255, 255};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + M_PI/2, 1.5f, tint);
                } else {
                    renderSprite(playerSprites_[idx], bodyPos, player_.rotation + M_PI/2, 1.5f);
                }
            }
        } else {
            // Death animation
            if (!playerDeathSprites_.empty()) {
                int idx = player_.animFrame % (int)playerDeathSprites_.size();
                renderSprite(playerDeathSprites_[idx], player_.pos, player_.rotation + M_PI/2, 1.5f);
            }
        }

        // Bullet trails + Bullets
        for (auto& b : bullets_) {
            if (!b.alive) continue;
            // Trail: draw fading dots behind the bullet
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                Uint8 a = (Uint8)(fade * 180);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 140, a);
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
            else {
                Vec2 sp = camera_.worldToScreen(b.pos);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 255);
                SDL_FRect r = {sp.x - 2, sp.y - 2, 4, 4};
                SDL_RenderFillRectF(renderer_, &r);
            }
        }
        for (auto& b : enemyBullets_) {
            if (!b.alive) continue;
            // Trail: red fading dots
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                Uint8 a = (Uint8)(fade * 180);
                SDL_SetRenderDrawColor(renderer_, 255, 60, 60, a);
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) {
                SDL_SetTextureColorMod(b.sprite, 255, 80, 80);
                renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
                SDL_SetTextureColorMod(b.sprite, 255, 255, 255);
            } else {
                Vec2 sp = camera_.worldToScreen(b.pos);
                SDL_SetRenderDrawColor(renderer_, 255, 80, 80, 255);
                SDL_FRect r = {sp.x - 3.0f, sp.y - 3.0f, 6, 6};
                SDL_RenderFillRectF(renderer_, &r);
            }
        }

        // Debris
        for (auto& d : debris_) {
            Vec2 sp = camera_.worldToScreen(d.pos);
            float a = 1.0f - d.age / d.lifetime;
            SDL_SetRenderDrawColor(renderer_, 200, 200, 180, (Uint8)(a * 255));
            for (int i = 0; i < 5; i++) {
                int ox = (rand() % 10) - 5;
                int oy = (rand() % 10) - 5;
                SDL_RenderDrawPoint(renderer_, (int)sp.x + ox, (int)sp.y + oy);
            }
        }

        // Box fragment particles
        for (auto& f : boxFragments_) {
            if (!f.alive) continue;
            Vec2 sp = camera_.worldToScreen(f.pos);
            // Cull off-screen fragments
            if (sp.x < -32 || sp.x > SCREEN_W + 32 || sp.y < -32 || sp.y > SCREEN_H + 32) continue;
            float alpha = 1.0f - (f.age / f.lifetime);
            int sz = (int)(f.size * alpha + 1);
            SDL_SetRenderDrawColor(renderer_, f.color.r, f.color.g, f.color.b, (Uint8)(alpha * 255));
            SDL_Rect r = {(int)(sp.x - sz/2), (int)(sp.y - sz/2), sz, sz};
            SDL_RenderFillRect(renderer_, &r);
        }

        // Crates & Pickups
        renderCrates();
        renderPickups();
        renderRemotePlayers();

        // UI Layer
        renderWallOverlay();
        renderRoofOverlay();
        renderShadingPass();
        renderUI();

        if (state_ == GameState::Paused) renderPauseMenu();
        if (state_ == GameState::Dead)   renderDeathScreen();
        break;

    case GameState::EditorConfig:
        editor_.render(renderer_);
        break;

    case GameState::Editor:
        editor_.render(renderer_);
        break;

    case GameState::MapSelect:
    case GameState::MapConfig:
        renderMapSelectMenu();
        if (state_ == GameState::MapConfig) renderMapConfigMenu();
        break;

    case GameState::CharSelect:
        renderCharSelectMenu();
        break;

    case GameState::CharCreator:
        renderCharCreator();
        break;

    case GameState::PlayingCustom:
    case GameState::CustomPaused:
    case GameState::CustomDead:
    case GameState::CustomWin:
        // Reuse same gameplay rendering as Playing
        renderMap();
        renderDecals();
        for (auto& b : bombs_) {
            if (!b.alive) continue;
            SDL_Texture* tex = (!bombSprites_.empty()) ?
                bombSprites_[b.animFrame % bombSprites_.size()] : nullptr;
            if (tex) renderSprite(tex, b.pos, 0, 2.0f);
        }
        for (auto& ex : explosions_) {
            if (!ex.alive) continue;
            renderExplosionPixelated(ex);
        }
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            SDL_Texture* tex = enemySpriteTex(e.type);
            float drawScale = e.renderScale;
            if (tex) {
                if (e.damageFlash > 0 || e.flashTimer > 0 || e.dashCharging) {
                    float flash = std::min(1.0f, e.damageFlash + e.flashTimer + (e.dashCharging ? 1.0f : 0.0f));
                    Uint8 other = (Uint8)(15.0f * (1.0f - std::min(1.0f, flash)));
                    SDL_Color tint = {255, (Uint8)(other + (Uint8)(flash * 20.0f / 12.0f)), (Uint8)(other + (Uint8)(flash * 20.0f / 12.0f)), 255};
                    renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, tint);
                } else {
                    float hpRatio = (e.maxHp > 0.0f) ? (e.hp / e.maxHp) : 1.0f;
                    hpRatio = std::max(0.0f, std::min(1.0f, hpRatio));
                    SDL_Color base = enemyBaseTint(e.type);
                    Uint8 rr = base.r, gg = (Uint8)(base.g * hpRatio), bb = (Uint8)(base.b * hpRatio);
                    SDL_Color tint = {rr, gg, bb, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, tint);
                }
            }
        }
        // Player rendering (same as Playing)
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + (float)M_PI/2, 1.5f);
        }
        if (!player_.dead) {
            if (!playerSprites_.empty()) {
                int idx = player_.animFrame % (int)playerSprites_.size();
                Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
                if (player_.invulnerable && ((int)(player_.invulnTimer * 10) % 2 == 0)) {
                    SDL_Color tint = {255, 255, 255, 128};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 1.5f, tint);
                } else if (player_.isParrying) {
                    SDL_Color tint = {128, 200, 255, 255};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 1.5f, tint);
                } else {
                    renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 1.5f);
                }
            }
        } else if (!playerDeathSprites_.empty()) {
            int idx = player_.animFrame % (int)playerDeathSprites_.size();
            renderSprite(playerDeathSprites_[idx], player_.pos, player_.rotation + (float)M_PI/2, 1.5f);
        }
        for (auto& b : bullets_) {
            if (!b.alive) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                Uint8 a = (Uint8)(fade * 180);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 140, a);
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
        }
        for (auto& b : enemyBullets_) {
            if (!b.alive) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                Uint8 a = (Uint8)(fade * 180);
                SDL_SetRenderDrawColor(renderer_, 255, 60, 60, a);
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) {
                SDL_SetTextureColorMod(b.sprite, 255, 80, 80);
                renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
                SDL_SetTextureColorMod(b.sprite, 255, 255, 255);
            }
        }
        for (auto& f : boxFragments_) {
            if (!f.alive) continue;
            Vec2 sp = camera_.worldToScreen(f.pos);
            if (sp.x < -32 || sp.x > SCREEN_W + 32 || sp.y < -32 || sp.y > SCREEN_H + 32) continue;
            float alpha = 1.0f - (f.age / f.lifetime);
            int sz = (int)(f.size * alpha + 1);
            SDL_SetRenderDrawColor(renderer_, f.color.r, f.color.g, f.color.b, (Uint8)(alpha * 255));
            SDL_Rect rr = {(int)(sp.x - sz/2), (int)(sp.y - sz/2), sz, sz};
            SDL_RenderFillRect(renderer_, &rr);
        }
        renderCrates();
        renderPickups();
        renderRemotePlayers();
        renderWallOverlay();
        renderRoofOverlay();
        renderShadingPass();
        renderUI();

        // Render goal indicator for custom maps
        if (playingCustomMap_ && customGoalOpen_) {
            MapTrigger* goal = customMap_.findEndTrigger();
            if (goal) {
                Vec2 gp = camera_.worldToScreen({goal->x, goal->y});
                SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 150);
                SDL_Rect gr = {(int)(gp.x - goal->width/2), (int)(gp.y - goal->height/2),
                              (int)goal->width, (int)goal->height};
                SDL_RenderFillRect(renderer_, &gr);
                drawTextCentered("EXIT", (int)gp.y - 8, 16, {50, 255, 100, 255});
            }
        }

        if (state_ == GameState::CustomPaused) renderPauseMenu();
        if (state_ == GameState::CustomDead)   renderDeathScreen();
        if (state_ == GameState::CustomWin)    renderCustomWinScreen();
        break;

    case GameState::PackSelect:
        renderPackSelectMenu();
        break;

    case GameState::PlayingPack:
    case GameState::PackPaused:
    case GameState::PackDead:
    case GameState::PackLevelWin:
        // Reuse same gameplay rendering as PlayingCustom
        renderMap();
        renderDecals();
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            SDL_Texture* tex = enemySpriteTex(e.type);
            float drawScale = e.renderScale;
            if (tex) renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, enemyBaseTint(e.type));
        }
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + (float)M_PI/2, 1.5f);
        }
        if (!player_.dead && !playerSprites_.empty()) {
            int idx = player_.animFrame % (int)playerSprites_.size();
            Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
            if (localTeam_ >= 0 && localTeam_ < 4 && currentRules_.teamCount >= 2) {
                static const SDL_Color ltTint[4] = {
                    {255,210,210,255},{210,220,255,255},{210,255,220,255},{255,250,210,255}
                };
                renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 1.5f, ltTint[localTeam_]);
            } else {
                renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 1.5f);
            }
        }
        for (auto& b : bullets_) {
            if (!b.alive || !b.sprite) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                Uint8 a = (Uint8)(fade * 180);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 140, a);
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
        }
        for (auto& b : enemyBullets_) {
            if (!b.alive || !b.sprite) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                Uint8 a = (Uint8)(fade * 180);
                SDL_SetRenderDrawColor(renderer_, 255, 60, 60, a);
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            SDL_SetTextureColorMod(b.sprite, 255, 80, 80);
            renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
            SDL_SetTextureColorMod(b.sprite, 255, 255, 255);
        }
        renderWallOverlay();
        renderRoofOverlay();
        renderShadingPass();
        renderUI();

        // Goal indicator
        if (customGoalOpen_) {
            MapTrigger* goal = customMap_.findEndTrigger();
            if (goal) {
                Vec2 gp = camera_.worldToScreen({goal->x, goal->y});
                SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 150);
                SDL_Rect gr = {(int)(gp.x - goal->width/2), (int)(gp.y - goal->height/2),
                              (int)goal->width, (int)goal->height};
                SDL_RenderFillRect(renderer_, &gr);
                drawTextCentered("EXIT", (int)gp.y - 8, 16, {50, 255, 100, 255});
            }
        }

        if (state_ == GameState::PackPaused) renderPauseMenu();
        if (state_ == GameState::PackDead) renderDeathScreen();
        if (state_ == GameState::PackLevelWin) renderPackLevelWin();
        break;

    case GameState::PackComplete:
        renderPackComplete();
        break;

    // ── Multiplayer states ──
    case GameState::MultiplayerMenu:
        renderMultiplayerMenu();
        break;

    case GameState::HostSetup:
        renderHostSetup();
        break;

    case GameState::JoinMenu:
        renderJoinMenu();
        break;

    case GameState::Lobby:
        renderLobby();
        break;

    case GameState::MultiplayerGame:
    case GameState::MultiplayerPaused:
    case GameState::MultiplayerDead:
    case GameState::MultiplayerSpectator:
        if (coopPlayerCount_ > 1) {
            // ── Splitscreen multiplayer rendering ──
            renderMultiplayerSplitscreen();
        } else {
        // Reuse standard gameplay rendering
        renderMap();
        renderDecals();
        for (auto& b : bombs_) {
            if (!b.alive) continue;
            SDL_Texture* tex = (!bombSprites_.empty()) ?
                bombSprites_[b.animFrame % bombSprites_.size()] : nullptr;
            if (tex) renderSprite(tex, b.pos, 0, 2.0f);
        }
        for (auto& ex : explosions_) {
            if (!ex.alive) continue;
            renderExplosionPixelated(ex);
        }
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            SDL_Texture* tex = enemySpriteTex(e.type);
            float drawScale = e.renderScale;
            if (tex) renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, enemyBaseTint(e.type));
        }
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            if (spectatorMode_) SDL_SetTextureAlphaMod(legSprites_[idx], 80);
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + (float)M_PI/2, 1.5f);
            if (spectatorMode_) SDL_SetTextureAlphaMod(legSprites_[idx], 255);
        }
        if (!player_.dead && !playerSprites_.empty()) {
            int idx = player_.animFrame % (int)playerSprites_.size();
            Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
            if (spectatorMode_) SDL_SetTextureAlphaMod(playerSprites_[idx], 80);
            if (localTeam_ >= 0 && localTeam_ < 4 && currentRules_.teamCount >= 2) {
                static const SDL_Color ltTint[4] = {
                    {255,210,210,255},{210,220,255,255},{210,255,220,255},{255,250,210,255}
                };
                renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 1.5f, ltTint[localTeam_]);
            } else {
                renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 1.5f);
            }
            if (spectatorMode_) SDL_SetTextureAlphaMod(playerSprites_[idx], 255);
        }
        for (auto& b : bullets_) {
            if (!b.alive) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                SDL_SetRenderDrawColor(renderer_, 255, 255, 140, (Uint8)(fade * 180));
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
        }
        // Enemy bullets
        for (auto& b : enemyBullets_) {
            if (!b.alive) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                SDL_SetRenderDrawColor(renderer_, 255, 60, 60, (Uint8)(fade * 180));
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) {
                SDL_SetTextureColorMod(b.sprite, 255, 80, 80);
                renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
                SDL_SetTextureColorMod(b.sprite, 255, 255, 255);
            }
        }
        renderCrates();
        renderPickups();
        renderRemotePlayers();
        renderWallOverlay();
        renderRoofOverlay();
        renderShadingPass();
        renderUI();
        renderMultiplayerHUD();
        } // end single-viewport block

        if (state_ == GameState::MultiplayerPaused) renderMultiplayerPause();
        if (state_ == GameState::MultiplayerDead && coopPlayerCount_ <= 1) renderMultiplayerDeath();
        if (state_ == GameState::MultiplayerSpectator) {
            // Ghost tint
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 40, 120, 200, 40);
            SDL_Rect full2 = {0, 0, SCREEN_W, SCREEN_H};
            SDL_RenderFillRect(renderer_, &full2);
            // Banner
            SDL_Color bannerCol = {100, 200, 255, 255};
            drawTextCentered("SPECTATING  —  Press ESC/START to pause", 14, 18, bannerCol);
        }
        break;

    case GameState::WinLoss:
        renderWinLoss();
        break;

    case GameState::Scoreboard:
        renderScoreboard();
        break;

    case GameState::TeamSelect:
        renderTeamSelect();
        break;

    case GameState::ModMenu:
        renderModMenu();
        break;

    case GameState::LocalCoopLobby:
        renderLocalCoopLobby();
        break;

    case GameState::LocalCoopGame:
    case GameState::LocalCoopPaused:
    case GameState::LocalCoopDead:
        renderLocalCoopGame();
        if (state_ == GameState::LocalCoopPaused) renderPauseMenu();
        if (state_ == GameState::LocalCoopDead)   renderDeathScreen();
        break;
    }

    // Mod-save dialog overlay — rendered on top of everything
    if (modSaveDialog_.isOpen())
        renderModSaveDialog();

    ui_.endFrame();
    if (usePostFX) {
        SDL_SetRenderTarget(renderer_, nullptr);
        renderPostFXComposite(gameplayView);
    }
    SDL_RenderPresent(renderer_);
}

bool Game::isSplitscreenActive() const {
    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused ||
        state_ == GameState::LocalCoopDead) {
        return true;
    }
    return coopPlayerCount_ > 1 &&
           (state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
            state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);
}

void Game::renderPostFXComposite(bool gameplayView) {
    SDL_SetRenderDrawColor(renderer_, 8, 8, 12, 255);
    SDL_RenderClear(renderer_);

    if (!sceneTarget_) return;

    // Use base viewport dimensions for rendering
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};

    float pulse = 0.0f;
    if (gameplayView) {
        pulse += std::min(1.0f, screenFlashTimer_ * 8.0f);
        pulse += std::min(0.55f, muzzleFlashTimer_ * 7.0f);
        if (player_.isMeleeSwinging) pulse += 0.16f;
        if (player_.dead) pulse += 0.35f;
    }
    pulse = std::min(1.0f, pulse);

    SDL_SetTextureColorMod(sceneTarget_, 255, 255, 255);
    SDL_SetTextureAlphaMod(sceneTarget_, 255);
    SDL_SetTextureBlendMode(sceneTarget_, SDL_BLENDMODE_BLEND);
    if (gameplayView && config_.shaderCRT) {
        const int stripH = 2;  // Reduced for smoother curve at higher resolutions
        for (int y = 0; y < SCREEN_H; y += stripH) {
            int h = std::min(stripH, SCREEN_H - y);
            float yNorm = ((float)y + h * 0.5f) / (float)SCREEN_H;
            yNorm = yNorm * 2.0f - 1.0f;
            float curve = std::fabs(yNorm);
            // Slightly reduced curve for cleaner look
            int inset = (int)std::floor(3.0f + curve * curve * 14.0f);
            int dstW = std::max(1, SCREEN_W - inset * 2);
            SDL_Rect src = {0, y, SCREEN_W, h};
            SDL_Rect dst = {inset, y, dstW, h};
            SDL_RenderCopy(renderer_, sceneTarget_, &src, &dst);
        }
    } else {
        SDL_RenderCopy(renderer_, sceneTarget_, nullptr, &full);
    }

    if (gameplayView) {
        int shift = 1 + (int)std::floor(pulse * 3.0f);

        if (config_.shaderChromatic) {
            SDL_SetTextureBlendMode(sceneTarget_, SDL_BLENDMODE_ADD);

            // Reduced alpha for cleaner chromatic aberration
            SDL_SetTextureColorMod(sceneTarget_, 255, 70, 70);
            SDL_SetTextureAlphaMod(sceneTarget_, (Uint8)(20 + pulse * 35.0f));
            SDL_Rect redRect = { shift, 0, SCREEN_W, SCREEN_H };
            SDL_RenderCopy(renderer_, sceneTarget_, nullptr, &redRect);

            SDL_SetTextureColorMod(sceneTarget_, 70, 220, 255);
            SDL_SetTextureAlphaMod(sceneTarget_, (Uint8)(16 + pulse * 30.0f));
            SDL_Rect cyanRect = { -shift, 0, SCREEN_W, SCREEN_H };
            SDL_RenderCopy(renderer_, sceneTarget_, nullptr, &cyanRect);
        }

        if (config_.shaderGlow) {
            SDL_SetTextureBlendMode(sceneTarget_, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(sceneTarget_, 255, 180, 90);
            SDL_SetTextureAlphaMod(sceneTarget_, (Uint8)(12 + pulse * 32.0f));
            SDL_Rect glowRect = { 0, shift / 2, SCREEN_W, SCREEN_H };
            SDL_RenderCopy(renderer_, sceneTarget_, nullptr, &glowRect);
        }

        SDL_SetTextureBlendMode(sceneTarget_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(sceneTarget_, 255, 255, 255);
        SDL_SetTextureAlphaMod(sceneTarget_, 255);

        // Horizontal glitch strips when action intensity spikes
        if (config_.shaderGlitch && pulse > 0.20f) {
            int strips = 2 + (int)std::floor(pulse * 4.0f);
            for (int i = 0; i < strips; i++) {
                int y = (int)((gameTime_ * 43.0f + i * 71.0f)) % SCREEN_H;
                int h = 2 + (i % 3);
                int offs = ((i & 1) ? 1 : -1) * (1 + (int)std::floor(pulse * 5.0f));
                SDL_Rect src = {0, y, SCREEN_W, std::min(h, SCREEN_H - y)};
                SDL_Rect dst = {offs, y, SCREEN_W, std::min(h, SCREEN_H - y)};
                SDL_SetTextureBlendMode(sceneTarget_, SDL_BLENDMODE_ADD);
                SDL_SetTextureColorMod(sceneTarget_, 120, 255, 240);
                SDL_SetTextureAlphaMod(sceneTarget_, (Uint8)(18 + pulse * 28.0f));
                SDL_RenderCopy(renderer_, sceneTarget_, &src, &dst);
            }
            SDL_SetTextureBlendMode(sceneTarget_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(sceneTarget_, 255, 255, 255);
            SDL_SetTextureAlphaMod(sceneTarget_, 255);
        }

        // Scanlines
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        if (config_.shaderScanlines) {
            // Thinner scanlines for better quality
            for (int y = 0; y < SCREEN_H; y += 3) {
                Uint8 a = (Uint8)(16 + ((y / 3 + (int)(gameTime_ * 20.0f)) & 1) * 6 + pulse * 8.0f);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, a);
                SDL_RenderDrawLine(renderer_, 0, y, SCREEN_W, y);
            }
        }

        // Neon edge tint
        if (config_.shaderNeonEdge) {
            SDL_SetRenderDrawColor(renderer_, 20, 180, 200, (Uint8)(14 + pulse * 26.0f));
            SDL_Rect top = {0, 0, SCREEN_W, 6};
            SDL_Rect bottom = {0, SCREEN_H - 6, SCREEN_W, 6};
            SDL_RenderFillRect(renderer_, &top);
            SDL_RenderFillRect(renderer_, &bottom);
        }

        if (config_.shaderCRT) {
            // RGB phosphor mask - thinner stripes for better quality
            for (int x = 0; x < SCREEN_W; x += 2) {
                SDL_Color maskColor = {255, 70, 60, (Uint8)(6 + pulse * 5.0f)};
                if (x % 6 == 2) maskColor = {90, 255, 140, (Uint8)(6 + pulse * 5.0f)};
                else if (x % 6 == 4) maskColor = {90, 180, 255, (Uint8)(6 + pulse * 5.0f)};
                SDL_SetRenderDrawColor(renderer_, maskColor.r, maskColor.g, maskColor.b, maskColor.a);
                SDL_RenderDrawLine(renderer_, x, 0, x, SCREEN_H);
            }

            // Vignette border effect
            for (int i = 0; i < 10; ++i) {
                int inset = i * 2;
                Uint8 alpha = (Uint8)(8 + i * 5);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, alpha);
                SDL_Rect topBand = {inset, inset, std::max(1, SCREEN_W - inset * 2), 2};
                SDL_Rect bottomBand = {inset, SCREEN_H - inset - 2, std::max(1, SCREEN_W - inset * 2), 2};
                SDL_Rect leftBand = {inset, inset, 2, std::max(1, SCREEN_H - inset * 2)};
                SDL_Rect rightBand = {SCREEN_W - inset - 2, inset, 2, std::max(1, SCREEN_H - inset * 2)};
                SDL_RenderFillRect(renderer_, &topBand);
                SDL_RenderFillRect(renderer_, &bottomBand);
                SDL_RenderFillRect(renderer_, &leftBand);
                SDL_RenderFillRect(renderer_, &rightBand);
            }
        }
    }
}

SDL_GameController* Game::getPrimaryGameplayController() const {
    auto isTakenBySubPlayer = [this](SDL_JoystickID jid) {
        for (int s = 1; s < 4; s++) {
            if (coopSlots_[s].joined && coopSlots_[s].joyInstanceId == jid) return true;
        }
        return false;
    };

    if (lobbyPrimaryPadId_ >= 0 && !isTakenBySubPlayer(lobbyPrimaryPadId_)) {
        if (SDL_GameController* preferred = SDL_GameControllerFromInstanceID(lobbyPrimaryPadId_)) {
            return preferred;
        }
    }

    if (coopSlots_[0].joined && coopSlots_[0].joyInstanceId >= 0 && !isTakenBySubPlayer(coopSlots_[0].joyInstanceId)) {
        if (SDL_GameController* preferred = SDL_GameControllerFromInstanceID(coopSlots_[0].joyInstanceId)) {
            return preferred;
        }
    }

    if (lastGamepadInputId_ >= 0 && !isTakenBySubPlayer(lastGamepadInputId_)) {
        if (SDL_GameController* preferred = SDL_GameControllerFromInstanceID(lastGamepadInputId_)) {
            return preferred;
        }
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(i);
        if (isTakenBySubPlayer(jid)) continue;
        if (SDL_GameController* gc = SDL_GameControllerFromInstanceID(jid)) {
            return gc;
        }
    }

    return nullptr;
}

Vec2 Game::resolveAimDirection(const Player& player, const Vec2& aimInput) const {
    if (aimInput.lengthSq() > 0.04f) return aimInput.normalized();
    if (player.moving && player.vel.lengthSq() > 1.0f) return player.vel.normalized();
    return {};
}

void Game::renderAimCrosshair(const Camera& camera, const Player& player, Vec2 aimDir,
                              float distance, SDL_Color color, int size) {
    if (player.dead || aimDir.lengthSq() <= 0.01f) return;

    Vec2 chWorld = player.pos + aimDir.normalized() * distance;
    Vec2 chScreen = camera.worldToScreen(chWorld);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer_, (int)(chScreen.x - size), (int)chScreen.y,
                                  (int)(chScreen.x + size), (int)chScreen.y);
    SDL_RenderDrawLine(renderer_, (int)chScreen.x, (int)(chScreen.y - size),
                                  (int)chScreen.x, (int)(chScreen.y + size));
    SDL_Rect dot = {(int)chScreen.x - 1, (int)chScreen.y - 1, 3, 3};
    SDL_RenderFillRect(renderer_, &dot);
}

// Bypass SDL_RenderCopyExF entirely — compute corners on CPU and submit raw
// triangles via SDL_RenderGeometry.  The driver's CopyExF codepath is broken
// on some Linux OpenGL drivers (vertex collapse / UV corruption).
static void renderRotatedQuad(SDL_Renderer* renderer, SDL_Texture* tex,
                              float cx, float cy, float hw, float hh,
                              float angle,
                              SDL_RendererFlip flip = SDL_FLIP_NONE,
                              SDL_Color tint = {255,255,255,255})
{
    float s = std::sin(angle);
    float c = std::cos(angle);

    // UV edge coordinates (flipped as needed)
    float u0 = (flip & SDL_FLIP_HORIZONTAL) ? 1.0f : 0.0f;
    float u1 = 1.0f - u0;
    float v0 = (flip & SDL_FLIP_VERTICAL)   ? 1.0f : 0.0f;
    float v1 = 1.0f - v0;

    // Four corners (relative to center), rotated
    auto corner = [&](float ox, float oy, float u, float v) -> SDL_Vertex {
        SDL_Vertex vt;
        vt.position.x  = cx + ox * c - oy * s;
        vt.position.y  = cy + ox * s + oy * c;
        vt.color       = tint;
        vt.tex_coord.x = u;
        vt.tex_coord.y = v;
        return vt;
    };

    SDL_Vertex verts[4] = {
        corner(-hw, -hh, u0, v0), // TL
        corner(+hw, -hh, u1, v0), // TR
        corner(+hw, +hh, u1, v1), // BR
        corner(-hw, +hh, u0, v1), // BL
    };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(renderer, tex, verts, 4, idx, 6);
}

void Game::renderSprite(SDL_Texture* tex, Vec2 worldPos, float angle, float scale) {
    if (!tex) return;
    int w = 0, h = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0) return;
    // Guard: NaN/Inf in any input sends poison to the GPU, causing geometry explosion
    if (!std::isfinite(angle) || !std::isfinite(scale)) return;
    if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.y)) return;
    Vec2 sp = camera_.worldToScreen(worldPos);
    if (!std::isfinite(sp.x) || !std::isfinite(sp.y)) return;
    float fw = w * scale;
    float fh = h * scale;
    if (fw <= 0.0f || fh <= 0.0f) return;
    renderRotatedQuad(renderer_, tex, sp.x, sp.y, fw * 0.5f, fh * 0.5f, angle);
}

void Game::renderSpriteEx(SDL_Texture* tex, Vec2 worldPos, float angle, float scale, SDL_Color tint) {
    if (!tex) return;
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    renderSprite(tex, worldPos, angle, scale);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

SDL_Texture* Game::enemySpriteTex(EnemyType t) const {
    switch (t) {
        case EnemyType::Brute:   return bruteSprite_   ? bruteSprite_   : enemySprite_;
        case EnemyType::Scout:   return scoutSprite_   ? scoutSprite_   : enemySprite_;
        case EnemyType::Sniper:  return sniperSprite_  ? sniperSprite_  : shooterSprite_;
        case EnemyType::Gunner:  return gunnerSprite_  ? gunnerSprite_  : shooterSprite_;
        case EnemyType::Shooter: return shooterSprite_;
        case EnemyType::Melee:
        default:                 return enemySprite_;
    }
}

void Game::renderExplosionPixelated(const Explosion& ex) {
    Vec2 sp = camera_.worldToScreen(ex.pos);

    // Cull completely off-screen explosions (generous margin)
    float screenMargin = ex.radius + 32.0f;
    if (sp.x < -screenMargin || sp.x > SCREEN_W + screenMargin ||
        sp.y < -screenMargin || sp.y > SCREEN_H + screenMargin) return;

    float t = ex.duration > 0.0f ? (ex.timer / ex.duration) : 1.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    float visT = std::max(0.0f, std::min(1.0f, t * 1.28f + 0.02f));

    // Switch: larger pixels = 4x fewer SDL draw calls per explosion
#ifdef __SWITCH__
    const int pixel = 8;
#else
    const int pixel = 4;
#endif
    int maxRing = std::max(2, (int)(ex.radius / (float)pixel));
    int activeRing = std::max(1, (int)(maxRing * (0.30f + 0.70f * visT)));
    int innerRing = std::max(0, activeRing - (1 + (int)(3.0f * visT)));

    int alphaOuter = (int)(185.0f * (1.0f - visT));
    int alphaMid   = (int)(220.0f * (1.0f - 0.75f * visT));
    int alphaCore  = (int)(240.0f * (1.0f - 0.9f * visT));

    if (visT > 0.52f) {
        float fade = (visT - 0.52f) / 0.48f;
        alphaCore = (int)(alphaCore * std::max(0.0f, 1.0f - fade * 1.3f));
    }

    int seed = ((int)ex.pos.x * 31 + (int)ex.pos.y * 57) & 1023;
    int ringJitter = ((seed + (int)(t * 60.0f)) & 3) - 1;
    activeRing = std::max(1, activeRing + ringJitter);

    for (int gy = -activeRing; gy <= activeRing; gy++) {
        for (int gx = -activeRing; gx <= activeRing; gx++) {
            int d2 = gx * gx + gy * gy;
            int jitter = ((gx * 23 + gy * 11 + seed + (int)(t * 130.0f)) & 7) - 3;
            int boundary = activeRing + (jitter > 0 ? 1 : 0);
            if (d2 > boundary * boundary) continue;

            int noise = (gx * 17 + gy * 31 + (int)(t * 100.0f) + seed) & 7;
            if (noise == 0 && d2 > (activeRing * activeRing) / 3) continue;

            Uint8 r = 255, g = 150, b = 40;
            int a = alphaOuter;

            if (d2 <= innerRing * innerRing) {
                r = 255; g = 95; b = 25; a = alphaMid;
            }
            if (d2 <= (innerRing * innerRing) / 3) {
                r = 255; g = 235; b = 140; a = alphaCore;
            }

            if (a <= 0) continue;

            SDL_SetRenderDrawColor(renderer_, r, g, b, (Uint8)std::min(255, a));
            SDL_Rect px = {
                (int)sp.x + gx * pixel - pixel / 2,
                (int)sp.y + gy * pixel - pixel / 2,
                pixel,
                pixel
            };
            SDL_RenderFillRect(renderer_, &px);
        }
    }

    if (visT > 0.08f && visT < 0.62f) {
        int sparkCount = 2 + (((seed >> 3) + (int)(t * 20.0f)) & 3);
        SDL_SetRenderDrawColor(renderer_, 255, 210, 120, (Uint8)(120 * (1.0f - visT)));
        for (int i = 0; i < sparkCount; ++i) {
            int angSeed = seed + i * 37 + (int)(t * 140.0f);
            float a = (float)(angSeed % 360) * (float)M_PI / 180.0f;
            float d = (activeRing + 1 + (angSeed & 1)) * pixel;
            SDL_Rect s = {
                (int)(sp.x + cosf(a) * d) - 1,
                (int)(sp.y + sinf(a) * d) - 1,
                3,
                3
            };
            SDL_RenderFillRect(renderer_, &s);
        }
    }

    if (visT > 0.40f) {
        float p = (visT - 0.40f) / 0.60f;
        int smokeRing = activeRing + 1 + (int)(p * 2.5f);
        int smokeAlpha = (int)(70.0f * (1.0f - p));
        if (smokeAlpha > 0) {
            SDL_SetRenderDrawColor(renderer_, 55, 45, 40, (Uint8)smokeAlpha);
            for (int gy = -smokeRing; gy <= smokeRing; gy++) {
                for (int gx = -smokeRing; gx <= smokeRing; gx++) {
                    int d2 = gx * gx + gy * gy;
                    if (d2 < (smokeRing - 1) * (smokeRing - 1) || d2 > smokeRing * smokeRing) continue;
                    int noise = (gx * 13 + gy * 19 + (int)(t * 140.0f) + seed) & 3;
                    if (noise == 0) continue;
                    SDL_Rect px = {
                        (int)sp.x + gx * pixel - pixel / 2,
                        (int)sp.y + gy * pixel - pixel / 2,
                        pixel,
                        pixel
                    };
                    SDL_RenderFillRect(renderer_, &px);
                }
            }
        }
    }
}

void Game::renderMap() {
    // Only render visible tiles
    int startX = (int)(camera_.pos.x / TILE_SIZE) - 1;
    int startY = (int)(camera_.pos.y / TILE_SIZE) - 1;
    int endX   = startX + camera_.viewW / TILE_SIZE + 3;
    int endY   = startY + camera_.viewH / TILE_SIZE + 3;

    startX = std::max(0, startX);
    startY = std::max(0, startY);
    endX   = std::min(map_.width, endX);
    endY   = std::min(map_.height, endY);

    // Helper to check if a tile is gravel
    auto isGrv = [&](int tx, int ty) -> bool {
        if (!map_.isInBounds(tx, ty)) return false;
        return map_.get(tx, ty) == TILE_GRAVEL;
    };

    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            uint8_t tile = map_.get(x, y);
            if (map_.isSolid(x, y)) continue; // solids are drawn in renderWallOverlay()
            SDL_Texture* tex = nullptr;
            double tileAngle = 0.0;
            SDL_RendererFlip tileFlip = SDL_FLIP_NONE;

            // Deterministic hash for per-tile randomization
            unsigned int tileHash = (unsigned int)(x * 73856093u ^ y * 19349663u);

            if (tile == TILE_FLOOR) {
                // Hard floor — render directly with floorTex_, no gravel transitions
                tex = floorTex_;
                // Slight rotation variety for less uniformity
                if (tileHash & 0x1) tileFlip = SDL_FLIP_HORIZONTAL;
                if (tileHash & 0x2) tileFlip = (SDL_RendererFlip)(tileFlip | SDL_FLIP_VERTICAL);
            } else if (tile == TILE_WOOD) {
                tex = woodTex_;
                tileAngle = (tileHash % 2) * 90.0;
            } else if (tile == TILE_SAND) {
                tex = sandTex_;
                tileAngle = (tileHash % 4) * 90.0;
                if (tileHash & 0x100) tileFlip = SDL_FLIP_HORIZONTAL;
            } else if (tile == TILE_GRAVEL) {
                tex = gravelTex_;
                // Randomize gravel rotation and flip for variety
                tileAngle = (tileHash % 4) * 90.0;
                if (tileHash & 0x100) tileFlip = (SDL_RendererFlip)(tileFlip | SDL_FLIP_HORIZONTAL);
                if (tileHash & 0x200) tileFlip = (SDL_RendererFlip)(tileFlip | SDL_FLIP_VERTICAL);
            } else if (tile >= TILE_CUSTOM_0 && tile <= TILE_CUSTOM_7) {
                tex = customTileTextures_[tile - TILE_CUSTOM_0];
            } else {
                // Grass — use gravel transition sprites
                bool R = isGrv(x+1, y);
                bool L = isGrv(x-1, y);
                bool U = isGrv(x, y-1);
                bool D = isGrv(x, y+1);

                if (R || L || U || D) {
                    tex = gravelGrass3Tex_;
                    if (R)      tileAngle = 0;
                    else if (D) tileAngle = 90;
                    else if (L) tileAngle = 180;
                    else        tileAngle = 270;
                } else {
                    bool UR = isGrv(x+1, y-1);
                    bool DR = isGrv(x+1, y+1);
                    bool DL = isGrv(x-1, y+1);
                    bool UL = isGrv(x-1, y-1);
                    if (UR)      { tex = gravelGrass2Tex_; tileAngle = 0; }
                    else if (DR) { tex = gravelGrass2Tex_; tileAngle = 90; }
                    else if (DL) { tex = gravelGrass2Tex_; tileAngle = 180; }
                    else if (UL) { tex = gravelGrass2Tex_; tileAngle = 270; }
                    else         { tex = gravelGrass1Tex_; tileAngle = 0; }
                }
                // Randomize pure grass (gravelGrass1 = no gravel neighbor)
                if (tex == gravelGrass1Tex_) {
                    tileAngle = (tileHash % 4) * 90.0;
                    if (tileHash & 0x400) tileFlip = (SDL_RendererFlip)(tileFlip | SDL_FLIP_HORIZONTAL);
                    if (tileHash & 0x800) tileFlip = (SDL_RendererFlip)(tileFlip | SDL_FLIP_VERTICAL);
                }
            }

            Vec2 wp = {(float)(x * TILE_SIZE + TILE_SIZE/2),
                       (float)(y * TILE_SIZE + TILE_SIZE/2)};
            Vec2 sp = camera_.worldToScreen(wp);
            SDL_FRect dst = {sp.x - TILE_SIZE * 0.5f, sp.y - TILE_SIZE * 0.5f,
                             (float)TILE_SIZE, (float)TILE_SIZE};

            if (tex) {
                // Subtle color variation for grass/gravel to break repetition
                if (tile == TILE_GRASS || tile == TILE_GRAVEL) {
                    int variation = (int)(tileHash % 30) - 15; // -15 to +14
                    Uint8 mod = (Uint8)std::max(220, std::min(255, 240 + variation));
                    SDL_SetTextureColorMod(tex, mod, mod, mod);
                }
                renderRotatedQuad(renderer_, tex,
                    sp.x, sp.y,
                    TILE_SIZE * 0.5f, TILE_SIZE * 0.5f,
                    (float)(tileAngle * M_PI / 180.0),
                    tileFlip);
                if (tile == TILE_GRASS || tile == TILE_GRAVEL) {
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                }
            } else {
                SDL_Color c = {60, 60, 65, 255};
                if (tile == TILE_WALL) c = {100, 90, 80, 255};
                SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
                SDL_RenderFillRectF(renderer_, &dst);
            }
        }
    }
}

void Game::renderWallOverlay() {
    int startX = (int)(camera_.pos.x / TILE_SIZE) - 1;
    int startY = (int)(camera_.pos.y / TILE_SIZE) - 1;
    int endX   = startX + camera_.viewW / TILE_SIZE + 3;
    int endY   = startY + camera_.viewH / TILE_SIZE + 3;

    startX = std::max(0, startX);
    startY = std::max(0, startY);
    endX   = std::min(map_.width, endX);
    endY   = std::min(map_.height, endY);

    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            if (!map_.isSolid(x, y)) continue;

            uint8_t tile = map_.get(x, y);
            SDL_Texture* tex = nullptr;
            if (tile == TILE_WALL) tex = wallTex_;
            else if (tile == TILE_GLASS) tex = glassTex_;
            else if (tile == TILE_DESK) tex = deskTex_;
            else if (tile == TILE_BOX) tex = boxTex_;

            Vec2 wp = {(float)(x * TILE_SIZE + TILE_SIZE/2),
                       (float)(y * TILE_SIZE + TILE_SIZE/2)};
            Vec2 sp = camera_.worldToScreen(wp);
            SDL_Rect dst = {(int)(sp.x - TILE_SIZE/2), (int)(sp.y - TILE_SIZE/2),
                           TILE_SIZE, TILE_SIZE};

            if (tex) {
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            } else {
                SDL_SetRenderDrawColor(renderer_, 100, 90, 80, 255);
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
    }
}

void Game::renderDecals() {
    if (!bloodTex_) return;
    for (auto& bd : blood_) {
        Vec2 sp = camera_.worldToScreen(bd.pos);
        float half = 32.0f * bd.scale;
        if (!std::isfinite(bd.rotation) || !std::isfinite(half) || half <= 0.0f) continue;
        if (bd.type == DecalType::Scorch) {
            if (scorchTex_) {
                SDL_SetTextureAlphaMod(scorchTex_, 200);
                renderRotatedQuad(renderer_, scorchTex_, sp.x, sp.y, half, half, bd.rotation);
                SDL_SetTextureAlphaMod(scorchTex_, 255);
            } else {
                SDL_SetTextureColorMod(bloodTex_, 20, 20, 20);
                SDL_SetTextureAlphaMod(bloodTex_, 160);
                renderRotatedQuad(renderer_, bloodTex_, sp.x, sp.y, half, half, bd.rotation);
                SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
                SDL_SetTextureAlphaMod(bloodTex_, 255);
            }
        } else {
            SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
            SDL_SetTextureAlphaMod(bloodTex_, 180);
            renderRotatedQuad(renderer_, bloodTex_, sp.x, sp.y, half, half, bd.rotation);
            SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
            SDL_SetTextureAlphaMod(bloodTex_, 255);
        }
    }
}

void Game::renderRoofOverlay() {
    // Draw transparent glass ceiling tiles over rooms (rendered after entities)
    if (map_.ceiling.size() != (size_t)(map_.width * map_.height)) return; // safety: ceiling not sized
    int startX = (int)(camera_.pos.x / TILE_SIZE) - 1;
    int startY = (int)(camera_.pos.y / TILE_SIZE) - 1;
    int endX   = startX + camera_.viewW / TILE_SIZE + 3;
    int endY   = startY + camera_.viewH / TILE_SIZE + 3;

    startX = std::max(0, startX);
    startY = std::max(0, startY);
    endX   = std::min(map_.width, endX);
    endY   = std::min(map_.height, endY);

    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            if (map_.ceiling[y * map_.width + x] != CEIL_GLASS) continue;

            Vec2 wp = {(float)(x * TILE_SIZE + TILE_SIZE/2),
                       (float)(y * TILE_SIZE + TILE_SIZE/2)};
            Vec2 sp = camera_.worldToScreen(wp);
            SDL_Rect dst = {(int)(sp.x - TILE_SIZE/2), (int)(sp.y - TILE_SIZE/2),
                           TILE_SIZE, TILE_SIZE};

            if (glassTileTex_) {
                SDL_SetTextureAlphaMod(glassTileTex_, 100);
                SDL_RenderCopy(renderer_, glassTileTex_, nullptr, &dst);
                SDL_SetTextureAlphaMod(glassTileTex_, 255);
            } else {
                // Fallback: semi-transparent blue tint
                SDL_SetRenderDrawColor(renderer_, 140, 180, 220, 35);
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
    }
}

void Game::renderShadingPass() {
    // ── Wall shadow / ambient occlusion pass ──
    // For each visible tile adjacent to a wall, darken it slightly
    int startX = (int)(camera_.pos.x / TILE_SIZE) - 1;
    int startY = (int)(camera_.pos.y / TILE_SIZE) - 1;
    int endX   = startX + camera_.viewW / TILE_SIZE + 3;
    int endY   = startY + camera_.viewH / TILE_SIZE + 3;

    startX = std::max(0, startX);
    startY = std::max(0, startY);
    endX   = std::min(map_.width, endX);
    endY   = std::min(map_.height, endY);

    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            if (map_.isSolid(x, y)) continue; // don't shade solid tiles themselves

            // Count adjacent solid tiles for shadow intensity
            int adj = 0;
            if (map_.isSolid(x-1, y))   adj++;
            if (map_.isSolid(x+1, y))   adj++;
            if (map_.isSolid(x, y-1))   adj++;
            if (map_.isSolid(x, y+1))   adj++;
            if (map_.isSolid(x-1, y-1)) adj++;
            if (map_.isSolid(x+1, y-1)) adj++;
            if (map_.isSolid(x-1, y+1)) adj++;
            if (map_.isSolid(x+1, y+1)) adj++;

            if (adj > 0) {
                Vec2 wp = {(float)(x * TILE_SIZE + TILE_SIZE/2),
                           (float)(y * TILE_SIZE + TILE_SIZE/2)};
                Vec2 sp = camera_.worldToScreen(wp);
                int alpha = std::min(adj * 12, 60);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, (Uint8)alpha);
                SDL_Rect dst = {(int)(sp.x - TILE_SIZE/2), (int)(sp.y - TILE_SIZE/2),
                               TILE_SIZE, TILE_SIZE};
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
    }

    // ── Vignette overlay (proper radial) ──
    if (vignetteTex_) {
        // Use camera view dimensions so the vignette fills the viewport correctly
        // even when SDL_RenderSetScale is active during splitscreen.
        SDL_Rect full = {0, 0, (int)camera_.viewW, (int)camera_.viewH};
        SDL_RenderCopy(renderer_, vignetteTex_, nullptr, &full);
    }
}

void Game::invalidateMinimapCache() {
    minimapCacheDirty_ = true;
}

void Game::renderMinimap() {
    // ── Minimap ─ bottom-right corner ────────────────────────────────────────
    const int MMAP_MAX_PX = 160;  // maximum rendered dimension in pixels
    const int MMAP_MARGIN = 10;   // screen-edge margin
    const int MMAP_INNER  = 4;    // inner padding between border and tiles

    int mapW = map_.width;
    int mapH = map_.height;
    if (mapW <= 0 || mapH <= 0) return;

    // Scale: choose the largest integer pixels-per-tile that fits in MMAP_MAX_PX
    int tpx = std::max(1, std::min(MMAP_MAX_PX / mapW, MMAP_MAX_PX / mapH));
    int mmW = mapW * tpx;
    int mmH = mapH * tpx;

    // Position in bottom-right corner
    int mmX = SCREEN_W - MMAP_MARGIN - mmW;
    int mmY = SCREEN_H - MMAP_MARGIN - mmH;
    int texW = mmW + MMAP_INNER * 2 + 2;
    int texH = mmH + MMAP_INNER * 2 + 2;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    bool cacheMismatch = !minimapCacheTex_ || minimapCacheDirty_ ||
                         minimapCacheMapW_ != mapW || minimapCacheMapH_ != mapH ||
                         minimapCacheTilePx_ != tpx;
    if (cacheMismatch) {
        if (minimapCacheTex_) {
            SDL_DestroyTexture(minimapCacheTex_);
            minimapCacheTex_ = nullptr;
        }

        minimapCacheTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, texW, texH);
        if (minimapCacheTex_) {
            SDL_SetTextureBlendMode(minimapCacheTex_, SDL_BLENDMODE_BLEND);

            SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer_);
            SDL_SetRenderTarget(renderer_, minimapCacheTex_);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
            SDL_RenderClear(renderer_);

            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
            SDL_Rect bg = {1, 1, mmW + MMAP_INNER * 2, mmH + MMAP_INNER * 2};
            SDL_RenderFillRect(renderer_, &bg);

            SDL_SetRenderDrawColor(renderer_, 0, 200, 180, 100);
            SDL_Rect border = {0, 0, texW, texH};
            SDL_RenderDrawRect(renderer_, &border);

            for (int ty = 0; ty < mapH; ty++) {
                for (int tx = 0; tx < mapW; tx++) {
                    SDL_Rect r = {1 + MMAP_INNER + tx * tpx, 1 + MMAP_INNER + ty * tpx, tpx, tpx};
                    if (map_.isSolid(tx, ty))
                        SDL_SetRenderDrawColor(renderer_, 150, 140, 120, 220);
                    else
                        SDL_SetRenderDrawColor(renderer_, 28, 30, 35, 200);
                    SDL_RenderFillRect(renderer_, &r);
                }
            }

            SDL_SetRenderTarget(renderer_, prevTarget);
            minimapCacheDirty_ = false;
            minimapCacheMapW_ = mapW;
            minimapCacheMapH_ = mapH;
            minimapCacheTilePx_ = tpx;
        }
    }

    if (minimapCacheTex_) {
        SDL_Rect dst = {mmX - MMAP_INNER - 1, mmY - MMAP_INNER - 1, texW, texH};
        SDL_RenderCopy(renderer_, minimapCacheTex_, nullptr, &dst);
    } else {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
        SDL_Rect bg = {mmX - MMAP_INNER, mmY - MMAP_INNER,
                       mmW + MMAP_INNER*2, mmH + MMAP_INNER*2};
        SDL_RenderFillRect(renderer_, &bg);

        SDL_SetRenderDrawColor(renderer_, 0, 200, 180, 100);
        SDL_Rect border = {mmX - MMAP_INNER - 1, mmY - MMAP_INNER - 1,
                           mmW + MMAP_INNER*2 + 2, mmH + MMAP_INNER*2 + 2};
        SDL_RenderDrawRect(renderer_, &border);

        for (int ty = 0; ty < mapH; ty++) {
            for (int tx = 0; tx < mapW; tx++) {
                SDL_Rect r = {mmX + tx * tpx, mmY + ty * tpx, tpx, tpx};
                if (map_.isSolid(tx, ty))
                    SDL_SetRenderDrawColor(renderer_, 150, 140, 120, 220);
                else
                    SDL_SetRenderDrawColor(renderer_, 28, 30, 35, 200);
                SDL_RenderFillRect(renderer_, &r);
            }
        }
    }

    float worldW = map_.worldWidth();
    float worldH = map_.worldHeight();

    // Camera viewport rectangle
    {
        int vx = mmX + (int)(camera_.pos.x / worldW * mmW);
        int vy = mmY + (int)(camera_.pos.y / worldH * mmH);
        int vw = (int)((float)SCREEN_W / worldW * mmW);
        int vh = (int)((float)SCREEN_H / worldH * mmH);
        vx = std::max(mmX, std::min(mmX + mmW - 1, vx));
        vy = std::max(mmY, std::min(mmY + mmH - 1, vy));
        vw = std::min(vw, mmX + mmW - vx);
        vh = std::min(vh, mmY + mmH - vy);
        if (vw > 0 && vh > 0) {
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 25);
            SDL_Rect vr = {vx, vy, vw, vh};
            SDL_RenderFillRect(renderer_, &vr);
        }
    }

    // Team colors (indices 0-3: red, blue, green, orange)
    static const SDL_Color kTeamColors[4] = {
        {255, 80,  80,  255}, {80,  80,  255, 255},
        {80,  220, 80,  255}, {255, 180, 60,  255}
    };
    auto blipColor = [&](int8_t team) -> SDL_Color {
        if (team >= 0 && team < 4) return kTeamColors[(int)team];
        return {255, 60, 60, 230};
    };

    // Helper to clamp+draw a blip
    auto drawBlip = [&](float wx, float wy, int halfSz, SDL_Color c) {
        int bx = mmX + (int)(wx / worldW * mmW);
        int by = mmY + (int)(wy / worldH * mmH);
        bx = std::max(mmX, std::min(mmX + mmW - 1, bx));
        by = std::max(mmY, std::min(mmY + mmH - 1, by));
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_Rect r = {bx - halfSz, by - halfSz, halfSz*2+1, halfSz*2+1};
        SDL_RenderFillRect(renderer_, &r);
    };

    // Upgrade crates (yellow diamonds – just squares for simplicity)
    for (auto& c : crates_) {
        if (!c.alive) continue;
        drawBlip(c.pos.x, c.pos.y, 2, {255, 220, 40, 230});
    }
    // Floating pickups (slightly dimmer yellow)
    for (auto& p : pickups_) {
        if (!p.alive) continue;
        drawBlip(p.pos.x, p.pos.y, 1, {255, 200, 60, 180});
    }

    // Enemy blips (red / team-colored for PVE AI)
    for (auto& e : enemies_) {
        if (!e.alive) continue;
        drawBlip(e.pos.x, e.pos.y, 1, {255, 60, 60, 230});
    }

    // Remote player blips
    // • Teammate  — team-colored, normal size (5×5)
    // • Enemy player — bright red/team-color, larger (7×7) + white outline
    {
        auto& net = NetworkManager::instance();
        if (net.isOnline()) {
            uint8_t localId = net.localPlayerId();
            for (auto& rp : net.players()) {
                if (rp.id == localId) continue;
                if (!rp.alive) continue;

                bool isEnemy = (localTeam_ < 0)              // FFA — all others are enemies
                               || (rp.team < 0)              // they have no team
                               || (rp.team != localTeam_);   // different team

                if (isEnemy) {
                    // Large blip (7×7) in the enemy's team color (or red if no team)
                    SDL_Color col = blipColor(rp.team);
                    drawBlip(rp.pos.x, rp.pos.y, 3, col);
                    // White outline one pixel outside the blip
                    int bx = mmX + (int)(rp.pos.x / worldW * mmW);
                    int by = mmY + (int)(rp.pos.y / worldH * mmH);
                    bx = std::max(mmX, std::min(mmX + mmW - 1, bx));
                    by = std::max(mmY, std::min(mmY + mmH - 1, by));
                    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 200);
                    SDL_Rect outline = {bx - 4, by - 4, 9, 9};
                    SDL_RenderDrawRect(renderer_, &outline);
                } else {
                    // Teammate — small blip in their team color
                    drawBlip(rp.pos.x, rp.pos.y, 2, blipColor(rp.team));
                }
            }
        }
    }

    // Local player blip (cyan, drawn on top of everything)
    drawBlip(player_.pos.x, player_.pos.y, 2, {0, 255, 228, 255});
}

void Game::renderUI() {
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color cyan  = {0, 255, 228, 255};
    (void)0; // red removed — used inline where needed

    // Health bar (pipe characters like Unity version)
    {
        char hpStr[32];
        memset(hpStr, 0, sizeof(hpStr));
        for (int i = 0; i < player_.hp && i < 20; i++) hpStr[i] = '|';
        SDL_Color hpColor = (player_.hp <= 1) ?
            SDL_Color{255, 50, 50, 255} : cyan;
        drawText(hpStr, 20, 20, 24, hpColor);
    }

    // Weapon indicator (y=52)
    {
        const char* wpnNames[] = { "GUN", "AXE" };
        int nw = 2;
        for (int i = 0; i < nw; i++) {
            bool active = (i == player_.activeWeapon);
            SDL_Color c = active ? SDL_Color{255, 220, 60, 255} : SDL_Color{120, 120, 120, 180};
            int fw = active ? 20 : 16;
            drawText(wpnNames[i], 20 + i * 60, 52, fw, c);
        }
    }

    // Ammo display — shown below weapon label when gun is active (y=76)
    if (player_.activeWeapon == 0) {
        char ammoStr[32];
        if (player_.reloading) {
            snprintf(ammoStr, sizeof(ammoStr), "RELOAD");
        } else {
            snprintf(ammoStr, sizeof(ammoStr), "%d/%d", player_.ammo, player_.maxAmmo);
        }
        drawText(ammoStr, 20, 76, 18, white);
    }

    // Bomb count: orbiting (ready-to-launch) + reserve
    {
        int orbitingBombs = 0;
        for (auto& b : bombs_) if (b.alive && !b.hasDashed) orbitingBombs++;
        int totalBombs = orbitingBombs + player_.bombCount;
        if (totalBombs > 0) {
            char bombStr[32];
            snprintf(bombStr, sizeof(bombStr), "BOMBS: %d", totalBombs);
            drawText(bombStr, 20, 100, 16, {255, 180, 50, 255});
        }
    }

    // Timer — in multiplayer move to center-top to avoid overlapping stats
    {
        char timeStr[32];
        int mins = (int)gameTime_ / 60;
        int secs = (int)gameTime_ % 60;
        snprintf(timeStr, sizeof(timeStr), "%d:%02d", mins, secs);
        if (NetworkManager::instance().isOnline()) {
            drawTextCentered(timeStr, 8, 18, {200, 200, 200, 180});
        } else {
            drawText(timeStr, SCREEN_W - 100, 20, 20, white);
        }
    }

    // FPS
    {
        char fpsStr[32];
        int fps = (dt_ > 0.0001f) ? (int)(1.0f / dt_) : 0;
        snprintf(fpsStr, sizeof(fpsStr), "FPS: %d", fps);
        drawText(fpsStr, SCREEN_W - 120, 52, 14, {180, 180, 180, 255});
    }

    // Kill counter for bomb progress
    if (player_.killCounter > 0) {
        char killStr[32];
        snprintf(killStr, sizeof(killStr), "Kills: %d/%d", player_.killCounter, upgrades_.killsPerBomb);
        drawText(killStr, 20, 120, 14, {180, 180, 180, 255});
    }

    // Minimap
    renderMinimap();

    // Parry cooldown indicator
    if (!player_.canParry) {
        // drawText("PARRY CD", SCREEN_W/2 - 40, SCREEN_H - 50, 14, {150, 150, 150, 255});
    }

    // Hit vignette (red overlay when damaged)
    if (player_.invulnerable) {
        float alpha = player_.invulnTimer / PLAYER_INVULN_TIME * 0.3f;
        SDL_SetRenderDrawColor(renderer_, 255, 0, 0, (Uint8)(alpha * 255));
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(renderer_, &full);
    }

    // ── Visual polish: screen flash (explosions, etc.) ──
    if (screenFlashTimer_ > 0) {
        float a = (screenFlashTimer_ / 0.12f) * 0.35f;
        SDL_SetRenderDrawColor(renderer_, (Uint8)screenFlashR_, (Uint8)screenFlashG_,
                               (Uint8)screenFlashB_, (Uint8)(a * 255));
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(renderer_, &full);
    }

    // ── Visual polish: muzzle flash glow ──
    if (muzzleFlashTimer_ > 0) {
        Vec2 sp = camera_.worldToScreen(muzzleFlashPos_);
        float flashAlpha = muzzleFlashTimer_ / 0.06f;
        int flashSize = (int)(8 + flashAlpha * 6);
        SDL_SetRenderDrawColor(renderer_, 255, 240, 150, (Uint8)(flashAlpha * 160));
        SDL_Rect fd = {(int)sp.x - flashSize/2, (int)sp.y - flashSize/2, flashSize, flashSize};
        SDL_RenderFillRect(renderer_, &fd);
    }

    // ── Pickup popup banner (name + description) ──
    if (pickupPopupTimer_ > 0) {
        float t = pickupPopupTimer_;

        // Fade in / slide-hold / fade out — mirror wave banner timing
        float alpha = 1.0f;
        if (t > 2.0f) alpha = (2.5f - t) * 2.0f;   // 0.5s fade in
        else if (t < 0.5f) alpha = t * 2.0f;          // 0.5s fade out
        alpha = fminf(1.0f, fmaxf(0.0f, alpha));

        // Position slightly below centre so it doesn't clash with wave banner
        int bannerY = SCREEN_H / 2 + 55;

        // Background
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, (Uint8)(alpha * 170));
        SDL_Rect banner = {0, bannerY, SCREEN_W, 58};
        SDL_RenderFillRect(renderer_, &banner);

        // Coloured accent lines using the pickup's own colour
        SDL_Color ac = pickupPopupColor_;
        SDL_SetRenderDrawColor(renderer_, ac.r, ac.g, ac.b, (Uint8)(alpha * 220));
        SDL_Rect topLine = {SCREEN_W / 5, bannerY, SCREEN_W * 3 / 5, 2};
        SDL_RenderFillRect(renderer_, &topLine);
        SDL_Rect botLine = {SCREEN_W / 5, bannerY + 56, SCREEN_W * 3 / 5, 2};
        SDL_RenderFillRect(renderer_, &botLine);

        // Upgrade name (large)
        SDL_Color nameCol = {ac.r, ac.g, ac.b, (Uint8)(alpha * 255)};
        drawTextCentered(pickupPopupName_.c_str(), bannerY + 6, 26, nameCol);

        // Description (smaller, white)
        SDL_Color descCol = {220, 220, 220, (Uint8)(alpha * 220)};
        drawTextCentered(pickupPopupDesc_.c_str(), bannerY + 36, 15, descCol);
    }

    // ── Visual polish: wave announcement banner ──
    if (waveAnnounceTimer_ > 0) {
        float t = waveAnnounceTimer_;

        // Fade in/out
        float alpha = 1.0f;
        if (t > 2.0f) alpha = (2.5f - t) * 2.0f;      // fade in first 0.5s
        else if (t < 0.5f) alpha = t * 2.0f;             // fade out last 0.5s
        alpha = fminf(1.0f, fmaxf(0.0f, alpha));

        // Dark banner background
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, (Uint8)(alpha * 180));
        SDL_Rect banner = {0, SCREEN_H/2 - 40, SCREEN_W, 80};
        SDL_RenderFillRect(renderer_, &banner);

        // Accent line
        SDL_SetRenderDrawColor(renderer_, 0, 255, 228, (Uint8)(alpha * 255));
        SDL_Rect line = {SCREEN_W/4, SCREEN_H/2 - 40, SCREEN_W/2, 2};
        SDL_RenderFillRect(renderer_, &line);
        SDL_Rect line2 = {SCREEN_W/4, SCREEN_H/2 + 38, SCREEN_W/2, 2};
        SDL_RenderFillRect(renderer_, &line2);

        // Text
        char waveTxt[64];
        snprintf(waveTxt, sizeof(waveTxt), "WAVE %d", waveAnnounceNum_);
        SDL_Color waveCol = {0, 255, 228, (Uint8)(alpha * 255)};
        drawTextCentered(waveTxt, SCREEN_H/2 - 20, 36, waveCol);
    }

    // ── Supply drop popup (crate spawned) ──
    if (cratePopupTimer_ > 0) {
        float t = cratePopupTimer_;

        float alpha = 1.0f;
        if (t > 2.0f) alpha = (2.5f - t) * 2.0f;
        else if (t < 0.5f) alpha = t * 2.0f;
        alpha = fminf(1.0f, fmaxf(0.0f, alpha));

        // Small banner just below the wave banner area
        int popY = SCREEN_H/2 + 50;
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, (Uint8)(alpha * 160));
        SDL_Rect bg = {SCREEN_W/3, popY, SCREEN_W/3, 40};
        SDL_RenderFillRect(renderer_, &bg);
        // Orange accent lines
        SDL_SetRenderDrawColor(renderer_, 255, 180, 30, (Uint8)(alpha * 220));
        SDL_Rect tl = {SCREEN_W/3, popY, SCREEN_W/3, 2};
        SDL_RenderFillRect(renderer_, &tl);
        SDL_Rect bl = {SCREEN_W/3, popY + 38, SCREEN_W/3, 2};
        SDL_RenderFillRect(renderer_, &bl);
        // Text
        SDL_Color dropCol = {255, 200, 60, (Uint8)(alpha * 255)};
        drawTextCentered("SUPPLY DROP", popY + 8, 20, dropCol);
    }

    bool drewGamepadCrosshair = false;
    if (SDL_GameController* gc = getPrimaryGameplayController()) {
        (void)gc;
#ifdef __SWITCH__
        bool wantsGamepadCrosshair = true;
#else
        bool wantsGamepadCrosshair = usingGamepad_;
#endif
        if (wantsGamepadCrosshair) {
            Vec2 aimDir = resolveAimDirection(player_, aimInput_);
            if (aimDir.lengthSq() > 0.01f) {
                renderAimCrosshair(camera_, player_, aimDir, 96.0f, {0, 255, 228, 200}, 12);
                drewGamepadCrosshair = true;
            }
        }
    }

#ifndef __SWITCH__
    if (!drewGamepadCrosshair) {
        // Gameplay crosshair (same style as editor cursor, but slightly smaller)
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        const int sz = 12;
        SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 200);
        SDL_RenderDrawLine(renderer_, mx - sz, my, mx + sz, my);
        SDL_RenderDrawLine(renderer_, mx, my - sz, mx, my + sz);
        SDL_Rect dot = {mx - 1, my - 1, 3, 3};
        SDL_RenderFillRect(renderer_, &dot);
    }
#endif
}

void Game::renderMainMenu() {
    // Background
    if (mainmenuBg_) {
        SDL_Rect dst = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(renderer_, mainmenuBg_, nullptr, &dst);
    }

    // Dark overlay
    ui_.drawDarkOverlay(160, 0, 0, 0);

    // Title
    ui_.drawTextCentered("COLD START", SCREEN_H / 8, 52, UI::Color::Cyan);

    // Version tag
    {
        char verBuf[32];
        snprintf(verBuf, sizeof(verBuf), "v%s", GAME_VERSION);
        ui_.drawTextCentered(verBuf, SCREEN_H / 8 + 58, 14, {0, 180, 160, 220});
    }

    // Subtitle separator
    ui_.drawSeparator(SCREEN_W / 2, SCREEN_H / 8 + 76, 120, {0, 180, 160, 80});

    // Menu items
    struct Item { const char* label; SDL_Color accent; bool enabled; };
#ifdef __SWITCH__
    Item items[] = {
        {"PLAY",             UI::Color::Green,  true},   // 0
        {"MULTIPLAYER",      UI::Color::Blue,   true},   // 1
        {"EDITOR",           UI::Color::Yellow, true},   // 2
        {"MAPS",             UI::Color::White,  true},   // 3
        {"PACKS",            UI::Color::White,  true},   // 4
        {"CHARACTER",        UI::Color::White,  true},   // 5
        {"CHARACTER EDITOR", UI::Color::White,  true},   // 6
        {"MODS",             UI::Color::Purple, true},   // 7
        {"CONFIG",           UI::Color::White,  true},   // 8
        {"QUIT",             UI::Color::Red,    true},   // 9
    };
    constexpr int count = 10;
#else
    Item items[] = {
        {"PLAY",             UI::Color::Green,  true},   // 0
        {"MULTIPLAYER",      UI::Color::Blue,   true},   // 1
        {"EDITOR",           UI::Color::Yellow, true},   // 2
        {"MAPS",             UI::Color::White,  true},   // 3
        {"PACKS",            UI::Color::White,  true},   // 4
        {"CHARACTER",        UI::Color::White,  true},   // 5
        {"CHARACTER EDITOR", UI::Color::White,  true},   // 6
        {"MODS",             UI::Color::Purple, true},   // 7
        {"CONFIG",           UI::Color::White,  true},   // 8
        {"UPDATE",           updateAvailable_ ? UI::Color::Green : UI::Color::DimCyan, updateAvailable_},  // 9
        {"QUIT",             UI::Color::Red,    true},   // 10
    };
    constexpr int count = 11;
#endif
    int baseY = SCREEN_H / 4 + 10;
    int stepY = 32;
    int itemW = 320;
    int itemH = 30;

    for (int i = 0; i < count; i++) {
        bool sel = (menuSelection_ == i);
        
        // Dim UPDATE button if no update available
        SDL_Color color = items[i].accent;
        if (!items[i].enabled) {
            color = {80, 80, 90, 255};  // dim gray
        }

        // Interactive menu item — handles hover, click, animation
        if (ui_.menuItem(i, items[i].label, SCREEN_W / 2, baseY + i * stepY,
                         itemW, itemH, color, sel, 20, 24)) {
            // Clicked — update selection and trigger confirm
            if (items[i].enabled) {
                menuSelection_ = i;
                confirmInput_ = true;
                if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
            }
        }

        // Update selection when mouse hovers (so keyboard and mouse stay in sync)
        if (ui_.hoveredItem == i && !usingGamepad_ && items[i].enabled) {
            menuSelection_ = i;
        }

        // Group separators
        if (i == 1 || i == 2 || i == 6 || i == 7 || i == 8) {
            ui_.drawSeparator(SCREEN_W / 2, baseY + i * stepY + 28, 80, {60, 60, 80, 60});
        }
    }
    
    // Show update notification if available
#ifndef __SWITCH__
    if (updateAvailable_ && !latestVersion_.empty()) {
        char updateMsg[64];
        snprintf(updateMsg, sizeof(updateMsg), "New version available: v%s", latestVersion_.c_str());
        ui_.drawTextCentered(updateMsg, baseY + 9 * stepY - 18, 12, UI::Color::Green);
    }
#endif

    // Selected character name
    if (selectedChar_ >= 0 && selectedChar_ < (int)availableChars_.size()) {
        char charStr[128];
        snprintf(charStr, sizeof(charStr), "Character: %s", availableChars_[selectedChar_].name.c_str());
        ui_.drawTextCentered(charStr, SCREEN_H - 72, 14, UI::Color::DimCyan);
    }

    // Hint bar with automatic glyphs
    UI::HintPair hints[] = {
        {UI::Action::Confirm, "Select"},
        {UI::Action::Navigate, "Navigate"},
    };
    ui_.drawHintBar(hints, 2);
}

void Game::renderPlayModeMenu() {
    ui_.drawDarkOverlay(235, 8, 8, 12);

    ui_.drawTextCentered("PLAY", 50, 36, UI::Color::Cyan);
    ui_.drawSeparator(SCREEN_W / 2, 92, 80);

    int y     = 120;
    int stepY = 46;
    int rowW  = 440;
    int rowH  = 40;

    // ── Mode buttons (0=Generated, 1=Map, 2=Pack) ──
    struct ModeBtn { int idx; const char* label; SDL_Color color; };
    ModeBtn modes[] = {
        {0, "GENERATED MAP", UI::Color::Green},
        {1, "MAP",           UI::Color::Cyan},
        {2, "PACK",          UI::Color::Cyan},
    };
    for (auto& m : modes) {
        bool sel = (playModeSelection_ == m.idx);
        if (ui_.menuItem(m.idx, m.label, SCREEN_W / 2, y, rowW, rowH, m.color, sel, 20, 24)) {
            playModeSelection_ = m.idx;
            menuSelection_     = m.idx;
            confirmInput_      = true;
        }
        if (ui_.hoveredItem == m.idx && !usingGamepad_) {
            playModeSelection_ = m.idx;
            menuSelection_     = m.idx;
        }
        y += stepY;
    }

    // ── Divider + section label ──
    y += 8;
    ui_.drawTextCentered("SETTINGS", y + 4, 13, UI::Color::HintGray);
    y += 28;
    ui_.drawSeparator(SCREEN_W / 2, y, 60);
    y += 24;

    // ── Slider rows (3-8): map size + difficulty ──
    char valBuf[128];
    struct PMRow { const char* label; int idx; };
    PMRow rows[] = {
        {"Map Width:",      3}, {"Map Height:",      4},
        {"Player HP:",      5}, {"Enemy Spawnrate:", 6},
        {"Enemy HP:",       7}, {"Enemy Speed:",     8},
    };

    auto fmtVal = [&](int idx) -> const char* {
        switch (idx) {
            case 3: snprintf(valBuf, sizeof(valBuf), "%d",    config_.mapWidth);        break;
            case 4: snprintf(valBuf, sizeof(valBuf), "%d",    config_.mapHeight);       break;
            case 5: snprintf(valBuf, sizeof(valBuf), "%d",    config_.playerMaxHp);     break;
            case 6: snprintf(valBuf, sizeof(valBuf), "%.1fx", config_.spawnRateScale);  break;
            case 7: snprintf(valBuf, sizeof(valBuf), "%.1fx", config_.enemyHpScale);    break;
            case 8: snprintf(valBuf, sizeof(valBuf), "%.1fx", config_.enemySpeedScale); break;
            default: valBuf[0] = 0; break;
        }
        return valBuf;
    };

    int sliderH = 36;
    for (auto& row : rows) {
        bool sel = (playModeSelection_ == row.idx);
        int delta = ui_.sliderRow(row.idx, row.label, fmtVal(row.idx),
                                  SCREEN_W / 2, y, rowW, sliderH,
                                  UI::Color::Cyan, sel, leftInput_, rightInput_);
        if (ui_.hoveredItem == row.idx && !usingGamepad_) {
            playModeSelection_ = row.idx;
            menuSelection_     = row.idx;
        }
        if (delta != 0 && ui_.hoveredItem == row.idx) {
            switch (row.idx) {
                case 3: config_.mapWidth        = std::max(20,   std::min(120,  config_.mapWidth        + delta * 2));    break;
                case 4: config_.mapHeight       = std::max(14,   std::min(80,   config_.mapHeight       + delta * 2));    break;
                case 5: config_.playerMaxHp     = std::max(1,    std::min(20,   config_.playerMaxHp     + delta));        break;
                case 6: config_.spawnRateScale  = std::max(0.3f, std::min(3.0f, config_.spawnRateScale  + delta * 0.1f)); break;
                case 7: config_.enemyHpScale    = std::max(0.3f, std::min(3.0f, config_.enemyHpScale    + delta * 0.1f)); break;
                case 8: config_.enemySpeedScale = std::max(0.5f, std::min(2.5f, config_.enemySpeedScale + delta * 0.1f)); break;
            }
        }
        y += stepY;
    }

    // ── Back button (9) ──
    {
        bool sel = (playModeSelection_ == 9);
        if (ui_.menuItem(9, "BACK", SCREEN_W / 2, y + 4, rowW, sliderH,
                         UI::Color::White, sel, 20, 22)) {
            playModeSelection_ = 9;
            menuSelection_     = 9;
            confirmInput_      = true;
        }
        if (ui_.hoveredItem == 9 && !usingGamepad_) {
            playModeSelection_ = 9;
            menuSelection_     = 9;
        }
    }

    // Hint bar
    UI::HintPair hints[] = {
        {UI::Action::Confirm,  "Select"},
        {UI::Action::Navigate, "Navigate"},
        {UI::Action::Left,     "Decrease"},
        {UI::Action::Right,    "Increase"},
        {UI::Action::Back,     "Back"},
    };
    ui_.drawHintBar(hints, 5);
}

void Game::renderConfigMenu() {
    // Background
    ui_.drawDarkOverlay(235, 8, 8, 12);

    // Title
    ui_.drawTextCentered("CONFIG", 50, 36, UI::Color::Cyan);
    ui_.drawSeparator(SCREEN_W / 2, 92, 80);

    char valBuf[128];
    int y = 116;
    int stepY = 34;
    int rowW = 460;
    int rowH = 28;

    // Slider rows (idx 0-5): label + adjustable value with mouse/keyboard
    struct ConfigRow { const char* label; int idx; };
    ConfigRow rows[] = {
        {"Player HP:", 0}, {"Enemy Spawnrate:", 1}, {"Enemy HP:", 2},
        {"Enemy Speed:", 3}, {"Music Volume:", 4}, {"SFX Volume:", 5},
#ifndef __SWITCH__
        {"Resolution:", CONFIG_RESOLUTION_INDEX},
#endif
    };

    // Values for display
    auto formatVal = [&](int idx) -> const char* {
        switch (idx) {
            case 0: snprintf(valBuf, sizeof(valBuf), "%d",   config_.playerMaxHp); break;
            case 1: snprintf(valBuf, sizeof(valBuf), "%.1fx",config_.spawnRateScale); break;
            case 2: snprintf(valBuf, sizeof(valBuf), "%.1fx",config_.enemyHpScale); break;
            case 3: snprintf(valBuf, sizeof(valBuf), "%.1fx",config_.enemySpeedScale); break;
            case 4: snprintf(valBuf, sizeof(valBuf), "%d%%",  config_.musicVolume * 100 / 128); break;
            case 5: snprintf(valBuf, sizeof(valBuf), "%d%%",  config_.sfxVolume   * 100 / 128); break;
            case CONFIG_RESOLUTION_INDEX: snprintf(valBuf, sizeof(valBuf), "%dx%d", config_.windowWidth, config_.windowHeight); break;
            default: valBuf[0] = 0; break;
        }
        return valBuf;
    };

    for (auto& row : rows) {
        bool sel = (configSelection_ == row.idx);
        int delta = ui_.sliderRow(row.idx, row.label, formatVal(row.idx),
                                  SCREEN_W / 2, y, rowW, rowH,
                                  UI::Color::Cyan, sel, leftInput_, rightInput_);
        // Mouse hover syncs config selection
        if (ui_.hoveredItem == row.idx && !usingGamepad_) {
            configSelection_ = row.idx;
            menuSelection_ = row.idx;
        }
        // Handle mouse-click delta on slider (adjust value via click)
        if (delta != 0 && ui_.hoveredItem == row.idx) {
            switch (row.idx) {
                case 0: config_.playerMaxHp    = std::max(1,    std::min(20,   config_.playerMaxHp    + delta));       break;
                case 1: config_.spawnRateScale = std::max(0.3f, std::min(3.0f, config_.spawnRateScale + delta * 0.1f)); break;
                case 2: config_.enemyHpScale   = std::max(0.3f, std::min(3.0f, config_.enemyHpScale   + delta * 0.1f)); break;
                case 3: config_.enemySpeedScale= std::max(0.5f, std::min(2.5f, config_.enemySpeedScale+ delta * 0.1f)); break;
                case 4: config_.musicVolume    = std::max(0,    std::min(128,  config_.musicVolume     + delta * 8));    Mix_VolumeMusic(config_.musicVolume); break;
                case 5: config_.sfxVolume      = std::max(0,    std::min(128,  config_.sfxVolume       + delta * 8));    break;
#ifndef __SWITCH__
                case CONFIG_RESOLUTION_INDEX: {
                    int idx = findResolutionPresetIndex(config_.windowWidth, config_.windowHeight);
                    idx = std::max(0, std::min((int)(sizeof(kResolutionPresets) / sizeof(kResolutionPresets[0])) - 1, idx + delta));
                    config_.windowWidth = kResolutionPresets[idx].w;
                    config_.windowHeight = kResolutionPresets[idx].h;
                    applyResolutionSettings(true);
                } break;
#endif
            }
        }
        y += stepY;
    }

    struct ToggleRow {
        const char* label;
        int idx;
        bool* value;
    };
    ToggleRow toggleRows[] = {
        {"CRT Filter:", CONFIG_SHADER_CRT_INDEX, &config_.shaderCRT},
        {"Chromatic Aberration:", CONFIG_SHADER_CHROMATIC_INDEX, &config_.shaderChromatic},
        {"Scanlines:", CONFIG_SHADER_SCANLINES_INDEX, &config_.shaderScanlines},
        {"Glow Pass:", CONFIG_SHADER_GLOW_INDEX, &config_.shaderGlow},
        {"Glitch Strips:", CONFIG_SHADER_GLITCH_INDEX, &config_.shaderGlitch},
        {"Neon Edge:", CONFIG_SHADER_NEON_INDEX, &config_.shaderNeonEdge},
        {"Save Incoming Mods:", CONFIG_SAVE_INCOMING_MODS_INDEX, &config_.saveIncomingModsPermanently},
    };

    for (auto& row : toggleRows) {
        bool sel = (configSelection_ == row.idx);
        char tbuf[128];
        snprintf(tbuf, sizeof(tbuf), "%s %s", row.label, *row.value ? "ON" : "OFF");

        SDL_Color accent = *row.value ? UI::Color::Cyan : UI::Color::White;
        if (ui_.menuItem(row.idx, tbuf, SCREEN_W / 2, y, rowW, rowH, accent, sel, 18, 20)) {
            configSelection_ = row.idx;
            menuSelection_ = row.idx;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == row.idx && !usingGamepad_) {
            configSelection_ = row.idx;
            menuSelection_ = row.idx;
        }
        if (sel) {
            char hint[72];
            snprintf(hint, sizeof(hint), "[%s/%s toggle]",
                     UI::glyphLabel(UI::Action::Confirm, usingGamepad_),
                     UI::glyphLabel(UI::Action::Right, usingGamepad_));
            ui_.drawText(hint, SCREEN_W / 2 + 160, y + 4, 10, UI::Color::HintGray);
        }
        y += stepY;
    }

    // Username field
    {
        bool sel = (configSelection_ == CONFIG_USERNAME_INDEX);
        std::string uDisplay = config_.username;
        if (usernameTyping_) {
            static float blinkT = 0; blinkT += dt_;
            uDisplay += ((int)(blinkT * 3.0f) % 2 == 0) ? '_' : ' ';
        }
        char ubuf[128];
        snprintf(ubuf, sizeof(ubuf), "Username:  %s", uDisplay.c_str());

        SDL_Color accent = usernameTyping_ ? UI::Color::Cyan : UI::Color::White;
        if (ui_.menuItem(CONFIG_USERNAME_INDEX, ubuf, SCREEN_W / 2, y, rowW, rowH, accent, sel, 18, 20)) {
            configSelection_ = CONFIG_USERNAME_INDEX;
            menuSelection_ = CONFIG_USERNAME_INDEX;
            if (!usernameTyping_) confirmInput_ = true;
        }
        if (ui_.hoveredItem == CONFIG_USERNAME_INDEX && !usingGamepad_) {
            configSelection_ = CONFIG_USERNAME_INDEX;
            menuSelection_ = CONFIG_USERNAME_INDEX;
        }
        if (sel && !usernameTyping_) {
            char hint[64];
            snprintf(hint, sizeof(hint), "[%s to edit]", UI::glyphLabel(UI::Action::Confirm, usingGamepad_));
            ui_.drawText(hint, SCREEN_W / 2 + 160, y + 6, 12, UI::Color::HintGray);
        }
        y += stepY;
    }

    // Back button
    {
        bool sel = (configSelection_ == CONFIG_BACK_INDEX);
        if (ui_.menuItem(CONFIG_BACK_INDEX, "BACK", SCREEN_W / 2, y + 8, rowW, rowH,
                         UI::Color::White, sel, 18, 20)) {
            configSelection_ = CONFIG_BACK_INDEX;
            menuSelection_ = CONFIG_BACK_INDEX;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == CONFIG_BACK_INDEX && !usingGamepad_) {
            configSelection_ = CONFIG_BACK_INDEX;
            menuSelection_ = CONFIG_BACK_INDEX;
        }
    }

    // Hint bar
    UI::HintPair hints[] = {
        {UI::Action::Navigate, "Select"},
        {UI::Action::Left, "Decrease"},
        {UI::Action::Right, "Increase"},
        {UI::Action::Confirm, "Confirm"},
    };
    ui_.drawHintBar(hints, 4, SCREEN_H - 40);
}

void Game::renderPauseMenu() {
    // Dark overlay
    ui_.drawDarkOverlay(200, 4, 6, 14);

    // Panel
    int panelW = 400;
#ifndef __SWITCH__
    int panelH = 370;
#else
    int panelH = 320;
#endif
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    ui_.drawPanel(px, py, panelW, panelH);

    // Title + separator
    ui_.drawTextCentered("PAUSED", py + 24, 36, UI::Color::Cyan);
    ui_.drawSeparator(SCREEN_W / 2, py + 68, (panelW - 80) / 2);

    // Build menu items
    char musBuf[64]; snprintf(musBuf, sizeof(musBuf), "%d%%", config_.musicVolume * 100 / 128);
    char sfxBuf[64]; snprintf(sfxBuf, sizeof(sfxBuf), "%d%%", config_.sfxVolume * 100 / 128);

    int itemY = py + 90;
    int stepY = 50;
    int rowW = panelW - 40;
    int rowH = 36;

    // 0: RESUME button
    if (ui_.menuItem(0, "RESUME", SCREEN_W / 2, itemY, rowW, rowH,
                     UI::Color::Green, menuSelection_ == 0, 20, 22)) {
        menuSelection_ = 0; confirmInput_ = true;
        if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    }
    if (ui_.hoveredItem == 0 && !usingGamepad_) menuSelection_ = 0;
    itemY += stepY;

    // 1: Music volume slider
    {
        int delta = ui_.sliderRow(1, "Music:", musBuf, SCREEN_W / 2, itemY, rowW, rowH,
                                  UI::Color::Cyan, menuSelection_ == 1, leftInput_, rightInput_);
        if (ui_.hoveredItem == 1 && !usingGamepad_) menuSelection_ = 1;
        if (delta != 0 && (menuSelection_ == 1 || ui_.hoveredItem == 1)) {
            config_.musicVolume = std::max(0, std::min(128, config_.musicVolume + delta * 8));
            Mix_VolumeMusic(config_.musicVolume);
        }
    }
    itemY += stepY;

    // 2: SFX volume slider
    {
        int delta = ui_.sliderRow(2, "SFX:", sfxBuf, SCREEN_W / 2, itemY, rowW, rowH,
                                  UI::Color::Cyan, menuSelection_ == 2, leftInput_, rightInput_);
        if (ui_.hoveredItem == 2 && !usingGamepad_) menuSelection_ = 2;
        if (delta != 0 && (menuSelection_ == 2 || ui_.hoveredItem == 2)) {
            config_.sfxVolume = std::max(0, std::min(128, config_.sfxVolume + delta * 8));
        }
    }
    itemY += stepY;

#ifndef __SWITCH__
    // 3: Fullscreen toggle
    {
        char fsBuf[32]; snprintf(fsBuf, sizeof(fsBuf), "Fullscreen: %s", config_.fullscreen ? "ON" : "OFF");
        if (ui_.menuItem(3, fsBuf, SCREEN_W / 2, itemY, rowW, rowH,
                         UI::Color::Lavender, menuSelection_ == 3, 20, 22)) {
            menuSelection_ = 3;
            config_.fullscreen = !config_.fullscreen;
            SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            saveConfig();
        }
        if (ui_.hoveredItem == 3 && !usingGamepad_) menuSelection_ = 3;
        itemY += stepY;
    }

    // 4: Main Menu
    if (ui_.menuItem(4, "MAIN MENU", SCREEN_W / 2, itemY, rowW, rowH,
                     UI::Color::Red, menuSelection_ == 4, 20, 22)) {
        menuSelection_ = 4; confirmInput_ = true;
        if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    }
    if (ui_.hoveredItem == 4 && !usingGamepad_) menuSelection_ = 4;
#else
    // 3: Main Menu (Switch — no fullscreen option)
    if (ui_.menuItem(3, "MAIN MENU", SCREEN_W / 2, itemY, rowW, rowH,
                     UI::Color::Red, menuSelection_ == 3, 20, 22)) {
        menuSelection_ = 3; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 3 && !usingGamepad_) menuSelection_ = 3;
#endif

    // Hint bar
    UI::HintPair hints[] = {
        {UI::Action::Confirm, "Select"},
        {UI::Action::Left, "Adjust"},
        {UI::Action::Right, "Adjust"},
        {UI::Action::Pause, "Resume"},
    };
    ui_.drawHintBar(hints, 4, py + panelH - 30);
}

void Game::renderDeathScreen() {
    // Dark + red tint overlay
    ui_.drawDarkOverlay(200, 30, 4, 4);

    // Panel
    int panelW = 380, panelH = 280;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    ui_.drawPanel(px, py, panelW, panelH, {12, 8, 10, 240}, {255, 60, 60, 60});

    // Title
    ui_.drawTextCentered("YOU DIED", py + 28, 36, UI::Color::DeepRed);
    ui_.drawSeparator(SCREEN_W / 2, py + 72, (panelW - 80) / 2, {255, 60, 60, 40});

    // Stats
    char timeStr[64];
    int mins = (int)gameTime_ / 60;
    int secs = (int)gameTime_ % 60;
    snprintf(timeStr, sizeof(timeStr), "Time: %d:%02d", mins, secs);
    ui_.drawTextCentered(timeStr, py + 90, 18, UI::Color::Gray);

    char waveBuf[64];
    snprintf(waveBuf, sizeof(waveBuf), "Wave: %d", waveNumber_);
    ui_.drawTextCentered(waveBuf, py + 116, 16, UI::Color::Gray);

    // Buttons
    int itemY = py + 160;
    int rowW = panelW - 40;
    int rowH = 36;

    // 0: RETRY
    if (ui_.menuItem(0, "RETRY", SCREEN_W / 2, itemY, rowW, rowH,
                     UI::Color::Green, menuSelection_ == 0, 20, 22)) {
        menuSelection_ = 0; confirmInput_ = true;
        if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    }
    if (ui_.hoveredItem == 0 && !usingGamepad_) menuSelection_ = 0;
    itemY += 48;

    // 1: MAIN MENU
    if (ui_.menuItem(1, "MAIN MENU", SCREEN_W / 2, itemY, rowW, rowH,
                     UI::Color::Red, menuSelection_ == 1, 20, 22)) {
        menuSelection_ = 1; confirmInput_ = true;
        if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    }
    if (ui_.hoveredItem == 1 && !usingGamepad_) menuSelection_ = 1;

    // Hint bar
    UI::HintPair hints[] = { {UI::Action::Confirm, "Select"} };
    ui_.drawHintBar(hints, 1, py + panelH - 26);
}

void Game::drawText(const char* text, int x, int y, int size, SDL_Color color) {
    ui_.drawText(text, x, y, size, color);
}

void Game::drawTextCentered(const char* text, int y, int size, SDL_Color color) {
    ui_.drawTextCentered(text, y, size, color);
}

bool Game::wallCollision(Vec2 pos, float halfSize) const {
    return map_.worldCollides(pos.x, pos.y, halfSize);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Custom Map Play
// ═════════════════════════════════════════════════════════════════════════════

void Game::startCustomMap(const std::string& path) {
    if (!customMap_.loadFromFile(path)) {
        printf("Failed to load custom map: %s\n", path.c_str());
        return;
    }

    // Load custom tile textures
    for (int i = 0; i < 8; i++) {
        customTileTextures_[i] = nullptr;
        if (!customMap_.customTilePaths[i].empty())
            customTileTextures_[i] = Assets::instance().tex(customMap_.customTilePaths[i]);
    }

    state_ = GameState::PlayingCustom;
    playingCustomMap_ = true;
    customGoalOpen_ = false;
    gameTime_ = 0;

    // Apply custom map tiles to the game tilemap
    map_.width  = customMap_.width;
    map_.height = customMap_.height;
    map_.tiles  = customMap_.tiles;
    map_.ceiling = customMap_.ceiling;
    invalidateMinimapCache();

    // Reset entities
    enemies_.clear();
    bullets_.clear();
    enemyBullets_.clear();
    bombs_.clear();
    explosions_.clear();
    debris_.clear();
    blood_.clear();
    boxFragments_.clear();
    crates_.clear();
    pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;

    // Apply sandbox mode (no enemies, no crate spawning)
    sandboxMode_ = (mapConfigMode_ == 1);

    // Reset wave state
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;

    // Reset player
    player_ = Player{};
    player_.maxHp = config_.playerMaxHp;
    player_.hp = config_.playerMaxHp;
    player_.bombCount = 1;
    applyCharacterStatsToPlayer(player_);

    // Find start trigger for player spawn
    MapTrigger* startT = customMap_.findStartTrigger();
    if (startT) {
        player_.pos = {startT->x, startT->y};
    } else {
        player_.pos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
    }

    // Camera
    camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
    camera_.worldW = map_.worldWidth();
    camera_.worldH = map_.worldHeight();

    // Spawn enemies from the map data
    customEnemiesTotal_ = 0;
    if (!sandboxMode_) {
        for (auto& es : customMap_.enemySpawns) {
            if (isCrateSpawnType(es.enemyType)) {
                PickupCrate crate;
                crate.pos = {es.x, es.y};
                crate.contents = rollRandomUpgrade();
                crates_.push_back(crate);
            } else {
                spawnEnemy({es.x, es.y}, enemyTypeFromSpawnId(es.enemyType));
                customEnemiesTotal_++;
            }
        }
    }

    // Also generate spawn points for wave spawning if map has them
    map_.findSpawnPoints();

    {
        std::string mf;
        size_t sl = path.find_last_of('/');
        mf = (sl != std::string::npos) ? path.substr(0, sl + 1) : "./";
        playMapMusic(mf, customMap_.musicPath);
    }
}

void Game::startCustomMapMultiplayer(const std::string& path) {
    if (!customMap_.loadFromFile(path)) {
        printf("Failed to load custom map for multiplayer: %s\n", path.c_str());
        return;
    }

    // Load custom tile textures
    for (int i = 0; i < 8; i++) {
        customTileTextures_[i] = nullptr;
        if (!customMap_.customTilePaths[i].empty())
            customTileTextures_[i] = Assets::instance().tex(customMap_.customTilePaths[i]);
    }

    playingCustomMap_ = true;
    customGoalOpen_ = false;
    gameTime_ = 0;

    // Apply custom map tiles to the game tilemap
    map_.width  = customMap_.width;
    map_.height = customMap_.height;
    map_.tiles  = customMap_.tiles;
    map_.ceiling = customMap_.ceiling;
    invalidateMinimapCache();

    // Reset entities
    enemies_.clear();
    bullets_.clear();
    enemyBullets_.clear();
    bombs_.clear();
    explosions_.clear();
    debris_.clear();
    blood_.clear();
    boxFragments_.clear();
    crates_.clear();
    pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    sandboxMode_ = false;

    // Reset wave state
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;

    // Reset player
    player_ = Player{};
    player_.maxHp = config_.playerMaxHp;
    player_.hp = config_.playerMaxHp;
    player_.bombCount = 1;
    applyCharacterStatsToPlayer(player_);

    // Find start trigger for player spawn (prefer team spawn in team/PvP mode)
    {
        bool spawned = false;
        if (localTeam_ >= 0 && currentRules_.teamCount >= 2) {
            MapTrigger* teamT = customMap_.findTeamSpawnTrigger(localTeam_);
            if (teamT) { player_.pos = {teamT->x, teamT->y}; spawned = true; }
        }
        if (!spawned) {
            MapTrigger* startT = customMap_.findStartTrigger();
            if (startT) player_.pos = {startT->x, startT->y};
            else player_.pos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
        }
    }

    // Camera
    camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
    camera_.worldW = map_.worldWidth();
    camera_.worldH = map_.worldHeight();

    // Spawn enemies from map data (host only, skip in PVP mode)
    auto& net = NetworkManager::instance();
    customEnemiesTotal_ = 0;
    if (net.isHost() && !lobbySettings_.isPvp) {
        for (auto& es : customMap_.enemySpawns) {
            if (isCrateSpawnType(es.enemyType)) {
                PickupCrate crate;
                crate.pos = {es.x, es.y};
                crate.contents = rollRandomUpgrade();
                crates_.push_back(crate);
            } else {
                spawnEnemy({es.x, es.y}, enemyTypeFromSpawnId(es.enemyType));
                customEnemiesTotal_++;
            }
        }
    }

    map_.findSpawnPoints();

    {
        std::string mf;
        size_t sl = path.find_last_of('/');
        mf = (sl != std::string::npos) ? path.substr(0, sl + 1) : "./";
        playMapMusic(mf, customMap_.musicPath);
    }
}

void Game::updateCustomMapGoal() {
    if (!playingCustomMap_) return;

    MapTrigger* goal = customMap_.findEndTrigger();
    if (!goal) return;

    // Check goal unlock condition
    switch (goal->condition) {
    case GoalCondition::DefeatAll: {
        // Count alive enemies
        int alive = 0;
        for (auto& e : enemies_) if (e.alive) alive++;
        customGoalOpen_ = (alive == 0 && customEnemiesTotal_ > 0);
        break;
    }
    case GoalCondition::Immediate:
        customGoalOpen_ = true;
        break;
    case GoalCondition::OnTrigger:
        // Check if some condition met (simple: after X seconds or enemies < half)
        customGoalOpen_ = (gameTime_ > 30.0f);
        break;
    }

    // Check if player reached the goal
    if (customGoalOpen_) {
        float dx = player_.pos.x - goal->x;
        float dy = player_.pos.y - goal->y;
        if (fabsf(dx) < goal->width/2 && fabsf(dy) < goal->height/2) {
            state_ = GameState::CustomWin;
            menuSelection_ = 0;
        }
    }
}

void Game::scanMapFiles() {
    mapFiles_.clear();

    // Scan "maps" directory for .csm files
    const char* dirs[] = {"maps", "romfs/maps", "romfs:/maps"};
    for (const char* dir : dirs) {
        DIR* d = opendir(dir);
        if (!d) continue;
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string fname(entry->d_name);
            if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".csm") {
                mapFiles_.push_back(std::string(dir) + "/" + fname);
            }
        }
        closedir(d);
    }

    // Also include maps from all enabled mods
    auto modMaps = ModManager::instance().allMapPaths();
    for (auto& mp : modMaps) {
        // Avoid duplicates
        bool found = false;
        for (auto& ex : mapFiles_) if (ex == mp) { found = true; break; }
        if (!found) mapFiles_.push_back(mp);
    }

    printf("Found %d map files\n", (int)mapFiles_.size());
}

void Game::scanCharacters() {
    for (auto& cd : availableChars_) cd.unload();
    availableChars_.clear();

    // Scan standard character directories
    const char* dirs[] = {"characters", "romfs/characters", "romfs:/characters"};
    for (const char* dir : dirs) {
        auto found = ::scanCharacters(dir, renderer_);
        for (auto& cd : found)
            availableChars_.push_back(std::move(cd));
    }

    // Also scan mod character directories
    const auto& mods = ModManager::instance().mods();
    for (const auto& mod : mods) {
        if (!mod.enabled) continue;
        std::string modCharDir = "mods/" + mod.id + "/characters";
        auto found = ::scanCharacters(modCharDir, renderer_);
        for (auto& cd : found)
            availableChars_.push_back(std::move(cd));
    }

    printf("Found %d characters\n", (int)availableChars_.size());
}

void Game::applyCharacter(const CharacterDef& cd) {
    // Override player sprites with character sprites
    if (!cd.bodySprites.empty()) playerSprites_ = cd.bodySprites;
    if (!cd.legSprites.empty()) legSprites_ = cd.legSprites;
    if (!cd.deathSprites.empty()) playerDeathSprites_ = cd.deathSprites;

    // Store active character for stat application
    activeCharDef_ = CharacterDef{};
    activeCharDef_.folder = cd.folder;
    activeCharDef_.name = cd.name;
    activeCharDef_.speed = cd.speed;
    activeCharDef_.hp = cd.hp;
    activeCharDef_.ammo = cd.ammo;
    activeCharDef_.fireRate = cd.fireRate;
    activeCharDef_.reloadTime = cd.reloadTime;
    activeCharDef_.shootOffsetX = cd.shootOffsetX;
    activeCharDef_.shootOffsetY = cd.shootOffsetY;
    hasActiveChar_ = true;
    printf("Applied character: %s (spd:%.0f hp:%d ammo:%d)\n",
           cd.name.c_str(), cd.speed, cd.hp, cd.ammo);
}

void Game::applyCharacterStatsToPlayer(Player& p) {
    if (!hasActiveChar_) return;
    p.speed      = activeCharDef_.speed;
    p.hp         = activeCharDef_.hp;
    p.maxHp      = activeCharDef_.hp;
    p.ammo       = activeCharDef_.ammo;
    p.maxAmmo    = activeCharDef_.ammo;
    p.fireRate   = activeCharDef_.fireRate;
    p.reloadTime = activeCharDef_.reloadTime;
}

void Game::resetToDefaultCharacter() {
    hasActiveChar_ = false;
    selectedChar_ = -1;
    activeCharDef_ = CharacterDef{};
    playerSprites_ = defaultPlayerSprites_;
    legSprites_ = defaultLegSprites_;
    playerDeathSprites_ = defaultPlayerDeathSprites_;
}

void Game::clearSyncedCharacter(uint8_t playerId) {
    auto it = syncedCharacters_.find(playerId);
    if (it == syncedCharacters_.end()) return;
    if (it->second.visualLoaded) {
        it->second.visual.unload();
        it->second.visualLoaded = false;
    }
    syncedCharacters_.erase(it);
}

void Game::clearSyncedCharacters() {
    for (auto& entry : syncedCharacters_) {
        if (entry.second.visualLoaded) entry.second.visual.unload();
    }
    syncedCharacters_.clear();
    lastCharacterSyncKey_.clear();
}

std::vector<uint8_t> Game::buildCharacterSyncBundle(const CharacterDef& cd) const {
    std::vector<uint8_t> bundle;
    if (cd.folder.empty()) return bundle;

    DIR* dir = opendir(cd.folder.c_str());
    if (!dir) return bundle;

    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (!isAllowedSyncedCharacterFile(name)) continue;

        std::string path = cd.folder;
        if (!path.empty() && path.back() != '/') path += '/';
        path += name;

        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) continue;

        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        std::vector<uint8_t> bytes((size_t)st.st_size);
        size_t got = bytes.empty() ? 0 : fread(bytes.data(), 1, bytes.size(), f);
        fclose(f);
        if (got != bytes.size()) continue;
        files.push_back({name, std::move(bytes)});
    }
    closedir(dir);

    if (files.empty()) return {};
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    auto appendU16 = [&](uint16_t value) {
        bundle.push_back((uint8_t)(value & 0xFF));
        bundle.push_back((uint8_t)((value >> 8) & 0xFF));
    };
    auto appendU32 = [&](uint32_t value) {
        bundle.push_back((uint8_t)(value & 0xFF));
        bundle.push_back((uint8_t)((value >> 8) & 0xFF));
        bundle.push_back((uint8_t)((value >> 16) & 0xFF));
        bundle.push_back((uint8_t)((value >> 24) & 0xFF));
    };

    bundle.push_back('C');
    bundle.push_back('S');
    bundle.push_back('C');
    bundle.push_back('B');
    appendU16((uint16_t)files.size());
    for (const auto& file : files) {
        appendU16((uint16_t)file.first.size());
        appendU32((uint32_t)file.second.size());
        bundle.insert(bundle.end(), file.first.begin(), file.first.end());
        bundle.insert(bundle.end(), file.second.begin(), file.second.end());
    }
    return bundle;
}

bool Game::installSyncedCharacterVisual(uint8_t playerId, const std::string& characterName,
                                        const std::vector<uint8_t>& data) {
    if (data.size() < 6) return false;
    if (!(data[0] == 'C' && data[1] == 'S' && data[2] == 'C' && data[3] == 'B')) return false;

    auto readU16 = [&](size_t off) -> uint16_t {
        return (uint16_t)data[off] | ((uint16_t)data[off + 1] << 8);
    };
    auto readU32 = [&](size_t off) -> uint32_t {
        return (uint32_t)data[off] |
               ((uint32_t)data[off + 1] << 8) |
               ((uint32_t)data[off + 2] << 16) |
               ((uint32_t)data[off + 3] << 24);
    };

    size_t offset = 4;
    uint16_t fileCount = readU16(offset);
    offset += 2;
    if (fileCount == 0) return false;

    mkdir("cache", 0755);
    mkdir("cache/remote_chars", 0755);
    std::string cacheFolder = "cache/remote_chars/p" + std::to_string((int)playerId) + "_" + sanitizeNetCharacterName(characterName);
    mkdir(cacheFolder.c_str(), 0755);

    DIR* dir = opendir(cacheFolder.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (name == "." || name == "..") continue;
            std::string path = cacheFolder + "/" + name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) remove(path.c_str());
        }
        closedir(dir);
    }

    for (uint16_t i = 0; i < fileCount; i++) {
        if (offset + 6 > data.size()) return false;
        uint16_t nameLen = readU16(offset);
        offset += 2;
        uint32_t fileSize = readU32(offset);
        offset += 4;
        if (nameLen == 0 || offset + nameLen > data.size()) return false;

        std::string fileName((const char*)data.data() + offset, nameLen);
        offset += nameLen;
        if (!isAllowedSyncedCharacterFile(fileName) || offset + fileSize > data.size()) return false;

        std::string outPath = cacheFolder + "/" + fileName;
        FILE* out = fopen(outPath.c_str(), "wb");
        if (!out) return false;
        if (fileSize > 0) fwrite(data.data() + offset, 1, fileSize, out);
        fclose(out);
        offset += fileSize;
    }

    CharacterDef loaded;
    if (!loaded.loadFromFolder(cacheFolder, renderer_)) return false;
    loaded.name = characterName;

    auto& synced = syncedCharacters_[playerId];
    if (synced.visualLoaded) synced.visual.unload();
    synced.visual = std::move(loaded);
    synced.visualLoaded = true;
    synced.cacheFolder = cacheFolder;
    return true;
}

void Game::syncLocalCharacterSelection(bool force) {
    auto& net = NetworkManager::instance();
    if (!net.isOnline() || net.state() == NetState::Connecting) return;

    bool isDefault = true;
    std::string characterName = "Default";
    std::vector<uint8_t> bundle;

    if (selectedChar_ >= 0 && selectedChar_ < (int)availableChars_.size()) {
        const CharacterDef& cd = availableChars_[selectedChar_];
        isDefault = false;
        characterName = cd.name;
        bundle = buildCharacterSyncBundle(cd);
        if (bundle.empty()) {
            printf("[NET] Character sync bundle empty for %s, using default instead\n", cd.name.c_str());
            isDefault = true;
            characterName = "Default";
        }
    }

    std::string key = std::string(isDefault ? "D:" : "C:") + characterName + ":" + std::to_string(bundle.size());
    if (!force && key == lastCharacterSyncKey_) return;

    net.sendLocalCharacterSync(characterName, isDefault, bundle);
    lastCharacterSyncKey_ = key;
}

void Game::renderMapSelectMenu() {
    ui_.drawDarkOverlay(235, 6, 8, 16);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color yellow = {255, 220, 60, 255};
    int panelW = 620;
    int panelH = SCREEN_H - 120;
    int panelX = (SCREEN_W - panelW) / 2;
    int panelY = 70;
    ui_.drawPanel(panelX, panelY, panelW, panelH, {10, 12, 24, 240}, {0, 180, 160, 90});

    drawTextCentered("SELECT MAP", panelY + 18, 36, cyan);
    ui_.drawSeparator(SCREEN_W / 2, panelY + 62, 100);

    if (mapFiles_.empty()) {
        drawTextCentered("No .csm maps found in maps/ folder", SCREEN_H / 2 - 10, 20, gray);
        drawTextCentered("Use the Editor to create maps!", SCREEN_H / 2 + 20, 14, {80, 80, 90, 255});
    } else {
        int baseY = panelY + 96;
        int stepY = 38;
        int maxVisible = (panelH - 150) / stepY;
        if (maxVisible < 3) maxVisible = 3;
        int scrollOff = std::max(0, menuSelection_ - maxVisible + 1);
        for (int i = scrollOff; i < (int)mapFiles_.size() && (i - scrollOff) < maxVisible; i++) {
            int y = baseY + (i - scrollOff) * stepY;
            bool sel = (menuSelection_ == i);
            std::string fname = mapFiles_[i];
            size_t slash = fname.find_last_of('/');
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            int animIdx = i - scrollOff;
            if (ui_.menuItem(animIdx, fname.c_str(), SCREEN_W / 2, y, 400, 32,
                             yellow, sel, 20, 22)) {
                menuSelection_ = i;
                confirmInput_ = true;
            }
            if (ui_.hoveredItem == animIdx && !usingGamepad_) menuSelection_ = i;
        }
        // Scroll indicator
        if ((int)mapFiles_.size() > maxVisible) {
            float ratio = (float)maxVisible / mapFiles_.size();
            float scrollRatio = mapFiles_.size() > 1 ? (float)scrollOff / (mapFiles_.size() - maxVisible) : 0;
            int trackH = panelH - 160;
            int barH = std::max(20, (int)(trackH * ratio));
            int barY = baseY + (int)((trackH - barH) * scrollRatio);
            SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 60);
            SDL_Rect sb = {panelX + panelW - 24, barY, 4, barH};
            SDL_RenderFillRect(renderer_, &sb);
        }
    }

    int backIdx = (int)mapFiles_.size();
    bool backSel = (menuSelection_ == backIdx);
    if (ui_.menuItem(62, "BACK", SCREEN_W / 2, SCREEN_H - 100, 200, 32,
                     UI::Color::White, backSel, 20, 22)) {
        menuSelection_ = backIdx;
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 62 && !usingGamepad_) menuSelection_ = backIdx;

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Select"}, {UI::Action::Back, "Back"} };
      ui_.drawHintBar(hints, 2); }
}

void Game::renderMapConfigMenu() {
    // Dark overlay
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 210);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    // Panel
    int panelW = 400, panelH = 260;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    SDL_SetRenderDrawColor(renderer_, 10, 12, 24, 240);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 80);
    SDL_RenderDrawRect(renderer_, &panel);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color green  = {50, 255, 150, 255};
    SDL_Color yellow = {255, 220, 60, 255};

    drawTextCentered("SELECT MODE", py + 20, 30, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect sep = {px + 40, py + 56, panelW - 80, 1};
    SDL_RenderFillRect(renderer_, &sep);

    struct ModeItem { const char* label; const char* desc; SDL_Color accent; };
    ModeItem items[] = {
        {"ARENA",   "Survive enemy waves",    green},
        {"SANDBOX", "No enemies, no crates", yellow},
        {"BACK",    "",                       white},
    };
    int itemY = py + 76;
    int stepY = 56;
    for (int i = 0; i < 3; i++) {
        bool sel = (menuSelection_ == i);
        if (ui_.menuItem(i, items[i].label, SCREEN_W / 2, itemY, panelW - 32, 42,
                         items[i].accent, sel, 20, 22)) {
            menuSelection_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) menuSelection_ = i;
        if (sel && items[i].desc[0]) {
            drawTextCentered(items[i].desc, itemY + 28, 12, {80, 80, 90, 255});
        }
        itemY += stepY;
    }
}

void Game::renderCharSelectMenu() {
    ui_.drawDarkOverlay(235, 6, 8, 16);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color yellow = {255, 220, 60, 255};
    SDL_Color dimCyan= {0, 140, 130, 255};
    SDL_Rect listPanel = {90, 90, 440, SCREEN_H - 180};
    SDL_Rect detailPanel = {570, 90, 620, SCREEN_H - 180};
    ui_.drawPanel(listPanel.x, listPanel.y, listPanel.w, listPanel.h, {10, 12, 24, 240}, {0, 180, 160, 90});
    ui_.drawPanel(detailPanel.x, detailPanel.y, detailPanel.w, detailPanel.h, {10, 12, 24, 240}, {0, 180, 160, 90});

    drawTextCentered("SELECT CHARACTER", 28, 36, cyan);
    ui_.drawSeparator(SCREEN_W / 2, 72, 120);

    auto drawWalkPreview = [&](const CharacterDef* cd, bool useDefault) {
        const std::vector<SDL_Texture*>* bodyFrames = nullptr;
        const std::vector<SDL_Texture*>* legFrames = nullptr;
        const char* previewName = "Default";
        int hp = config_.playerMaxHp;
        float speed = 520.0f;
        int ammo = 10;

        if (!useDefault && cd) {
            bodyFrames = &cd->bodySprites;
            legFrames = &cd->legSprites;
            previewName = cd->name.c_str();
            hp = cd->hp;
            speed = cd->speed;
            ammo = cd->ammo;
        } else {
            bodyFrames = &defaultPlayerSprites_;
            legFrames = &defaultLegSprites_;
        }

        drawText("PREVIEW", detailPanel.x + 24, detailPanel.y + 20, 18, cyan);
        drawText(previewName, detailPanel.x + 24, detailPanel.y + 48, 14, white);

        SDL_Rect previewBox = {detailPanel.x + 32, detailPanel.y + 82, detailPanel.w - 64, 320};
        ui_.drawPanel(previewBox.x, previewBox.y, previewBox.w, previewBox.h, {10, 14, 24, 255}, {0, 150, 140, 60});

        SDL_Rect previewGrid = {previewBox.x + 18, previewBox.y + 18, previewBox.w - 36, previewBox.h - 36};
        SDL_SetRenderDrawColor(renderer_, 16, 20, 32, 255);
        SDL_RenderFillRect(renderer_, &previewGrid);
        SDL_SetRenderDrawColor(renderer_, 28, 34, 48, 255);
        for (int gx = 0; gx <= previewGrid.w; gx += 36)
            SDL_RenderDrawLine(renderer_, previewGrid.x + gx, previewGrid.y, previewGrid.x + gx, previewGrid.y + previewGrid.h);
        for (int gy = 0; gy <= previewGrid.h; gy += 36)
            SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + gy, previewGrid.x + previewGrid.w, previewGrid.y + gy);
        SDL_SetRenderDrawColor(renderer_, 0, 100, 95, 110);
        SDL_RenderDrawLine(renderer_, previewGrid.x + previewGrid.w / 2, previewGrid.y, previewGrid.x + previewGrid.w / 2, previewGrid.y + previewGrid.h);
        SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + previewGrid.h / 2, previewGrid.x + previewGrid.w, previewGrid.y + previewGrid.h / 2);

        SDL_Texture* bodyTex = nullptr;
        SDL_Texture* legTex = nullptr;
        if (bodyFrames && !bodyFrames->empty()) {
            int idx = ((int)(gameTime_ * 8.0f)) % std::max(1, (int)bodyFrames->size());
            bodyTex = (*bodyFrames)[idx];
        }
        if (legFrames && !legFrames->empty()) {
            int idx = ((int)(gameTime_ * 10.0f)) % std::max(1, (int)legFrames->size());
            legTex = (*legFrames)[idx];
        }

        auto drawCenteredTex = [&](SDL_Texture* tex, float scaleMul, int yOffset) {
            if (!tex) return;
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            float scale = std::min((float)previewGrid.w / (float)std::max(1, tw),
                                   (float)previewGrid.h / (float)std::max(1, th)) * scaleMul;
            SDL_Rect dst;
            dst.w = (int)(tw * scale);
            dst.h = (int)(th * scale);
            dst.x = previewGrid.x + (previewGrid.w - dst.w) / 2;
            dst.y = previewGrid.y + (previewGrid.h - dst.h) / 2 + yOffset;
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        };

        drawCenteredTex(legTex, 0.88f, 12);
        drawCenteredTex(bodyTex, 0.88f, -8);

        char stats[128];
        snprintf(stats, sizeof(stats), "HP:%d   SPD:%.0f   AMMO:%d", hp, speed, ammo);
    };

    int baseY = listPanel.y + 54;
    int stepY = 42;

    if (menuSelection_ == 0) {
        drawWalkPreview(nullptr, true);
    }

    // Default option
    {
        bool sel = (menuSelection_ == 0);
        if (ui_.menuItem(0, "Default", listPanel.x + listPanel.w / 2, baseY, 360, 34,
                         yellow, sel, 20, 22)) {
            menuSelection_ = 0;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == 0 && !usingGamepad_) menuSelection_ = 0;
    }

    for (int i = 0; i < (int)availableChars_.size(); i++) {
        int y = baseY + (i + 1) * stepY;
        bool sel = (menuSelection_ == i + 1);
        int animIdx = i + 1;
        if (ui_.menuItem(animIdx, availableChars_[i].name.c_str(), listPanel.x + listPanel.w / 2, y, 360, 34,
                         yellow, sel, 20, 22)) {
            menuSelection_ = i + 1;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == animIdx && !usingGamepad_) menuSelection_ = i + 1;

        if (sel && availableChars_[i].detailSprite) {
            SDL_Rect detDst = {detailPanel.x + 120, detailPanel.y + 70, 320, 420};
            SDL_RenderCopy(renderer_, availableChars_[i].detailSprite, nullptr, &detDst);
        }
        if (sel) {
            drawWalkPreview(&availableChars_[i], false);
        }
    }

    int backIdx = (int)availableChars_.size() + 1;
    bool backSel = (menuSelection_ == backIdx);
    int backY = baseY + backIdx * stepY;
    if (ui_.menuItem(63, "BACK", listPanel.x + listPanel.w / 2, backY, 200, 32,
                     UI::Color::White, backSel, 20, 22)) {
        menuSelection_ = backIdx;
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 63 && !usingGamepad_) menuSelection_ = backIdx;

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Select"}, {UI::Action::Back, "Back"} };
      ui_.drawHintBar(hints, 2); }
}

void Game::renderCustomWinScreen() {
    // Dark overlay
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 200);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color green  = {50, 255, 100, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};

    // Panel
    int panelW = 380, panelH = 220;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    SDL_SetRenderDrawColor(renderer_, 10, 16, 12, 240);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 60);
    SDL_RenderDrawRect(renderer_, &panel);

    drawTextCentered("LEVEL COMPLETE!", py + 28, 34, green);
    SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 40);
    SDL_Rect sep = {px + 40, py + 68, panelW - 80, 1};
    SDL_RenderFillRect(renderer_, &sep);

    char timeStr[64];
    int mins = (int)gameTime_ / 60;
    int secs = (int)gameTime_ % 60;
    snprintf(timeStr, sizeof(timeStr), "Time: %d:%02d", mins, secs);
    drawTextCentered(timeStr, py + 88, 18, gray);

    // Continue button
    if (ui_.menuItem(0, "CONTINUE", SCREEN_W / 2, py + 140, panelW - 40, 36,
                     UI::Color::Green, true, 22, 24)) {
        confirmInput_ = true;
    }

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Continue"} };
      ui_.drawHintBar(hints, 1, py + panelH - 22); }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Character Creator
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderCharCreator() {
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    auto& cc = charCreator_;
    bool modalOpen = modSaveDialog_.isOpen();
    SDL_Color title  = UI::Color::Cyan;
    SDL_Color white  = UI::Color::White;
    SDL_Color gray   = UI::Color::Gray;
    SDL_Color cyan   = UI::Color::Cyan;
    SDL_Color yellow = UI::Color::Yellow;
    SDL_Color green  = UI::Color::Green;
    SDL_Color red    = UI::Color::Red;
    SDL_Color blue   = UI::Color::Blue;
    char buf[256];

    const char* heading = cc.isEditing ? "CHARACTER WORKBENCH" : "CHARACTER CREATOR";
    drawTextCentered(heading, 24, 34, title);
    drawTextCentered("Edit stats, preview exact frames, and click the sprite to place the shooting point.", 60, 14, {120, 160, 170, 255});

    const SDL_Rect leftPanel = {36, 92, 420, 590};
    const SDL_Rect rightPanel = {480, 92, 764, 590};
    ui_.drawPanel(leftPanel.x, leftPanel.y, leftPanel.w, leftPanel.h, {8, 12, 24, 235}, {0, 180, 160, 70});
    ui_.drawPanel(rightPanel.x, rightPanel.y, rightPanel.w, rightPanel.h, {8, 12, 24, 235}, {0, 180, 160, 70});

    drawText("PROFILE", leftPanel.x + 22, leftPanel.y + 18, 20, title);
    drawText("PREVIEW + MUZZLE", rightPanel.x + 22, rightPanel.y + 18, 20, title);

    int contentX = leftPanel.x + 18;
    int contentY = leftPanel.y + 56;
    const int rowW = leftPanel.w - 36;
    const int rowH = 36;
    const int rowGap = 8;

    auto drawField = [&](int idx, const char* label, const char* value, bool adjustable, const char* hint) {
        SDL_Rect row = {contentX, contentY, rowW, rowH};
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, row.x, row.y, row.w, row.h);
        bool selected = (cc.field == idx);
        SDL_SetRenderDrawColor(renderer_, selected ? 18 : 12, selected ? 34 : 18, selected ? 44 : 28, 255);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(renderer_, selected ? 0 : 40, selected ? 210 : 80, selected ? 190 : 90, hovered ? 180 : 110);
        SDL_RenderDrawRect(renderer_, &row);

        drawText(label, row.x + 14, row.y + 7, 16, selected ? white : gray);
        drawText(value, row.x + 178, row.y + 7, 18, selected ? cyan : white);
        if (adjustable) {
            drawText("<", row.x + row.w - 66, row.y + 7, 18, yellow);
            drawText(">", row.x + row.w - 28, row.y + 7, 18, yellow);
        }
        if (hint && *hint) drawText(hint, row.x + 14, row.y + 25, 11, {90, 110, 120, 255});

        if (!modalOpen && hovered && !usingGamepad_) cc.field = idx;
        if (hovered) ui_.hoveredItem = idx;
        if (!modalOpen && hovered && ui_.mouseClicked) {
            cc.field = idx;
            if (idx == 0) confirmInput_ = true;
        }
        contentY += rowH + rowGap;
    };

    if (cc.textEditing) {
        SDL_Rect row = {contentX, contentY, rowW, rowH};
        SDL_SetRenderDrawColor(renderer_, 20, 26, 32, 255);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 200);
        SDL_RenderDrawRect(renderer_, &row);
        drawText("NAME", row.x + 14, row.y + 7, 16, yellow);
        static float ccBlink = 0.0f;
        ccBlink += 0.016f;
        std::string display = cc.textBuf + (((int)(ccBlink * 1.5f) % 2) == 0 ? "_" : " ");
        drawText(display.c_str(), row.x + 178, row.y + 7, 18, yellow);
        drawText("Type and press Enter", row.x + 14, row.y + 25, 11, {120, 120, 90, 255});
        contentY += rowH + rowGap;
        renderSoftKB(contentY + 2);
        contentY += 150;
    } else {
        drawField(0, "NAME", cc.name.c_str(), false, "Press Enter to rename");
    }

    snprintf(buf, sizeof(buf), "%.0f", cc.speed);
    drawField(1, "SPEED", buf, true, "Move speed");
    snprintf(buf, sizeof(buf), "%d", cc.hp);
    drawField(2, "HP", buf, true, "Starting health");
    snprintf(buf, sizeof(buf), "%d", cc.ammo);
    drawField(3, "AMMO", buf, true, "Magazine size");
    snprintf(buf, sizeof(buf), "%.1f", cc.fireRate);
    drawField(4, "FIRE RATE", buf, true, "Shots per second");
    snprintf(buf, sizeof(buf), "%.1f", cc.reloadTime);
    drawField(5, "RELOAD", buf, true, "Seconds to reload");

    SDL_Rect spriteInfo = {contentX, contentY + 4, rowW, 88};
    ui_.drawPanel(spriteInfo.x, spriteInfo.y, spriteInfo.w, spriteInfo.h, {10, 16, 28, 220}, {0, 150, 140, 60});
    drawText("SPRITE SET", spriteInfo.x + 12, spriteInfo.y + 10, 16, title);
    if (cc.loaded) {
        snprintf(buf, sizeof(buf), "Body: %d   Legs: %d   Death: %d", (int)cc.charDef.bodySprites.size(), (int)cc.charDef.legSprites.size(), (int)cc.charDef.deathSprites.size());
        drawText(buf, spriteInfo.x + 14, spriteInfo.y + 38, 13, cc.charDef.bodySprites.empty() ? red : green);
        drawText(cc.charDef.hasDetail ? "Detail sprite loaded" : "No detail sprite", spriteInfo.x + 14, spriteInfo.y + 58, 13, cc.charDef.hasDetail ? green : gray);
    } else {
        drawText("No character folder loaded yet.", spriteInfo.x + 14, spriteInfo.y + 40, 13, gray);
        drawText("Sprites are taken from characters/<name>/", spriteInfo.x + 14, spriteInfo.y + 58, 11, {90, 110, 120, 255});
    }

    SDL_Rect existingPanel = {contentX, spriteInfo.y + spriteInfo.h + 12, rowW, 72};
    ui_.drawPanel(existingPanel.x, existingPanel.y, existingPanel.w, existingPanel.h, {10, 16, 28, 220}, {0, 150, 140, 60});
    drawText("EXISTING CHARACTERS", existingPanel.x + 12, existingPanel.y + 8, 14, title);
    if (availableChars_.empty()) {
        drawText("No saved characters found.", existingPanel.x + 12, existingPanel.y + 36, 12, gray);
    } else {
        int chipX = existingPanel.x + 12;
        int chipY = existingPanel.y + 34;
        int shown = std::min((int)availableChars_.size(), 4);
        for (int i = 0; i < shown; i++) {
            SDL_Rect chip = {chipX, chipY, 88, 22};
            bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, chip.x, chip.y, chip.w, chip.h);
            if (hovered) {
                SDL_SetRenderDrawColor(renderer_, 0, 160, 150, 40);
                SDL_RenderFillRect(renderer_, &chip);
            }
            drawText(availableChars_[i].name.c_str(), chip.x + 5, chip.y + 4, 11, hovered ? white : gray);
            if (!modalOpen && hovered && ui_.mouseClicked) loadCharacterIntoCreator(availableChars_[i].folder);
            chipX += 98;
        }
    }

    int buttonY = existingPanel.y + existingPanel.h + 12;
    int buttonCX1 = leftPanel.x + 80;
    int buttonCX2 = leftPanel.x + 210;
    int buttonCX3 = leftPanel.x + 340;
    const char* reloadLabel = cc.loaded ? "RELOAD" : "LOAD SPRITES";
    bool reloadPressed = ui_.menuItem(6, reloadLabel, buttonCX1, buttonY, 116, 30, cc.loaded ? blue : yellow, cc.field == 6, 15, 18);
    bool testPressed   = ui_.menuItem(7, "TEST PLAY", buttonCX2, buttonY, 116, 30, green, cc.field == 7, 15, 18);
    bool modSavePressed= ui_.menuItem(8, "SAVE TO MOD", buttonCX3, buttonY, 132, 30, green, cc.field == 8, 13, 15);
    if (ui_.hoveredItem == 6 && !usingGamepad_) cc.field = 6;
    if (ui_.hoveredItem == 7 && !usingGamepad_) cc.field = 7;
    if (ui_.hoveredItem == 8 && !usingGamepad_) cc.field = 8;
    buttonY += 38;
    bool localSavePressed = ui_.menuItem(9, "SAVE LOCAL", leftPanel.x + 118, buttonY, 150, 30, yellow, cc.field == 9, 15, 18);
    bool backPressed      = ui_.menuItem(10, "BACK", leftPanel.x + 304, buttonY, 150, 30, white, cc.field == 10, 15, 18);
    if (ui_.hoveredItem == 9 && !usingGamepad_) cc.field = 9;
    if (ui_.hoveredItem == 10 && !usingGamepad_) cc.field = 10;

    if (!modalOpen && ui_.mouseClicked) {
        if (reloadPressed) {
            cc.field = 6;
            if (cc.loaded) {
                cc.charDef.reloadSprites(renderer_);
                cc.warnings = cc.charDef.validate();
                cc.statusMsg = "Sprites reloaded!";
                cc.statusTimer = 2.0f;
            } else if (!cc.folderPath.empty()) {
                loadCharacterIntoCreator(cc.folderPath);
            } else {
                loadCharacterIntoCreator("characters/" + cc.name);
            }
        } else if (testPressed) {
            cc.field = 7;
            testCharacter();
        } else if (modSavePressed) {
            cc.field = 8;
            if (!modSaveDialog_.isOpen()) {
                openModSaveDialog(ModSaveDialogState::AssetCharacter);
            }
        } else if (localSavePressed) {
            cc.field = 9;
            std::string folder = cc.folderPath.empty() ? "characters/" + cc.name : cc.folderPath;
            saveCharacterToFolder(folder);
            cc.statusMsg = "Saved to " + folder + "/";
            cc.statusTimer = 3.0f;
        } else if (backPressed) {
            cc.field = 10;
            cc.clearPreviews(renderer_);
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
    }

    const char* sections[] = {"IDLE", "BODY", "LEGS", "DEATH", "DETAIL"};
    int tabX = rightPanel.x + 22;
    int tabY = rightPanel.y + 54;
    for (int i = 0; i < 5; i++) {
        SDL_Rect tab = {tabX + i * 92, tabY, 82, 28};
        bool selected = (cc.previewSection == i);
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, tab.x, tab.y, tab.w, tab.h);
        SDL_SetRenderDrawColor(renderer_, selected ? 0 : 12, selected ? 120 : 35, selected ? 110 : 45, selected ? 120 : 80);
        SDL_RenderFillRect(renderer_, &tab);
        SDL_SetRenderDrawColor(renderer_, selected ? 0 : 60, selected ? 220 : 90, selected ? 190 : 110, hovered ? 180 : 120);
        SDL_RenderDrawRect(renderer_, &tab);
        drawText(sections[i], tab.x + 12, tab.y + 6, 14, selected ? cyan : gray);
        if (!modalOpen && hovered && ui_.mouseClicked) {
            cc.previewSection = i;
            cc.previewFrame = 0;
            cc.playAnimation = false;
        }
    }

    SDL_Rect previewArea = {rightPanel.x + 24, rightPanel.y + 98, 420, 420};
    ui_.drawPanel(previewArea.x, previewArea.y, previewArea.w, previewArea.h, {10, 14, 24, 255}, {70, 90, 110, 120});
    SDL_Rect previewGrid = {previewArea.x + 20, previewArea.y + 20, 380, 380};
    SDL_SetRenderDrawColor(renderer_, 18, 22, 34, 255);
    SDL_RenderFillRect(renderer_, &previewGrid);
    SDL_SetRenderDrawColor(renderer_, 32, 38, 50, 255);
    for (int gx = 0; gx <= previewGrid.w; gx += 38)
        SDL_RenderDrawLine(renderer_, previewGrid.x + gx, previewGrid.y, previewGrid.x + gx, previewGrid.y + previewGrid.h);
    for (int gy = 0; gy <= previewGrid.h; gy += 38)
        SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + gy, previewGrid.x + previewGrid.w, previewGrid.y + gy);
    SDL_SetRenderDrawColor(renderer_, 0, 100, 95, 100);
    SDL_RenderDrawLine(renderer_, previewGrid.x + previewGrid.w / 2, previewGrid.y, previewGrid.x + previewGrid.w / 2, previewGrid.y + previewGrid.h);
    SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + previewGrid.h / 2, previewGrid.x + previewGrid.w, previewGrid.y + previewGrid.h / 2);

    SDL_Texture* previewTex = nullptr;
    int frameCount = 1;
    auto& ccd = cc.charDef;
    switch (cc.previewSection) {
        case 0:
            if (cc.loaded && !ccd.bodySprites.empty()) previewTex = ccd.bodySprites[0];
            else if (!playerSprites_.empty()) previewTex = playerSprites_[0];
            break;
        case 1:
            if (cc.loaded && !ccd.bodySprites.empty()) {
                frameCount = (int)ccd.bodySprites.size();
                previewTex = ccd.bodySprites[cc.previewFrame % frameCount];
            } else if (!playerSprites_.empty()) {
                frameCount = (int)playerSprites_.size();
                previewTex = playerSprites_[cc.previewFrame % frameCount];
            }
            break;
        case 2:
            if (cc.loaded && !ccd.legSprites.empty()) {
                frameCount = (int)ccd.legSprites.size();
                previewTex = ccd.legSprites[cc.previewFrame % frameCount];
            } else if (!legSprites_.empty()) {
                frameCount = (int)legSprites_.size();
                previewTex = legSprites_[cc.previewFrame % frameCount];
            }
            break;
        case 3:
            if (cc.loaded && !ccd.deathSprites.empty()) {
                frameCount = (int)ccd.deathSprites.size();
                previewTex = ccd.deathSprites[cc.previewFrame % frameCount];
            } else if (!playerDeathSprites_.empty()) {
                frameCount = (int)playerDeathSprites_.size();
                previewTex = playerDeathSprites_[cc.previewFrame % frameCount];
            }
            break;
        case 4:
            if (cc.loaded && ccd.detailSprite) previewTex = ccd.detailSprite;
            break;
    }

    SDL_Rect spriteDst = {0, 0, 0, 0};
    float spriteScale = 1.0f;
    int texW = 0, texH = 0;
    if (previewTex) {
        SDL_QueryTexture(previewTex, nullptr, nullptr, &texW, &texH);
        spriteScale = std::min((float)previewGrid.w / texW, (float)previewGrid.h / texH) * 0.78f;
        spriteDst.w = (int)(texW * spriteScale);
        spriteDst.h = (int)(texH * spriteScale);
        spriteDst.x = previewGrid.x + (previewGrid.w - spriteDst.w) / 2;
        spriteDst.y = previewGrid.y + (previewGrid.h - spriteDst.h) / 2;
        SDL_RenderCopy(renderer_, previewTex, nullptr, &spriteDst);

        if (!modalOpen && ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, spriteDst.x, spriteDst.y, spriteDst.w, spriteDst.h)) {
            cc.shootOffsetX = roundf(((float)(ui_.mouseX - spriteDst.x) / spriteScale) - texW * 0.5f);
            cc.shootOffsetY = roundf(((float)(ui_.mouseY - spriteDst.y) / spriteScale) - texH * 0.5f);
            cc.statusMsg = "Shoot point updated";
            cc.statusTimer = 1.5f;
        }

        int shootX = spriteDst.x + (int)((cc.shootOffsetX + texW * 0.5f) * spriteScale);
        int shootY = spriteDst.y + (int)((cc.shootOffsetY + texH * 0.5f) * spriteScale);
        SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 230);
        SDL_RenderDrawLine(renderer_, shootX - 10, shootY, shootX + 10, shootY);
        SDL_RenderDrawLine(renderer_, shootX, shootY - 10, shootX, shootY + 10);
        SDL_Rect dot = {shootX - 2, shootY - 2, 5, 5};
        SDL_RenderFillRect(renderer_, &dot);
        SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 120);
        SDL_RenderDrawLine(renderer_, previewGrid.x + previewGrid.w / 2, previewGrid.y + previewGrid.h / 2, shootX, shootY);
    } else {
        drawText("NO SPRITE PREVIEW", previewGrid.x + 105, previewGrid.y + 162, 18, gray);
        drawText("Load a character folder to preview exact art.", previewGrid.x + 58, previewGrid.y + 190, 12, {90, 110, 120, 255});
    }

    SDL_Rect sideCard = {rightPanel.x + 468, rightPanel.y + 98, 272, 420};
    ui_.drawPanel(sideCard.x, sideCard.y, sideCard.w, sideCard.h, {10, 16, 28, 220}, {0, 150, 140, 60});
    drawText("FRAME CONTROL", sideCard.x + 14, sideCard.y + 14, 16, title);
    snprintf(buf, sizeof(buf), "Frame %d / %d", frameCount > 0 ? cc.previewFrame + 1 : 1, std::max(1, frameCount));
    drawText(buf, sideCard.x + 14, sideCard.y + 42, 18, white);
    drawText("Left/Right steps frames when a button row is focused.", sideCard.x + 14, sideCard.y + 66, 11, {90, 110, 120, 255});

    SDL_Rect prevBtn = {sideCard.x + 14, sideCard.y + 96, 52, 30};
    SDL_Rect toggleBtn = {sideCard.x + 76, sideCard.y + 96, 118, 30};
    SDL_Rect nextBtn = {sideCard.x + 204, sideCard.y + 96, 52, 30};
    auto drawMiniButton = [&](const SDL_Rect& r, const char* label, SDL_Color c) -> bool {
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, r.x, r.y, r.w, r.h);
        SDL_SetRenderDrawColor(renderer_, 18, 24, 34, 255);
        SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, hovered ? 220 : 150);
        SDL_RenderDrawRect(renderer_, &r);
        drawText(label, r.x + 12, r.y + 6, 16, c);
        return hovered;
    };
    bool prevHovered = drawMiniButton(prevBtn, "<", yellow);
    bool toggleHovered = drawMiniButton(toggleBtn, cc.playAnimation ? "ANIM ON" : "ANIM OFF", cc.playAnimation ? green : gray);
    bool nextHovered = drawMiniButton(nextBtn, ">", yellow);
    if (!modalOpen && ui_.mouseClicked && frameCount > 1) {
        if (prevHovered) { cc.playAnimation = false; cc.previewFrame = (cc.previewFrame + frameCount - 1) % frameCount; }
        if (nextHovered) { cc.playAnimation = false; cc.previewFrame = (cc.previewFrame + 1) % frameCount; }
    }
    if (!modalOpen && ui_.mouseClicked && toggleHovered) cc.playAnimation = !cc.playAnimation;

    drawText("SHOOT POINT", sideCard.x + 14, sideCard.y + 150, 16, title);
    snprintf(buf, sizeof(buf), "X: %.0f px", cc.shootOffsetX);
    drawText(buf, sideCard.x + 14, sideCard.y + 180, 16, white);
    snprintf(buf, sizeof(buf), "Y: %.0f px", cc.shootOffsetY);
    drawText(buf, sideCard.x + 14, sideCard.y + 204, 16, white);
    drawText("Click directly on the sprite to place the bullet spawn point.", sideCard.x + 14, sideCard.y + 232, 12, {90, 110, 120, 255});
    drawText("Negative Y is forward/up. Positive X is to the right.", sideCard.x + 14, sideCard.y + 252, 11, {90, 110, 120, 255});
    drawText("The yellow cross is exactly what gets saved to character.cfg.", sideCard.x + 14, sideCard.y + 280, 11, {90, 110, 120, 255});

    std::string folderLabel = cc.folderPath.empty() ? ("mods/<choose>/characters/" + cc.name + "/") : cc.folderPath;
    drawText("OUTPUT FOLDER", sideCard.x + 14, sideCard.y + 322, 14, title);
    drawText(folderLabel.c_str(), sideCard.x + 14, sideCard.y + 344, 11, green);
    drawText(cc.loaded ? "Editing an existing character folder" : "SAVE TO MOD exports into a mod's characters/ folder", sideCard.x + 14, sideCard.y + 366, 11, gray);

    SDL_Rect warnPanel = {rightPanel.x + 24, rightPanel.y + 528, rightPanel.w - 48, 124};
    ui_.drawPanel(warnPanel.x, warnPanel.y, warnPanel.w, warnPanel.h, {10, 16, 28, 220}, {0, 150, 140, 60});
    drawText(cc.warnings.empty() ? "READY" : "WARNINGS", warnPanel.x + 12, warnPanel.y + 10, 14, cc.warnings.empty() ? green : yellow);
    if (cc.warnings.empty()) {
        drawText("Looks good. Save writes stats and the shooting point into character.cfg.", warnPanel.x + 12, warnPanel.y + 34, 12, gray);
        drawText("Tips: Tab changes preview set, R reloads sprites, P toggles animation.", warnPanel.x + 12, warnPanel.y + 54, 12, {90, 110, 120, 255});
    } else {
        int wy = warnPanel.y + 32;
        for (size_t i = 0; i < cc.warnings.size() && i < 3; i++) {
            drawText(cc.warnings[i].c_str(), warnPanel.x + 12, wy, 12, yellow);
            wy += 18;
        }
    }

    if (cc.statusTimer > 0) drawTextCentered(cc.statusMsg.c_str(), SCREEN_H - 44, 16, green);

    { UI::HintPair hints[] = {
        {UI::Action::Navigate, "Move/Adjust"},
        {UI::Action::Confirm, "Select"},
        {UI::Action::Back, "Back"}
    };
            ui_.drawHintBar(hints, 3, SCREEN_H - 22); }
}

// ── Mod build folder helper ──────────────────────────────────────────────────

/*static*/ std::string Game::modBuildFolder(const std::string& modId, const std::string& displayName) {
    std::string base = "mods/" + modId;
    mkdir("mods",                                  0755);
    mkdir(base.c_str(),                            0755);
    mkdir((base + "/maps").c_str(),                0755);
    mkdir((base + "/characters").c_str(),          0755);
    mkdir((base + "/sprites").c_str(),             0755);
    mkdir((base + "/tiles").c_str(),               0755);
    mkdir((base + "/tiles/ground").c_str(),        0755);
    mkdir((base + "/tiles/walls").c_str(),         0755);
    mkdir((base + "/tiles/ceiling").c_str(),       0755);
    mkdir((base + "/tiles/props").c_str(),         0755);
    mkdir((base + "/sprites/characters").c_str(),     0755);
    mkdir((base + "/sprites/characters/body").c_str(),0755);
    mkdir((base + "/sprites/characters/legs").c_str(),0755);
    mkdir((base + "/sounds").c_str(),              0755);

    // Write mod.cfg only if it doesn't exist yet
    std::string cfgPath = base + "/mod.cfg";
    FILE* f = fopen(cfgPath.c_str(), "r");
    if (f) { fclose(f); return base; }  // already exists — don't overwrite
    f = fopen(cfgPath.c_str(), "w");
    if (f) {
        fprintf(f, "[mod]\n");
        fprintf(f, "id=%s\n",          modId.c_str());
        fprintf(f, "name=%s\n",        displayName.c_str());
        fprintf(f, "author=Unknown\n");
        fprintf(f, "version=1.0\n");
        fprintf(f, "description=\n");
        fprintf(f, "game_version=1\n\n");
        fprintf(f, "[content]\n");
        fprintf(f, "characters=true\n");
        fprintf(f, "maps=true\n");
        fprintf(f, "sprites=true\n");
        fprintf(f, "sounds=true\n");
        fclose(f);
        printf("Created mod: %s\n", base.c_str());
    }
    return base;
}

// ── Character save/load helpers ─────────────────────────────────────────────

void Game::saveCharacterToFolder(const std::string& folderPath) {
    auto& cc = charCreator_;

    // Create folder
    mkdir("characters", 0755);
    mkdir(folderPath.c_str(), 0755);

    // Write character.cfg
    std::string cfgPath = folderPath + "/character.cfg";
    FILE* f = fopen(cfgPath.c_str(), "w");
    if (!f) { printf("Failed to save: %s\n", cfgPath.c_str()); return; }
    fprintf(f, "# Character config — sprites are auto-detected from PNGs\n");
    fprintf(f, "name=%s\n\n", cc.name.c_str());
    fprintf(f, "speed=%.0f\n",       cc.speed);
    fprintf(f, "hp=%d\n",            cc.hp);
    fprintf(f, "ammo=%d\n",          cc.ammo);
    fprintf(f, "fire_rate=%.1f\n",   cc.fireRate);
    fprintf(f, "reload_time=%.1f\n", cc.reloadTime);
    fprintf(f, "shoot_x=%.0f\n",     cc.shootOffsetX);
    fprintf(f, "shoot_y=%.0f\n",     cc.shootOffsetY);
    fclose(f);

    // If no sprites exist, copy default templates
    if (!cc.loaded || cc.charDef.bodySprites.empty()) {
        auto copySprite = [&](const char* srcPat, const char* dstPat, int count) {
            for (int i = 1; i <= count; i++) {
                char src[256], dst[256];
                snprintf(src, sizeof(src), srcPat, i);
                snprintf(dst, sizeof(dst), dstPat, i);
                std::string dstPath = folderPath + "/" + dst;
                FILE* check = fopen(dstPath.c_str(), "rb");
                if (check) { fclose(check); continue; }
                const char* srcDirs[] = {"romfs/sprites/player/", "sprites/player/"};
                for (const char* sd : srcDirs) {
                    std::string srcPath = std::string(sd) + src;
                    FILE* sf = fopen(srcPath.c_str(), "rb");
                    if (sf) {
                        FILE* df = fopen(dstPath.c_str(), "wb");
                        if (df) {
                            char buffer[4096]; size_t bytes;
                            while ((bytes = fread(buffer, 1, sizeof(buffer), sf)) > 0)
                                fwrite(buffer, 1, bytes, df);
                            fclose(df);
                        }
                        fclose(sf);
                        break;
                    }
                }
            }
        };
        copySprite("body-%04d.png", "body-%04d.png", 11);
        copySprite("legs-%04d.png", "legs-%04d.png", 8);
        copySprite("death-%d.png",  "death-%d.png",  12);
        printf("Copied default sprite templates to %s\n", folderPath.c_str());
    }

    printf("Character saved: %s\n", cfgPath.c_str());
}

void Game::loadCharacterIntoCreator(const std::string& folderPath) {
    auto& cc = charCreator_;
    cc.charDef.unload();
    cc.charDef = CharacterDef{};

    if (cc.charDef.loadFromFolder(folderPath, renderer_)) {
        cc.loaded = true;
        cc.folderPath = folderPath;
        cc.name = cc.charDef.name;
        cc.speed = cc.charDef.speed;
        cc.hp = cc.charDef.hp;
        cc.ammo = cc.charDef.ammo;
        cc.fireRate = cc.charDef.fireRate;
        cc.reloadTime = cc.charDef.reloadTime;
        cc.shootOffsetX = cc.charDef.shootOffsetX;
        cc.shootOffsetY = cc.charDef.shootOffsetY;
        cc.isEditing = true;
        cc.warnings = cc.charDef.validate();
        cc.previewFrame = 0;
        cc.animTime = 0.0f;
        cc.statusMsg = "Loaded: " + cc.name;
        cc.statusTimer = 2.0f;
        printf("Loaded character into creator: %s\n", cc.name.c_str());
    } else {
        cc.loaded = false;
        cc.statusMsg = "Failed to load from " + folderPath;
        cc.statusTimer = 3.0f;
    }
}

void Game::testCharacter() {
    auto& cc = charCreator_;

    // Save current state first
    std::string folder = cc.folderPath.empty() ? "characters/" + cc.name : cc.folderPath;
    saveCharacterToFolder(folder);

    // Load/reload character
    if (!cc.loaded) {
        loadCharacterIntoCreator(folder);
    }

    // Build a temporary CharacterDef with current stats
    CharacterDef testDef;
    testDef.name = cc.name;
    testDef.speed = cc.speed;
    testDef.hp = cc.hp;
    testDef.ammo = cc.ammo;
    testDef.fireRate = cc.fireRate;
    testDef.reloadTime = cc.reloadTime;
    testDef.shootOffsetX = cc.shootOffsetX;
    testDef.shootOffsetY = cc.shootOffsetY;
    if (cc.loaded) {
        testDef.bodySprites = cc.charDef.bodySprites;
        testDef.legSprites = cc.charDef.legSprites;
        testDef.deathSprites = cc.charDef.deathSprites;
    }
    applyCharacter(testDef);

    // Start sandbox game for testing
    sandboxMode_ = true;
    mapConfigMode_ = 1;
    state_ = GameState::Playing;
    testPlayFromEditor_ = true;  // return to creator when done
    gameTime_ = 0;
    lobbySettings_.isPvp = false;
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;
    enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
    bombs_.clear(); explosions_.clear(); debris_.clear();
    blood_.clear(); boxFragments_.clear();
    crates_.clear(); pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    map_.generate(config_.mapWidth, config_.mapHeight);
    invalidateMinimapCache();

    player_ = Player{};
    player_.maxHp = cc.hp;
    player_.hp = cc.hp;
    player_.speed = cc.speed;
    player_.ammo = cc.ammo;
    player_.maxAmmo = cc.ammo;
    player_.fireRate = cc.fireRate;
    player_.reloadTime = cc.reloadTime;
    player_.pos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
    player_.bombCount = 1;

    camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
    camera_.worldW = map_.worldWidth();
    camera_.worldH = map_.worldHeight();

    if (bgMusic_) { Mix_PlayMusic(bgMusic_, -1); Mix_VolumeMusic(config_.musicVolume); }

    printf("Test playing character: %s\n", cc.name.c_str());
}

void Game::openModSaveDialog(ModSaveDialogState::Asset asset) {
    auto& d = modSaveDialog_;
    d.phase       = ModSaveDialogState::ChooseMod;
    d.asset       = asset;
    d.selIdx      = 0;
    d.confirmed   = false;
    d.newModId.clear();
    d.textEditing = false;
    d.gpCharIdx   = 0;
    d.catIdx      = 0;
    d.confirmedModFolder.clear();

    // Snapshot current mod list (exclude sync mods)
    const auto& mods = ModManager::instance().mods();
    d.modIds.clear();
    d.modNames.clear();
    for (auto& m : mods) {
        if (m.id.find("_mp_sync") != std::string::npos) continue;
        d.modIds.push_back(m.id);
        d.modNames.push_back(m.name.empty() ? m.id : m.name);
    }
    if (softKB_.active) softKB_.close(false);
}

void Game::handleModSaveDialogEvent(const SDL_Event& e) {
    auto& d = modSaveDialog_;
    static const char modNamePal[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    auto beginNewModName = [&]() {
        d.phase = ModSaveDialogState::NameNewMod;
        d.textEditing = true;
        d.gpCharIdx = 0;
        softKB_.open(modNamePal, 8, &d.newModId, 24, [this](bool confirmed) {
            auto& dialog = modSaveDialog_;
            dialog.textEditing = false;
            if (!dialog.isOpen()) return;
            if (confirmed && !dialog.newModId.empty()) {
                dialog.confirmedModFolder = modBuildFolder(dialog.newModId, dialog.newModId);
                dialog.confirmed = true;
            } else {
                dialog.phase = ModSaveDialogState::ChooseMod;
            }
        });
    };
    // Total entries in ChooseMod list = "New Mod" + existing mods
    int total = 1 + (int)d.modIds.size();

    if (d.phase == ModSaveDialogState::ChooseMod) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:    d.selIdx = (d.selIdx - 1 + total) % total; break;
                case SDLK_DOWN:  d.selIdx = (d.selIdx + 1) % total; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (d.selIdx == 0) {
                        beginNewModName();
                    } else {
                        // Existing mod selected
                        d.confirmedModFolder = "mods/" + d.modIds[d.selIdx - 1];
                        if (false)
                            d.phase = ModSaveDialogState::ChooseCategory;
                        else
                            d.confirmed = true;
                    }
                    break;
                case SDLK_ESCAPE: d.close(); break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    d.selIdx = (d.selIdx - 1 + total) % total; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    d.selIdx = (d.selIdx + 1) % total; break;
                case SDL_CONTROLLER_BUTTON_A:
                    if (d.selIdx == 0) {
                        beginNewModName();
                    } else {
                        d.confirmedModFolder = "mods/" + d.modIds[d.selIdx - 1];
                        if (false)
                            d.phase = ModSaveDialogState::ChooseCategory;
                        else
                            d.confirmed = true;
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    d.close(); break;
            }
        }
    }
    else if (d.phase == ModSaveDialogState::NameNewMod) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_BACKSPACE:
                    if (!d.newModId.empty()) d.newModId.pop_back(); break;
                case SDLK_ESCAPE:
                    if (softKB_.active) softKB_.close(false);
                    else d.phase = ModSaveDialogState::ChooseMod;
                    break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_B:
                    if (softKB_.active) softKB_.close(false);
                    else d.phase = ModSaveDialogState::ChooseMod;
                    break;
            }
        }
    }
    else if (d.phase == ModSaveDialogState::ChooseCategory) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:   d.catIdx = (d.catIdx - 1 + ModSaveDialogState::CAT_COUNT) % ModSaveDialogState::CAT_COUNT; break;
                case SDLK_DOWN: d.catIdx = (d.catIdx + 1) % ModSaveDialogState::CAT_COUNT; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    d.confirmedCat = d.catIdx;
                    d.confirmed    = true;
                    break;
                case SDLK_ESCAPE:
                    d.phase = ModSaveDialogState::ChooseMod; break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    d.catIdx = (d.catIdx - 1 + ModSaveDialogState::CAT_COUNT) % ModSaveDialogState::CAT_COUNT; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    d.catIdx = (d.catIdx + 1) % ModSaveDialogState::CAT_COUNT; break;
                case SDL_CONTROLLER_BUTTON_A:
                    d.confirmedCat = d.catIdx;
                    d.confirmed    = true;
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    d.phase = ModSaveDialogState::ChooseMod; break;
            }
        }
    }
}

void Game::renderModSaveDialog() {
    auto& d = modSaveDialog_;
    static const char modNamePal[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    SDL_Color white  = UI::Color::White;
    SDL_Color cyan   = UI::Color::Cyan;
    SDL_Color yellow = UI::Color::Yellow;
    SDL_Color gray   = UI::Color::Gray;
    SDL_Color selBg  = {0, 140, 120, 255};

    // Dim overlay
    ui_.drawDarkOverlay(190, 0, 0, 0);

    // Panel
    int panW = 640, panH = 420;
    int panX = (SCREEN_W - panW) / 2;
    int panY = (SCREEN_H - panH) / 2;
    ui_.drawPanel(panX, panY, panW, panH, {10, 12, 24, 245}, {0, 180, 160, 110});

    int cx = panX + panW / 2;
    int y  = panY + 18;

    static const char* ASSET_NAMES[] = {"MAP", "CHARACTER"};
    char title[64];
    snprintf(title, sizeof(title), "SAVE %s TO MOD", ASSET_NAMES[(int)d.asset]);
    ui_.drawTextCentered(title, y, 24, cyan);
    ui_.drawSeparator(cx, y + 34, 110);
    y += 38;

    // ── Phase: choose mod ──────────────────────────────────────────────
    if (d.phase == ModSaveDialogState::ChooseMod) {
        drawTextCentered("Choose or create a mod to save into:", y, 14, gray);
        y += 26;

        int total = 1 + (int)d.modIds.size();
        for (int i = 0; i < total; i++) {
            bool sel = (i == d.selIdx);
            if (i == 0) {
                if (ui_.menuItem(40 + i, "[ + NEW MOD ]", cx, y, panW - 40, 30, yellow, sel, 16, 18)) {
                    d.selIdx = i;
                    d.phase = ModSaveDialogState::NameNewMod;
                    d.textEditing = true;
                    softKB_.open(modNamePal, 8, &d.newModId, 24, [this](bool confirmed) {
                        auto& dialog = modSaveDialog_;
                        dialog.textEditing = false;
                        if (!dialog.isOpen()) return;
                        if (confirmed && !dialog.newModId.empty()) {
                            dialog.confirmedModFolder = modBuildFolder(dialog.newModId, dialog.newModId);
                            dialog.confirmed = true;
                        } else {
                            dialog.phase = ModSaveDialogState::ChooseMod;
                        }
                    });
                }
            } else {
                char buf[80];
                snprintf(buf, sizeof(buf), "%s  (%s)", d.modNames[i-1].c_str(), d.modIds[i-1].c_str());
                if (ui_.menuItem(40 + i, buf, cx, y, panW - 40, 30, cyan, sel, 16, 18)) {
                    d.selIdx = i;
                    d.confirmedModFolder = "mods/" + d.modIds[i - 1];
                    d.confirmed = true;
                }
            }
            if (ui_.hoveredItem == 40 + i && !usingGamepad_) d.selIdx = i;
            y += 30;
        }

        y = panY + panH - 40;
        { UI::HintPair hints[] = { {UI::Action::Navigate, "Navigate"}, {UI::Action::Confirm, "Confirm"}, {UI::Action::Back, "Cancel"} };
          ui_.drawHintBar(hints, 3, y); }
    }
    // ── Phase: name new mod ────────────────────────────────────────────
    else if (d.phase == ModSaveDialogState::NameNewMod) {
        drawTextCentered("Type a mod ID (letters, digits, _ -)", y, 14, gray);
        y += 34;

        // Text box
        std::string display = d.newModId + "_";
        int bx = panX + 80, bw = panW - 160, bh = 36;
        ui_.drawPanel(bx, y, bw, bh, {18, 22, 40, 255}, {0, 180, 160, 160});
        drawText(display.c_str(), bx + 8, y + 8, 20, yellow);
        y += bh + 16;

        drawTextCentered("The shared on-screen keyboard now handles naming here too.", y, 12, gray);
        renderSoftKB(y + 24);
        y = panY + panH - 40;
        drawTextCentered("ENTER / OK confirms   ESC / CANCEL goes back", y, 11, gray);
    }
    // ── Phase: choose sprite category ─────────────────────────────────
    else if (d.phase == ModSaveDialogState::ChooseCategory) {
        drawTextCentered("Where should this sprite go?", y, 14, gray);
        y += 28;

        for (int i = 0; i < ModSaveDialogState::CAT_COUNT; i++) {
            bool sel = (i == d.catIdx);

            // Click detection
            bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, panX + 20, y - 2, panW - 40, 26);
            if (hovered) ui_.hoveredItem = 20 + i;
            if (hovered && !usingGamepad_) d.catIdx = i;
            if (hovered && ui_.mouseClicked) {
                d.catIdx = i;
                d.confirmedCat = i;
                d.confirmed = true;
            }

            if (sel) {
                SDL_SetRenderDrawColor(renderer_, selBg.r, selBg.g, selBg.b, selBg.a);
                SDL_Rect bg = {panX + 20, y - 2, panW - 40, 26};
                SDL_RenderFillRect(renderer_, &bg);
            }
            drawTextCentered(ModSaveDialogState::CAT_NAMES[i], y + 3, sel ? 17 : 15, sel ? white : gray);
            y += 30;
        }

        y = panY + panH - 40;
        { UI::HintPair hints[] = { {UI::Action::Navigate, "Navigate"}, {UI::Action::Confirm, "Confirm"}, {UI::Action::Back, "Back"} };
          ui_.drawHintBar(hints, 3, y); }
    }
    (void)cx;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Config persistence
// ═════════════════════════════════════════════════════════════════════════════

void Game::saveConfig() {
    FILE* f = fopen("config.txt", "w");
    if (!f) { printf("Failed to save config\n"); return; }
    fprintf(f, "mapWidth=%d\n", config_.mapWidth);
    fprintf(f, "mapHeight=%d\n", config_.mapHeight);
    fprintf(f, "windowWidth=%d\n", config_.windowWidth);
    fprintf(f, "windowHeight=%d\n", config_.windowHeight);
    fprintf(f, "playerMaxHp=%d\n", config_.playerMaxHp);
    fprintf(f, "spawnRateScale=%.2f\n", config_.spawnRateScale);
    fprintf(f, "enemyHpScale=%.2f\n", config_.enemyHpScale);
    fprintf(f, "enemySpeedScale=%.2f\n", config_.enemySpeedScale);
    fprintf(f, "musicVolume=%d\n", config_.musicVolume);
    fprintf(f, "sfxVolume=%d\n", config_.sfxVolume);
    fprintf(f, "username=%s\n", config_.username.c_str());
    fprintf(f, "fullscreen=%d\n", config_.fullscreen ? 1 : 0);
    fprintf(f, "shaderCRT=%d\n", config_.shaderCRT ? 1 : 0);
    fprintf(f, "shaderChromatic=%d\n", config_.shaderChromatic ? 1 : 0);
    fprintf(f, "shaderScanlines=%d\n", config_.shaderScanlines ? 1 : 0);
    fprintf(f, "shaderGlow=%d\n", config_.shaderGlow ? 1 : 0);
    fprintf(f, "shaderGlitch=%d\n", config_.shaderGlitch ? 1 : 0);
    fprintf(f, "shaderNeonEdge=%d\n", config_.shaderNeonEdge ? 1 : 0);
    fprintf(f, "saveIncomingModsPermanently=%d\n", config_.saveIncomingModsPermanently ? 1 : 0);
    fclose(f);
    printf("Config saved to config.txt\n");
}

void Game::loadConfig() {
    FILE* f = fopen("config.txt", "r");
    if (!f) return; // no saved config, use defaults
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        float fval;
        int ival;
        if (sscanf(line, "mapWidth=%d", &ival) == 1) config_.mapWidth = ival;
        else if (sscanf(line, "mapHeight=%d", &ival) == 1) config_.mapHeight = ival;
        else if (sscanf(line, "windowWidth=%d", &ival) == 1) config_.windowWidth = ival;
        else if (sscanf(line, "windowHeight=%d", &ival) == 1) config_.windowHeight = ival;
        else if (sscanf(line, "playerMaxHp=%d", &ival) == 1) config_.playerMaxHp = ival;
        else if (sscanf(line, "spawnRateScale=%f", &fval) == 1) config_.spawnRateScale = fval;
        else if (sscanf(line, "enemyHpScale=%f", &fval) == 1) config_.enemyHpScale = fval;
        else if (sscanf(line, "enemySpeedScale=%f", &fval) == 1) config_.enemySpeedScale = fval;
        else if (sscanf(line, "musicVolume=%d", &ival) == 1) config_.musicVolume = ival;
        else if (sscanf(line, "sfxVolume=%d", &ival) == 1) config_.sfxVolume = ival;
        else if (strncmp(line, "username=", 9) == 0) {
            char uname[64];
            if (sscanf(line, "username=%63[^\n]", uname) == 1) config_.username = uname;
        }
        else if (sscanf(line, "fullscreen=%d", &ival) == 1) config_.fullscreen = (ival != 0);
        else if (sscanf(line, "shaderCRT=%d", &ival) == 1) config_.shaderCRT = (ival != 0);
        else if (sscanf(line, "shaderChromatic=%d", &ival) == 1) config_.shaderChromatic = (ival != 0);
        else if (sscanf(line, "shaderScanlines=%d", &ival) == 1) config_.shaderScanlines = (ival != 0);
        else if (sscanf(line, "shaderGlow=%d", &ival) == 1) config_.shaderGlow = (ival != 0);
        else if (sscanf(line, "shaderGlitch=%d", &ival) == 1) config_.shaderGlitch = (ival != 0);
        else if (sscanf(line, "shaderNeonEdge=%d", &ival) == 1) config_.shaderNeonEdge = (ival != 0);
        else if (sscanf(line, "saveIncomingModsPermanently=%d", &ival) == 1) config_.saveIncomingModsPermanently = (ival != 0);
    }
    clampResolutionConfig(config_);
    fclose(f);
    printf("Config loaded from config.txt\n");
}

// ═════════════════════════════════════════════════════════════════════════════
//  Saved Servers
// ═════════════════════════════════════════════════════════════════════════════

void Game::loadSavedServers() {
    savedServers_.clear();
    FILE* f = fopen("servers.txt", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char name[128], addr[128];
        int port = 7777;
        if (sscanf(line, "%127[^|]|%127[^|]|%d", name, addr, &port) >= 2) {
            SavedServer s;
            s.name = name;
            s.address = addr;
            s.port = port;
            savedServers_.push_back(s);
        }
    }
    fclose(f);
    printf("Loaded %d saved servers\n", (int)savedServers_.size());
}

void Game::saveSavedServers() {
    FILE* f = fopen("servers.txt", "w");
    if (!f) { printf("Failed to save servers\n"); return; }
    for (auto& s : savedServers_) {
        fprintf(f, "%s|%s|%d\n", s.name.c_str(), s.address.c_str(), s.port);
    }
    fclose(f);
    printf("Saved %d servers\n", (int)savedServers_.size());
}

void Game::addSavedServer(const std::string& name, const std::string& addr, int port) {
    SavedServer s;
    s.name = name.empty() ? addr : name;
    s.address = addr;
    s.port = port;
    savedServers_.push_back(s);
    saveSavedServers();
}

void Game::removeSavedServer(int idx) {
    if (idx >= 0 && idx < (int)savedServers_.size()) {
        savedServers_.erase(savedServers_.begin() + idx);
        saveSavedServers();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Server Config Presets
// ═════════════════════════════════════════════════════════════════════════════

void Game::loadServerPresets() {
    serverPresets_.clear();
    FILE* f = fopen("presets.txt", "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char name[128], gmId[128];
        int maxP = 8, port = 7777, mapIdx = 0;
        int isPvp = 0, ffir = 0, teams = 2, mw = 200, mh = 200, pHp = 100, lives = 0, livesShared = 0, waves = 10, waveCount = 10;
        float ehp = 1.0f, espd = 1.0f, spawnR = 1.0f, crate = 30.0f, pvpMatchDur = 0.0f;
        int n = sscanf(line, "%127[^|]|%127[^|]|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%f|%d|%d|%d|%d|%f|%f",
                       name, gmId, &maxP, &port, &mapIdx,
                       &isPvp, &ffir, &teams, &mw, &mh,
                       &ehp, &espd, &spawnR, &pHp, &lives, &livesShared, &waveCount, &crate, &pvpMatchDur);
        if (n >= 2) {
            ServerPreset p;
            p.name = name;
            p.gamemodeId = gmId;
            p.maxPlayers = maxP;
            p.hostPort = port;
            p.mapIndex = mapIdx;
            if (n >= 18) {
                p.lobbySettings.isPvp        = (bool)isPvp;
                p.lobbySettings.friendlyFire = (bool)ffir;
                p.lobbySettings.teamCount    = teams;
                p.lobbySettings.mapWidth     = mw;
                p.lobbySettings.mapHeight    = mh;
                p.lobbySettings.enemyHpScale    = ehp;
                p.lobbySettings.enemySpeedScale = espd;
                p.lobbySettings.spawnRateScale  = spawnR;
                p.lobbySettings.playerMaxHp  = pHp;
                p.lobbySettings.livesPerPlayer = lives;
                p.lobbySettings.livesShared  = (bool)livesShared;
                p.lobbySettings.waveCount    = waveCount;
                p.lobbySettings.crateInterval = crate;
            }
            if (n >= 19) {
                p.lobbySettings.pvpMatchDuration = pvpMatchDur;
            }
            serverPresets_.push_back(p);
        }
    }
    fclose(f);
    printf("Loaded %d server presets\n", (int)serverPresets_.size());
}

void Game::saveServerPresets() {
    FILE* f = fopen("presets.txt", "w");
    if (!f) { printf("Failed to save presets\n"); return; }
    for (auto& p : serverPresets_) {
        const auto& ls = p.lobbySettings;
        fprintf(f, "%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|%.4f|%.4f|%.4f|%d|%d|%d|%d|%.4f|%.4f\n",
                p.name.c_str(), p.gamemodeId.c_str(),
                p.maxPlayers, p.hostPort, p.mapIndex,
                (int)ls.isPvp, (int)ls.friendlyFire, ls.teamCount,
                ls.mapWidth, ls.mapHeight,
                ls.enemyHpScale, ls.enemySpeedScale, ls.spawnRateScale,
                ls.playerMaxHp, ls.livesPerPlayer, (int)ls.livesShared,
                ls.waveCount, ls.crateInterval, ls.pvpMatchDuration);
    }
    fclose(f);
    printf("Saved %d presets\n", (int)serverPresets_.size());
}

void Game::addServerPreset(const std::string& name, const std::string& gamemodeId, int maxPlayers, int port, int mapIdx, const LobbySettings& ls) {
    ServerPreset p;
    p.name = name;
    p.gamemodeId = gamemodeId;
    p.maxPlayers = maxPlayers;
    p.hostPort = port;
    p.mapIndex = mapIdx;
    p.lobbySettings = ls;
    serverPresets_.push_back(p);
    saveServerPresets();
}

void Game::removeServerPreset(int idx) {
    if (idx >= 0 && idx < (int)serverPresets_.size()) {
        serverPresets_.erase(serverPresets_.begin() + idx);
        saveServerPresets();
    }
}

void Game::applyServerPreset(int idx) {
    if (idx < 0 || idx >= (int)serverPresets_.size()) return;
    auto& p = serverPresets_[idx];
    auto& reg = GameModeRegistry::instance();
    auto& modes = reg.all();
    for (int i = 0; i < (int)modes.size(); i++) {
        if (modes[i].id == p.gamemodeId) {
            gamemodeSelectIdx_ = i;
            break;
        }
    }
    hostMaxPlayers_ = p.maxPlayers;
    hostPort_ = p.hostPort;
    hostMapSelectIdx_ = p.mapIndex;
    lobbySettings_ = p.lobbySettings;
    lobbySettings_.maxPlayers = p.maxPlayers;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Map Pack / Campaign
// ═════════════════════════════════════════════════════════════════════════════

void Game::scanMapPacks() {
    availablePacks_.clear();
    // Scan several possible directories
    std::vector<std::string> dirs = {"packs", "maps/packs", "romfs/packs", "romfs:/packs"};
    for (auto& d : dirs) {
        auto found = ::scanMapPacks(d);
        for (auto& p : found) availablePacks_.push_back(std::move(p));
    }
    printf("Found %d map pack(s)\n", (int)availablePacks_.size());
}

void Game::renderPackSelectMenu() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color blue   = {80, 200, 255, 255};

    drawTextCentered("MAP PACKS", SCREEN_H / 8, 36, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 80, SCREEN_H / 8 + 44, 160, 1};
    SDL_RenderFillRect(renderer_, &tl);

    if (availablePacks_.empty()) {
        drawTextCentered("No packs found", SCREEN_H / 2 - 10, 20, gray);
        drawTextCentered("Place .cspack files in packs/ folder", SCREEN_H / 2 + 20, 14, {80, 80, 90, 255});
    }

    int baseY = SCREEN_H / 4 + 20;
    int stepY = 52;
    for (int i = 0; i < (int)availablePacks_.size(); i++) {
        int y = baseY + i * stepY;
        bool sel = (packSelectIdx_ == i);
        std::string label = availablePacks_[i].name;
        if (!availablePacks_[i].creator.empty()) label += " by " + availablePacks_[i].creator;
        label += " (" + std::to_string(availablePacks_[i].maps.size()) + " levels)";
        if (ui_.menuItem(i, label.c_str(), SCREEN_W / 2, y, 520, 42,
                         blue, sel, 18, 20)) {
            packSelectIdx_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) packSelectIdx_ = i;
        if (sel && !availablePacks_[i].description.empty()) {
            drawTextCentered(availablePacks_[i].description.c_str(), y + 28, 12, {80, 80, 90, 255});
        }
    }

    // BACK option
    int backIdx = (int)availablePacks_.size();
    bool backSel = (packSelectIdx_ == backIdx);
    int backY = baseY + backIdx * stepY + 10;
    if (ui_.menuItem(63, "BACK", SCREEN_W / 2, backY, 200, 32,
                     UI::Color::White, backSel, 20, 22)) {
        packSelectIdx_ = backIdx;
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 63 && !usingGamepad_) packSelectIdx_ = backIdx;

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Select"}, {UI::Action::Back, "Back"} };
      ui_.drawHintBar(hints, 2); }
}

void Game::startPackLevel() {
    std::string path = currentPack_.currentMapPath();
    if (path.empty()) {
        printf("Pack level path empty!\n");
        state_ = GameState::PackComplete;
        return;
    }

    if (!customMap_.loadFromFile(path)) {
        printf("Failed to load pack level: %s\n", path.c_str());
        state_ = GameState::PackComplete;
        return;
    }

    state_ = GameState::PlayingPack;
    playingCustomMap_ = true;
    customGoalOpen_ = false;
    gameTime_ = 0;

    map_.width  = customMap_.width;
    map_.height = customMap_.height;
    map_.tiles  = customMap_.tiles;
    map_.ceiling = customMap_.ceiling;

    enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
    bombs_.clear(); explosions_.clear(); debris_.clear();
    blood_.clear(); boxFragments_.clear();
    crates_.clear(); pickups_.clear();
    upgrades_.reset();
    sandboxMode_ = false;
    crateSpawnTimer_ = 20.0f;

    waveNumber_ = 0; waveEnemiesLeft_ = 0; waveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE; waveSpawnTimer_ = 0;

    player_ = Player{};
    player_.maxHp = config_.playerMaxHp;
    player_.hp = config_.playerMaxHp;
    player_.bombCount = 1;

    MapTrigger* startT = customMap_.findStartTrigger();
    if (startT) player_.pos = {startT->x, startT->y};
    else player_.pos = {map_.worldWidth()/2.f, map_.worldHeight()/2.f};

    camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
    camera_.worldW = map_.worldWidth();
    camera_.worldH = map_.worldHeight();

    customEnemiesTotal_ = 0;
    for (auto& es : customMap_.enemySpawns) {
        if (isCrateSpawnType(es.enemyType)) {
            PickupCrate crate;
            crate.pos = {es.x, es.y};
            crate.contents = rollRandomUpgrade();
            crates_.push_back(crate);
        } else {
            spawnEnemy({es.x, es.y}, enemyTypeFromSpawnId(es.enemyType));
            customEnemiesTotal_++;
        }
    }
    map_.findSpawnPoints();

    {
        // Pack-entry music overrides map-embedded music
        const std::string& packTrack = currentPack_.maps[currentPack_.currentMapIndex].musicPath;
        const std::string& mapTrack  = customMap_.musicPath;
        playMapMusic(currentPack_.folder, packTrack.empty() ? mapTrack : packTrack);
    }

    printf("Pack level %d/%d: %s\n", currentPack_.currentMapIndex + 1,
           (int)currentPack_.maps.size(), path.c_str());
}

void Game::advancePackLevel() {
    if (currentPack_.currentMapIndex < (int)currentPack_.maps.size())
        currentPack_.maps[currentPack_.currentMapIndex].completed = true;

    if (currentPack_.advance()) {
        startPackLevel();
    } else {
        state_ = GameState::PackComplete;
        menuSelection_ = 0;
        playMenuMusic();
    }
}

void Game::renderPackLevelWin() {
    // Dark overlay
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 200);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color green  = {50, 255, 100, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color red    = {255, 100, 100, 255};

    // Panel
    int panelW = 400, panelH = 260;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    SDL_SetRenderDrawColor(renderer_, 10, 14, 12, 240);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 60);
    SDL_RenderDrawRect(renderer_, &panel);

    drawTextCentered("LEVEL COMPLETE!", py + 24, 32, green);
    SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 40);
    SDL_Rect sep = {px + 40, py + 62, panelW - 80, 1};
    SDL_RenderFillRect(renderer_, &sep);

    std::string prog = "Level " + std::to_string(currentPack_.currentMapIndex + 1) +
                       " / " + std::to_string(currentPack_.maps.size());
    drawTextCentered(prog.c_str(), py + 80, 18, gray);

    struct WinItem { const char* label; SDL_Color accent; };
    const char* nextLabel = currentPack_.hasNextMap() ? "NEXT LEVEL" : "FINISH";
    WinItem items[] = { {nextLabel, green}, {"QUIT PACK", red} };
    int itemY = py + 126;
    for (int i = 0; i < 2; i++) {
        bool sel = (menuSelection_ == i);
        if (ui_.menuItem(i, items[i].label, SCREEN_W / 2, itemY, panelW - 40, 36,
                         items[i].accent, sel, 20, 22)) {
            menuSelection_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) menuSelection_ = i;
        itemY += 48;
    }

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Select"} };
      ui_.drawHintBar(hints, 1, py + panelH - 26); }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Local Co-op (splitscreen, up to 4 players)
// ═════════════════════════════════════════════════════════════════════════════

// Splitscreen zoom-out: each viewport sees 1/SPLITSCREEN_ZOOM times more world.
// SDL_RenderSetScale is applied inside each viewport so HUD stays at 1:1.
static constexpr float SPLITSCREEN_ZOOM = 0.75f;

SDL_Rect Game::coopViewport(int idx, int n) const {
    if (n <= 1) return {0, 0, SCREEN_W, SCREEN_H};
    if (n == 2) return {idx * SCREEN_W/2, 0, SCREEN_W/2, SCREEN_H};
    // 3 players: top-left, top-right, bottom full
    if (n == 3 && idx == 2) return {0, SCREEN_H/2, SCREEN_W, SCREEN_H/2};
    int col = idx % 2, row = idx / 2;
    return {col * SCREEN_W/2, row * SCREEN_H/2, SCREEN_W/2, SCREEN_H/2};
}

void Game::startLocalCoopGame() {
    state_ = GameState::LocalCoopGame;
    gameTime_ = 0;
    matchElapsed_ = 0.0f;
    discordSessionStart_ = (int64_t)time(nullptr);
    spectatorMode_ = false;
    
    // Apply lobby settings to config and rules (like multiplayer)
    config_.playerMaxHp    = lobbySettings_.playerMaxHp;
    currentRules_.friendlyFire  = lobbySettings_.friendlyFire;
    currentRules_.pvpEnabled    = lobbySettings_.isPvp || lobbySettings_.pvpEnabled;
    currentRules_.lives         = lobbySettings_.livesPerPlayer;
    currentRules_.sharedLives   = lobbySettings_.livesShared;
    currentRules_.respawnTime    = 3.0f;
    
    // Initialize lives tracking
    if (currentRules_.lives > 0 && !currentRules_.sharedLives) {
        localLives_ = currentRules_.lives;
    } else {
        localLives_ = -1;
    }
    if (currentRules_.lives > 0 && currentRules_.sharedLives) {
        sharedLives_ = currentRules_.lives * coopPlayerCount_;
    } else {
        sharedLives_ = -1;
    }
    
    waveNumber_ = 0; waveEnemiesLeft_ = 0; waveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE; waveSpawnTimer_ = 0;
    enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
    bombs_.clear(); explosions_.clear(); debris_.clear();
    blood_.clear(); boxFragments_.clear();
    crates_.clear(); pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    sandboxMode_ = false;
    map_.generate(config_.mapWidth, config_.mapHeight);
    invalidateMinimapCache();
    map_.findSpawnPoints();

    // Viewport dimensions per player count (divide by SPLITSCREEN_ZOOM so camera sees more world)
    const int n  = coopPlayerCount_;
    const int vw = (n >= 2) ? (int)((SCREEN_W/2) / SPLITSCREEN_ZOOM) : SCREEN_W;
    const int vh = (n >= 3) ? (int)((SCREEN_H/2) / SPLITSCREEN_ZOOM) : (n >= 2) ? (int)(SCREEN_H / SPLITSCREEN_ZOOM) : SCREEN_H;
    const Vec2 base = {map_.worldWidth()/2.f, map_.worldHeight()/2.f};
    const Vec2 off[4] = {{-80,0},{80,0},{0,-80},{0,80}};

    int si = 0;
    int gpNum = 1;
    for (int i = 0; i < 4; i++) {
        if (!coopSlots_[i].joined) continue;
        coopSlots_[i].player = Player{};
        coopSlots_[i].player.maxHp     = config_.playerMaxHp;
        coopSlots_[i].player.hp        = config_.playerMaxHp;
        coopSlots_[i].player.bombCount = 1;
        coopSlots_[i].kills  = 0;
        coopSlots_[i].deaths = 0;
        // Usernames: P1 = config username, others = pc-N
        if (coopSlots_[i].joyInstanceId < 0) {
            coopSlots_[i].username = config_.username;
        } else {
            char uname[16]; snprintf(uname, sizeof(uname), "pc-%d", gpNum++);
            coopSlots_[i].username = uname;
        }
        Vec2 sp = base + off[si % 4];
        if (!map_.spawnPoints.empty())
            sp = map_.spawnPoints[si % (int)map_.spawnPoints.size()];
        coopSlots_[i].player.pos = sp;
        coopSlots_[i].respawnTimer = 0;
        coopSlots_[i].upgrades.reset();
        coopSlots_[i].camera.worldW = map_.worldWidth();
        coopSlots_[i].camera.worldH = map_.worldHeight();
        coopSlots_[i].camera.viewW  = vw;
        coopSlots_[i].camera.viewH  = vh;
        coopSlots_[i].camera.pos    = {sp.x - vw/2.f, sp.y - vh/2.f};
        si++;
    }
    // Sync global state with first joined slot
    for (int i = 0; i < 4; i++) {
        if (coopSlots_[i].joined) {
            player_   = coopSlots_[i].player;
            camera_   = coopSlots_[i].camera;
            upgrades_ = coopSlots_[i].upgrades;
            break;
        }
    }
    respawnTimer_ = currentRules_.respawnTime;
    if (bgMusic_) { Mix_PlayMusic(bgMusic_, -1); Mix_VolumeMusic(config_.musicVolume); }
}

void Game::handleLocalCoopInput() {
#ifdef __SWITCH__
    // On Switch, all slots can be gamepads
    const float dead = 0.18f;
    for (int i = 0; i < 4; i++) {
        if (!coopSlots_[i].joined) continue;
        SDL_GameController* gc = SDL_GameControllerFromInstanceID(coopSlots_[i].joyInstanceId);
        if (!gc) continue;
        float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX)      / 32767.f;
        float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY)      / 32767.f;
        float rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX)     / 32767.f;
        float ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY)     / 32767.f;
        float rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)/ 32767.f;
        if (fabsf(lx) < dead) lx = 0; if (fabsf(ly) < dead) ly = 0;
        if (fabsf(rx) < dead) rx = 0; if (fabsf(ry) < dead) ry = 0;
        coopSlots_[i].moveInput  = {lx, ly};
        if (rx != 0.0f || ry != 0.0f) coopSlots_[i].aimInput = {rx, ry};
        coopSlots_[i].fireInput  = (rt > 0.25f);
        coopSlots_[i].bombInput  = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X);
        coopSlots_[i].parryInput = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        coopSlots_[i].meleeInput = false;  // no dedicated quick-melee on gamepad; use axe slot
        coopSlots_[i].weaponSwitchInput = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 1 : 0;
        coopSlots_[i].pauseInput = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START);
        if (coopSlots_[i].pauseInput && state_ == GameState::LocalCoopGame) {
            state_ = GameState::LocalCoopPaused;
            menuSelection_ = 0;
        }
    }
#else
    const float dead = 0.18f;
    auto readPadIntoSlot = [&](int i, SDL_GameController* gc) {
        if (!gc) return false;
        float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX)      / 32767.f;
        float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY)      / 32767.f;
        float rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX)     / 32767.f;
        float ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY)     / 32767.f;
        float rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)/ 32767.f;
        if (fabsf(lx) < dead) lx = 0; if (fabsf(ly) < dead) ly = 0;
        if (fabsf(rx) < dead) rx = 0; if (fabsf(ry) < dead) ry = 0;
        coopSlots_[i].moveInput  = {lx, ly};
        if (rx != 0.0f || ry != 0.0f) coopSlots_[i].aimInput = {rx, ry};
        coopSlots_[i].fireInput  = (rt > 0.25f);
        coopSlots_[i].bombInput  = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X);
        coopSlots_[i].parryInput = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        coopSlots_[i].meleeInput = false;
        coopSlots_[i].weaponSwitchInput = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 1 : 0;
        coopSlots_[i].pauseInput = (bool)SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START);
        if (coopSlots_[i].pauseInput && state_ == GameState::LocalCoopGame) {
            state_ = GameState::LocalCoopPaused;
            menuSelection_ = 0;
        }
        return true;
    };

    bool slot0PadRead = false;
    if (coopSlots_[0].joined && coopSlots_[0].joyInstanceId >= 0) {
        slot0PadRead = readPadIntoSlot(0, SDL_GameControllerFromInstanceID(coopSlots_[0].joyInstanceId));
    }
    if (!slot0PadRead) {
        coopSlots_[0].moveInput       = moveInput_;
        coopSlots_[0].aimInput        = aimInput_;
        coopSlots_[0].fireInput       = fireInput_;
        coopSlots_[0].bombInput       = bombInput_;
        coopSlots_[0].parryInput      = parryInput_;
        coopSlots_[0].meleeInput      = meleeInput_;
        coopSlots_[0].weaponSwitchInput = weaponSwitchDelta_;
        coopSlots_[0].bombLaunchInput = bombLaunchInput_;
        coopSlots_[0].bombLaunchHeld  = bombLaunchHeld_;
    }

    // Slots 1-3 = gamepads: read from their assigned controller by joystick instance ID
    for (int i = 1; i < 4; i++) {
        if (!coopSlots_[i].joined) continue;
        readPadIntoSlot(i, SDL_GameControllerFromInstanceID(coopSlots_[i].joyInstanceId));
    }
#endif
}

void Game::updateLocalCoopPlayers(float dt) {
    handleLocalCoopInput();
    for (int i = 0; i < 4; i++) {
        if (!coopSlots_[i].joined) continue;
        Player         savedPlayer  = player_;
        PlayerUpgrades savedUpg     = upgrades_;
        Vec2  savedMove = moveInput_, savedAim = aimInput_;
        bool  savedFire = fireInput_, savedBomb = bombInput_, savedParry = parryInput_, savedMelee = meleeInput_;
        bool  savedBombLaunch = bombLaunchInput_;  // prevent P1's ZL from firing all slots' bombs
        int   savedWeaponSwitch = weaponSwitchDelta_;

        player_    = coopSlots_[i].player;
        upgrades_  = coopSlots_[i].upgrades;
        moveInput_ = coopSlots_[i].moveInput;
        aimInput_  = coopSlots_[i].aimInput;
        fireInput_ = coopSlots_[i].fireInput;
        bombInput_ = coopSlots_[i].bombInput;
        bombLaunchInput_ = coopSlots_[i].bombLaunchInput;  // per-slot ZL
        parryInput_= coopSlots_[i].parryInput;
        meleeInput_= coopSlots_[i].meleeInput;
        weaponSwitchDelta_ = coopSlots_[i].weaponSwitchInput;
        activeLocalPlayerSlot_ = i;

        updatePlayer(dt);

        coopSlots_[i].player   = player_;
        coopSlots_[i].upgrades = upgrades_;

        // Check if player just died and set respawn timer.
        // Must run AFTER write-back so we read the updated dead flag;
        // otherwise the respawn loop sees dead=true + timer=0 on the
        // same frame and fires an immediate respawn.
        if (coopSlots_[i].player.dead && coopSlots_[i].respawnTimer == 0) {
            coopSlots_[i].respawnTimer = currentRules_.respawnTime;
        }

        Vec2 aimDir = {};
        if (coopSlots_[i].aimInput.lengthSq() > 0.04f)
            aimDir = coopSlots_[i].aimInput.normalized();
        else if (coopSlots_[i].player.moving && coopSlots_[i].player.vel.lengthSq() > 1.f)
            aimDir = coopSlots_[i].player.vel.normalized();
        coopSlots_[i].camera.update(coopSlots_[i].player.pos, aimDir, dt);

        player_    = savedPlayer;  upgrades_  = savedUpg;
        moveInput_ = savedMove;    aimInput_  = savedAim;
        fireInput_ = savedFire;    bombInput_ = savedBomb;  parryInput_ = savedParry;  meleeInput_ = savedMelee;
        bombLaunchInput_ = savedBombLaunch;
        weaponSwitchDelta_ = savedWeaponSwitch;
        activeLocalPlayerSlot_ = 0;
    }
    // Sync primary state to first joined slot
    for (int i = 0; i < 4; i++) {
        if (coopSlots_[i].joined) {
            player_   = coopSlots_[i].player;
            camera_   = coopSlots_[i].camera;
            upgrades_ = coopSlots_[i].upgrades;
            break;
        }
    }
    
    // Handle respawning for dead players (like multiplayer)
    for (int i = 0; i < 4; i++) {
        if (!coopSlots_[i].joined) continue;
        if (coopSlots_[i].player.dead) {
            coopSlots_[i].respawnTimer -= dt;
            if (coopSlots_[i].respawnTimer <= 0) {
                // Check lives
                bool canRespawn = true;
                if (currentRules_.lives > 0) {
                    if (currentRules_.sharedLives) {
                        if (sharedLives_ > 0) {
                            sharedLives_--;
                        } else {
                            canRespawn = false;
                        }
                    } else {
                        if (localLives_ > 0) {
                            localLives_--;
                        } else {
                            canRespawn = false;
                        }
                    }
                }
                
                if (canRespawn) {
                    // Respawn player
                    Vec2 sp;
                    if (!map_.spawnPoints.empty())
                        sp = map_.spawnPoints[rand() % (int)map_.spawnPoints.size()];
                    else
                        sp = {map_.worldWidth()/2.f, map_.worldHeight()/2.f};
                    coopSlots_[i].player = Player{};
                    coopSlots_[i].player.pos = sp;
                    coopSlots_[i].player.maxHp = config_.playerMaxHp;
                    coopSlots_[i].player.hp = config_.playerMaxHp;
                    coopSlots_[i].player.bombCount = 1;
                    coopSlots_[i].player.invulnerable = true;
                    coopSlots_[i].player.invulnTimer = 3.0f;
                    coopSlots_[i].respawnTimer = 0;
                }
            }
        }
    }

    // Re-sync primary player state after potential respawns.
    // Without this, the outer update() sync at line ~3263 would overwrite
    // coopSlots_[0].player with the still-dead player_ state, undoing every respawn.
    for (int i = 0; i < 4; i++) {
        if (coopSlots_[i].joined) {
            player_   = coopSlots_[i].player;
            camera_   = coopSlots_[i].camera;
            upgrades_ = coopSlots_[i].upgrades;
            break;
        }
    }

    // All dead + no lives → game over
    bool anyAlive = false;
    bool anyCanRespawn = false;
    for (int i = 0; i < 4; i++) {
        if (!coopSlots_[i].joined) continue;
        if (!coopSlots_[i].player.dead) anyAlive = true;
        if (coopSlots_[i].player.dead && coopSlots_[i].respawnTimer > 0) anyCanRespawn = true;
    }
    
    bool hasLives = (currentRules_.lives == 0) || 
                   (currentRules_.sharedLives && sharedLives_ > 0) ||
                   (!currentRules_.sharedLives && localLives_ > 0);
    
    if (!anyAlive && !anyCanRespawn && !hasLives && state_ == GameState::LocalCoopGame) {
        state_ = GameState::LocalCoopDead;
        menuSelection_ = 0;
    }
}

void Game::renderLocalCoopLobby() {
    ui_.drawDarkOverlay(235, 8, 8, 12);
    ui_.drawTextCentered("LOCAL MULTIPLAYER", 40, 36, UI::Color::Cyan);
    ui_.drawSeparator(SCREEN_W/2, 82, 100);

    static const SDL_Color slotColors[4] = {
        {80, 220, 255, 255}, {255, 220, 50, 255},
        {80, 220, 80,  255}, {255, 140, 50, 255}};
    static const char* slotLabels[4] = {"P1","P2","P3","P4"};
    const int slotW = 200, slotH = 110, gapX = 12;
    const int totalW = 4*slotW + 3*gapX;
    const int startX = (SCREEN_W - totalW) / 2;
    const int slotY  = 100;
    char buf[128];

    for (int i = 0; i < 4; i++) {
        int x = startX + i*(slotW+gapX);
        SDL_Color col = slotColors[i];
        SDL_Rect box = {x, slotY, slotW, slotH};
        SDL_SetRenderDrawColor(renderer_,
            coopSlots_[i].joined ? col.r/4 : 22,
            coopSlots_[i].joined ? col.g/4 : 22,
            coopSlots_[i].joined ? col.b/4 : 28, 220);
        SDL_RenderFillRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b,
                               coopSlots_[i].joined ? 255 : 50);
        SDL_RenderDrawRect(renderer_, &box);
        drawText(slotLabels[i], x + slotW/2 - 10, slotY + 8, 22, col);
        if (coopSlots_[i].joined) {
            // Username
            drawText(coopSlots_[i].username.c_str(), x + 10, slotY + 40, 16, {255,255,255,255});
            // Input device
            if (coopSlots_[i].joyInstanceId < 0) {
                drawText("Keyboard + Mouse", x + 10, slotY + 64, 13, col);
            } else {
                SDL_GameController* gc = SDL_GameControllerFromInstanceID(coopSlots_[i].joyInstanceId);
                const char* gcName = gc ? SDL_GameControllerName(gc) : "Gamepad";
                // Truncate long names
                char devBuf[40]; snprintf(devBuf, sizeof(devBuf), "%.38s", gcName);
                drawText(devBuf, x + 10, slotY + 64, 12, col);
            }
            // Status
            drawText("READY", x + slotW/2 - 24, slotY + 100, 16, {80,255,80,255});
        } else {
            SDL_Color dim = {(Uint8)(col.r/2),(Uint8)(col.g/2),(Uint8)(col.b/2),255};
            if (i == 0)
                drawText("Auto-joined", x + 10, slotY + 56, 13, dim);
            else
                drawText("Press A on gamepad", x + 10, slotY + 50, 13, dim);
            drawText("to join", x + 10, slotY + 68, 13, dim);
        }
    }
    int joined = 0;
    for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;

    snprintf(buf, sizeof(buf), "%d player%s", joined, joined == 1 ? "" : "s");
    drawTextCentered(buf, slotY + slotH + 16, 15, joined >= 1 ? UI::Color::Green : UI::Color::HintGray);

    // Game settings panel
    int settingsY = slotY + slotH + 48;
    drawText("GAME SETTINGS:", 50, settingsY, 16, UI::Color::Cyan);
    settingsY += 28;

    auto drawSetting = [&](const char* label, const char* value) {
        drawText(label, 50, settingsY, 14, UI::Color::White);
        drawText(value, 350, settingsY, 14, UI::Color::Yellow);
        settingsY += 24;
    };

    snprintf(buf, sizeof(buf), "%s", lobbySettings_.isPvp ? "PVP" : "CO-OP");
    drawSetting("Mode:", buf);

    if (lobbySettings_.isPvp && lobbySettings_.teamCount > 0) {
        snprintf(buf, sizeof(buf), "%s", lobbySettings_.friendlyFire ? "ON" : "OFF");
        drawSetting("Friendly Fire:", buf);
    }

    if (lobbySettings_.livesPerPlayer > 0) {
        snprintf(buf, sizeof(buf), "%d %s", lobbySettings_.livesPerPlayer, 
                 lobbySettings_.livesShared ? "(shared)" : "(each)");
    } else {
        snprintf(buf, sizeof(buf), "Infinite");
    }
    drawSetting("Lives:", buf);

    snprintf(buf, sizeof(buf), "%d HP", lobbySettings_.playerMaxHp);
    drawSetting("Player HP:", buf);

    drawTextCentered("Press TAB to cycle mode  |  START to begin  |  B/Esc to cancel",
                     settingsY + 12, 13, UI::Color::HintGray);

    UI::HintPair hints[] = {
        {UI::Action::Tab,     "Cycle Mode"},
        {UI::Action::Confirm, "Start"},
        {UI::Action::Back,    "Back"},
    };
    ui_.drawHintBar(hints, 3);
}

void Game::renderLocalCoopGame() {
    // LocalCoopGame is unreachable (no menu entry transitions to that state).
    // Delegate to the multiplayer splitscreen renderer which is the live path.
    renderMultiplayerSplitscreen();
}

void Game::renderPackComplete() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color gold   = {255, 220, 50, 255};
    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color white  = {255, 255, 255, 255};

    // Decorative panel
    int panelW = 440, panelH = 280;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    SDL_SetRenderDrawColor(renderer_, 14, 14, 24, 240);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 255, 200, 50, 60);
    SDL_RenderDrawRect(renderer_, &panel);

    drawTextCentered("CAMPAIGN COMPLETE!", py + 28, 34, gold);
    SDL_SetRenderDrawColor(renderer_, 255, 200, 50, 40);
    SDL_Rect sep = {px + 40, py + 68, panelW - 80, 1};
    SDL_RenderFillRect(renderer_, &sep);

    drawTextCentered(currentPack_.name.c_str(), py + 88, 22, cyan);

    if (!currentPack_.creator.empty()) {
        std::string byLine = "by " + currentPack_.creator;
        drawTextCentered(byLine.c_str(), py + 116, 14, gray);
    }

    std::string levels = std::to_string(currentPack_.maps.size()) + " levels completed!";
    drawTextCentered(levels.c_str(), py + 152, 18, gray);

    // Continue button
    if (ui_.menuItem(0, "CONTINUE", SCREEN_W / 2, py + 200, panelW - 60, 36,
                     UI::Color::Yellow, true, 22, 24)) {
        confirmInput_ = true;
    }

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Continue"} };
      ui_.drawHintBar(hints, 1, py + panelH - 22); }

}

// ═════════════════════════════════════════════════════════════════════════════
//  Multiplayer Splitscreen Rendering
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderMultiplayerSplitscreen() {
    static const SDL_Color pColors[4] = {
        {255,255,255,255},{255,220,50,255},{80,220,80,255},{255,140,50,255}};
    const int n = coopPlayerCount_;

    Player         savedPlayer = player_;
    Camera         savedCam    = camera_;
    PlayerUpgrades savedUpg    = upgrades_;

    int slotOrder = 0;
    for (int i = 0; i < 4; i++) {
        if (!coopSlots_[i].joined) continue;
        SDL_Rect vp = coopViewport(slotOrder++, n);
        SDL_RenderSetViewport(renderer_, &vp);
        // Zoom out: scale down draw coordinates so the larger camera view area fills the viewport
        if (n > 1) SDL_RenderSetScale(renderer_, SPLITSCREEN_ZOOM, SPLITSCREEN_ZOOM);

        auto drawViewportCentered = [&](const char* text, int y, int size, SDL_Color color) {
            int tw = ui_.textWidth(text, size);
            drawText(text, std::max(6, (vp.w - tw) / 2), y, size, color);
        };

        camera_   = coopSlots_[i].camera;
        player_   = coopSlots_[i].player;
        upgrades_ = coopSlots_[i].upgrades;

        // ── World rendering ──
        renderMap();
        renderDecals();

        for (auto& b : bombs_) {
            if (!b.alive) continue;
            SDL_Texture* tex = bombSprites_.empty() ? nullptr : bombSprites_[b.animFrame % bombSprites_.size()];
            if (tex) renderSprite(tex, b.pos, 0, 2.f);
        }
        for (auto& ex : explosions_) { if (ex.alive) renderExplosionPixelated(ex); }

        // Enemies
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            SDL_Texture* etex = enemySpriteTex(e.type);
            if (!etex) continue;
            float ds = e.renderScale;
            if (e.damageFlash > 0 || e.flashTimer > 0) {
                float fl = std::min(1.f, e.damageFlash + e.flashTimer);
                Uint8 oth = (Uint8)(15.f*(1.f-fl));
                renderSpriteEx(etex, e.pos, e.rotation+(float)M_PI/2, ds, {255,oth,oth,255});
            } else {
                float r = e.maxHp > 0 ? e.hp/e.maxHp : 1.f;
                SDL_Color base = enemyBaseTint(e.type);
                renderSpriteEx(etex, e.pos, e.rotation+(float)M_PI/2, ds,
                               {base.r,(Uint8)(base.g*r),(Uint8)(base.b*r),255});
            }
        }

        // Box fragments — fade out and shrink as they age
        for (auto& f : boxFragments_) {
            if (!f.alive) continue;
            float frac = f.age / f.lifetime;
            Uint8 a    = (Uint8)((1.f - frac) * 220.f);
            float s    = f.size * (1.f - frac * 0.3f);
            Vec2 sp = camera_.worldToScreen(f.pos);
            SDL_SetRenderDrawColor(renderer_, f.color.r, f.color.g, f.color.b, a);
            SDL_Rect r = {(int)(sp.x - s/2), (int)(sp.y - s/2), (int)s, (int)s};
            SDL_RenderFillRect(renderer_, &r);
        }

        // ── Render ALL local co-op players in this viewport ──
        for (int j = 0; j < 4; j++) {
            if (!coopSlots_[j].joined) continue;
            Player& cp = coopSlots_[j].player;
            if (cp.dead) continue;

            // Legs
            if (!legSprites_.empty() && cp.moving) {
                int idx = cp.legAnimFrame % (int)legSprites_.size();
                renderSprite(legSprites_[idx], cp.pos, cp.legRotation + (float)M_PI/2, 1.5f);
            }
            // Body — tint by slot color; flash white when invulnerable, blue when parrying
            if (!playerSprites_.empty()) {
                int idx = cp.animFrame % (int)playerSprites_.size();
                Vec2 bodyPos = cp.pos + Vec2::fromAngle(cp.rotation) * 6.f;
                SDL_Color tint = pColors[j];
                if (cp.invulnerable && ((int)(cp.invulnTimer * 10) % 2 == 0))
                    tint = {255, 255, 255, 128};
                else if (cp.isParrying)
                    tint = {128, 200, 255, 255};
                renderSpriteEx(playerSprites_[idx], bodyPos, cp.rotation + (float)M_PI/2, 1.5f, tint);
            }
        }

        // ── Remote players (other network clients + their sub-players) ──
        renderRemotePlayers();

        // Bullets (player)
        for (auto& b : bullets_) {
            if (!b.alive) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                SDL_SetRenderDrawColor(renderer_, 255, 255, 140, (Uint8)(fade * 180));
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
        }

        // Enemy bullets
        for (auto& b : enemyBullets_) {
            if (!b.alive) continue;
            Vec2 backDir = b.vel.normalized() * -1.0f;
            for (int t = 1; t <= 6; t++) {
                Vec2 tp = b.pos + backDir * (float)(t * 6);
                Vec2 sp = camera_.worldToScreen(tp);
                float fade = 1.0f - (float)t / 7.0f;
                SDL_SetRenderDrawColor(renderer_, 255, 60, 60, (Uint8)(fade * 180));
                float sz = 3.0f * fade;
                SDL_FRect r = {sp.x - sz, sp.y - sz, sz * 2, sz * 2};
                SDL_RenderFillRectF(renderer_, &r);
            }
            if (b.sprite) {
                SDL_SetTextureColorMod(b.sprite, 255, 80, 80);
                renderSprite(b.sprite, b.pos, b.rotation, 0.7f);
                SDL_SetTextureColorMod(b.sprite, 255, 255, 255);
            }
        }

        renderCrates();
        renderPickups();

        // ── Name tags + HP bars for other local co-op players ──
        for (int j = 0; j < 4; j++) {
            if (!coopSlots_[j].joined || j == i) continue;
            Player& op = coopSlots_[j].player;
            if (op.dead) continue;
            Vec2 sp = camera_.worldToScreen(op.pos);
            if (sp.x < -50 || sp.x > (float)camera_.viewW + 50 ||
                sp.y < -50 || sp.y > (float)camera_.viewH + 50) continue;
            // HP bar
            float barW = 40.f, barH = 4.f;
            float hpR  = (op.maxHp > 0) ? (float)op.hp / op.maxHp : 0.f;
            SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 180);
            SDL_FRect hbg = {sp.x - barW/2, sp.y - 28.f, barW, barH};
            SDL_RenderFillRectF(renderer_, &hbg);
            SDL_Color hbc = hpR > .5f ? SDL_Color{50,220,50,255}
                          : hpR > .25f ? SDL_Color{255,180,0,255}
                          : SDL_Color{220,50,50,255};
            SDL_SetRenderDrawColor(renderer_, hbc.r, hbc.g, hbc.b, 255);
            SDL_FRect hfg = {sp.x - barW/2, sp.y - 28.f, barW * hpR, barH};
            SDL_RenderFillRectF(renderer_, &hfg);
            drawText(coopSlots_[j].username.c_str(), (int)sp.x - 20, (int)sp.y - 44, 11, pColors[j]);
        }

        renderWallOverlay();
        renderRoofOverlay();
        renderShadingPass();

        // ── Crosshair ── must render at zoom scale (worldToScreen coords)
        {
            Player& cp = coopSlots_[i].player;
            Vec2 aimDir = resolveAimDirection(cp, coopSlots_[i].aimInput);
            float crosshairDistance = (i == 0) ? 96.0f : 80.0f;
            renderAimCrosshair(camera_, cp, aimDir, crosshairDistance, pColors[i]);
        }

        // ── Per-slot HUD (bottom of viewport) ── restore 1:1 scale for UI overlay
        if (n > 1) SDL_RenderSetScale(renderer_, 1.0f, 1.0f);
        {
            Player& cp = coopSlots_[i].player;

            char hpStr[32];
            memset(hpStr, 0, sizeof(hpStr));
            for (int h = 0; h < cp.hp && h < 20; h++) hpStr[h] = '|';
            SDL_Color hpColor = (cp.hp <= 1) ? SDL_Color{255, 70, 70, 255} : pColors[i];
            drawText(hpStr, 8, 10, 18, hpColor);

            const char* weaponName = (cp.activeWeapon == 0) ? "GUN" : "AXE";
            drawText(weaponName, 8, 30, 13, UI::Color::Yellow);
            if (cp.activeWeapon == 0) {
                char ammoBuf[32];
                if (cp.reloading) snprintf(ammoBuf, sizeof(ammoBuf), "RELOAD");
                else snprintf(ammoBuf, sizeof(ammoBuf), "%d/%d", cp.ammo, cp.maxAmmo);
                drawText(ammoBuf, 8, 46, 12, UI::Color::White);
            }

            int orbitingBombs = 0;
            for (auto& b : bombs_) {
                if (b.alive && !b.hasDashed && b.ownerId == NetworkManager::instance().localPlayerId() &&
                    b.ownerSubSlot == (uint8_t)i) orbitingBombs++;
            }
            char bombBuf[32];
            snprintf(bombBuf, sizeof(bombBuf), "B:%d", orbitingBombs + cp.bombCount);
            drawText(bombBuf, 8, 62, 12, {255, 180, 50, 255});

            char killBuf[32];
            snprintf(killBuf, sizeof(killBuf), "KILLS %d/%d", cp.killCounter, coopSlots_[i].upgrades.killsPerBomb);
            drawText(killBuf, 8, 78, 10, UI::Color::HintGray);

            char perkBuf[96] = "";
            auto appendPerk = [&](const char* tag) {
                if (perkBuf[0] != '\0') strncat(perkBuf, " ", sizeof(perkBuf) - strlen(perkBuf) - 1);
                strncat(perkBuf, tag, sizeof(perkBuf) - strlen(perkBuf) - 1);
            };
            const PlayerUpgrades& slotUpg = coopSlots_[i].upgrades;
            if (slotUpg.hasTripleShot) appendPerk("TRI");
            if (slotUpg.hasPiercing) appendPerk("PIERCE");
            if (slotUpg.hasRicochet) appendPerk("RICO");
            if (slotUpg.hasMagnet) appendPerk("MAG");
            if (slotUpg.hasExplosiveTips) appendPerk("EXP");
            if (slotUpg.hasStunRounds) appendPerk("STUN");
            if (slotUpg.hasChainLightning) appendPerk("CHAIN");
            if (slotUpg.hasBloodlust) appendPerk("BLOOD");
            if (slotUpg.hasShockEdge) appendPerk("SHOCK");
            if (perkBuf[0] != '\0') {
                int perkW = ui_.textWidth(perkBuf, 10);
                drawText(perkBuf, std::max(8, vp.w - perkW - 8), 10, 10, {170, 230, 255, 220});
            }

            if (cp.invulnerable) {
                float alpha = std::min(0.3f, (cp.invulnTimer / std::max(0.001f, cp.invulnDuration > 0 ? cp.invulnDuration : PLAYER_INVULN_TIME)) * 0.3f);
                SDL_SetRenderDrawColor(renderer_, 255, 0, 0, (Uint8)(alpha * 255));
                SDL_Rect full = {0, 0, vp.w, vp.h};
                SDL_RenderFillRect(renderer_, &full);
            }
            // Screen flash (pickups, explosions, etc.) — renderUI() isn't called in coop so draw it here
            if (screenFlashTimer_ > 0) {
                float a = std::min(0.35f, (screenFlashTimer_ / 0.12f) * 0.35f);
                SDL_SetRenderDrawColor(renderer_, (Uint8)screenFlashR_, (Uint8)screenFlashG_,
                                       (Uint8)screenFlashB_, (Uint8)(a * 255));
                SDL_Rect sff = {0, 0, vp.w, vp.h};
                SDL_RenderFillRect(renderer_, &sff);
            }

            if (!lobbySettings_.isPvp && waveAnnounceTimer_ > 0) {
                float t = waveAnnounceTimer_;
                float alpha = 1.0f;
                if (t > 2.0f) alpha = (2.5f - t) * 2.0f;
                else if (t < 0.5f) alpha = t * 2.0f;
                alpha = fminf(1.0f, fmaxf(0.0f, alpha));
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, (Uint8)(alpha * 180));
                SDL_Rect banner = {0, vp.h/2 - 28, vp.w, 56};
                SDL_RenderFillRect(renderer_, &banner);
                char waveTxt[64];
                snprintf(waveTxt, sizeof(waveTxt), "WAVE %d", waveAnnounceNum_);
                drawViewportCentered(waveTxt, vp.h/2 - 10, 22, {0, 255, 228, (Uint8)(alpha * 255)});
            }

            if (pickupPopupTimer_ > 0) {
                float t = pickupPopupTimer_;
                float alpha = 1.0f;
                if (t > 2.0f) alpha = (2.5f - t) * 2.0f;
                else if (t < 0.5f) alpha = t * 2.0f;
                alpha = fminf(1.0f, fmaxf(0.0f, alpha));
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, (Uint8)(alpha * 150));
                SDL_Rect banner = {0, vp.h/2 + 26, vp.w, 36};
                SDL_RenderFillRect(renderer_, &banner);
                drawViewportCentered(pickupPopupName_.c_str(), vp.h/2 + 34, 14,
                                     {pickupPopupColor_.r, pickupPopupColor_.g, pickupPopupColor_.b, (Uint8)(alpha * 255)});
            }

            if (cp.dead && coopSlots_[i].respawnTimer > 0) {
                char respawnMsg[64];
                snprintf(respawnMsg, sizeof(respawnMsg), "RESPAWNING IN %.1f", coopSlots_[i].respawnTimer);
                drawViewportCentered(respawnMsg, vp.h/2, 22, UI::Color::Yellow);
            } else if (cp.dead) {
                drawViewportCentered("WAITING FOR RESPAWN", vp.h/2, 22, UI::Color::Red);
            }

            int bw = vp.w-20, bh = 8, bx = 10, by = vp.h-20;
            SDL_SetRenderDrawColor(renderer_, 30,30,30,200);
            SDL_Rect bg = {bx,by,bw,bh}; SDL_RenderFillRect(renderer_, &bg);
            float hf = std::max(0.f,(float)cp.hp/cp.maxHp);
            SDL_Color hc = hf>.5f?SDL_Color{50,220,50,255}:hf>.25f?SDL_Color{255,180,0,255}:SDL_Color{220,50,50,255};
            SDL_SetRenderDrawColor(renderer_, hc.r,hc.g,hc.b,255);
            SDL_Rect bar = {bx,by,(int)(bw*hf),bh}; SDL_RenderFillRect(renderer_, &bar);
            char at[32]; snprintf(at,sizeof(at),"%d/%d",cp.ammo,cp.maxAmmo);
            drawText(at, bx, by-20, 14, UI::Color::White);
            char kd[32]; snprintf(kd,sizeof(kd),"K:%d D:%d",coopSlots_[i].kills,coopSlots_[i].deaths);
            drawText(kd, bx, by-38, 12, UI::Color::HintGray);
            char nameTag[64]; snprintf(nameTag,sizeof(nameTag),"%s",coopSlots_[i].username.c_str());
            drawText(nameTag, 6, 6, 14, pColors[i]);
        }
    }

    SDL_RenderSetViewport(renderer_, nullptr);

    // Divider lines between viewports
    SDL_SetRenderDrawColor(renderer_, 0,0,0,255);
    if (n == 2) {
        SDL_RenderDrawLine(renderer_, SCREEN_W/2,   0, SCREEN_W/2,   SCREEN_H);
        SDL_RenderDrawLine(renderer_, SCREEN_W/2+1, 0, SCREEN_W/2+1, SCREEN_H);
    } else if (n >= 3) {
        SDL_RenderDrawLine(renderer_, SCREEN_W/2,   0,         SCREEN_W/2,   SCREEN_H);
        SDL_RenderDrawLine(renderer_, SCREEN_W/2+1, 0,         SCREEN_W/2+1, SCREEN_H);
        SDL_RenderDrawLine(renderer_, 0, SCREEN_H/2,   SCREEN_W, SCREEN_H/2);
        SDL_RenderDrawLine(renderer_, 0, SCREEN_H/2+1, SCREEN_W, SCREEN_H/2+1);
    }

    // ── Minimap ──
    if (n >= 2 && map_.width > 0 && map_.height > 0) {
        const int MMAP_MAX_PX = 140;
        const int MMAP_MARGIN = 6;
        const int MMAP_INNER  = 4;
        int mapW = map_.width, mapH = map_.height;
        int tpx = std::max(1, std::min(MMAP_MAX_PX / mapW, MMAP_MAX_PX / mapH));
        int mmW = mapW * tpx, mmH = mapH * tpx;
        int mmX, mmY;
        if (n == 3) {
            mmX = SCREEN_W/2 + (SCREEN_W/2 - mmW - MMAP_MARGIN);
            mmY = SCREEN_H/2 + (SCREEN_H/2 - mmH - MMAP_MARGIN);
        } else {
            mmX = (SCREEN_W - mmW) / 2;
            mmY = (SCREEN_H - mmH) / 2;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
        SDL_Rect bg = {mmX-MMAP_INNER, mmY-MMAP_INNER, mmW+MMAP_INNER*2, mmH+MMAP_INNER*2};
        SDL_RenderFillRect(renderer_, &bg);
        SDL_SetRenderDrawColor(renderer_, 0, 200, 180, 140);
        SDL_Rect border = {mmX-MMAP_INNER-1, mmY-MMAP_INNER-1, mmW+MMAP_INNER*2+2, mmH+MMAP_INNER*2+2};
        SDL_RenderDrawRect(renderer_, &border);
        for (int ty = 0; ty < mapH; ty++) {
            for (int tx = 0; tx < mapW; tx++) {
                SDL_Rect r = {mmX + tx*tpx, mmY + ty*tpx, tpx, tpx};
                if (map_.isSolid(tx, ty))
                    SDL_SetRenderDrawColor(renderer_, 150, 140, 120, 240);
                else
                    SDL_SetRenderDrawColor(renderer_, 28, 30, 35, 220);
                SDL_RenderFillRect(renderer_, &r);
            }
        }
        float worldW = map_.worldWidth(), worldH = map_.worldHeight();
        // Enemies (red dots)
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            int ex = mmX + (int)(e.pos.x / worldW * mmW);
            int ey = mmY + (int)(e.pos.y / worldH * mmH);
            SDL_SetRenderDrawColor(renderer_, 255, 80, 80, 255);
            SDL_Rect r = {std::max(mmX,std::min(mmX+mmW-1,ex))-1,
                          std::max(mmY,std::min(mmY+mmH-1,ey))-1, 3, 3};
            SDL_RenderFillRect(renderer_, &r);
        }
        // Local co-op players (colored dots)
        for (int i = 0; i < 4; i++) {
            if (!coopSlots_[i].joined || coopSlots_[i].player.dead) continue;
            int px = mmX + (int)(coopSlots_[i].player.pos.x / worldW * mmW);
            int py = mmY + (int)(coopSlots_[i].player.pos.y / worldH * mmH);
            SDL_SetRenderDrawColor(renderer_, pColors[i].r, pColors[i].g, pColors[i].b, 255);
            SDL_Rect r = {std::max(mmX,std::min(mmX+mmW-1,px))-2,
                          std::max(mmY,std::min(mmY+mmH-1,py))-2, 5, 5};
            SDL_RenderFillRect(renderer_, &r);
        }
        // Remote players (blue dots)
        auto& net = NetworkManager::instance();
        const auto& rpPlayers = net.players();
        for (auto& rp : rpPlayers) {
            if (rp.id == net.localPlayerId() || !rp.alive) continue;
            int rx = mmX + (int)(rp.pos.x / worldW * mmW);
            int ry = mmY + (int)(rp.pos.y / worldH * mmH);
            SDL_SetRenderDrawColor(renderer_, 120, 180, 255, 255);
            SDL_Rect r = {std::max(mmX,std::min(mmX+mmW-1,rx))-2,
                          std::max(mmY,std::min(mmY+mmH-1,ry))-2, 5, 5};
            SDL_RenderFillRect(renderer_, &r);
            // Remote sub-players
            for (auto& sp : rp.subPlayers) {
                if (!sp.alive) continue;
                int sx = mmX + (int)(sp.pos.x / worldW * mmW);
                int sy = mmY + (int)(sp.pos.y / worldH * mmH);
                SDL_SetRenderDrawColor(renderer_, 100, 160, 220, 255);
                SDL_Rect sr = {std::max(mmX,std::min(mmX+mmW-1,sx))-1,
                               std::max(mmY,std::min(mmY+mmH-1,sy))-1, 3, 3};
                SDL_RenderFillRect(renderer_, &sr);
            }
        }
    }

    // Wave counter at top center
    char wb[32]; snprintf(wb, sizeof(wb), "WAVE %d", waveNumber_);
    drawTextCentered(wb, 4, 15, UI::Color::Cyan);

    player_   = savedPlayer;
    camera_   = savedCam;
    upgrades_ = savedUpg;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Mod System Integration
// ═════════════════════════════════════════════════════════════════════════════

void Game::initMods() {
    auto& mm = ModManager::instance();
    mm.scanMods();
    mm.loadModConfig();
    mm.loadAllEnabled();
    applyModOverrides();
    printf("Mods initialized (%d loaded)\n", (int)mm.mods().size());
}

void Game::reloadModdedContent() {
    std::string activeCharName = hasActiveChar_ ? activeCharDef_.name : "";
    bool hadActiveChar = hasActiveChar_;

    Mix_HaltMusic();
    for (auto& cd : availableChars_) cd.unload();
    availableChars_.clear();

    Assets::instance().shutdown();
    Assets::instance().init(renderer_);
    loadAssets();

    scanCharacters();
    scanMapFiles();
    scanMapPacks();

    if (hadActiveChar) {
        bool reapplied = false;
        for (int i = 0; i < (int)availableChars_.size(); i++) {
            if (availableChars_[i].name != activeCharName) continue;
            selectedChar_ = i;
            applyCharacter(availableChars_[i]);
            reapplied = true;
            break;
        }
        if (!reapplied) resetToDefaultCharacter();
    } else {
        resetToDefaultCharacter();
    }

    if (state_ == GameState::MainMenu || state_ == GameState::PlayModeMenu ||
        state_ == GameState::ConfigMenu || state_ == GameState::MultiplayerMenu ||
        state_ == GameState::HostSetup || state_ == GameState::JoinMenu ||
        state_ == GameState::Lobby || state_ == GameState::ModMenu ||
        state_ == GameState::CharSelect || state_ == GameState::MapSelect ||
        state_ == GameState::PackSelect || state_ == GameState::MapConfig) {
        playMenuMusic();
    }
}

void Game::applyModOverrides() {
    reloadModdedContent();

    auto overrides = ModManager::instance().mergedOverrides();
    if (overrides.has("player_speed")) {
        player_.speed = overrides.getFloat("player_speed", PLAYER_SPEED);
    }
    if (overrides.has("player_hp")) {
        int hpVal = overrides.getInt("player_hp", PLAYER_MAX_HP);
        config_.playerMaxHp = hpVal;
        player_.maxHp = hpVal;
        player_.hp = hpVal;
    }
    if (overrides.has("enemy_hp_scale")) {
        config_.enemyHpScale = overrides.getFloat("enemy_hp_scale", 1.0f);
    }
    if (overrides.has("enemy_speed_scale")) {
        config_.enemySpeedScale = overrides.getFloat("enemy_speed_scale", 1.0f);
    }
    if (overrides.has("spawn_rate_scale")) {
        config_.spawnRateScale = overrides.getFloat("spawn_rate_scale", 1.0f);
    }

    // Register mod gamemodes
    auto& reg = GameModeRegistry::instance();
    for (auto& mod : ModManager::instance().mods()) {
        if (!mod.enabled) continue;
        for (auto& gm : mod.gamemodes) {
            reg.registerMode(gm);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Multiplayer Integration
// ═════════════════════════════════════════════════════════════════════════════

void Game::initMultiplayer() {
    auto& net = NetworkManager::instance();
    net.init();
    net.setDedicatedServer(dedicatedMode_);
    net.setUsername(config_.username);
    setupNetworkCallbacks();
    printf("Multiplayer initialized\n");
}

void Game::shutdownMultiplayer() {
    clearSyncedCharacters();
    NetworkManager::instance().shutdown();
}

void Game::setupNetworkCallbacks() {
    auto& net = NetworkManager::instance();

    net.onPlayerJoined = [this](uint8_t id, const std::string& name) {
        printf("[NET] Player joined: %s (id=%d)\n", name.c_str(), id);
        auto& net2 = NetworkManager::instance();
        if (net2.isHost()) {
            // Auto-kick banned players
            if (bannedPlayerIds_.count(id)) {
                printf("[NET] Auto-kicking banned player %d\n", id);
                net2.sendAdminKick(id);
                return;
            }
            // Send current lobby settings to the new player
            net2.sendConfigSync(lobbySettings_);
            for (const auto& entry : syncedCharacters_) {
                if (entry.first == id) continue;
                const auto& synced = entry.second;
                net2.sendCharacterSyncForPlayer(entry.first, synced.name, synced.isDefault, synced.data, id);
            }
        }
    };

    net.onPlayerLeft = [this](uint8_t id) {
        printf("[NET] Player left (id=%d)\n", id);
        clearSyncedCharacter(id);
        // If we're the host and all clients disconnected during a game,
        // end the game and go to scoreboard
        auto& net2 = NetworkManager::instance();
        if (net2.isHost() && net2.isInGame()) {
            bool anyRemote = false;
            for (auto& p : net2.players()) {
                if (p.peer) { anyRemote = true; break; }
            }
            if (!anyRemote) {
                printf("All clients disconnected, ending game\n");
                state_ = GameState::Scoreboard;
                menuSelection_ = 0;
            }
        }
    };

    net.onLobbyHostChanged = [this](uint8_t newHostId) {
        printf("[NET] Lobby host changed to id=%d\n", newHostId);
        auto& net2 = NetworkManager::instance();
        if (newHostId == net2.localPlayerId()) {
            lobbyReady_ = false;
        }
    };

    net.onLobbyStartRequested = [this]() {
        if (state_ == GameState::Lobby) {
            startMultiplayerGame();
        }
    };

    net.onPlayerStateReceived = [this](const NetPlayer& state) {
        // Position/interpolation and all fields are now handled in network.cpp handlePacket.
        // This callback is kept for any game-side logic that needs to fire on state updates.
        (void)state;
    };

    net.onBulletSpawned = [this](Vec2 pos, float angle, uint8_t playerId, uint32_t netId, uint8_t playerSlot) {
        if (playerId != NetworkManager::instance().localPlayerId()) {
            Entity b;
            b.pos = pos;
            b.vel = Vec2::fromAngle(angle) * BULLET_SPEED;
            b.rotation = angle;
            b.size = BULLET_SIZE;
            b.lifetime = BULLET_LIFETIME;
            b.tag = TAG_BULLET;
            b.sprite = bulletSprite_;
            b.damage = 1;
            b.netId = netId;
            b.ownerId = playerId;
            b.ownerSubSlot = playerSlot;
            bullets_.push_back(b);
        }
    };

    // Remove a remote bullet when the host signals it hit something
    net.onBulletRemoved = [this](uint32_t netId) {
        for (auto& b : bullets_) {
            if (b.netId == netId && b.alive) {
                b.alive = false;
                break;
            }
        }
    };

    // Enemy killed notification from host — kill locally on clients (with effects)
    net.onEnemyKilled = [this](uint32_t enemyIdx, uint8_t killerId) {
        // Credit the bomb counter if WE made this kill; otherwise just sync the death
        if (enemyIdx < enemies_.size() && enemies_[enemyIdx].alive) {
            auto& net2 = NetworkManager::instance();
            bool ours = (killerId == net2.localPlayerId());
            killEnemy(enemies_[enemyIdx], ours);
        }
    };

    // Host broadcast a config change (e.g. lobby settings update)
    net.onConfigSyncReceived = [this](const LobbySettings& settings) {
        lobbySettings_ = settings;
        currentRules_.pvpEnabled = settings.isPvp || settings.pvpEnabled;
        currentRules_.friendlyFire = settings.friendlyFire;
        currentRules_.upgradesShared = settings.upgradesShared;
        currentRules_.teamCount = settings.teamCount;
        printf("Game: Config sync received — isPvp=%d pvp=%d ff=%d shared=%d teams=%d\n",
               (int)settings.isPvp, (int)settings.pvpEnabled, (int)settings.friendlyFire,
               (int)settings.upgradesShared, settings.teamCount);
    };

    net.onTeamAssigned = [this](uint8_t playerId, int8_t team) {
        printf("Game: Player %d assigned to team %d\n", playerId, team);
        auto& net2 = NetworkManager::instance();
        // If we're host and in TeamSelect, check if all players have teams
        if (net2.isHost() && state_ == GameState::TeamSelect) {
            bool allAssigned = true;
            for (auto& p : net2.players()) {
                if (p.team < 0) { allAssigned = false; break; }
            }
            if (allAssigned) {
                // Launch the game
                std::string customMapFile = net2.lobbyInfo().mapFile;
                std::vector<uint8_t> customMapData;
                if (!customMapFile.empty()) {
                    FILE* f = fopen(customMapFile.c_str(), "rb");
                    if (f) {
                        fseek(f, 0, SEEK_END);
                        long sz = ftell(f);
                        fseek(f, 0, SEEK_SET);
                        customMapData.resize(sz);
                        fread(customMapData.data(), 1, sz, f);
                        fclose(f);
                        startCustomMapMultiplayer(customMapFile);
                        state_ = GameState::MultiplayerGame;
                        net2.startGame(0, map_.width, map_.height, customMapData);
                        respawnTimer_ = currentRules_.respawnTime;
                    }
                }
                if (state_ != GameState::MultiplayerGame) {
                    uint32_t mapSeed = (uint32_t)time(nullptr) ^ (uint32_t)rand();
                    mapSrand(mapSeed);
                    startGame();
                    state_ = GameState::MultiplayerGame;
                    net2.startGame(mapSeed, config_.mapWidth, config_.mapHeight);
                    respawnTimer_ = currentRules_.respawnTime;
                }
            }
        }
    };

    net.onTeamSelectStarted = [this](int teamCount) {
        // Client received team selection notification from host
        lobbySettings_.teamCount = teamCount;
        currentRules_.teamCount = teamCount;
        localTeam_ = -1;
        teamSelectCursor_ = 0;
        teamLocked_ = false;
        state_ = GameState::TeamSelect;
        menuSelection_ = 0;
        printf("Game: Entering team select screen (%d teams)\n", teamCount);
    };

    net.onBombSpawned = [this](Vec2 pos, Vec2 vel, uint8_t playerId, uint8_t playerSlot) {
        if (playerId == NetworkManager::instance().localPlayerId()) return;
        // If we already have an orbiting bomb from this player, convert it in-place
        // so there is no duplicate when it launches
        for (auto& b : bombs_) {
            if (b.ownerId == playerId && b.ownerSubSlot == playerSlot && b.alive && !b.hasDashed) {
                b.pos = pos;
                b.vel = vel;
                b.hasDashed = true;
                return;
            }
        }
        // No orbiting bomb found — create a launched one (fallback)
        Bomb bomb;
        bomb.ownerId = playerId;
        bomb.ownerSubSlot = playerSlot;
        bomb.pos = pos;
        bomb.vel = vel;
        bomb.alive = true;
        bomb.hasDashed = true;
        bomb.animFrame = 0;
        bombs_.push_back(bomb);
    };

    net.onBombOrbit = [this](uint8_t ownerId, uint8_t ownerSlot) {
        auto& net2 = NetworkManager::instance();
        if (ownerId == net2.localPlayerId()) return;
        // Don't duplicate if we already have an orbiting bomb for this player
        for (auto& b : bombs_) {
            if (b.ownerId == ownerId && b.ownerSubSlot == ownerSlot && b.alive && !b.hasDashed) return;
        }
        Bomb rb;
        rb.ownerId = ownerId;
        rb.ownerSubSlot = ownerSlot;
        rb.orbitAngle = (float)(rand() % 360) * (float)M_PI / 180.0f;
        rb.orbitRadius = 55.0f;
        rb.orbitSpeed = 3.0f;
        rb.alive = true;
        rb.hasDashed = false;
        bombs_.push_back(rb);
    };

    net.onExplosionSpawned = [this](Vec2 pos, uint8_t ownerId, uint8_t ownerSlot) {
        suppressNetExplosion_ = true;
        spawnExplosion(pos, ownerId, ownerSlot);
        suppressNetExplosion_ = false;
    };

    net.onCrateSpawned = [this](Vec2 pos, uint8_t upgradeType) {
        PickupCrate crate;
        crate.pos = pos;
        crate.contents = (UpgradeType)upgradeType;
        crates_.push_back(crate);
    };

    net.onPickupCollected = [this](Vec2 pos, uint8_t upgradeType, uint8_t playerId) {
        bool isLocalPick = (playerId == NetworkManager::instance().localPlayerId());
        // Individual mode: only the picker gets the upgrade; shared: everyone gets it
        if (isLocalPick || currentRules_.upgradesShared) {
            applyUpgrade((UpgradeType)upgradeType);
        }
        // Remove the pickup from the world on all clients (it should disappear for everyone)
        if (!isLocalPick) {
            for (auto& p : pickups_) {
                if (p.alive && (p.pos - pos).lengthSq() < 64.0f * 64.0f) {
                    p.alive = false;
                    break;
                }
            }
        }
    };

    net.onPlayerDied = [this](uint8_t playerId, uint8_t killerId) {
        auto& net = NetworkManager::instance();
        NetPlayer* victim = net.findPlayer(playerId);
        if (victim) victim->alive = false;

        // ── Death explosion + scorch ──────────────────────────────────────
        {
            Vec2 dpos = victim ? victim->pos : player_.pos;
            spawnPlayerDeathEffect(dpos);
        }

        // ── Team-colored death burst ──────────────────────────────────────
        if (currentRules_.teamCount >= 2) {
            Vec2 dpos = victim ? victim->pos : player_.pos;
            int  team = victim ? (int)victim->team : (int)localTeam_;
            if (victim || playerId == net.localPlayerId()) {
                static const SDL_Color teamColors[4] = {
                    {255, 80,  80,  255}, {80,  80,  255, 255},
                    {80,  220, 80,  255}, {255, 180, 60,  255}
                };
                SDL_Color tc = (team >= 0 && team < 4) ? teamColors[team]
                                                       : SDL_Color{180, 180, 180, 255};
                int numGibs = 22 + rand() % 10;
                for (int i = 0; i < numGibs; i++) {
                    BoxFragment f;
                    f.pos = {dpos.x + (float)(rand() % 24 - 12),
                             dpos.y + (float)(rand() % 24 - 12)};
                    float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
                    float spd   = 130.0f + (float)(rand() % 320);
                    f.vel       = {cosf(angle) * spd, sinf(angle) * spd};
                    f.size      = 3.0f + (float)(rand() % 7);
                    f.lifetime  = 0.55f + (float)(rand() % 100) / 200.0f;
                    f.age       = 0;
                    f.alive     = true;
                    f.rotation  = (float)(rand() % 360);
                    f.rotSpeed  = (float)(rand() % 600 - 300);
                    int var = rand() % 50 - 25;
                    f.color = {
                        (Uint8)std::max(0, std::min(255, (int)tc.r + var)),
                        (Uint8)std::max(0, std::min(255, (int)tc.g + var)),
                        (Uint8)std::max(0, std::min(255, (int)tc.b + var)),
                        255
                    };
                    boxFragments_.push_back(f);
                }
            }
        }
        // If WE are the one who died, actually kill the local player
        if (playerId == net.localPlayerId() && !player_.dead) {
            player_.dead = true;
            player_.hp = 0;
            if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
            camera_.addShake(4.0f);
            // Transition to death / spectator based on lives
            if (currentRules_.lives > 0 && localLives_ > 0) {
                localLives_--;
            }
            if (currentRules_.lives > 0 && localLives_ <= 0) {
                spectatorMode_ = true;
                state_ = GameState::MultiplayerSpectator;
                // PVE defeat: check if all players are out of lives
                if (!lobbySettings_.isPvp && net.isHost()) {
                    bool allDead = true;
                    for (auto& pp : net.players()) {
                        if (pp.alive || pp.lives > 0) { allDead = false; break; }
                    }
                    if (allDead) {
                        net.sendGameEnd((uint8_t)MatchEndReason::TeamWiped);
                    }
                }
                // PVP battle-royale: check if only one player/team remains (no-timer mode only)
                if (lobbySettings_.isPvp && net.isHost() && lobbySettings_.pvpMatchDuration == 0.0f) {
                    if (lobbySettings_.teamCount >= 2) {
                        std::set<int> teamsAlive;
                        for (auto& pp : net.players()) {
                            if ((pp.alive || pp.lives > 0) && pp.team >= 0)
                                teamsAlive.insert(pp.team);
                        }
                        if ((int)teamsAlive.size() <= 1)
                            net.sendGameEnd((uint8_t)MatchEndReason::LastAlive);
                    } else {
                        int aliveCount = 0;
                        for (auto& pp : net.players()) {
                            if (pp.alive || pp.lives > 0) aliveCount++;
                        }
                        if (aliveCount <= 1)
                            net.sendGameEnd((uint8_t)MatchEndReason::LastAlive);
                    }
                }
            } else {
                state_ = GameState::MultiplayerDead;
                respawnTimer_ = currentRules_.respawnTime;
            }
        }
        // Update kill score if the killer is us
        if (killerId == net.localPlayerId() && killerId != playerId) {
            NetPlayer* localP = net.localPlayer();
            if (localP) {
                localP->kills++;
                localP->score += 10;
                net.sendScoreUpdate(net.localPlayerId(), localP->score);
            }
        }
        (void)killerId;
    };

    net.onSubPlayerDied = [this](uint8_t ownerId, uint8_t slot, uint8_t /*killerId*/) {
        auto& net2 = NetworkManager::instance();
        if (ownerId != net2.localPlayerId()) return;
        if (slot == 0 || slot >= 4 || !coopSlots_[slot].joined) return;
        Player& target = coopSlots_[slot].player;
        if (!target.dead) {
            target.dead = true;
            target.hp = 0;
            target.deathTimer = 0.0f;
            target.animTimer = 0.0f;
            target.animFrame = 0;
            coopSlots_[slot].respawnTimer = currentRules_.respawnTime;
            coopSlots_[slot].deaths++;
            coopSlots_[slot].camera.addShake(4.0f);
            rumbleForSlot(slot, 0.92f, 340, 1.50f, 0.82f);
            if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
        }
    };

    net.onSubPlayerHpSync = [this](uint8_t ownerId, uint8_t slot, int hp, int maxHp, uint8_t /*killerId*/) {
        auto& net2 = NetworkManager::instance();
        if (ownerId != net2.localPlayerId()) return;
        if (slot == 0 || slot >= 4 || !coopSlots_[slot].joined) return;
        Player& target = coopSlots_[slot].player;
        target.maxHp = maxHp;
        target.hp = hp;
        if (hp <= 0) {
            if (!target.dead) {
                coopSlots_[slot].deaths++;
            }
            target.dead = true;
            target.deathTimer = 0.0f;
            target.animTimer = 0.0f;
            target.animFrame = 0;
            coopSlots_[slot].respawnTimer = currentRules_.respawnTime;
        } else {
            coopSlots_[slot].camera.addShake(1.8f);
            rumbleForSlot(slot, 0.48f, 150, 1.25f, 0.78f);
        }
    };

    net.onPlayerRespawned = [this](uint8_t playerId, Vec2 pos) {
        auto& net = NetworkManager::instance();
        NetPlayer* p = net.findPlayer(playerId);
        if (p) {
            p->alive = true;
            p->pos = pos;
            p->targetPos = pos;
            p->prevPos = pos;
        }
        if (playerId == net.localPlayerId()) {
            player_.dead = false;
            player_.hp = player_.maxHp;
            player_.pos = pos;
            state_ = GameState::MultiplayerGame;
        }
    };

    // ── PvP host-authoritative damage ──
    // Host validates a bullet-hit reported by a client and applies authoritative damage
    net.onHitRequest = [this](uint32_t bulletNetId, int damage, uint8_t ownerId, uint8_t senderPlayerId, uint8_t targetSlot) -> bool {
        auto& net = NetworkManager::instance();
        // Remove bullet from host's list (prevents double-hit)
        bool bulletFound = false;
        for (auto& b : bullets_) {
            if (b.netId == bulletNetId && b.alive) {
                b.alive = false;
                bulletFound = true;
                break;
            }
        }
        // Allow even if bullet wasn't found locally — network ordering may differ
        if (targetSlot == 0) {
            NetPlayer* victim = net.findPlayer(senderPlayerId);
            if (!victim || !victim->alive) return false;
            victim->hp -= damage;
            if (victim->hp <= 0) {
                victim->hp = 0;
                victim->alive = false;
                net.sendPlayerDied(senderPlayerId, ownerId);
            } else {
                net.sendPlayerHpSync(senderPlayerId, victim->hp, victim->maxHp, ownerId);
            }
        } else {
            if (senderPlayerId == net.localPlayerId()) {
                if (targetSlot >= 4 || !coopSlots_[targetSlot].joined || coopSlots_[targetSlot].player.dead) return false;
                Player& victim = coopSlots_[targetSlot].player;
                victim.hp = std::max(0, victim.hp - damage);
                if (victim.hp <= 0) {
                    victim.dead = true;
                    net.sendSubPlayerDied(senderPlayerId, targetSlot, ownerId);
                } else {
                    net.sendSubPlayerHpSync(senderPlayerId, targetSlot, victim.hp, victim.maxHp, ownerId);
                }
            } else {
                NetPlayer* owner = net.findPlayer(senderPlayerId);
                size_t idx = (targetSlot > 0) ? (size_t)(targetSlot - 1) : (size_t)-1;
                if (!owner || idx >= owner->subPlayers.size() || !owner->subPlayers[idx].alive) return false;
                auto& victim = owner->subPlayers[idx];
                victim.hp = std::max(0, victim.hp - damage);
                if (victim.hp <= 0) {
                    victim.alive = false;
                    net.sendSubPlayerDied(senderPlayerId, targetSlot, ownerId);
                } else {
                    net.sendSubPlayerHpSync(senderPlayerId, targetSlot, victim.hp, victim.maxHp, ownerId);
                }
            }
        }
        (void)bulletFound;
        return true;
    };

    // PvP melee: host applies authoritative damage for a client melee hit
    net.onMeleeHitRequest = [this](uint8_t attackerId, uint8_t targetId, int damage, uint8_t targetSlot) {
        auto& net = NetworkManager::instance();
        if (targetSlot == 0) {
            NetPlayer* target = net.findPlayer(targetId);
            if (!target || !target->alive) return;
            // Friendly-fire guard
            if (attackerId != 255 && target->team >= 0) {
                NetPlayer* attacker = net.findPlayer(attackerId);
                if (attacker && attacker->team == target->team) return;
            }
            target->hp -= std::max(1, damage);
            if (target->hp <= 0) {
                target->hp = 0; target->alive = false;
                net.sendPlayerDied(targetId, attackerId);
            } else {
                net.sendPlayerHpSync(targetId, target->hp, target->maxHp, attackerId);
            }
        } else {
            if (targetId == net.localPlayerId()) {
                if (targetSlot >= 4 || !coopSlots_[targetSlot].joined) return;
                Player& target = coopSlots_[targetSlot].player;
                if (target.dead) return;
                target.hp -= std::max(1, damage);
                if (target.hp <= 0) {
                    target.hp = 0; target.dead = true;
                    net.sendSubPlayerDied(targetId, targetSlot, attackerId);
                } else {
                    net.sendSubPlayerHpSync(targetId, targetSlot, target.hp, target.maxHp, attackerId);
                }
            } else {
                NetPlayer* owner = net.findPlayer(targetId);
                size_t idx = (targetSlot > 0) ? (size_t)(targetSlot - 1) : (size_t)-1;
                if (!owner || idx >= owner->subPlayers.size()) return;
                auto& target = owner->subPlayers[idx];
                if (!target.alive) return;
                target.hp -= std::max(1, damage);
                if (target.hp <= 0) {
                    target.hp = 0; target.alive = false;
                    net.sendSubPlayerDied(targetId, targetSlot, attackerId);
                } else {
                    net.sendSubPlayerHpSync(targetId, targetSlot, target.hp, target.maxHp, attackerId);
                }
            }
        }
    };

    // Receive authoritative HP from host — update local player if it's ours
    net.onPlayerHpSync = [this](uint8_t playerId, int hp, int maxHp, uint8_t killerId) {
        auto& net = NetworkManager::instance();
        if (playerId == net.localPlayerId()) {
            player_.hp    = hp;
            player_.maxHp = maxHp;
            if (hp <= 0 && !player_.dead) {
                // Host confirmed we're dead
                player_.dead = true;
                if (sfxHurt_)  { int ch = Mix_PlayChannel(-1, sfxHurt_,  0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
                if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 5); }
                camera_.addShake(4.0f);
            } else if (hp < player_.maxHp) {
                camera_.addShake(1.8f);
                if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume / 4); }
            }
        }
        (void)killerId;
    };

    net.onChatMessage = [this](const std::string& sender, const std::string& text) {
        printf("[CHAT] %s: %s\n", sender.c_str(), text.c_str());
    };

    net.onWaveStarted = [this](int waveNum) {
        waveNumber_ = waveNum;
        waveAnnounceTimer_ = 2.5f;
        waveAnnounceNum_ = waveNum;
    };

    net.onEnemyBulletSpawned = [this](Vec2 pos, Vec2 vel) {
        // Clients only — spawn the bullet locally for visuals and local collision
        auto& net2 = NetworkManager::instance();
        if (net2.isHost()) return;
        Entity b;
        b.pos = pos;
        b.vel = vel;
        b.rotation = atan2f(vel.y, vel.x);
        b.size = BULLET_SIZE;
        b.lifetime = ENEMY_BULLET_LIFETIME;
        b.tag = TAG_ENEMY_BULLET;
        b.sprite = enemyBulletSprite_;
        b.damage = 1;
        enemyBullets_.push_back(b);
        if (sfxEnemyShoot_) {
            int ch = Mix_PlayChannel(-1, sfxEnemyShoot_, 0);
            if (ch >= 0) Mix_Volume(ch, config_.sfxVolume * 2 / 5);
        }
    };

    net.onEnemyStatesReceived = [this](const void* rawData, int count) {
        // Only clients apply host-authoritative enemy state
        auto& net2 = NetworkManager::instance();
        if (net2.isHost()) return;

        // Wire format: 26 bytes per enemy
        // {x:4, y:4, rot:4, hp:4, type:1, flags:1, dashDir.x:4, dashDir.y:4}
        static const size_t PER_ENEMY = 26;
        const uint8_t* raw = (const uint8_t*)rawData;

        // Helper to read one enemy entry
        auto readEntry = [&](int i, float& x, float& y, float& rot, int& hp,
                             uint8_t& type, uint8_t& flags, float& ddx, float& ddy) {
            const uint8_t* p = raw + i * PER_ENEMY;
            memcpy(&x,   p,      4);
            memcpy(&y,   p + 4,  4);
            memcpy(&rot, p + 8,  4);
            memcpy(&hp,  p + 12, 4);
            type  = p[16];
            flags = p[17];
            memcpy(&ddx, p + 18, 4);
            memcpy(&ddy, p + 22, 4);
        };

        // Collect indices of currently alive enemies
        std::vector<int> aliveIdx;
        aliveIdx.reserve(enemies_.size());
        for (int i = 0; i < (int)enemies_.size(); i++) {
            if (enemies_[i].alive) aliveIdx.push_back(i);
        }

        // Kill surplus enemies beyond what the host reports
        for (int i = count; i < (int)aliveIdx.size(); i++) {
            enemies_[aliveIdx[i]].alive = false;
        }

        // Spawn missing enemies
        while ((int)aliveIdx.size() < count) {
            int ni = (int)aliveIdx.size();
            float x, y, rot, ddx, ddy; int hp; uint8_t type, flags;
            readEntry(ni, x, y, rot, hp, type, flags, ddx, ddy);
            spawnEnemy({x, y}, (EnemyType)type);
            // Immediately place at the authoritative position on first spawn
            auto& ne = enemies_.back();
            ne.netTargetPos = {x, y};
            ne.pos          = {x, y};
            aliveIdx.push_back((int)enemies_.size() - 1);
        }

        // Update all reported enemies
        for (int i = 0; i < count; i++) {
            float x, y, rot, ddx, ddy; int hp; uint8_t type, flags;
            readEntry(i, x, y, rot, hp, type, flags, ddx, ddy);
            auto& e = enemies_[aliveIdx[i]];
            e.alive          = true;
            e.netTargetPos   = {x, y};   // interpolated in updateEnemies
            e.rotation       = rot;
            e.hp             = hp;
            e.type           = (EnemyType)type;
            e.netIsDashing    = (flags & 0x01) != 0;
            e.netDashCharging = (flags & 0x02) != 0;
            e.dashDir        = {ddx, ddy};
        }
    };

    net.onGameStarted = [this](uint32_t mapSeed, int mapW, int mapH, const std::vector<uint8_t>& customMapData) {
        // Clear any active text editing flags to prevent stale input handling
        ipTyping_ = false;
        joinPortTyping_ = false;
        mpUsernameTyping_ = false;
        portTyping_ = false;
        if (softKB_.active) softKB_.close(false);
#ifndef __SWITCH__
        SDL_StopTextInput();
#endif
        // Apply lobby settings received from host
        config_.playerMaxHp    = lobbySettings_.playerMaxHp;
        config_.spawnRateScale = lobbySettings_.spawnRateScale;
        config_.enemyHpScale   = lobbySettings_.enemyHpScale;
        config_.enemySpeedScale= lobbySettings_.enemySpeedScale;
        currentRules_.friendlyFire  = lobbySettings_.friendlyFire;
        currentRules_.pvpEnabled    = lobbySettings_.isPvp || lobbySettings_.pvpEnabled;
        currentRules_.upgradesShared= lobbySettings_.upgradesShared;
        currentRules_.teamCount     = lobbySettings_.teamCount;
        currentRules_.lives         = lobbySettings_.livesPerPlayer;
        currentRules_.sharedLives   = lobbySettings_.livesShared;
        player_.maxHp = config_.playerMaxHp;
        player_.hp    = player_.maxHp;

        // Initialise lives tracking (client side)
        spectatorMode_ = false;
        matchElapsed_ = 0.0f;
        discordSessionStart_ = (int64_t)time(nullptr);
        matchTimer_   = (lobbySettings_.pvpMatchDuration > 0.0f) ? lobbySettings_.pvpMatchDuration : 0.0f;
        localLives_ = (currentRules_.lives > 0 && !currentRules_.sharedLives) ? currentRules_.lives : -1;
        sharedLives_ = -1; // host tracks shared pool

        // Clients must initialise the world before entering multiplayer game state;
        // without this the map/player/camera are uninitialised, causing an immediate crash.
        if (!NetworkManager::instance().isHost()) {
            config_.mapWidth  = mapW;   // use host's dimensions
            config_.mapHeight = mapH;

            if (!customMapData.empty()) {
                // Custom map was sent — write temp file and load it
                std::string tmpPath = "maps/_mp_recv.csm";
                mkdir("maps", 0755);
                FILE* f = fopen(tmpPath.c_str(), "wb");
                if (f) {
                    fwrite(customMapData.data(), 1, customMapData.size(), f);
                    fclose(f);
                }
                startCustomMapMultiplayer(tmpPath);
            } else {
                mapSrand(mapSeed);             // same seed as host → same map
                {
                    bool savedIsPvp = lobbySettings_.isPvp; // startGame() resets this to false
                    startGame();                // generates map, resets player & camera
                    lobbySettings_.isPvp = savedIsPvp;      // restore so updateSpawning blocks PvP waves
                }
                player_.pos = pickSpawnPos(); // team corner or random, not map centre
            }
        }
        // PvP: no damage cooldown so rapid hits always register
        // Set AFTER startGame/startCustomMapMultiplayer which reset the Player struct
        player_.invulnDuration = lobbySettings_.isPvp ? 0.0f : PLAYER_INVULN_TIME;
        state_ = GameState::MultiplayerGame;
        menuSelection_ = 0;
        respawnTimer_ = currentRules_.respawnTime;

        // ── Splitscreen: init coopSlots for local sub-players (client side) ──
        {
            int joined = 0;
            for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;
            coopPlayerCount_ = joined;
            if (coopPlayerCount_ > 1) {
                const int vw = (coopPlayerCount_ >= 2) ? (int)((SCREEN_W/2) / SPLITSCREEN_ZOOM) : SCREEN_W;
                const int vh = (coopPlayerCount_ >= 3) ? (int)((SCREEN_H/2) / SPLITSCREEN_ZOOM) : (coopPlayerCount_ >= 2) ? (int)(SCREEN_H / SPLITSCREEN_ZOOM) : SCREEN_H;
                const Vec2 off[4] = {{-80,0},{80,0},{0,-80},{0,80}};
                int si = 0;
                for (int i = 0; i < 4; i++) {
                    if (!coopSlots_[i].joined) continue;
                    if (si == 0) {
                        coopSlots_[i].player   = player_;
                        coopSlots_[i].upgrades = upgrades_;
                    } else {
                        coopSlots_[i].player = Player{};
                        coopSlots_[i].player.maxHp     = config_.playerMaxHp;
                        coopSlots_[i].player.hp        = config_.playerMaxHp;
                        coopSlots_[i].player.bombCount = 1;
                        Vec2 sp = player_.pos + off[si % 4];
                        if (!map_.spawnPoints.empty())
                            sp = map_.spawnPoints[si % (int)map_.spawnPoints.size()];
                        coopSlots_[i].player.pos = sp;
                        coopSlots_[i].upgrades.reset();
                    }
                    coopSlots_[i].kills  = 0;
                    coopSlots_[i].deaths = 0;
                    coopSlots_[i].respawnTimer = 0;
                    coopSlots_[i].camera.worldW = map_.worldWidth();
                    coopSlots_[i].camera.worldH = map_.worldHeight();
                    coopSlots_[i].camera.viewW  = vw;
                    coopSlots_[i].camera.viewH  = vh;
                    coopSlots_[i].camera.pos    = {coopSlots_[i].player.pos.x - vw/2.f,
                                                    coopSlots_[i].player.pos.y - vh/2.f};
                    si++;
                }
            }
        }
    };

    net.onGameEnded = [this](uint8_t rawReason) {
        auto reason = (MatchEndReason)rawReason;
        auto& netR = NetworkManager::instance();
        auto  players = netR.players(); // snapshot for result computation

        matchResult_ = MatchResult{};
        matchResult_.reason      = reason;
        matchResult_.matchElapsed = matchElapsed_;

        // Team-name helper
        static const char* kTeamNames[4] = {"Red","Blue","Green","Yellow"};

        switch (reason) {
            case MatchEndReason::WavesCleared:
                matchResult_.isWin    = true;
                matchResult_.headline = "VICTORY";
                matchResult_.subtitle = "All waves cleared!";
                break;

            case MatchEndReason::TeamWiped:
                matchResult_.isWin    = false;
                matchResult_.headline = "DEFEAT";
                matchResult_.subtitle = "All players eliminated";
                break;

            case MatchEndReason::LastAlive:
                if (!spectatorMode_) {
                    matchResult_.isWin    = true;
                    matchResult_.headline = "VICTORY";
                    if (lobbySettings_.teamCount >= 2)
                        matchResult_.subtitle = "Your team is the last standing!";
                    else
                        matchResult_.subtitle = "Last player standing!";
                    matchResult_.winnerTeam = localTeam_;
                    if (auto* lp = netR.localPlayer()) matchResult_.winnerName = lp->username;
                } else {
                    matchResult_.isWin    = false;
                    matchResult_.headline = "DEFEAT";
                    // Find winner
                    if (lobbySettings_.teamCount >= 2) {
                        for (auto& p : players) {
                            if (!p.spectating && p.alive && p.team >= 0) {
                                matchResult_.winnerTeam = p.team;
                                break;
                            }
                        }
                        int wt = matchResult_.winnerTeam;
                        matchResult_.subtitle = std::string(wt >= 0 && wt < 4 ? kTeamNames[wt] : "Unknown") + " team wins!";
                    } else {
                        for (auto& p : players) {
                            if (!p.spectating && p.alive) {
                                matchResult_.winnerName = p.username;
                                matchResult_.winnerKills = p.kills;
                                break;
                            }
                        }
                        if (matchResult_.winnerName.empty() && !players.empty())
                            matchResult_.winnerName = players[0].username;
                        matchResult_.subtitle = matchResult_.winnerName + " wins!";
                    }
                }
                break;

            case MatchEndReason::TimeUp:
                if (lobbySettings_.teamCount >= 2) {
                    int teamKills[4] = {};
                    for (auto& p : players)
                        if (p.team >= 0 && p.team < 4) teamKills[p.team] += p.kills;
                    int bestTeam = 0;
                    for (int t = 1; t < 4; t++)
                        if (teamKills[t] > teamKills[bestTeam]) bestTeam = t;
                    matchResult_.winnerTeam  = bestTeam;
                    matchResult_.winnerKills = teamKills[bestTeam];
                    matchResult_.isWin = (localTeam_ == bestTeam);
                    if (matchResult_.isWin) {
                        matchResult_.headline = "VICTORY";
                        matchResult_.subtitle = std::string("Time's up! ") + kTeamNames[bestTeam] + " team wins with " + std::to_string(teamKills[bestTeam]) + " kills";
                    } else {
                        matchResult_.headline = "DEFEAT";
                        matchResult_.subtitle = std::string("Time's up! ") + kTeamNames[bestTeam] + " team wins with " + std::to_string(teamKills[bestTeam]) + " kills";
                    }
                } else {
                    NetPlayer* best = nullptr;
                    for (auto& p : players)
                        if (!best || p.kills > best->kills) best = (NetPlayer*)&p;
                    if (best) {
                        matchResult_.winnerName  = best->username;
                        matchResult_.winnerKills = best->kills;
                        matchResult_.isWin = (best->id == netR.localPlayerId());
                    }
                    if (matchResult_.isWin) {
                        matchResult_.headline = "VICTORY";
                        matchResult_.subtitle = "Time's up! You win with " + std::to_string(matchResult_.winnerKills) + " kills!";
                    } else {
                        matchResult_.headline = "DEFEAT";
                        matchResult_.subtitle = "Time's up!  " + matchResult_.winnerName + " wins with " + std::to_string(matchResult_.winnerKills) + " kills";
                    }
                }
                break;

            default: // HostEnded / Unknown
                matchResult_.isWin    = false;
                matchResult_.headline = "GAME OVER";
                matchResult_.subtitle = "Match ended by host";
                break;
        }

        state_ = GameState::WinLoss;
        menuSelection_ = 0;
        spectatorMode_ = false;
        lobbyKickCursor_ = -1;
        playMenuMusic();
    };

    net.onModSyncReceived = [this](const std::vector<uint8_t>& modData) {
        // Client received mod data from host — install and apply
        printf("Game: Received mod sync from host, installing...\n");
        auto& mm = ModManager::instance();
        mm.deserializeAndInstallMods(modData, config_.saveIncomingModsPermanently);
        applyModOverrides();
        printf("Game: Mod sync complete\n");
    };

    // ── Admin / Lives callbacks ─────────────────────────────────────────
    net.onAdminKicked = [this](uint8_t targetId) {
        auto& net2 = NetworkManager::instance();
        if (targetId == net2.localPlayerId()) {
            // We were kicked — return to main menu
            net2.disconnect();
            playMenuMusic();
            state_         = GameState::MainMenu;
            menuSelection_ = 0;
            spectatorMode_ = false;
        }
    };

    net.onAdminRespawned = [this](uint8_t targetId) {
        auto& net2 = NetworkManager::instance();
        if (targetId == net2.localPlayerId()) {
            player_.dead    = false;
            player_.hp      = player_.maxHp;
            spectatorMode_  = false;
            localLives_     = -1; // admin respawn grants infinite lives
            state_          = GameState::MultiplayerGame;
        }
    };

    net.onAdminTeamMoved = [this](uint8_t targetId, int8_t newTeam) {
        auto& net2 = NetworkManager::instance();
        if (targetId == net2.localPlayerId()) {
            localTeam_ = newTeam;
            // Respawn at updated team spawn if currently dead or spectating
            if (player_.dead || spectatorMode_) {
                player_.dead   = false;
                player_.hp     = player_.maxHp;
                spectatorMode_ = false;
                state_         = GameState::MultiplayerGame;
            }
        }
    };

    net.onLivesUpdated = [this](uint8_t /*playerId*/, int lives) {
        if (currentRules_.sharedLives) sharedLives_ = lives;
    };

    net.onCharacterSyncReceived = [this](uint8_t playerId, const std::string& characterName,
                                         bool isDefault, const std::vector<uint8_t>& data) {
        auto& synced = syncedCharacters_[playerId];
        synced.name = characterName;
        synced.isDefault = isDefault;
        synced.data = data;

        if (isDefault) {
            if (synced.visualLoaded) synced.visual.unload();
            synced.visualLoaded = false;
            synced.cacheFolder.clear();
            return;
        }

        if (playerId == NetworkManager::instance().localPlayerId()) return;

        if (!installSyncedCharacterVisual(playerId, characterName, data)) {
            printf("[NET] Failed to install synced character '%s' for player %u\n",
                   characterName.c_str(), (unsigned)playerId);
        }
    };
}

void Game::updateMultiplayer(float dt) {
    auto& net = NetworkManager::instance();
    if (!net.isOnline()) {
        clearSyncedCharacters();
        // Connection lost — return to main menu
        if (state_ == GameState::MultiplayerGame ||
            state_ == GameState::MultiplayerPaused ||
            state_ == GameState::MultiplayerDead ||
            state_ == GameState::MultiplayerSpectator) {
            printf("Lost connection to host, returning to menu\n");
            playMenuMusic();
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
        return;
    }

    syncLocalCharacterSelection();

    // NOTE: net.update(dt) is already called in run() every frame — do NOT call again here

    // Send local player state at fixed rate
    if (net.isInGame()) {
        netStateSendTimer_ -= dt;
        if (netStateSendTimer_ <= 0) {
            netStateSendTimer_ = 1.0f / 20.0f; // 20 Hz — interpolation handles smooth display

            NetPlayer state;
            state.id = net.localPlayerId();
            state.pos = player_.pos;
            state.rotation = player_.rotation;
            state.hp = player_.hp;
            state.maxHp = player_.maxHp;
            state.animFrame = player_.animFrame;
            state.legFrame = player_.legAnimFrame;
            state.legRotation = player_.legRotation;
            state.moving = player_.moving;
            state.alive = !player_.dead;
            net.sendPlayerState(state);

            // ── Splitscreen: send sub-player states to other clients ──
            if (coopPlayerCount_ > 1) {
                SubPlayerInfo subs[3];
                int subCount = 0;
                int si = 0;
                for (int i = 0; i < 4 && subCount < 3; i++) {
                    if (!coopSlots_[i].joined) continue;
                    if (si++ == 0) continue; // skip primary (slot 0 = main player state)
                    auto& cp = coopSlots_[i].player;
                    subs[subCount].pos = cp.pos;
                    subs[subCount].rotation = cp.rotation;
                    subs[subCount].legRotation = cp.legRotation;
                    subs[subCount].hp = cp.hp;
                    subs[subCount].maxHp = cp.maxHp;
                    subs[subCount].animFrame = cp.animFrame;
                    subs[subCount].legFrame = cp.legAnimFrame;
                    subs[subCount].moving = cp.moving;
                    subs[subCount].alive = !cp.dead;
                    subCount++;
                }
                if (subCount > 0) {
                    net.sendSubPlayerStates(net.localPlayerId(), subs, subCount);
                }
            }
        }

        // Host sends enemy states at 20 Hz, or immediately when an enemy dies.
        // On a dedicated server the lobby-host client is the simulation authority.
        const bool simAuthority = net.isHost() || (net.isConnectedToDedicated() && net.isLobbyHost());
        if (simAuthority) {
            enemySendTimer_ -= dt;
            if (enemySendTimer_ <= 0 || enemyStatesNeedUpdate_) {
                enemySendTimer_ = 1.0f / 20.0f;
                enemyStatesNeedUpdate_ = false;

                // Pack enemy data: {pos.x:4, pos.y:4, rot:4, hp:4, type:1, flags:1, dashDir.x:4, dashDir.y:4} = 26 bytes each
                static const size_t PER_ENEMY = 26;
                int aliveCount = 0;
                for (auto& e : enemies_) if (e.alive) aliveCount++;
                if (aliveCount > 0) {
                    std::vector<uint8_t> edata(aliveCount * PER_ENEMY);
                    int idx = 0;
                    for (auto& e : enemies_) {
                        if (!e.alive) continue;
                        uint8_t* p = edata.data() + idx * PER_ENEMY;
                        memcpy(p,      &e.pos.x,    4);
                        memcpy(p + 4,  &e.pos.y,    4);
                        memcpy(p + 8,  &e.rotation,  4);
                        memcpy(p + 12, &e.hp,        4);
                        p[16] = (uint8_t)e.type;
                        uint8_t flags = 0;
                        if (e.isDashing)    flags |= 0x01;
                        if (e.dashCharging) flags |= 0x02;
                        p[17] = flags;
                        memcpy(p + 18, &e.dashDir.x, 4);
                        memcpy(p + 22, &e.dashDir.y, 4);
                        idx++;
                    }
                    net.sendEnemyStates(edata.data(), aliveCount);
                }
            }
        }

        // Respawn countdown
        if (state_ == GameState::MultiplayerDead && player_.dead && currentRules_.respawnTime > 0) {
            respawnTimer_ -= dt;
            if (respawnTimer_ <= 0) {
                Vec2 spawnPos;
                bool found = false;

                // Team spawn: first try team spawn trigger from custom map, then fall back to corners
                int myTeam = localTeam_;
                if (myTeam >= 0 && currentRules_.teamCount >= 2) {
                    if (playingCustomMap_) {
                        MapTrigger* teamT = customMap_.findTeamSpawnTrigger(myTeam);
                        if (teamT) {
                            // scatter slightly around trigger centre so players don't stack
                            for (int attempt = 0; attempt < 50; attempt++) {
                                float rx = teamT->x + (float)(rand() % (int)teamT->width  - (int)(teamT->width  / 2));
                                float ry = teamT->y + (float)(rand() % (int)teamT->height - (int)(teamT->height / 2));
                                if (!map_.worldCollides(rx, ry, PLAYER_SIZE * 0.5f)) {
                                    spawnPos = {rx, ry};
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) { spawnPos = {teamT->x, teamT->y}; found = true; }
                        }
                    }
                    if (!found) {
                        // Corner fallback (team 0=top-left, 1=bottom-right, 2=top-right, 3=bottom-left)
                        float margin = 96.0f;
                        float ww = map_.worldWidth();
                        float wh = map_.worldHeight();
                        Vec2 corners[4] = {
                            {margin, margin},
                            {ww - margin, wh - margin},
                            {ww - margin, margin},
                            {margin, wh - margin}
                        };
                        Vec2 target = corners[myTeam % 4];
                        for (int attempt = 0; attempt < 50; attempt++) {
                            float rx = target.x + (float)(rand() % 200 - 100);
                            float ry = target.y + (float)(rand() % 200 - 100);
                            rx = std::max(64.0f, std::min(ww - 64.0f, rx));
                            ry = std::max(64.0f, std::min(wh - 64.0f, ry));
                            if (!map_.worldCollides(rx, ry, PLAYER_SIZE * 0.5f)) {
                                spawnPos = {rx, ry};
                                found = true;
                                break;
                            }
                        }
                    }
                }

                // Default random spawn (no teams or corner search failed)
                if (!found) {
                    for (int attempt = 0; attempt < 50; attempt++) {
                        float rx = (float)(64 + rand() % (map_.width * 64 - 128));
                        float ry = (float)(64 + rand() % (map_.height * 64 - 128));
                        if (!map_.worldCollides(rx, ry, PLAYER_SIZE * 0.5f)) {
                            spawnPos = {rx, ry};
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    spawnPos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
                }
                player_.dead = false;
                player_.hp = player_.maxHp;
                player_.pos = spawnPos;
                player_.invulnerable = true;
                player_.invulnTimer = 1.5f;
                net.sendPlayerRespawn(net.localPlayerId(), spawnPos);
                state_ = GameState::MultiplayerGame;
            }
        }

        // ── Match timer (visible on all clients; only host acts on expiry) ─────
        if (lobbySettings_.isPvp) {
            matchElapsed_ += dt;
            if (lobbySettings_.pvpMatchDuration > 0.0f && matchTimer_ > 0.0f) {
                matchTimer_ -= dt;
                if (matchTimer_ < 0.0f) matchTimer_ = 0.0f;
                // Host triggers end when timer expires
                if (net.isHost() && matchTimer_ <= 0.0f) {
                    net.sendGameEnd((uint8_t)MatchEndReason::TimeUp);
                }
            }
        } else if (net.isHost()) {
            matchElapsed_ += dt;
        }
    }
}

void Game::hostGame() {
    auto& net = NetworkManager::instance();
    clearSyncedCharacters();
    int maxClients = dedicatedMode_ ? hostMaxPlayers_ : (hostMaxPlayers_ - 1);
    if (net.host(hostPort_, maxClients)) {
        net.setHostPassword(lobbyPassword_);
        if (state_ != GameState::HostSetup) {
            lobbySettings_.isPvp           = (currentRules_.type == GameModeType::Deathmatch ||
                                             currentRules_.type == GameModeType::TeamDeathmatch);
            lobbySettings_.mapWidth        = config_.mapWidth;
            lobbySettings_.mapHeight       = config_.mapHeight;
            lobbySettings_.playerMaxHp     = config_.playerMaxHp;
            lobbySettings_.spawnRateScale  = config_.spawnRateScale;
            lobbySettings_.enemyHpScale    = config_.enemyHpScale;
            lobbySettings_.enemySpeedScale = config_.enemySpeedScale;
            lobbySettings_.friendlyFire    = currentRules_.friendlyFire;
            lobbySettings_.pvpEnabled      = currentRules_.pvpEnabled;
            lobbySettings_.upgradesShared  = currentRules_.upgradesShared;
            lobbySettings_.teamCount       = currentRules_.teamCount;
        }
        lobbySettings_.maxPlayers = hostMaxPlayers_;
        currentRules_.friendlyFire   = lobbySettings_.friendlyFire;
        currentRules_.pvpEnabled     = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
        currentRules_.upgradesShared = lobbySettings_.upgradesShared;
        currentRules_.teamCount      = lobbySettings_.teamCount;
        currentRules_.lives          = lobbySettings_.livesPerPlayer;
        currentRules_.sharedLives    = lobbySettings_.livesShared;

        net.setGamemode(lobbySettings_.isPvp
            ? (lobbySettings_.teamCount >= 2 ? "team_deathmatch" : "deathmatch")
            : "coop_arena");
        if (hostMapSelectIdx_ == 0) {
            net.setMap("", "Generated");
        } else if (hostMapSelectIdx_ - 1 < (int)mapFiles_.size()) {
            const std::string& mf = mapFiles_[hostMapSelectIdx_ - 1];
            size_t sl = mf.rfind('/'); if (sl == std::string::npos) sl = mf.rfind('\\');
            std::string mname = (sl != std::string::npos) ? mf.substr(sl + 1) : mf;
            size_t dot = mname.rfind('.'); if (dot != std::string::npos) mname = mname.substr(0, dot);
            net.setMap(mf, mname);
        }
        lobbyPrimaryPadId_ = usingGamepad_ ? lastGamepadInputId_ : -1;
        state_ = GameState::Lobby;
        menuSelection_ = 0;
        lobbySettingsScrollY_ = 0;
        lobbyReady_ = false;
        lobbyKickCursor_ = -1;
        bannedPlayerIds_.clear();
        for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
        coopSlots_[0].joined = true;
        coopSlots_[0].joyInstanceId = lobbyPrimaryPadId_;
        coopSlots_[0].username = config_.username;
        lobbySubPlayersSent_ = 0;
        net.setLocalSubPlayers(0);
        lobbyGamemodeIdx_ = gamemodeSelectIdx_;
        lobbyMapIdx_      = hostMapSelectIdx_;
        lobbySettingsSel_ = 0;
        syncLocalCharacterSelection(true);
        printf("Hosting game on port %d\n", hostPort_);
    } else {
        printf("Failed to host game!\n");
    }
}

std::string Game::getLocalIP() {
#ifdef __SWITCH__
    // libnx: nifmGetCurrentIpAddress
    u32 ip = 0;
    nifmInitialize(NifmServiceType_User);
    nifmGetCurrentIpAddress(&ip);
    nifmExit();
    if (ip == 0) return "N/A";
    char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return buf;
#elif defined(_WIN32)
    // Use Winsock to find the first non-loopback IPv4 address
    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname)) != 0) return "N/A";
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, nullptr, &hints, &res) != 0) return "N/A";
    std::string result = "N/A";
    for (auto* rp = res; rp; rp = rp->ai_next) {
        char ip[INET_ADDRSTRLEN] = {};
        auto* sa = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
        if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip)) && strcmp(ip, "127.0.0.1") != 0) {
            result = ip;
            break;
        }
    }
    freeaddrinfo(res);
    return result;
#else
    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return "N/A";
    std::string result = "N/A";
    for (auto* ifa = addrs; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        auto* sa = (struct sockaddr_in*)ifa->ifa_addr;
        char* ip = inet_ntoa(sa->sin_addr);
        // Skip loopback
        if (strcmp(ip, "127.0.0.1") == 0) continue;
        result = ip;
        break;
    }
    freeifaddrs(addrs);
    return result;
#endif
}

void Game::joinGame() {
    auto& net = NetworkManager::instance();
    clearSyncedCharacters();
    connectStatus_.clear();
    if (net.join(joinAddress_, joinPort_, joinPassword_)) {
        lobbyPrimaryPadId_ = usingGamepad_ ? lastGamepadInputId_ : -1;
        state_ = GameState::Lobby;
        menuSelection_ = 0;
        lobbySettingsScrollY_ = 0;
        lobbyReady_ = false;
        lobbyKickCursor_ = -1;
        for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
        coopSlots_[0].joined = true;
        coopSlots_[0].joyInstanceId = lobbyPrimaryPadId_;
        coopSlots_[0].username = config_.username;
        lobbySubPlayersSent_ = -1;
        connectTimer_ = 5.0f; // 5 second timeout
        printf("Joining %s:%d...\n", joinAddress_.c_str(), joinPort_);
    } else {
        connectStatus_ = "Failed to connect";
        printf("Failed to join game!\n");
    }
}

void Game::startMultiplayerGame() {
    auto& net = NetworkManager::instance();
    if (!net.isHost()) return;

    // Apply lobby settings to config and rules before starting
    config_.mapWidth       = lobbySettings_.mapWidth;
    config_.mapHeight      = lobbySettings_.mapHeight;
    config_.playerMaxHp    = lobbySettings_.playerMaxHp;
    config_.spawnRateScale = lobbySettings_.spawnRateScale;
    config_.enemyHpScale   = lobbySettings_.enemyHpScale;
    config_.enemySpeedScale= lobbySettings_.enemySpeedScale;
    currentRules_.friendlyFire  = lobbySettings_.friendlyFire;
    currentRules_.pvpEnabled    = lobbySettings_.isPvp || lobbySettings_.pvpEnabled;
    currentRules_.upgradesShared= lobbySettings_.upgradesShared;
    currentRules_.teamCount     = lobbySettings_.teamCount;
    currentRules_.lives         = lobbySettings_.livesPerPlayer;
    currentRules_.sharedLives   = lobbySettings_.livesShared;
    player_.maxHp = config_.playerMaxHp;
    player_.hp    = player_.maxHp;

    // Initialise lives tracking
    spectatorMode_ = false;
    matchElapsed_ = 0.0f;
    discordSessionStart_ = (int64_t)time(nullptr);
    matchTimer_   = (lobbySettings_.pvpMatchDuration > 0.0f) ? lobbySettings_.pvpMatchDuration : 0.0f;
    if (currentRules_.lives > 0 && !currentRules_.sharedLives) {
        localLives_ = currentRules_.lives;
    } else {
        localLives_ = -1;
    }
    if (currentRules_.lives > 0 && currentRules_.sharedLives) {
        sharedLives_ = currentRules_.lives * (int)net.players().size();
    } else {
        sharedLives_ = -1;
    }
    if (lobbySettings_.teamCount > 0) {
        // Ensure all clients have the latest settings and know to enter team select
        net.sendConfigSync(lobbySettings_);
        net.sendTeamSelectStart(lobbySettings_.teamCount);
        state_ = GameState::TeamSelect;
        teamSelectCursor_ = 0;
        localTeam_ = 0;
        teamLocked_ = false;
        menuSelection_ = 0;
        return;
    }

    // Send enabled mod data to all clients before starting the game
    {
        auto& mm = ModManager::instance();
        auto modBlob = mm.serializeEnabledMods();
        if (!modBlob.empty()) {
            net.sendModSync(modBlob);
        }
    }

    // Check if a custom map was selected
    std::string customMapFile = net.lobbyInfo().mapFile;
    std::vector<uint8_t> customMapData;

    if (!customMapFile.empty()) {
        // Read the .csm file to send to clients
        FILE* f = fopen(customMapFile.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            customMapData.resize(sz);
            fread(customMapData.data(), 1, sz, f);
            fclose(f);

            // Load custom map for host
            startCustomMapMultiplayer(customMapFile);
            if (lobbySettings_.isPvp) player_.invulnDuration = 0.0f;
            state_ = GameState::MultiplayerGame;
            net.startGame(0, map_.width, map_.height, customMapData);
            respawnTimer_ = currentRules_.respawnTime;

            // ── Splitscreen: init coopSlots for local sub-players (custom map) ──
            {
                int joined = 0;
                for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;
                coopPlayerCount_ = joined;
                if (coopPlayerCount_ > 1) {
                    const int vw = (coopPlayerCount_ >= 2) ? (int)((SCREEN_W/2) / SPLITSCREEN_ZOOM) : SCREEN_W;
                    const int vh = (coopPlayerCount_ >= 3) ? (int)((SCREEN_H/2) / SPLITSCREEN_ZOOM) : (coopPlayerCount_ >= 2) ? (int)(SCREEN_H / SPLITSCREEN_ZOOM) : SCREEN_H;
                    const Vec2 off[4] = {{-80,0},{80,0},{0,-80},{0,80}};
                    int si = 0;
                    for (int i = 0; i < 4; i++) {
                        if (!coopSlots_[i].joined) continue;
                        if (si == 0) {
                            coopSlots_[i].player   = player_;
                            coopSlots_[i].upgrades = upgrades_;
                        } else {
                            coopSlots_[i].player = Player{};
                            coopSlots_[i].player.maxHp     = config_.playerMaxHp;
                            coopSlots_[i].player.hp        = config_.playerMaxHp;
                            coopSlots_[i].player.bombCount = 1;
                            Vec2 sp = player_.pos + off[si % 4];
                            if (!map_.spawnPoints.empty())
                                sp = map_.spawnPoints[si % (int)map_.spawnPoints.size()];
                            coopSlots_[i].player.pos = sp;
                            coopSlots_[i].upgrades.reset();
                        }
                        coopSlots_[i].kills  = 0;
                        coopSlots_[i].deaths = 0;
                        coopSlots_[i].respawnTimer = 0;
                        coopSlots_[i].camera.worldW = map_.worldWidth();
                        coopSlots_[i].camera.worldH = map_.worldHeight();
                        coopSlots_[i].camera.viewW  = vw;
                        coopSlots_[i].camera.viewH  = vh;
                        coopSlots_[i].camera.pos    = {coopSlots_[i].player.pos.x - vw/2.f,
                                                        coopSlots_[i].player.pos.y - vh/2.f};
                        si++;
                    }
                }
            }
            return;
        }
        // If file can't be read, fall through to generated map
        printf("Warning: custom map '%s' not found, using generated map\n", customMapFile.c_str());
    }

    // Generate a shared map seed so host and client produce the same map
    uint32_t mapSeed = (uint32_t)time(nullptr) ^ (uint32_t)rand();
    mapSrand(mapSeed);

    // Start the game — use generated map
    {
        bool savedIsPvp = lobbySettings_.isPvp; // startGame() resets this to false
        startGame();
        lobbySettings_.isPvp = savedIsPvp;      // restore so updateSpawning blocks PvP waves
    }
    player_.pos = pickSpawnPos(); // team corner or random, not map centre
    if (lobbySettings_.isPvp) player_.invulnDuration = 0.0f;
    state_ = GameState::MultiplayerGame;
    net.startGame(mapSeed, config_.mapWidth, config_.mapHeight);
    respawnTimer_ = currentRules_.respawnTime;

    // ── Splitscreen: initialise coopSlots for local sub-players in MP ──
    {
        int joined = 0;
        for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;
        coopPlayerCount_ = joined;
        if (coopPlayerCount_ > 1) {
            const int vw = (coopPlayerCount_ >= 2) ? (int)((SCREEN_W/2) / SPLITSCREEN_ZOOM) : SCREEN_W;
            const int vh = (coopPlayerCount_ >= 3) ? (int)((SCREEN_H/2) / SPLITSCREEN_ZOOM) : (coopPlayerCount_ >= 2) ? (int)(SCREEN_H / SPLITSCREEN_ZOOM) : SCREEN_H;
            const Vec2 off[4] = {{-80,0},{80,0},{0,-80},{0,80}};
            int si = 0;
            for (int i = 0; i < 4; i++) {
                if (!coopSlots_[i].joined) continue;
                if (si == 0) {
                    // Slot 0 = primary player — already initialised by startGame()
                    coopSlots_[i].player   = player_;
                    coopSlots_[i].upgrades = upgrades_;
                } else {
                    coopSlots_[i].player = Player{};
                    coopSlots_[i].player.maxHp     = config_.playerMaxHp;
                    coopSlots_[i].player.hp        = config_.playerMaxHp;
                    coopSlots_[i].player.bombCount = 1;
                    Vec2 sp = player_.pos + off[si % 4];
                    if (!map_.spawnPoints.empty())
                        sp = map_.spawnPoints[si % (int)map_.spawnPoints.size()];
                    coopSlots_[i].player.pos = sp;
                    coopSlots_[i].upgrades.reset();
                }
                coopSlots_[i].kills  = 0;
                coopSlots_[i].deaths = 0;
                coopSlots_[i].respawnTimer = 0;
                coopSlots_[i].camera.worldW = map_.worldWidth();
                coopSlots_[i].camera.worldH = map_.worldHeight();
                coopSlots_[i].camera.viewW  = vw;
                coopSlots_[i].camera.viewH  = vh;
                coopSlots_[i].camera.pos    = {coopSlots_[i].player.pos.x - vw/2.f,
                                                coopSlots_[i].player.pos.y - vh/2.f};
                si++;
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Pickup / Crate Update & Render
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateCrates(float dt) {
    // Auto-spawn crates on a timer (every ~20-30 seconds)
    // In multiplayer, only the host spawns crates — clients receive them via network
    auto& net = NetworkManager::instance();
    bool isMultiplayer = net.isOnline();
    bool isSimDelegate = net.isConnectedToDedicated() && net.isLobbyHost(); // lobby-host on dedicated server
    bool shouldSpawn = !sandboxMode_ && (!isMultiplayer || net.isHost() || isSimDelegate);

    if (shouldSpawn) {
        crateSpawnTimer_ -= dt;
        if (crateSpawnTimer_ <= 0) {
            // In PVP mode, use the lobby-configured fixed interval;
            // otherwise use random 20-30s
            if (lobbySettings_.isPvp)
                crateSpawnTimer_ = lobbySettings_.crateInterval;
            else
                crateSpawnTimer_ = 20.0f + (float)(rand() % 100) / 10.0f; // 20-30s

            // Find a random open position
            for (int attempts = 0; attempts < 20; attempts++) {
                int tx = 2 + rand() % (map_.width - 4);
                int ty = 2 + rand() % (map_.height - 4);
                if (map_.get(tx, ty) == TILE_GRASS) {
                    Vec2 pos = {TileMap::toWorld(tx), TileMap::toWorld(ty)};
                    spawnCrate(pos);
                    cratePopupTimer_ = 2.5f;  // trigger "SUPPLY DROP" popup
                    break;
                }
            }
        }
    }

    // Update existing crates
    for (auto& c : crates_) {
        if (!c.alive) continue;
        c.bobTimer += dt * 2.5f;
        c.glowTimer += dt * 3.0f;

        // Bullet-crate collision
        for (auto& b : bullets_) {
            if (!b.alive) continue;
            if (circleOverlap(b.pos, b.size, c.pos, 20.0f)) {
                c.takeDamage(b.damage);
                b.alive = false;
                if (!c.alive) {
                    // Crate destroyed — spawn pickup
                    Pickup p;
                    p.pos = c.pos;
                    p.type = c.contents;
                    pickups_.push_back(p);

                    // Spawn wood fragments
                    for (int i = 0; i < 10; i++) {
                        BoxFragment f;
                        f.pos = c.pos;
                        float angle = (float)(rand() % 360) * (float)M_PI / 180.0f;
                        float spd = 100.0f + (float)(rand() % 200);
                        f.vel = {cosf(angle) * spd, sinf(angle) * spd};
                        f.size = 3.0f + (float)(rand() % 5);
                        f.lifetime = 0.4f + (float)(rand() % 30) / 100.0f;
                        f.age = 0;
                        f.alive = true;
                        f.rotation = (float)(rand() % 360);
                        f.rotSpeed = (float)(rand() % 400 - 200);
                        f.color = {(Uint8)(140 + rand() % 60), (Uint8)(90 + rand() % 50), (Uint8)(40 + rand() % 20), 255};
                        boxFragments_.push_back(f);
                    }
                    camera_.addShake(2.5f);
                    screenFlashTimer_ = 0.06f;
                    screenFlashR_ = 255; screenFlashG_ = 200; screenFlashB_ = 50;
                    if (sfxBreak_) { int ch = Mix_PlayChannel(-1, sfxBreak_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                }
                break;
            }
        }
    }

    // Remove dead crates
    crates_.erase(std::remove_if(crates_.begin(), crates_.end(),
        [](const PickupCrate& c) { return !c.alive; }), crates_.end());
}

void Game::updatePickups(float dt) {
    for (auto& p : pickups_) {
        if (!p.alive) continue;
        p.age += dt;
        p.bobTimer += dt * 3.0f;
        p.flashTimer += dt;

        // Despawn after lifetime
        if (p.age >= p.lifetime) {
            p.alive = false;
            continue;
        }

        // Player collection — walk over it (spectators can't collect)
        float collectRadius = 28.0f;
        if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
            // Co-op: any alive player can collect
            for (int ci = 0; ci < 4 && p.alive; ci++) {
                if (!coopSlots_[ci].joined || coopSlots_[ci].player.dead) continue;
                if (circleOverlap(p.pos, collectRadius, coopSlots_[ci].player.pos, PLAYER_SIZE * 0.5f)) {
                    // Swap in the collecting player's state
                    Player savedP = player_; PlayerUpgrades savedU = upgrades_;
                    player_ = coopSlots_[ci].player; upgrades_ = coopSlots_[ci].upgrades;
                    collectPickup(p);
                    coopSlots_[ci].player = player_; coopSlots_[ci].upgrades = upgrades_;
                    player_ = savedP; upgrades_ = savedU;
                    // Sync slot 0 if it was the collector
                    if (ci == 0) { player_ = coopSlots_[0].player; upgrades_ = coopSlots_[0].upgrades; }
                }
            }
        } else if (!player_.dead && !spectatorMode_ && circleOverlap(p.pos, collectRadius, player_.pos, PLAYER_SIZE * 0.5f)) {
            collectPickup(p);
        }
    }

    // Remove dead pickups
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(),
        [](const Pickup& p) { return !p.alive; }), pickups_.end());
}

void Game::spawnCrate(Vec2 pos) {
    PickupCrate crate;
    crate.pos = pos;
    crate.contents = rollRandomUpgrade();
    crates_.push_back(crate);

    // Notify network
    auto& net = NetworkManager::instance();
    if ((net.isHost() || (net.isConnectedToDedicated() && net.isLobbyHost())) && net.isInGame()) {
        net.sendCrateSpawn(pos, (uint8_t)crate.contents);
    }
}

void Game::collectPickup(Pickup& p) {
    p.alive = false;
    applyUpgrade(p.type);

    // UI flash feedback
    const auto& info = getUpgradeInfo(p.type);
    screenFlashTimer_ = 0.08f;
    screenFlashR_ = info.color.r;
    screenFlashG_ = info.color.g;
    screenFlashB_ = info.color.b;
    camera_.addShake(1.0f);

    // Pickup name popup banner (same style as wave announce)
    pickupPopupTimer_ = 2.5f;
    pickupPopupName_ = info.name;
    pickupPopupDesc_ = info.description;
    pickupPopupColor_ = info.color;

    // Notify network
    auto& net = NetworkManager::instance();
    if (net.isOnline()) {
        net.sendPickupCollect(p.pos, (uint8_t)p.type, net.localPlayerId());
    }
}

void Game::applyUpgrade(UpgradeType type) {
    upgrades_.apply(type);

    // Also apply direct effects to player
    switch (type) {
        case UpgradeType::SpeedUp:
            player_.speed += 40.0f;
            break;
        case UpgradeType::DamageUp:
            // Handled by upgrades_.damageMulti in bullet spawn
            break;
        case UpgradeType::FireRateUp:
            player_.fireRate = player_.fireRate / 0.85f; // increase shots/sec
            break;
        case UpgradeType::AmmoUp:
            player_.maxAmmo += 5;
            player_.ammo = player_.maxAmmo;
            break;
        case UpgradeType::HealthUp:
            player_.maxHp += 1;
            player_.hp = player_.maxHp; // full heal
            break;
        case UpgradeType::ReloadUp:
            player_.reloadTime = std::max(0.2f, player_.reloadTime * 0.8f);
            break;
        case UpgradeType::Blindness:
            player_.invulnerable = true;
            player_.invulnTimer = 5.0f;
            break;
        case UpgradeType::BombPickup:
            player_.bombCount = std::min(MAX_BOMBS, player_.bombCount + 3);
            break;
        case UpgradeType::Overclock:
            player_.fireRate = player_.fireRate / 0.80f;
            player_.reloadTime = std::max(0.18f, player_.reloadTime * 0.85f);
            break;
        case UpgradeType::HeavyRounds:
            player_.fireRate = std::max(1.5f, player_.fireRate * 0.90f);
            break;
        case UpgradeType::BombCore:
            player_.bombCount = std::min(MAX_BOMBS, player_.bombCount + 1);
            break;
        case UpgradeType::Juggernaut:
            player_.maxHp += 2;
            player_.hp = std::min(player_.maxHp, player_.hp + 2);
            player_.speed = std::max(180.0f, player_.speed - 35.0f);
            break;
        case UpgradeType::StunRounds:
        case UpgradeType::Scavenger:
        case UpgradeType::ExplosiveTips:
        case UpgradeType::ChainLightning:
            break;
        case UpgradeType::RailSlugs:
            player_.fireRate = std::max(1.8f, player_.fireRate * 0.94f);
            break;
        case UpgradeType::SharpenedEdge:
        case UpgradeType::Bloodlust:
        case UpgradeType::ShockEdge:
            break;
        case UpgradeType::AutoReloader:
        case UpgradeType::Vampire:
        case UpgradeType::LastStand:
            break; // handled via flags in upgrades_
        case UpgradeType::SlowDown:
            player_.speed = std::max(200.0f, player_.speed - 60.0f);
            break;
        case UpgradeType::GlassCannon:
            player_.maxHp = std::max(1, player_.maxHp - 1);
            player_.hp = std::min(player_.hp, player_.maxHp);
            // But damage goes way up
            break;
        default:
            break;
    }

    printf("Upgrade applied: %s\n", getUpgradeInfo(type).name);
}

void Game::renderCrates() {
    for (auto& c : crates_) {
        if (!c.alive) continue;
        Vec2 sp = camera_.worldToScreen(c.pos);
        int screenX = (int)sp.x;
        int screenY = (int)sp.y;
        // Only render if on screen
        if (screenX < -64 || screenX > SCREEN_W + 64 || screenY < -64 || screenY > SCREEN_H + 64) continue;
        bool glowing = (sinf(c.glowTimer) > 0.5f);
        drawCratePixelArt(renderer_, screenX, screenY, 28, sinf(c.bobTimer) * 3.0f, glowing);
    }
}

void Game::renderPickups() {
    for (auto& p : pickups_) {
        if (!p.alive) continue;
        Vec2 sp = camera_.worldToScreen(p.pos);
        int screenX = (int)sp.x;
        int screenY = (int)sp.y;
        if (screenX < -64 || screenX > SCREEN_W + 64 || screenY < -64 || screenY > SCREEN_H + 64) continue;
        float flash = (p.age > p.lifetime - 2.0f) ? sinf(p.flashTimer * 10.0f) : 0;
        drawPickupPixelArt(renderer_, screenX, screenY, 20, p.type, sinf(p.bobTimer) * 4.0f, flash);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Multiplayer Menu Rendering
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderMultiplayerMenu() {
    ui_.drawDarkOverlay(235, 6, 8, 16);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color blue   = {80, 200, 255, 255};
    SDL_Color dimCyan = {0, 140, 130, 255};

    drawTextCentered("MULTIPLAYER", SCREEN_H / 8, 38, cyan);
    ui_.drawSeparator(SCREEN_W / 2, SCREEN_H / 8 + 48, 100);

    // ── Left column: Main actions ──
    int leftX = SCREEN_W / 4;
    int actionY = SCREEN_H / 4 + 20;
    ui_.drawPanel(leftX - 170, actionY - 40, 340, 220, {10, 12, 24, 240}, {0, 180, 160, 90});

    struct MenuItem { const char* label; const char* desc; SDL_Color accent; };
    MenuItem items[] = {
        {"HOST GAME",       "Create a server for others to join", {50, 255, 150, 255}},
        {"IP CONNECT",      "Enter IP and connect directly",      blue},
        {"BACK",            "",                                    {255, 100, 100, 255}},
    };
    int count = 3;
    int stepY = 50;

    for (int i = 0; i < count; i++) {
        bool sel = (multiMenuSelection_ == i);
        int itemY = actionY + i * stepY;
        if (ui_.menuItem(i, items[i].label, leftX, itemY, 280, 38,
                         items[i].accent, sel, 20, 24)) {
            multiMenuSelection_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) multiMenuSelection_ = i;
        if (sel && items[i].desc[0]) {
            int dw = ui_.textWidth(items[i].desc, 11);
            ui_.drawText(items[i].desc, leftX - dw / 2, itemY + 28, 11, {90, 90, 100, 255});
        }
    }

    // ── Right column: Saved Servers ──
    int rightX = SCREEN_W * 3 / 4 - 100;
    int serverStartY = SCREEN_H / 4 - 10;

    // Panel background
    ui_.drawPanel(rightX - 20, serverStartY - 30, 350, SCREEN_H / 2 + 60, {10, 12, 24, 240}, {0, 180, 160, 90});

    drawText("SAVED SERVERS", rightX, serverStartY - 20, 16, dimCyan);
    SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 40);
    SDL_Rect sLine = {rightX, serverStartY + 2, 200, 1};
    SDL_RenderFillRect(renderer_, &sLine);

    if (savedServers_.empty()) {
        drawText("No saved servers", rightX, serverStartY + 18, 14, {60, 60, 70, 200});
        drawText("Connect to a server", rightX, serverStartY + 36, 11, {50, 50, 60, 180});
        drawText("and save it from there", rightX, serverStartY + 50, 11, {50, 50, 60, 180});
    } else {
        int sy = serverStartY + 14;
        int maxVisible = 7;
        int startIdx = 0;
        if (serverListSelection_ >= maxVisible) startIdx = serverListSelection_ - maxVisible + 1;

        for (int i = startIdx; i < (int)savedServers_.size() && (i - startIdx) < maxVisible; i++) {
            bool sel = (multiMenuSelection_ == 3 + i);
            auto& s = savedServers_[i];

            // Click detection for server rows
            int rowX = rightX - 8, rowTop = sy - 4, rowW = 330, rowH = 32;
            bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, rowX, rowTop, rowW, rowH);
            if (hovered && !usingGamepad_) { multiMenuSelection_ = 3 + i; sel = true; }
            if (hovered) ui_.hoveredItem = 10 + (i - startIdx);
            if (hovered && ui_.mouseClicked) {
                multiMenuSelection_ = 3 + i;
                confirmInput_ = true;
            }

            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 30);
                SDL_Rect row = {rowX, rowTop, rowW, rowH};
                SDL_RenderFillRect(renderer_, &row);
                SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 120);
                SDL_Rect acc = {rowX, rowTop, 2, rowH};
                SDL_RenderFillRect(renderer_, &acc);
            }

            // Server name
            SDL_Color nameC = sel ? cyan : white;
            drawText(s.name.c_str(), rightX, sy, sel ? 16 : 14, nameC);

            // Address below name
            char addrBuf[128];
            snprintf(addrBuf, sizeof(addrBuf), "%s:%d", s.address.c_str(), s.port);
            drawText(addrBuf, rightX, sy + 16, 10, {80, 80, 90, 200});

            sy += 36;
        }

        // Scroll indicators
        if (startIdx > 0) {
            drawText("\xe2\x96\xb2", rightX + 300, serverStartY + 10, 12, gray);
        }
        if (startIdx + maxVisible < (int)savedServers_.size()) {
            drawText("\xe2\x96\xbc", rightX + 300, serverStartY + maxVisible * 36, 12, gray);
        }
    }

    // Username display
    char uname[128];
    snprintf(uname, sizeof(uname), "Playing as: %s", config_.username.c_str());
    drawTextCentered(uname, SCREEN_H - 72, 14, dimCyan);

    // Controls
    if (multiMenuSelection_ >= 3 && !savedServers_.empty()) {
        UI::HintPair hints[] = { {UI::Action::Confirm, "Connect"}, {UI::Action::Bomb, "Delete"}, {UI::Action::Back, "Back"} };
        ui_.drawHintBar(hints, 3);
    } else {
        UI::HintPair hints[] = { {UI::Action::Confirm, "Select"}, {UI::Action::Back, "Back"} };
        ui_.drawHintBar(hints, 2);
    }
}

void Game::renderHostSetup() {
    ui_.drawDarkOverlay(232, 6, 8, 16);

    const SDL_Color cyan    = UI::Color::Cyan;
    const SDL_Color white   = UI::Color::White;
    const SDL_Color gray    = UI::Color::Gray;
    const SDL_Color green   = UI::Color::Green;
    const SDL_Color yellow  = UI::Color::Yellow;
    const SDL_Color red     = UI::Color::Red;
    const SDL_Color dimCyan = UI::Color::DimCyan;

    int panelW = 980;
    int panelH = 620;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    int leftW = 280;
    int rightX = px + leftW + 26;
    int rightW = panelW - leftW - 42;

    ui_.drawPanel(px, py, panelW, panelH, {10, 12, 26, 242}, {0, 180, 160, 95});
    ui_.drawTextCentered("HOST SESSION", py + 20, 34, cyan);
    ui_.drawTextCentered("Set the essentials here, then fine-tune everything else inside the lobby.", py + 58, 13, dimCyan);
    ui_.drawSeparator(SCREEN_W / 2, py + 84, 180, {0, 180, 160, 70});

    auto drawBadge = [&](int x, int y, int w, SDL_Color accent, const char* title, const std::string& value) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, accent.r / 6, accent.g / 6, accent.b / 6, 230);
        SDL_Rect bg = {x, y, w, 44};
        SDL_RenderFillRect(renderer_, &bg);
        SDL_SetRenderDrawColor(renderer_, accent.r, accent.g, accent.b, 120);
        SDL_RenderDrawRect(renderer_, &bg);
        drawText(title, x + 10, y + 7, 10, gray);
        drawText(value.c_str(), x + 10, y + 21, 16, accent);
    };

    std::string ip = getLocalIP();
    std::string accessLabel = lobbyPassword_.empty() ? "Open lobby" : "Password locked";
    std::string modeLabel = lobbySettings_.isPvp ? "PvP skirmish" : "Co-op survival";

    drawBadge(px + 24, py + 102, leftW - 20, cyan, "LOCAL ADDRESS", ip + ":" + std::to_string(hostPort_));
    drawBadge(px + 24, py + 154, leftW - 20, lobbyPassword_.empty() ? green : yellow, "ACCESS", accessLabel);
    drawBadge(px + 24, py + 206, leftW - 20, lobbySettings_.isPvp ? yellow : green, "PLAYSTYLE", modeLabel);

    ui_.drawPanel(px + 18, py + 266, leftW - 8, 224, {8, 10, 20, 220}, {0, 120, 110, 90});
    drawText("SESSION SNAPSHOT", px + 34, py + 284, 14, gray);
    drawText(config_.username.c_str(), px + 34, py + 316, 22, white);

    std::string mapLabel = "Generated arena";
    if (hostMapSelectIdx_ > 0 && hostMapSelectIdx_ <= (int)mapFiles_.size()) {
        mapLabel = mapFiles_[hostMapSelectIdx_ - 1];
        size_t slash = mapLabel.rfind('/'); if (slash == std::string::npos) slash = mapLabel.rfind('\\');
        if (slash != std::string::npos) mapLabel = mapLabel.substr(slash + 1);
        size_t dot = mapLabel.rfind('.'); if (dot != std::string::npos) mapLabel = mapLabel.substr(0, dot);
    }

    char summary1[96];
    const char* teamSummary = (lobbySettings_.teamCount == 4) ? "4 teams" : (lobbySettings_.teamCount == 2 ? "2 teams" : "open teams");
    snprintf(summary1, sizeof(summary1), "%d slots • %s", hostMaxPlayers_, teamSummary);
    drawText(summary1, px + 34, py + 348, 14, dimCyan);

    std::string livesSummary = (lobbySettings_.livesPerPlayer == 0)
        ? "infinite lives"
        : std::to_string(lobbySettings_.livesPerPlayer) + " lives";
    char summary2[96]; snprintf(summary2, sizeof(summary2), "HP %d • %s", lobbySettings_.playerMaxHp, livesSummary.c_str());
    drawText(summary2, px + 34, py + 370, 14, dimCyan);
    drawText("Map", px + 34, py + 406, 11, gray);
    drawText(mapLabel.c_str(), px + 34, py + 422, 16, white);
    drawText("Objective", px + 34, py + 452, 11, gray);
    if (lobbySettings_.isPvp) {
        char obj[64];
        if (lobbySettings_.pvpMatchDuration <= 0.0f) snprintf(obj, sizeof(obj), "Last team alive wins");
        else snprintf(obj, sizeof(obj), "%d:%02d round timer", (int)lobbySettings_.pvpMatchDuration / 60, (int)lobbySettings_.pvpMatchDuration % 60);
        drawText(obj, px + 34, py + 468, 16, white);
    } else {
        char obj[64];
        if (lobbySettings_.waveCount == 0) snprintf(obj, sizeof(obj), "Endless waves");
        else snprintf(obj, sizeof(obj), "%d-wave run", lobbySettings_.waveCount);
        drawText(obj, px + 34, py + 468, 16, white);
    }

    ui_.drawPanel(px + 18, py + 502, leftW - 8, 92, {8, 10, 20, 220}, {0, 120, 110, 90});
    drawText("TIP", px + 34, py + 520, 12, yellow);
    drawText("Use presets for recurring lobbies.", px + 34, py + 542, 13, gray);
    drawText("The lobby still exposes full tuning later.", px + 34, py + 562, 13, gray);

    ui_.drawPanel(rightX - 12, py + 102, rightW + 24, panelH - 126, {8, 10, 20, 220}, {0, 120, 110, 90});
    drawText("HOST OPTIONS", rightX + 4, py + 118, 18, white);
    drawText("Stick/arrows navigate. Hold to scroll. Confirm edits or applies.", rightX + 4, py + 140, 12, gray);

    // ── Auto-scroll to keep selection visible ──
    {
        // Y offsets (from py+168) of each option row, and heights
        static const int ROW_REL_Y[] = { 20, 58, 96, 134, 200, 238, 276, 314, 352, 390, 428, 494, 544, 592 };
        static const int ROW_REL_H[] = { 34, 34, 34, 34,  34,  34,  34,  34,  34,  34,  34,  34,  38,  34 };
        constexpr int VIS_H   = 420;
        constexpr int MAX_SCR = 206;
        if (hostSetupSelection_ >= 0 && hostSetupSelection_ < 14) {
            int iy = ROW_REL_Y[hostSetupSelection_];
            int ih = ROW_REL_H[hostSetupSelection_];
            if (iy < hostSetupScrollY_)              hostSetupScrollY_ = iy;
            if (iy + ih > hostSetupScrollY_ + VIS_H) hostSetupScrollY_ = iy + ih - VIS_H;
            hostSetupScrollY_ = std::max(0, std::min(MAX_SCR, hostSetupScrollY_));
        }
    }

    // ── Row helpers (defined before scissor so lambdas capture correctly) ──
    auto sectionHeader = [&](const char* label, int y) {
        drawText(label, rightX + 4, y, 11, dimCyan);
        SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 80);
        SDL_Rect sep = {rightX + 92, y + 7, rightW - 112, 1};
        SDL_RenderFillRect(renderer_, &sep);
    };

    auto drawOptionRow = [&](int idx, int y, const char* label, const std::string& value,
                             const char* hint, SDL_Color accent, bool editable = true, bool dim = false) {
        SDL_Color border = dim ? SDL_Color{70, 74, 90, 255} : accent;
        bool sel = (hostSetupSelection_ == idx);
        // For mouse hit-test use the UN-scrolled y so click still works
        SDL_Rect row = {rightX, y, rightW, 34};
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, row.x, row.y, row.w, row.h);
        if (hovered) ui_.hoveredItem = idx;
        if (hovered && !usingGamepad_) { hostSetupSelection_ = idx; menuSelection_ = idx; }
        if (hovered && ui_.mouseClicked) { hostSetupSelection_ = idx; menuSelection_ = idx; confirmInput_ = true; }

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, border.r / 6, border.g / 6, border.b / 6, sel ? 220 : 165);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(renderer_, border.r, border.g, border.b, sel ? 170 : 70);
        SDL_RenderDrawRect(renderer_, &row);
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, accent.r, accent.g, accent.b, 220);
            SDL_Rect bar = {row.x, row.y, 4, row.h};
            SDL_RenderFillRect(renderer_, &bar);
        }

        SDL_Color labelColor = dim ? SDL_Color{85, 90, 105, 255} : (sel ? white : gray);
        SDL_Color valueColor = dim ? SDL_Color{95, 100, 115, 255} : accent;
        drawText(label, rightX + 14, y + 8, 14, labelColor);

        std::string shown = value;
        if (sel && editable) shown = "< " + shown + " >";
        ui_.drawTextRight(shown.c_str(), rightX + rightW - 12, y + 8, 15, valueColor);
        if (hint && hint[0]) drawText(hint, rightX + 14, y + 23, 10, dimCyan);
    };

    // ── Scissor-clip the rows area to the right panel ──
    SDL_Rect prevClipRect;
    SDL_RenderGetClipRect(renderer_, &prevClipRect);
    {
        SDL_Rect clip = {rightX - 12, py + 162, rightW + 24, py + 589 - (py + 162)};
        SDL_RenderSetClipRect(renderer_, &clip);
    }

    int rowY = py + 168 - hostSetupScrollY_;
    sectionHeader("NETWORK", rowY); rowY += 20;

    drawOptionRow(0, rowY, "Max players", std::to_string(hostMaxPlayers_), "How many players can join this session.", cyan); rowY += 38;

    std::string portDisplay = portTyping_ ? portStr_ : std::to_string(hostPort_);
    if (portTyping_) portDisplay += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
    drawOptionRow(1, rowY, "Port", portDisplay, "Change if another app already uses 7777.", yellow, false); rowY += 38;

    std::string userDisplay = config_.username;
    if (mpUsernameTyping_) userDisplay += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
    drawOptionRow(2, rowY, "Host name", userDisplay, "Shown to everyone in the lobby.", green, false); rowY += 38;

    std::string passwordDisplay;
    if (hostPasswordTyping_) {
        passwordDisplay = lobbyPassword_;
        passwordDisplay += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
    } else if (lobbyPassword_.empty()) {
        passwordDisplay = "Open";
    } else {
        passwordDisplay = std::string(lobbyPassword_.size(), '*');
    }
    drawOptionRow(3, rowY, "Password", passwordDisplay, "Leave empty for a public lobby.", lobbyPassword_.empty() ? green : yellow, false); rowY += 46;

    sectionHeader("MATCH", rowY); rowY += 20;

    drawOptionRow(4, rowY, "Mode", lobbySettings_.isPvp ? "PvP" : "PvE", "Switches between versus and survival defaults.", lobbySettings_.isPvp ? yellow : green); rowY += 38;
    drawOptionRow(5, rowY, "Map", mapLabel, hostMapSelectIdx_ == 0 ? "Generated map. Custom maps can still override size." : "Cycles through discovered custom maps.", cyan); rowY += 38;
    drawOptionRow(6, rowY, "Teams", teamSummary, "Useful for team battles or squad runs.", yellow); rowY += 38;
    drawOptionRow(7, rowY, "Player HP", std::to_string(lobbySettings_.playerMaxHp), "Quick durability tuning before players join.", green); rowY += 38;
    drawOptionRow(8, rowY, "Lives", livesSummary, "Zero means endless respawns.", cyan); rowY += 38;

    std::string objectiveLabel;
    if (lobbySettings_.isPvp) {
        if (lobbySettings_.pvpMatchDuration <= 0.0f) objectiveLabel = "Unlimited";
        else {
            char buf[32]; snprintf(buf, sizeof(buf), "%d:%02d", (int)lobbySettings_.pvpMatchDuration / 60, (int)lobbySettings_.pvpMatchDuration % 60);
            objectiveLabel = buf;
        }
    } else {
        objectiveLabel = (lobbySettings_.waveCount == 0) ? "Endless" : std::to_string(lobbySettings_.waveCount) + " waves";
    }
    drawOptionRow(9, rowY, lobbySettings_.isPvp ? "Round timer" : "Wave goal", objectiveLabel,
                  lobbySettings_.isPvp ? "Zero keeps the match open-ended." : "Zero runs endless survival.", lobbySettings_.isPvp ? yellow : green); rowY += 38;

    bool ffForced = lobbySettings_.isPvp && lobbySettings_.teamCount == 0;
    std::string ffLabel = ffForced ? "Forced on" : (lobbySettings_.friendlyFire ? "On" : "Off");
    drawOptionRow(10, rowY, lobbySettings_.isPvp ? "Friendly fire" : "PvP damage", ffLabel,
                  ffForced ? "FFA PvP always allows player damage." : "Lets allies damage each other.",
                  ffForced ? yellow : red, !ffForced, ffForced); rowY += 46;

    sectionHeader("PRESET", rowY); rowY += 20;
    std::string presetLabel = serverPresets_.empty() ? "No presets saved" : serverPresets_[presetSelection_ % (int)serverPresets_.size()].name;
    drawOptionRow(11, rowY, "Load preset", presetLabel,
                  serverPresets_.empty() ? "Save presets from the lobby to reuse them here." : "Left/right chooses. Confirm applies.",
                  cyan, !serverPresets_.empty(), serverPresets_.empty()); rowY += 50;

    bool startSel = (hostSetupSelection_ == 12);
    if (ui_.menuItem(12, "START HOSTING", rightX + rightW / 2, rowY, rightW, 38, green, startSel, 22, 24)) {
        hostSetupSelection_ = 12;
        menuSelection_ = 12;
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 12 && !usingGamepad_) { hostSetupSelection_ = 12; menuSelection_ = 12; }
    rowY += 48;

    bool backSel = (hostSetupSelection_ == 13);
    if (ui_.menuItem(13, "BACK", rightX + rightW / 2, rowY, rightW, 34, white, backSel, 18, 20)) {
        hostSetupSelection_ = 13;
        menuSelection_ = 13;
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 13 && !usingGamepad_) { hostSetupSelection_ = 13; menuSelection_ = 13; }

    // ── Restore clip rect and draw scrollbar ──
    SDL_RenderSetClipRect(renderer_, prevClipRect.w > 0 ? &prevClipRect : nullptr);
    {
        constexpr int CONTENT_H = 626;
        constexpr int VIS_H     = 420;
        constexpr int MAX_SCR   = 206;
        if (MAX_SCR > 0) {
            int sbX    = rightX + rightW + 2;
            int sbTopY = py + 168;
            int sbH    = VIS_H;
            // track
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 40, 46, 60, 160);
            SDL_Rect sbTrack = {sbX, sbTopY, 4, sbH};
            SDL_RenderFillRect(renderer_, &sbTrack);
            // thumb
            int thumbH = std::max(20, VIS_H * VIS_H / CONTENT_H);
            int thumbY = sbTopY + (int)((long long)hostSetupScrollY_ * (sbH - thumbH) / MAX_SCR);
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 200);
            SDL_Rect sbThumb = {sbX, thumbY, 4, thumbH};
            SDL_RenderFillRect(renderer_, &sbThumb);
        }
    }

    if (softKB_.active) {
        renderSoftKB(py + panelH - 44);
    }

    UI::HintPair hints[] = {
        {UI::Action::Navigate, "Move / adjust"},
        {UI::Action::Confirm, "Edit / apply"},
        {UI::Action::Back, "Cancel"}
    };
    ui_.drawHintBar(hints, 3, py + panelH - 18);
}

void Game::renderJoinMenu() {
    // ── Background ──
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    // ── Centered card panel ──
    int panelW = 480, panelH = 520;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2 - 10;
    ui_.drawPanel(px, py, panelW, panelH);

    // Title
    ui_.drawTextCentered("JOIN GAME", py + 22, 30, UI::Color::Cyan);
    ui_.drawSeparator(SCREEN_W / 2, py + 58, 80);

    // Connection status
    auto& net = NetworkManager::instance();
    if (net.state() == NetState::Connecting) {
        ui_.drawTextCentered("Connecting...", py + 70, 16, UI::Color::Yellow);
    } else if (!connectStatus_.empty()) {
        SDL_Color statusColor = UI::Color::Red;
        if (connectStatus_.find("saved") != std::string::npos ||
            connectStatus_.find("Saved") != std::string::npos) {
            statusColor = UI::Color::Green;
        }
        ui_.drawTextCentered(connectStatus_.c_str(), py + 70, 14, statusColor);
    }

    ui_.drawTextCentered("IP or hostname (example: 192.168.1.10 or play.example.com)",
                         py + 86, 10, UI::Color::HintGray);

    int fieldY = py + 92;
    int fieldStep = 54;
    int fieldW = panelW - 60;
    int fieldX = px + 30;

    // ── Clickable text input field helper ──
    auto drawField = [&](int idx, const char* label, const std::string& value, bool editing) {
        bool sel = (joinMenuSelection_ == idx);
        int fy = fieldY;

        // Field box background
        SDL_SetRenderDrawColor(renderer_, 16, 18, 30, 255);
        SDL_Rect box = {fieldX, fy, fieldW, 44};
        SDL_RenderFillRect(renderer_, &box);

        // Border color — bright when editing, teal when selected, dim otherwise
        SDL_Color border = editing ? SDL_Color{0, 255, 228, 200} :
                          sel     ? SDL_Color{0, 140, 130, 180} :
                                    SDL_Color{40, 44, 60, 160};
        SDL_SetRenderDrawColor(renderer_, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer_, &box);

        // Left accent when editing
        if (editing) {
            SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 200);
            SDL_Rect acc = {fieldX, fy, 3, 44};
            SDL_RenderFillRect(renderer_, &acc);
        }

        // Label (small, above the value)
        ui_.drawText(label, fieldX + 10, fy + 4, 10, UI::Color::Gray);

        // Value with optional cursor blink
        std::string display = value;
        if (editing) {
            display += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
        }
        SDL_Color valC = editing ? UI::Color::Cyan :
                        (sel ? UI::Color::White : SDL_Color{160, 160, 170, 255});
        ui_.drawText(display.empty() ? " " : display.c_str(), fieldX + 10, fy + 20, 18, valC);

        // Click detection
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, fieldX, fy, fieldW, 44);
        if (hovered && !usingGamepad_) { menuSelection_ = idx; joinMenuSelection_ = idx; }
        if (hovered) ui_.hoveredItem = idx;
        if (hovered && ui_.mouseClicked) {
            menuSelection_ = idx; joinMenuSelection_ = idx;
            confirmInput_ = true;
        }

        fieldY += fieldStep;
    };

    // ── Four input fields ──
    drawField(0, "IP / HOST", joinAddress_, ipTyping_);

    {
        std::string pStr = joinPortTyping_ ? joinPortStr_ : std::to_string(joinPort_);
        drawField(1, "PORT", pStr, joinPortTyping_);
    }

    drawField(2, "USERNAME", config_.username, mpUsernameTyping_);

    {
        std::string pwDisp;
        if (joinPasswordTyping_) pwDisp = joinPassword_;
        else if (!joinPassword_.empty()) pwDisp = std::string(joinPassword_.size(), '*');
        drawField(3, "PASSWORD", pwDisp, joinPasswordTyping_);
    }

    // ── Soft keyboard (gamepad/touch) or action buttons ──
    if (softKB_.active) {
        renderSoftKB(fieldY + 6);
    } else {
        // Action buttons
        int btnY = fieldY + 8;

        if (ui_.menuItem(4, "CONNECT", SCREEN_W / 2, btnY, fieldW, 42,
                         UI::Color::Green, joinMenuSelection_ == 4, 22, 26)) {
            menuSelection_ = 4; joinMenuSelection_ = 4;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == 4 && !usingGamepad_) { menuSelection_ = 4; joinMenuSelection_ = 4; }
        btnY += 48;

        if (ui_.menuItem(5, "SAVE SERVER", SCREEN_W / 2, btnY, fieldW, 34,
                         {0, 200, 180, 255}, joinMenuSelection_ == 5, 18, 20)) {
            menuSelection_ = 5; joinMenuSelection_ = 5;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == 5 && !usingGamepad_) { menuSelection_ = 5; joinMenuSelection_ = 5; }
        btnY += 40;

        if (ui_.menuItem(6, "BACK", SCREEN_W / 2, btnY, 200, 32,
                         UI::Color::Red, joinMenuSelection_ == 6, 18, 20)) {
            menuSelection_ = 6; joinMenuSelection_ = 6;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == 6 && !usingGamepad_) { menuSelection_ = 6; joinMenuSelection_ = 6; }

        UI::HintPair hints[] = { {UI::Action::Confirm, "Select"}, {UI::Action::Navigate, "Navigate"}, {UI::Action::Back, "Back"} };
        ui_.drawHintBar(hints, 3);
    }
}

void Game::renderLobby() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    auto& net = NetworkManager::instance();
    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color green  = {50, 255, 100, 255};
    SDL_Color dimGrn = {30, 160, 60, 255};
    SDL_Color yellow = {255, 220, 60, 255};
    SDL_Color red    = {255, 80, 80, 255};

    // If still connecting (client), show a connecting screen instead of full lobby
    if (!net.isHost() && net.state() == NetState::Connecting) {
        drawTextCentered("CONNECTING...", SCREEN_H / 2 - 60, 34, cyan);
        float remaining = connectTimer_;
        if (remaining < 0) remaining = 0;
        char timBuf[64];
        snprintf(timBuf, sizeof(timBuf), "Timeout in %.1fs", remaining);
        drawTextCentered(timBuf, SCREEN_H / 2, 18, gray);

        // Animated dots
        int dots = ((int)(gameTime_ * 3)) % 4;
        char dotBuf[8] = "";
        for (int i = 0; i < dots; i++) strcat(dotBuf, ".");
        char statusBuf[128];
        snprintf(statusBuf, sizeof(statusBuf), "Connecting to %s:%d%s", joinAddress_.c_str(), joinPort_, dotBuf);
        drawTextCentered(statusBuf, SCREEN_H / 2 + 40, 14, {80, 80, 90, 255});

        // Progress bar
        float prog = 1.0f - (remaining / 5.0f);
        if (prog < 0) prog = 0; if (prog > 1) prog = 1;
        int barW = 300, barH = 4;
        int bx = (SCREEN_W - barW) / 2;
        int by = SCREEN_H / 2 + 70;
        SDL_SetRenderDrawColor(renderer_, 30, 30, 40, 180);
        SDL_Rect bgBar = {bx, by, barW, barH};
        SDL_RenderFillRect(renderer_, &bgBar);
        SDL_SetRenderDrawColor(renderer_, 0, 200, 180, 255);
        SDL_Rect fgBar = {bx, by, (int)(barW * prog), barH};
        SDL_RenderFillRect(renderer_, &fgBar);

        { UI::HintPair hints[] = { {UI::Action::Back, "Cancel"} };
          ui_.drawHintBar(hints, 1, SCREEN_H - 40); }
        return;
    }

    // Title
    drawTextCentered("LOBBY", SCREEN_H / 10, 34, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 60, SCREEN_H / 10 + 42, 120, 1};
    SDL_RenderFillRect(renderer_, &tl);

    int readyPlayers = 0;
    for (const auto& p : net.players()) if (p.ready || p.id == net.lobbyHostId()) readyPlayers++;

    auto drawTopBadge = [&](int x, int y, int w, SDL_Color accent, const char* title, const std::string& value) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, accent.r / 7, accent.g / 7, accent.b / 7, 220);
        SDL_Rect bg = {x, y, w, 40};
        SDL_RenderFillRect(renderer_, &bg);
        SDL_SetRenderDrawColor(renderer_, accent.r, accent.g, accent.b, 110);
        SDL_RenderDrawRect(renderer_, &bg);
        drawText(title, x + 10, y + 6, 10, gray);
        drawText(value.c_str(), x + 10, y + 20, 14, accent);
    };

    std::string hostName = net.lobbyInfo().hostName.empty() ? config_.username : net.lobbyInfo().hostName;
    std::string modeSummary = lobbySettings_.isPvp ? "PvP" : "PvE";
    if (lobbySettings_.teamCount == 2) modeSummary += " • 2 teams";
    else if (lobbySettings_.teamCount == 4) modeSummary += " • 4 teams";
    else modeSummary += " • open teams";

    std::string mapSummary = (lobbyMapIdx_ == 0) ? "Generated arena" : net.lobbyInfo().mapName;
    if (mapSummary.empty()) mapSummary = "Generated arena";
    std::string accessSummary = lobbyPassword_.empty() ? "Open" : "Password locked";

    int badgeY = SCREEN_H / 10 + 58;
    int badgeW = 230;
    int badgeGap = 14;
    int badgeX = (SCREEN_W - (badgeW * 4 + badgeGap * 3)) / 2;
    drawTopBadge(badgeX, badgeY, badgeW, cyan, "HOST", hostName);
    drawTopBadge(badgeX + (badgeW + badgeGap), badgeY, badgeW, lobbySettings_.isPvp ? yellow : green, "MODE", modeSummary);
    drawTopBadge(badgeX + (badgeW + badgeGap) * 2, badgeY, badgeW, white, "MAP", mapSummary);
    drawTopBadge(badgeX + (badgeW + badgeGap) * 3, badgeY, badgeW,
                 readyPlayers == (int)net.players().size() ? green : yellow,
                 "READY", std::to_string(readyPlayers) + "/" + std::to_string((int)net.players().size()) + " • " + accessSummary);

    // ══════════════════════════════════════════════════════════
    //  Settings panel (right side) — host can adjust, clients read-only
    // ══════════════════════════════════════════════════════════
    {
        bool isHostPlayer = net.isLobbyHost();
        int panelX = SCREEN_W / 2 + 20;
        int panelY = SCREEN_H / 10 + 114;
        int panelW = SCREEN_W / 2 - 40;
        int panelH = SCREEN_H - panelY - 156;

        ui_.drawPanel(panelX - 10, panelY - 10, panelW + 20, panelH + 52,
                      {10, 12, 24, 236}, {0, 120, 110, 90});

        drawText("SETTINGS", panelX, panelY, 16, gray);
        if (isHostPlayer) drawText("Host can tweak these live before starting.", panelX + 110, panelY + 2, 11, {60, 60, 70, 255});
        else drawText("Read-only mirror of the host configuration.", panelX + 110, panelY + 2, 11, {60, 60, 70, 255});
        SDL_SetRenderDrawColor(renderer_, 60, 60, 70, 100);
        SDL_Rect hdrLine = {panelX, panelY + 22, panelW, 1};
        SDL_RenderFillRect(renderer_, &hdrLine);

        int rowStep = 34;
        int rowsVisH = panelH - 36;  // visible height for rows (below header, above hint)
        int clipTop  = panelY + 28;
        int clipBot  = panelY + panelH - 8;

        // ── Auto-scroll to keep selection visible (approximate, 1 row = rowStep) ──
        {
            int rowTop = 28 + lobbySettingsSel_ * rowStep;  // relative to panelY
            if (rowTop < lobbySettingsScrollY_)
                lobbySettingsScrollY_ = rowTop;
            if (rowTop + rowStep > lobbySettingsScrollY_ + rowsVisH)
                lobbySettingsScrollY_ = rowTop + rowStep - rowsVisH;
            lobbySettingsScrollY_ = std::max(0, lobbySettingsScrollY_);
        }

        int rowY = panelY + 30 - lobbySettingsScrollY_;

        // Scissor-clip the row area to the panel
        SDL_Rect prevClip;
        SDL_RenderGetClipRect(renderer_, &prevClip);
        {
            SDL_Rect rowClip = {panelX - 10, clipTop, panelW + 20, clipBot - clipTop};
            SDL_RenderSetClipRect(renderer_, &rowClip);
        }

        auto drawSettingRow = [&](int idx, const char* label, const char* value, SDL_Color valColor = {255,255,255,255}) {
            bool sel = isHostPlayer && (lobbySettingsSel_ == idx);
            SDL_Color lc = sel ? white : gray;

            // Click detection (host only)
            if (isHostPlayer) {
                bool rowVisible = (rowY >= clipTop - rowStep) && (rowY < clipBot);
                bool hovered = rowVisible && ui_.pointInRect(ui_.mouseX, ui_.mouseY, panelX - 4, rowY - 2, panelW + 8, rowStep - 2);
                if (hovered) ui_.hoveredItem = 100 + idx;
                if (hovered && !usingGamepad_) { lobbySettingsSel_ = idx; menuSelection_ = idx; }
                if (hovered && ui_.mouseClicked) {
                    lobbySettingsSel_ = idx; menuSelection_ = idx;
                    confirmInput_ = true;
                }
            }

            if (sel) {
                // Highlight row
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 25);
                SDL_Rect bg = {panelX - 4, rowY - 2, panelW + 8, rowStep - 2};
                SDL_RenderFillRect(renderer_, &bg);
                SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 160);
                SDL_Rect bar = {panelX - 4, rowY - 2, 3, rowStep - 2};
                SDL_RenderFillRect(renderer_, &bar);
            }

            char buf[128];
            if (sel)
                snprintf(buf, sizeof(buf), "%s  < %s >", label, value);
            else
                snprintf(buf, sizeof(buf), "%s  %s", label, value);
            drawText(buf, panelX + 4, rowY, sel ? 16 : 14, lc);
            (void)valColor; // could tint value separately - keep simple
            rowY += rowStep;
        };

        // 0: Gamemode (PVP vs PVE)
        {
            const char* val = lobbySettings_.isPvp ? "PVP" : "PVE";
            drawSettingRow(0, "Gamemode:", val);
        }

        // 1: PvP toggle — shown in PvE mode as "PvP" (player damage), in PvP team mode as "Friendly Fire",
        //    greyed-out (implicit ON) in PvP no-teams mode.
        {
            bool pvpNoTeams = lobbySettings_.isPvp && lobbySettings_.teamCount == 0;
            if (pvpNoTeams) {
                // Greyed out — PvP is always-on in this mode
                char buf2[128]; snprintf(buf2, sizeof(buf2), "PvP:  ON");
                drawText(buf2, panelX + 4, rowY, 14, SDL_Color{55, 55, 65, 255});
                rowY += rowStep;
            } else if (!lobbySettings_.isPvp) {
                // PvE mode — show an explicit PvP (friendly fire) toggle
                const char* val = lobbySettings_.friendlyFire ? "ON" : "OFF";
                drawSettingRow(1, "PvP:", val,
                    lobbySettings_.friendlyFire ? red : (SDL_Color){80,200,120,255});
            } else {
                // PvP team mode — show as "Friendly Fire"
                const char* val = lobbySettings_.friendlyFire ? "ON" : "OFF";
                drawSettingRow(1, "Friendly Fire:", val,
                    lobbySettings_.friendlyFire ? red : (SDL_Color){80,200,120,255});
            }
        }

        // 2: Upgrades
        {
            const char* val = lobbySettings_.upgradesShared ? "Shared (all players)" : "Individual (picker only)";
            drawSettingRow(2, "Upgrades:", val);
        }

        // 3: Map selection (Generated or custom map file)
        {
            std::string mapLabel = "Generated";
            if (lobbyMapIdx_ > 0 && lobbyMapIdx_ <= (int)mapFiles_.size()) {
                std::string mf = mapFiles_[lobbyMapIdx_ - 1];
                size_t sl = mf.rfind('/'); if (sl == std::string::npos) sl = mf.rfind('\\');
                std::string base = (sl != std::string::npos) ? mf.substr(sl + 1) : mf;
                size_t dot = base.rfind('.'); if (dot != std::string::npos) base = base.substr(0, dot);
                mapLabel = base;
            }
            if (mapFiles_.empty()) mapLabel = "Generated (no custom maps)";
            drawSettingRow(3, "Map:", mapLabel.c_str());
        }

        // 4: Map width (greyed out when custom map selected)
        {
            bool custom = (lobbyMapIdx_ > 0);
            char v[16];
            if (custom) snprintf(v, sizeof(v), "(custom)");
            else        snprintf(v, sizeof(v), "%d", lobbySettings_.mapWidth);
            SDL_Color dc = custom ? SDL_Color{55,55,65,255} : SDL_Color{255,255,255,255};
            // dim the row text manually when custom map is active
            bool saved_sel = isHostPlayer && (lobbySettingsSel_ == 4);
            if (custom && !saved_sel) {
                // draw as plain dim text, no arrows
                char buf[128]; snprintf(buf, sizeof(buf), "Map Width:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, dc);
                rowY += rowStep;
            } else {
                drawSettingRow(4, "Map Width:", v);
            }
        }

        // 5: Map height (greyed out when custom map selected)
        {
            bool custom = (lobbyMapIdx_ > 0);
            char v[16];
            if (custom) snprintf(v, sizeof(v), "(custom)");
            else        snprintf(v, sizeof(v), "%d", lobbySettings_.mapHeight);
            SDL_Color dc = custom ? SDL_Color{55,55,65,255} : SDL_Color{255,255,255,255};
            bool saved_sel = isHostPlayer && (lobbySettingsSel_ == 5);
            if (custom && !saved_sel) {
                char buf[128]; snprintf(buf, sizeof(buf), "Map Height:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, dc);
                rowY += rowStep;
            } else {
                drawSettingRow(5, "Map Height:", v);
            }
        }

        // 6: Enemy HP (greyed out in PVP)
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.enemyHpScale);
            if (lobbySettings_.isPvp) {
                char buf[128]; snprintf(buf, sizeof(buf), "Enemy HP:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, SDL_Color{55,55,65,255});
                rowY += rowStep;
            } else {
                drawSettingRow(6, "Enemy HP:", v);
            }
        }

        // 7: Enemy speed (greyed out in PVP)
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.enemySpeedScale);
            if (lobbySettings_.isPvp) {
                char buf[128]; snprintf(buf, sizeof(buf), "Enemy Speed:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, SDL_Color{55,55,65,255});
                rowY += rowStep;
            } else {
                drawSettingRow(7, "Enemy Speed:", v);
            }
        }

        // 8: Spawn rate (greyed out in PVP)
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.spawnRateScale);
            if (lobbySettings_.isPvp) {
                char buf[128]; snprintf(buf, sizeof(buf), "Spawn Rate:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, SDL_Color{55,55,65,255});
                rowY += rowStep;
            } else {
                drawSettingRow(8, "Spawn Rate:", v);
            }
        }

        // 9: Player HP
        {
            char v[16]; snprintf(v, sizeof(v), "%d", lobbySettings_.playerMaxHp);
            drawSettingRow(9, "Player HP:", v);
        }

        // 10: Team count
        {
            const char* val = (lobbySettings_.teamCount == 4) ? "4 Teams" :
                              (lobbySettings_.teamCount == 2) ? "2 Teams" : "None";
            drawSettingRow(10, "Teams:", val);
        }

        // 11: Lives per player (up to 100)
        {
            char v[16];
            if (lobbySettings_.livesPerPlayer == 0)
                snprintf(v, sizeof(v), "Infinite");
            else
                snprintf(v, sizeof(v), "%d", lobbySettings_.livesPerPlayer);
            drawSettingRow(11, "Lives:", v);
        }

        // 12: Lives mode (only shown when lives are limited)
        if (lobbySettings_.livesPerPlayer > 0) {
            const char* val = lobbySettings_.livesShared ? "Shared Pool" : "Individual";
            drawSettingRow(12, "Lives Mode:", val);
        }

        // 13 / 12: Crate Interval (PVP) or Wave Count (PVE) — index depends on whether LivesMode is visible
        {
            int condIdx = (lobbySettings_.livesPerPlayer > 0) ? 13 : 12;
            if (lobbySettings_.isPvp) {
                char v[16]; snprintf(v, sizeof(v), "%.0fs", lobbySettings_.crateInterval);
                drawSettingRow(condIdx, "Crate Interval:", v);
            } else {
                char v[16];
                if (lobbySettings_.waveCount == 0)
                    snprintf(v, sizeof(v), "Endless");
                else
                    snprintf(v, sizeof(v), "%d", lobbySettings_.waveCount);
                drawSettingRow(condIdx, "Waves:", v);
            }
            // Match Time (PVP only)
            int matchTimeIdx  = -1;
            int presetSaveIdx = condIdx + 1;
            int presetLoadIdx = condIdx + 2;
            if (lobbySettings_.isPvp) {
                matchTimeIdx  = condIdx + 1;
                presetSaveIdx = condIdx + 2;
                presetLoadIdx = condIdx + 3;
                char v[16];
                if (lobbySettings_.pvpMatchDuration <= 0.0f)
                    snprintf(v, sizeof(v), "Unlimited");
                else
                    snprintf(v, sizeof(v), "%d:%02d",
                             (int)lobbySettings_.pvpMatchDuration / 60,
                             (int)lobbySettings_.pvpMatchDuration % 60);
                drawSettingRow(matchTimeIdx, "Match Time:", v);
            }
            drawSettingRow(presetSaveIdx, "Save Preset:", "[confirm]");
            {
                const char* presetLabel = serverPresets_.empty()
                    ? "(none saved)"
                    : serverPresets_[presetSelection_ % (int)serverPresets_.size()].name.c_str();
                drawSettingRow(presetLoadIdx, "Load Preset:", presetLabel);
            }
        }

        const char* selectionHint = "Choose a row to see what it changes.";
        switch (lobbySettingsSel_) {
            case 0: selectionHint = "Swap between co-op survival and player-vs-player rules."; break;
            case 1: selectionHint = lobbySettings_.isPvp ? "Controls ally damage in team PvP." : "Lets players hurt each other in PvE."; break;
            case 2: selectionHint = "Shared upgrades help coordinated runs; individual rewards selfish play."; break;
            case 3: selectionHint = "Generated maps use the size fields below. Custom maps override them."; break;
            case 4: case 5: selectionHint = "Map size only affects generated arenas."; break;
            case 6: case 7: case 8: selectionHint = "Enemy tuning only matters in PvE survival."; break;
            case 9: selectionHint = "Raise HP for slower, tankier rounds."; break;
            case 10: selectionHint = "Teams are optional in PvE and define squads in PvP."; break;
            case 11: case 12: selectionHint = "Lives can turn matches into elimination rounds."; break;
            default: selectionHint = lobbySettings_.isPvp ? "Use presets to reuse good competitive rulesets." : "Wave goals decide whether the run is endless or finite."; break;
        }
        drawText(selectionHint, panelX + 4, panelY + panelH + 14, 11, UI::Color::DimCyan);

        // Restore clip rect
        SDL_RenderSetClipRect(renderer_, prevClip.w > 0 ? &prevClip : nullptr);

        // ── Scrollbar ──
        {
            int contentH = 17 * rowStep;  // approximate total content height
            if (contentH > rowsVisH) {
                int maxScroll = contentH - rowsVisH;
                int sbX = panelX + panelW + 4;
                int sbH = rowsVisH;
                int sbY = clipTop;
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer_, 40, 46, 60, 160);
                SDL_Rect track = {sbX, sbY, 4, sbH};
                SDL_RenderFillRect(renderer_, &track);
                int thumbH = std::max(16, sbH * rowsVisH / contentH);
                int thumbY = sbY + (int)((long long)lobbySettingsScrollY_ * (sbH - thumbH) / maxScroll);
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 200);
                SDL_Rect thumb = {sbX, thumbY, 4, thumbH};
                SDL_RenderFillRect(renderer_, &thumb);
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Player list (left side)
    // ══════════════════════════════════════════════════════════
    int listX = 60;
    int listY = SCREEN_H / 10 + 114;
    int listPanelW = SCREEN_W / 2 - 92;
    int listPanelH = SCREEN_H - listY - 156;
    ui_.drawPanel(listX - 18, listY - 10, listPanelW + 36, listPanelH + 52,
                  {10, 12, 24, 236}, {0, 120, 110, 90});
    drawText("PLAYERS", listX, listY, 16, gray);
    bool canManageLobby = net.isLobbyHost();
    bool isHostInKickMode = canManageLobby && (lobbyKickCursor_ >= 0);
    if (canManageLobby) {
        SDL_Color kickHint = isHostInKickMode
            ? SDL_Color{255, 80, 80, 220}
            : SDL_Color{80, 80, 90, 200};
        const char* kickLabel = isHostInKickMode
            ? "\xe2\x9c\x96 HOST ACTIONS  [A] Kick  [X] Transfer Host  [B] Cancel"
            : "[Y/TAB] Kick / Transfer Host";
        drawText(kickLabel, listX + 100, listY + 2, 12, kickHint);
    }
    SDL_SetRenderDrawColor(renderer_, 60, 60, 70, 100);
    SDL_Rect hdr = {listX - 4, listY + 22, SCREEN_W / 2 - 60, 1};
    SDL_RenderFillRect(renderer_, &hdr);

    // Team colors for display
    static const SDL_Color teamColors[4] = {
        {255, 80, 80, 255}, {80, 140, 255, 255}, {80, 255, 100, 255}, {255, 220, 60, 255}
    };

    const auto& players = net.players();
    int py = listY + 32;
    for (size_t i = 0; i < players.size(); i++) {
        bool isLocal = (players[i].id == net.localPlayerId());
        bool isHostP = (players[i].id == net.lobbyHostId());
        bool isKickTarget = isHostInKickMode && ((int)i == lobbyKickCursor_);

        // Row background — local player teal, kick target red
        if (isKickTarget) {
            SDL_SetRenderDrawColor(renderer_, 180, 30, 30, 40);
            SDL_Rect row = {listX - 8, py - 4, SCREEN_W / 2 - 40, 28};
            SDL_RenderFillRect(renderer_, &row);
            SDL_SetRenderDrawColor(renderer_, 255, 80, 80, 180);
            SDL_Rect bar = {listX - 8, py - 4, 3, 28};
            SDL_RenderFillRect(renderer_, &bar);
        } else if (isLocal) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
            SDL_Rect row = {listX - 8, py - 4, SCREEN_W / 2 - 40, 28};
            SDL_RenderFillRect(renderer_, &row);
        }

        // Ready indicator
        if (players[i].ready) {
            SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 255);
            SDL_Rect dot = {listX, py + 6, 8, 8};
            SDL_RenderFillRect(renderer_, &dot);
        } else {
            SDL_SetRenderDrawColor(renderer_, 100, 100, 110, 180);
            SDL_Rect dotOutline = {listX, py + 6, 8, 8};
            SDL_RenderDrawRect(renderer_, &dotOutline);
        }

        // Name (team-colored if team assigned; kick target shown in red)
        char entryBuf[128];
        snprintf(entryBuf, sizeof(entryBuf), "%s%s%s", players[i].username.c_str(),
                 isHostP ? "  \xe2\x98\x85" : "",
                 isKickTarget ? "  \xe2\x9c\x96" : "");
        SDL_Color nameC = isKickTarget ? SDL_Color{255, 90, 90, 255}
                        : isLocal      ? yellow
                        : (players[i].ready ? green : gray);
        if (!isKickTarget && players[i].team >= 0 && players[i].team < 4)
            nameC = teamColors[players[i].team];
        drawText(entryBuf, listX + 16, py, 18, nameC);

        // Per-player ping
        if (!isLocal) {
            uint32_t peerPing = net.getPlayerPing(players[i].id);
            if (peerPing > 0) {
                char pingBuf[32];
                snprintf(pingBuf, sizeof(pingBuf), "%dms", peerPing);
                SDL_Color pc = (peerPing < 50) ? SDL_Color{50, 255, 100, 160} :
                               (peerPing < 100) ? SDL_Color{255, 220, 60, 160} :
                               SDL_Color{255, 80, 80, 160};
                drawText(pingBuf, listX + SCREEN_W / 2 - 120, py + 2, 14, pc);
            }
        }

        py += 30;

        // Render local sub-players under each network player row.
        int subCount = (int)players[i].localSubPlayers;
        if (subCount < 0) subCount = 0;
        if (subCount > 3) subCount = 3;
        for (int s = 0; s < subCount; s++) {
            std::string subLabel;
            if (isLocal) {
                int found = 0;
                for (int si = 1; si < 4; si++) {
                    if (!coopSlots_[si].joined) continue;
                    if (found == s) {
                        subLabel = coopSlots_[si].username.empty() ? ("local-" + std::to_string(s + 1)) : coopSlots_[si].username;
                        break;
                    }
                    found++;
                }
                if (subLabel.empty()) subLabel = "local-" + std::to_string(s + 1);
            } else {
                subLabel = "local-" + std::to_string(s + 1);
            }
            std::string rowText = std::string("\xe2\x86\xb3 ") + subLabel;
            drawText(rowText.c_str(), listX + 32, py - 1, 13, SDL_Color{120, 150, 170, 220});
            py += 18;
        }
    }

    // ── Bottom buttons ──
    int actionPanelW = 520;
    int actionPanelH = 78;
    int actionPanelX = (SCREEN_W - actionPanelW) / 2;
    int actionPanelY = SCREEN_H - 110;
    ui_.drawPanel(actionPanelX, actionPanelY, actionPanelW, actionPanelH,
                  {10, 12, 24, 236}, {0, 120, 110, 90});
    int btnY = actionPanelY + 12;
    if (canManageLobby) {
        bool allReady = true;
        for (auto& p : players) { if (!p.ready && p.id != net.lobbyHostId()) allReady = false; }
        SDL_Color startC = allReady ? green : dimGrn;
        if (ui_.menuItem(50, "START GAME", SCREEN_W / 2, btnY, 300, 36,
                         startC, true, 22, 26)) {
            confirmInput_ = true;
        }
        if (!allReady && players.size() > 1) {
            drawTextCentered("Waiting for everyone else to ready up.", actionPanelY + 48, 13, gray);
        }
    } else {
        const char* rdyLabel = lobbyReady_ ? "READY!" : "READY UP";
        SDL_Color rdyC = lobbyReady_ ? green : white;
        if (ui_.menuItem(50, rdyLabel, SCREEN_W / 2, btnY, 300, 36,
                         rdyC, true, 22, 26)) {
            confirmInput_ = true;
        }
        drawTextCentered(lobbyReady_ ? "You are ready. Confirm again to unready." : "Confirm once you are happy with the setup.",
                         actionPanelY + 48, 13, gray);
    }
    { UI::HintPair hints[] = { {UI::Action::Back, "Leave"}, {UI::Action::Navigate, "Navigate/Adjust"} };
      ui_.drawHintBar(hints, 2, SCREEN_H - 40); }
    if (canManageLobby) {
        const char* kickHintStr = isHostInKickMode
            ? "[Y/TAB] Exit action mode    [A] Kick    [X] Transfer Host    [B] Cancel"
            : "[Y/TAB] Kick / Transfer Host mode";
        drawTextCentered(kickHintStr, SCREEN_H - 22, 12,
                         isHostInKickMode ? SDL_Color{255, 120, 80, 220} : SDL_Color{80, 80, 90, 180});
    }
}

void Game::renderMultiplayerGame() {
    // Reuse the standard gameplay rendering — this is called from render()
    // The actual gameplay scene is rendered in the Playing case, we just add MP HUD on top
    renderMultiplayerHUD();
}

void Game::renderMultiplayerHUD() {
    auto& net = NetworkManager::instance();
    if (!net.isOnline()) return;

    // Remote player names and health bars
    const auto& players = net.players();
    for (auto& rp : players) {
        if (rp.id == net.localPlayerId()) continue;
        if (!rp.alive) continue;

        Vec2 sp = camera_.worldToScreen(rp.pos);
        if (sp.x < -50 || sp.x > SCREEN_W + 50 || sp.y < -50 || sp.y > SCREEN_H + 50) continue;

        // Health bar — drawn first so name tag renders on top
        float barW = 40.0f;
        float barH = 4.0f;
        float hpRatio = (rp.maxHp > 0) ? (float)rp.hp / rp.maxHp : 0;
        SDL_FRect bgBar = {sp.x - barW / 2, sp.y - 28, barW, barH};
        SDL_FRect fgBar = {sp.x - barW / 2, sp.y - 28, barW * hpRatio, barH};

        // Name tag — above the HP bar
        {
            // Team colors for name tags
            static const SDL_Color teamNameColors[4] = {
                {255, 120, 120, 200}, {120, 160, 255, 200}, {120, 255, 140, 200}, {255, 230, 100, 200}
            };
            SDL_Color nameColor = {200, 200, 255, 200}; // default
            if (rp.team >= 0 && rp.team < 4) nameColor = teamNameColors[rp.team];

            TTF_Font* nf = Assets::instance().font(12);
            if (nf) {
                SDL_Surface* ns = TTF_RenderText_Blended(nf, rp.username.c_str(), nameColor);
                if (ns) {
                    SDL_Texture* nt = SDL_CreateTextureFromSurface(renderer_, ns);
                    // Name sits 6px above the HP bar
                    SDL_Rect nd = {(int)sp.x - ns->w / 2, (int)sp.y - 46, ns->w, ns->h};
                    SDL_RenderCopy(renderer_, nt, nullptr, &nd);
                    SDL_DestroyTexture(nt);
                    SDL_FreeSurface(ns);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, 180);
        SDL_RenderFillRectF(renderer_, &bgBar);
        Uint8 hr = (Uint8)(255 * (1.0f - hpRatio));
        Uint8 hg = (Uint8)(255 * hpRatio);
        SDL_SetRenderDrawColor(renderer_, hr, hg, 0, 220);
        SDL_RenderFillRectF(renderer_, &fgBar);
    }

    // Kill/death/score + ping — compact panel in top-right corner
    NetPlayer* local = net.localPlayer();

    // Match timer (PVP: center-top)
    if (lobbySettings_.isPvp) {
        char timeBuf[32];
        SDL_Color timerCol = {220, 220, 220, 220};
        if (lobbySettings_.pvpMatchDuration > 0.0f && matchTimer_ > 0.0f) {
            int secs = (int)matchTimer_;
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", secs / 60, secs % 60);
            // Turn red in last 30 seconds
            if (matchTimer_ < 30.0f) timerCol = {255, 80, 80, 255};
            else if (matchTimer_ < 60.0f) timerCol = {255, 200, 60, 255};
        } else if (lobbySettings_.pvpMatchDuration <= 0.0f) {
            int secs = (int)matchElapsed_;
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", secs / 60, secs % 60);
            timerCol = {160, 160, 180, 180};
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "0:00");
            timerCol = {255, 80, 80, 255};
        }
        // Subtle background
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 70);
        SDL_Rect tBg = {SCREEN_W / 2 - 50, 4, 100, 26};
        SDL_RenderFillRect(renderer_, &tBg);
        drawTextCentered(timeBuf, 6, 20, timerCol);
    }
    {
        uint32_t ping = net.getPing();
        char line1[80] = "";
        if (local) {
            snprintf(line1, sizeof(line1), "K:%d  D:%d  Score:%d",
                     local->kills, local->deaths, local->score);
        }
        SDL_Color pingColor = (ping < 50)  ? SDL_Color{50, 255, 100, 220} :
                              (ping < 100) ? SDL_Color{255, 220, 60, 220} :
                                             SDL_Color{255, 80, 80, 220};
        char line2[48];
        snprintf(line2, sizeof(line2), "%dms  |  %d players", ping, (int)players.size());

        // Subtle background panel
        int pw = 220, ph = 38, pm = 10;
        int px = SCREEN_W - pw - pm;
        int py = pm;
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 90);
        SDL_Rect panel = {px - 6, py - 4, pw + 12, ph + 8};
        SDL_RenderFillRect(renderer_, &panel);

        if (line1[0]) drawText(line1, px, py,     13, {220, 220, 220, 200});
        drawText(line2,              px, py + 20, 11, pingColor);
    }
}

void Game::renderMultiplayerPause() {
    auto& net2 = NetworkManager::instance();
    bool hasTeams     = currentRules_.teamCount >= 2;
    bool isHostPlayer = net2.isLobbyHost();

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 180);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {120, 120, 130, 255};
    SDL_Color red   = {255, 80, 80, 255};
    SDL_Color gold  = {255, 200, 60, 255};

    // Build the item list dynamically
    struct MenuItem { const char* label; SDL_Color col; int idx; bool isVolume; };
    char musBuf[64]; snprintf(musBuf, sizeof(musBuf), "Music: %d%%", config_.musicVolume * 100 / 128);
    char sfxBuf2[64]; snprintf(sfxBuf2, sizeof(sfxBuf2), "SFX: %d%%", config_.sfxVolume * 100 / 128);

    MenuItem items[10];
    int itemCount = 0;

    items[itemCount++] = { "RESUME",      white, 0, false };
    items[itemCount++] = { musBuf,        cyan,  1, true };
    items[itemCount++] = { sfxBuf2,       cyan,  2, true };
    if (hasTeams)     items[itemCount++] = { "CHANGE TEAM",  gold,  3, false };
    if (isHostPlayer) items[itemCount++] = { "ADMIN",        cyan,  hasTeams ? 4 : 3, false };
    if (isHostPlayer) {
        int egIdx = 3 + (hasTeams ? 1 : 0) + 1;
        items[itemCount++] = { "END GAME",    (SDL_Color){255, 160, 60, 255}, egIdx, false };
    }
    int dcIdx = -1;
    if (!isHostPlayer) {
        dcIdx = 3 + (hasTeams ? 1 : 0) + (isHostPlayer ? 1 : 0);
        if (isHostPlayer) dcIdx++; // after End Game
        const char* dcLabel = spectatorMode_ ? "BACK TO LOBBY" : "DISCONNECT";
        items[itemCount++] = { dcLabel, red, dcIdx, false };
    }

    int panelW = 380;
    int panelH = 90 + itemCount * 50 + 20;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;

    SDL_SetRenderDrawColor(renderer_, 12, 14, 26, 240);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 100);
    SDL_RenderDrawRect(renderer_, &panel);

    const char* title = spectatorMode_ ? "SPECTATING" : "PAUSED";
    drawTextCentered(title, py + 24, 30, spectatorMode_ ? gold : cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect sep = {px + 30, py + 62, panelW - 60, 1};
    SDL_RenderFillRect(renderer_, &sep);

    // ── Team-pick sub-state ────────────────────────────────────────────
    if (pauseMenuSub_ == 1) {
        drawTextCentered("CHOOSE TEAM", py + 80, 22, white);
        static const SDL_Color teamColors[] = {
            {255, 100, 100, 255}, {100, 100, 255, 255},
            {100, 220, 100, 255}, {255, 180, 60, 255}
        };
        int tc = currentRules_.teamCount; if (tc < 2) tc = 2;
        int boxW = 70, boxH = 40, gap = 12;
        int totalW = tc * (boxW + gap) - gap;
        int bx0 = (SCREEN_W - totalW) / 2;
        int by  = py + 120;
        for (int t = 0; t < tc; t++) {
            SDL_Color c = (t < 4) ? teamColors[t] : white;
            int bx = bx0 + t * (boxW + gap);
            bool sel = (pauseTeamCursor_ == t);
            SDL_SetRenderDrawColor(renderer_, c.r/4, c.g/4, c.b/4, 200);
            SDL_Rect box = {bx, by, boxW, boxH};
            SDL_RenderFillRect(renderer_, &box);
            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, sel ? 255 : 130);
            SDL_RenderDrawRect(renderer_, &box);
            char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "T%d", t + 1);
            drawTextCentered(tbuf, by + 10, 18, sel ? c : gray);
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 200);
                SDL_Rect cur = {bx + boxW/2 - 4, by + boxH + 4, 8, 4};
                SDL_RenderFillRect(renderer_, &cur);
            }
        }
        drawTextCentered("[A] Select  [B] Back", py + panelH - 24, 14, gray);
        return;
    }

    // ── Normal menu items ──────────────────────────────────────────────
    int itemY = py + 80;
    for (int i = 0; i < itemCount; i++) {
        bool sel = (menuSelection_ == items[i].idx);
        const char* displayLabel = items[i].label;
        char tmp[80];
        if (sel && items[i].isVolume) {
            snprintf(tmp, sizeof(tmp), "< %s >", items[i].label);
            displayLabel = tmp;
        }
        if (ui_.menuItem(items[i].idx, displayLabel, SCREEN_W / 2, itemY, panelW - 40, 32,
                         items[i].col, sel, 20, 22)) {
            menuSelection_ = items[i].idx;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == items[i].idx && !usingGamepad_) menuSelection_ = items[i].idx;
        itemY += 50;
    }

    // Admin overlay on top
    if (adminMenuOpen_) {
        renderAdminMenu();
    }
}

void Game::renderAdminMenu() {
    auto& net2 = NetworkManager::instance();
    const auto& players = net2.players();

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    int panelW = 500, panelH = 60 + (int)players.size() * 44 + 80;
    if (panelH > SCREEN_H - 40) panelH = SCREEN_H - 40;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;

    SDL_SetRenderDrawColor(renderer_, 8, 10, 22, 250);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 200, 100, 0, 160);
    SDL_RenderDrawRect(renderer_, &panel);

    SDL_Color gold  = {255, 200, 60, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {120, 120, 130, 255};
    SDL_Color red   = {255, 80, 80, 255};
    SDL_Color cyan  = {0, 220, 200, 255};

    drawTextCentered("ADMIN MENU", py + 18, 24, gold);
    SDL_SetRenderDrawColor(renderer_, 200, 100, 0, 60);
    SDL_Rect sep = {px + 30, py + 50, panelW - 60, 1};
    SDL_RenderFillRect(renderer_, &sep);

    static const char* actionLabels[] = {"KICK", "RESPAWN", "TEAM-", "TEAM+"};
    static const SDL_Color actionColors[] = {
        {255, 80, 80, 255}, {80, 220, 80, 255},
        {100, 180, 255, 255}, {255, 180, 100, 255}
    };

    int rowY = py + 60;
    for (int i = 0; i < (int)players.size(); i++) {
        const NetPlayer& np = players[i];
        bool rowSel = (adminMenuSel_ == i);

        // Row click → select player
        bool rowHovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, px + 10, rowY - 2, panelW - 20, 38);
        if (rowHovered && !usingGamepad_) adminMenuSel_ = i;

        if (rowSel || rowHovered) {
            SDL_SetRenderDrawColor(renderer_, 200, 100, 0, 30);
            SDL_Rect bar = {px + 10, rowY - 2, panelW - 20, 38};
            SDL_RenderFillRect(renderer_, &bar);
        }

        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "#%d  %s", np.id, np.username.c_str());
        drawText(nameBuf, px + 18, rowY + 8, 16, rowSel ? white : gray);

        // Action buttons
        int actX = px + panelW - 220;
        for (int a = 0; a < 4; a++) {
            bool actSel = rowSel && (adminMenuAction_ == a);
            // Click support for action buttons
            SDL_Rect btn = {actX + a * 50, rowY + 4, 44, 26};
            bool btnHovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, btn.x, btn.y, btn.w, btn.h);
            if (btnHovered && !usingGamepad_) { adminMenuSel_ = i; adminMenuAction_ = a; actSel = true; }
            if (btnHovered) ui_.hoveredItem = 40 + i * 4 + a;
            if (btnHovered && ui_.mouseClicked) {
                adminMenuSel_ = i; adminMenuAction_ = a;
                confirmInput_ = true;
            }
            SDL_SetRenderDrawColor(renderer_,
                actSel ? actionColors[a].r : 30,
                actSel ? actionColors[a].g : 30,
                actSel ? actionColors[a].b : 35, 200);
            SDL_RenderFillRect(renderer_, &btn);
            SDL_SetRenderDrawColor(renderer_, actionColors[a].r, actionColors[a].g, actionColors[a].b, actSel ? 255 : 80);
            SDL_RenderDrawRect(renderer_, &btn);
            drawText(actionLabels[a], actX + a * 50 + 4, rowY + 8, 12, actSel ? actionColors[a] : gray);
        }
        rowY += 44;
    }

    { UI::HintPair hints[] = { {UI::Action::Navigate, "Player/Action"}, {UI::Action::Confirm, "Confirm"}, {UI::Action::Back, "Close"} };
      ui_.drawHintBar(hints, 3, py + panelH - 28); }
}

void Game::renderMultiplayerDeath() {
    // Darken + red tint
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 30, 4, 4, 160);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color red    = {255, 60, 60, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color cyan   = {0, 255, 228, 255};

    drawTextCentered("YOU DIED", SCREEN_H / 2 - 80, 40, red);

    // Lives remaining display
    if (currentRules_.lives > 0) {
        SDL_Color livesColor = (localLives_ > 1) ? white : red;
        char livesBuf[48];
        if (localLives_ > 0)
            snprintf(livesBuf, sizeof(livesBuf), "Lives remaining: %d", localLives_);
        else
            snprintf(livesBuf, sizeof(livesBuf), "NO LIVES LEFT");
        drawTextCentered(livesBuf, SCREEN_H / 2 - 30, 22, livesColor);
    }

    // Respawn countdown
    float totalTime = currentRules_.respawnTime;
    if (totalTime <= 0) totalTime = 3.0f;
    float remaining = respawnTimer_;
    if (remaining < 0) remaining = 0;

    if (remaining > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Respawning in %.1f...", remaining);
        drawTextCentered(buf, SCREEN_H / 2 + 10, 22, white);

        // Progress bar
        int barW = 300, barH = 6;
        int bx = (SCREEN_W - barW) / 2;
        int by = SCREEN_H / 2 + 50;
        float progress = 1.0f - (remaining / totalTime);
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;
        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, 180);
        SDL_Rect bgBar = {bx, by, barW, barH};
        SDL_RenderFillRect(renderer_, &bgBar);
        SDL_SetRenderDrawColor(renderer_, 0, 200, 180, 255);
        SDL_Rect fgBar = {bx, by, (int)(barW * progress), barH};
        SDL_RenderFillRect(renderer_, &fgBar);
    } else {
        if (ui_.menuItem(0, "Respawn", SCREEN_W / 2, SCREEN_H / 2 + 10, 200, 32,
                         cyan, true, 18, 22)) {
            confirmInput_ = true;
        }
    }

    // Stats
    auto& net = NetworkManager::instance();
    NetPlayer* local = net.localPlayer();
    if (local) {
        float kd = (local->deaths > 0) ? (float)local->kills / local->deaths : (float)local->kills;
        char statBuf[128];
        snprintf(statBuf, sizeof(statBuf), "K: %d   D: %d   K/D: %.1f   Score: %d",
                 local->kills, local->deaths, kd, local->score);
        drawTextCentered(statBuf, SCREEN_H / 2 + 90, 16, gray);
    }

    drawTextCentered("TAB - Scoreboard", SCREEN_H - 40, 13, {80, 80, 90, 255});
}

void Game::renderWinLoss() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Background — dark with a tinted overlay
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    const bool isWin = matchResult_.isWin;

    // Banner overlay tint — green for win, red for loss, grey for neutral
    if (matchResult_.reason == MatchEndReason::HostEnded || matchResult_.reason == MatchEndReason::Unknown) {
        SDL_SetRenderDrawColor(renderer_, 60, 60, 80, 40);
    } else if (isWin) {
        SDL_SetRenderDrawColor(renderer_, 20, 180, 60, 40);
    } else {
        SDL_SetRenderDrawColor(renderer_, 180, 20, 20, 40);
    }
    SDL_RenderFillRect(renderer_, &full);

    // Headline colour
    SDL_Color headCol = isWin
        ? SDL_Color{80, 255, 120, 255}
        : (matchResult_.reason == MatchEndReason::HostEnded || matchResult_.reason == MatchEndReason::Unknown)
            ? SDL_Color{180, 180, 200, 255}
            : SDL_Color{255, 60, 60, 255};

    SDL_Color white = {220, 220, 220, 255};
    SDL_Color gray  = {110, 110, 120, 255};
    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color gold  = {255, 200, 50, 255};

    // Headline (VICTORY / DEFEAT / GAME OVER)
    int headY = SCREEN_H / 6;
    drawTextCentered(matchResult_.headline.c_str(), headY, 54, headCol);

    // Decorative separator
    SDL_SetRenderDrawColor(renderer_, headCol.r, headCol.g, headCol.b, 80);
    SDL_Rect sep = {SCREEN_W / 2 - 120, headY + 64, 240, 2};
    SDL_RenderFillRect(renderer_, &sep);

    // Subtitle
    if (!matchResult_.subtitle.empty()) {
        drawTextCentered(matchResult_.subtitle.c_str(), headY + 82, 20, white);
    }

    // Match duration
    {
        int secs = (int)matchResult_.matchElapsed;
        char dur[32];
        snprintf(dur, sizeof(dur), "Match time:  %d:%02d", secs / 60, secs % 60);
        drawTextCentered(dur, headY + 116, 16, gray);
    }

    // Player / team highlight block
    int blockY = headY + 158;

    if (matchResult_.reason == MatchEndReason::TimeUp || matchResult_.reason == MatchEndReason::LastAlive) {
        auto& netR = NetworkManager::instance();
        auto  players = netR.players();

        // Sort by kills descending
        std::sort(players.begin(), players.end(),
                  [](const NetPlayer& a, const NetPlayer& b){ return a.kills > b.kills; });

        const int tableW = 480;
        const int tableX = (SCREEN_W - tableW) / 2;

        // Column headers
        drawText("#",       tableX,         blockY, 13, gray);
        drawText("PLAYER",  tableX + 32,    blockY, 13, gray);
        drawText("KILLS",   tableX + 300,   blockY, 13, gray);
        drawText("STATUS",  tableX + 388,   blockY, 13, gray);
        blockY += 24;

        for (size_t i = 0; i < players.size() && i < 8; i++) {
            bool isLocal = (players[i].id == netR.localPlayerId());
            bool survived = !players[i].spectating;
            SDL_Color rc = isLocal ? gold : white;
            if (!survived) { rc.r = (Uint8)(rc.r / 2); rc.g = (Uint8)(rc.g / 2); rc.b = (Uint8)(rc.b / 2); }

            // Row bg
            if (isLocal) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
                SDL_Rect row = {tableX - 8, blockY - 2, tableW + 16, 24};
                SDL_RenderFillRect(renderer_, &row);
            }

            char rankBuf[4], killBuf[8];
            snprintf(rankBuf, sizeof(rankBuf), "%d", (int)i + 1);
            snprintf(killBuf, sizeof(killBuf), "%d", players[i].kills);

            drawText(rankBuf,               tableX,         blockY, 16, i == 0 ? gold : gray);
            drawText(players[i].username.c_str(), tableX + 32, blockY, 16, rc);
            drawText(killBuf,               tableX + 300,   blockY, 16, rc);
            drawText(survived ? "Alive" : "Eliminated", tableX + 388, blockY, 14,
                     survived ? SDL_Color{50, 220, 80, 255} : SDL_Color{200, 60, 60, 255});
            blockY += 26;
        }
    } else if (matchResult_.reason == MatchEndReason::WavesCleared) {
        // PVE win — show wave count
        char waveBuf[64];
        if (lobbySettings_.waveCount > 0)
            snprintf(waveBuf, sizeof(waveBuf), "Cleared %d waves!", lobbySettings_.waveCount);
        else
            snprintf(waveBuf, sizeof(waveBuf), "All enemies defeated!");
        drawTextCentered(waveBuf, blockY, 22, gold);
        blockY += 36;

        // Show all players survived
        auto& netR = NetworkManager::instance();
        drawTextCentered("All players survived", blockY, 16, {80, 220, 80, 255});
        (void)netR;
    }

    // Hint — clickable button
    if (ui_.menuItem(0, "View Full Scoreboard", SCREEN_W / 2, SCREEN_H - 44, 300, 32,
                     gray, true, 14, 16)) {
        confirmInput_ = true;
    }
    SDL_Color hintLine = isWin ? SDL_Color{50, 200, 80, 100} : SDL_Color{200, 60, 60, 100};
    SDL_SetRenderDrawColor(renderer_, hintLine.r, hintLine.g, hintLine.b, hintLine.a);
    SDL_Rect hintBar = {0, SCREEN_H - 52, SCREEN_W, 2};
    SDL_RenderFillRect(renderer_, &hintBar);
}

void Game::renderScoreboard() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 220);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {220, 220, 220, 255};
    SDL_Color gray   = {100, 100, 110, 255};
    SDL_Color yellow = {255, 255, 100, 255};
    SDL_Color gold   = {255, 200, 50, 255};

    drawTextCentered("SCOREBOARD", SCREEN_H / 10, 34, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 100, SCREEN_H / 10 + 42, 200, 1};
    SDL_RenderFillRect(renderer_, &tl);

    // Table layout — centered
    int tableW = 600;
    int tableX = (SCREEN_W - tableW) / 2;
    int col0 = tableX;       // #
    int col1 = tableX + 40;  // Player
    int col2 = tableX + 320; // Kills
    int col3 = tableX + 410; // Deaths
    int col4 = tableX + 510; // Score

    // Header
    int headerY = SCREEN_H / 5 + 10;
    SDL_SetRenderDrawColor(renderer_, 20, 22, 35, 255);
    SDL_Rect hdrBg = {tableX - 12, headerY - 6, tableW + 24, 28};
    SDL_RenderFillRect(renderer_, &hdrBg);

    drawText("#", col0, headerY, 14, gray);
    drawText("PLAYER", col1, headerY, 14, gray);
    drawText("KILLS", col2, headerY, 14, gray);
    drawText("DEATHS", col3, headerY, 14, gray);
    drawText("SCORE", col4, headerY, 14, gray);

    // Player rows — sorted by score desc
    auto& net = NetworkManager::instance();
    auto players = net.players(); // copy for sorting
    std::sort(players.begin(), players.end(), [](const NetPlayer& a, const NetPlayer& b) {
        return a.score > b.score;
    });

    int y = headerY + 36;
    for (size_t i = 0; i < players.size(); i++) {
        bool isLocal = (players[i].id == net.localPlayerId());

        // Row background
        if (isLocal) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 30);
            SDL_Rect row = {tableX - 12, y - 4, tableW + 24, 28};
            SDL_RenderFillRect(renderer_, &row);
        } else if (i % 2 == 0) {
            SDL_SetRenderDrawColor(renderer_, 16, 18, 28, 180);
            SDL_Rect row = {tableX - 12, y - 4, tableW + 24, 28};
            SDL_RenderFillRect(renderer_, &row);
        }

        SDL_Color c = isLocal ? yellow : white;
        SDL_Color rankC = (i == 0) ? gold : gray;

        char rankBuf[8], killBuf[32], deathBuf[32], scoreBuf[32];
        snprintf(rankBuf, sizeof(rankBuf), "%d", (int)i + 1);
        snprintf(killBuf, sizeof(killBuf), "%d", players[i].kills);
        snprintf(deathBuf, sizeof(deathBuf), "%d", players[i].deaths);
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", players[i].score);

        drawText(rankBuf, col0, y, 18, rankC);
        drawText(players[i].username.c_str(), col1, y, 18, c);
        drawText(killBuf, col2, y, 18, c);
        drawText(deathBuf, col3, y, 18, c);
        drawText(scoreBuf, col4, y, 18, c);
        y += 32;
    }

    if (ui_.menuItem(0, "Continue", SCREEN_W / 2, SCREEN_H - 50, 200, 32,
                     {80, 80, 90, 255}, true, 14, 16)) {
        confirmInput_ = true;
    }
}

void Game::renderRemotePlayers() {
    auto& net = NetworkManager::instance();
    if (!net.isOnline()) return;

    // Team colors for tinting
    static const SDL_Color teamTints[4] = {
        {255, 160, 160, 255}, // Red team - light red tint
        {160, 180, 255, 255}, // Blue team - light blue tint
        {160, 255, 180, 255}, // Green team - light green tint
        {255, 240, 160, 255}, // Yellow team - light yellow tint
    };

    const auto& players = net.players();
    for (auto& rp : players) {
        if (rp.id == net.localPlayerId()) continue;
        if (!rp.alive) continue;

        const std::vector<SDL_Texture*>* bodyFrames = &defaultPlayerSprites_;
        const std::vector<SDL_Texture*>* legFrames = &defaultLegSprites_;
        bool usingCustomFrames = false;
        auto syncedIt = syncedCharacters_.find(rp.id);
        if (syncedIt != syncedCharacters_.end() && !syncedIt->second.isDefault && syncedIt->second.visualLoaded) {
            if (!syncedIt->second.visual.bodySprites.empty()) {
                bodyFrames = &syncedIt->second.visual.bodySprites;
                usingCustomFrames = true;
            }
            if (!syncedIt->second.visual.legSprites.empty()) legFrames = &syncedIt->second.visual.legSprites;
        } else if (rp.customCharacter) {
            for (auto& cd : availableChars_) {
                if (cd.name != rp.characterName) continue;
                if (!cd.bodySprites.empty()) {
                    bodyFrames = &cd.bodySprites;
                    usingCustomFrames = true;
                }
                if (!cd.legSprites.empty()) legFrames = &cd.legSprites;
                break;
            }
        }

        // Position/rotation are already interpolated in network.cpp
        Vec2 drawPos = rp.pos;
        float drawRot = rp.rotation;

        const Uint8 ghostAlpha = 80;
        const bool isGhost = rp.spectating;

        // Legs
        if (rp.moving && !legFrames->empty()) {
            int idx = rp.legFrame % (int)legFrames->size();
            SDL_Texture* legTex = (*legFrames)[idx];
            if (isGhost) SDL_SetTextureAlphaMod(legTex, ghostAlpha);
            renderSprite(legTex, drawPos, rp.legRotation + (float)M_PI / 2, 1.5f);
            if (isGhost) SDL_SetTextureAlphaMod(legTex, 255);
        }

        // Body — tint by team color or default blue
        if (!bodyFrames->empty()) {
            int idx = rp.animFrame % (int)bodyFrames->size();
            SDL_Texture* bodyTex = (*bodyFrames)[idx];
            Vec2 bodyPos = drawPos + Vec2::fromAngle(drawRot) * 6.0f;
            SDL_Color tint = usingCustomFrames ? SDL_Color{255, 255, 255, 255}
                                               : SDL_Color{180, 200, 255, 255};
            if (isGhost) tint = {140, 180, 255, ghostAlpha};
            else if (rp.team >= 0 && rp.team < 4) tint = teamTints[rp.team];
            if (isGhost) SDL_SetTextureAlphaMod(bodyTex, ghostAlpha);
            renderSpriteEx(bodyTex, bodyPos, drawRot + (float)M_PI / 2, 1.5f, tint);
            if (isGhost) SDL_SetTextureAlphaMod(bodyTex, 255);
        }

        // ── Render this client's sub-players (splitscreen partners) ──
        static const SDL_Color subTints[3] = {
            {255, 220, 140, 255}, {140, 255, 180, 255}, {255, 160, 200, 255}
        };
        for (int si = 0; si < (int)rp.subPlayers.size(); si++) {
            auto& sp = rp.subPlayers[si];
            if (!sp.alive) continue;
            Vec2 spPos = sp.pos;
            float spRot = sp.rotation;
            // Legs
            if (sp.moving && !legFrames->empty()) {
                int idx = sp.legFrame % (int)legFrames->size();
                renderSprite((*legFrames)[idx], spPos, sp.legRotation + (float)M_PI / 2, 1.5f);
            }
            // Body — slightly different tint from primary
            if (!bodyFrames->empty()) {
                int idx = sp.animFrame % (int)bodyFrames->size();
                Vec2 bodyPos = spPos + Vec2::fromAngle(spRot) * 6.0f;
                SDL_Color tint = usingCustomFrames ? SDL_Color{255, 255, 255, 255}
                                                   : subTints[si % 3];
                if (rp.team >= 0 && rp.team < 4) tint = teamTints[rp.team]; // use team color if teams
                renderSpriteEx((*bodyFrames)[idx], bodyPos, spRot + (float)M_PI / 2, 1.5f, tint);
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Team Selection Screen
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderTeamSelect() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color green  = {50, 255, 100, 255};

    // Team colors
    static const SDL_Color teamColors[4] = {
        {255, 80, 80, 255}, {80, 140, 255, 255}, {80, 255, 100, 255}, {255, 220, 60, 255}
    };
    static const char* teamNames[4] = { "RED", "BLUE", "GREEN", "YELLOW" };

    int tc = lobbySettings_.teamCount;
    if (tc < 2) tc = 2;

    // Title
    drawTextCentered("CHOOSE YOUR TEAM", SCREEN_H / 6, 34, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 100, SCREEN_H / 6 + 42, 200, 1};
    SDL_RenderFillRect(renderer_, &tl);

    // Team boxes side by side
    int boxW = 160;
    int boxH = 200;
    int gap = 30;
    int totalW = tc * boxW + (tc - 1) * gap;
    int startX = (SCREEN_W - totalW) / 2;
    int boxY = SCREEN_H / 3;

    for (int t = 0; t < tc; t++) {
        int bx = startX + t * (boxW + gap);
        int cx = bx + boxW / 2;  // center x of this box
        bool selected = (teamSelectCursor_ == t);
        bool locked = (teamLocked_ && localTeam_ == t);

        // Helper: center text within this box
        auto drawBoxCentered = [&](const char* text, int y, int size, SDL_Color col) {
            TTF_Font* fnt = Assets::instance().font(size);
            if (!fnt || !text || !text[0]) return;
            SDL_Surface* s = TTF_RenderText_Blended(fnt, text, col);
            if (!s) return;
            SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer_, s);
            SDL_Rect dst = {cx - s->w / 2, y, s->w, s->h};
            SDL_RenderCopy(renderer_, tx, nullptr, &dst);
            SDL_DestroyTexture(tx);
            SDL_FreeSurface(s);
        };

        // Box background
        SDL_Color tc2 = teamColors[t];
        SDL_SetRenderDrawColor(renderer_, tc2.r / 8, tc2.g / 8, tc2.b / 8, selected ? 200 : 120);
        SDL_Rect box = {bx, boxY, boxW, boxH};
        SDL_RenderFillRect(renderer_, &box);

        // Click detection on team box
        if (!teamLocked_) {
            bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, bx, boxY, boxW, boxH);
            if (hovered) ui_.hoveredItem = t;
            if (hovered && !usingGamepad_) teamSelectCursor_ = t;
            if (hovered && ui_.mouseClicked) {
                teamSelectCursor_ = t;
                confirmInput_ = true;
            }
        }

        // Border
        if (selected || locked) {
            SDL_SetRenderDrawColor(renderer_, tc2.r, tc2.g, tc2.b, 255);
            SDL_RenderDrawRect(renderer_, &box);
            // Inner glow
            SDL_Rect inner = {bx + 1, boxY + 1, boxW - 2, boxH - 2};
            SDL_SetRenderDrawColor(renderer_, tc2.r, tc2.g, tc2.b, 80);
            SDL_RenderDrawRect(renderer_, &inner);
        } else {
            SDL_SetRenderDrawColor(renderer_, tc2.r / 3, tc2.g / 3, tc2.b / 3, 180);
            SDL_RenderDrawRect(renderer_, &box);
        }

        // Team name — centered in box
        drawBoxCentered(teamNames[t], boxY + 20, 26, selected ? tc2 : gray);

        // Color swatch
        SDL_SetRenderDrawColor(renderer_, tc2.r, tc2.g, tc2.b, 200);
        SDL_Rect swatch = {cx - 20, boxY + 60, 40, 40};
        SDL_RenderFillRect(renderer_, &swatch);

        // Player count on this team — centered in box
        auto& net = NetworkManager::instance();
        int count = 0;
        for (auto& p : net.players()) {
            if (p.team == t) count++;
        }
        char countBuf[32];
        snprintf(countBuf, sizeof(countBuf), "%d player%s", count, count == 1 ? "" : "s");
        drawBoxCentered(countBuf, boxY + 120, 14, gray);

        // List player names on this team — centered in box
        int nameY = boxY + 140;
        for (auto& p : net.players()) {
            if (p.team == t) {
                drawBoxCentered(p.username.c_str(), nameY, 12, tc2);
                nameY += 16;
                if (nameY > boxY + boxH - 10) break;
            }
        }

        if (locked) {
            drawBoxCentered("LOCKED IN", boxY + boxH + 10, 14, green);
        }
    }

    // Instructions
    if (teamLocked_) {
        drawTextCentered("Waiting for all players to choose...", SCREEN_H - 80, 18, gray);
    } else {
        { UI::HintPair hints[] = { {UI::Action::Left, "Choose"}, {UI::Action::Confirm, "Lock In"}, {UI::Action::Back, "Back"} };
          ui_.drawHintBar(hints, 3, SCREEN_H - 80); }
    }

    // Show how many have chosen
    auto& net = NetworkManager::instance();
    int assigned = 0, total = (int)net.players().size();
    for (auto& p : net.players()) {
        if (p.team >= 0) assigned++;
    }
    char progBuf[64];
    snprintf(progBuf, sizeof(progBuf), "%d / %d players ready", assigned, total);
    drawTextCentered(progBuf, SCREEN_H - 50, 13, {80, 80, 90, 255});
}

// ═════════════════════════════════════════════════════════════════════════════
//  Mod Menu
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderModMenu() {
    ui_.drawDarkOverlay(235, 6, 8, 16);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color dimGray= {80, 80, 90, 255};
    SDL_Color green  = {50, 255, 100, 255};
    SDL_Color red    = {255, 80, 80, 255};
    SDL_Color yellow = {255, 220, 50, 255};

    // Title
    drawTextCentered("MODS & CONTENT", SCREEN_H / 12, 32, cyan);
    ui_.drawSeparator(SCREEN_W / 2, SCREEN_H / 12 + 40, 100);

    // ── Tab bar ──
    static const char* tabNames[] = { "MODS", "CHARACTERS", "MAPS", "PLAYLISTS" };
    static const SDL_Color tabAccents[] = {
        {0, 255, 228, 255}, {255, 200, 50, 255}, {80, 200, 255, 255}, {200, 100, 255, 255}
    };
    int tabCount = 4;
    int tabW = SCREEN_W / tabCount;
    int tabY = SCREEN_H / 12 + 54;

    for (int t = 0; t < tabCount; t++) {
        bool active = (t == modMenuTab_);
        int tx = tabW * t;

        if (active) {
            // Active tab background
            SDL_SetRenderDrawColor(renderer_, tabAccents[t].r / 12, tabAccents[t].g / 12, tabAccents[t].b / 12, 200);
            SDL_Rect tabBg = {tx + 2, tabY - 6, tabW - 4, 30};
            SDL_RenderFillRect(renderer_, &tabBg);
            // Active indicator bar
            SDL_SetRenderDrawColor(renderer_, tabAccents[t].r, tabAccents[t].g, tabAccents[t].b, 255);
            SDL_Rect tabLine = {tx + 2, tabY + 24, tabW - 4, 2};
            SDL_RenderFillRect(renderer_, &tabLine);
        }

        // Tab click support
        if (ui_.pointInRect(ui_.mouseX, ui_.mouseY, tx, tabY - 6, tabW, 32)) {
            if (ui_.mouseClicked && !usingGamepad_) {
                modMenuTab_ = t;
                modMenuSelection_ = 0;
            }
        }

        // Tab label (centered in tab)
        int labelW = (int)strlen(tabNames[t]) * 8;
        drawText(tabNames[t], tx + (tabW - labelW) / 2, tabY, active ? 16 : 14,
                 active ? white : dimGray);
    }
    drawTextCentered("L/R switch tabs", tabY + 32, 11, {60, 60, 70, 255});

    auto& mm = ModManager::instance();
    int baseY = tabY + 52;
    int stepY = 52;
    int maxVisible = (SCREEN_H - baseY - 100) / stepY;
    if (maxVisible < 3) maxVisible = 3;

    if (modMenuTab_ == 0) {
        // ════ Mods tab ════
        const auto& mods = mm.mods();
        if (mods.empty()) {
            drawTextCentered("No mods installed", SCREEN_H / 2 - 10, 22, gray);
            drawTextCentered("Place mod folders in the mods/ directory", SCREEN_H / 2 + 20, 14, dimGray);
            drawTextCentered("", SCREEN_H / 2 + 40, 12, {60, 60, 70, 255});
        } else {
            int scrollOff = std::max(0, modMenuSelection_ - maxVisible + 1);
            for (int i = scrollOff; i < (int)mods.size() && (i - scrollOff) < maxVisible; i++) {
                auto& mod = mods[i];
                int y = baseY + (i - scrollOff) * stepY;
                bool sel = (i == modMenuSelection_);

                // Click detection
                bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, 60, y - 6, SCREEN_W - 120, stepY - 4);
                if (hovered && !usingGamepad_) { modMenuSelection_ = i; sel = true; }
                if (hovered) ui_.hoveredItem = i % 60;
                if (hovered && ui_.mouseClicked) {
                    modMenuSelection_ = i;
                    confirmInput_ = true;
                }

                // Card background
                if (sel) {
                    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 25);
                    SDL_Rect card = {60, y - 6, SCREEN_W - 120, stepY - 4};
                    SDL_RenderFillRect(renderer_, &card);
                    // Left accent
                    SDL_SetRenderDrawColor(renderer_, mod.enabled ? 50 : 255, mod.enabled ? 255 : 80, mod.enabled ? 100 : 80, 200);
                    SDL_Rect acc = {60, y - 6, 3, stepY - 4};
                    SDL_RenderFillRect(renderer_, &acc);
                } else if (i % 2 == 0) {
                    SDL_SetRenderDrawColor(renderer_, 14, 16, 26, 180);
                    SDL_Rect card = {60, y - 6, SCREEN_W - 120, stepY - 4};
                    SDL_RenderFillRect(renderer_, &card);
                }

                // Enable/disable badge
                SDL_Color badgeC = mod.enabled ? green : red;
                const char* badge = mod.enabled ? "\xe2\x97\x8f ON" : "\xe2\x97\x8b OFF";
                drawText(badge, 80, y, 14, badgeC);

                // Mod name + version
                char nameLine[256];
                snprintf(nameLine, sizeof(nameLine), "%s  v%s", mod.name.c_str(), mod.version.c_str());
                drawText(nameLine, 150, y, sel ? 18 : 16, sel ? white : gray);

                // Author
                char authorLine[128];
                snprintf(authorLine, sizeof(authorLine), "by %s", mod.author.c_str());
                drawText(authorLine, 150, y + 20, 12, dimGray);

                // Content badges (small tags)
                int tagX = SCREEN_W - 350;
                if (mod.content.characters) { drawText("chars", tagX, y + 2, 10, {200, 180, 50, 180}); tagX += 44; }
                if (mod.content.maps)       { drawText("maps", tagX, y + 2, 10, {80, 180, 255, 180}); tagX += 38; }
                if (mod.content.gamemodes)  { drawText("modes", tagX, y + 2, 10, {180, 100, 255, 180}); tagX += 44; }
                if (mod.content.sprites)    { drawText("sprites", tagX, y + 2, 10, {100, 200, 150, 180}); tagX += 52; }
                if (mod.content.sounds)     { drawText("sounds", tagX, y + 2, 10, {200, 150, 100, 180}); tagX += 48; }
                if (mod.content.items)      { drawText("items", tagX, y + 2, 10, {200, 200, 100, 180}); }

                // Description (selected only)
                if (sel && !mod.description.empty()) {
                    drawText(mod.description.c_str(), 150, y + 34, 11, {90, 90, 100, 255});
                }
            }

            // Scroll indicator
            if ((int)mods.size() > maxVisible) {
                float ratio = (float)maxVisible / mods.size();
                float scrollRatio = mods.size() > 1 ? (float)scrollOff / (mods.size() - maxVisible) : 0;
                int barH = std::max(20, (int)((SCREEN_H - baseY - 110) * ratio));
                int barY = baseY + (int)((SCREEN_H - baseY - 110 - barH) * scrollRatio);
                SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 60);
                SDL_Rect sb = {SCREEN_W - 48, barY, 4, barH};
                SDL_RenderFillRect(renderer_, &sb);
            }
        }

        // Bottom
        int backY = SCREEN_H - 70;
        bool backSel = (modMenuSelection_ >= (int)mods.size());
        if (ui_.menuItem(62, "BACK", SCREEN_W / 2, backY, 160, 28,
                         UI::Color::White, backSel, 18, 20)) {
            modMenuSelection_ = (int)mods.size();
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == 62 && !usingGamepad_) modMenuSelection_ = (int)mods.size();
        { UI::HintPair hints[] = { {UI::Action::Confirm, "Toggle"}, {UI::Action::Back, "Back"} };
          ui_.drawHintBar(hints, 2); }
    }
    else {
        // ════ Content tabs (Characters / Maps / Playlists) ════
        std::vector<std::string> paths;
        const char* emptyMsg = "";
        const char* emptyHint = "Enable mods with content in the MODS tab";
        if (modMenuTab_ == 1) {
            paths = mm.allCharacterPaths();
            emptyMsg = "No custom characters";
        } else if (modMenuTab_ == 2) {
            paths = mm.allMapPaths();
            emptyMsg = "No custom maps";
        } else if (modMenuTab_ == 3) {
            paths = mm.allPackPaths();
            emptyMsg = "No custom playlists";
        }

        if (paths.empty()) {
            drawTextCentered(emptyMsg, SCREEN_H / 2 - 10, 22, gray);
            drawTextCentered(emptyHint, SCREEN_H / 2 + 20, 14, dimGray);
        } else {
            int scrollOff = std::max(0, modMenuSelection_ - maxVisible + 1);
            for (int i = scrollOff; i < (int)paths.size() && (i - scrollOff) < maxVisible; i++) {
                int y = baseY + (i - scrollOff) * stepY;
                bool sel = (i == modMenuSelection_);

                // Click detection
                bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, 80, y - 4, SCREEN_W - 160, stepY - 8);
                if (hovered && !usingGamepad_) { modMenuSelection_ = i; sel = true; }
                if (hovered) ui_.hoveredItem = i % 60;
                if (hovered && ui_.mouseClicked) {
                    modMenuSelection_ = i;
                    confirmInput_ = true;
                }

                if (sel) {
                    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 25);
                    SDL_Rect card = {80, y - 4, SCREEN_W - 160, stepY - 8};
                    SDL_RenderFillRect(renderer_, &card);
                    SDL_SetRenderDrawColor(renderer_, tabAccents[modMenuTab_].r, tabAccents[modMenuTab_].g, tabAccents[modMenuTab_].b, 180);
                    SDL_Rect acc = {80, y - 4, 3, stepY - 8};
                    SDL_RenderFillRect(renderer_, &acc);
                }

                // Filename
                std::string name = paths[i];
                auto slash = name.rfind('/');
                if (slash == std::string::npos) slash = name.rfind('\\');
                if (slash != std::string::npos) name = name.substr(slash + 1);
                drawText(name.c_str(), 100, y, sel ? 18 : 16, sel ? yellow : gray);

                // Source path (dimmed)
                std::string srcDir = paths[i];
                auto slashDir = srcDir.rfind('/');
                if (slashDir != std::string::npos) srcDir = srcDir.substr(0, slashDir);
                drawText(srcDir.c_str(), 100, y + 22, 11, dimGray);
            }

            // Scroll indicator
            if ((int)paths.size() > maxVisible) {
                float ratio = (float)maxVisible / paths.size();
                float scrollRatio = paths.size() > 1 ? (float)scrollOff / (paths.size() - maxVisible) : 0;
                int barH = std::max(20, (int)((SCREEN_H - baseY - 110) * ratio));
                int barY = baseY + (int)((SCREEN_H - baseY - 110 - barH) * scrollRatio);
                SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 60);
                SDL_Rect sb = {SCREEN_W - 48, barY, 4, barH};
                SDL_RenderFillRect(renderer_, &sb);
            }
        }

        int backY = SCREEN_H - 70;
        bool backSel = (modMenuSelection_ >= (int)paths.size());
        if (ui_.menuItem(62, "BACK", SCREEN_W / 2, backY, 160, 28,
                         UI::Color::White, backSel, 18, 20)) {
            modMenuSelection_ = (int)paths.size();
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == 62 && !usingGamepad_) modMenuSelection_ = (int)paths.size();
        { UI::HintPair hints[] = { {UI::Action::Back, "Back"} };
          ui_.drawHintBar(hints, 1, SCREEN_H - 36); }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Discord Rich Presence
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateDiscordPresence() {
    auto& net = NetworkManager::instance();

    DiscordActivity act;
    act.largeImageKey  = "logo";
    act.largeImageText = "Cold Start";
    act.startTime      = 0; // filled below when in a session

    const bool inGame =
        state_ == GameState::Playing    || state_ == GameState::PlayingCustom ||
        state_ == GameState::PlayingPack || state_ == GameState::Dead          ||
        state_ == GameState::CustomDead || state_ == GameState::CustomWin      ||
        state_ == GameState::PackDead   || state_ == GameState::PackLevelWin   ||
        state_ == GameState::PackComplete;

    const bool inMP =
        state_ == GameState::MultiplayerGame    || state_ == GameState::MultiplayerPaused  ||
        state_ == GameState::MultiplayerDead    || state_ == GameState::MultiplayerSpectator;

    const bool isMPSplitscreen = inMP && coopPlayerCount_ > 1;

    if (state_ == GameState::MainMenu     || state_ == GameState::PlayModeMenu ||
        state_ == GameState::ConfigMenu   || state_ == GameState::CharSelect   ||
        state_ == GameState::CharCreator  || state_ == GameState::MapSelect    ||
        state_ == GameState::PackSelect   || state_ == GameState::MapConfig) {
        act.details = "Main Menu";
        act.state   = "Browsing menus";
    }
    else if (state_ == GameState::Editor      || state_ == GameState::EditorConfig ||
             state_ == GameState::CharCreator) {
        act.details = "Map Editor";
        act.state   = "Building a level";
    }
    else if (state_ == GameState::Lobby) {
        int total = 0;
        auto& players = net.players();
        for (auto& p : players) if (p.alive || true) total++;
        char buf[64];
        snprintf(buf, sizeof(buf), "%d player%s in lobby", total, total == 1 ? "" : "s");
        act.details = "Online Lobby";
        act.state   = buf;
    }
    else if (state_ == GameState::MultiplayerMenu || state_ == GameState::HostSetup ||
             state_ == GameState::JoinMenu) {
        act.details = "Multiplayer";
        act.state   = "Setting up game";
    }
    else if (inMP || isMPSplitscreen) {
        char det[64], st[64];
        if (isMPSplitscreen) {
            snprintf(det, sizeof(det), "Splitscreen · %d players", coopPlayerCount_);
        } else {
            int online = (int)net.players().size();
            snprintf(det, sizeof(det), "Online · %d player%s", online, online == 1 ? "" : "s");
        }
        if (lobbySettings_.isPvp) {
            int kills = 0;
            if (!coopSlots_[0].joined) kills = player_.killCounter;
            else kills = coopSlots_[0].kills;
            snprintf(st, sizeof(st), "PvP · %d kill%s", kills, kills == 1 ? "" : "s");
        } else {
            snprintf(st, sizeof(st), "Wave %d", waveNumber_);
        }
        act.details   = det;
        act.state     = st;
        act.startTime = discordSessionStart_;
    }
    else if (inGame) {
        char det[64], st[64];
        if (state_ == GameState::PlayingPack || state_ == GameState::PackDead  ||
            state_ == GameState::PackLevelWin || state_ == GameState::PackComplete) {
            snprintf(det, sizeof(det), "Campaign: %s", currentPack_.name.c_str());
        } else {
            snprintf(det, sizeof(det), "Solo Play");
        }
        snprintf(st, sizeof(st), "Wave %d - %d/%d HP",
                 waveNumber_, player_.hp, player_.maxHp);
        act.details   = det;
        act.state     = st;
        act.startTime = discordSessionStart_;
    }
    else {
        act.details = "Cold Start";
        act.state   = "";
    }

    DiscordRPC::instance().setActivity(act);
}
