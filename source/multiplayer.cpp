#include "game.h"
#include "game_internal.h"
#include "discord_rpc.h"
#include <ctime>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__SWITCH__)
#include <switch.h>
#include <arpa/inet.h>
#elif defined(__WIIU__)
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#else
#ifndef __ANDROID__
#include <ifaddrs.h>
#endif
#include <arpa/inet.h>
#endif

static constexpr float SPLITSCREEN_ZOOM = 0.75f;

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

        // World rendering
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

        // Box fragments - fade out and shrink as they age
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

        // Render ALL local co-op players in this viewport
        for (int j = 0; j < 4; j++) {
            if (!coopSlots_[j].joined) continue;
            Player& cp = coopSlots_[j].player;
            if (cp.dead) continue;

            // Legs
            if (!legSprites_.empty() && cp.moving) {
                int idx = cp.legAnimFrame % (int)legSprites_.size();
                renderSprite(legSprites_[idx], cp.pos, cp.legRotation + (float)M_PI/2, 1.5f);
            }
            // Body - tint by slot color; flash white when invulnerable, blue when parrying
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

        // Remote players (other network clients + their sub-players)
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

        // Name tags + HP bars for other local co-op players
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

        // Crosshair: must render at zoom scale (worldToScreen coords)
        {
            Player& cp = coopSlots_[i].player;
            Vec2 aimDir = resolveAimDirection(cp, coopSlots_[i].aimInput);
            float crosshairDistance = (i == 0) ? 96.0f : 80.0f;
            // Smaller + translucent to match single-player; keep each player's
            // color so crosshairs stay distinguishable in splitscreen.
            SDL_Color chColor = pColors[i]; chColor.a = 120;
            renderAimCrosshair(camera_, cp, aimDir, crosshairDistance, chColor, 7);
        }

        // Per-slot HUD (bottom of viewport): restore 1:1 scale for UI overlay
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
            // Screen flash (pickups, explosions, etc.) - renderUI() isn't called in coop so draw it here
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

    // Minimap
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


// Multiplayer Integration

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

    // Enemy killed notification from host - kill locally on clients (with effects)
    net.onEnemyKilled = [this](uint32_t enemyIdx, uint8_t killerId) {
        // Credit the bomb counter if WE made this kill; otherwise just sync the death
        if (enemyIdx < enemies_.size() && enemies_[enemyIdx].alive) {
            auto& net2 = NetworkManager::instance();
            bool ours = (killerId == net2.localPlayerId());
            killEnemy(enemies_[enemyIdx], ours);
        }
        // Keep bossWaveActive_ in sync on clients after each boss kill
        if (bossWaveActive_) {
            bool anyBossAlive = false;
            for (auto& be : enemies_) {
                if (be.alive && isBossType(be.type)) { anyBossAlive = true; break; }
            }
            if (!anyBossAlive) bossWaveActive_ = false;
        }
    };

    // Host broadcast a config change (e.g. lobby settings update)
    net.onConfigSyncReceived = [this](const LobbySettings& settings) {
        lobbySettings_ = settings;
        currentRules_.pvpEnabled = settings.isPvp || settings.pvpEnabled;
        currentRules_.friendlyFire = settings.friendlyFire;
        currentRules_.upgradesShared = settings.upgradesShared;
        currentRules_.teamCount = settings.teamCount;
        printf("Game: Config sync received - isPvp=%d pvp=%d ff=%d shared=%d teams=%d\n",
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
        // No orbiting bomb found - create a launched one (fallback)
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

        // Death explosion + scorch
        {
            Vec2 dpos = victim ? victim->pos : player_.pos;
            spawnPlayerDeathEffect(dpos);
        }

        // Team-colored death burst
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
            if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
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
            if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
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

    // PvP host-authoritative damage
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
        // Allow even if bullet wasn't found locally - network ordering may differ
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

    // Receive authoritative HP from host - update local player if it's ours
    net.onPlayerHpSync = [this](uint8_t playerId, int hp, int maxHp, uint8_t killerId) {
        auto& net = NetworkManager::instance();
        if (playerId == net.localPlayerId()) {
            player_.hp    = hp;
            player_.maxHp = maxHp;
            if (hp <= 0 && !player_.dead) {
                // Host confirmed we're dead
                player_.dead = true;
                if (sfxHurt_)  playSFX(sfxHurt_, config_.sfxVolume / 4);
                if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
                camera_.addShake(4.0f);
            } else if (hp < player_.maxHp) {
                camera_.addShake(1.8f);
                if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
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
        // Clients don't run updateSpawning(), so sync the boss wave flag here
        bool isBoss = false;
        for (int bw : BOSS_WAVES) if (waveNum == bw) { isBoss = true; break; }
        if (isBoss) bossWaveActive_ = true;
    };

    net.onEnemyBulletSpawned = [this](Vec2 pos, Vec2 vel) {
        // Clients only - spawn the bullet locally for visuals and local collision
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
        if (sfxEnemyShoot_) playSFX(sfxEnemyShoot_, config_.sfxVolume * 2 / 5);
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
#if !defined(__SWITCH__) && !defined(__WIIU__)
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
                // Custom map was sent - write temp file and load it
                std::string tmpPath = "maps/_mp_recv.csm";
                mkdir("maps", 0755);
                FILE* f = fopen(tmpPath.c_str(), "wb");
                if (f) {
                    fwrite(customMapData.data(), 1, customMapData.size(), f);
                    fclose(f);
                }
                startCustomMapMultiplayer(tmpPath);
            } else {
                mapSrand(mapSeed);             // same seed as host -> same map
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

        // Splitscreen: init coopSlots for local sub-players (client side)
        {
            int joined = 0;
            for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;
            coopPlayerCount_ = joined;
            if (coopPlayerCount_ > 1) {
                const int n2 = coopPlayerCount_;
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
                    SDL_Rect vp2 = coopViewport(si, n2);
                    int vw2 = (int)(vp2.w / SPLITSCREEN_ZOOM);
                    int vh2 = (int)(vp2.h / SPLITSCREEN_ZOOM);
                    coopSlots_[i].camera.worldW = map_.worldWidth();
                    coopSlots_[i].camera.worldH = map_.worldHeight();
                    coopSlots_[i].camera.viewW  = vw2;
                    coopSlots_[i].camera.viewH  = vh2;
                    coopSlots_[i].camera.pos    = {coopSlots_[i].player.pos.x - vw2/2.f,
                                                    coopSlots_[i].player.pos.y - vh2/2.f};
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
        // Client received mod data from host - gate by per-type user settings
        if (!config_.acceptWorkshopMods && !config_.acceptLocalMods) {
            printf("Game: Mod sync skipped (both workshop and local mods disabled)\n");
            return;
        }
        printf("Game: Received mod sync from host (workshop=%d local=%d)\n",
               (int)config_.acceptWorkshopMods, (int)config_.acceptLocalMods);
        auto& mm = ModManager::instance();
        mm.deserializeAndInstallMods(modData, config_.saveIncomingModsPermanently,
                                     config_.acceptWorkshopMods, config_.acceptLocalMods);
        applyModOverrides();
        printf("Game: Mod sync complete\n");
    };

    // Admin / Lives callbacks
    net.onAdminKicked = [this](uint8_t targetId) {
        auto& net2 = NetworkManager::instance();
        if (targetId == net2.localPlayerId()) {
            net2.disconnect();
            playMenuMusic();
            disconnectReason_ = "You were kicked from the server.";
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
        // Connection lost - return to main menu
        if (state_ == GameState::MultiplayerGame ||
            state_ == GameState::MultiplayerPaused ||
            state_ == GameState::MultiplayerDead ||
            state_ == GameState::MultiplayerSpectator) {
            printf("Lost connection to host, returning to menu\n");
            playMenuMusic();
            disconnectReason_ = "Connection to host lost.";
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
        return;
    }

    syncLocalCharacterSelection();

    // NOTE: net.update(dt) is already called in run() every frame - do NOT call again here

    // Send local player state at fixed rate
    if (net.isInGame()) {
        netStateSendTimer_ -= dt;
        if (netStateSendTimer_ <= 0) {
            netStateSendTimer_ = 1.0f / 30.0f; // 30 Hz - packets arrive every 33ms, lerp takes 42ms so always interpolating

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

            // Splitscreen: send sub-player states to other clients
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

        // Match timer (visible on all clients; only host acts on expiry)
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
    net.setUpnpEnabled(config_.enableUpnp);
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
#if defined(__SWITCH__)
    // libnx: nifmGetCurrentIpAddress
    u32 ip = 0;
    nifmInitialize(NifmServiceType_User);
    nifmGetCurrentIpAddress(&ip);
    nifmExit();
    if (ip == 0) return "N/A";
    char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return buf;
#elif defined(__WIIU__)
    // UDP connect trick: OS fills in the source address without sending a packet
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "N/A";
    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    dst.sin_addr.s_addr = inet_addr("8.8.8.8");
    if (connect(sock, (struct sockaddr*)&dst, sizeof(dst)) != 0) { close(sock); return "N/A"; }
    struct sockaddr_in local = {};
    socklen_t len = sizeof(local);
    getsockname(sock, (struct sockaddr*)&local, &len);
    close(sock);
    char* ip = inet_ntoa(local.sin_addr);
    return ip ? ip : "N/A";
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
#ifdef __ANDROID__
    return "N/A";
#else
    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return "N/A";
    std::string result = "N/A";
    for (auto* ifa = addrs; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        auto* sa = (struct sockaddr_in*)ifa->ifa_addr;
        char* ip = inet_ntoa(sa->sin_addr);
        if (strcmp(ip, "127.0.0.1") == 0) continue;
        result = ip;
        break;
    }
    freeifaddrs(addrs);
    return result;
#endif
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
        // net.players() counts network slots (1 per machine); add local sub-players
        // so splitscreen players on this machine each contribute to the shared pool.
        int totalPlayers = (int)net.players().size() + std::max(0, coopPlayerCount_ - 1);
        sharedLives_ = currentRules_.lives * totalPlayers;
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

    // Send enabled mod data to all clients before starting the game.
    // Workshop mods (hash-verified) and local mods are sent as separate blobs
    // so the receiving client can gate each type independently.
    {
        auto& mm = ModManager::instance();
        // Workshop mods - always send if any are enabled (clients verify hashes)
        auto workshopBlob = mm.serializeEnabledMods(ModSource::Workshop);
        if (!workshopBlob.empty()) {
            net.sendModSync(workshopBlob);
        }
        // Local mods - only send if host has them and they're relevant
        auto localBlob = mm.serializeEnabledMods(ModSource::Local);
        if (!localBlob.empty()) {
            net.sendModSync(localBlob);
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

            // Splitscreen: init coopSlots for local sub-players (custom map)
            {
                int joined = 0;
                for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;
                coopPlayerCount_ = joined;
                if (coopPlayerCount_ > 1) {
                    const int n2 = coopPlayerCount_;
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
                        SDL_Rect vp2 = coopViewport(si, n2);
                        int vw2 = (int)(vp2.w / SPLITSCREEN_ZOOM);
                        int vh2 = (int)(vp2.h / SPLITSCREEN_ZOOM);
                        coopSlots_[i].camera.worldW = map_.worldWidth();
                        coopSlots_[i].camera.worldH = map_.worldHeight();
                        coopSlots_[i].camera.viewW  = vw2;
                        coopSlots_[i].camera.viewH  = vh2;
                        coopSlots_[i].camera.pos    = {coopSlots_[i].player.pos.x - vw2/2.f,
                                                        coopSlots_[i].player.pos.y - vh2/2.f};
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

    // Start the game - use generated map
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

    // Splitscreen: initialise coopSlots for local sub-players in MP
    {
        int joined = 0;
        for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) joined++;
        coopPlayerCount_ = joined;
        if (coopPlayerCount_ > 1) {
            const int n2 = coopPlayerCount_;
            const Vec2 off[4] = {{-80,0},{80,0},{0,-80},{0,80}};
            int si = 0;
            for (int i = 0; i < 4; i++) {
                if (!coopSlots_[i].joined) continue;
                if (si == 0) {
                    // Slot 0 = primary player - already initialised by startGame()
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
                SDL_Rect vp2 = coopViewport(si, n2);
                int vw2 = (int)(vp2.w / SPLITSCREEN_ZOOM);
                int vh2 = (int)(vp2.h / SPLITSCREEN_ZOOM);
                coopSlots_[i].camera.worldW = map_.worldWidth();
                coopSlots_[i].camera.worldH = map_.worldHeight();
                coopSlots_[i].camera.viewW  = vw2;
                coopSlots_[i].camera.viewH  = vh2;
                coopSlots_[i].camera.pos    = {coopSlots_[i].player.pos.x - vw2/2.f,
                                                coopSlots_[i].player.pos.y - vh2/2.f};
                si++;
            }
        }
    }
}


// Discord Rich Presence

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
        state_ == GameState::PackSelect) {
        act.details = "Main Menu";
        act.state   = "Browsing menus";
    }
    else if (state_ == GameState::Editor      || state_ == GameState::EditorConfig ||
             state_ == GameState::CharCreator) {
        act.details = "Map Editor";
        act.state   = "Building a level";
    }
    else if (state_ == GameState::Lobby) {
        int total = (int)net.players().size();  // everyone connected to the lobby
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
