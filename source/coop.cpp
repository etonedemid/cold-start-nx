#include "game.h"
#include "game_internal.h"
#include <ctime>


void Game::scanMapPacks() {
    availablePacks_.clear();
    // Scan several possible directories
    std::vector<std::string> dirs = {"packs", "maps/packs", "romfs/packs", "romfs:/packs",
                                     "fs:/vol/content/packs", "fs:/vol/content/maps/packs"};
    for (auto& d : dirs) {
        auto found = ::scanMapPacks(d);
        for (auto& p : found) availablePacks_.push_back(std::move(p));
    }
    printf("Found %d map pack(s)\n", (int)availablePacks_.size());
}

void Game::renderPackSelectMenu() {
    ui_.drawDesktop();

    const int padX = 14;
    const int btnH = 26;
    const int btnGap = 6;
    const int winW = 560;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = 60;
    const int winH = SCREEN_H - winY - 60;
    ui_.drawWin98Window(winX, winY, winW, winH, "Map Packs");

    int baseY = winY + UI::W98::TitleH + 10;
    int bx = winX + padX;
    int bw = winW - padX * 2;

    if (availablePacks_.empty()) {
        ui_.drawText("No packs found", bx, baseY + 10, 14, UI::W98::Black);
        ui_.drawText("Place .cspack files in packs/ folder", bx, baseY + 30, 12, UI::W98::Shadow);
    }

    for (int i = 0; i < (int)availablePacks_.size(); i++) {
        int y = baseY + i * (btnH + btnGap);
        bool sel = (packSelectIdx_ == i);
        std::string label = availablePacks_[i].name;
        if (!availablePacks_[i].creator.empty()) label += " by " + availablePacks_[i].creator;
        label += " (" + std::to_string(availablePacks_[i].maps.size()) + " levels)";
        if (ui_.win98Button(i, label.c_str(), bx, y, bw, btnH, sel)) {
            packSelectIdx_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) packSelectIdx_ = i;
    }

    // BACK button pinned to bottom
    int backIdx = (int)availablePacks_.size();
    bool backSel = (packSelectIdx_ == backIdx);
    int backY = winY + winH - btnH - 10;
    ui_.drawWin98Bevel(bx, backY - 6, bw, 2, false);
    if (ui_.win98Button(63, "Back", bx, backY, bw, btnH, backSel)) {
        packSelectIdx_ = backIdx;
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 63 && !usingGamepad_) packSelectIdx_ = backIdx;

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Select a map pack");
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
    map_.noCollide = customMap_.tileNoCollide;
    map_.noCollide.resize(map_.tiles.size(), 0);

    enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
    bombs_.clear(); explosions_.clear(); debris_.clear();
    blood_.clear(); tileBlood_.clear(); boxFragments_.clear();
    crates_.clear(); pickups_.clear();
    vehicles_.clear(); inVehicle_ = false; vehicleIdx_ = -1;
    upgrades_.reset();
    sandboxMode_ = false;
    crateSpawnTimer_ = 20.0f;

    // Clear layer images from any previous map
    bgImageTex_   = customMap_.bgImagePath.empty()  ? nullptr : Assets::instance().loadRelTex(customMap_.bgImagePath);
    topImageTex_  = customMap_.topImagePath.empty() ? nullptr : Assets::instance().loadRelTex(customMap_.topImagePath);
    topLayerAlpha_ = 1.0f;
    for (int _i = 0; _i < 8; _i++) {
        customTileTextures_[_i] = nullptr;
        if (!customMap_.customTilePaths[_i].empty())
            customTileTextures_[_i] = Assets::instance().tex(customMap_.customTilePaths[_i]);
    }

    waveNumber_ = 0; waveEnemiesLeft_ = 0; waveActive_ = false;
    bossWaveActive_ = false; lastBossWaveNum_ = -1;
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
            pushOutCollisionZones(crate.pos, 24.0f);
            crate.contents = rollRandomUpgrade();
            crates_.push_back(crate);
        } else if (isBystanderSpawn(es.enemyType)) {
            // Story bystanders are single-player only; skip in co-op.
            continue;
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
    ui_.drawDarkOverlay(160);

    const int padX = 14;
    const int btnH = 26;
    const int btnGap = 6;
    const int winW = 360;
    const int winH = UI::W98::TitleH + 14 + 20 + 14 + 2 + 14 + 2 * (btnH + btnGap) + 10;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Level Complete!");

    std::string prog = "Level " + std::to_string(currentPack_.currentMapIndex + 1) +
                       " / " + std::to_string(currentPack_.maps.size());

    int cy = winY + UI::W98::TitleH + 14;
    ui_.drawTextCentered(prog.c_str(), cy, 16, UI::W98::Black);
    cy += 20;
    ui_.drawWin98Bevel(winX + padX, cy, winW - padX * 2, 2, false);
    cy += 14;

    int bx = winX + padX;
    int bw = winW - padX * 2;
    const char* nextLabel = currentPack_.hasNextMap() ? "Next Level" : "Finish";
    if (ui_.win98Button(0, nextLabel, bx, cy, bw, btnH, menuSelection_ == 0)) {
        menuSelection_ = 0; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 0 && !usingGamepad_) menuSelection_ = 0;
    cy += btnH + btnGap;

    if (ui_.win98Button(1, "Quit Pack", bx, cy, bw, btnH, menuSelection_ == 1)) {
        menuSelection_ = 1; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 1 && !usingGamepad_) menuSelection_ = 1;

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Level complete");
}

// Local Co-op (splitscreen, up to 4 players)

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
    
    // Initialize lives tracking.
    // Non-shared: each player has their own pool of lives, tracked as a combined
    // total (lives * playerCount) so every player gets their fair share.
    // Shared: one explicit shared pool also sized lives * playerCount.
    if (currentRules_.lives > 0 && !currentRules_.sharedLives) {
        localLives_ = currentRules_.lives * coopPlayerCount_;
    } else {
        localLives_ = -1;
    }
    if (currentRules_.lives > 0 && currentRules_.sharedLives) {
        sharedLives_ = currentRules_.lives * coopPlayerCount_;
    } else {
        sharedLives_ = -1;
    }
    
    waveNumber_ = 0; waveEnemiesLeft_ = 0; waveActive_ = false;
    bossWaveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE; waveSpawnTimer_ = 0;
    enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
    bombs_.clear(); explosions_.clear(); debris_.clear();
    blood_.clear(); tileBlood_.clear(); boxFragments_.clear();
    crates_.clear(); pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    sandboxMode_ = false;
    map_.generate(config_.mapWidth, config_.mapHeight);
    invalidateMinimapCache();
    map_.findSpawnPoints();

    const int n  = coopPlayerCount_;
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
        // Per-slot camera dimensions: derive from the actual viewport rect so the
        // 3-player layout (P3 gets a full-width bottom half) is handled correctly.
        SDL_Rect vp = coopViewport(si, n);
        int vw = (n > 1) ? (int)(vp.w / SPLITSCREEN_ZOOM) : SCREEN_W;
        int vh = (n > 1) ? (int)(vp.h / SPLITSCREEN_ZOOM) : SCREEN_H;
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
    playActionMusic();
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
        coopSlots_[i].camera.shakeScale = config_.shakeScale;
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

    // All dead + no lives -> game over
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
    ui_.drawDesktop();

    // Main window
    const int winW = SCREEN_W - 80;
    const int winH = SCREEN_H - 80;
    const int winX = 40;
    const int winY = 40;
    ui_.drawWin98Window(winX, winY, winW, winH, "Local Multiplayer");

    // --- Player slot cards ---
    static const SDL_Color slotColors[4] = {
        {80, 220, 255, 255}, {255, 220, 50, 255},
        {80, 220, 80,  255}, {255, 140, 50, 255}};
    static const char* slotLabels[4] = {"P1","P2","P3","P4"};
    const int slotW = 220, slotH = 100, gapX = 10;
    const int totalW = 4*slotW + 3*gapX;
    const int startX = winX + (winW - totalW) / 2;
    const int slotY  = winY + UI::W98::TitleH + 14;
    char buf[128];

    for (int i = 0; i < 4; i++) {
        int x = startX + i*(slotW+gapX);
        SDL_Color col = slotColors[i];
        // Win98-style sunken bevel for each slot
        ui_.drawWin98Bevel(x, slotY, slotW, slotH, false);

        // Slot label row (raised sub-header)
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_,
            coopSlots_[i].joined ? col.r/3 : 192,
            coopSlots_[i].joined ? col.g/3 : 192,
            coopSlots_[i].joined ? col.b/3 : 192, 255);
        SDL_Rect hdr = {x+2, slotY+2, slotW-4, 22};
        SDL_RenderFillRect(renderer_, &hdr);
        SDL_Color lblCol = coopSlots_[i].joined ? UI::W98::White : UI::W98::Shadow;
        drawText(slotLabels[i], x + slotW/2 - 8, slotY + 4, 16, lblCol);

        if (coopSlots_[i].joined) {
            drawText(coopSlots_[i].username.c_str(), x + 8, slotY + 32, 14, UI::W98::Black);
            if (coopSlots_[i].joyInstanceId < 0) {
                drawText("KB+Mouse", x + 8, slotY + 52, 12, UI::W98::Shadow);
            } else {
                SDL_GameController* gc = SDL_GameControllerFromInstanceID(coopSlots_[i].joyInstanceId);
                const char* gcName = gc ? SDL_GameControllerName(gc) : "Gamepad";
                char devBuf[32]; snprintf(devBuf, sizeof(devBuf), "%.30s", gcName);
                drawText(devBuf, x + 8, slotY + 52, 11, UI::W98::Shadow);
            }
            drawText("READY", x + 8, slotY + 72, 13, SDL_Color{0,128,0,255});
        } else {
            SDL_Color dim = UI::W98::Shadow;
            if (i == 0)
                drawText("Auto-joined", x + 8, slotY + 42, 12, dim);
            else {
                drawText("Press A on gamepad", x + 8, slotY + 36, 12, dim);
                drawText("to join", x + 8, slotY + 54, 12, dim);
            }
        }
    }

    int joined = 0;
    for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;

    // --- Settings panel below slots ---
    const int settWinX = winX + 14;
    const int settWinY = slotY + slotH + 14;
    const int settWinW = winW - 28;
    const int settWinH = winH - (settWinY - winY) - 50;
    ui_.drawWin98Bevel(settWinX, settWinY, settWinW, settWinH, false);

    int sy = settWinY + 10;
    int labelX = settWinX + 14;
    int valueX = settWinX + 220;

    snprintf(buf, sizeof(buf), "%d player%s joined", joined, joined == 1 ? "" : "s");
    drawText(buf, labelX, sy, 14, joined >= 1 ? SDL_Color{0,128,0,255} : UI::W98::Shadow);
    sy += 24;

    ui_.drawWin98Bevel(settWinX + 4, sy, settWinW - 8, 2, false);
    sy += 10;

    auto drawSetting = [&](const char* label, const char* value) {
        drawText(label, labelX, sy, 13, UI::W98::Black);
        drawText(value, valueX, sy, 13, UI::W98::Black);
        sy += 22;
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

    ui_.drawWin98StatusBar(SCREEN_H - 26, "TAB: Cycle Mode  |  Enter/Start: Begin  |  Esc/B: Back");
}

void Game::renderLocalCoopGame() {
    // LocalCoopGame is unreachable (no menu entry transitions to that state).
    // Delegate to the multiplayer splitscreen renderer which is the live path.
    renderMultiplayerSplitscreen();
}

void Game::renderPackComplete() {
    ui_.drawDesktop();

    const int padX = 14;
    const int btnH = 26;
    const int winW = 400;
    // TitleH + pad + pack name + creator (optional) + levels line + sep + btn + pad
    const int winH = UI::W98::TitleH + 14 + 22 + 20 + 20 + 14 + 2 + 14 + btnH + 14;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Campaign Complete!");

    int cy = winY + UI::W98::TitleH + 14;
    ui_.drawTextCentered(currentPack_.name.c_str(), cy, 18, UI::W98::Black);
    cy += 22;

    if (!currentPack_.creator.empty()) {
        std::string byLine = "by " + currentPack_.creator;
        ui_.drawTextCentered(byLine.c_str(), cy, 13, UI::W98::Shadow);
    }
    cy += 20;

    std::string levels = std::to_string(currentPack_.maps.size()) + " levels completed!";
    ui_.drawTextCentered(levels.c_str(), cy, 14, UI::W98::Black);
    cy += 20;

    ui_.drawWin98Bevel(winX + padX, cy, winW - padX * 2, 2, false);
    cy += 14;

    if (ui_.win98Button(0, "Continue", winX + padX, cy, winW - padX * 2, btnH, true)) {
        confirmInput_ = true;
    }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Campaign complete!");
}

// Multiplayer Splitscreen Rendering

