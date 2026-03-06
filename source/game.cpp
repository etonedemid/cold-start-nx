// ─── game.cpp ─── Main game implementation ──────────────────────────────────
#include "game.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// ═════════════════════════════════════════════════════════════════════════════
//  Init / Shutdown
// ═════════════════════════════════════════════════════════════════════════════

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
    }
    Mix_AllocateChannels(32);

    window_ = SDL_CreateWindow("COLD START",
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

    // Lock the logical resolution to SCREEN_W x SCREEN_H.
    // SDL will scale and letterbox to fill any window/fullscreen size.
    SDL_RenderSetLogicalSize(renderer_, SCREEN_W, SCREEN_H);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

#ifndef __SWITCH__
    SDL_ShowCursor(SDL_DISABLE);
#endif
    // Cursor is re-enabled per-state in render() for editors that need it

    // Open all connected game controllers
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i))
            SDL_GameControllerOpen(i);
    }

    Assets::instance().init(renderer_);
    loadAssets();
    loadConfig();
#ifndef __SWITCH__
    if (config_.fullscreen)
        SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
    loadSavedServers();
    loadServerPresets();

    // Initialize map editor
    editor_.init(renderer_, SCREEN_W, SCREEN_H);

    // Scan for custom characters and maps
    scanCharacters();
    scanMapFiles();

    // Initialize mod system
    initMods();

    // Initialize multiplayer
    initMultiplayer();

    // Generate radial vignette texture
    {
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_RGBA8888);
        if (surf) {
            SDL_LockSurface(surf);
            Uint32* pixels = (Uint32*)surf->pixels;
            float cx = SCREEN_W / 2.0f;
            float cy = SCREEN_H / 2.0f;
            // maxDist not needed; we use normalized coords
            for (int py = 0; py < SCREEN_H; py++) {
                for (int px = 0; px < SCREEN_W; px++) {
                    float dx = (px - cx) / cx;  // normalized -1..1
                    float dy = (py - cy) / cy;
                    float dist = sqrtf(dx * dx + dy * dy); // 0 at center, ~1.41 at corners
                    // Smooth vignette curve: starts fading at 0.4, maxes out at edges
                    float t = (dist - 0.4f) / 0.9f;
                    if (t < 0) t = 0;
                    if (t > 1) t = 1;
                    t = t * t; // quadratic falloff
                    Uint8 alpha = (Uint8)(t * 120); // max 120 alpha at corners
                    // RGBA8888: R in bits 31-24, A in bits 7-0
                    pixels[py * (surf->pitch / 4) + px] = SDL_MapRGBA(surf->format, 0, 0, 0, alpha);
                }
            }
            SDL_UnlockSurface(surf);
            vignetteTex_ = SDL_CreateTextureFromSurface(renderer_, surf);
            if (vignetteTex_) {
                SDL_SetTextureBlendMode(vignetteTex_, SDL_BLENDMODE_BLEND);
            }
            SDL_FreeSurface(surf);
        }
    }

    // Start main menu music
    if (menuMusic_) {
        Mix_PlayMusic(menuMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
    }

    return true;
}

void Game::playMenuMusic() {
    Mix_HaltMusic();
    if (menuMusic_) {
        Mix_PlayMusic(menuMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
    }
}

void Game::shutdown() {
    shutdownMultiplayer();
    editor_.shutdown();
    for (auto& cd : availableChars_) cd.unload();
    Assets::instance().shutdown();
    if (vignetteTex_) SDL_DestroyTexture(vignetteTex_);
    if (bgMusic_) Mix_HaltMusic();
    Mix_CloseAudio();
    TTF_Quit();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
#ifdef __SWITCH__
    romfsExit();
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
//  Asset Loading
// ═════════════════════════════════════════════════════════════════════════════

void Game::loadAssets() {
    auto& a = Assets::instance();

    // Player body frames (body-01..10)
    playerSprites_ = a.loadAnim("sprites/player/body-", 10, 1);

    // Player death frames
    char buf[128];
    playerDeathSprites_.clear();
    for (int i = 1; i <= 12; i++) {
        snprintf(buf, sizeof(buf), "sprites/player/death-%d.png", i);
        auto* t = a.tex(buf);
        if (t) playerDeathSprites_.push_back(t);
    }

    // Leg frames (legs-01..08)
    legSprites_ = a.loadAnim("sprites/player/legs-", 8, 1);

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
    bulletSprite_  = a.tex("sprites/projectiles/bullet-player.png");
    // Red-tinted copy for enemy bullets – reuse same texture with color mod at draw time
    enemyBulletSprite_ = bulletSprite_;
    shieldSprite_  = a.tex("sprites/effects/shield.png");
    mainmenuBg_   = a.tex("sprites/ui/mainmenu.png");
    bloodTex_     = a.tex("sprites/effects/blood.png");

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
    sfxHurt_     = a.sfx("hurt.wav");
    sfxDeath_    = a.sfx("death.mp3");
    sfxExplosion_= a.sfx("explosion.mp3");
    sfxParry_    = a.sfx("parry.mp3");
    sfxSwoosh_   = a.sfx("swoosh.wav");
    sfxBeep_     = a.sfx("beep.mp3");
    sfxPress_    = a.sfx("press.mp3");
    bgMusic_     = a.music("cybergrind.mp3");
    menuMusic_   = a.music("mainmenu.mp3");
}

// ═════════════════════════════════════════════════════════════════════════════
//  Game State Management
// ═════════════════════════════════════════════════════════════════════════════

void Game::startGame() {
    state_ = GameState::Playing;
    gameTime_ = 0;
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

    // Reset player
    player_ = Player{};
    player_.maxHp = config_.playerMaxHp;
    player_.hp = config_.playerMaxHp;
    player_.pos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
    player_.bombCount = 1; // Start with one bomb

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
        Uint64 now = SDL_GetPerformanceCounter();
        dt_ = (float)(now - lastTime) / (float)freq;
        lastTime = now;
        if (dt_ > 0.05f) dt_ = 0.05f; // cap at 20fps min

        handleInput();

        // Always update the network (for lobby, connecting, in-game, etc.)
        {
            auto& net = NetworkManager::instance();
            if (net.isOnline()) net.update(dt_);
        }

        if (state_ == GameState::Playing || state_ == GameState::PlayingCustom
            || state_ == GameState::PlayingPack
            || state_ == GameState::MultiplayerGame
            || state_ == GameState::MultiplayerPaused
            || state_ == GameState::MultiplayerDead) {
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
            // Check if editor wants to test play
            if (editor_.wantsTestPlay()) {
                editor_.clearTestPlay();
                // Copy the editor's map directly into customMap_
                customMap_ = editor_.getMap();
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
                    EnemyType type = (es.enemyType == 1) ? EnemyType::Shooter : EnemyType::Melee;
                    spawnEnemy({es.x, es.y}, type);
                    customEnemiesTotal_++;
                }
                map_.findSpawnPoints();
                testPlayFromEditor_ = true;
            }
        }
        else if (state_ == GameState::SpriteEditor) {
            texEditor_.update(dt_);
            if (texEditor_.wantsExit()) {
                texEditor_.setActive(false);
                texEditor_.shutdown();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }

        render();
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
    pauseInput_ = false;
    confirmInput_ = false;
    backInput_ = false;
    leftInput_ = false;
    rightInput_ = false;

    // Username keyboard picker hold-repeat (16 columns, 64-char palette)
    {
        static const int KB_COLS    = 16;
        static const int KB_PAL_LEN = 64;
        bool kbActive = (usernameTyping_  && state_ == GameState::ConfigMenu) ||
                        (mpUsernameTyping_ && (state_ == GameState::HostSetup ||
                                               state_ == GameState::JoinMenu));
        if (kbActive && kbNavHeldButton_ >= 0 && SDL_GetTicks() >= kbNavRepeatAt_) {
            int delta = 0;
            if (kbNavHeldButton_ == SDL_CONTROLLER_BUTTON_DPAD_LEFT)  delta = -1;
            if (kbNavHeldButton_ == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) delta = +1;
            if (kbNavHeldButton_ == SDL_CONTROLLER_BUTTON_DPAD_UP)    delta = -KB_COLS;
            if (kbNavHeldButton_ == SDL_CONTROLLER_BUTTON_DPAD_DOWN)  delta = +KB_COLS;
            if (delta != 0)
                usernameCharIdx_ = (usernameCharIdx_ + delta + KB_PAL_LEN * 4) % KB_PAL_LEN;
            kbNavRepeatAt_ = SDL_GetTicks() + 80;
        }
        if (!kbActive) kbNavHeldButton_ = -1;
    }

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { running_ = false; return; }

        // Mod-save dialog gets first pick of all events when open
        if (modSaveDialog_.isOpen()) {
            handleModSaveDialogEvent(e);
            continue;
        }

        // Clear D-pad hold-repeat when button released
        if (e.type == SDL_CONTROLLERBUTTONUP) {
            if (e.cbutton.button == (Uint8)kbNavHeldButton_)
                kbNavHeldButton_ = -1;
        }

        // Pass events to editor if active
        if ((state_ == GameState::Editor || state_ == GameState::EditorConfig) && editor_.isActive()) {
            editor_.handleInput(e);
        }

        // Pass events to sprite/texture editor if active
        if (state_ == GameState::SpriteEditor && texEditor_.isActive()) {
            texEditor_.handleInput(e);
        }

        // Character creator text input handling
        if (state_ == GameState::CharCreator && charCreator_.textEditing) {
            // Gamepad text input for Switch
            static const char ccCharPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
            int& ccCharIdx = charCreator_.gpCharIdx;
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int palLen = (int)strlen(ccCharPalette);
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        ccCharIdx = (ccCharIdx - 1 + palLen) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        ccCharIdx = (ccCharIdx + 1) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        ccCharIdx = (ccCharIdx - 10 + palLen) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        ccCharIdx = (ccCharIdx + 10) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        charCreator_.textBuf += ccCharPalette[ccCharIdx];
                        break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        if (!charCreator_.textBuf.empty()) charCreator_.textBuf.pop_back();
                        break;
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_START:
                        charCreator_.name = charCreator_.textBuf;
                        charCreator_.textEditing = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                        charCreator_.textEditing = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        break;
                }
                continue;
            }
            if (e.type == SDL_TEXTINPUT) {
                charCreator_.textBuf += e.text.text;
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN) {
                    charCreator_.name = charCreator_.textBuf;
                    charCreator_.textEditing = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    continue;
                }
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    charCreator_.textEditing = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    continue;
                }
                if (e.key.keysym.sym == SDLK_BACKSPACE && !charCreator_.textBuf.empty()) {
                    charCreator_.textBuf.pop_back();
                    continue;
                }
                continue; // consume all keys while text editing
            }
        }

        // Port editing (JoinMenu keyboard input)
        if (state_ == GameState::JoinMenu && joinPortTyping_) {
            static const char portPalette[] = "0123456789";
            int palLen = (int)strlen(portPalette);
            if (e.type == SDL_TEXTINPUT) {
                for (const char* p = e.text.text; *p; p++) {
                    if (*p >= '0' && *p <= '9' && joinPortStr_.size() < 5)
                        joinPortStr_ += *p;
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER ||
                    e.key.keysym.sym == SDLK_ESCAPE) {
                    int v = joinPortStr_.empty() ? 7777 : std::stoi(joinPortStr_);
                    joinPort_ = std::max(1024, std::min(65535, v));
                    joinPortStr_ = std::to_string(joinPort_);
                    joinPortTyping_ = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    continue;
                }
                if (e.key.keysym.sym == SDLK_BACKSPACE && !joinPortStr_.empty())
                    joinPortStr_.pop_back();
                continue;
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        joinPortCharIdx_ = (joinPortCharIdx_ - 1 + palLen) % palLen; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        joinPortCharIdx_ = (joinPortCharIdx_ + 1) % palLen; break;
                    case SDL_CONTROLLER_BUTTON_A:
                        if (joinPortStr_.size() < 5) joinPortStr_ += portPalette[joinPortCharIdx_]; break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        if (!joinPortStr_.empty()) joinPortStr_.pop_back(); break;
                    case SDL_CONTROLLER_BUTTON_B:
                    case SDL_CONTROLLER_BUTTON_START: {
                        int v = joinPortStr_.empty() ? 7777 : std::stoi(joinPortStr_);
                        joinPort_ = std::max(1024, std::min(65535, v));
                        joinPortStr_ = std::to_string(joinPort_);
                        joinPortTyping_ = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        break;
                    }
                }
                continue;
            }
            continue;
        }

        // Port editing (HostSetup keyboard input)
        if (state_ == GameState::HostSetup && portTyping_) {
            static const char portPalette[] = "0123456789";
            int palLen = (int)strlen(portPalette);
            if (e.type == SDL_TEXTINPUT) {
                for (const char* p = e.text.text; *p; p++) {
                    if (*p >= '0' && *p <= '9' && portStr_.size() < 5)
                        portStr_ += *p;
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER ||
                    e.key.keysym.sym == SDLK_ESCAPE) {
                    int v = portStr_.empty() ? 7777 : std::stoi(portStr_);
                    hostPort_ = std::max(1024, std::min(65535, v));
                    portStr_ = std::to_string(hostPort_);
                    portTyping_ = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    continue;
                }
                if (e.key.keysym.sym == SDLK_BACKSPACE && !portStr_.empty())
                    portStr_.pop_back();
                continue;
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        portCharIdx_ = (portCharIdx_ - 1 + palLen) % palLen; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        portCharIdx_ = (portCharIdx_ + 1) % palLen; break;
                    case SDL_CONTROLLER_BUTTON_A:
                        if (portStr_.size() < 5) portStr_ += portPalette[portCharIdx_]; break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        if (!portStr_.empty()) portStr_.pop_back(); break;
                    case SDL_CONTROLLER_BUTTON_B:
                    case SDL_CONTROLLER_BUTTON_START: {
                        int v = portStr_.empty() ? 7777 : std::stoi(portStr_);
                        hostPort_ = std::max(1024, std::min(65535, v));
                        portStr_ = std::to_string(hostPort_);
                        portTyping_ = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        break;
                    }
                }
                continue;
            }
            continue;
        }

        // IP address editing (JoinMenu gamepad text input — same system as map editor)
        if (state_ == GameState::JoinMenu && ipTyping_) {
            static const char ipPalette[] = "0123456789.";
            int palLen = (int)strlen(ipPalette);
            if (e.type == SDL_TEXTINPUT) {
                for (const char* p = e.text.text; *p; p++) {
                    if ((*p >= '0' && *p <= '9') || *p == '.') {
                        if (joinAddress_.size() < 21)
                            joinAddress_ += *p;
                    }
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    ipTyping_ = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    joinGame();
                    continue;
                }
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    ipTyping_ = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    continue;
                }
                if (e.key.keysym.sym == SDLK_BACKSPACE && !joinAddress_.empty()) {
                    joinAddress_.pop_back();
                }
                continue;
            }
            // Gamepad palette cycling (Switch / gamepad)
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        ipCharIdx_ = (ipCharIdx_ - 1 + palLen) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        ipCharIdx_ = (ipCharIdx_ + 1) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        ipCharIdx_ = (ipCharIdx_ - 4 + palLen) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        ipCharIdx_ = (ipCharIdx_ + 4) % palLen;
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        joinAddress_ += ipPalette[ipCharIdx_];
                        break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        if (!joinAddress_.empty()) joinAddress_.pop_back();
                        break;
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_START:
                        ipTyping_ = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        joinGame();
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                        ipTyping_ = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        break;
                }
                continue;
            }
            continue;
        }

        // Username editing in Host/Join menus
        if ((state_ == GameState::HostSetup || state_ == GameState::JoinMenu) && mpUsernameTyping_) {
            static const char userPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
            int palLen = (int)strlen(userPalette);
            if (e.type == SDL_TEXTINPUT) {
                for (const char* p = e.text.text; *p; p++) {
                    if ((*p >= ' ' && *p <= '~') && *p != '=' && *p != '\n') {
                        if (config_.username.size() < 32)
                            config_.username += *p;
                    }
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER ||
                    e.key.keysym.sym == SDLK_ESCAPE) {
                    mpUsernameTyping_ = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    if (config_.username.empty()) config_.username = "Player";
                    NetworkManager::instance().setUsername(config_.username);
                    continue;
                }
                if (e.key.keysym.sym == SDLK_BACKSPACE && !config_.username.empty()) {
                    config_.username.pop_back();
                }
                continue;
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        usernameCharIdx_ = (usernameCharIdx_ - 1 + palLen) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        usernameCharIdx_ = (usernameCharIdx_ + 1) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        usernameCharIdx_ = (usernameCharIdx_ - 16 + palLen) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_UP;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        usernameCharIdx_ = (usernameCharIdx_ + 16) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        if (config_.username.size() < 32)
                            config_.username += userPalette[usernameCharIdx_];
                        break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        if (!config_.username.empty()) config_.username.pop_back();
                        break;
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_START:
                    case SDL_CONTROLLER_BUTTON_B:
                        mpUsernameTyping_ = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        if (config_.username.empty()) config_.username = "Player";
                        NetworkManager::instance().setUsername(config_.username);
                        break;
                }
                continue;
            }
            continue;
        }

        // Username editing (ConfigMenu gamepad + keyboard text input)
        if (state_ == GameState::ConfigMenu && usernameTyping_) {
            static const char userPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
            int palLen = (int)strlen(userPalette);
            if (e.type == SDL_TEXTINPUT) {
                for (const char* p = e.text.text; *p; p++) {
                    if ((*p >= ' ' && *p <= '~') && *p != '=' && *p != '\n') {
                        if (config_.username.size() < 32)
                            config_.username += *p;
                    }
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    usernameTyping_ = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    if (config_.username.empty()) config_.username = "Player";
                    NetworkManager::instance().setUsername(config_.username);
                    continue;
                }
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    usernameTyping_ = false;
#ifndef __SWITCH__
                    SDL_StopTextInput();
#endif
                    continue;
                }
                if (e.key.keysym.sym == SDLK_BACKSPACE && !config_.username.empty()) {
                    config_.username.pop_back();
                }
                continue;
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        usernameCharIdx_ = (usernameCharIdx_ - 1 + palLen) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        usernameCharIdx_ = (usernameCharIdx_ + 1) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        usernameCharIdx_ = (usernameCharIdx_ - 16 + palLen) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_UP;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        usernameCharIdx_ = (usernameCharIdx_ + 16) % palLen;
                        kbNavHeldButton_ = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
                        kbNavRepeatAt_   = SDL_GetTicks() + 350;
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        if (config_.username.size() < 32)
                            config_.username += userPalette[usernameCharIdx_];
                        break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        if (!config_.username.empty()) config_.username.pop_back();
                        break;
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_START:
                        usernameTyping_ = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        if (config_.username.empty()) config_.username = "Player";
                        NetworkManager::instance().setUsername(config_.username);
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                        usernameTyping_ = false;
#ifndef __SWITCH__
                        SDL_StopTextInput();
#endif
                        break;
                }
                continue;
            }
            continue;
        }

        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_START:    pauseInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_A:        confirmInput_ = true; if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); } break;
                case SDL_CONTROLLER_BUTTON_B:        backInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: parryInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_X:        bombInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:  menuSelection_--; if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); } break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:menuSelection_++; if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); } break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:leftInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:rightInput_ = true; break;
            }
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: pauseInput_ = true; break;
                case SDLK_RETURN: confirmInput_ = true; break;
                case SDLK_BACKSPACE: backInput_ = true; break;
                case SDLK_SPACE:  parryInput_ = true; break;
                case SDLK_q:     bombInput_ = true; break;
                case SDLK_UP:    menuSelection_--; break;
                case SDLK_DOWN:  menuSelection_++; break;
                case SDLK_LEFT:  leftInput_ = true; break;
                case SDLK_RIGHT: rightInput_ = true; break;
                case SDLK_F11:
                    config_.fullscreen = !config_.fullscreen;
                    SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    break;
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

    // Controller left stick
    SDL_GameController* gc = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(i);
            gc = SDL_GameControllerFromInstanceID(jid);
            break;
        }
    }
    if (gc) {
        float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        if (fabsf(lx) > 0.15f || fabsf(ly) > 0.15f) moveInput_ = {lx, ly};

        // Right stick for aiming
        float rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
        if (fabsf(rx) > 0.2f || fabsf(ry) > 0.2f) {
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
        else aimInput_ = {0, 0};

        // ZR = fire
        fireInput_ = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        // ZL = launch bomb (one-shot)
        bool zlDown = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        if (zlDown && !bombLaunchHeld_) bombLaunchInput_ = true;
        bombLaunchHeld_ = zlDown;
    } else {
        // PC: Mouse aiming
        int mx, my;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        Vec2 mouseWorld = camera_.screenToWorld({(float)mx, (float)my});
        Vec2 diff = mouseWorld - player_.pos;
        if (diff.length() > 5.0f) {
            aimInput_ = diff.normalized();
        } else {
            aimInput_ = moveInput_;
        }
        fireInput_ = (mb & SDL_BUTTON_LMASK) || keys[SDL_SCANCODE_J] || keys[SDL_SCANCODE_Z];
        if (mb & SDL_BUTTON_RMASK) bombLaunchInput_ = true;
    }

    // Normalize move
    if (moveInput_.length() > 1.0f) moveInput_ = moveInput_.normalized();

    // Handle menu state transitions
    if (state_ == GameState::MainMenu) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 10) menuSelection_ = 10;
        if (confirmInput_) {
            if (menuSelection_ == 0) startGame();
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
                // Sprite editor
                state_ = GameState::SpriteEditor;
                texEditor_.init(renderer_, SCREEN_W, SCREEN_H);
                texEditor_.setActive(true);
                texEditor_.showConfig();
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 4) {
                scanMapFiles();
                state_ = GameState::MapSelect;
                mapSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 5) {
                scanMapPacks();
                state_ = GameState::PackSelect;
                packSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 6) {
                scanCharacters();
                state_ = GameState::CharSelect;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 7) {
                // Create Character
                charCreator_ = CharCreatorState{};
                state_ = GameState::CharCreator;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 8) {
                // Mods
                ModManager::instance().scanMods();
                state_ = GameState::ModMenu;
                modMenuSelection_ = 0;
                modMenuTab_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 9) {
                state_ = GameState::ConfigMenu;
                configSelection_ = 0;
                menuSelection_ = 0;
            }
            else running_ = false;
        }
    }
    else if (state_ == GameState::ConfigMenu) {
        if (configSelection_ < 0) configSelection_ = 0;
        if (configSelection_ > 9) configSelection_ = 9;

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 9) menuSelection_ = 9;

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

            if (configSelection_ == 0) adjustInt(config_.mapWidth, 20, 120, 2);
            else if (configSelection_ == 1) adjustInt(config_.mapHeight, 14, 80, 2);
            else if (configSelection_ == 2) adjustInt(config_.playerMaxHp, 1, 20, 1);
            else if (configSelection_ == 3) adjustFloat(config_.spawnRateScale, 0.3f, 3.0f, 0.1f);
            else if (configSelection_ == 4) adjustFloat(config_.enemyHpScale, 0.3f, 3.0f, 0.1f);
            else if (configSelection_ == 5) adjustFloat(config_.enemySpeedScale, 0.5f, 2.5f, 0.1f);
            else if (configSelection_ == 6) { adjustInt(config_.musicVolume, 0, 128, 8); Mix_VolumeMusic(config_.musicVolume); }
            else if (configSelection_ == 7) { adjustInt(config_.sfxVolume, 0, 128, 8); }
            else if (configSelection_ == 8 && confirmInput_) {
                // Edit username
                usernameTyping_ = true;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
            }
            else if (configSelection_ == 9 && (confirmInput_ || backInput_ || pauseInput_)) {
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
            else if (menuSelection_ == 4) { state_ = GameState::MainMenu; playMenuMusic(); }
#else
            else if (menuSelection_ == 3) { state_ = GameState::MainMenu; playMenuMusic(); }
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
            if (menuSelection_ == 0) startGame();
            else { state_ = GameState::MainMenu; playMenuMusic(); menuSelection_ = 0; }
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
        if (backInput_) { state_ = GameState::MainMenu; menuSelection_ = 0; }
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
                state_ = GameState::MainMenu; menuSelection_ = 0;
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
                selectedChar_ = -1; // default
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
        if (cc.textEditing) {
            // Text input handled via SDL_TEXTINPUT events already processed
        } else {
            // Navigate fields
            if (menuSelection_ != cc.field) {
                cc.field = menuSelection_;
            }
            if (cc.field < 0) cc.field = 0;
            if (cc.field > 10) cc.field = 10;
            menuSelection_ = cc.field;

            // Adjust values with left/right
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
            if (cc.field == 6 && (leftInput_ || rightInput_)) {
                cc.bodyFrames += rightInput_ ? 1 : -1;
                if (cc.bodyFrames < 1) cc.bodyFrames = 1;
                if (cc.bodyFrames > 30) cc.bodyFrames = 30;
            }
            if (cc.field == 7 && (leftInput_ || rightInput_)) {
                cc.legFrames += rightInput_ ? 1 : -1;
                if (cc.legFrames < 1) cc.legFrames = 1;
                if (cc.legFrames > 30) cc.legFrames = 30;
            }
            if (cc.field == 8 && (leftInput_ || rightInput_)) {
                cc.deathFrames += rightInput_ ? 1 : -1;
                if (cc.deathFrames < 1) cc.deathFrames = 1;
                if (cc.deathFrames > 30) cc.deathFrames = 30;
            }

            // Name field: start text editing on confirm
            if (cc.field == 0 && confirmInput_) {
                cc.textEditing = true;
                cc.textBuf = cc.name;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
            }
            // Save button
            if (cc.field == 9 && confirmInput_) {
                charCreatorWantsModSave_ = true;
            }
            // Back button
            if (cc.field == 10 && confirmInput_) {
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }
        if (backInput_ && !cc.textEditing) {
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
        if (backInput_) { state_ = GameState::MainMenu; menuSelection_ = 0; }
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
                state_ = GameState::MainMenu; menuSelection_ = 0;
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
                gamemodeSelectIdx_ = 0;
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
        if (mpUsernameTyping_ || portTyping_) {
            // Editing consumes events; nothing else to do
        } else {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 6) menuSelection_ = 6;
        hostSetupSelection_ = menuSelection_;

        if (backInput_ || pauseInput_) { state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0; }
        if (hostSetupSelection_ == 0) {
            // Adjust max players with left/right (2-16)
            if (leftInput_)  hostMaxPlayers_ = std::max(2, hostMaxPlayers_ - 1);
            if (rightInput_) hostMaxPlayers_ = std::min(128, hostMaxPlayers_ + 1);
        }
        if (hostSetupSelection_ == 1 && confirmInput_) {
            portStr_ = std::to_string(hostPort_);
            portTyping_ = true;
            portCharIdx_ = 0;
#ifndef __SWITCH__
            SDL_StartTextInput();
#endif
        }
        if (hostSetupSelection_ == 2 && confirmInput_) {
            // Edit username
            mpUsernameTyping_ = true;
            usernameCharIdx_ = 0;
#ifndef __SWITCH__
            SDL_StartTextInput();
#endif
        }
        else if (hostSetupSelection_ == 3 && confirmInput_) {
            // Save current settings as preset
            addServerPreset("preset", "arena", hostMaxPlayers_, hostPort_, 0);
        }
        else if (hostSetupSelection_ == 4) {
            // Cycle through presets with left/right
            if (!serverPresets_.empty()) {
                int n = (int)serverPresets_.size();
                if (leftInput_) presetSelection_ = (presetSelection_ - 1 + n) % n;
                if (rightInput_) presetSelection_ = (presetSelection_ + 1) % n;
                if (confirmInput_) {
                    applyServerPreset(presetSelection_);
                }
            }
        }
        else if (confirmInput_) {
            if (hostSetupSelection_ == 5) {
                // Start hosting
                currentRules_ = createCoopArenaRules(hostMaxPlayers_);
                NetworkManager::instance().setGamemode("coop_arena");
                hostGame();
            }
            else if (hostSetupSelection_ == 6) {
                state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
            }
        }
        } // end !mpUsernameTyping_
    }
    else if (state_ == GameState::JoinMenu) {
        if (!ipTyping_ && !mpUsernameTyping_ && !joinPortTyping_) {
            if (menuSelection_ < 0) menuSelection_ = 0;
            if (menuSelection_ > 5) menuSelection_ = 5;
            joinMenuSelection_ = menuSelection_;

            if (backInput_ || pauseInput_) {
                connectStatus_.clear();
                state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
            }
            if (confirmInput_) {
                if (joinMenuSelection_ == 0) {
                    // Edit address
                    ipTyping_ = true;
                    ipCharIdx_ = 0;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                } else if (joinMenuSelection_ == 1) {
                    // Edit port
                    joinPortStr_ = std::to_string(joinPort_);
                    joinPortTyping_ = true;
                    joinPortCharIdx_ = 0;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                } else if (joinMenuSelection_ == 2) {
                    // Edit username
                    mpUsernameTyping_ = true;
                    usernameCharIdx_ = 0;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                } else if (joinMenuSelection_ == 3) {
                    // Connect
                    joinGame();
                } else if (joinMenuSelection_ == 4) {
                    // Save server
                    addSavedServer(joinAddress_, joinAddress_, joinPort_);
                    connectStatus_ = "Server saved!";
                } else if (joinMenuSelection_ == 5) {
                    // Back
                    connectStatus_.clear();
                    state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
                }
            }
        }
        // (while ipTyping_, joinPortTyping_, or mpUsernameTyping_, events are consumed above)
    }
    else if (state_ == GameState::Lobby) {
        auto& net = NetworkManager::instance();

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

        if (backInput_ || pauseInput_) {
            net.disconnect();
            connectStatus_.clear();
            state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
        }
        if (confirmInput_) {
            if (net.isHost()) {
                startMultiplayerGame();
            } else {
                // Only allow ready-up once actually connected to lobby
                if (net.state() == NetState::InLobby) {
                    lobbyReady_ = !lobbyReady_;
                    net.setReady(lobbyReady_);
                }
            }
        }
        // Host can adjust lobby settings with UP/DOWN/LEFT/RIGHT
        if (net.isHost()) {
            // Settings row list:
            //   0=Gamemode(PVP/PVE), 1=FriendlyFire, 2=Upgrades, 3=MapWidth, 4=MapHeight,
            //   5=EnemyHP, 6=EnemySpeed, 7=SpawnRate, 8=PlayerHP,
            //   9=TeamCount, 10=Lives, 11=LivesMode, 12=CrateInterval(PVP), 13=WaveCount(PVE)
            // Compute effective setting count based on current mode
            int SETTING_COUNT = 12; // base: 0-11
            if (lobbySettings_.livesPerPlayer == 0) SETTING_COUNT = 11; // hide LivesMode
            // Add conditional setting
            if (lobbySettings_.isPvp) SETTING_COUNT++; // CrateInterval
            else SETTING_COUNT++; // WaveCount

            if (menuSelection_ < 0) menuSelection_ = SETTING_COUNT - 1;
            if (menuSelection_ >= SETTING_COUNT) menuSelection_ = 0;
            lobbySettingsSel_ = menuSelection_;

            if (leftInput_ || rightInput_) {
                int dir = rightInput_ ? 1 : -1;
                switch (lobbySettingsSel_) {
                    case 0: // Gamemode (PVP vs PVE)
                        lobbySettings_.isPvp = !lobbySettings_.isPvp;
                        break;
                    case 1: // Friendly fire
                        lobbySettings_.friendlyFire = !lobbySettings_.friendlyFire;
                        lobbySettings_.pvpEnabled   = lobbySettings_.friendlyFire;
                        currentRules_.friendlyFire  = lobbySettings_.friendlyFire;
                        currentRules_.pvpEnabled    = lobbySettings_.pvpEnabled;
                        break;
                    case 2: // Upgrades shared
                        lobbySettings_.upgradesShared = !lobbySettings_.upgradesShared;
                        break;
                    case 3: // Map width
                        lobbySettings_.mapWidth = std::max(10, std::min(200, lobbySettings_.mapWidth + dir * 10));
                        break;
                    case 4: // Map height
                        lobbySettings_.mapHeight = std::max(10, std::min(200, lobbySettings_.mapHeight + dir * 10));
                        break;
                    case 5: // Enemy HP
                        lobbySettings_.enemyHpScale = std::max(0.1f, std::min(5.0f, lobbySettings_.enemyHpScale + dir * 0.1f));
                        break;
                    case 6: // Enemy speed
                        lobbySettings_.enemySpeedScale = std::max(0.1f, std::min(5.0f, lobbySettings_.enemySpeedScale + dir * 0.1f));
                        break;
                    case 7: // Spawn rate
                        lobbySettings_.spawnRateScale = std::max(0.1f, std::min(5.0f, lobbySettings_.spawnRateScale + dir * 0.1f));
                        break;
                    case 8: // Player HP
                        lobbySettings_.playerMaxHp = std::max(1, std::min(100, lobbySettings_.playerMaxHp + dir));
                        break;
                    case 9: { // Team count
                        int tc = lobbySettings_.teamCount;
                        if (dir > 0) tc = (tc == 0) ? 2 : (tc == 2) ? 4 : 0;
                        else         tc = (tc == 0) ? 4 : (tc == 4) ? 2 : 0;
                        lobbySettings_.teamCount = tc;
                        currentRules_.teamCount = tc;
                        break;
                    }
                    case 10: // Lives per player (0-100)
                        lobbySettings_.livesPerPlayer = std::max(0, std::min(100, lobbySettings_.livesPerPlayer + dir));
                        break;
                    case 11: // Lives mode OR conditional setting
                        if (lobbySettings_.livesPerPlayer > 0) {
                            // LivesMode
                            lobbySettings_.livesShared = !lobbySettings_.livesShared;
                        } else if (lobbySettings_.isPvp) {
                            // CrateInterval (no livesMode visible, so 11 = crateInterval)
                            lobbySettings_.crateInterval = std::max(5.0f, std::min(120.0f, lobbySettings_.crateInterval + dir * 5.0f));
                        } else {
                            // WaveCount (no livesMode visible, so 11 = waveCount)
                            lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * (lobbySettings_.waveCount >= 10 ? 10 : 1)));
                        }
                        break;
                    case 12: // CrateInterval (PVP, when lives>0) or WaveCount (PVE, when lives>0)
                        if (lobbySettings_.isPvp) {
                            lobbySettings_.crateInterval = std::max(5.0f, std::min(120.0f, lobbySettings_.crateInterval + dir * 5.0f));
                        } else {
                            lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * (lobbySettings_.waveCount >= 10 ? 10 : 1)));
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
        bool isHostPlayer = net2.isHost();

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
        //  1 = Change Team   (if hasTeams)
        //  1/2 = Admin Menu  (if isHostPlayer)
        //  next = End Game   (if isHostPlayer)
        //  last = Disconnect / Back to Lobby
        int resumeIdx = 0;
        int teamIdx   = hasTeams ? 1 : -1;
        int adminIdx  = isHostPlayer ? (hasTeams ? 2 : 1) : -1;
        int endGameIdx = -1;
        int nextIdx   = 1 + (hasTeams ? 1 : 0) + (isHostPlayer ? 1 : 0);
        if (isHostPlayer) { endGameIdx = nextIdx; nextIdx++; }
        int dcIdx     = nextIdx;
        int maxSel    = dcIdx;

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > maxSel) menuSelection_ = maxSel;

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
                net2.sendGameEnd();
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
    else if (state_ == GameState::Scoreboard) {
        if (confirmInput_ || backInput_) {
            NetworkManager::instance().disconnect();
            playMenuMusic();
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
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

    // Only run gameplay logic in active playing states
    bool isPlayingState =
        state_ == GameState::Playing || state_ == GameState::Paused || state_ == GameState::Dead ||
        state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused ||
        state_ == GameState::CustomDead || state_ == GameState::CustomWin ||
        state_ == GameState::PlayingPack || state_ == GameState::PackPaused ||
        state_ == GameState::PackDead || state_ == GameState::PackLevelWin ||
        state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
        state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator;

    if (isPlayingState) {
        updatePlayer(dt);
        updateEnemies(dt);
        updateBullets(dt);
        updateBombs(dt);
        updateExplosions(dt);
        updateBoxFragments(dt);
        updateSpawning(dt);
        updateCrates(dt);
        updatePickups(dt);
        resolveCollisions();

        // Camera
        Vec2 aimDir = {0,0};
        if (aimInput_.lengthSq() > 0.04f) aimDir = aimInput_.normalized();
        else if (player_.moving && player_.vel.lengthSq() > 1.0f) aimDir = player_.vel.normalized();
        camera_.update(player_.pos, aimDir, dt);

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

    // ── Mod-save dialog: open when any editor flags it ──────────────────────
    if (!modSaveDialog_.isOpen()) {
        if (editor_.wantsModSave()) {
            editor_.clearWantsModSave();
            openModSaveDialog(ModSaveDialogState::AssetMap);
        } else if (texEditor_.wantsModSave()) {
            texEditor_.clearWantsModSave();
            openModSaveDialog(ModSaveDialogState::AssetSprite);
        } else if (charCreatorWantsModSave_) {
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
            case ModSaveDialogState::AssetSprite:
                texEditor_.performModSave(folder, modSaveDialog_.confirmedCat);
                break;
            case ModSaveDialogState::AssetCharacter:
                saveCharCreatorToMod(folder);
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
                break;
        }
        // Refresh mod list so the new mod shows up immediately
        ModManager::instance().scanMods();
        modSaveDialog_.close();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Player Update
// ═════════════════════════════════════════════════════════════════════════════

void Game::updatePlayer(float dt) {
    Player& p = player_;
    if (p.dead) {
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
        if (p.deathTimer > 1.5f && state_ != GameState::Dead
            && state_ != GameState::MultiplayerDead
            && state_ != GameState::CustomDead
            && state_ != GameState::PackDead) {
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

    // ── Movement ──
    Vec2 targetVel = {0, 0};
    p.moving = moveInput_.lengthSq() > 0.01f;
    if (p.moving) {
        targetVel = moveInput_.normalized() * p.speed * moveInput_.length();
    }

    // Smooth velocity (like Unity's Lerp)
    p.vel = Vec2::lerp(p.vel, targetVel, dt * PLAYER_SMOOTHING);

    // Parry dash override
    if (p.isParrying && p.parryTimer > 0) {
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

    // ── Body animation: shooting anim ──
    p.shootAnimTimer -= dt;
    if (p.shootAnimTimer > 0) {
        p.animFrame = 2; // Sprite-0003 (shooting/recoil)
    } else if (p.hasFiredOnce) {
        p.animFrame = 1; // Sprite-0002 (gun ready, default after first shot)
    } else {
        p.animFrame = 0; // Sprite-0001 (idle, never fired)
    }

    // Leg anim (faster at higher movement speed)
    if (p.moving && !legSprites_.empty()) {
        p.legAnimTimer += dt;
        float speedNorm = std::min(1.0f, p.vel.length() / p.speed);
        float legFrameInterval = 0.12f - speedNorm * 0.050f; // 0.12 (slow) -> 0.07 (fast)
        if (p.legAnimTimer > legFrameInterval) {
            p.legAnimTimer = 0;
            p.legAnimFrame = (p.legAnimFrame + 1) % (int)legSprites_.size();
        }
    } else {
        p.legAnimFrame = 0;
    }

    // ── Invulnerability ──
    if (p.invulnerable) {
        p.invulnTimer -= dt;
        if (p.invulnTimer <= 0) p.invulnerable = false;
    }

    // ── Shooting ──
    p.fireCooldown -= dt;
    if (!spectatorMode_) {
    if (p.reloading) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0) {
            p.ammo = p.maxAmmo;
            p.reloading = false;
        }
    } else if (fireInput_ && p.fireCooldown <= 0 && p.ammo > 0) {
        spawnBullet(p.pos, p.rotation);
        // Triple shot – two extra bullets at ±15°
        if (upgrades_.hasTripleShot) {
            const float spread = 0.26f; // ~15 degrees
            spawnBullet(p.pos, p.rotation + spread);
            spawnBullet(p.pos, p.rotation - spread);
        }
        p.ammo--;
        p.fireCooldown = 1.0f / p.fireRate;
        p.hasFiredOnce = true;
        p.shootAnimTimer = 0.12f;
        camera_.addShake(1.2f);
        if (sfxShoot_) { int ch = Mix_PlayChannel(-1, sfxShoot_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    } else if (fireInput_ && p.ammo <= 0 && !p.reloading) {
        // Auto-reload
        p.reloading = true;
        p.reloadTimer = p.reloadTime;
        if (sfxReload_) { int ch = Mix_PlayChannel(-1, sfxReload_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
    }
    } // !spectatorMode_

    // ── Parry ──
    if (!spectatorMode_) {
    if (parryInput_ && p.canParry && !p.isParrying) {
        playerParry();
    }
    if (p.isParrying) {
        p.parryTimer -= dt;
        if (p.parryTimer <= 0) {
            p.isParrying = false;
        }
    }
    if (!p.canParry) {
        p.parryCdTimer -= dt;
        if (p.parryCdTimer <= 0) p.canParry = true;
    }
    } // !spectatorMode_

    // ── Bombs ──
    if (!spectatorMode_) {
        bool hasOrbiting = false;
        for (auto& b : bombs_) if (b.alive && !b.hasDashed) { hasOrbiting = true; break; }
        if (!hasOrbiting && p.bombCount > 0) {
            spawnBomb();
            p.bombCount--;
        }
    }
    // ZL / RMB = Launch the nearest orbiting bomb
    if (!spectatorMode_ && bombLaunchInput_) {
        // Find an orbiting (non-dashed) bomb
        Bomb* toFire = nullptr;
        for (auto& b : bombs_) {
            if (b.alive && !b.hasDashed) { toFire = &b; break; }
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
                        bestIdx = -1; // player target wins over AI enemy
                    }
                }
            }

            Vec2 launchDir;
            // Reset both homing fields before assigning
            toFire->homingTarget   = -1;
            toFire->homingPlayerId = 255;
            toFire->homingStr      = 0;
            if (bestPlayerId != 255) {
                // Home toward closest enemy player
                auto& net2 = NetworkManager::instance();
                NetPlayer* tp = net2.findPlayer(bestPlayerId);
                launchDir = tp ? (tp->targetPos - toFire->pos).normalized() : aimDir;
                toFire->homingPlayerId = bestPlayerId;
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
                net.sendBombSpawn(toFire->pos, toFire->vel, net.localPlayerId());
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

    // Clients: smoothly interpolate enemy positions from last host snapshot
    if (net.isOnline() && !net.isHost()) {
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            float lerpRate = (e.netIsDashing) ? 25.0f : 14.0f;
            e.pos = Vec2::lerp(e.pos, e.netTargetPos, std::min(1.0f, dt * lerpRate));
            if (e.damageFlash > 0) e.damageFlash -= dt * 4.0f;
        }
        return;
    }

    for (auto& e : enemies_) {
        if (!e.alive) continue;

        // Stun
        if (e.stunTimer > 0) { e.stunTimer -= dt; continue; }

        // Damage flash decay
        if (e.damageFlash > 0) e.damageFlash -= dt * 4.0f;

        // Vision check — sets e.targetPlayerId if a player is seen
        e.canSeePlayer = enemyCanSeeAnyPlayer(e);
        if (e.canSeePlayer) {
            e.state = EnemyState::Chase;
            e.lastSeenTime = gameTime_;
            e.idleTimer = 0;
        } else if (e.state == EnemyState::Chase && gameTime_ - e.lastSeenTime > LOSE_PLAYER_DELAY) {
            e.state = EnemyState::Wander;
        }

        // After 30s of wandering with no sight of any player, hunt down the nearest one
        if (e.state == EnemyState::Wander) {
            e.idleTimer += dt;
            if (e.idleTimer > 30.0f) {
                e.idleTimer = 0;
                // Find nearest alive non-spectating player
                float best = 1e9f;
                uint8_t bestId = 255;
                auto testClose = [&](uint8_t pid, Vec2 ppos) {
                    float d = (ppos - e.pos).length();
                    if (d < best) { best = d; bestId = pid; }
                };
                if (net.isOnline()) {
                    for (auto& p : net.players()) { if (p.alive && !p.spectating) testClose(p.id, p.pos); }
                } else {
                    if (!player_.dead) testClose(0, player_.pos);
                }
                if (bestId != 255) {
                    e.targetPlayerId = bestId;
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
                e.dashTimer = ENEMY_DASH_DUR;
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

        // Rotation toward movement
        if (e.vel.lengthSq() > 1.0f && !e.isDashing && !e.dashCharging) {
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
    bool sawCurrentTarget = false;

    // Helper: LOS check for one candidate player
    auto testVis = [&](uint8_t pid, Vec2 ppos) {
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
        if (pid == e.targetPlayerId) sawCurrentTarget = true;
        if (dist < bestDist) { bestDist = dist; newTarget = pid; }
    };

    if (net.isOnline()) {
        for (auto& p : net.players()) {
            if (p.alive && !p.spectating) testVis(p.id, p.pos);
        }
    } else {
        if (!player_.dead) testVis(0, player_.pos);
    }

    if (newTarget != 255) {
        // Keep chasing current target while they're still visible;
        // switch to closest visible if target gone or not yet set
        if (!sawCurrentTarget || e.targetPlayerId == 255)
            e.targetPlayerId = newTarget;
        return true;
    }
    return false;
}

Vec2 Game::getEnemyTargetPos(const Enemy& e) const {
    auto& net = NetworkManager::instance();
    if (net.isOnline() && e.targetPlayerId != 255) {
        for (auto& p : net.players()) {
            if (p.id == e.targetPlayerId && p.alive && !p.spectating)
                return p.pos;
        }
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
    if (e.type == EnemyType::Melee) {
        float inertia = MELEE_INERTIA;
        e.vel = Vec2::lerp(e.vel, desired, dt * inertia);
    } else {
        e.vel = desired;
    }
}

void Game::enemyChase(Enemy& e, float dt) {
    Vec2 targetPos = getEnemyTargetPos(e);
    Vec2 toPlayer  = targetPos - e.pos;
    float dist     = toPlayer.length();

    // Shooter: keep range and strafe while shooting
    if (e.type == EnemyType::Shooter) {
        if (dist < 260.0f) {
            // Back away and strafe sideways
            Vec2 away  = (e.pos - targetPos).normalized();
            Vec2 perp  = Vec2{-away.y, away.x}; // perpendicular
            float sideSign = (sinf(gameTime_ * 1.1f) > 0) ? 1.0f : -1.0f;
            Vec2 retreat = (away + perp * sideSign * 0.6f).normalized();
            e.vel = steerToward(e.pos, e.pos + retreat * 200.0f, e.speed * 0.7f, dt);
        } else if (dist > 420.0f) {
            e.vel = steerToward(e.pos, targetPos, e.speed, dt);
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
    if (dist < ENEMY_DASH_DIST && !e.dashOnCd && !e.dashCharging) {
        e.dashCharging   = true;
        e.dashDelayTimer = ENEMY_DASH_DELAY;
        e.flashTimer     = ENEMY_DASH_DELAY;
        e.dashDir        = toPlayer.normalized(); // lock dash direction now
        e.vel = {0, 0}; // stop for wind-up
    }
}

void Game::enemyDash(Enemy& e, float dt) {
    e.vel = e.dashDir * ENEMY_DASH_FORCE;
    e.dashTimer -= dt;
    if (e.dashTimer <= 0) {
        e.isDashing = false;
        e.dashOnCd = true;
        e.dashCdTimer = ENEMY_DASH_CD;
        // Keep some momentum after dash instead of hard stop
        e.vel = e.dashDir * ENEMY_SPEED * 0.5f;
    }
}

void Game::enemyShoot(Enemy& e, float dt) {
    e.shootCooldown -= dt;
    if (e.shootCooldown <= 0 && e.canSeePlayer) {
        spawnEnemyBullet(e.pos, getEnemyTargetPos(e));
        e.shootCooldown = SHOOTER_SHOOT_CD;
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
    for (auto& b : bullets_) {
        b.tick(dt);
        // Wall collision
        int tx = TileMap::toTile(b.pos.x);
        int ty = TileMap::toTile(b.pos.y);
        if (map_.isSolid(tx, ty)) {
            // Check if it's a breakable box
            if (map_.get(tx, ty) == TILE_BOX) {
                destroyBox(tx, ty);
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
                // Step back out of wall
                b.pos.x = b.pos.x - b.vel.x * dt * 2.0f;
                b.pos.y = b.pos.y - b.vel.y * dt * 2.0f;
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
    b.pos = pos + fwd * 30.0f + right * GUN_OFFSET_RIGHT;
    b.vel = Vec2::fromAngle(angle) * BULLET_SPEED;
    b.rotation = angle;
    b.size = BULLET_SIZE;
    b.lifetime = BULLET_LIFETIME;
    b.tag = TAG_BULLET;
    b.sprite = bulletSprite_;
    b.damage = std::max(1, (int)roundf(upgrades_.damageMulti));
    b.piercing = upgrades_.hasPiercing;

    // Assign a stable network ID so remote peers can remove this bullet on hit
    auto& net = NetworkManager::instance();
    if (net.isInGame()) {
        b.netId = nextBulletNetId_++;
        if (nextBulletNetId_ == 0) nextBulletNetId_ = 1; // skip 0 (means "not networked")
        b.ownerId = net.localPlayerId();
    }

    bullets_.push_back(b);

    // ── Visual polish: muzzle flash ──
    muzzleFlashTimer_ = 0.06f;
    muzzleFlashPos_ = b.pos; // store exact bullet spawn point
    camera_.addShake(1.5f);  // subtle recoil shake

    // ── Sync bullet to other players ──
    if (net.isInGame()) {
        net.sendBulletSpawn(b.pos, angle, net.localPlayerId(), b.netId);
    }
}

void Game::spawnEnemyBullet(Vec2 pos, Vec2 target) {
    Entity b;
    Vec2 dir = (target - pos).normalized();
    Vec2 right = {-dir.y, dir.x};
    b.pos = pos + dir * 30.0f + right * 8.0f; // gun offset for enemies too
    b.vel = dir * ENEMY_BULLET_SPEED;
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
                    Vec2 toTarget = tp->targetPos - b.pos;
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
                }
            }

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
                    spawnExplosion(b.pos);
                    b.alive = false;
                }
            }
            // Explode if speed drops too low
            if (b.vel.length() < 30.0f) {
                spawnExplosion(b.pos);
                b.alive = false;
            }
            // Proximity: explode on contact with any enemy
            if (b.alive) {
                for (auto& e : enemies_) {
                    if (!e.alive) continue;
                    if (Vec2::dist(b.pos, e.pos) < BOMB_SIZE + 20.0f) {
                        spawnExplosion(b.pos);
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
                        spawnExplosion(b.pos);
                        b.alive = false;
                        break;
                    }
                }
            }
        } else {
            // Orbit around the player
            b.orbitAngle += b.orbitSpeed * dt;
            b.pos.x = player_.pos.x + cosf(b.orbitAngle) * b.orbitRadius;
            b.pos.y = player_.pos.y + sinf(b.orbitAngle) * b.orbitRadius;
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
            for (auto& e : enemies_) {
                if (!e.alive) continue;
                if (Vec2::dist(ex.pos, e.pos) < ex.radius) {
                    e.hp -= ex.damage;
                    e.damageFlash = 1.0f;
                    if (e.hp <= 0) killEnemy(e);
                }
            }
            // In PvP, explosions deal 3 HP damage to the local player — host-validated
            if ((lobbySettings_.isPvp || currentRules_.pvpEnabled) &&
                !player_.dead &&
                Vec2::dist(ex.pos, player_.pos) < ex.radius) {
                auto& netEx = NetworkManager::instance();
                if (netEx.isHost()) {
                    // Host applies damage and broadcasts authoritative HP
                    player_.takeDamage(3);
                    NetPlayer* localNetP = netEx.localPlayer();
                    if (localNetP) localNetP->hp = player_.hp;
                    if (player_.dead) {
                        netEx.sendPlayerDied(netEx.localPlayerId(), 255);
                    } else {
                        netEx.sendPlayerHpSync(netEx.localPlayerId(), player_.hp, player_.maxHp, 255);
                    }
                }
                // Clients do not apply damage locally — they wait for PlayerHpSync from host
            }
            // In PvP, host also applies explosion damage to remote players
            if ((lobbySettings_.isPvp || currentRules_.pvpEnabled)) {
                auto& netEx2 = NetworkManager::instance();
                if (netEx2.isHost()) {
                    for (const auto& rp : netEx2.players()) {
                        if (rp.id == netEx2.localPlayerId() || !rp.alive) continue;
                        if (localTeam_ >= 0 && rp.team == localTeam_) continue;
                        if (Vec2::dist(ex.pos, rp.pos) < ex.radius) {
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
                    }
                }
            }
            ex.dealtDmg = true;
        }
    }
}

void Game::spawnExplosion(Vec2 pos) {
    Explosion ex;
    ex.pos = pos;
    explosions_.push_back(ex);
    camera_.addShake(6.0f);
    if (sfxExplosion_) { int ch = Mix_PlayChannel(-1, sfxExplosion_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }

    // ── Sync explosion to other players (guard prevents echo-loop from network callback) ──
    auto& net = NetworkManager::instance();
    if (net.isOnline() && !suppressNetExplosion_) {
        net.sendExplosion(pos);
    }

    // ── Visual polish: screen flash on explosion ──
    screenFlashTimer_ = 0.15f;
    screenFlashR_ = 255; screenFlashG_ = 200; screenFlashB_ = 80;

    // ── Spawn fire/debris particles ──
    int numSparks = 20 + rand() % 12;
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
    int numSmoke = 8 + rand() % 6;
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

    // ── Scorch marks on ground ──
    {
        BloodDecal scorch;
        scorch.pos = pos;
        scorch.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
        scorch.scale = 1.5f + (float)(rand() % 50) / 100.0f;
        scorch.type = DecalType::Scorch;
        blood_.push_back(scorch);
        // Smaller surrounding scorch splatters
        int numScorch = 3 + rand() % 3;
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

void Game::spawnBomb() {
    Bomb b;
    b.pos = player_.pos;
    // Start at a random angle around the player
    b.orbitAngle = (float)(rand() % 360) * M_PI / 180.0f;
    b.orbitRadius = 55.0f + (float)(rand() % 20);
    b.orbitSpeed = 3.0f + (float)(rand() % 100) / 100.0f; // radians/sec
    bombs_.push_back(b);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Spawning
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateSpawning(float dt) {
    // In multiplayer the host is authoritative — clients receive enemy spawns via state packets
    {
        auto& net = NetworkManager::instance();
        if (net.isOnline() && !net.isHost()) return;
    }
    // In sandbox mode or PvP mode skip all wave spawning
    if (sandboxMode_ || lobbySettings_.isPvp) return;
    // Wave-based spawning system
    if (waveActive_) {
        // Spawning enemies within current wave
        waveSpawnTimer_ -= dt;
        if (waveSpawnTimer_ <= 0 && waveEnemiesLeft_ > 0) {
            waveSpawnTimer_ = WAVE_SPAWN_INTERVAL;
            if (!map_.spawnPoints.empty()) {
                for (int attempt = 0; attempt < 5; attempt++) {
                    Vec2 sp = map_.spawnPoints[rand() % map_.spawnPoints.size()];
                    if (!map_.worldCollides(sp.x, sp.y, ENEMY_SIZE * 0.5f)) {
                        float roll = (float)(rand() % 100) / 100.0f;
                        EnemyType type = (roll < WAVE_SHOOTER_CHANCE) ? EnemyType::Shooter : EnemyType::Melee;
                        spawnEnemy(sp, type);
                        waveEnemiesLeft_--;
                        break;
                    }
                }
            }
        }
        if (waveEnemiesLeft_ <= 0) {
            waveActive_ = false;

            // ── PVE victory check: all enemies dead + reached wave count ──
            if (!lobbySettings_.isPvp && lobbySettings_.waveCount > 0 &&
                waveNumber_ >= lobbySettings_.waveCount) {
                // Check if all enemies are dead
                bool allDead = true;
                for (auto& e : enemies_) {
                    if (e.alive) { allDead = false; break; }
                }
                if (allDead) {
                    auto& net = NetworkManager::instance();
                    if (net.isHost()) {
                        net.sendGameEnd();
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
            if (!lobbySettings_.isPvp && lobbySettings_.waveCount > 0 &&
                waveNumber_ >= lobbySettings_.waveCount) {
                // Check if all enemies are dead for victory
                bool allDead = true;
                for (auto& e : enemies_) {
                    if (e.alive) { allDead = false; break; }
                }
                if (allDead) {
                    auto& net = NetworkManager::instance();
                    if (net.isHost()) {
                        net.sendGameEnd();
                    }
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

            // ── Sync wave start to clients (host only) ──
            auto& net = NetworkManager::instance();
            if (net.isHost()) net.sendWaveStart(waveNumber_);

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
    if (type == EnemyType::Shooter) {
        e.hp = SHOOTER_HP * config_.enemyHpScale;
        e.maxHp = SHOOTER_HP * config_.enemyHpScale;
        e.speed = SHOOTER_SPEED * config_.enemySpeedScale;
        e.size = SHOOTER_SIZE;
        e.shootCooldown = SHOOTER_SHOOT_CD;
    } else {
        e.hp = ENEMY_HP * config_.enemyHpScale;
        e.maxHp = ENEMY_HP * config_.enemyHpScale;
        e.speed = ENEMY_SPEED * config_.enemySpeedScale;
    }
    enemies_.push_back(e);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Collisions
// ═════════════════════════════════════════════════════════════════════════════

void Game::resolveCollisions() {
    Player& p = player_;
    auto& net = NetworkManager::instance();

    // Player bullets vs enemies — all peers resolve locally for instant feedback;
    // the killer also sends EnemyKilled so the host/others stay in sync.
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            if (circleOverlap(b.pos, b.size, e.pos, e.size * 0.7f)) {
                e.hp -= b.damage;
                e.damageFlash = 1.0f;
                if (!b.piercing) {
                    b.alive = false;
                    // Notify remote peers that this bullet is gone
                    if (net.isInGame() && b.netId != 0) {
                        net.sendBulletHit(b.netId);
                    }
                }
                // Aggro — target the player who shot this bullet
                e.state = EnemyState::Chase;
                e.lastSeenTime = gameTime_;
                e.idleTimer    = 0;
                if (b.ownerId != 255) e.targetPlayerId = b.ownerId;
                if (e.hp <= 0) {
                    uint32_t eIdx = (uint32_t)(&e - &enemies_[0]);
                    killEnemy(e);
                    // Broadcast kill so clients sync immediately
                    if (net.isInGame()) {
                        net.sendEnemyKilled(eIdx, net.localPlayerId());
                        enemyStatesNeedUpdate_ = true;
                    }
                }
                break;
            }
        }
    }

    // Enemy bullets vs player
    for (auto& b : enemyBullets_) {
        if (!b.alive) continue;
        if (circleOverlap(b.pos, b.size, p.pos, PLAYER_SIZE * 0.5f)) {
            if (p.isParrying) {
                // Parry reflects!
                b.vel = b.vel * -1.0f;
                b.tag = TAG_BULLET;
                bullets_.push_back(b);
                b.alive = false;
                camera_.addShake(2.2f);
                if (sfxParry_) { int ch = Mix_PlayChannel(-1, sfxParry_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
            } else {
                p.takeDamage(b.damage);
                b.alive = false;
                camera_.addShake(1.8f);
                if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                if (p.dead) {
                    if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    camera_.addShake(4.0f);
                    auto& net = NetworkManager::instance();
                    if (net.isInGame()) net.sendPlayerDied(net.localPlayerId(), 0);
                }
            }
        }
    }

    // Melee enemies vs player (dash collision)
    for (auto& e : enemies_) {
        if (!e.alive || !e.isDashing) continue;
        if (circleOverlap(e.pos, e.size * 0.6f, p.pos, PLAYER_SIZE * 0.5f)) {
            if (p.isParrying) {
                // Parry counter!
                e.hp -= PARRY_DMG;
                e.stunTimer = 1.0f;
                e.isDashing = false;
                e.vel = (e.pos - p.pos).normalized() * 500.0f;
                e.damageFlash = 1.0f;
                camera_.addShake(2.2f);
                if (sfxParry_) { int ch = Mix_PlayChannel(-1, sfxParry_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                if (e.hp <= 0) killEnemy(e);
            } else {
                p.takeDamage(ENEMY_DASH_DMG);
                camera_.addShake(1.8f);
                if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                if (p.dead) {
                    if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    camera_.addShake(4.0f);
                    auto& net = NetworkManager::instance();
                    if (net.isInGame()) net.sendPlayerDied(net.localPlayerId(), 0);
                }
            }
        }
    }

    // ── PVP: Player bullets vs remote players (when friendlyFire/pvp is enabled) ──
    if (net.isInGame() && currentRules_.pvpEnabled) {
        auto& players = net.players();
        for (auto& b : bullets_) {
            if (!b.alive) continue;
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
                if (circleOverlap(b.pos, b.size, rpPos, PLAYER_SIZE * 0.5f)) {
                    b.alive = false;
                    // Visual feedback on shooter side — damage/kill is handled victim-side
                    // (the bullet is now in bullets_[] on the victim's machine and resolveCollisions
                    // there will call p.takeDamage() and send PlayerDied when HP reaches 0)
                    camera_.addShake(1.5f);
                    break;
                }
            }
        }

        // Remote player bullets vs LOCAL player
        if (!p.dead) {
            for (auto& b : bullets_) {
                if (!b.alive) continue;
                if (b.ownerId == net.localPlayerId() || b.ownerId == 255) continue; // skip own / unowned
                // Team check
                if (currentRules_.teamCount >= 2 && !currentRules_.friendlyFire) {
                    NetPlayer* shooter = net.findPlayer(b.ownerId);
                    if (shooter && shooter->team == localTeam_ && localTeam_ >= 0) continue;
                }
                if (circleOverlap(b.pos, b.size, p.pos, PLAYER_SIZE * 0.5f)) {
                    b.alive = false;
                    if (net.isHost()) {
                        // Host processes damage directly and broadcasts to all
                        NetPlayer* localNetP = net.localPlayer();
                        int newHp = player_.hp - b.damage;
                        if (localNetP) localNetP->hp = std::max(0, newHp);
                        if (newHp <= 0) {
                            p.takeDamage(b.damage);  // sets dead flag
                            camera_.addShake(4.0f);
                            if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                            net.sendPlayerDied(net.localPlayerId(), b.ownerId);
                        } else {
                            p.takeDamage(b.damage);
                            camera_.addShake(2.0f);
                            if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                            net.sendPlayerHpSync(net.localPlayerId(), player_.hp, player_.maxHp, b.ownerId);
                        }
                    } else {
                        // Client: report hit to host for validation — host will send back HP/death
                        net.sendHitRequest(b.netId, b.damage, b.ownerId);
                        // Optimistic visual feedback only (no HP deducted yet)
                        camera_.addShake(1.5f);
                        if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
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
                spawnExplosion(b.pos);
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
                if (localTeam_ >= 0 && rp.team == localTeam_) continue;
                if (circleOverlap(b.pos, BOMB_SIZE, rp.targetPos, 18.0f)) {
                    spawnExplosion(b.pos);
                    b.alive = false;
                    break;
                }
            }
        }
    }
}

void Game::killEnemy(Enemy& e) {
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
    int numGibs = 22 + rand() % 12;
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
    int numExtra = 4 + rand() % 4;
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

    // Brief red flash
    screenFlashTimer_ = 0.05f;
    screenFlashR_ = 180; screenFlashG_ = 30; screenFlashB_ = 30;

    // Player kill registration
    player_.killCounter++;
    if (player_.killCounter >= KILLS_PER_BOMB) {
        player_.killCounter = 0;
        player_.bombCount++;
    }
}

void Game::playerParry() {
    Player& p = player_;
    p.canParry = false;
    p.isParrying = true;
    p.parryTimer = PARRY_WINDOW;
    p.parryCdTimer = PARRY_COOLDOWN;

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
    screenFlashTimer_ = 0.06f;
    screenFlashR_ = 200; screenFlashG_ = 160; screenFlashB_ = 60;
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
        state_ == GameState::MultiplayerDead)
        SDL_ShowCursor(SDL_DISABLE);
    else
        SDL_ShowCursor(SDL_ENABLE);
#endif

    switch (state_) {
    case GameState::MainMenu:
        renderMainMenu();
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
            SDL_Texture* tex = (e.type == EnemyType::Shooter) ? shooterSprite_ : enemySprite_;
            float drawScale = (e.type == EnemyType::Shooter) ? SHOOTER_RENDER_SCALE : 3.0f;
            if (tex) {
                // Dash state (works both on host and clients via netIsDashing)
                bool showDash    = e.isDashing    || e.netIsDashing;
                bool showCharge  = e.dashCharging || e.netDashCharging;

                // Red ghost trail during dash
                if (showDash && e.type == EnemyType::Melee) {
                    Vec2 trailDir = (e.dashDir.lengthSq() > 0.001f) ? e.dashDir : Vec2{1,0};
                    for (int t = 1; t <= 4; t++) {
                        Vec2 trailPos = e.pos - trailDir * e.size * 0.65f * (float)t;
                        Uint8 alpha = (Uint8)std::max(0, 160 - t * 38);
                        SDL_Color trailCol = {255, 30, 30, alpha};
                        renderSpriteEx(tex, trailPos, e.rotation + M_PI/2,
                                       drawScale * (1.0f - t * 0.07f), trailCol);
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
                    SDL_Color tint = {255, 100, 100, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, tint);
                } else {
                    // Health-based tint
                    float hpRatio = e.hp / e.maxHp;
                    Uint8 r = 255;
                    Uint8 g = (Uint8)(255 * hpRatio);
                    Uint8 b = (Uint8)(255 * hpRatio);
                    SDL_Color tint = {r, g, b, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, tint);
                }
            }
        }

        // Player legs
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + M_PI/2, 3.0f);
        }

        // Player body
        if (!player_.dead) {
            if (!playerSprites_.empty()) {
                int idx = player_.animFrame % (int)playerSprites_.size();
                Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
                // Flash white when invulnerable
                if (player_.invulnerable && ((int)(player_.invulnTimer * 10) % 2 == 0)) {
                    SDL_Color tint = {255, 255, 255, 128};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + M_PI/2, 3.0f, tint);
                } else if (player_.isParrying) {
                    SDL_Color tint = {128, 200, 255, 255};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + M_PI/2, 3.0f, tint);
                } else {
                    renderSprite(playerSprites_[idx], bodyPos, player_.rotation + M_PI/2, 3.0f);
                }
            }
        } else {
            // Death animation
            if (!playerDeathSprites_.empty()) {
                int idx = player_.animFrame % (int)playerDeathSprites_.size();
                renderSprite(playerDeathSprites_[idx], player_.pos, player_.rotation + M_PI/2, 3.0f);
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

    case GameState::SpriteEditor:
        texEditor_.render();
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
            SDL_Texture* tex = (e.type == EnemyType::Shooter) ? shooterSprite_ : enemySprite_;
            float drawScale = (e.type == EnemyType::Shooter) ? SHOOTER_RENDER_SCALE : 3.0f;
            if (tex) {
                if (e.damageFlash > 0 || e.flashTimer > 0 || e.dashCharging) {
                    SDL_Color tint = {255, 100, 100, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, tint);
                } else {
                    float hpRatio = e.hp / e.maxHp;
                    Uint8 rr = 255, gg = (Uint8)(255 * hpRatio), bb = (Uint8)(255 * hpRatio);
                    SDL_Color tint = {rr, gg, bb, 255};
                    renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, tint);
                }
            }
        }
        // Player rendering (same as Playing)
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + (float)M_PI/2, 3.0f);
        }
        if (!player_.dead) {
            if (!playerSprites_.empty()) {
                int idx = player_.animFrame % (int)playerSprites_.size();
                Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
                if (player_.invulnerable && ((int)(player_.invulnTimer * 10) % 2 == 0)) {
                    SDL_Color tint = {255, 255, 255, 128};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f, tint);
                } else if (player_.isParrying) {
                    SDL_Color tint = {128, 200, 255, 255};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f, tint);
                } else {
                    renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f);
                }
            }
        } else if (!playerDeathSprites_.empty()) {
            int idx = player_.animFrame % (int)playerDeathSprites_.size();
            renderSprite(playerDeathSprites_[idx], player_.pos, player_.rotation + (float)M_PI/2, 3.0f);
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
            SDL_Texture* tex = (e.type == EnemyType::Shooter) ? shooterSprite_ : enemySprite_;
            float drawScale = (e.type == EnemyType::Shooter) ? SHOOTER_RENDER_SCALE : 3.0f;
            if (tex) renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, {255,255,255,255});
        }
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + (float)M_PI/2, 3.0f);
        }
        if (!player_.dead && !playerSprites_.empty()) {
            int idx = player_.animFrame % (int)playerSprites_.size();
            Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
            if (localTeam_ >= 0 && localTeam_ < 4 && currentRules_.teamCount >= 2) {
                static const SDL_Color ltTint[4] = {
                    {255,210,210,255},{210,220,255,255},{210,255,220,255},{255,250,210,255}
                };
                renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f, ltTint[localTeam_]);
            } else {
                renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f);
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
            SDL_Texture* tex = (e.type == EnemyType::Shooter) ? shooterSprite_ : enemySprite_;
            float drawScale = (e.type == EnemyType::Shooter) ? SHOOTER_RENDER_SCALE : 3.0f;
            if (tex) renderSpriteEx(tex, e.pos, e.rotation + (float)M_PI/2, drawScale, {255,255,255,255});
        }
        if (!player_.dead && !legSprites_.empty()) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            if (spectatorMode_) SDL_SetTextureAlphaMod(legSprites_[idx], 80);
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + (float)M_PI/2, 3.0f);
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
                renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f, ltTint[localTeam_]);
            } else {
                renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f);
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

        if (state_ == GameState::MultiplayerPaused) renderMultiplayerPause();
        if (state_ == GameState::MultiplayerDead)   renderMultiplayerDeath();
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

    case GameState::Scoreboard:
        renderScoreboard();
        break;

    case GameState::TeamSelect:
        renderTeamSelect();
        break;

    case GameState::ModMenu:
        renderModMenu();
        break;
    }

    // Mod-save dialog overlay — rendered on top of everything
    if (modSaveDialog_.isOpen())
        renderModSaveDialog();

    SDL_RenderPresent(renderer_);
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

void Game::renderExplosionPixelated(const Explosion& ex) {
    Vec2 sp = camera_.worldToScreen(ex.pos);

    float t = ex.duration > 0.0f ? (ex.timer / ex.duration) : 1.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    float visT = std::max(0.0f, std::min(1.0f, t * 1.28f + 0.02f));

    const int pixel = 4;
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
    int endX   = startX + SCREEN_W / TILE_SIZE + 3;
    int endY   = startY + SCREEN_H / TILE_SIZE + 3;

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
    int endX   = startX + SCREEN_W / TILE_SIZE + 3;
    int endY   = startY + SCREEN_H / TILE_SIZE + 3;

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
        if (bd.type == DecalType::Scorch) {
            SDL_SetTextureColorMod(bloodTex_, 20, 20, 20);
            SDL_SetTextureAlphaMod(bloodTex_, 160);
        } else {
            SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
            SDL_SetTextureAlphaMod(bloodTex_, 180);
        }
        if (!std::isfinite(bd.rotation) || !std::isfinite(half) || half <= 0.0f) {
            SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
            SDL_SetTextureAlphaMod(bloodTex_, 255);
            continue;
        }
        renderRotatedQuad(renderer_, bloodTex_, sp.x, sp.y, half, half, bd.rotation);
        SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
        SDL_SetTextureAlphaMod(bloodTex_, 255);
    }
}

void Game::renderRoofOverlay() {
    // Draw transparent glass ceiling tiles over rooms (rendered after entities)
    int startX = (int)(camera_.pos.x / TILE_SIZE) - 1;
    int startY = (int)(camera_.pos.y / TILE_SIZE) - 1;
    int endX   = startX + SCREEN_W / TILE_SIZE + 3;
    int endY   = startY + SCREEN_H / TILE_SIZE + 3;

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
    int endX   = startX + SCREEN_W / TILE_SIZE + 3;
    int endY   = startY + SCREEN_H / TILE_SIZE + 3;

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
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(renderer_, vignetteTex_, nullptr, &full);
    }
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

    // Ammo display
    {
        char ammoStr[32];
        if (player_.reloading) {
            snprintf(ammoStr, sizeof(ammoStr), "RELOAD");
        } else {
            snprintf(ammoStr, sizeof(ammoStr), "%d/%d", player_.ammo, player_.maxAmmo);
        }
        drawText(ammoStr, 20, 52, 20, white);
    }

    // Bomb count
    if (player_.bombCount > 0) {
        char bombStr[32];
        snprintf(bombStr, sizeof(bombStr), "BOMBS: %d", player_.bombCount);
        drawText(bombStr, 20, 80, 16, {255, 180, 50, 255});
    }

    // Active bombs indicator
    int activeBombs = 0;
    for (auto& b : bombs_) if (b.alive && !b.hasDashed) activeBombs++;
    if (activeBombs > 0) {
        //char str[32];
        //snprintf(str, sizeof(str), "BOMB x%d", activeBombs);
        //drawText(str, 20, 100, 16, {255, 200, 100, 255});
    }

    // Timer
    {
        char timeStr[32];
        int mins = (int)gameTime_ / 60;
        int secs = (int)gameTime_ % 60;
        snprintf(timeStr, sizeof(timeStr), "%d:%02d", mins, secs);
        drawText(timeStr, SCREEN_W - 100, 20, 20, white);
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
        snprintf(killStr, sizeof(killStr), "Kills: %d/%d", player_.killCounter, KILLS_PER_BOMB);
        drawText(killStr, 20, 120, 14, {180, 180, 180, 255});
    }

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
        screenFlashTimer_ -= dt_;
    }

    // ── Visual polish: muzzle flash glow ──
    if (muzzleFlashTimer_ > 0) {
        Vec2 sp = camera_.worldToScreen(muzzleFlashPos_);
        float flashAlpha = muzzleFlashTimer_ / 0.06f;
        int flashSize = (int)(8 + flashAlpha * 6);
        SDL_SetRenderDrawColor(renderer_, 255, 240, 150, (Uint8)(flashAlpha * 160));
        SDL_Rect fd = {(int)sp.x - flashSize/2, (int)sp.y - flashSize/2, flashSize, flashSize};
        SDL_RenderFillRect(renderer_, &fd);
        muzzleFlashTimer_ -= dt_;
    }

    // ── Pickup popup banner (name + description) ──
    if (pickupPopupTimer_ > 0) {
        float t = pickupPopupTimer_;
        pickupPopupTimer_ -= dt_;

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
        waveAnnounceTimer_ -= dt_;

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
        cratePopupTimer_ -= dt_;

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

#ifndef __SWITCH__
    // Gameplay crosshair (same style as editor cursor, but slightly smaller)
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    const int sz = 12;
    SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 200);
    SDL_RenderDrawLine(renderer_, mx - sz, my, mx + sz, my);
    SDL_RenderDrawLine(renderer_, mx, my - sz, mx, my + sz);
    SDL_Rect dot = {mx - 1, my - 1, 3, 3};
    SDL_RenderFillRect(renderer_, &dot);
#endif
}

void Game::renderMainMenu() {
    // Background
    if (mainmenuBg_) {
        SDL_Rect dst = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(renderer_, mainmenuBg_, nullptr, &dst);
    }

    // Dark overlay with gradient effect
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color dimCyan = {0, 140, 130, 255};

    // Title
    drawTextCentered("COLD START", SCREEN_H / 8, 52, cyan);

    // Version tag — shown under the title
    {
        char verBuf[32];
        snprintf(verBuf, sizeof(verBuf), "v%s", GAME_VERSION);
        drawTextCentered(verBuf, SCREEN_H / 8 + 58, 14, {0, 180, 160, 220});
    }

    // Subtitle line
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 80);
    SDL_Rect titleLine = {SCREEN_W / 2 - 120, SCREEN_H / 8 + 76, 240, 1};
    SDL_RenderFillRect(renderer_, &titleLine);

    // Menu items grouped visually
    struct MenuItem { const char* label; SDL_Color accent; };
    MenuItem items[] = {
        {"PLAY",             {50, 255, 150, 255}},   // 0 - green
        {"MULTIPLAYER",      {80, 200, 255, 255}},   // 1 - blue
        {"EDITOR",           {255, 220, 60, 255}},   // 2 - yellow
        {"SPRITE EDITOR",    {255, 160, 80, 255}},   // 3 - orange
        {"MAPS",             white},                   // 4
        {"PACKS",            white},                   // 5
        {"CHARACTER",        white},                   // 6
        {"CHARACTER EDITOR", white},                   // 7
        {"MODS",             {200, 140, 255, 255}},   // 8 - purple
        {"CONFIG",           white},                   // 9
        {"QUIT",             {255, 100, 100, 255}},   // 10 - red
    };
    int count = 11;
    int baseY = SCREEN_H / 4 + 10;
    int stepY = 32;

    for (int i = 0; i < count; i++) {
        bool sel = (menuSelection_ == i);
        SDL_Color c = sel ? items[i].accent : gray;

        // Selection indicator bar
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 25);
            SDL_Rect selBg = {SCREEN_W / 2 - 160, baseY + i * stepY - 4, 320, 30};
            SDL_RenderFillRect(renderer_, &selBg);
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 180);
            SDL_Rect selBar = {SCREEN_W / 2 - 160, baseY + i * stepY - 4, 3, 30};
            SDL_RenderFillRect(renderer_, &selBar);
        }

        char buf[64];
        if (sel)
            snprintf(buf, sizeof(buf), "  %s", items[i].label);
        else
            snprintf(buf, sizeof(buf), "%s", items[i].label);
        drawTextCentered(buf, baseY + i * stepY, sel ? 24 : 20, c);

        // Separator after logical groups
        if (i == 1 || i == 3 || i == 7 || i == 8) {
            SDL_SetRenderDrawColor(renderer_, 60, 60, 80, 60);
            SDL_Rect sep = {SCREEN_W / 2 - 80, baseY + i * stepY + 28, 160, 1};
            SDL_RenderFillRect(renderer_, &sep);
        }
    }

    // Show selected character name
    if (selectedChar_ >= 0 && selectedChar_ < (int)availableChars_.size()) {
        char charStr[128];
        snprintf(charStr, sizeof(charStr), "Character: %s", availableChars_[selectedChar_].name.c_str());
        drawTextCentered(charStr, SCREEN_H - 72, 14, dimCyan);
    }

    // Bottom hint
    drawTextCentered("A / ENTER - Select     D-Pad / Arrows - Navigate", SCREEN_H - 36, 13, {80, 80, 90, 255});
}

void Game::renderConfigMenu() {
    SDL_SetRenderDrawColor(renderer_, 8, 8, 12, 235);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color title = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray = {120, 120, 130, 255};
    SDL_Color cyan = {0, 200, 180, 255};

    drawTextCentered("CONFIG", 50, 36, title);

    // Decorative line
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 80, 92, 160, 1};
    SDL_RenderFillRect(renderer_, &tl);

    char line[128];
    int y = 120;
    int stepY = 46;

    auto drawConfigRow = [&](int idx, const char* label, const char* value) {
        bool sel = (configSelection_ == idx);
        SDL_Color c = sel ? white : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 220, y - 4, 440, 36};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 160);
            SDL_Rect bar = {SCREEN_W / 2 - 220, y - 4, 3, 36};
            SDL_RenderFillRect(renderer_, &bar);
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "%s  %s", label, value);
        drawTextCentered(buf, y + 4, sel ? 22 : 20, c);
        if (sel && idx < 9) {
            drawText("<", SCREEN_W / 2 - 240, y + 4, 20, cyan);
            drawText(">", SCREEN_W / 2 + 226, y + 4, 20, cyan);
        }
        y += stepY;
    };

    snprintf(line, sizeof(line), "%d", config_.mapWidth);
    drawConfigRow(0, "Map Width:", line);

    snprintf(line, sizeof(line), "%d", config_.mapHeight);
    drawConfigRow(1, "Map Height:", line);

    snprintf(line, sizeof(line), "%d", config_.playerMaxHp);
    drawConfigRow(2, "Player HP:", line);

    snprintf(line, sizeof(line), "%.1fx", config_.spawnRateScale);
    drawConfigRow(3, "Enemy Spawnrate:", line);

    snprintf(line, sizeof(line), "%.1fx", config_.enemyHpScale);
    drawConfigRow(4, "Enemy HP:", line);

    snprintf(line, sizeof(line), "%.1fx", config_.enemySpeedScale);
    drawConfigRow(5, "Enemy Speed:", line);

    snprintf(line, sizeof(line), "%d%%", config_.musicVolume * 100 / 128);
    drawConfigRow(6, "Music Volume:", line);

    snprintf(line, sizeof(line), "%d%%", config_.sfxVolume * 100 / 128);
    drawConfigRow(7, "SFX Volume:", line);

    // Username field
    {
        bool sel = (configSelection_ == 8);
        SDL_Color c = sel ? white : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 220, y - 4, 440, 36};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 160);
            SDL_Rect bar = {SCREEN_W / 2 - 220, y - 4, 3, 36};
            SDL_RenderFillRect(renderer_, &bar);
        }
        std::string uDisplay = config_.username;
        if (usernameTyping_) {
            static float blink = 0; blink += 0.016f;
            if ((int)(blink * 3) % 2 == 0) uDisplay += '_';
        }
        char ubuf[128];
        snprintf(ubuf, sizeof(ubuf), "Username:  %s", uDisplay.c_str());
        drawTextCentered(ubuf, y + 4, sel ? 22 : 20, usernameTyping_ ? cyan : c);
        if (sel && !usernameTyping_) drawText("[ENTER to edit]", SCREEN_W / 2 + 160, y + 6, 12, {80, 80, 100, 255});
        y += stepY;
    }

    // Back button
    {
        bool sel = (configSelection_ == 9);
        SDL_Color c = sel ? white : gray;
        drawTextCentered(sel ? "> BACK <" : "BACK", y + 10, 22, c);
    }

    drawTextCentered("UP/DOWN select  LEFT/RIGHT change  ENTER confirm", SCREEN_H - 40, 13, {80, 80, 90, 255});
}

void Game::renderPauseMenu() {
    // Dark overlay
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 200);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color red    = {255, 100, 100, 255};

    // Panel
    int panelW = 400;
#ifndef __SWITCH__
    int panelH = 370;
#else
    int panelH = 320;
#endif
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    SDL_SetRenderDrawColor(renderer_, 10, 12, 24, 240);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 80);
    SDL_RenderDrawRect(renderer_, &panel);

    drawTextCentered("PAUSED", py + 24, 36, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect sep = {px + 40, py + 68, panelW - 80, 1};
    SDL_RenderFillRect(renderer_, &sep);

    char musBuf[64]; snprintf(musBuf, sizeof(musBuf), "Music: %d%%", config_.musicVolume * 100 / 128);
    char sfxBuf[64]; snprintf(sfxBuf, sizeof(sfxBuf), "SFX: %d%%", config_.sfxVolume * 100 / 128);

    struct PauseItem { const char* label; SDL_Color accent; };
#ifndef __SWITCH__
    char fsBuf[32]; snprintf(fsBuf, sizeof(fsBuf), "Fullscreen: %s", config_.fullscreen ? "ON" : "OFF");
    PauseItem items[5];
    items[0] = {"RESUME", {50, 255, 150, 255}};
    items[1] = {musBuf, cyan};
    items[2] = {sfxBuf, cyan};
    items[3] = {fsBuf, {180, 180, 255, 255}};
    items[4] = {"MAIN MENU", red};
    int itemCount = 5;
#else
    PauseItem items[4];
    items[0] = {"RESUME", {50, 255, 150, 255}};
    items[1] = {musBuf, cyan};
    items[2] = {sfxBuf, cyan};
    items[3] = {"MAIN MENU", red};
    int itemCount = 4;
#endif

    int itemY = py + 90;
    int stepY = 50;
    for (int i = 0; i < itemCount; i++) {
        bool sel = (menuSelection_ == i);
        SDL_Color c = sel ? items[i].accent : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 20);
            SDL_Rect bg = {px + 20, itemY - 4, panelW - 40, 36};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 180);
            SDL_Rect bar = {px + 20, itemY - 4, 3, 36};
            SDL_RenderFillRect(renderer_, &bar);
        }
        if (sel && (i == 1 || i == 2)) {
            char tmp[80]; snprintf(tmp, sizeof(tmp), "< %s >", items[i].label);
            drawTextCentered(tmp, itemY + 4, 22, c);
#ifndef __SWITCH__
        } else if (sel && i == 3) {
            char tmp[80]; snprintf(tmp, sizeof(tmp), "< %s >", items[i].label);
            drawTextCentered(tmp, itemY + 4, 22, c);
#endif
        } else {
            drawTextCentered(items[i].label, itemY + 4, sel ? 22 : 20, c);
        }
        itemY += stepY;
    }

    drawTextCentered("LEFT/RIGHT adjust volume & fullscreen   A/ENTER confirm", py + panelH - 30, 12, {80, 80, 90, 255});
}

void Game::renderDeathScreen() {
    // Dark + red tint overlay
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 30, 4, 4, 200);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color red    = {255, 60, 60, 255};
    SDL_Color green  = {50, 255, 150, 255};

    // Panel
    int panelW = 380, panelH = 280;
    int px = (SCREEN_W - panelW) / 2;
    int py = (SCREEN_H - panelH) / 2;
    SDL_SetRenderDrawColor(renderer_, 12, 8, 10, 240);
    SDL_Rect panel = {px, py, panelW, panelH};
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 255, 60, 60, 60);
    SDL_RenderDrawRect(renderer_, &panel);

    drawTextCentered("YOU DIED", py + 28, 36, red);
    SDL_SetRenderDrawColor(renderer_, 255, 60, 60, 40);
    SDL_Rect sep = {px + 40, py + 72, panelW - 80, 1};
    SDL_RenderFillRect(renderer_, &sep);

    // Time survived
    char timeStr[64];
    int mins = (int)gameTime_ / 60;
    int secs = (int)gameTime_ % 60;
    snprintf(timeStr, sizeof(timeStr), "Time: %d:%02d", mins, secs);
    drawTextCentered(timeStr, py + 90, 18, gray);

    char waveBuf[64];
    snprintf(waveBuf, sizeof(waveBuf), "Wave: %d", waveNumber_);
    drawTextCentered(waveBuf, py + 116, 16, gray);

    // Buttons
    struct DeathItem { const char* label; SDL_Color accent; };
    DeathItem items[] = { {"RETRY", green}, {"MAIN MENU", {255, 100, 100, 255}} };
    int itemY = py + 160;
    for (int i = 0; i < 2; i++) {
        bool sel = (menuSelection_ == i);
        SDL_Color c = sel ? items[i].accent : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 20);
            SDL_Rect bg = {px + 20, itemY - 4, panelW - 40, 36};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 180);
            SDL_Rect bar = {px + 20, itemY - 4, 3, 36};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered(items[i].label, itemY + 4, sel ? 22 : 20, c);
        itemY += 48;
    }

    drawTextCentered("A / ENTER - Select", py + panelH - 26, 12, {80, 80, 90, 255});
}

void Game::drawText(const char* text, int x, int y, int size, SDL_Color color) {
    TTF_Font* f = Assets::instance().font(size);
    if (!f || !text || text[0] == '\0') return;

    SDL_Surface* surf = TTF_RenderText_Blended(f, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void Game::drawTextCentered(const char* text, int y, int size, SDL_Color color) {
    TTF_Font* f = Assets::instance().font(size);
    if (!f || !text || text[0] == '\0') return;

    SDL_Surface* surf = TTF_RenderText_Blended(f, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_Rect dst = {SCREEN_W / 2 - surf->w / 2, y, surf->w, surf->h};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
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

    state_ = GameState::PlayingCustom;
    playingCustomMap_ = true;
    customGoalOpen_ = false;
    gameTime_ = 0;

    // Apply custom map tiles to the game tilemap
    map_.width  = customMap_.width;
    map_.height = customMap_.height;
    map_.tiles  = customMap_.tiles;
    map_.ceiling = customMap_.ceiling;

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
            EnemyType type = (es.enemyType == 1) ? EnemyType::Shooter : EnemyType::Melee;
            spawnEnemy({es.x, es.y}, type);
            customEnemiesTotal_++;
        }
    }

    // Also generate spawn points for wave spawning if map has them
    map_.findSpawnPoints();

    if (bgMusic_) {
        Mix_PlayMusic(bgMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
    }
}

void Game::startCustomMapMultiplayer(const std::string& path) {
    if (!customMap_.loadFromFile(path)) {
        printf("Failed to load custom map for multiplayer: %s\n", path.c_str());
        return;
    }

    playingCustomMap_ = true;
    customGoalOpen_ = false;
    gameTime_ = 0;

    // Apply custom map tiles to the game tilemap
    map_.width  = customMap_.width;
    map_.height = customMap_.height;
    map_.tiles  = customMap_.tiles;
    map_.ceiling = customMap_.ceiling;

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

    // Spawn enemies from map data (host only)
    auto& net = NetworkManager::instance();
    customEnemiesTotal_ = 0;
    if (net.isHost()) {
        for (auto& es : customMap_.enemySpawns) {
            EnemyType type = (es.enemyType == 1) ? EnemyType::Shooter : EnemyType::Melee;
            spawnEnemy({es.x, es.y}, type);
            customEnemiesTotal_++;
        }
    }

    map_.findSpawnPoints();

    if (bgMusic_) {
        Mix_PlayMusic(bgMusic_, -1);
        Mix_VolumeMusic(config_.musicVolume);
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
    printf("Found %d map files\n", (int)mapFiles_.size());
}

void Game::scanCharacters() {
    for (auto& cd : availableChars_) cd.unload();
    availableChars_.clear();

    const char* dirs[] = {"characters", "romfs/characters", "romfs:/characters"};
    for (const char* dir : dirs) {
        auto found = ::scanCharacters(dir, renderer_);
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

    // Apply stats to config
    // (Stats applied when startGame/startCustomMap creates the player)
}

void Game::renderMapSelectMenu() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color yellow = {255, 220, 60, 255};

    drawTextCentered("SELECT MAP", SCREEN_H / 8, 36, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 100, SCREEN_H / 8 + 44, 200, 1};
    SDL_RenderFillRect(renderer_, &tl);

    if (mapFiles_.empty()) {
        drawTextCentered("No .csm maps found in maps/ folder", SCREEN_H / 2 - 10, 20, gray);
        drawTextCentered("Use the Editor to create maps!", SCREEN_H / 2 + 20, 14, {80, 80, 90, 255});
    } else {
        int baseY = SCREEN_H / 4 + 10;
        int stepY = 38;
        int maxVisible = (SCREEN_H - baseY - 120) / stepY;
        if (maxVisible < 3) maxVisible = 3;
        int scrollOff = std::max(0, menuSelection_ - maxVisible + 1);
        for (int i = scrollOff; i < (int)mapFiles_.size() && (i - scrollOff) < maxVisible; i++) {
            int y = baseY + (i - scrollOff) * stepY;
            bool sel = (menuSelection_ == i);
            SDL_Color c = sel ? yellow : gray;
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 20);
                SDL_Rect bg = {SCREEN_W / 2 - 200, y - 4, 400, 32};
                SDL_RenderFillRect(renderer_, &bg);
                SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 180);
                SDL_Rect bar = {SCREEN_W / 2 - 200, y - 4, 3, 32};
                SDL_RenderFillRect(renderer_, &bar);
            }
            std::string fname = mapFiles_[i];
            size_t slash = fname.find_last_of('/');
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            drawTextCentered(fname.c_str(), y + 2, sel ? 22 : 20, c);
        }
        // Scroll indicator
        if ((int)mapFiles_.size() > maxVisible) {
            float ratio = (float)maxVisible / mapFiles_.size();
            float scrollRatio = mapFiles_.size() > 1 ? (float)scrollOff / (mapFiles_.size() - maxVisible) : 0;
            int barH = std::max(20, (int)((SCREEN_H - baseY - 130) * ratio));
            int barY = baseY + (int)((SCREEN_H - baseY - 130 - barH) * scrollRatio);
            SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 60);
            SDL_Rect sb = {SCREEN_W - 60, barY, 4, barH};
            SDL_RenderFillRect(renderer_, &sb);
        }
    }

    int backIdx = (int)mapFiles_.size();
    bool backSel = (menuSelection_ == backIdx);
    SDL_Color cb = backSel ? white : gray;
    if (backSel) {
        SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
        SDL_Rect bg = {SCREEN_W / 2 - 80, SCREEN_H - 104, 160, 32};
        SDL_RenderFillRect(renderer_, &bg);
    }
    drawTextCentered("BACK", SCREEN_H - 100, backSel ? 22 : 20, cb);

    drawTextCentered("A / ENTER - Select     B / ESC - Back", SCREEN_H - 36, 13, {80, 80, 90, 255});
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
        SDL_Color c = sel ? items[i].accent : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 20);
            SDL_Rect bg = {px + 16, itemY - 4, panelW - 32, 42};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 180);
            SDL_Rect bar = {px + 16, itemY - 4, 3, 42};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered(items[i].label, itemY + 4, sel ? 22 : 20, c);
        if (sel && items[i].desc[0]) {
            drawTextCentered(items[i].desc, itemY + 28, 12, {80, 80, 90, 255});
        }
        itemY += stepY;
    }
}

void Game::renderCharSelectMenu() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color yellow = {255, 220, 60, 255};
    SDL_Color dimCyan= {0, 140, 130, 255};

    drawTextCentered("SELECT CHARACTER", SCREEN_H / 8, 36, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 120, SCREEN_H / 8 + 44, 240, 1};
    SDL_RenderFillRect(renderer_, &tl);

    int baseY = SCREEN_H / 4 + 10;
    int stepY = 42;

    // Default option
    {
        bool sel = (menuSelection_ == 0);
        SDL_Color c = sel ? yellow : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 180, baseY - 4, 360, 34};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 180);
            SDL_Rect bar = {SCREEN_W / 2 - 180, baseY - 4, 3, 34};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered("Default", baseY + 4, sel ? 22 : 20, c);
    }

    for (int i = 0; i < (int)availableChars_.size(); i++) {
        int y = baseY + (i + 1) * stepY;
        bool sel = (menuSelection_ == i + 1);
        SDL_Color c = sel ? yellow : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 180, y - 4, 360, 34};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 180);
            SDL_Rect bar = {SCREEN_W / 2 - 180, y - 4, 3, 34};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered(availableChars_[i].name.c_str(), y + 4, sel ? 22 : 20, c);

        // Show character detail sprite if selected
        if (sel && availableChars_[i].detailSprite) {
            SDL_Rect detDst = {SCREEN_W - 300, 120, 200, 400};
            SDL_RenderCopy(renderer_, availableChars_[i].detailSprite, nullptr, &detDst);
        }
        // Show stats
        if (sel) {
            char stats[128];
            snprintf(stats, sizeof(stats), "HP:%d  SPD:%.0f  AMMO:%d",
                availableChars_[i].hp, availableChars_[i].speed, availableChars_[i].ammo);
            drawTextCentered(stats, SCREEN_H - 100, 14, dimCyan);
        }
    }

    int backIdx = (int)availableChars_.size() + 1;
    bool backSel = (menuSelection_ == backIdx);
    int backY = baseY + backIdx * stepY;
    SDL_Color cb = backSel ? white : gray;
    if (backSel) {
        SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
        SDL_Rect bg = {SCREEN_W / 2 - 80, backY - 4, 160, 32};
        SDL_RenderFillRect(renderer_, &bg);
    }
    drawTextCentered("BACK", backY + 4, backSel ? 22 : 20, cb);

    if (availableChars_.empty()) {
        drawTextCentered("No .cschar found in characters/ folder", SCREEN_H / 2 + 40, 14, {80, 80, 90, 255});
    }
    drawTextCentered("A / ENTER - Select     B / ESC - Back", SCREEN_H - 36, 13, {80, 80, 90, 255});
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
    bool sel = true; // only one option
    SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 20);
    SDL_Rect bg = {px + 20, py + 140, panelW - 40, 36};
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 180);
    SDL_Rect bar = {px + 20, py + 140, 3, 36};
    SDL_RenderFillRect(renderer_, &bar);
    drawTextCentered("CONTINUE", py + 144, 22, white);

    drawTextCentered("A / ENTER - Continue", py + panelH - 22, 12, {80, 80, 90, 255});
}

// ═════════════════════════════════════════════════════════════════════════════
//  Character Creator
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderCharCreator() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color title = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {120, 120, 130, 255};
    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color yellow = {255, 220, 50, 255};

    drawTextCentered("CHARACTER CREATOR", 30, 36, title);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 120, 72, 240, 1};
    SDL_RenderFillRect(renderer_, &tl);

    auto& cc = charCreator_;
    int y = 90;
    int step = 38;
    char buf[256];

    auto drawField = [&](int idx, const char* label, const char* value) {
        SDL_Color c = (cc.field == idx) ? white : gray;
        SDL_Color vc = (cc.field == idx) ? cyan : gray;
        drawText(label, 120, y, 20, c);
        if (cc.field == idx && idx >= 1 && idx <= 8) {
            // Draw left/right arrows
            drawText("<", 520, y, 20, yellow);
            drawText(value, 560, y, 20, vc);
            drawText(">", 760, y, 20, yellow);
        } else {
            drawText(value, 560, y, 20, vc);
        }
        y += step;
    };

    // Field 0: Name
    if (cc.textEditing) {
        drawText("NAME:", 120, y, 20, yellow);
        std::string display = cc.textBuf + "_";
        drawText(display.c_str(), 560, y, 20, yellow);
        // Show char palette row
        static const char ccCharPal[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.";
        int pi = cc.gpCharIdx;
        int palLen = (int)strlen(ccCharPal);
        char charRow[48];
        int cx = 0;
        for (int d = -5; d <= 5; d++) {
            int idx = (pi + d + palLen) % palLen;
            charRow[cx++] = (d == 0) ? '[' : ' ';
            charRow[cx++] = ccCharPal[idx];
            charRow[cx++] = (d == 0) ? ']' : ' ';
        }
        charRow[cx] = 0;
        drawText(charRow, 120, y + 24, 16, cyan);
        drawText("ENTER/X confirm  ESC/B cancel  DPad:select char  A:type  Y:delete", 120, y + 44, 12, gray);
    } else {
        SDL_Color nc = (cc.field == 0) ? white : gray;
        drawText("NAME:", 120, y, 20, nc);
        drawText(cc.name.c_str(), 560, y, 20, (cc.field == 0) ? cyan : gray);
        if (cc.field == 0) drawText("[ENTER to edit]", 800, y, 14, gray);
    }
    y += step;

    // Fields 1-8: stats
    snprintf(buf, sizeof(buf), "%.0f", cc.speed);
    drawField(1, "SPEED:", buf);

    snprintf(buf, sizeof(buf), "%d", cc.hp);
    drawField(2, "HP:", buf);

    snprintf(buf, sizeof(buf), "%d", cc.ammo);
    drawField(3, "AMMO:", buf);

    snprintf(buf, sizeof(buf), "%.1f", cc.fireRate);
    drawField(4, "FIRE RATE:", buf);

    snprintf(buf, sizeof(buf), "%.1f", cc.reloadTime);
    drawField(5, "RELOAD TIME:", buf);

    snprintf(buf, sizeof(buf), "%d", cc.bodyFrames);
    drawField(6, "BODY FRAMES:", buf);

    snprintf(buf, sizeof(buf), "%d", cc.legFrames);
    drawField(7, "LEG FRAMES:", buf);

    snprintf(buf, sizeof(buf), "%d", cc.deathFrames);
    drawField(8, "DEATH FRAMES:", buf);

    // Buttons
    y += 10;
    SDL_Color saveC = (cc.field == 9) ? white : gray;
    drawTextCentered(cc.field == 9 ? "> SAVE <" : "SAVE", y, 24, saveC);
    y += step;

    SDL_Color backC = (cc.field == 10) ? white : gray;
    drawTextCentered(cc.field == 10 ? "> BACK <" : "BACK", y, 24, backC);

    // Info panel on the right
    int panelX = 880;
    drawText("FOLDER STRUCTURE:", panelX, 90, 14, title);
    snprintf(buf, sizeof(buf), "characters/%s/", cc.name.c_str());
    drawText(buf, panelX, 115, 12, gray);

    drawText("Required sprites:", panelX, 145, 14, title);
    snprintf(buf, sizeof(buf), "body-0001.png .. body-%04d.png", cc.bodyFrames);
    drawText(buf, panelX, 170, 12, gray);
    snprintf(buf, sizeof(buf), "legs-0001.png .. legs-%04d.png", cc.legFrames);
    drawText(buf, panelX, 190, 12, gray);
    snprintf(buf, sizeof(buf), "death-1.png .. death-%d.png", cc.deathFrames);
    drawText(buf, panelX, 210, 12, gray);
    drawText("detail.png (optional)", panelX, 230, 12, gray);

    drawText("All sprites should be 32x32 px", panelX, 265, 12, {180, 180, 180, 255});

    // Hint
    drawTextCentered("UP/DOWN navigate  LEFT/RIGHT adjust  ENTER confirm  BACKSPACE back", SCREEN_H - 30, 14, gray);
}

void Game::saveCharCreator() {
    auto& cc = charCreator_;

    // Sanitize name for folder (replace spaces with underscores)
    std::string safeName = cc.name;
    for (char& c : safeName) {
        if (c == ' ') c = '_';
        if (c == '/' || c == '\\') c = '_';
    }
    if (safeName.empty()) safeName = "unnamed";

    // Create character directory
    std::string dir = "characters/" + safeName;
    mkdir("characters", 0755);
    mkdir(dir.c_str(), 0755);

    // Also create in romfs for Switch
    std::string romfsDir = "romfs/characters/" + safeName;
    mkdir("romfs/characters", 0755);
    mkdir(romfsDir.c_str(), 0755);

    // Write .cschar file
    std::string path = dir + "/" + safeName + ".cschar";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        printf("Failed to save character to: %s\n", path.c_str());
        return;
    }

    fprintf(f, "[character]\n");
    fprintf(f, "name=%s\n", cc.name.c_str());
    fprintf(f, "speed=%.1f\n", cc.speed);
    fprintf(f, "hp=%d\n", cc.hp);
    fprintf(f, "ammo=%d\n", cc.ammo);
    fprintf(f, "fire_rate=%.1f\n", cc.fireRate);
    fprintf(f, "reload_time=%.1f\n", cc.reloadTime);
    fprintf(f, "\n[sprites]\n");
    fprintf(f, "body_frames=%d\n", cc.bodyFrames);
    fprintf(f, "leg_frames=%d\n", cc.legFrames);
    fprintf(f, "death_frames=%d\n", cc.deathFrames);
    fprintf(f, "detail=detail.png\n");
    fclose(f);

    // Copy to romfs too
    std::string romfsPath = romfsDir + "/" + safeName + ".cschar";
    FILE* f2 = fopen(romfsPath.c_str(), "w");
    if (f2) {
        fprintf(f2, "[character]\n");
        fprintf(f2, "name=%s\n", cc.name.c_str());
        fprintf(f2, "speed=%.1f\n", cc.speed);
        fprintf(f2, "hp=%d\n", cc.hp);
        fprintf(f2, "ammo=%d\n", cc.ammo);
        fprintf(f2, "fire_rate=%.1f\n", cc.fireRate);
        fprintf(f2, "reload_time=%.1f\n", cc.reloadTime);
        fprintf(f2, "\n[sprites]\n");
        fprintf(f2, "body_frames=%d\n", cc.bodyFrames);
        fprintf(f2, "leg_frames=%d\n", cc.legFrames);
        fprintf(f2, "death_frames=%d\n", cc.deathFrames);
        fprintf(f2, "detail=detail.png\n");
        fclose(f2);
    }

    printf("Character saved: %s -> %s\n", cc.name.c_str(), path.c_str());
    printf("Place sprites in: %s/\n", dir.c_str());
}

// ═════════════════════════════════════════════════════════════════════════════
//  Mod-save dialog helpers
// ═════════════════════════════════════════════════════════════════════════════

// Create standard folder tree for a new mod and write mod.cfg.
// Returns the base mod folder path (e.g. "mods/mymod").
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

void Game::saveCharCreatorToMod(const std::string& modFolder) {
    auto& cc = charCreator_;
    std::string safeName = cc.name;
    for (char& c : safeName) {
        if (c == ' ') c = '_';
        if (c == '/' || c == '\\') c = '_';
    }
    if (safeName.empty()) safeName = "unnamed";

    // Create character directory inside the mod
    std::string dir = modFolder + "/characters/" + safeName;
    mkdir((modFolder + "/characters").c_str(), 0755);
    mkdir(dir.c_str(), 0755);

    // Helper lambda: write character .cschar to a given path
    auto writeChar = [&](const std::string& path) {
        FILE* f = fopen(path.c_str(), "w");
        if (!f) { printf("Failed to save character to: %s\n", path.c_str()); return; }
        fprintf(f, "[character]\n");
        fprintf(f, "name=%s\n",            cc.name.c_str());
        fprintf(f, "speed=%.1f\n",         cc.speed);
        fprintf(f, "hp=%d\n",              cc.hp);
        fprintf(f, "ammo=%d\n",            cc.ammo);
        fprintf(f, "fire_rate=%.1f\n",     cc.fireRate);
        fprintf(f, "reload_time=%.1f\n",   cc.reloadTime);
        fprintf(f, "\n[sprites]\n");
        fprintf(f, "body_frames=%d\n",     cc.bodyFrames);
        fprintf(f, "leg_frames=%d\n",      cc.legFrames);
        fprintf(f, "death_frames=%d\n",    cc.deathFrames);
        fprintf(f, "detail=detail.png\n");
        fclose(f);
    };

    std::string path = dir + "/" + safeName + ".cschar";
    writeChar(path);
    printf("Character saved to mod: %s\n", path.c_str());
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
#ifndef __SWITCH__
    if (d.phase == ModSaveDialogState::ChooseMod ||
        d.phase == ModSaveDialogState::NameNewMod) {
        SDL_StartTextInput();
    }
#endif
}

void Game::handleModSaveDialogEvent(const SDL_Event& e) {
    auto& d = modSaveDialog_;
    // ── Alphabet for new-mod-name input ──
    static const char modNamePal[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    const int palLen = (int)strlen(modNamePal);
    // Total entries in ChooseMod list = "New Mod" + existing mods
    int total = 1 + (int)d.modIds.size();

    if (d.phase == ModSaveDialogState::ChooseMod) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:    d.selIdx = (d.selIdx - 1 + total) % total; break;
                case SDLK_DOWN:  d.selIdx = (d.selIdx + 1) % total; break;
                case SDLK_RETURN:
                    if (d.selIdx == 0) {
                        d.phase = ModSaveDialogState::NameNewMod;
                    } else {
                        // Existing mod selected
                        d.confirmedModFolder = "mods/" + d.modIds[d.selIdx - 1];
                        if (d.asset == ModSaveDialogState::AssetSprite)
                            d.phase = ModSaveDialogState::ChooseCategory;
                        else
                            d.confirmed = true;
                    }
                    break;
                case SDLK_ESCAPE: d.close(); break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    d.selIdx = (d.selIdx - 1 + total) % total; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    d.selIdx = (d.selIdx + 1) % total; break;
                case SDL_CONTROLLER_BUTTON_A:
                    if (d.selIdx == 0) {
                        d.phase = ModSaveDialogState::NameNewMod;
                    } else {
                        d.confirmedModFolder = "mods/" + d.modIds[d.selIdx - 1];
                        if (d.asset == ModSaveDialogState::AssetSprite)
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
        if (d.textEditing) {
            // Gamepad char picker held-repeat
        }
        if (e.type == SDL_TEXTINPUT) {
            for (const char* p = e.text.text; *p; p++) {
                char c = *p;
                // Only allow alphanumeric, dash, underscore — lowercase it
                if (c >= 'A' && c <= 'Z') c += 32;
                bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
                if (ok && d.newModId.size() < 24) d.newModId += c;
            }
        }
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_BACKSPACE:
                    if (!d.newModId.empty()) d.newModId.pop_back(); break;
                case SDLK_RETURN: {
                    if (d.newModId.empty()) break;
                    d.confirmedModFolder = modBuildFolder(d.newModId, d.newModId);
                    if (d.asset == ModSaveDialogState::AssetSprite)
                        d.phase = ModSaveDialogState::ChooseCategory;
                    else
                        d.confirmed = true;
                    break;
                }
                case SDLK_ESCAPE:
                    d.phase = ModSaveDialogState::ChooseMod; break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    d.gpCharIdx = (d.gpCharIdx - 1 + palLen) % palLen; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    d.gpCharIdx = (d.gpCharIdx + 1) % palLen; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    d.gpCharIdx = (d.gpCharIdx - 8 + palLen) % palLen; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    d.gpCharIdx = (d.gpCharIdx + 8) % palLen; break;
                case SDL_CONTROLLER_BUTTON_A:
                    if (d.newModId.size() < 24)
                        d.newModId += modNamePal[d.gpCharIdx];
                    break;
                case SDL_CONTROLLER_BUTTON_Y:
                    if (!d.newModId.empty()) d.newModId.pop_back(); break;
                case SDL_CONTROLLER_BUTTON_X:
                case SDL_CONTROLLER_BUTTON_START: {
                    if (d.newModId.empty()) break;
                    d.confirmedModFolder = modBuildFolder(d.newModId, d.newModId);
                    if (d.asset == ModSaveDialogState::AssetSprite)
                        d.phase = ModSaveDialogState::ChooseCategory;
                    else
                        d.confirmed = true;
                    break;
                }
                case SDL_CONTROLLER_BUTTON_B:
                    d.phase = ModSaveDialogState::ChooseMod; break;
            }
        }
    }
    else if (d.phase == ModSaveDialogState::ChooseCategory) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:   d.catIdx = (d.catIdx - 1 + ModSaveDialogState::CAT_COUNT) % ModSaveDialogState::CAT_COUNT; break;
                case SDLK_DOWN: d.catIdx = (d.catIdx + 1) % ModSaveDialogState::CAT_COUNT; break;
                case SDLK_RETURN:
                    d.confirmedCat = d.catIdx;
                    d.confirmed    = true;
                    break;
                case SDLK_ESCAPE:
                    d.phase = ModSaveDialogState::ChooseMod; break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e.cbutton.button) {
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
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color cyan   = {0,   220, 200, 255};
    SDL_Color yellow = {255, 220,  50, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color selBg  = {0,   140, 120, 255};

    // Dim overlay
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    // Panel
    int panW = 640, panH = 420;
    int panX = (SCREEN_W - panW) / 2;
    int panY = (SCREEN_H - panH) / 2;
    SDL_SetRenderDrawColor(renderer_, 10, 12, 24, 240);
    SDL_Rect pan = {panX, panY, panW, panH};
    SDL_RenderFillRect(renderer_, &pan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 200);
    SDL_Rect border = {panX, panY, panW, 2};
    SDL_RenderFillRect(renderer_, &border);
    border = {panX, panY + panH - 2, panW, 2};
    SDL_RenderFillRect(renderer_, &border);

    int cx = panX + panW / 2;
    int y  = panY + 18;

    static const char* ASSET_NAMES[] = {"MAP", "SPRITE", "CHARACTER"};
    char title[64];
    snprintf(title, sizeof(title), "SAVE %s TO MOD", ASSET_NAMES[(int)d.asset]);
    drawTextCentered(title, y, 24, cyan);
    y += 38;

    // ── Phase: choose mod ──────────────────────────────────────────────
    if (d.phase == ModSaveDialogState::ChooseMod) {
        drawTextCentered("Choose or create a mod to save into:", y, 14, gray);
        y += 26;

        int total = 1 + (int)d.modIds.size();
        for (int i = 0; i < total; i++) {
            bool sel = (i == d.selIdx);
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, selBg.r, selBg.g, selBg.b, selBg.a);
                SDL_Rect bg = {panX + 20, y - 2, panW - 40, 26};
                SDL_RenderFillRect(renderer_, &bg);
            }
            if (i == 0) {
                drawTextCentered("[ + New Mod ]", y + 3, sel ? 18 : 16, sel ? yellow : gray);
            } else {
                char buf[80];
                snprintf(buf, sizeof(buf), "%s  (%s)", d.modNames[i-1].c_str(), d.modIds[i-1].c_str());
                drawTextCentered(buf, y + 3, sel ? 18 : 16, sel ? white : gray);
            }
            y += 30;
        }

        y = panY + panH - 40;
        drawTextCentered("\xe2\x86\x91\xe2\x86\x93 navigate   A/Enter confirm   B/Esc cancel", y, 12, gray);
    }
    // ── Phase: name new mod ────────────────────────────────────────────
    else if (d.phase == ModSaveDialogState::NameNewMod) {
        drawTextCentered("Type a mod ID (letters, digits, _ -)", y, 14, gray);
        y += 34;

        // Text box
        std::string display = d.newModId + "_";
        int bx = panX + 80, bw = panW - 160, bh = 36;
        SDL_SetRenderDrawColor(renderer_, 20, 22, 40, 255);
        SDL_Rect box = {bx, y, bw, bh};
        SDL_RenderFillRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 255);
        SDL_Rect boxBorder = {bx, y + bh - 2, bw, 2};
        SDL_RenderFillRect(renderer_, &boxBorder);
        drawText(display.c_str(), bx + 8, y + 8, 20, yellow);
        y += bh + 16;

        // Gamepad char picker
        static const char palStr[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
        const int pl = (int)strlen(palStr);
        char row[120]; int rx = 0;
        for (int dd = -6; dd <= 6; dd++) {
            int idx = (d.gpCharIdx + dd + pl) % pl;
            row[rx++] = (dd == 0) ? '[' : ' ';
            row[rx++] = palStr[idx];
            row[rx++] = (dd == 0) ? ']' : ' ';
        }
        row[rx] = 0;
        drawTextCentered(row, y, 16, cyan);
        y += 30;

        y = panY + panH - 40;
        drawTextCentered("Type mod name   X/START confirm   A append char   Y delete   B back", y, 11, gray);
    }
    // ── Phase: choose sprite category ─────────────────────────────────
    else if (d.phase == ModSaveDialogState::ChooseCategory) {
        drawTextCentered("Where should this sprite go?", y, 14, gray);
        y += 28;

        for (int i = 0; i < ModSaveDialogState::CAT_COUNT; i++) {
            bool sel = (i == d.catIdx);
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, selBg.r, selBg.g, selBg.b, selBg.a);
                SDL_Rect bg = {panX + 20, y - 2, panW - 40, 26};
                SDL_RenderFillRect(renderer_, &bg);
            }
            drawTextCentered(ModSaveDialogState::CAT_NAMES[i], y + 3, sel ? 17 : 15, sel ? white : gray);
            y += 30;
        }

        y = panY + panH - 40;
        drawTextCentered("\xe2\x86\x91\xe2\x86\x93 navigate   A/Enter confirm   B back", y, 12, gray);
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
    fprintf(f, "playerMaxHp=%d\n", config_.playerMaxHp);
    fprintf(f, "spawnRateScale=%.2f\n", config_.spawnRateScale);
    fprintf(f, "enemyHpScale=%.2f\n", config_.enemyHpScale);
    fprintf(f, "enemySpeedScale=%.2f\n", config_.enemySpeedScale);
    fprintf(f, "musicVolume=%d\n", config_.musicVolume);
    fprintf(f, "sfxVolume=%d\n", config_.sfxVolume);
    fprintf(f, "username=%s\n", config_.username.c_str());
    fprintf(f, "fullscreen=%d\n", config_.fullscreen ? 1 : 0);
    fclose(f);
    printf("Config saved to config.txt\n");
}

void Game::loadConfig() {
    FILE* f = fopen("config.txt", "r");
    if (!f) return; // no saved config, use defaults
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
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
    }
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
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char name[128], gmId[128];
        int maxP = 8, port = 7777, mapIdx = 0;
        if (sscanf(line, "%127[^|]|%127[^|]|%d|%d|%d", name, gmId, &maxP, &port, &mapIdx) >= 2) {
            ServerPreset p;
            p.name = name;
            p.gamemodeId = gmId;
            p.maxPlayers = maxP;
            p.hostPort = port;
            p.mapIndex = mapIdx;
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
        fprintf(f, "%s|%s|%d|%d|%d\n", p.name.c_str(), p.gamemodeId.c_str(),
                p.maxPlayers, p.hostPort, p.mapIndex);
    }
    fclose(f);
    printf("Saved %d presets\n", (int)serverPresets_.size());
}

void Game::addServerPreset(const std::string& name, const std::string& gamemodeId, int maxPlayers, int port, int mapIdx) {
    ServerPreset p;
    p.name = name;
    p.gamemodeId = gamemodeId;
    p.maxPlayers = maxPlayers;
    p.hostPort = port;
    p.mapIndex = mapIdx;
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
    hostPort_ = p.hostPort;
    hostMapSelectIdx_ = p.mapIndex;
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
        SDL_Color c = sel ? blue : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 80, 200, 255, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 260, y - 6, 520, 42};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 80, 200, 255, 180);
            SDL_Rect bar = {SCREEN_W / 2 - 260, y - 6, 3, 42};
            SDL_RenderFillRect(renderer_, &bar);
        }
        std::string label = availablePacks_[i].name;
        if (!availablePacks_[i].creator.empty()) label += " by " + availablePacks_[i].creator;
        label += " (" + std::to_string(availablePacks_[i].maps.size()) + " levels)";
        drawTextCentered(label.c_str(), y + 4, sel ? 20 : 18, c);

        if (sel && !availablePacks_[i].description.empty()) {
            drawTextCentered(availablePacks_[i].description.c_str(), y + 28, 12, {80, 80, 90, 255});
        }
    }

    // BACK option
    int backIdx = (int)availablePacks_.size();
    bool backSel = (packSelectIdx_ == backIdx);
    int backY = baseY + backIdx * stepY + 10;
    SDL_Color backC = backSel ? white : gray;
    if (backSel) {
        SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
        SDL_Rect bg = {SCREEN_W / 2 - 80, backY - 4, 160, 32};
        SDL_RenderFillRect(renderer_, &bg);
    }
    drawTextCentered("BACK", backY + 2, backSel ? 22 : 20, backC);

    drawTextCentered("A / ENTER - Select     B / ESC - Back", SCREEN_H - 36, 13, {80, 80, 90, 255});
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
        EnemyType type = (es.enemyType == 1) ? EnemyType::Shooter : EnemyType::Melee;
        spawnEnemy({es.x, es.y}, type);
        customEnemiesTotal_++;
    }
    map_.findSpawnPoints();

    if (bgMusic_) { Mix_PlayMusic(bgMusic_, -1); Mix_VolumeMusic(config_.musicVolume); }

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
        SDL_Color c = sel ? items[i].accent : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 20);
            SDL_Rect bg = {px + 20, itemY - 4, panelW - 40, 36};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 180);
            SDL_Rect bar = {px + 20, itemY - 4, 3, 36};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered(items[i].label, itemY + 4, sel ? 22 : 20, c);
        itemY += 48;
    }

    drawTextCentered("A / ENTER - Select", py + panelH - 26, 12, {80, 80, 90, 255});
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
    SDL_SetRenderDrawColor(renderer_, 255, 200, 50, 20);
    SDL_Rect bg = {px + 30, py + 200, panelW - 60, 36};
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, 255, 200, 50, 180);
    SDL_Rect bar = {px + 30, py + 200, 3, 36};
    SDL_RenderFillRect(renderer_, &bar);
    drawTextCentered("CONTINUE", py + 204, 22, white);

    drawTextCentered("A / ENTER - Continue", py + panelH - 22, 12, {80, 80, 90, 255});

}

// ═════════════════════════════════════════════════════════════════════════════
//  Mod System Integration
// ═════════════════════════════════════════════════════════════════════════════

void Game::initMods() {
    auto& mm = ModManager::instance();
    mm.loadModConfig();
    mm.scanMods();
    mm.loadAllEnabled();
    applyModOverrides();
    printf("Mods initialized (%d loaded)\n", (int)mm.mods().size());
}

void Game::applyModOverrides() {
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
    net.setUsername(config_.username);
    setupNetworkCallbacks();
    printf("Multiplayer initialized\n");
}

void Game::shutdownMultiplayer() {
    NetworkManager::instance().shutdown();
}

void Game::setupNetworkCallbacks() {
    auto& net = NetworkManager::instance();

    net.onPlayerJoined = [this](uint8_t id, const std::string& name) {
        printf("[NET] Player joined: %s (id=%d)\n", name.c_str(), id);
        // Send current lobby settings to the new player (host only)
        auto& net2 = NetworkManager::instance();
        if (net2.isHost()) {
            net2.sendConfigSync(lobbySettings_);
        }
    };

    net.onPlayerLeft = [this](uint8_t id) {
        printf("[NET] Player left (id=%d)\n", id);
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

    net.onPlayerStateReceived = [this](const NetPlayer& state) {
        // Position/interpolation and all fields are now handled in network.cpp handlePacket.
        // This callback is kept for any game-side logic that needs to fire on state updates.
        (void)state;
    };

    net.onBulletSpawned = [this](Vec2 pos, float angle, uint8_t playerId, uint32_t netId) {
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
    net.onEnemyKilled = [this](uint32_t enemyIdx, uint8_t /*killerId*/) {
        // Kill the enemy locally (idempotent — safe if already dead)
        if (enemyIdx < enemies_.size() && enemies_[enemyIdx].alive) {
            killEnemy(enemies_[enemyIdx]);
        }
    };

    // Host broadcast a config change (e.g. lobby settings update)
    net.onConfigSyncReceived = [this](const LobbySettings& settings) {
        lobbySettings_ = settings;
        currentRules_.pvpEnabled = settings.pvpEnabled;
        currentRules_.friendlyFire = settings.friendlyFire;
        currentRules_.upgradesShared = settings.upgradesShared;
        currentRules_.teamCount = settings.teamCount;
        printf("Game: Config sync received — pvp=%d ff=%d shared=%d teams=%d\n",
               (int)settings.pvpEnabled, (int)settings.friendlyFire,
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

    net.onBombSpawned = [this](Vec2 pos, Vec2 vel, uint8_t playerId) {
        if (playerId != NetworkManager::instance().localPlayerId()) {
            Bomb bomb;
            bomb.pos = pos;
            bomb.vel = vel;
            bomb.alive = true;
            bomb.hasDashed = true;   // network bombs are always launched
            bomb.animFrame = 0;
            bombs_.push_back(bomb);
        }
    };

    net.onExplosionSpawned = [this](Vec2 pos) {
        suppressNetExplosion_ = true;
        spawnExplosion(pos);
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
        // Individual mode: only the picker gets the upgrade (they already applied it in collectPickup)
        // Shared mode: everyone applies when ANY player picks it up
        if (isLocalPick || currentRules_.upgradesShared) {
            applyUpgrade((UpgradeType)upgradeType);
        }
    };

    net.onPlayerDied = [this](uint8_t playerId, uint8_t killerId) {
        auto& net = NetworkManager::instance();
        NetPlayer* victim = net.findPlayer(playerId);
        if (victim) victim->alive = false;
        // If WE are the one who died, actually kill the local player
        if (playerId == net.localPlayerId() && !player_.dead) {
            player_.dead = true;
            player_.hp = 0;
            if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
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
                        net.sendGameEnd();
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
    net.onHitRequest = [this](uint32_t bulletNetId, int damage, uint8_t ownerId, uint8_t senderPlayerId) -> bool {
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
        (void)bulletFound;
        return true;
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
                if (sfxHurt_)  { int ch = Mix_PlayChannel(-1, sfxHurt_,  0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                if (sfxDeath_) { int ch = Mix_PlayChannel(-1, sfxDeath_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                camera_.addShake(4.0f);
            } else if (hp < player_.maxHp) {
                camera_.addShake(1.8f);
                if (sfxHurt_) { int ch = Mix_PlayChannel(-1, sfxHurt_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
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
#ifndef __SWITCH__
        SDL_StopTextInput();
#endif
        // Apply lobby settings received from host
        config_.playerMaxHp    = lobbySettings_.playerMaxHp;
        config_.spawnRateScale = lobbySettings_.spawnRateScale;
        config_.enemyHpScale   = lobbySettings_.enemyHpScale;
        config_.enemySpeedScale= lobbySettings_.enemySpeedScale;
        currentRules_.friendlyFire  = lobbySettings_.friendlyFire;
        currentRules_.pvpEnabled    = lobbySettings_.pvpEnabled;
        currentRules_.upgradesShared= lobbySettings_.upgradesShared;
        currentRules_.teamCount     = lobbySettings_.teamCount;
        currentRules_.lives         = lobbySettings_.livesPerPlayer;
        currentRules_.sharedLives   = lobbySettings_.livesShared;
        player_.maxHp = config_.playerMaxHp;
        player_.hp    = player_.maxHp;

        // Initialise lives tracking (client side)
        spectatorMode_ = false;
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
                startGame();                // generates map, resets player & camera
            }
        }
        // PvP: no damage cooldown so rapid hits always register
        // Set AFTER startGame/startCustomMapMultiplayer which reset the Player struct
        player_.invulnDuration = lobbySettings_.isPvp ? 0.0f : PLAYER_INVULN_TIME;
        state_ = GameState::MultiplayerGame;
        menuSelection_ = 0;
        respawnTimer_ = currentRules_.respawnTime;
    };

    net.onGameEnded = [this]() {
        state_ = GameState::Lobby;
        menuSelection_ = 0;
        spectatorMode_ = false;
    };

    net.onModSyncReceived = [this](const std::vector<uint8_t>& modData) {
        // Client received mod data from host — install and apply
        printf("Game: Received mod sync from host, installing...\n");
        auto& mm = ModManager::instance();
        mm.deserializeAndInstallMods(modData);
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
}

void Game::updateMultiplayer(float dt) {
    auto& net = NetworkManager::instance();
    if (!net.isOnline()) {
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

    // NOTE: net.update(dt) is already called in run() every frame — do NOT call again here

    // Send local player state at fixed rate
    if (net.isInGame()) {
        netStateSendTimer_ -= dt;
        if (netStateSendTimer_ <= 0) {
            netStateSendTimer_ = 1.0f / 30.0f; // 30 Hz

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
        }

        // Host sends enemy states at 20 Hz, or immediately when an enemy dies
        if (net.isHost()) {
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
        if (player_.dead && currentRules_.respawnTime > 0) {
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
    }
}

void Game::hostGame() {
    auto& net = NetworkManager::instance();
    if (net.host(hostPort_, hostMaxPlayers_ - 1)) {
        state_ = GameState::Lobby;
        menuSelection_ = 0;
        lobbyReady_ = false;
        // Initialize lobby settings from current config
        lobbySettings_.mapWidth       = config_.mapWidth;
        lobbySettings_.mapHeight      = config_.mapHeight;
        lobbySettings_.playerMaxHp    = config_.playerMaxHp;
        lobbySettings_.spawnRateScale = config_.spawnRateScale;
        lobbySettings_.enemyHpScale   = config_.enemyHpScale;
        lobbySettings_.enemySpeedScale= config_.enemySpeedScale;
        lobbySettings_.friendlyFire   = currentRules_.friendlyFire;
        lobbySettings_.pvpEnabled     = currentRules_.pvpEnabled;
        lobbySettings_.upgradesShared = currentRules_.upgradesShared;
        lobbySettings_.teamCount      = currentRules_.teamCount;
        lobbySettings_.maxPlayers     = hostMaxPlayers_;
        lobbyGamemodeIdx_ = gamemodeSelectIdx_;
        lobbyMapIdx_      = hostMapSelectIdx_;
        lobbySettingsSel_ = 0;
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
    connectStatus_.clear();
    if (net.join(joinAddress_, joinPort_)) {
        state_ = GameState::Lobby;
        menuSelection_ = 0;
        lobbyReady_ = false;
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
    currentRules_.pvpEnabled    = lobbySettings_.pvpEnabled;
    currentRules_.upgradesShared= lobbySettings_.upgradesShared;
    currentRules_.teamCount     = lobbySettings_.teamCount;
    currentRules_.lives         = lobbySettings_.livesPerPlayer;
    currentRules_.sharedLives   = lobbySettings_.livesShared;
    player_.maxHp = config_.playerMaxHp;
    player_.hp    = player_.maxHp;

    // Initialise lives tracking
    spectatorMode_ = false;
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
            return;
        }
        // If file can't be read, fall through to generated map
        printf("Warning: custom map '%s' not found, using generated map\n", customMapFile.c_str());
    }

    // Generate a shared map seed so host and client produce the same map
    uint32_t mapSeed = (uint32_t)time(nullptr) ^ (uint32_t)rand();
    mapSrand(mapSeed);

    // Start the game — use generated map
    startGame();
    if (lobbySettings_.isPvp) player_.invulnDuration = 0.0f;
    state_ = GameState::MultiplayerGame;
    net.startGame(mapSeed, config_.mapWidth, config_.mapHeight);
    respawnTimer_ = currentRules_.respawnTime;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Pickup / Crate Update & Render
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateCrates(float dt) {
    // Auto-spawn crates on a timer (every ~20-30 seconds)
    // In multiplayer, only the host spawns crates — clients receive them via network
    auto& net = NetworkManager::instance();
    bool isMultiplayer = net.isOnline();
    bool shouldSpawn = !sandboxMode_ && (!isMultiplayer || net.isHost());

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
                    camera_.addShake(1.5f);
                    screenFlashTimer_ = 0.04f;
                    screenFlashR_ = 255; screenFlashG_ = 200; screenFlashB_ = 50;
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
        if (!player_.dead && !spectatorMode_ && circleOverlap(p.pos, collectRadius, player_.pos, PLAYER_SIZE * 0.5f)) {
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
    if (net.isHost() && net.isInGame()) {
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
        case UpgradeType::Shield:
            player_.invulnerable = true;
            player_.invulnTimer = 5.0f;
            break;
        case UpgradeType::BombPickup:
            player_.bombCount += 3;
            break;
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
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color blue   = {80, 200, 255, 255};
    SDL_Color dimCyan = {0, 140, 130, 255};

    drawTextCentered("MULTIPLAYER", SCREEN_H / 8, 38, cyan);

    // Decorative line
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 100, SCREEN_H / 8 + 48, 200, 1};
    SDL_RenderFillRect(renderer_, &tl);

    // ── Left column: Main actions ──
    int leftX = SCREEN_W / 4;
    int actionY = SCREEN_H / 4 + 20;

    struct MenuItem { const char* label; const char* desc; SDL_Color accent; };
    MenuItem items[] = {
        {"HOST GAME",       "Create a server for others to join", {50, 255, 150, 255}},
        {"IP CONNECT",   "Enter IP and connect directly",      blue},
        {"BACK",            "",                                    {255, 100, 100, 255}},
    };
    int count = 3;
    int stepY = 50;

    for (int i = 0; i < count; i++) {
        bool sel = (multiMenuSelection_ == i);
        SDL_Color c = sel ? items[i].accent : gray;
        int itemY = actionY + i * stepY;

        if (sel) {
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 20);
            SDL_Rect bg = {leftX - 140, itemY - 6, 280, 38};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, items[i].accent.r, items[i].accent.g, items[i].accent.b, 160);
            SDL_Rect bar = {leftX - 140, itemY - 6, 3, 38};
            SDL_RenderFillRect(renderer_, &bar);
        }

        // Center text in left column
        {
            TTF_Font* font = Assets::instance().font(sel ? 24 : 20);
            if (font) {
                SDL_Surface* surf = TTF_RenderText_Blended(font, items[i].label, c);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
                    SDL_Rect dst = {leftX - surf->w / 2, itemY, surf->w, surf->h};
                    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                    SDL_FreeSurface(surf);
                }
            }
        }
        if (sel && items[i].desc[0]) {
            TTF_Font* df = Assets::instance().font(11);
            if (df) {
                SDL_Surface* ds = TTF_RenderText_Blended(df, items[i].desc, {90, 90, 100, 255});
                if (ds) {
                    SDL_Texture* dt = SDL_CreateTextureFromSurface(renderer_, ds);
                    SDL_Rect dd = {leftX - ds->w / 2, itemY + 26, ds->w, ds->h};
                    SDL_RenderCopy(renderer_, dt, nullptr, &dd);
                    SDL_DestroyTexture(dt);
                    SDL_FreeSurface(ds);
                }
            }
        }
    }

    // ── Right column: Saved Servers ──
    int rightX = SCREEN_W * 3 / 4 - 100;
    int serverStartY = SCREEN_H / 4 - 10;

    // Panel background
    SDL_SetRenderDrawColor(renderer_, 12, 14, 26, 200);
    SDL_Rect serverPanel = {rightX - 20, serverStartY - 30, 350, SCREEN_H / 2 + 60};
    SDL_RenderFillRect(renderer_, &serverPanel);
    SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 60);
    SDL_RenderDrawRect(renderer_, &serverPanel);

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

            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 30);
                SDL_Rect row = {rightX - 8, sy - 4, 330, 32};
                SDL_RenderFillRect(renderer_, &row);
                SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 120);
                SDL_Rect acc = {rightX - 8, sy - 4, 2, 32};
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
        drawTextCentered("A - Connect    X - Delete    B - Back", SCREEN_H - 36, 13, {80, 80, 90, 255});
    } else {
        drawTextCentered("A / ENTER - Select     B / ESC - Back", SCREEN_H - 36, 13, {80, 80, 90, 255});
    }
}

void Game::renderHostSetup() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color green  = {50, 255, 100, 255};
    SDL_Color dimCyan = {0, 140, 130, 255};

    drawTextCentered("HOST SETUP", SCREEN_H / 8, 34, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 80, SCREEN_H / 8 + 42, 160, 1};
    SDL_RenderFillRect(renderer_, &tl);

    int y = SCREEN_H / 4;
    int step = 52;

    auto drawRow = [&](int idx, const char* label, const char* value, bool arrows = true) {
        bool sel = (hostSetupSelection_ == idx);
        SDL_Color c = sel ? white : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 240, y - 4, 480, 38};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 140);
            SDL_Rect bar = {SCREEN_W / 2 - 240, y - 4, 3, 38};
            SDL_RenderFillRect(renderer_, &bar);
        }
        char buf[256];
        if (sel && arrows)
            snprintf(buf, sizeof(buf), "%s  < %s >", label, value);
        else
            snprintf(buf, sizeof(buf), "%s  %s", label, value);
        drawTextCentered(buf, y + 4, sel ? 22 : 20, c);
        y += step;
    };

    // Max players (adjustable)
    {
        char mpBuf[16]; snprintf(mpBuf, sizeof(mpBuf), "%d", hostMaxPlayers_);
        drawRow(0, "Max Players:", mpBuf);
    }

    // Port (editable with keyboard)
    {
        std::string pDisp = portTyping_ ? portStr_ : std::to_string(hostPort_);
        if (portTyping_) {
            static float pBlink = 0; pBlink += 0.016f;
            if ((int)(pBlink * 3) % 2 == 0) pDisp += '_';
        }
        drawRow(1, "Port:", pDisp.c_str(), false);
        if (portTyping_) {
            // Digit palette
            static const char portPalette[] = "0123456789";
            int palLen = (int)strlen(portPalette);
            int cellW = 36, cellH = 36;
            int totalW = palLen * cellW;
            int startX = (SCREEN_W - totalW) / 2;
            int palY = y - step + step / 2 + 4;
            SDL_SetRenderDrawColor(renderer_, 15, 16, 28, 210);
            SDL_Rect palBg = {startX - 8, palY - 8, totalW + 16, cellH + 16};
            SDL_RenderFillRect(renderer_, &palBg);
            for (int i = 0; i < palLen; i++) {
                int cx = startX + i * cellW;
                bool sel2 = (i == portCharIdx_);
                if (sel2) {
                    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 255);
                    SDL_Rect bg2 = {cx, palY, cellW - 4, cellH - 4};
                    SDL_RenderFillRect(renderer_, &bg2);
                }
                char ch[2] = { portPalette[i], 0 };
                drawText(ch, cx + 10, palY + 6, sel2 ? 22 : 18, sel2 ? white : gray);
            }
            drawTextCentered("Type digits   ENTER confirm   BACKSPACE delete",
                             palY + cellH + 8, 12, {80, 80, 90, 255});
        }
    }

    // Username (editable)
    {
        std::string uDisplay = config_.username;
        if (mpUsernameTyping_) {
            static float blinkT = 0; blinkT += 0.016f;
            if ((int)(blinkT * 3) % 2 == 0) uDisplay += '_';
        }
        drawRow(2, "Username:", uDisplay.c_str(), false);
    }

    // IP Address display
    {
        std::string ip = getLocalIP();
        char ipBuf[64];
        snprintf(ipBuf, sizeof(ipBuf), "Your IP: %s", ip.c_str());
        drawTextCentered(ipBuf, y + 4, 18, {0, 255, 228, 200});
        drawTextCentered("Share this IP with players who want to join", y + 26, 12, dimCyan);
        y += step + 10;
    }

    // Spacer
    y += 6;

    // --- Preset buttons ---
    // Save preset
    {
        bool sel = (hostSetupSelection_ == 3);
        SDL_Color c = sel ? cyan : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 200, y - 4, 400, 34};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 140);
            SDL_Rect bar = {SCREEN_W / 2 - 200, y - 4, 3, 34};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered(sel ? "> SAVE AS PRESET <" : "SAVE AS PRESET", y + 4, sel ? 20 : 18, c);
        y += step - 6;
    }

    // Load preset
    {
        bool sel = (hostSetupSelection_ == 4);
        SDL_Color c = sel ? cyan : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 200, y - 4, 400, 34};
            SDL_RenderFillRect(renderer_, &bg);
            SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 140);
            SDL_Rect bar = {SCREEN_W / 2 - 200, y - 4, 3, 34};
            SDL_RenderFillRect(renderer_, &bar);
        }
        std::string presetLabel = "LOAD PRESET";
        if (!serverPresets_.empty()) {
            int idx = presetSelection_ % (int)serverPresets_.size();
            presetLabel = sel
                ? "< " + serverPresets_[idx].name + " >"
                : serverPresets_[idx].name;
        } else {
            presetLabel = "(no presets saved)";
        }
        char pbuf[128];
        snprintf(pbuf, sizeof(pbuf), "LOAD:  %s", presetLabel.c_str());
        drawTextCentered(pbuf, y + 4, sel ? 20 : 18, !serverPresets_.empty() ? c : (SDL_Color){60, 60, 70, 255});
        y += step - 6;
    }

    y += 10;

    // Start button
    {
        bool sel = (hostSetupSelection_ == 5);
        SDL_Color c = sel ? green : gray;
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 20);
            SDL_Rect bg = {SCREEN_W / 2 - 140, y - 4, 280, 38};
            SDL_RenderFillRect(renderer_, &bg);
        }
        drawTextCentered(sel ? "> START HOSTING <" : "START HOSTING", y + 4, sel ? 26 : 22, c);
        y += step;
    }

    // Back button
    {
        bool sel = (hostSetupSelection_ == 6);
        SDL_Color c = sel ? white : gray;
        drawTextCentered(sel ? "> BACK <" : "BACK", y + 4, 20, c);
    }

    drawTextCentered("UP/DOWN navigate  LEFT/RIGHT change  A/ENTER confirm", SCREEN_H - 36, 13, {80, 80, 90, 255});
}

void Game::renderJoinMenu() {
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {120, 120, 130, 255};
    SDL_Color blue  = {80, 200, 255, 255};
    SDL_Color green = {50, 255, 100, 255};

    drawTextCentered("JOIN GAME", SCREEN_H / 6, 34, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 80, SCREEN_H / 6 + 42, 160, 1};
    SDL_RenderFillRect(renderer_, &tl);

    // Connection status
    auto& net = NetworkManager::instance();
    if (net.state() == NetState::Connecting) {
        drawTextCentered("Connecting...", SCREEN_H / 3 - 40, 18, {255, 220, 60, 255});
    } else if (!connectStatus_.empty()) {
        drawTextCentered(connectStatus_.c_str(), SCREEN_H / 3 - 40, 16, {255, 100, 100, 255});
    }

    // IP address display
    std::string addrDisplay = joinAddress_;
    if (ipTyping_) {
        static float blinkTimer = 0;
        blinkTimer += 0.016f;
        if ((int)(blinkTimer * 3) % 2 == 0) addrDisplay += '_';
    }

    // Address box
    int boxY = SCREEN_H / 2 - 100;
    SDL_SetRenderDrawColor(renderer_, 20, 22, 35, 255);
    SDL_Rect addrBox = {SCREEN_W / 2 - 200, boxY - 8, 400, 42};
    SDL_RenderFillRect(renderer_, &addrBox);
    SDL_SetRenderDrawColor(renderer_, ipTyping_ ? 0 : 60, ipTyping_ ? 200 : 60, ipTyping_ ? 180 : 80, 180);
    SDL_RenderDrawRect(renderer_, &addrBox);
    drawText("Address:", SCREEN_W / 2 - 190, boxY, 20, gray);
    drawText(addrDisplay.c_str(), SCREEN_W / 2 - 50, boxY, 20, ipTyping_ ? cyan : white);

    // Port box
    {
        int pBoxY = boxY + 48;
        std::string pDisp = joinPortTyping_ ? joinPortStr_ : std::to_string(joinPort_);
        if (joinPortTyping_) {
            static float pBlink = 0; pBlink += 0.016f;
            if ((int)(pBlink * 3) % 2 == 0) pDisp += '_';
        }
        SDL_SetRenderDrawColor(renderer_, 20, 22, 35, 255);
        SDL_Rect pBox = {SCREEN_W / 2 - 200, pBoxY - 8, 400, 42};
        SDL_RenderFillRect(renderer_, &pBox);
        SDL_SetRenderDrawColor(renderer_, joinPortTyping_ ? 0 : 60, joinPortTyping_ ? 200 : 60, joinPortTyping_ ? 180 : 80, 180);
        SDL_RenderDrawRect(renderer_, &pBox);
        drawText("Port:", SCREEN_W / 2 - 190, pBoxY, 20, gray);
        drawText(pDisp.c_str(), SCREEN_W / 2 - 50, pBoxY, 20, joinPortTyping_ ? cyan : white);
    }

    // Username box
    {
        int uBoxY = boxY + 96;
        std::string uDisp = config_.username;
        if (mpUsernameTyping_) {
            static float uBlink = 0; uBlink += 0.016f;
            if ((int)(uBlink * 3) % 2 == 0) uDisp += '_';
        }
        SDL_SetRenderDrawColor(renderer_, 20, 22, 35, 255);
        SDL_Rect uBox = {SCREEN_W / 2 - 200, uBoxY - 8, 400, 42};
        SDL_RenderFillRect(renderer_, &uBox);
        SDL_SetRenderDrawColor(renderer_, mpUsernameTyping_ ? 0 : 60, mpUsernameTyping_ ? 200 : 60, mpUsernameTyping_ ? 180 : 80, 180);
        SDL_RenderDrawRect(renderer_, &uBox);
        drawText("Username:", SCREEN_W / 2 - 190, uBoxY, 20, gray);
        drawText(uDisp.c_str(), SCREEN_W / 2 - 50, uBoxY, 20, mpUsernameTyping_ ? cyan : white);
    }

    if (ipTyping_) {
        // Gamepad char picker
        static const char ipPalette[] = "0123456789.";
        int palLen = (int)strlen(ipPalette);
        int cellW = 36, cellH = 36;
        int totalW = palLen * cellW;
        int startX = (SCREEN_W - totalW) / 2;
        int palY = SCREEN_H / 2 + 10;

        // Palette background
        SDL_SetRenderDrawColor(renderer_, 15, 16, 28, 200);
        SDL_Rect palBg = {startX - 8, palY - 8, totalW + 16, cellH + 16};
        SDL_RenderFillRect(renderer_, &palBg);

        for (int i = 0; i < palLen; i++) {
            int cx = startX + i * cellW;
            bool sel = (i == ipCharIdx_);
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 255);
                SDL_Rect bg = {cx, palY, cellW - 4, cellH - 4};
                SDL_RenderFillRect(renderer_, &bg);
            }
            char ch[2] = { ipPalette[i], 0 };
            drawText(ch, cx + 10, palY + 6, sel ? 22 : 18, sel ? white : gray);
        }
        drawTextCentered("\xe2\x86\x90\xe2\x86\x92 cycle   A insert   Y delete   START connect   B cancel",
                         palY + 56, 12, {80, 80, 90, 255});
    } else if (mpUsernameTyping_) {
        // Username editing palette
        static const char userPalette[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
        int palLen = (int)strlen(userPalette);
        int cellW = 20, cellH = 26;
        int cols = 16;
        int rows = (palLen + cols - 1) / cols;
        int totalW = cols * cellW;
        int startX = (SCREEN_W - totalW) / 2;
        int palY = SCREEN_H / 2 + 10;

        SDL_SetRenderDrawColor(renderer_, 15, 16, 28, 200);
        SDL_Rect palBg = {startX - 8, palY - 8, totalW + 16, rows * cellH + 16};
        SDL_RenderFillRect(renderer_, &palBg);

        for (int i = 0; i < palLen; i++) {
            int col = i % cols, row = i / cols;
            int cx = startX + col * cellW;
            int cy = palY + row * cellH;
            bool sel = (i == usernameCharIdx_);
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 255);
                SDL_Rect bg = {cx, cy, cellW - 2, cellH - 2};
                SDL_RenderFillRect(renderer_, &bg);
            }
            char ch[2] = { userPalette[i], 0 };
            drawText(ch, cx + 4, cy + 3, sel ? 18 : 14, sel ? white : gray);
        }
        drawTextCentered("Type or use D-pad   A insert   Y delete   B/Enter confirm",
                         palY + rows * cellH + 8, 12, {80, 80, 90, 255});
    } else if (joinPortTyping_) {
        // Port digit palette (gamepad)
        static const char portPalette[] = "0123456789";
        int palLen = (int)strlen(portPalette);
        int cellW = 36, cellH = 36;
        int totalW = palLen * cellW;
        int startX = (SCREEN_W - totalW) / 2;
        int palY = SCREEN_H / 2 + 30;

        SDL_SetRenderDrawColor(renderer_, 15, 16, 28, 200);
        SDL_Rect palBg = {startX - 8, palY - 8, totalW + 16, cellH + 16};
        SDL_RenderFillRect(renderer_, &palBg);

        for (int i = 0; i < palLen; i++) {
            int cx = startX + i * cellW;
            bool sel = (i == joinPortCharIdx_);
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 255);
                SDL_Rect bg2 = {cx, palY, cellW - 4, cellH - 4};
                SDL_RenderFillRect(renderer_, &bg2);
            }
            char ch[2] = { portPalette[i], 0 };
            drawText(ch, cx + 10, palY + 6, sel ? 22 : 18, sel ? white : gray);
        }
        drawTextCentered("Type digits   ENTER confirm   BACKSPACE delete",
                         palY + cellH + 8, 12, {80, 80, 90, 255});
    } else {
        int btnY = SCREEN_H / 2 + 30;
        int btnStep = 36;

        // Edit address
        {
            bool sel = (joinMenuSelection_ == 0);
            drawTextCentered(sel ? "> EDIT ADDRESS <" : "EDIT ADDRESS", btnY, sel ? 22 : 20, sel ? blue : gray);
            btnY += btnStep;
        }
        // Edit port
        {
            bool sel = (joinMenuSelection_ == 1);
            char pBuf[64];
            snprintf(pBuf, sizeof(pBuf), sel ? "> PORT: %d <" : "PORT: %d", joinPort_);
            drawTextCentered(pBuf, btnY, sel ? 22 : 20, sel ? cyan : gray);
            btnY += btnStep;
        }
        // Edit username
        {
            bool sel = (joinMenuSelection_ == 2);
            char uBuf[128];
            snprintf(uBuf, sizeof(uBuf), sel ? "> USERNAME: %s <" : "USERNAME: %s", config_.username.c_str());
            drawTextCentered(uBuf, btnY, sel ? 22 : 20, sel ? cyan : gray);
            btnY += btnStep;
        }
        // Connect
        {
            bool sel = (joinMenuSelection_ == 3);
            drawTextCentered(sel ? "> CONNECT <" : "CONNECT", btnY, sel ? 24 : 20, sel ? green : gray);
            btnY += btnStep;
        }
        // Save server
        {
            bool sel = (joinMenuSelection_ == 4);
            SDL_Color saveColor = {0, 200, 180, 255};
            drawTextCentered(sel ? "> SAVE SERVER <" : "SAVE SERVER", btnY, sel ? 22 : 18, sel ? saveColor : gray);
            btnY += btnStep;
        }
        // Back
        {
            bool sel = (joinMenuSelection_ == 5);
            drawTextCentered(sel ? "> BACK <" : "BACK", btnY, 20, sel ? white : gray);
        }
        drawTextCentered("A / ENTER - Select    B / ESC - Back", SCREEN_H - 40, 13, {80, 80, 90, 255});
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

        drawTextCentered("B / ESC - Cancel", SCREEN_H - 40, 13, {80, 80, 90, 255});
        return;
    }

    // Title
    drawTextCentered("LOBBY", SCREEN_H / 10, 34, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 60, SCREEN_H / 10 + 42, 120, 1};
    SDL_RenderFillRect(renderer_, &tl);

    // ══════════════════════════════════════════════════════════
    //  Settings panel (right side) — host can adjust, clients read-only
    // ══════════════════════════════════════════════════════════
    {
        bool isHostPlayer = net.isHost();
        int panelX = SCREEN_W / 2 + 20;
        int panelY = SCREEN_H / 10 + 60;
        int panelW = SCREEN_W / 2 - 40;
        int panelH = SCREEN_H - panelY - 120;

        // Panel background
        SDL_SetRenderDrawColor(renderer_, 14, 16, 28, 255);
        SDL_Rect panel = {panelX - 8, panelY - 4, panelW + 16, panelH + 8};
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 80);
        SDL_RenderDrawRect(renderer_, &panel);

        drawText("SETTINGS", panelX, panelY, 16, gray);
        if (isHostPlayer) drawText("(LEFT/RIGHT adjust)", panelX + 110, panelY + 2, 11, {60, 60, 70, 255});
        SDL_SetRenderDrawColor(renderer_, 60, 60, 70, 100);
        SDL_Rect hdrLine = {panelX, panelY + 22, panelW, 1};
        SDL_RenderFillRect(renderer_, &hdrLine);

        int rowY = panelY + 30;
        int rowStep = 28;

        auto drawSettingRow = [&](int idx, const char* label, const char* value, SDL_Color valColor = {255,255,255,255}) {
            bool sel = isHostPlayer && (lobbySettingsSel_ == idx);
            SDL_Color lc = sel ? white : gray;
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

        // 1: Friendly fire
        {
            const char* val = lobbySettings_.friendlyFire ? "ON" : "OFF";
            drawSettingRow(1, "Friendly Fire:", val,
                lobbySettings_.friendlyFire ? red : (SDL_Color){80,200,120,255});
        }

        // 2: Upgrades
        {
            const char* val = lobbySettings_.upgradesShared ? "Shared (all players)" : "Individual (picker only)";
            drawSettingRow(2, "Upgrades:", val);
        }

        // 3: Map width
        {
            char v[16]; snprintf(v, sizeof(v), "%d", lobbySettings_.mapWidth);
            drawSettingRow(3, "Map Width:", v);
        }

        // 4: Map height
        {
            char v[16]; snprintf(v, sizeof(v), "%d", lobbySettings_.mapHeight);
            drawSettingRow(4, "Map Height:", v);
        }

        // 5: Enemy HP
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.enemyHpScale);
            drawSettingRow(5, "Enemy HP:", v);
        }

        // 6: Enemy speed
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.enemySpeedScale);
            drawSettingRow(6, "Enemy Speed:", v);
        }

        // 7: Spawn rate
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.spawnRateScale);
            drawSettingRow(7, "Spawn Rate:", v);
        }

        // 8: Player HP
        {
            char v[16]; snprintf(v, sizeof(v), "%d", lobbySettings_.playerMaxHp);
            drawSettingRow(8, "Player HP:", v);
        }

        // 9: Team count
        {
            const char* val = (lobbySettings_.teamCount == 4) ? "4 Teams" :
                              (lobbySettings_.teamCount == 2) ? "2 Teams" : "None";
            drawSettingRow(9, "Teams:", val);
        }

        // 10: Lives per player (up to 100)
        {
            char v[16];
            if (lobbySettings_.livesPerPlayer == 0)
                snprintf(v, sizeof(v), "Infinite");
            else
                snprintf(v, sizeof(v), "%d", lobbySettings_.livesPerPlayer);
            drawSettingRow(10, "Lives:", v);
        }

        // 11: Lives mode (only shown when lives are limited)
        if (lobbySettings_.livesPerPlayer > 0) {
            const char* val = lobbySettings_.livesShared ? "Shared Pool" : "Individual";
            drawSettingRow(11, "Lives Mode:", val);
        }

        // 12: Crate Interval (PVP only)
        if (lobbySettings_.isPvp) {
            char v[16]; snprintf(v, sizeof(v), "%.0fs", lobbySettings_.crateInterval);
            drawSettingRow(12, "Crate Interval:", v);
        }

        // 13: Wave Count (PVE only)
        if (!lobbySettings_.isPvp) {
            char v[16];
            if (lobbySettings_.waveCount == 0)
                snprintf(v, sizeof(v), "Endless");
            else
                snprintf(v, sizeof(v), "%d", lobbySettings_.waveCount);
            drawSettingRow(13, "Waves:", v);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Player list (left side)
    // ══════════════════════════════════════════════════════════
    int listX = 60;
    int listY = SCREEN_H / 10 + 60;
    drawText("PLAYERS", listX, listY, 16, gray);
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
        bool isHostP = (players[i].id == 0);

        // Row background for local player
        if (isLocal) {
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

        // Name (team-colored if team assigned)
        char entryBuf[128];
        snprintf(entryBuf, sizeof(entryBuf), "%s%s", players[i].username.c_str(),
                 isHostP ? "  \xe2\x98\x85" : "");
        SDL_Color nameC = isLocal ? yellow : (players[i].ready ? green : white);
        if (players[i].team >= 0 && players[i].team < 4)
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
    }

    // ── Bottom buttons ──
    int btnY = SCREEN_H - 100;
    if (net.isHost()) {
        bool allReady = true;
        for (auto& p : players) { if (!p.ready && p.id != 0) allReady = false; }
        SDL_Color startC = allReady ? green : dimGrn;
        drawTextCentered("> START GAME <", btnY, 24, startC);
        if (!allReady && players.size() > 1) {
            drawTextCentered("Waiting for players to ready up...", btnY + 30, 13, gray);
        }
    } else {
        const char* rdyLabel = lobbyReady_ ? "> READY! <" : "> READY UP <";
        drawTextCentered(rdyLabel, btnY, 24, lobbyReady_ ? green : white);
    }
    drawTextCentered("B / ESC - Leave    UP/DOWN navigate    LEFT/RIGHT adjust", SCREEN_H - 40, 13, {80, 80, 90, 255});
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

        // Name tag — draw at world position; team-colored if team assigned
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
                    SDL_Rect nd = {(int)sp.x - ns->w / 2, (int)sp.y - 40, ns->w, ns->h};
                    SDL_RenderCopy(renderer_, nt, nullptr, &nd);
                    SDL_DestroyTexture(nt);
                    SDL_FreeSurface(ns);
                }
            }
        }

        // Health bar
        float barW = 40.0f;
        float barH = 4.0f;
        float hpRatio = (rp.maxHp > 0) ? (float)rp.hp / rp.maxHp : 0;
        SDL_FRect bgBar = {sp.x - barW / 2, sp.y - 32, barW, barH};
        SDL_FRect fgBar = {sp.x - barW / 2, sp.y - 32, barW * hpRatio, barH};
        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, 180);
        SDL_RenderFillRectF(renderer_, &bgBar);
        Uint8 hr = (Uint8)(255 * (1.0f - hpRatio));
        Uint8 hg = (Uint8)(255 * hpRatio);
        SDL_SetRenderDrawColor(renderer_, hr, hg, 0, 220);
        SDL_RenderFillRectF(renderer_, &fgBar);
    }

    // Kill/death counter and ping in corner
    NetPlayer* local = net.localPlayer();
    if (local) {
        char kdStr[64];
        snprintf(kdStr, sizeof(kdStr), "K:%d  D:%d  Score:%d", local->kills, local->deaths, local->score);
        drawText(kdStr, SCREEN_W - 250, 10, 14, {200, 200, 200, 200});
    }

    // Ping display
    {
        uint32_t ping = net.getPing();
        char pingStr[32];
        snprintf(pingStr, sizeof(pingStr), "Ping: %dms", ping);
        SDL_Color pingColor = (ping < 50) ? SDL_Color{50, 255, 100, 200} :
                              (ping < 100) ? SDL_Color{255, 220, 60, 200} :
                              SDL_Color{255, 80, 80, 200};
        drawText(pingStr, SCREEN_W - 120, 30, 12, pingColor);
    }

    // Player count
    {
        char plrStr[32];
        snprintf(plrStr, sizeof(plrStr), "Players: %d", (int)players.size());
        drawText(plrStr, 10, SCREEN_H - 20, 11, {150, 150, 160, 180});
    }
}

void Game::renderMultiplayerPause() {
    auto& net2 = NetworkManager::instance();
    bool hasTeams     = currentRules_.teamCount >= 2;
    bool isHostPlayer = net2.isHost();

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
    struct MenuItem { const char* label; SDL_Color col; int idx; };
    MenuItem items[6];
    int itemCount = 0;

    items[itemCount++] = { "RESUME",      white, 0 };
    if (hasTeams)     items[itemCount++] = { "CHANGE TEAM",  gold,  1 };
    if (isHostPlayer) items[itemCount++] = { "ADMIN",        cyan,  hasTeams ? 2 : 1 };
    if (isHostPlayer) {
        int egIdx = itemCount;
        items[itemCount++] = { "END GAME",    (SDL_Color){255, 160, 60, 255}, egIdx };
    }
    int dcIdx = itemCount;
    const char* dcLabel = spectatorMode_ ? "BACK TO LOBBY" : "DISCONNECT";
    items[itemCount++] = { dcLabel, red, dcIdx };

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
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 40);
            SDL_Rect bar = {px + 20, itemY - 4, panelW - 40, 32};
            SDL_RenderFillRect(renderer_, &bar);
            SDL_SetRenderDrawColor(renderer_, items[i].col.r, items[i].col.g, items[i].col.b, 200);
            SDL_Rect acc = {px + 20, itemY - 4, 3, 32};
            SDL_RenderFillRect(renderer_, &acc);
        }
        drawTextCentered(items[i].label, itemY, 22, sel ? items[i].col : gray);
        itemY += 50;
    }

    // Volume hint
    int volY = py + panelH - 28;
    char sfxBuf[64], musBuf[64];
    snprintf(sfxBuf, sizeof(sfxBuf), "SFX: %d%%", config_.sfxVolume * 100 / 128);
    snprintf(musBuf, sizeof(musBuf), "Music: %d%%", config_.musicVolume * 100 / 128);
    drawText(sfxBuf, px + 40, volY, 13, {70, 70, 80, 255});
    drawText(musBuf, px + 210, volY, 13, {70, 70, 80, 255});

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

        if (rowSel) {
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
            SDL_SetRenderDrawColor(renderer_,
                actSel ? actionColors[a].r : 30,
                actSel ? actionColors[a].g : 30,
                actSel ? actionColors[a].b : 35, 200);
            SDL_Rect btn = {actX + a * 50, rowY + 4, 44, 26};
            SDL_RenderFillRect(renderer_, &btn);
            SDL_SetRenderDrawColor(renderer_, actionColors[a].r, actionColors[a].g, actionColors[a].b, actSel ? 255 : 80);
            SDL_RenderDrawRect(renderer_, &btn);
            drawText(actionLabels[a], actX + a * 50 + 4, rowY + 8, 12, actSel ? actionColors[a] : gray);
        }
        rowY += 44;
    }

    drawTextCentered("[↑↓] Player  [←→] Action  [A] Confirm  [B] Close", py + panelH - 28, 13, gray);
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
        drawTextCentered("Press A / ENTER to respawn", SCREEN_H / 2 + 10, 20, cyan);
    }

    // Stats
    auto& net = NetworkManager::instance();
    NetPlayer* local = net.localPlayer();
    if (local) {
        char statBuf[128];
        snprintf(statBuf, sizeof(statBuf), "Kills: %d   Deaths: %d   Score: %d",
                 local->kills, local->deaths, local->score);
        drawTextCentered(statBuf, SCREEN_H / 2 + 90, 16, gray);
    }

    drawTextCentered("TAB - Scoreboard", SCREEN_H - 40, 13, {80, 80, 90, 255});
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

    drawTextCentered("ENTER / A - Continue", SCREEN_H - 50, 14, {80, 80, 90, 255});
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

        // Interpolated position
        Vec2 drawPos = {
            rp.prevPos.x + (rp.targetPos.x - rp.prevPos.x) * rp.interpT,
            rp.prevPos.y + (rp.targetPos.y - rp.prevPos.y) * rp.interpT
        };
        float drawRot = rp.prevRotation + (rp.targetRotation - rp.prevRotation) * rp.interpT;

        const Uint8 ghostAlpha = 80;
        const bool isGhost = rp.spectating;

        // Legs
        if (rp.moving && !legSprites_.empty()) {
            int idx = rp.legFrame % (int)legSprites_.size();
            if (isGhost) SDL_SetTextureAlphaMod(legSprites_[idx], ghostAlpha);
            renderSprite(legSprites_[idx], drawPos, rp.legRotation + (float)M_PI / 2, 3.0f);
            if (isGhost) SDL_SetTextureAlphaMod(legSprites_[idx], 255);
        }

        // Body — tint by team color or default blue
        if (!playerSprites_.empty()) {
            int idx = rp.animFrame % (int)playerSprites_.size();
            Vec2 bodyPos = drawPos + Vec2::fromAngle(drawRot) * 6.0f;
            SDL_Color tint = {180, 200, 255, 255}; // default slight blue
            if (isGhost) tint = {140, 180, 255, ghostAlpha};
            else if (rp.team >= 0 && rp.team < 4) tint = teamTints[rp.team];
            if (isGhost) SDL_SetTextureAlphaMod(playerSprites_[idx], ghostAlpha);
            renderSpriteEx(playerSprites_[idx], bodyPos, drawRot + (float)M_PI / 2, 3.0f, tint);
            if (isGhost) SDL_SetTextureAlphaMod(playerSprites_[idx], 255);
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
        drawTextCentered("LEFT/RIGHT - Choose    A/ENTER - Lock In    B/ESC - Back", SCREEN_H - 80, 14, gray);
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
    SDL_SetRenderDrawColor(renderer_, 6, 8, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color cyan   = {0, 255, 228, 255};
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color dimGray= {80, 80, 90, 255};
    SDL_Color green  = {50, 255, 100, 255};
    SDL_Color red    = {255, 80, 80, 255};
    SDL_Color yellow = {255, 220, 50, 255};

    // Title
    drawTextCentered("MODS & CONTENT", SCREEN_H / 12, 32, cyan);
    SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 60);
    SDL_Rect tl = {SCREEN_W / 2 - 100, SCREEN_H / 12 + 40, 200, 1};
    SDL_RenderFillRect(renderer_, &tl);

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
        if (backSel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 30);
            SDL_Rect bar = {SCREEN_W / 2 - 60, backY - 4, 120, 28};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered("BACK", backY, 20, backSel ? white : dimGray);
        drawTextCentered("A / ENTER toggle   B / ESC back", SCREEN_H - 36, 12, {60, 60, 70, 255});
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
        if (backSel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 30);
            SDL_Rect bar = {SCREEN_W / 2 - 60, backY - 4, 120, 28};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawTextCentered("BACK", backY, 20, backSel ? white : dimGray);
        drawTextCentered("B / ESC back", SCREEN_H - 36, 12, {60, 60, 70, 255});
    }
}
