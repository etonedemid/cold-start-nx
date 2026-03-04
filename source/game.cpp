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
        SDL_WINDOW_OPENGL
#endif
    );
    if (!window_) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return false; }

#ifdef __SWITCH__
    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#else
    // Try OpenGL accelerated, then software fallback
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_) {
        printf("PC renderer: opengl (accelerated)\n");
    } else {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (renderer_) printf("PC renderer: software fallback\n");
    }
#endif
    if (!renderer_) { printf("SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Open all connected game controllers
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i))
            SDL_GameControllerOpen(i);
    }

    Assets::instance().init(renderer_);
    loadAssets();
    loadConfig();

    // Initialize map editor
    editor_.init(renderer_, SCREEN_W, SCREEN_H);

    // Scan for custom characters and maps
    scanCharacters();
    scanMapFiles();

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
    floorTex_  = a.tex("sprites/tiles/floor.png");
    grassTex_  = a.tex("sprites/tiles/grass.png");
    gravelTex_ = a.tex("sprites/tiles/gravel.png");
    woodTex_   = a.tex("sprites/tiles/wood.png");
    sandTex_   = a.tex("sprites/tiles/sand.png");
    wallTex_   = a.tex("sprites/tiles/floor.png");
    glassTex_  = a.tex("sprites/tiles/glass.png");
    deskTex_   = a.tex("sprites/tiles/desk.png");
    boxTex_    = a.tex("sprites/props/box.png");
    gravelGrass1Tex_ = a.tex("sprites/tiles/gravel-grass1.png");
    gravelGrass2Tex_ = a.tex("sprites/tiles/gravel-grass2.png");
    gravelGrass3Tex_ = a.tex("sprites/tiles/gravel-grass3.png");
    glassTileTex_   = a.tex("sprites/tiles/glasstile.png");

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

    // Generate map
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

        if (state_ == GameState::Playing || state_ == GameState::PlayingCustom
            || state_ == GameState::PlayingPack) {
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
            // Check if editor wants to test play
            if (editor_.wantsTestPlay()) {
                editor_.clearTestPlay();
                // Copy the editor's map directly into customMap_
                customMap_ = editor_.getMap();
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

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { running_ = false; return; }

        // Pass events to editor if active
        if ((state_ == GameState::Editor || state_ == GameState::EditorConfig) && editor_.isActive()) {
            editor_.handleInput(e);
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
        if (SDL_IsGameController(i)) { gc = SDL_GameControllerFromInstanceID(i); break; }
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
        if (menuSelection_ > 7) menuSelection_ = 7;
        if (confirmInput_) {
            if (menuSelection_ == 0) startGame();
            else if (menuSelection_ == 1) {
                // Open editor config screen first
                state_ = GameState::EditorConfig;
                editor_.setActive(true);
                editor_.showConfig();
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 2) {
                scanMapFiles();
                state_ = GameState::MapSelect;
                mapSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 3) {
                scanMapPacks();
                state_ = GameState::PackSelect;
                packSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 4) {
                scanCharacters();
                state_ = GameState::CharSelect;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 5) {
                // Create Character
                charCreator_ = CharCreatorState{};
                state_ = GameState::CharCreator;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 6) {
                state_ = GameState::ConfigMenu;
                configSelection_ = 0;
            }
            else running_ = false;
        }
    }
    else if (state_ == GameState::ConfigMenu) {
        if (configSelection_ < 0) configSelection_ = 0;
        if (configSelection_ > 8) configSelection_ = 8;

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 8) menuSelection_ = 8;
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
        else if (configSelection_ == 8 && (confirmInput_ || backInput_ || pauseInput_)) {
            saveConfig();
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::Paused) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 3) menuSelection_ = 3;
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
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::Playing;
            else if (menuSelection_ == 3) { state_ = GameState::MainMenu; playMenuMusic(); }
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
                startCustomMap(mapFiles_[mapSelectIdx_]);
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
        if (menuSelection_ > 3) menuSelection_ = 3;
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
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::PlayingCustom;
            else if (menuSelection_ == 3) {
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
                saveCharCreator();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
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
        if (menuSelection_ > 3) menuSelection_ = 3;
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
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::PlayingPack;
            else if (menuSelection_ == 3) {
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
}

// ═════════════════════════════════════════════════════════════════════════════
//  Update
// ═════════════════════════════════════════════════════════════════════════════

void Game::update() {
    float dt = dt_;
    gameTime_ += dt;

    updatePlayer(dt);
    updateEnemies(dt);
    updateBullets(dt);
    updateBombs(dt);
    updateExplosions(dt);
    updateBoxFragments(dt);
    updateSpawning(dt);
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
        if (p.deathTimer > 1.5f && state_ != GameState::Dead) {
            state_ = GameState::Dead;
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

    // Tile collision (slide)
    if (!map_.worldCollides(newPos.x, p.pos.y, PLAYER_SIZE * 0.4f))
        p.pos.x = newPos.x;
    if (!map_.worldCollides(p.pos.x, newPos.y, PLAYER_SIZE * 0.4f))
        p.pos.y = newPos.y;

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

    // Leg anim
    if (p.moving && !legSprites_.empty()) {
        p.legAnimTimer += dt;
        float legSpeed = p.vel.length() / 200.0f * 0.05f + 0.03f;
        if (p.legAnimTimer > legSpeed) {
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
    if (p.reloading) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0) {
            p.ammo = p.maxAmmo;
            p.reloading = false;
        }
    } else if (fireInput_ && p.fireCooldown <= 0 && p.ammo > 0) {
        spawnBullet(p.pos, p.rotation);
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

    // ── Parry ──
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

    // ── Bombs ──
    // Auto-spawn: if player has bomb charges and no orbiting bomb exists, spawn one
    {
        bool hasOrbiting = false;
        for (auto& b : bombs_) if (b.alive && !b.hasDashed) { hasOrbiting = true; break; }
        if (!hasOrbiting && p.bombCount > 0) {
            spawnBomb();
            p.bombCount--;
        }
    }
    // ZL / RMB = Launch the nearest orbiting bomb
    if (bombLaunchInput_) {
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

            Vec2 launchDir;
            if (bestIdx >= 0) {
                // Launch toward that enemy
                launchDir = (enemies_[bestIdx].pos - toFire->pos).normalized();
                toFire->homingTarget = bestIdx;
                toFire->homingStr = 3.5f; // homing strength (rad/s turn rate)
            } else {
                // Raycast: launch in aim direction
                launchDir = aimDir;
                toFire->homingTarget = -1;
                toFire->homingStr = 0;
            }
            toFire->activate(launchDir);
        }
    }
}

void Player::takeDamage(int dmg) {
    if (invulnerable || dead) return;
    hp -= dmg;
    invulnerable = true;
    invulnTimer = PLAYER_INVULN_TIME;
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
    for (auto& e : enemies_) {
        if (!e.alive) continue;

        // Stun
        if (e.stunTimer > 0) { e.stunTimer -= dt; continue; }

        // Damage flash decay
        if (e.damageFlash > 0) e.damageFlash -= dt * 4.0f;

        // Vision check
        e.canSeePlayer = enemyCanSeePlayer(e);
        if (e.canSeePlayer) {
            e.state = EnemyState::Chase;
            e.lastSeenTime = gameTime_;
        } else if (e.state == EnemyState::Chase && gameTime_ - e.lastSeenTime > LOSE_PLAYER_DELAY) {
            e.state = EnemyState::Wander;
        }

        // Behavior
        if (e.isDashing) {
            enemyDash(e, dt);
        } else if (e.dashCharging) {
            e.dashDelayTimer -= dt;
            e.flashTimer -= dt;
            if (e.dashDelayTimer <= 0) {
                // Initiate actual dash
                e.isDashing = true;
                e.dashTimer = ENEMY_DASH_DUR;
                Vec2 toPlayer = (player_.pos - e.pos).normalized();
                e.dashDir = toPlayer;
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

bool Game::enemyCanSeePlayer(const Enemy& e) const {
    if (player_.dead) return false;
    Vec2 toPlayer = player_.pos - e.pos;
    float dist = toPlayer.length();
    if (dist > ENEMY_VISION_DIST) return false;

    // Angle check
    float angleTo = atan2f(toPlayer.y, toPlayer.x);
    float diff = angleTo - e.rotation;
    // Normalize angle diff
    while (diff > M_PI)  diff -= 2*M_PI;
    while (diff < -M_PI) diff += 2*M_PI;
    if (fabsf(diff) > ENEMY_VISION_ANGLE * M_PI / 180.0f) return false;

    // Raycast through tiles (simple line walk)
    Vec2 dir = toPlayer.normalized();
    float step = TILE_SIZE * 0.5f;
    for (float d = step; d < dist; d += step) {
        Vec2 p = e.pos + dir * d;
        int tx = TileMap::toTile(p.x);
        int ty = TileMap::toTile(p.y);
        if (map_.isSolid(tx, ty)) return false;
    }
    return true;
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
    Vec2 toPlayer = player_.pos - e.pos;
    float dist = toPlayer.length();

    // Shooter: keep range and shoot
    if (e.type == EnemyType::Shooter) {
        if (dist < 300.0f) {
            // Back away
            e.vel = steerToward(e.pos, e.pos - toPlayer.normalized() * 200.0f, e.speed * 0.6f, dt);
        } else if (dist > 450.0f) {
            e.vel = steerToward(e.pos, player_.pos, e.speed, dt);
        } else {
            e.vel = e.vel * 0.9f; // slow stop
        }
        enemyShoot(e, dt);
        // Face player
        e.rotation = atan2f(toPlayer.y, toPlayer.x);
        return;
    }

    // Melee: charge toward player with inertia
    Vec2 desired = steerToward(e.pos, player_.pos, e.speed, dt);
    float inertia = MELEE_INERTIA;
    e.vel = Vec2::lerp(e.vel, desired, dt * inertia);

    // Start dash when close
    if (dist < ENEMY_DASH_DIST && !e.dashOnCd && !e.dashCharging) {
        e.dashCharging = true;
        e.dashDelayTimer = ENEMY_DASH_DELAY;
        e.flashTimer = 0.08f; // red flash
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
        spawnEnemyBullet(e.pos, player_.pos);
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
            }
            b.alive = false;
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
    b.damage = 1;
    bullets_.push_back(b);

    // ── Visual polish: muzzle flash ──
    muzzleFlashTimer_ = 0.06f;
    muzzleFlashPos_ = b.pos; // store exact bullet spawn point
    camera_.addShake(1.5f);  // subtle recoil shake
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
        if (b.age > b.lifetime) { b.alive = false; continue; }

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
            // Homing toward target enemy if alive
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
            // Pause before next wave, gets shorter over time
            float pauseScale = fmaxf(0.3f, 1.0f - waveNumber_ * 0.05f);
            wavePauseTimer_ = WAVE_PAUSE_BASE * pauseScale * config_.spawnRateScale;
        }
    } else {
        wavePauseTimer_ -= dt;
        if (wavePauseTimer_ <= 0) {
            // Start new wave
            waveNumber_++;
            int waveSize = WAVE_SIZE_BASE + waveNumber_ * WAVE_SIZE_GROWTH;
            if (waveSize > WAVE_MAX_SIZE) waveSize = WAVE_MAX_SIZE;
            waveEnemiesLeft_ = waveSize;
            waveActive_ = true;
            waveSpawnTimer_ = 0;

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

    // Player bullets vs enemies
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            if (circleOverlap(b.pos, b.size, e.pos, e.size * 0.7f)) {
                e.hp -= b.damage;
                e.damageFlash = 1.0f;
                b.alive = false;
                // Aggro
                e.state = EnemyState::Chase;
                e.lastSeenTime = gameTime_;
                if (e.hp <= 0) killEnemy(e);
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
}

void Game::killEnemy(Enemy& e) {
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

        // Blood & scorch decals
        for (auto& bd : blood_) {
            if (bloodTex_) {
                Vec2 sp = camera_.worldToScreen(bd.pos);
                int half = (int)(32 * bd.scale);
                SDL_Rect dst = {(int)(sp.x - half), (int)(sp.y - half), half * 2, half * 2};
                if (bd.type == DecalType::Scorch) {
                    SDL_SetTextureColorMod(bloodTex_, 20, 20, 20);
                    SDL_SetTextureAlphaMod(bloodTex_, 160);
                } else {
                    SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
                    SDL_SetTextureAlphaMod(bloodTex_, 180);
                }
                SDL_RenderCopyEx(renderer_, bloodTex_, nullptr, &dst,
                    bd.rotation * 180.0f / (float)M_PI, nullptr, SDL_FLIP_NONE);
                SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
                SDL_SetTextureAlphaMod(bloodTex_, 255);
            }
        }

        // Bombs
        for (auto& b : bombs_) {
            if (!b.alive) continue;
            SDL_Texture* tex = (!bombSprites_.empty()) ?
                bombSprites_[b.animFrame % bombSprites_.size()] : nullptr;
            if (tex) renderSprite(tex, b.pos, 0, 2.0f);
        }

        // Explosions
        for (auto& ex : explosions_) {
            if (!ex.alive) continue;
            Vec2 sp = camera_.worldToScreen(ex.pos);
            float t = ex.timer / ex.duration;
            // Phase 1: bright expanding fireball (0-30%)
            // Phase 2: fading ring + dissipating (30-100%)
            if (t < 0.3f) {
                float p = t / 0.3f; // 0..1 within phase 1
                float r = ex.radius * (0.3f + 0.7f * p);
                // Hot white-yellow core
                int coreAlpha = (int)(220 * (1.0f - p * 0.5f));
                float coreR = r * 0.4f * (1.0f - p * 0.3f);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 200, (Uint8)coreAlpha);
                for (int iy = -(int)coreR; iy <= (int)coreR; iy += 2) {
                    int hw = (int)sqrtf(coreR * coreR - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 2};
                    SDL_RenderFillRect(renderer_, &row);
                }
                // Orange mid layer
                int midAlpha = (int)(180 * (1.0f - p * 0.3f));
                float midR = r * 0.7f;
                SDL_SetRenderDrawColor(renderer_, 255, 160, 40, (Uint8)midAlpha);
                for (int iy = -(int)midR; iy <= (int)midR; iy += 2) {
                    int hw = (int)sqrtf(midR * midR - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 2};
                    SDL_RenderFillRect(renderer_, &row);
                }
                // Outer fireball edge
                int outerAlpha = (int)(120 * (1.0f - p * 0.5f));
                SDL_SetRenderDrawColor(renderer_, 255, 100, 20, (Uint8)outerAlpha);
                for (int iy = -(int)r; iy <= (int)r; iy += 2) {
                    int hw = (int)sqrtf(r * r - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 2};
                    SDL_RenderFillRect(renderer_, &row);
                }
            } else {
                float p = (t - 0.3f) / 0.7f; // 0..1 within phase 2
                float r = ex.radius * (1.0f - p * 0.1f);
                // Fading orange ring
                int alpha = (int)(140 * (1.0f - p));
                float thickness = 8.0f + 12.0f * p;
                SDL_SetRenderDrawColor(renderer_, 255, 120, 30, (Uint8)alpha);
                for (float a = 0; a < 2.0f * (float)M_PI; a += 0.05f) {
                    float cx = cosf(a), cy = sinf(a);
                    for (float d = r - thickness; d < r; d += 2.0f) {
                        SDL_Rect pt = {(int)(sp.x + cx * d) - 1, (int)(sp.y + cy * d) - 1, 3, 3};
                        SDL_RenderFillRect(renderer_, &pt);
                    }
                }
                // Inner smoke fade
                float smokeR = r * 0.5f * (1.0f - p * 0.5f);
                int smokeAlpha = (int)(60 * (1.0f - p));
                SDL_SetRenderDrawColor(renderer_, 80, 60, 40, (Uint8)smokeAlpha);
                for (int iy = -(int)smokeR; iy <= (int)smokeR; iy += 3) {
                    int hw = (int)sqrtf(smokeR * smokeR - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 3};
                    SDL_RenderFillRect(renderer_, &row);
                }
            }
        }

        // Enemies
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            SDL_Texture* tex = (e.type == EnemyType::Shooter) ? shooterSprite_ : enemySprite_;
            float drawScale = (e.type == EnemyType::Shooter) ? SHOOTER_RENDER_SCALE : 3.0f;
            if (tex) {
                // Tint red when damaged or charging
                if (e.damageFlash > 0 || e.flashTimer > 0 || e.dashCharging) {
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

        // UI Layer
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
        renderMapSelectMenu();
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
        for (auto& bd : blood_) {
            if (bloodTex_) {
                Vec2 sp = camera_.worldToScreen(bd.pos);
                int half = (int)(32 * bd.scale);
                SDL_Rect dst = {(int)(sp.x - half), (int)(sp.y - half), half * 2, half * 2};
                if (bd.type == DecalType::Scorch) {
                    SDL_SetTextureColorMod(bloodTex_, 20, 20, 20);
                    SDL_SetTextureAlphaMod(bloodTex_, 160);
                } else {
                    SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
                    SDL_SetTextureAlphaMod(bloodTex_, 180);
                }
                SDL_RenderCopyEx(renderer_, bloodTex_, nullptr, &dst,
                    bd.rotation * 180.0f / (float)M_PI, nullptr, SDL_FLIP_NONE);
                SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
                SDL_SetTextureAlphaMod(bloodTex_, 255);
            }
        }
        for (auto& b : bombs_) {
            if (!b.alive) continue;
            SDL_Texture* tex = (!bombSprites_.empty()) ?
                bombSprites_[b.animFrame % bombSprites_.size()] : nullptr;
            if (tex) renderSprite(tex, b.pos, 0, 2.0f);
        }
        for (auto& ex : explosions_) {
            if (!ex.alive) continue;
            Vec2 sp = camera_.worldToScreen(ex.pos);
            float t = ex.timer / ex.duration;
            if (t < 0.3f) {
                float p = t / 0.3f;
                float r = ex.radius * (0.3f + 0.7f * p);
                float coreR = r * 0.4f * (1.0f - p * 0.3f);
                int coreAlpha = (int)(220 * (1.0f - p * 0.5f));
                SDL_SetRenderDrawColor(renderer_, 255, 255, 200, (Uint8)coreAlpha);
                for (int iy = -(int)coreR; iy <= (int)coreR; iy += 2) {
                    int hw = (int)sqrtf(coreR * coreR - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 2};
                    SDL_RenderFillRect(renderer_, &row);
                }
                int midAlpha = (int)(180 * (1.0f - p * 0.3f));
                float midR = r * 0.7f;
                SDL_SetRenderDrawColor(renderer_, 255, 160, 40, (Uint8)midAlpha);
                for (int iy = -(int)midR; iy <= (int)midR; iy += 2) {
                    int hw = (int)sqrtf(midR * midR - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 2};
                    SDL_RenderFillRect(renderer_, &row);
                }
                int outerAlpha = (int)(120 * (1.0f - p * 0.5f));
                SDL_SetRenderDrawColor(renderer_, 255, 100, 20, (Uint8)outerAlpha);
                for (int iy = -(int)r; iy <= (int)r; iy += 2) {
                    int hw = (int)sqrtf(r * r - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 2};
                    SDL_RenderFillRect(renderer_, &row);
                }
            } else {
                float p = (t - 0.3f) / 0.7f;
                float r = ex.radius * (1.0f - p * 0.1f);
                int alpha = (int)(140 * (1.0f - p));
                float thickness = 8.0f + 12.0f * p;
                SDL_SetRenderDrawColor(renderer_, 255, 120, 30, (Uint8)alpha);
                for (float a = 0; a < 2.0f * (float)M_PI; a += 0.05f) {
                    float cx = cosf(a), cy = sinf(a);
                    for (float d = r - thickness; d < r; d += 2.0f) {
                        SDL_Rect pt = {(int)(sp.x + cx * d) - 1, (int)(sp.y + cy * d) - 1, 3, 3};
                        SDL_RenderFillRect(renderer_, &pt);
                    }
                }
                float smokeR = r * 0.5f * (1.0f - p * 0.5f);
                int smokeAlpha = (int)(60 * (1.0f - p));
                SDL_SetRenderDrawColor(renderer_, 80, 60, 40, (Uint8)smokeAlpha);
                for (int iy = -(int)smokeR; iy <= (int)smokeR; iy += 3) {
                    int hw = (int)sqrtf(smokeR * smokeR - (float)(iy * iy));
                    SDL_Rect row = {(int)sp.x - hw, (int)sp.y + iy, hw * 2, 3};
                    SDL_RenderFillRect(renderer_, &row);
                }
            }
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
        for (auto& bd : blood_) {
            if (bloodTex_) {
                Vec2 sp = camera_.worldToScreen(bd.pos);
                int half = (int)(32 * bd.scale);
                SDL_Rect dst = {(int)(sp.x - half), (int)(sp.y - half), half * 2, half * 2};
                if (bd.type == DecalType::Scorch) {
                    SDL_SetTextureColorMod(bloodTex_, 20, 20, 20);
                    SDL_SetTextureAlphaMod(bloodTex_, 160);
                } else {
                    SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
                    SDL_SetTextureAlphaMod(bloodTex_, 180);
                }
                SDL_RenderCopyEx(renderer_, bloodTex_, nullptr, &dst,
                    bd.rotation * 180.0f / (float)M_PI, nullptr, SDL_FLIP_NONE);
                SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
                SDL_SetTextureAlphaMod(bloodTex_, 255);
            }
        }
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
            renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, 3.0f);
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
    }

    SDL_RenderPresent(renderer_);
}

void Game::renderSprite(SDL_Texture* tex, Vec2 worldPos, float angle, float scale) {
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    Vec2 sp = camera_.worldToScreen(worldPos);
    SDL_Rect dst = {
        (int)(sp.x - w * scale / 2),
        (int)(sp.y - h * scale / 2),
        (int)(w * scale),
        (int)(h * scale)
    };
    double deg = angle * 180.0 / M_PI;
    SDL_RenderCopyEx(renderer_, tex, nullptr, &dst, deg, nullptr, SDL_FLIP_NONE);
}

void Game::renderSpriteEx(SDL_Texture* tex, Vec2 worldPos, float angle, float scale, SDL_Color tint) {
    if (!tex) return;
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    renderSprite(tex, worldPos, angle, scale);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
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
            SDL_Texture* tex = nullptr;
            double tileAngle = 0.0;
            SDL_RendererFlip tileFlip = SDL_FLIP_NONE;

            // Deterministic hash for per-tile randomization
            unsigned int tileHash = (unsigned int)(x * 73856093u ^ y * 19349663u);

            if (tile == TILE_WALL) {
                tex = wallTex_;
            } else if (tile == TILE_GLASS) {
                tex = glassTex_;
            } else if (tile == TILE_DESK) {
                tex = deskTex_;
            } else if (tile == TILE_BOX) {
                tex = boxTex_;
            } else if (tile == TILE_GRAVEL) {
                tex = gravelTex_;
                // Randomize gravel rotation and flip for variety
                tileAngle = (tileHash % 4) * 90.0;
                if (tileHash & 0x100) tileFlip = (SDL_RendererFlip)(tileFlip | SDL_FLIP_HORIZONTAL);
                if (tileHash & 0x200) tileFlip = (SDL_RendererFlip)(tileFlip | SDL_FLIP_VERTICAL);
            } else {
                // Grass/floor tile — check neighbors for gravel transitions
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
            SDL_Rect dst = {(int)(sp.x - TILE_SIZE/2), (int)(sp.y - TILE_SIZE/2),
                           TILE_SIZE, TILE_SIZE};

            if (tex) {
                // Subtle color variation for grass/gravel to break repetition
                if (tile == TILE_GRASS || tile == TILE_FLOOR || tile == TILE_GRAVEL) {
                    int variation = (int)(tileHash % 30) - 15; // -15 to +14
                    Uint8 mod = (Uint8)std::max(220, std::min(255, 240 + variation));
                    SDL_SetTextureColorMod(tex, mod, mod, mod);
                }
                SDL_RenderCopyEx(renderer_, tex, nullptr, &dst,
                    tileAngle, nullptr, tileFlip);
                if (tile == TILE_GRASS || tile == TILE_FLOOR || tile == TILE_GRAVEL) {
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                }
            } else {
                SDL_Color c = {60, 60, 65, 255};
                if (tile == TILE_WALL) c = {100, 90, 80, 255};
                SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
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
                SDL_SetTextureAlphaMod(glassTileTex_, 50);
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
        char str[32];
        snprintf(str, sizeof(str), "BOMB x%d", activeBombs);
        drawText(str, 20, 100, 16, {255, 200, 100, 255});
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
        drawText("PARRY CD", SCREEN_W/2 - 40, SCREEN_H - 50, 14, {150, 150, 150, 255});
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
}

void Game::renderMainMenu() {
    // Background
    if (mainmenuBg_) {
        SDL_Rect dst = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(renderer_, mainmenuBg_, nullptr, &dst);
    }

    // Dark overlay
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 150);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color gray  = {150, 150, 150, 255};

    drawTextCentered("COLD START", SCREEN_H / 4, 48, cyan);

    const char* items[] = {"PLAY", "EDITOR", "MAPS", "PACKS", "CHARACTER", "CHARACTER EDITOR", "CONFIG", "QUIT"};
    int count = 8;
    int baseY = SCREEN_H / 2 - 100;
    int stepY = 34;

    for (int i = 0; i < count; i++) {
        SDL_Color c = (menuSelection_ == i) ? white : gray;
        char buf[64];
        if (menuSelection_ == i)
            snprintf(buf, sizeof(buf), "> %s <", items[i]);
        else
            snprintf(buf, sizeof(buf), "%s", items[i]);
        drawTextCentered(buf, baseY + i * stepY, 24, c);
    }

    // Show selected character name
    if (selectedChar_ >= 0 && selectedChar_ < (int)availableChars_.size()) {
        char charStr[128];
        snprintf(charStr, sizeof(charStr), "Character: %s", availableChars_[selectedChar_].name.c_str());
        drawTextCentered(charStr, SCREEN_H - 80, 14, {180, 180, 180, 255});
    }

    drawTextCentered("ENTER - Select   Arrow Keys - Navigate", SCREEN_H - 40, 14, gray);
}

void Game::renderConfigMenu() {
    SDL_SetRenderDrawColor(renderer_, 8, 8, 12, 235);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color title = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray = {150, 150, 150, 255};

    drawTextCentered("CONFIG", 70, 34, title);

    char line[128];
    int y = 170;
    int stepY = 52;

    SDL_Color c0 = (configSelection_ == 0) ? white : gray;
    snprintf(line, sizeof(line), "Map Width: %d", config_.mapWidth);
    drawTextCentered(line, y + stepY * 0, 22, c0);

    SDL_Color c1 = (configSelection_ == 1) ? white : gray;
    snprintf(line, sizeof(line), "Map Height: %d", config_.mapHeight);
    drawTextCentered(line, y + stepY * 1, 22, c1);

    SDL_Color c2 = (configSelection_ == 2) ? white : gray;
    snprintf(line, sizeof(line), "Player HP: %d", config_.playerMaxHp);
    drawTextCentered(line, y + stepY * 2, 22, c2);

    SDL_Color c3 = (configSelection_ == 3) ? white : gray;
    snprintf(line, sizeof(line), "Enemy Spawnrate: %.1fx", config_.spawnRateScale);
    drawTextCentered(line, y + stepY * 3, 22, c3);

    SDL_Color c4 = (configSelection_ == 4) ? white : gray;
    snprintf(line, sizeof(line), "Enemy HP: %.1fx", config_.enemyHpScale);
    drawTextCentered(line, y + stepY * 4, 22, c4);

    SDL_Color c5 = (configSelection_ == 5) ? white : gray;
    snprintf(line, sizeof(line), "Enemy Speed: %.1fx", config_.enemySpeedScale);
    drawTextCentered(line, y + stepY * 5, 22, c5);

    SDL_Color c6 = (configSelection_ == 6) ? white : gray;
    snprintf(line, sizeof(line), "Music Volume: %d%%", config_.musicVolume * 100 / 128);
    drawTextCentered(line, y + stepY * 6, 22, c6);

    SDL_Color c7 = (configSelection_ == 7) ? white : gray;
    snprintf(line, sizeof(line), "SFX Volume: %d%%", config_.sfxVolume * 100 / 128);
    drawTextCentered(line, y + stepY * 7, 22, c7);

    SDL_Color c8 = (configSelection_ == 8) ? white : gray;
    drawTextCentered(configSelection_ == 8 ? "> BACK <" : "BACK", y + stepY * 8, 22, c8);

    drawTextCentered("UP/DOWN select  LEFT/RIGHT change", SCREEN_H - 60, 16, gray);
}

void Game::renderPauseMenu() {
    // Semi-transparent overlay
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {150, 150, 150, 255};
    SDL_Color cyan  = {0, 255, 228, 255};

    drawTextCentered("PAUSED", SCREEN_H / 3, 40, cyan);

    int startY = SCREEN_H / 2 - 20;
    SDL_Color c0 = (menuSelection_ == 0) ? white : gray;
    SDL_Color c1 = (menuSelection_ == 1) ? white : gray;
    SDL_Color c2 = (menuSelection_ == 2) ? white : gray;
    SDL_Color c3 = (menuSelection_ == 3) ? white : gray;

    drawTextCentered(menuSelection_ == 0 ? "> RESUME <" : "RESUME", startY, 24, c0);

    char musBuf[64]; snprintf(musBuf, sizeof(musBuf), "Music Volume: %d%%", config_.musicVolume * 100 / 128);
    char sfxBuf[64]; snprintf(sfxBuf, sizeof(sfxBuf), "SFX Volume: %d%%", config_.sfxVolume * 100 / 128);
    if (menuSelection_ == 1) { char tmp[80]; snprintf(tmp, sizeof(tmp), "< %s >", musBuf); drawTextCentered(tmp, startY + 36, 24, c1); }
    else drawTextCentered(musBuf, startY + 36, 24, c1);
    if (menuSelection_ == 2) { char tmp[80]; snprintf(tmp, sizeof(tmp), "< %s >", sfxBuf); drawTextCentered(tmp, startY + 72, 24, c2); }
    else drawTextCentered(sfxBuf, startY + 72, 24, c2);

    drawTextCentered(menuSelection_ == 3 ? "> MAIN MENU <" : "MAIN MENU", startY + 108, 24, c3);
}

void Game::renderDeathScreen() {
    // Darken + red tint
    SDL_SetRenderDrawColor(renderer_, 80, 0, 0, 200);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {150, 150, 150, 255};
    SDL_Color red   = {255, 60, 60, 255};

    drawTextCentered("YOU DIED", SCREEN_H / 3, 48, red);

    // Show time survived
    char timeStr[64];
    int mins = (int)gameTime_ / 60;
    int secs = (int)gameTime_ % 60;
    snprintf(timeStr, sizeof(timeStr), "Time: %d:%02d", mins, secs);
    drawTextCentered(timeStr, SCREEN_H / 3 + 60, 20, white);

    SDL_Color c0 = (menuSelection_ == 0) ? white : gray;
    SDL_Color c1 = (menuSelection_ == 1) ? white : gray;

    drawTextCentered(menuSelection_ == 0 ? "> RETRY <" : "RETRY", SCREEN_H / 2 + 40, 24, c0);
    drawTextCentered(menuSelection_ == 1 ? "> MAIN MENU <" : "MAIN MENU", SCREEN_H / 2 + 80, 24, c1);
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
    for (auto& es : customMap_.enemySpawns) {
        EnemyType type = (es.enemyType == 1) ? EnemyType::Shooter : EnemyType::Melee;
        spawnEnemy({es.x, es.y}, type);
        customEnemiesTotal_++;
    }

    // Also generate spawn points for wave spawning if map has them
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
    SDL_SetRenderDrawColor(renderer_, 8, 8, 12, 240);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color title = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {150, 150, 150, 255};

    drawTextCentered("SELECT MAP", 60, 34, title);

    if (mapFiles_.empty()) {
        drawTextCentered("No .csm maps found in maps/ folder", SCREEN_H / 2, 20, gray);
        drawTextCentered("Use the Editor to create maps!", SCREEN_H / 2 + 30, 16, gray);
    } else {
        int y = 140;
        for (int i = 0; i < (int)mapFiles_.size(); i++) {
            SDL_Color c = (menuSelection_ == i) ? white : gray;
            // Extract filename from path
            std::string fname = mapFiles_[i];
            size_t slash = fname.find_last_of('/');
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            char buf[128];
            if (menuSelection_ == i)
                snprintf(buf, sizeof(buf), "> %s <", fname.c_str());
            else
                snprintf(buf, sizeof(buf), "%s", fname.c_str());
            drawTextCentered(buf, y + i * 36, 20, c);
        }
    }

    int backIdx = (int)mapFiles_.size();
    SDL_Color cb = (menuSelection_ == backIdx) ? white : gray;
    drawTextCentered(menuSelection_ == backIdx ? "> BACK <" : "BACK", SCREEN_H - 100, 20, cb);

    drawTextCentered("UP/DOWN select  ENTER confirm  BACKSPACE back", SCREEN_H - 40, 14, gray);
}

void Game::renderCharSelectMenu() {
    SDL_SetRenderDrawColor(renderer_, 8, 8, 12, 240);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color title = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {150, 150, 150, 255};

    drawTextCentered("SELECT CHARACTER", 60, 34, title);

    int y = 140;
    // Default option
    SDL_Color c0 = (menuSelection_ == 0) ? white : gray;
    drawTextCentered(menuSelection_ == 0 ? "> Default <" : "Default", y, 22, c0);
    y += 44;

    for (int i = 0; i < (int)availableChars_.size(); i++) {
        SDL_Color c = (menuSelection_ == i + 1) ? white : gray;
        char buf[128];
        if (menuSelection_ == i + 1)
            snprintf(buf, sizeof(buf), "> %s <", availableChars_[i].name.c_str());
        else
            snprintf(buf, sizeof(buf), "%s", availableChars_[i].name.c_str());
        drawTextCentered(buf, y, 22, c);

        // Show character detail sprite if selected
        if (menuSelection_ == i + 1 && availableChars_[i].detailSprite) {
            SDL_Rect detDst = {SCREEN_W - 300, 120, 200, 400};
            SDL_RenderCopy(renderer_, availableChars_[i].detailSprite, nullptr, &detDst);
        }

        // Show stats
        if (menuSelection_ == i + 1) {
            char stats[128];
            snprintf(stats, sizeof(stats), "HP:%d SPD:%.0f AMMO:%d",
                availableChars_[i].hp, availableChars_[i].speed, availableChars_[i].ammo);
            drawText(stats, 80, SCREEN_H - 120, 14, {180, 180, 180, 255});
        }
        y += 44;
    }

    int backIdx = (int)availableChars_.size() + 1;
    SDL_Color cb = (menuSelection_ == backIdx) ? white : gray;
    drawTextCentered(menuSelection_ == backIdx ? "> BACK <" : "BACK", SCREEN_H - 100, 20, cb);

    if (availableChars_.empty()) {
        drawTextCentered("No .cschar found in characters/ folder", SCREEN_H / 2 + 40, 14, gray);
    }
    drawTextCentered("UP/DOWN select  ENTER confirm  BACKSPACE back", SCREEN_H - 40, 14, gray);
}

void Game::renderCustomWinScreen() {
    SDL_SetRenderDrawColor(renderer_, 0, 40, 20, 200);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color green = {50, 255, 100, 255};

    drawTextCentered("LEVEL COMPLETE!", SCREEN_H / 3, 48, green);

    char timeStr[64];
    int mins = (int)gameTime_ / 60;
    int secs = (int)gameTime_ % 60;
    snprintf(timeStr, sizeof(timeStr), "Time: %d:%02d", mins, secs);
    drawTextCentered(timeStr, SCREEN_H / 3 + 60, 20, white);

    drawTextCentered("> MAIN MENU <", SCREEN_H / 2 + 60, 24, white);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Character Creator
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderCharCreator() {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 16, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    SDL_Color title = {0, 255, 228, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {150, 150, 150, 255};
    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color yellow = {255, 220, 50, 255};

    drawTextCentered("CHARACTER CREATOR", 30, 34, title);

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
    }
    fclose(f);
    printf("Config loaded from config.txt\n");
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
    drawTextCentered("MAP PACKS", SCREEN_H / 6, 32, {0, 200, 255, 255});

    if (availablePacks_.empty()) {
        drawTextCentered("No packs found", SCREEN_H / 2, 20, {180, 180, 180, 255});
        drawTextCentered("Place .cspack files in packs/ folder", SCREEN_H / 2 + 30, 16, {120, 120, 120, 255});
    }

    int baseY = SCREEN_H / 4 + 20;
    int stepY = 50;
    for (int i = 0; i < (int)availablePacks_.size(); i++) {
        SDL_Color col = (packSelectIdx_ == i) ? SDL_Color{255, 255, 100, 255} : SDL_Color{200, 200, 200, 255};
        std::string label = availablePacks_[i].name;
        if (!availablePacks_[i].creator.empty()) label += " by " + availablePacks_[i].creator;
        label += " (" + std::to_string(availablePacks_[i].maps.size()) + " levels)";
        drawTextCentered(label.c_str(), baseY + i * stepY, 20, col);

        // Description
        if (packSelectIdx_ == i && !availablePacks_[i].description.empty()) {
            drawTextCentered(availablePacks_[i].description.c_str(), baseY + i * stepY + 22, 14, {150, 150, 150, 255});
        }
    }

    // BACK option
    int backIdx = (int)availablePacks_.size();
    SDL_Color backCol = (packSelectIdx_ == backIdx) ? SDL_Color{255, 100, 100, 255} : SDL_Color{180, 180, 180, 255};
    drawTextCentered("BACK", baseY + backIdx * stepY + 10, 20, backCol);
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
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    drawTextCentered("LEVEL COMPLETE!", SCREEN_H / 3, 32, {50, 255, 100, 255});

    std::string prog = "Level " + std::to_string(currentPack_.currentMapIndex + 1) +
                       " / " + std::to_string(currentPack_.maps.size());
    drawTextCentered(prog.c_str(), SCREEN_H / 3 + 40, 18, {200, 200, 200, 255});

    int baseY = SCREEN_H / 2 + 20;
    SDL_Color col0 = (menuSelection_ == 0) ? SDL_Color{255, 255, 100, 255} : SDL_Color{200, 200, 200, 255};
    SDL_Color col1 = (menuSelection_ == 1) ? SDL_Color{255, 100, 100, 255} : SDL_Color{200, 200, 200, 255};

    const char* nextLabel = currentPack_.hasNextMap() ? "NEXT LEVEL" : "FINISH";
    drawTextCentered(nextLabel, baseY, 22, col0);
    drawTextCentered("QUIT PACK", baseY + 36, 22, col1);
}

void Game::renderPackComplete() {
    drawTextCentered("CAMPAIGN COMPLETE!", SCREEN_H / 3 - 20, 36, {255, 220, 50, 255});
    drawTextCentered(currentPack_.name.c_str(), SCREEN_H / 3 + 30, 22, {0, 200, 255, 255});

    if (!currentPack_.creator.empty()) {
        std::string byLine = "by " + currentPack_.creator;
        drawTextCentered(byLine.c_str(), SCREEN_H / 3 + 58, 16, {180, 180, 180, 255});
    }

    std::string levels = std::to_string(currentPack_.maps.size()) + " levels completed!";
    drawTextCentered(levels.c_str(), SCREEN_H / 2 + 10, 18, {200, 200, 200, 255});

    drawTextCentered("Press A / ENTER to continue", SCREEN_H * 2 / 3, 18, {150, 150, 150, 255});
}
