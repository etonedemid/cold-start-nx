// ─── game.cpp ─── Core: init, main loop, config, assets, rumble ──────────────
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

#include "game_internal.h"

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

// ═════════════════════════════════════════════════════════════════════════════
//  SoftKeyboard — centralized on-screen keyboard for all text input
// ═════════════════════════════════════════════════════════════════════════════
void Game::shutdown() {
    shutdownMultiplayer();
    editor_.shutdown();
    for (auto& cd : availableChars_) cd.unload();
    clearSyncedCharacters();
    ui_.shutdown();
    // Halt music BEFORE Assets::shutdown() frees the Mix_Music* pointers —
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
    // Always use native HID vibration on Switch — SDL_GameControllerRumble is
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
    bossBruteSprite_  = a.tex("sprites/enemy/boss_brute.png");
    bossSniperSprite_ = a.tex("sprites/enemy/boss_sniper.png");
    bossGunnerSprite_ = a.tex("sprites/enemy/boss_gunner.png");
    bulletSprite_  = a.tex("sprites/projectiles/bullet-player.png");
    // Red-tinted copy for enemy bullets – reuse same texture with color mod at draw time
    enemyBulletSprite_ = bulletSprite_;
    shieldSprite_  = a.tex("sprites/effects/shield.png");
    mainmenuBg_   = a.tex("sprites/ui/mainmenu.png");
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
    sfxHurt_     = a.sfx("hurt.mp3");
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

// ═════════════════════════════════════════════════════════════════════════════
//  Game State Management
// ══════════════════════════════

void Game::startGame() {
    state_ = GameState::Playing;
    gameTime_ = 0;
    discordSessionStart_ = (int64_t)time(nullptr);
    lobbySettings_.isPvp = false;
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    bossWaveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;

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

    map_.generate(config_.mapWidth, config_.mapHeight);
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

void Game::update() {
    float dt = dt_;
    gameTime_ += dt;

    screenFlashTimer_  = std::max(0.0f, screenFlashTimer_ - dt);
    muzzleFlashTimer_  = std::max(0.0f, muzzleFlashTimer_ - dt);
    pickupPopupTimer_  = std::max(0.0f, pickupPopupTimer_ - dt);
    waveAnnounceTimer_ = std::max(0.0f, waveAnnounceTimer_ - dt);
    cratePopupTimer_   = std::max(0.0f, cratePopupTimer_ - dt);

    // Low-HP red tint: ramps in below 40% HP, smoothly follows current HP
    {
        float hpRatio = (player_.maxHp > 0) ? (float)player_.hp / player_.maxHp : 1.0f;
        float target  = (hpRatio < 0.4f) ? (1.0f - hpRatio / 0.4f) : 0.0f;
        float speed   = (target > lowHpTint_) ? 2.0f : 4.0f; // fade in slower, fade out faster
        lowHpTint_ += (target - lowHpTint_) * std::min(1.0f, speed * dt);
    }

    if (actionMusicActive_ && !Mix_PlayingMusic())
        playActionMusic();

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


