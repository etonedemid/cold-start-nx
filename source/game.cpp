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

#ifdef HAS_CURL
#include <curl/curl.h>
#endif

#include "game_internal.h"

// Pitch-varied SFX
// SDL_AudioCVT resamples audio as if it were recorded at a different rate,
// producing a pitch shift. Each pitched copy lives until its channel finishes.
static Mix_Chunk* s_pitchPool[64] = {};

static void SDLCALL onPitchChannelDone(int ch) {
    if (ch >= 0 && ch < 64 && s_pitchPool[ch]) {
        SDL_free(s_pitchPool[ch]->abuf);
        SDL_free(s_pitchPool[ch]);
        s_pitchPool[ch] = nullptr;
    }
}

void initPitchSFX() {
    Mix_ChannelFinished(onPitchChannelDone);
}

void playSFX(Mix_Chunk* chunk, int volume) {
    if (!chunk) return;
    static const float kPitches[] = { 0.92f, 0.95f, 0.97f, 1.00f, 1.03f, 1.05f, 1.08f };
    float pitch = kPitches[rand() % 7];

    int freq; Uint16 fmt; int chans;
    Mix_QuerySpec(&freq, &fmt, &chans);
    int srcFreq = (int)((float)freq * pitch);

    Mix_Chunk* toPlay = chunk;
    if (srcFreq != freq) {
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, fmt, (Uint8)chans, srcFreq,
                                    fmt, (Uint8)chans, freq) > 0) {
            cvt.len = (int)chunk->alen;
            Uint8* buf = (Uint8*)SDL_malloc(cvt.len * cvt.len_mult);
            if (buf) {
                SDL_memcpy(buf, chunk->abuf, chunk->alen);
                cvt.buf = buf;
                if (SDL_ConvertAudio(&cvt) == 0) {
                    Mix_Chunk* p = (Mix_Chunk*)SDL_malloc(sizeof(Mix_Chunk));
                    if (p) {
                        p->allocated = 1;
                        p->abuf = buf;
                        p->alen = (Uint32)cvt.len_cvt;
                        p->volume = chunk->volume;
                        toPlay = p;
                    } else { SDL_free(buf); }
                } else { SDL_free(buf); }
            }
        }
    }

    int ch = Mix_PlayChannel(-1, toPlay, 0);
    if (ch >= 0) {
        Mix_Volume(ch, volume);
        if (toPlay != chunk) {
            if (ch < 64) {
                if (s_pitchPool[ch]) { SDL_free(s_pitchPool[ch]->abuf); SDL_free(s_pitchPool[ch]); }
                s_pitchPool[ch] = toPlay;
            }
        }
    } else if (toPlay != chunk) {
        SDL_free(toPlay->abuf);
        SDL_free(toPlay);
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

void Game::updateAspectMode() {
    // Pick logical resolution based on window shape:
    //   aspect <= 1.5  →  4:3 mode  (960×720, e.g. 4:3 monitor or square-ish window)
    //   aspect >  1.5  → 16:9 mode  (1280×720, letterboxed inside any wider window)
    // SDL_RenderSetLogicalSize handles letterboxing for anything in between.
    int winW = SCREEN_W, winH = SCREEN_H;
    if (window_) SDL_GetWindowSize(window_, &winW, &winH);
    float aspect = (winH > 0) ? (float)winW / (float)winH : 16.0f / 9.0f;
    int newW;
    if      (aspect <= 1.50f) newW = 960;   // 4:3
    else if (aspect <= 1.95f) newW = 1280;  // 16:9
    else if (aspect <= 2.15f) newW = 1560;  // 19.5:9
    else                      newW = 1600;  // 20:9
    int newH = 720;
    if (newW == SCREEN_W && newH == SCREEN_H) return;
    SCREEN_W = newW;
    SCREEN_H = newH;
    camera_.viewW = SCREEN_W;
    camera_.viewH = SCREEN_H;
    editor_.setScreenSize(SCREEN_W, SCREEN_H);
    if (renderer_) {
        SDL_RenderSetLogicalSize(renderer_, SCREEN_W, SCREEN_H);
        rebuildScreenTextures();
    }
}

void Game::applyResolutionSettings(bool rebuildTargets) {
    camera_.viewW = SCREEN_W;
    camera_.viewH = SCREEN_H;
    editor_.setScreenSize(SCREEN_W, SCREEN_H);

    if (renderer_) {
        SDL_RenderSetLogicalSize(renderer_, SCREEN_W, SCREEN_H);
        if (rebuildTargets) rebuildScreenTextures();
    }
}

// Init / Shutdown

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
#ifdef HAS_CURL
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return false;
    }
#ifdef PLATFORM_ANDROID
    Assets::androidInitRomfs();
#endif
    if (TTF_Init() < 0) {
        printf("TTF_Init: %s\n", TTF_GetError());
        return false;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        printf("Mix_OpenAudio: %s\n", Mix_GetError());
        // Non-fatal: continue without audio
    }
    Mix_AllocateChannels(32);
    initPitchSFX();

    loadConfig();
    loadBestRun();
    applyResolutionSettings(false);

    char windowTitle[64];
    snprintf(windowTitle, sizeof(windowTitle), "COLD START v%s", GAME_VERSION);

    window_ = SDL_CreateWindow(windowTitle,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
#if defined(__ANDROID__)
        SDL_WINDOW_FULLSCREEN_DESKTOP
#elif defined(__SWITCH__)
        0
#else
        SDL_WINDOW_RESIZABLE
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
    //     filtering is used for ALL textures including rotated ones.
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
    updateAspectMode(); // set initial 4:3 or 16:9 based on actual window/screen shape
    loadSavedServers();
    loadServerPresets();

    // Initialize map editor
    editor_.init(renderer_, SCREEN_W, SCREEN_H, &ui_);
    texEditor_.init(renderer_, SCREEN_W, SCREEN_H, &ui_);

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
            actionMusicActive_ = false;
            Mix_PlayMusic(customMapMusic_, -1);
            Mix_VolumeMusic(config_.musicVolume);
            return;
        }
        printf("Warning: could not load map music: %s\n", resolved.c_str());
    }
    playActionMusic();
}

void Game::playMenuMusic() {
    actionMusicActive_ = false;
    Mix_HaltMusic();
    if (menuMusic_) {
        Mix_PlayMusic(menuMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
    }
}

void Game::playActionMusic() {
    if (bgMusicTracks_.empty()) return;
    
    int n   = (int)bgMusicTracks_.size();
    int idx = lastActionTrack_;

    // If this is the very first time playing (track is -1)
    if (idx < 0) {
        idx = rand() % n; // Pick a random starting track
    } 
    else if (!musicLoopCurrent_) {
        // Normal sequential playback
        idx = (idx + 1) % n;
    }
    // If idx >= 0 AND musicLoopCurrent_ is true, idx stays the same so it loops.

    lastActionTrack_   = idx;
    actionMusicActive_ = true;
    Mix_PlayMusic(bgMusicTracks_[idx], 0);
    Mix_VolumeMusic(config_.musicVolume);
}

// SoftKeyboard - centralized on-screen keyboard for all text input
void Game::shutdown() {
    clearWorkshopIcons();
    clearDetailScreenshots();
    shutdownMultiplayer();
    editor_.shutdown();
    for (auto& cd : availableChars_) cd.unload();
    clearSyncedCharacters();
    ui_.shutdown();
    // Halt music BEFORE Assets::shutdown() frees the Mix_Music* pointers -
    // SDL_mixer reads freed memory if a track is still playing when freed.
    Mix_HaltMusic();
    if (customMapMusic_) { Mix_FreeMusic(customMapMusic_); customMapMusic_ = nullptr; }
    Assets::instance().shutdown();
    if (sceneTarget_) SDL_DestroyTexture(sceneTarget_);
    if (vignetteTex_) SDL_DestroyTexture(vignetteTex_);
    if (minimapCacheTex_) SDL_DestroyTexture(minimapCacheTex_);
    Mix_CloseAudio();
    TTF_Quit();
    DiscordRPC::instance().shutdown();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
#ifdef HAS_CURL
    curl_global_cleanup();
#endif
#ifdef __SWITCH__
    romfsExit();
#endif
}

// Controller Rumble

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
    // Always use native HID vibration on Switch - SDL_GameControllerRumble is
    // unreliable for Joy-Cons through the devkitpro SDL2 port.
    {
        HidVibrationValue value = makeSwitchVibrationValue(strength, lowBandScale, highBandScale);
        sendSwitchVibrationNow(value);
        switchRumbleStopTick_ = SDL_GetTicks() + (Uint32)std::max(16, durationMs);
        switchRumbleActive_ = true;
        return;
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
        if (volume > 0) playSFX(sfxExplosion_, volume);
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

// Update Checker

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

// Asset Loading

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

    enemyLegSprites_ = a.loadAnim("sprites/enemy/legs-", 8, 1);

    // Single sprites
    enemySprite_   = a.tex("sprites/enemy/melee.png");
    shooterSprite_ = a.tex("sprites/enemy/shooter.png");
    bruteSprite_   = a.tex("sprites/enemy/heavy.png");
    scoutSprite_   = a.tex("sprites/enemy/scout.png");
    sniperSprite_  = a.tex("sprites/enemy/sniper.png");
    gunnerSprite_  = a.tex("sprites/enemy/gunner.png");  // falls back to nullptr -> uses shooterSprite_
    bossBruteSprite_  = a.tex("sprites/enemy/boss_brute.png");
    bossSniperSprite_ = a.tex("sprites/enemy/boss_sniper.png");
    bossGunnerSprite_ = a.tex("sprites/enemy/boss_gunner.png");
    bulletSprite_  = a.tex("sprites/projectiles/bullet-player.png");
    // Red-tinted copy for enemy bullets - reuse same texture with color mod at draw time
    enemyBulletSprite_ = bulletSprite_;
    shieldSprite_  = a.tex("sprites/effects/shield.png");
    mainmenuBg_   = a.tex("sprites/ui/mainmenu.png");
    discordTex_   = a.tex("sprites/discord.png");
    ui_.desktopBg = a.tex("sprites/AVA desktop.png");
    bloodTex_     = a.tex("sprites/effects/blood.png");
    scorchTex_    = a.tex("sprites/effects/scorch.png");

    // Map tiles
    floorTex_  = a.tex("tiles/walls/floor.png");
    grassTex_  = a.tex("tiles/ground/grass.png");
    gravelTex_ = a.tex("tiles/ground/gravel.png");
    woodTex_   = a.tex("tiles/walls/wood.png");
    sandTex_   = a.tex("tiles/ground/sand.png");
    wallTex_   = a.tex("tiles/walls/floor.png");
    glassTex_  = a.tex("tiles/walls/glass.png");
    deskTex_   = a.tex("tiles/walls/desk.png");
    boxTex_    = a.tex("tiles/props/box.png");
    gravelGrass1Tex_ = a.tex("tiles/ground/gravel-grass1.png");
    gravelGrass2Tex_ = a.tex("tiles/ground/gravel-grass2.png");
    gravelGrass3Tex_ = a.tex("tiles/ground/gravel-grass3.png");
    glassTileTex_   = a.tex("tiles/ceiling/glasstile.png");

    // Sound effects
    sfxShoot_    = a.sfx("shootfx.wav");
    sfxEnemyShoot_ = a.sfx("laserShoot.wav");
    sfxReload_   = a.sfx("reload.mp3");
    sfxHurt_       = a.sfx("hurt.mp3");
    sfxPlayerHurt_ = a.sfx("universfield-punch-02-123106.mp3");
    sfxDeath_    = a.sfx("death.mp3");
    sfxExplosion_= a.sfx("explosion.mp3");
    sfxParry_    = a.sfx("parry.mp3");
    sfxSwoosh_   = a.sfx("swing.mp3");
    sfxBreak_    = a.sfx("break.mp3");
    sfxBeep_     = a.sfx("beep.mp3");
    sfxPress_        = a.sfx("press.mp3");
    sfxClick_        = a.sfx("universfield-computer-mouse-click-352734.mp3");
    sfxBoot_         = a.sfx("freesound_community-bootup-63385.mp3");
    sfxEnemyExplode_ = a.sfx("enemyexplode.mp3");
    vehicleCarSprite_ = a.loadRelTex("sprites/vehicles/car/test.png");
    bgMusicTracks_.clear();
    bgMusicTrackNames_.clear();
    bgMusicTrackAuthors_.clear();
    struct TI { const char* file; const char* name; const char* author; };
    static const TI kTracks[] = {
        {"action/cybergrind.mp3",                                    "CYBERGRIND",           ""},
        {"action/comastudio-action-techno-beat_eclipse-121310.mp3",  "ECLIPSE",              "Comastudio"},
        {"action/musicgururecords-vivid-160148.mp3",                 "VIVID",                "MusicGuruRecords"},
        {"action/skadix-dying-zeus-skadix-264325.mp3",               "DYING ZEUS",           "Skadix"},
        {"action/KyleDeadman_BurningSynapses.ogg",                   "BURNING SYNAPSES",     "Kyle Deadman"},
        {"action/KyleDeadmanXWintware_YouWouldBetterObey.ogg",       "YOU WOULD BETTER OBEY","Kyle Deadman x Wintware"},
    };
    for (auto& t : kTracks) {
        if (auto* m = a.music(t.file)) {
            bgMusicTracks_.push_back(m);
            bgMusicTrackNames_.push_back(t.name);
            bgMusicTrackAuthors_.push_back(t.author);
        }
    }
    menuMusic_   = a.music("FOTOSHOPPE CO. - Home Screen.mp3");
}

// Game State Management

void Game::startGame() {
    state_ = GameState::Playing;
    gameTime_ = 0;
    discordSessionStart_ = (int64_t)time(nullptr);
    lobbySettings_.isPvp = false;
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    bossWaveActive_ = false;
    lastBossWaveNum_ = -1;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;

    enemies_.clear();
    bullets_.clear();
    enemyBullets_.clear();
    bombs_.clear();
    explosions_.clear();
    debris_.clear();
    blood_.clear(); tileBlood_.clear();
    vehicles_.clear(); inVehicle_ = false; vehicleIdx_ = -1;
    boxFragments_.clear();
    crates_.clear();
    pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    sandboxMode_ = false;

    // Clear all custom-map visual/audio state so nothing leaks into normal games
    playingCustomMap_  = false;
    customGoalOpen_    = false;
    customEnemiesTotal_= 0;
    bgImageTex_    = nullptr;
    topImageTex_   = nullptr;
    topLayerAlpha_ = 1.0f;
    for (int _i = 0; _i < 8; _i++) customTileTextures_[_i] = nullptr;
    customMap_ = CustomMap{};

    map_.generate(config_.mapWidth, config_.mapHeight);
    map_.noCollide.clear();
    invalidateMinimapCache();

    player_ = Player{};
    player_.maxHp = config_.playerMaxHp;
    player_.hp = config_.playerMaxHp;
    player_.pos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
    player_.bombCount = 1;
    applyCharacterStatsToPlayer(player_);

    camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
    camera_.worldW = map_.worldWidth();
    camera_.worldH = map_.worldHeight();

    playActionMusic();
}

// Main Loop

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

        // Advance to next track whenever current one finishes, regardless of pause/dead state
        if (actionMusicActive_ && !Mix_PlayingMusic())
            playActionMusic();

        if (state_ == GameState::Playing || state_ == GameState::PlayingCustom
            || state_ == GameState::PlayingPack
            || state_ == GameState::MultiplayerGame
            || state_ == GameState::MultiplayerPaused
            || state_ == GameState::MultiplayerDead
            || state_ == GameState::MultiplayerSpectator
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
            // Check if editor wants back -> return to main menu and rescan maps
            if (editor_.wantsBack()) {
                editor_.clearWantsBack();
                editor_.setActive(false);
                scanMapFiles();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
            // Handle save dialog from editor (runs here, not in update(), because
            // update() is only called for gameplay states)
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
                // Load full-map layer images (same logic as startCustomMap)
                bgImageTex_  = customMap_.bgImagePath.empty()  ? nullptr : Assets::instance().loadRelTex(customMap_.bgImagePath);
                topImageTex_ = customMap_.topImagePath.empty() ? nullptr : Assets::instance().loadRelTex(customMap_.topImagePath);
                topLayerAlpha_ = 1.0f;
                // Editor playtest is always sandbox (no waves, no boss drops)
                sandboxMode_ = true;
                // Start playing it
                state_ = GameState::PlayingCustom;
                playingCustomMap_ = true;
                customGoalOpen_ = false;
                gameTime_ = 0;

                // Story: reset run + use the editor's in-memory cutscene library
                resetStoryRun();
                storyCutscenes_ = editor_.cutsceneLibrary();
                csPlay_.stop();

                map_.width  = customMap_.width;
                map_.height = customMap_.height;
                map_.tiles  = customMap_.tiles;
                map_.ceiling = customMap_.ceiling;
                map_.noCollide = customMap_.tileNoCollide;
                map_.noCollide.resize(map_.tiles.size(), 0);

                enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
                bombs_.clear(); explosions_.clear(); debris_.clear();
                blood_.clear(); tileBlood_.clear(); boxFragments_.clear();
                crates_.clear(); pickups_.clear();
                vehicles_.clear(); inVehicle_ = false; vehicleIdx_ = -1;
                upgrades_.reset();
                crateSpawnTimer_ = 0;
                waveNumber_ = 0; waveEnemiesLeft_ = 0; waveActive_ = false;
                bossWaveActive_ = false; lastBossWaveNum_ = -1;

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

                bystanders_.clear();
                customEnemiesTotal_ = 0;
                for (auto& es : customMap_.enemySpawns) {
                    if (isCrateSpawnType(es.enemyType)) {
                        // Spawn as a breakable crate
                        PickupCrate crate;
                        crate.pos = {es.x, es.y};
                        crate.contents = rollRandomUpgrade();
                        crates_.push_back(crate);
                    } else if (isResponderSpawn(es.enemyType)) {
                        spawnEnemy({es.x, es.y}, EnemyType::Shooter);
                        if (!enemies_.empty()) {
                            enemies_.back().isResponder = true;
                            enemies_.back().disableable = (es.reserved[0] != 0);
                        }
                        customEnemiesTotal_++;
                    } else if (isBystanderSpawn(es.enemyType)) {
                        spawnBystanderFromSpawn(es);
                    } else {
                        spawnEnemy({es.x, es.y}, enemyTypeFromSpawnId(es.enemyType));
                        customEnemiesTotal_++;
                    }
                }
                map_.findSpawnPoints();
                testPlayFromEditor_ = true;

                // Auto-play an intro cutscene if the library has one
                if (storyCutscenes_.findById("intro"))
                    startStoryCutscene("intro");
            }
        }

        else if (state_ == GameState::SpriteEditor) {
            texEditor_.update(dt_);
            if (texEditor_.wantsExit()) {
                texEditor_.setActive(false);
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }

        // Run for every state (update() above is gameplay-only): advance the
        // character-creator preview and apply a confirmed mod-save.
        updateCharCreatorPreview(dt_);
        processModSaveConfirm();

        if (!dedicatedMode_) {
            render();
        } else {
            SDL_Delay(5);
        }
    }
}

// Input

void Game::updateCharCreatorPreview(float dt) {
    if (state_ != GameState::CharCreator) return;
    auto& cc = charCreator_;
    if (!cc.playAnimation) return;
    cc.animTime += dt;
    const float frameTime = 0.1f;
    if (cc.animTime < frameTime) return;
    cc.animTime -= frameTime;
    cc.previewFrame++;
    int maxFrames = 0;
    switch (cc.previewSection) {
        case 1: maxFrames = cc.loaded ? (int)cc.charDef.bodySprites.size()  : (int)playerSprites_.size();      break;
        case 2: maxFrames = cc.loaded ? (int)cc.charDef.legSprites.size()   : (int)legSprites_.size();         break;
        case 3: maxFrames = cc.loaded ? (int)cc.charDef.deathSprites.size() : (int)playerDeathSprites_.size(); break;
        default: maxFrames = 1; break;
    }
    if (maxFrames > 0 && cc.previewFrame >= maxFrames) cc.previewFrame = 0;
}

void Game::processModSaveConfirm() {
    if (!modSaveDialog_.confirmed) return;
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
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == ' ') {
                        continue;
                    }
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

void Game::update() {
    float dt = dt_;
    gameTime_ += dt;

    // --- Story cutscenes (custom-map play only) ---
    // When a blocking cutscene is active, advance it and freeze the rest of
    // gameplay. Otherwise scan story triggers so zones can fire cutscenes.
    if (playingCustomMap_) {
        if (csPlay_.active) {
            // No shooting during any active dialog.
            if (csPlay_.dialog.active) fireInput_ = false;

            // Dialog input: confirm button or mouse click.
            if (csPlay_.dialog.active) {
                // Update hover for choices and handle clicks.
                if (csPlay_.dialog.lineComplete &&
                    csPlay_.dialog.seq &&
                    csPlay_.dialog.lineIdx < (int)csPlay_.dialog.seq->lines.size()) {
                    const auto& dline = csPlay_.dialog.seq->lines[csPlay_.dialog.lineIdx];
                    if (!dline.choices.empty()) {
                        // Choice box geometry (must match cutsceneRenderDialogBox constants).
                        const int PAD    = 14;
                        const int PORT_SZ= 128;
                        const int BAR_H  = PORT_SZ + 32;
                        const int CH_H   = 26;
                        const int CH_W   = std::min(420, SCREEN_W - 2 * PAD);
                        int n    = (int)dline.choices.size();
                        int barY = SCREEN_H - BAR_H;
                        int cx   = (SCREEN_W - CH_W) / 2;
                        int cy   = barY - n * (CH_H + 4) - 8;
                        int mx   = ui_.mouseX, my = ui_.mouseY;
                        csPlay_.dialog.hoveredChoice = -1;
                        for (int i = 0; i < n; i++) {
                            SDL_Rect cr = {cx, cy + i*(CH_H+4), CH_W, CH_H};
                            if (mx >= cr.x && mx < cr.x+cr.w && my >= cr.y && my < cr.y+cr.h)
                                csPlay_.dialog.hoveredChoice = i;
                        }
                        if ((ui_.mouseClicked || confirmInput_) && csPlay_.dialog.hoveredChoice >= 0) {
                            csPlay_.selectDialogChoice(csPlay_.dialog.hoveredChoice, storyCutscenes_);
                            ui_.mouseClicked = false;
                            confirmInput_ = false;
                        }
                    } else {
                        // No choices: any confirm or click advances.
                        if (confirmInput_ || ui_.mouseClicked) {
                            csPlay_.advanceDialog();
                            ui_.mouseClicked = false;
                            confirmInput_ = false;
                        }
                    }
                } else if (!csPlay_.dialog.lineComplete) {
                    // Typewriter still running: confirm/click skips to end.
                    if (confirmInput_ || ui_.mouseClicked) {
                        csPlay_.advanceDialog();
                        ui_.mouseClicked = false;
                        confirmInput_ = false;
                    }
                }
            }
            updateStoryCutscene(dt);
            if (csPlay_.active && csPlay_.cutscene && csPlay_.cutscene->blockInput) {
                // Ramp up control-loss over ~0.5s so it feels like losing control,
                // not a sudden freeze.
                csControlBlend_ = std::min(1.0f, csControlBlend_ + dt * 2.0f);
                fireInput_ = bombInput_ = parryInput_ = meleeInput_ = false;
                confirmInput_ = false;
                moveInput_ = moveInput_ * (1.0f - csControlBlend_);
                aimInput_  = aimInput_  * (1.0f - csControlBlend_);
                if (csControlBlend_ >= 1.0f) return;
            }
        } else {
            csControlBlend_ = 0.0f;
            updateStoryTriggers();  // may start a cutscene this frame
            if (csPlay_.active && csPlay_.cutscene && csPlay_.cutscene->blockInput)
                return;
        }
    }

    screenFlashTimer_  = std::max(0.0f, screenFlashTimer_ - dt);
    muzzleFlashTimer_  = std::max(0.0f, muzzleFlashTimer_ - dt);

    // Acid FX fade in/out
    {
        bool running = (acidFXFade_ > 0.001f || acidFXTimer_ > 0.0f);
        bool shouldFadeOut = (acidFXDuration_ > 0.0f && acidFXTimer_ >= acidFXDuration_);
        if (shouldFadeOut) {
            acidFXFade_ = std::max(0.0f, acidFXFade_ - dt * 2.0f);
            if (acidFXFade_ <= 0.0f) { acidFXTimer_ = 0.0f; acidFXDuration_ = 0.0f; }
        } else if (running) {
            acidFXTimer_ += dt;
            acidFXFade_ = std::min(1.0f, acidFXFade_ + dt * 2.0f);
        }
    }

    // Top-layer fade: detect if player is inside any LayerFade trigger
    if (topImageTex_) {
        bool inFadeZone = false;
        for (auto& t : customMap_.triggers) {
            if (t.type != TriggerType::LayerFade) continue;
            if (player_.pos.x >= t.x - t.width  * 0.5f && player_.pos.x <= t.x + t.width  * 0.5f &&
                player_.pos.y >= t.y - t.height * 0.5f && player_.pos.y <= t.y + t.height * 0.5f) {
                inFadeZone = true; break;
            }
        }
        float target = inFadeZone ? 0.25f : 1.0f;
        topLayerAlpha_ += (target - topLayerAlpha_) * std::min(1.0f, 6.0f * dt);
    }
    pickupPopupTimer_  = std::max(0.0f, pickupPopupTimer_ - dt);
    waveAnnounceTimer_ = std::max(0.0f, waveAnnounceTimer_ - dt);
    cratePopupTimer_   = std::max(0.0f, cratePopupTimer_ - dt);

    // Workshop status message timer
    workshopStatusTimer_ = std::max(0.0f, workshopStatusTimer_ - dt);
    if (workshopStatusTimer_ <= 0.0f) workshopStatus_ = "";

    // Low-HP red tint: ramps in below 40% HP, smoothly follows current HP
    {
        float hpRatio = (player_.maxHp > 0) ? (float)player_.hp / player_.maxHp : 1.0f;
        float target  = (hpRatio < 0.4f) ? (1.0f - hpRatio / 0.4f) : 0.0f;
        float speed   = (target > lowHpTint_) ? 2.0f : 4.0f; // fade in slower, fade out faster
        lowHpTint_ += (target - lowHpTint_) * std::min(1.0f, speed * dt);
    }

    // Only run gameplay logic in active playing states
    bool isPlayingState =
        state_ == GameState::Playing || state_ == GameState::Paused || state_ == GameState::Workshop || state_ == GameState::Dead ||
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
        updateBystanders(dt);
        updateBoxFragments(dt);
        updateBloodDecals(dt);
        updateVehicles(dt);
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

        // Camera - co-op/splitscreen cameras are updated inside updateLocalCoopPlayers
        if (!isCoopState && !isMPSplitscreen) {
            Vec2 aimDir = {0,0};
            if (aimInput_.lengthSq() > 0.04f) aimDir = aimInput_.normalized();
            else if (player_.moving && player_.vel.lengthSq() > 1.0f) aimDir = player_.vel.normalized();
            camera_.shakeScale = config_.shakeScale;
            const Uint8* kb = SDL_GetKeyboardState(nullptr);
            float lookScale = (kb[SDL_SCANCODE_LSHIFT] || kb[SDL_SCANCODE_RSHIFT]) ? 4.0f : 1.0f;
            Vec2 cameraTarget = player_.pos;
            if (inVehicle_ && vehicleIdx_ >= 0 && vehicleIdx_ < (int)vehicles_.size()) {
                auto& veh = vehicles_[vehicleIdx_];
                Vec2 vehFwd = {cosf(veh.rotation), sinf(veh.rotation)};
                float spdFrac = fminf(1.0f, veh.vel.length() / 300.0f);
                cameraTarget = player_.pos + vehFwd * (140.0f * spdFrac);
            }
            camera_.update(cameraTarget, aimDir, dt, lookScale);
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

    // NOTE: the character-creator preview animation and the mod-save
    // confirmation used to live here, but update() only runs for gameplay
    // states. They now run every frame from the main loop via
    // updateCharCreatorPreview() and processModSaveConfirm().
}

// Player Update

void Game::saveConfig() {
    FILE* f = fopen("config.txt", "w");
    if (!f) { printf("Failed to save config\n"); return; }
    fprintf(f, "mapWidth=%d\n", config_.mapWidth);
    fprintf(f, "mapHeight=%d\n", config_.mapHeight);
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
    fprintf(f, "enableUpnp=%d\n",           config_.enableUpnp           ? 1 : 0);
    fprintf(f, "acceptWorkshopMods=%d\n",  config_.acceptWorkshopMods   ? 1 : 0);
    fprintf(f, "acceptLocalMods=%d\n",     config_.acceptLocalMods      ? 1 : 0);
    fprintf(f, "uiScale=%.2f\n", config_.uiScale);
    fprintf(f, "shakeScale=%.2f\n", config_.shakeScale);
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
        else if (sscanf(line, "enableUpnp=%d",          &ival) == 1) config_.enableUpnp          = (ival != 0);
        else if (sscanf(line, "acceptWorkshopMods=%d",  &ival) == 1) config_.acceptWorkshopMods  = (ival != 0);
        else if (sscanf(line, "acceptLocalMods=%d",     &ival) == 1) config_.acceptLocalMods     = (ival != 0);
        // Legacy key migration
        else if (sscanf(line, "acceptMods=%d",          &ival) == 1) config_.acceptWorkshopMods  = (ival != 0);
        else if (sscanf(line, "uiScale=%f",   &fval) == 1) config_.uiScale   = std::clamp(fval, 0.5f, 2.0f);
        else if (sscanf(line, "shakeScale=%f", &fval) == 1) config_.shakeScale = std::clamp(fval, 0.0f, 1.0f);
    }
    fclose(f);
    printf("Config loaded from config.txt\n");
}

// Best-run persistence

void Game::saveBestRun() {
    FILE* f = fopen("best_run.txt", "w");
    if (!f) return;
    fprintf(f, "wave=%d\n",  bestRun_.wave);
    fprintf(f, "kills=%d\n", bestRun_.kills);
    fprintf(f, "time=%.2f\n", bestRun_.time);
    fclose(f);
}

void Game::loadBestRun() {
    FILE* f = fopen("best_run.txt", "r");
    if (!f) return;
    char line[128];
    int ival; float fval;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "wave=%d",  &ival) == 1) bestRun_.wave  = ival;
        if (sscanf(line, "kills=%d", &ival) == 1) bestRun_.kills = ival;
        if (sscanf(line, "time=%f",  &fval) == 1) bestRun_.time  = fval;
    }
    fclose(f);
}

void Game::checkAndSaveBestRun() {
    // Only count solo/survival runs (not multiplayer or custom maps)
    if (state_ != GameState::Playing && state_ != GameState::Paused &&
        state_ != GameState::Workshop) return;
    int  kills = coopSlots_[0].kills;
    BestRun current{ waveNumber_, kills, gameTime_ };
    newBestRun_ = false;
    bool better = !bestRun_.valid()
        || current.wave  > bestRun_.wave
        || (current.wave == bestRun_.wave && current.kills  > bestRun_.kills)
        || (current.wave == bestRun_.wave && current.kills == bestRun_.kills && current.time > bestRun_.time);
    if (better) {
        bestRun_    = current;
        newBestRun_ = true;
        saveBestRun();
    }
}

// Dev console

// --- Story / SIGNAL ---

void Game::adjustSignal(int delta, const char* reason) {
    if (delta == 0) return;
    int before = signal_;
    signal_ += delta;
    if (signal_ < 0)   signal_ = 0;
    if (signal_ > 100) signal_ = 100;
    if (signal_ != before) {
        char msg[128];
        snprintf(msg, sizeof(msg), "SIGNAL %+d -> %d  (%s)",
                 signal_ - before, signal_, reason ? reason : "");
        consoleOut(msg);
    }
}

void Game::resetStoryRun() {
    signal_      = 50;
    storyRoute_  = 0;
    storyFlags_.clear();
    packVars_.clear();
    csFiredZones_.clear();
    storyFiredTriggers_.clear();
    storyRunActive_ = true;
}

void Game::loadStoryCutsceneLib(const std::string& csmPath) {
    storyCutscenes_.clear();
    localVars_.clear();
    csFiredZones_.clear();
    storyFiredTriggers_.clear();
    triggerPlayerInside_.clear();
    triggerMultiCooldowns_.clear();
    csPlay_.stop();
    // Derive the sibling .csc path (same basename, .csc extension).
    std::string p = csmPath;
    size_t dot = p.rfind('.');
    if (dot != std::string::npos && p.find('/', dot) == std::string::npos &&
        p.find('\\', dot) == std::string::npos)
        p = p.substr(0, dot);
    p += ".csc";
    storyCutscenes_.load(p);   // silently leaves library empty if absent
}

void Game::startStoryCutscene(const std::string& id) {
    const Cutscene* cs = storyCutscenes_.findById(id);
    if (!cs) return;
    csPlay_.extSignal = signal_;
    csPlay_.extRoute  = storyRoute_;
    csPlay_.extVarGet = [this](const std::string& name) -> int {
        auto it = localVars_.find(name);
        if (it != localVars_.end()) return it->second;
        auto it2 = packVars_.find(name);
        if (it2 != packVars_.end()) return it2->second;
        return 0;
    };
    csPlay_.start(cs, renderer_);
    csControlBlend_ = 0.0f;
}

void Game::updateStoryCutscene(float dt) {
    if (!csPlay_.active) return;
    csPlay_.extSignal = signal_;
    csPlay_.extRoute  = storyRoute_;
    csPlay_.update(dt, storyCutscenes_);

    // Camera: CameraMove events override; otherwise follow the Player actor.
    if (csPlay_.active && csPlay_.camDriven) {
        camera_.pos.x = csPlay_.cam.x - SCREEN_W * 0.5f;
        camera_.pos.y = csPlay_.cam.y - SCREEN_H * 0.5f;
    } else if (csPlay_.active && csPlay_.cutscene) {
        for (int i = 0; i < (int)csPlay_.cutscene->actors.size() &&
                        i < (int)csPlay_.actorStates.size(); i++) {
            if (csPlay_.cutscene->actors[i].type == CsActorType::Player &&
                csPlay_.actorStates[i].visible) {
                Vec2 target = {csPlay_.actorStates[i].x, csPlay_.actorStates[i].y};
                camera_.update(target, {0, 0}, dt);
                break;
            }
        }
    }

    // Drain one-shot spawn requests.
    for (auto& pe : csPlay_.pendingExplosions)
        spawnExplosion({pe.x, pe.y});
    csPlay_.pendingExplosions.clear();

    for (auto& en : csPlay_.pendingEnemies) {
        int tid = (int)en.typeId;
        if (tid < 0) tid = 0;
        if (tid > (int)EnemyType::Gunner) tid = (int)EnemyType::Gunner;
        spawnEnemy({en.x, en.y}, static_cast<EnemyType>(tid));
    }
    csPlay_.pendingEnemies.clear();

    for (auto& pk : csPlay_.pendingPickups) {
        Pickup p;
        p.pos  = {pk.x, pk.y};
        int tid = (int)pk.typeId;
        if (tid < 0) tid = 0;
        if (tid >= (int)UpgradeType::COUNT) tid = 0;
        p.type = static_cast<UpgradeType>(tid);
        pickups_.push_back(p);
    }
    csPlay_.pendingPickups.clear();

    // Drain SIGNAL deltas raised by AdjustSignal events.
    if (csPlay_.pendingSignalDelta != 0) {
        adjustSignal(csPlay_.pendingSignalDelta, "cutscene");
        csPlay_.pendingSignalDelta = 0;
    }
    // Drain variable set actions.
    for (const auto& vs : csPlay_.pendingVarSets) {
        auto& map = (vs.scope == 1) ? packVars_ : localVars_;
        switch (vs.op) {
            case 1:  map[vs.key] += vs.value; break;
            case 2:  map[vs.key] -= vs.value; break;
            default: map[vs.key]  = vs.value; break;
        }
    }
    csPlay_.pendingVarSets.clear();

    // Acid FX requests from PostFXAcid events.
    for (const auto& af : csPlay_.pendingAcidFX) {
        if (af.enable) {
            acidColor1_ = {(Uint8)af.c1r, (Uint8)af.c1g, (Uint8)af.c1b, 255};
            acidColor2_ = {(Uint8)af.c2r, (Uint8)af.c2g, (Uint8)af.c2b, 255};
            acidFXDuration_ = af.duration;
            if (acidFXFade_ <= 0.001f) acidFXTimer_ = 0.0f;
            acidFXFade_ = std::max(acidFXFade_, 0.001f);  // trigger fade-in
        } else {
            // Disable: start fade out by setting duration to a tiny value that is already elapsed
            if (acidFXFade_ > 0.001f) acidFXDuration_ = acidFXTimer_ + 0.001f;
        }
    }
    csPlay_.pendingAcidFX.clear();

    // Console command requests from ConsoleCmd events.
    for (const auto& rawCmd : csPlay_.pendingConsoleCmds) {
        // Expand {varname} tokens
        std::string cmd;
        cmd.reserve(rawCmd.size());
        for (size_t i = 0; i < rawCmd.size(); ) {
            if (rawCmd[i] == '{') {
                size_t end = rawCmd.find('}', i + 1);
                if (end != std::string::npos) {
                    std::string vn = rawCmd.substr(i + 1, end - i - 1);
                    auto it = localVars_.find(vn);
                    int vval = 0;
                    if (it != localVars_.end()) vval = it->second;
                    else {
                        auto it2 = packVars_.find(vn);
                        if (it2 != packVars_.end()) vval = it2->second;
                    }
                    char tmp[32]; snprintf(tmp, sizeof(tmp), "%d", vval);
                    cmd += tmp;
                    i = end + 1;
                    continue;
                }
            }
            cmd += rawCmd[i++];
        }
        consoleExec(cmd.c_str());
    }
    csPlay_.pendingConsoleCmds.clear();

    // Map load request from LoadMap event.
    if (!csPlay_.pendingLoadMap.empty()) {
        std::string path = csPlay_.pendingLoadMap;
        csPlay_.stop();
        startCustomMap(path);
        return;
    }
    // Death screen request from ondeath cutscene.
    if (csPlay_.pendingDeathScreen) {
        csPlay_.pendingDeathScreen = false;
        csPlay_.stop();
        if (state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused ||
            state_ == GameState::PlayingPack    || state_ == GameState::PackPaused)
            state_ = GameState::CustomDead;
        return;
    }
    // Copy any script flags out into campaign-spanning state.
    for (auto& kv : csPlay_.scriptFlags) {
        storyFlags_[kv.first] = kv.second;
        // Convention: flags named "route_a"/"route_b" commit the route split.
        if (kv.second && kv.first == "route_a") storyRoute_ = 1;
        if (kv.second && kv.first == "route_b") storyRoute_ = 2;
    }

    // Push Player actor state onto the real player so the normal render path
    // handles position, rotation, animation, and visual overrides.
    if (csPlay_.active && csPlay_.cutscene) {
        bool foundPlayer = false;
        for (int i = 0; i < (int)csPlay_.cutscene->actors.size() &&
                        i < (int)csPlay_.actorStates.size(); i++) {
            if (csPlay_.cutscene->actors[i].type != CsActorType::Player) continue;
            const auto& s   = csPlay_.actorStates[i];
            const float D2R = 3.14159265f / 180.0f;
            player_.pos          = {s.x, s.y};
            player_.rotation     = s.rot * D2R - (float)M_PI / 2;
            player_.animFrame    = s.frame;
            player_.legAnimFrame = s.legAnimFrame;
            player_.legRotation  = s.legRotation;
            player_.csScale      = s.scaleX;
            player_.csAlpha      = s.alpha;
            player_.csVisible    = s.visible;
            player_.csFlashAmt   = s.flashAmt;
            player_.csFlashR     = s.flashR;
            player_.csFlashG     = s.flashG;
            player_.csFlashB     = s.flashB;
            foundPlayer = true;
            break;
        }
        if (!foundPlayer) {
            player_.csScale = 1.0f; player_.csAlpha = 1.0f;
            player_.csVisible = true; player_.csFlashAmt = 0.0f;
        }
    }

    // Handle chain / end requests (start() resets pending fields, so read first).
    if (!csPlay_.pendingChainId.empty()) {
        std::string next = csPlay_.pendingChainId;
        startStoryCutscene(next);
    } else if (csPlay_.pendingEnd || csPlay_.isDone()) {
        bool wasOnDeath = csPlay_.cutscene &&
                          csPlay_.cutscene->id == storyCutscenes_.onDeathId;
        csPlay_.stop();
        // Reset cutscene overrides on the player when the cutscene ends.
        player_.csScale = 1.0f; player_.csAlpha = 1.0f;
        player_.csVisible = true; player_.csFlashAmt = 0.0f;
        // If this was the ondeath cutscene and no DeathScreen event fired, show death screen now.
        if (wasOnDeath) {
            if (state_ == GameState::PlayingPack || state_ == GameState::PackPaused)
                state_ = GameState::PackDead;
            else
                state_ = GameState::CustomDead;
        }
    }
}

// --- Story bystanders (civilians / infrastructure / surrendered operators) ---

void Game::spawnBystanderFromSpawn(const EnemySpawn& es) {
    Bystander b;
    b.pos = {es.x, es.y};
    switch (es.enemyType) {
        case ENTITY_CIVILIAN:        b.kind = Bystander::Kind::Civilian; b.hp = b.maxHp = 24;  b.radius = 20; break;
        case ENTITY_INFRA_MEDRELAY:  b.kind = Bystander::Kind::MedRelay; b.hp = b.maxHp = 140; b.radius = 32; break;
        case ENTITY_INFRA_POWER:     b.kind = Bystander::Kind::Power;    b.hp = b.maxHp = 140; b.radius = 32; break;
        case ENTITY_INFRA_WATER:     b.kind = Bystander::Kind::Water;    b.hp = b.maxHp = 120; b.radius = 30; break;
        case ENTITY_INFRA_ANTENNA:   b.kind = Bystander::Kind::Antenna;  b.hp = b.maxHp = 100; b.radius = 26; break;
        default: return;
    }
    b.wanderDir = Vec2::fromAngle((float)(rand() % 628) * 0.01f);
    bystanders_.push_back(b);
}

void Game::onBystanderDestroyed(Bystander& b) {
    if (!b.alive) return;
    b.alive = false;
    int penalty = 0; const char* why = "";
    switch (b.kind) {
        case Bystander::Kind::Civilian: penalty = -15; why = "civilian harmed";      break;
        case Bystander::Kind::Operator: penalty = -20; why = "surrendered killed";   break;
        case Bystander::Kind::MedRelay: penalty = -14; why = "med-relay destroyed";  break;
        case Bystander::Kind::Power:    penalty = -12; why = "power destroyed";       break;
        case Bystander::Kind::Water:    penalty = -12; why = "water destroyed";       break;
        case Bystander::Kind::Antenna:  penalty =  -8; why = "antenna destroyed";     break;
    }
    if (!b.scored) { adjustSignal(penalty, why); b.scored = true; }
    // Non-damaging FX: blood for people, scorch decal + debris for structures.
    if (b.kind == Bystander::Kind::Civilian || b.kind == Bystander::Kind::Operator) {
        BloodDecal bd; bd.pos = b.pos; bd.type = DecalType::Blood;
        bd.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
        bd.scale = 1.0f + (float)(rand() % 30) / 100.0f;
        blood_.push_back(bd);
        stampTileBlood(b.pos, 0.8f);
    } else {
        BloodDecal sd; sd.pos = b.pos; sd.type = DecalType::Scorch;
        sd.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
        sd.scale = 1.4f;
        blood_.push_back(sd);
    }
    for (int i = 0; i < 16; i++) {
        BoxFragment f; f.pos = b.pos;
        float a = (float)(rand() % 360) * (float)M_PI / 180.0f;
        float spd = 120.0f + (float)(rand() % 220);
        f.vel = {cosf(a) * spd, sinf(a) * spd};
        f.size = 2.5f + (float)(rand() % 5);
        f.lifetime = 0.4f + (float)(rand() % 40) / 100.0f;
        f.rotation = (float)(rand() % 360);
        f.rotSpeed = (float)(rand() % 600 - 300);
        f.alive = true;
        bool people = (b.kind == Bystander::Kind::Civilian || b.kind == Bystander::Kind::Operator);
        f.color = people ? SDL_Color{(Uint8)(120 + rand() % 100), 0, 0, 255}
                         : SDL_Color{140, 140, 150, 255};
        boxFragments_.push_back(f);
    }
}

bool Game::damageBystandersAt(Vec2 pos, float radius, float amount) {
    bool hit = false;
    for (auto& b : bystanders_) {
        if (!b.alive) continue;
        float reach = radius + b.radius;
        if ((b.pos - pos).lengthSq() > reach * reach) continue;
        hit = true;
        if (b.kind == Bystander::Kind::Civilian) b.fleeing = true;
        if (amount < 0) { b.hp = 0; }
        else            { b.hp -= amount; }
        if (b.hp <= 0) onBystanderDestroyed(b);
    }
    return hit;
}

void Game::updateBystanders(float dt) {
    if (!playingCustomMap_) return;

    // Responder enemies: disable below a non-lethal threshold (if authored so),
    // converting them into a surrendered operator and crediting the player.
    for (auto& e : enemies_) {
        if (!e.alive || !e.isResponder) continue;
        if (e.disableable && e.hp > 0 && e.hp < e.maxHp * 0.30f) {
            e.alive = false;
            Bystander op; op.kind = Bystander::Kind::Operator;
            op.pos = e.pos; op.hp = op.maxHp = 16; op.radius = 18;
            bystanders_.push_back(op);
            adjustSignal(+10, "responder disabled");
        }
    }

    const float CIV_SPEED  = 150.0f;
    const float FLEE_RANGE  = 260.0f;
    for (auto& b : bystanders_) {
        if (!b.alive) continue;

        // Player-bullet collisions (player bullets only live in bullets_)
        for (auto& bl : bullets_) {
            if (!bl.alive) continue;
            float reach = b.radius + bl.size;
            if ((bl.pos - b.pos).lengthSq() > reach * reach) continue;
            bl.alive = false;
            if (b.kind == Bystander::Kind::Civilian) b.fleeing = true;
            b.hp -= (float)bl.damage;
            if (b.hp <= 0) { onBystanderDestroyed(b); break; }
        }
        if (!b.alive) continue;

        // Movement (civilians only; infrastructure + operators are static)
        if (b.kind == Bystander::Kind::Civilian) {
            Vec2 toPlayer = player_.pos - b.pos;
            float pd = toPlayer.length();
            if (pd < FLEE_RANGE) b.fleeing = true;
            else if (pd > FLEE_RANGE * 1.6f) b.fleeing = false;

            Vec2 dir;
            if (b.fleeing && pd > 1.0f) {
                dir = toPlayer * (-1.0f / pd);   // away from player
            } else {
                b.wanderT -= dt;
                if (b.wanderT <= 0) {
                    b.wanderT = 1.0f + (float)(rand() % 200) * 0.01f;
                    b.wanderDir = Vec2::fromAngle((float)(rand() % 628) * 0.01f);
                }
                dir = b.wanderDir * 0.4f;
            }
            float spd = b.fleeing ? CIV_SPEED : CIV_SPEED * 0.4f;
            Vec2 next = b.pos + dir * (spd * dt);
            if (!wallCollision(next, b.radius * 0.5f)) b.pos = next;
            // keep inside world bounds
            b.pos.x = std::max(16.0f, std::min(map_.worldWidth()  - 16.0f, b.pos.x));
            b.pos.y = std::max(16.0f, std::min(map_.worldHeight() - 16.0f, b.pos.y));
        }
    }

    // Reap dead bystanders occasionally to keep the list bounded
    bystanders_.erase(std::remove_if(bystanders_.begin(), bystanders_.end(),
        [](const Bystander& b){ return !b.alive; }), bystanders_.end());
}

bool Game::playerInTriggerRect(const MapTrigger& t) const {
    return player_.pos.x >= t.x - t.width  * 0.5f && player_.pos.x <= t.x + t.width  * 0.5f &&
           player_.pos.y >= t.y - t.height * 0.5f && player_.pos.y <= t.y + t.height * 0.5f;
}

void Game::updateStoryTriggers() {
    auto getVar = [&](const std::string& key) -> int {
        auto it = localVars_.find(key); if (it != localVars_.end()) return it->second;
        auto it2 = packVars_.find(key); if (it2 != packVars_.end()) return it2->second;
        return 0;
    };
    auto condMet = [&](int idx) -> bool {
        for (const auto& tc : storyCutscenes_.triggerConditions) {
            if (tc.triggerIndex != idx || tc.varName.empty()) continue;
            int lhs = getVar(tc.varName);
            bool ok;
            switch (tc.cmp) {
                case 1:  ok = lhs != tc.value; break;
                case 2:  ok = lhs >  tc.value; break;
                case 3:  ok = lhs <  tc.value; break;
                case 4:  ok = lhs >= tc.value; break;
                case 5:  ok = lhs <= tc.value; break;
                default: ok = lhs == tc.value; break;
            }
            if (!ok) return false;
        }
        return true;
    };

    // Find multi-config for a trigger index (nullptr if not multi)
    auto findMulti = [&](int idx) -> const TriggerMultiConfig* {
        for (const auto& mc : storyCutscenes_.triggerMultiConfigs)
            if (mc.triggerIndex == idx) return &mc;
        return nullptr;
    };

    // Tick cooldowns
    for (auto& kv : triggerMultiCooldowns_)
        kv.second -= dt_;

    // Build current inside set; detect leave events to start cooldown countdown
    std::set<int> nowInside;
    for (int i = 0; i < (int)customMap_.triggers.size(); i++)
        if (playerInTriggerRect(customMap_.triggers[i]))
            nowInside.insert(i);

    // Triggers player just left: start/reset their cooldown timer
    for (int i : triggerPlayerInside_) {
        if (!nowInside.count(i)) {
            const TriggerMultiConfig* mc = findMulti(i);
            if (mc) triggerMultiCooldowns_[i] = mc->cooldown;
        }
    }

    // Triggers player just entered this frame
    std::set<int> justEntered;
    for (int i : nowInside)
        if (!triggerPlayerInside_.count(i))
            justEntered.insert(i);

    triggerPlayerInside_ = nowInside;

    for (int i = 0; i < (int)customMap_.triggers.size(); i++) {
        const MapTrigger& t = customMap_.triggers[i];
        const TriggerMultiConfig* mc = findMulti(i);

        bool inside = nowInside.count(i) > 0;
        bool entered = justEntered.count(i) > 0;

        // For multi triggers: fire on just-entered and cooldown expired (or no prior fire)
        // For one-shot triggers: fire once while inside (original behavior)
        auto canFire = [&](std::set<int>& firedSet) -> bool {
            if (mc) {
                // multi: must just enter AND not in active cooldown
                if (!entered) return false;
                auto cd = triggerMultiCooldowns_.find(i);
                if (cd != triggerMultiCooldowns_.end() && cd->second > 0.0f) return false;
                if (!condMet(i)) return false;
                return true;
            } else {
                if (firedSet.count(i) || !inside || !condMet(i)) return false;
                firedSet.insert(i);
                return true;
            }
        };

        switch (t.type) {
            case TriggerType::Cutscene: {
                if (mc) {
                    if (!entered) break;
                    auto cd = triggerMultiCooldowns_.find(i);
                    if (cd != triggerMultiCooldowns_.end() && cd->second > 0.0f) break;
                    if (!condMet(i)) break;
                    // Reset cooldown to -inf so it doesn't re-arm until player leaves
                    triggerMultiCooldowns_[i] = -1e9f;
                } else {
                    if (csFiredZones_.count(i) || !inside || !condMet(i)) break;
                    csFiredZones_.insert(i);
                }
                int idx = t.param;
                if (idx >= 0 && idx < (int)storyCutscenes_.cutscenes.size())
                    startStoryCutscene(storyCutscenes_.cutscenes[idx].id);
                return;  // a cutscene started; stop scanning this frame
            }
            case TriggerType::SetVariable: {
                if (!canFire(storyFiredTriggers_)) break;
                if (mc) triggerMultiCooldowns_[i] = -1e9f;
                for (const auto& va : storyCutscenes_.triggerVarActions) {
                    if (va.triggerIndex != i || va.key.empty()) continue;
                    auto& m = (va.scope == 1) ? packVars_ : localVars_;
                    switch (va.op) {
                        case 1:  m[va.key] += va.value; break;
                        case 2:  m[va.key] -= va.value; break;
                        default: m[va.key]  = va.value; break;
                    }
                }
                break;
            }
            case TriggerType::LoadMap: {
                if (!canFire(storyFiredTriggers_)) break;
                if (mc) triggerMultiCooldowns_[i] = -1e9f;
                for (const auto& tm : storyCutscenes_.triggerMapLoads) {
                    if (tm.triggerIndex == i && !tm.mapPath.empty()) {
                        startCustomMap(tm.mapPath);
                        return;
                    }
                }
                break;
            }
            case TriggerType::Waypoint: {
                if (!canFire(storyFiredTriggers_)) break;
                if (mc) triggerMultiCooldowns_[i] = -1e9f;
                if (t.param == 2) { storyRoute_ = 2; storyFlags_["route_b"] = true; }
                else              { storyRoute_ = 1; storyFlags_["route_a"] = true; }
                consoleOut(storyRoute_ == 2 ? "Route committed: SIGNAL (B)"
                                            : "Route committed: SPEARHEAD (A)");
                break;
            }
            case TriggerType::SignalZone: {
                if (!canFire(storyFiredTriggers_)) break;
                if (mc) triggerMultiCooldowns_[i] = -1e9f;
                adjustSignal((int)(int8_t)t.param, "signal zone");
                break;
            }
            case TriggerType::Objective: {
                if (storyFiredTriggers_.count(i) || !inside) break;
                if ((ObjectiveKind)t.param == ObjectiveKind::Recover) {
                    storyFiredTriggers_.insert(i);
                    storyFlags_["goal_open"] = true;
                    char fn[32]; snprintf(fn, sizeof(fn), "obj_%d", i);
                    storyFlags_[fn] = true;
                    adjustSignal(+8, "objective recovered");
                }
                break;
            }
            default: break;
        }
    }
}

void Game::consoleOut(const char* line) {
    consoleLog_.push_back(line);
    if (consoleLog_.size() > 100) consoleLog_.erase(consoleLog_.begin());
    printf("[console] %s\n", line);
}

void Game::consoleExec(const char* cmd) {
    consoleOut(cmd);   // echo command
    char tok[64] = {}, rest[192] = {};
    sscanf(cmd, "%63s %191[^\n]", tok, rest);

    // All spawn commands use current mouse world position
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    Vec2 mouseWorldPos = camera_.screenToWorld({(float)mx, (float)my});

    if (strcmp(tok, "wave") == 0) {
        int n = 0;
        if (sscanf(rest, "%d", &n) == 1 && n >= 1) {
            waveNumber_ = n - 1;  // incremented at start of next wave
            wavePauseTimer_ = 0;
            waveActive_ = false;
            bossWaveActive_ = false;
            lastBossWaveNum_ = -1;
            char msg[64]; snprintf(msg, sizeof(msg), "Wave set to %d (starts next)", n);
            consoleOut(msg);
        } else { consoleOut("usage: wave <number>"); }
    } else if (strcmp(tok, "hp") == 0) {
        int n = 0;
        if (sscanf(rest, "%d", &n) == 1 && n >= 1) {
            player_.hp = std::min(n, player_.maxHp);
            char msg[64]; snprintf(msg, sizeof(msg), "HP set to %d", player_.hp);
            consoleOut(msg);
        } else { consoleOut("usage: hp <number>"); }
    } else if (strcmp(tok, "god") == 0) {
        godMode_ = !godMode_;
        consoleOut(godMode_ ? "God mode ON" : "God mode OFF");
    } else if (strcmp(tok, "clear") == 0) {
        for (auto& e : enemies_) e.alive = false;
        consoleOut("All enemies killed");
    } else if (strcmp(tok, "spawn") == 0) {
        char typeName[64] = {};
        int count = 1;
        sscanf(rest, "%63s %d", typeName, &count);
        EnemyType etype = EnemyType::Melee;
        bool known = true;
        if      (strcmp(typeName, "melee")      == 0) etype = EnemyType::Melee;
        else if (strcmp(typeName, "shooter")    == 0) etype = EnemyType::Shooter;
        else if (strcmp(typeName, "brute")      == 0) etype = EnemyType::Brute;
        else if (strcmp(typeName, "scout")      == 0) etype = EnemyType::Scout;
        else if (strcmp(typeName, "sniper")     == 0) etype = EnemyType::Sniper;
        else if (strcmp(typeName, "gunner")     == 0) etype = EnemyType::Gunner;
        else if (strcmp(typeName, "boss_brute") == 0) etype = EnemyType::BossBrute;
        else if (strcmp(typeName, "boss_sniper")== 0) etype = EnemyType::BossSniper;
        else if (strcmp(typeName, "boss_gunner")== 0) etype = EnemyType::BossGunner;
        else { consoleOut("unknown type - melee/shooter/brute/scout/sniper/gunner/boss_brute/boss_sniper/boss_gunner"); known = false; }
        if (known) {
            count = std::max(1, std::min(count, 20));
            for (int i = 0; i < count; i++) spawnEnemy(mouseWorldPos, etype);
            char msg[64]; snprintf(msg, sizeof(msg), "Spawned %d %s", count, typeName);
            consoleOut(msg);
        }
    } else if (strcmp(tok, "spawn_box") == 0) {
        PickupCrate box;
        box.pos = mouseWorldPos;
        box.hasContents = false;
        crates_.push_back(box);
        consoleOut("Spawned destructible box at mouse");
    } else if (strcmp(tok, "vehicle") == 0) {
        if (strcmp(rest, "car") == 0) {
            Vehicle v;
            v.pos    = mouseWorldPos;
            v.type   = VehicleType::Car;
            v.sprite = vehicleCarSprite_;
            if (v.sprite) SDL_QueryTexture(v.sprite, nullptr, nullptr, &v.spriteW, &v.spriteH);
            vehicles_.push_back(v);
            consoleOut("Spawned car near player");
        } else {
            consoleOut("usage: vehicle car");
        }
    } else if (strcmp(tok, "give") == 0) {
        auto ciPrefix = [](const char* name, const char* query) {
            while (*query) {
                if (tolower((unsigned char)*name) != tolower((unsigned char)*query)) return false;
                name++; query++;
            }
            return true;
        };
        // Try exact match first, then case-insensitive prefix
        bool found = false;
        int  prefixMatch = -1;
        for (int i = 0; i < (int)UpgradeType::COUNT; i++) {
            const char* n = getUpgradeInfo((UpgradeType)i).name;
            if (strcmp(n, rest) == 0) {
                applyUpgrade((UpgradeType)i);
                char msg[80]; snprintf(msg, sizeof(msg), " Gave: %s", n);
                consoleOut(msg); found = true; break;
            }
            if (prefixMatch < 0 && rest[0] != '\0' && ciPrefix(n, rest))
                prefixMatch = i;
        }
        if (!found && prefixMatch >= 0) {
            const char* n = getUpgradeInfo((UpgradeType)prefixMatch).name;
            applyUpgrade((UpgradeType)prefixMatch);
            char msg[80]; snprintf(msg, sizeof(msg), " Gave: %s", n);
            consoleOut(msg); found = true;
        }
        if (!found) {
            consoleOut(" Unknown upgrade. All upgrades:");
            std::string row;
            int col = 0;
            for (int i = 0; i < (int)UpgradeType::COUNT; i++) {
                if (col > 0) row += "  |  ";
                row += getUpgradeInfo((UpgradeType)i).name;
                if (++col == 4) { consoleOut((" " + row).c_str()); row.clear(); col = 0; }
            }
            if (!row.empty()) consoleOut((" " + row).c_str());
        }
    } else if (strcmp(tok, "bombs") == 0) {
        int n = 0;
        if (sscanf(rest, "%d", &n) == 1) {
            player_.bombCount = std::max(0, std::min(n, MAX_BOMBS));
            char msg[64]; snprintf(msg, sizeof(msg), "Bombs set to %d", player_.bombCount);
            consoleOut(msg);
        } else { consoleOut("usage: bombs <number>"); }
    } else if (strcmp(tok, "signal") == 0) {
        int n = 0;
        if (sscanf(rest, "%d", &n) == 1) {
            signal_ = std::max(0, std::min(100, n));
            char msg[64]; snprintf(msg, sizeof(msg), "SIGNAL set to %d", signal_);
            consoleOut(msg);
        } else {
            const char* tier = signal_ < 34 ? "Low" : (signal_ >= 67 ? "High" : "Mid");
            char msg[96]; snprintf(msg, sizeof(msg),
                "SIGNAL=%d (%s)  route=%s", signal_, tier,
                storyRoute_ == 1 ? "Spearhead" : storyRoute_ == 2 ? "Signal" : "undecided");
            consoleOut(msg);
        }
    } else if (strcmp(tok, "help") == 0) {
        consoleOut(" wave N | hp N | god | clear | bombs N | signal [N] | help");
        consoleOut(" spawn melee|shooter|brute|scout|sniper|gunner|boss_* [N]");
        consoleOut(" spawn_box | vehicle car");
        consoleOut(" give <upgrade_name>");
    } else if (tok[0] != '\0') {
        char msg[128]; snprintf(msg, sizeof(msg), "Unknown command: %s  (type help)", tok);
        consoleOut(msg);
    }
}

// Saved Servers

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

// Server Config Presets

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


