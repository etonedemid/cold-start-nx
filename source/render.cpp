#include "game.h"
#include "game_internal.h"
#include <iterator>
#include <list>
#include <random>
#include <vector>
#include <iostream>
void Game::render() {
    bool gameplayView =
        state_ == GameState::Playing || state_ == GameState::Paused || state_ == GameState::Dead ||
        state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused || state_ == GameState::CustomDead || state_ == GameState::CustomWin ||
        state_ == GameState::PlayingPack || state_ == GameState::PackPaused || state_ == GameState::PackDead || state_ == GameState::PackLevelWin ||
        state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused || state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator ||
        state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused || state_ == GameState::LocalCoopDead;

#ifdef __ANDROID__
    bool usePostFX = false;  // PostFX sceneTarget_ compositing breaks rendering on Android
#else
    bool usePostFX = sceneTarget_ && !isSplitscreenActive();
#endif

    if (usePostFX) SDL_SetRenderTarget(renderer_, sceneTarget_);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

#ifndef __SWITCH__
    // During active gameplay: hide OS cursor and confine it to the window.
    // During any menu/pause/dead state: restore cursor and release window grab.
    {
        bool inGame = (state_ == GameState::Playing ||
                       state_ == GameState::PlayingCustom ||
                       state_ == GameState::PlayingPack ||
                       state_ == GameState::MultiplayerGame ||
                       state_ == GameState::LocalCoopGame);
        SDL_ShowCursor(inGame ? SDL_DISABLE : SDL_ENABLE);
        SDL_SetWindowGrab(window_, inGame ? SDL_TRUE : SDL_FALSE);
    }
#endif

    switch (state_) {
    case GameState::BiosIntro:
        renderBiosIntro();
        break;
    case GameState::LoginScreen:
        renderLoginScreen();
        break;
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
    case GameState::Workshop:
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
                bool showDash   = e.isDashing    || e.netIsDashing;
                bool showCharge = e.dashCharging || e.netDashCharging;

                // Legs under body - all enemy types
                {
                    auto& eLegs = !enemyLegSprites_.empty() ? enemyLegSprites_ : legSprites_;
                    if (!eLegs.empty()) {
                        int li = e.legAnimFrame % (int)eLegs.size();
                        float legSc = e.renderScale * (isShooterEnemyType(e.type) ? 0.33f : 0.5f);
                        renderSpriteEx(eLegs[li], e.pos, e.legRotation+(float)M_PI/2, legSc, enemyBaseTint(e.type));
                    }
                }

                // Body tint
                if (showDash) {
                    // Strike frame: bright white-orange flash
                    Uint8 fl = (Uint8)(180 + (int)(sinf(gameTime_ * 80.0f) * 75.0f + 75.0f) / 2);
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, {255, fl, 60, 255});
                } else if (showCharge) {
                    // Wind-up: pulsing angry red
                    Uint8 pulse = (Uint8)(80 + (int)(sinf(gameTime_ * 28.0f) * 80.0f + 80.0f) / 2);
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale, {255, pulse, pulse, 255});
                } else if (e.damageFlash > 0 || e.flashTimer > 0) {
                    Uint8 redness = (Uint8)(255.0f * std::min(1.0f, e.damageFlash + e.flashTimer));
                    Uint8 other   = (Uint8)(15.0f * (1.0f - std::min(1.0f, e.damageFlash + e.flashTimer)));
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale,
                                   {255, (Uint8)(other + redness/12), (Uint8)(other + redness/12), 255});
                } else {
                    float hpRatio = std::max(0.0f, std::min(1.0f, e.maxHp > 0.0f ? e.hp / e.maxHp : 1.0f));
                    SDL_Color base = enemyBaseTint(e.type);
                    renderSpriteEx(tex, e.pos, e.rotation + M_PI/2, drawScale,
                                   {base.r, (Uint8)(base.g * hpRatio), (Uint8)(base.b * hpRatio), 255});
                }
            }
        }

        // Player legs
        if (!player_.dead && !legSprites_.empty() && !inVehicle_) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + M_PI/2, 1.5f);
        }

        // Player body
        if (!player_.dead && !inVehicle_) {
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
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
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
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

        // Crates, Pickups & Vehicles
        renderVehicles();
        renderCrates();
        renderPickups();
        renderRemotePlayers();

        // UI Layer
        renderWallOverlay();
        renderRoofOverlay();
        renderShadingPass();
        renderUI();

        if (state_ == GameState::Paused)   renderPauseMenu();
        if (state_ == GameState::Workshop) renderWorkshopMenu();
        if (state_ == GameState::Dead)     renderDeathScreen();
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
                bool showDash   = e.isDashing    || e.netIsDashing;
                bool showCharge = e.dashCharging || e.netDashCharging;
                auto& eLegs = !enemyLegSprites_.empty() ? enemyLegSprites_ : legSprites_;
                if (!eLegs.empty()) {
                    int li = e.legAnimFrame % (int)eLegs.size();
                    renderSpriteEx(eLegs[li], e.pos, e.legRotation+(float)M_PI/2, 1.5f, enemyBaseTint(e.type));
                }
                if (showDash) {
                    Uint8 fl = (Uint8)(180+(int)(sinf(gameTime_*80.0f)*75.0f+75.0f)/2);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale, {255,fl,60,255});
                } else if (showCharge) {
                    Uint8 pulse = (Uint8)(80+(int)(sinf(gameTime_*28.0f)*80.0f+80.0f)/2);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale, {255,pulse,pulse,255});
                } else if (e.damageFlash > 0 || e.flashTimer > 0) {
                    Uint8 redness = (Uint8)(255.0f*std::min(1.0f,e.damageFlash+e.flashTimer));
                    Uint8 other   = (Uint8)(15.0f*(1.0f-std::min(1.0f,e.damageFlash+e.flashTimer)));
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale,
                                   {255,(Uint8)(other+redness/12),(Uint8)(other+redness/12),255});
                } else {
                    float hpRatio = std::max(0.0f,std::min(1.0f,e.maxHp>0?e.hp/e.maxHp:1.0f));
                    SDL_Color base = enemyBaseTint(e.type);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale,
                                   {base.r,(Uint8)(base.g*hpRatio),(Uint8)(base.b*hpRatio),255});
                }
            }
        }
        // Player rendering (same as Playing, plus cutscene visual overrides)
        if (!player_.dead && !legSprites_.empty() && !inVehicle_ && player_.csVisible) {
            int idx = player_.legAnimFrame % (int)legSprites_.size();
            Uint8 legA = (Uint8)(player_.csAlpha * 255);
            SDL_SetTextureAlphaMod(legSprites_[idx], legA);
            renderSprite(legSprites_[idx], player_.pos, player_.legRotation + (float)M_PI/2,
                         1.5f * player_.csScale);
            SDL_SetTextureAlphaMod(legSprites_[idx], 255);
        }
        if (!player_.dead && !inVehicle_ && player_.csVisible) {
            if (!playerSprites_.empty()) {
                int idx = player_.animFrame % (int)playerSprites_.size();
                Vec2 bodyPos = player_.pos + Vec2::fromAngle(player_.rotation) * 6.0f;
                float sc = 1.5f * player_.csScale;
                Uint8 ba = (Uint8)(player_.csAlpha * 255);
                if (player_.csFlashAmt > 0.01f) {
                    SDL_Color tint = {
                        (Uint8)(255*(1-player_.csFlashAmt) + player_.csFlashR*player_.csFlashAmt),
                        (Uint8)(255*(1-player_.csFlashAmt) + player_.csFlashG*player_.csFlashAmt),
                        (Uint8)(255*(1-player_.csFlashAmt) + player_.csFlashB*player_.csFlashAmt),
                        ba};
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, sc, tint);
                } else if (player_.invulnerable && ((int)(player_.invulnTimer * 10) % 2 == 0)) {
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, sc,
                                   {255, 255, 255, ba});
                } else if (player_.isParrying) {
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, sc,
                                   {128, 200, 255, ba});
                } else if (ba < 255) {
                    renderSpriteEx(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, sc,
                                   {255, 255, 255, ba});
                } else {
                    renderSprite(playerSprites_[idx], bodyPos, player_.rotation + (float)M_PI/2, sc);
                }
            }
        } else if (!inVehicle_ && !playerDeathSprites_.empty() && player_.csVisible) {
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
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
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
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        renderVehicles();
        renderCrates();
        renderPickups();
        renderBystanders();
        renderRemotePlayers();
        renderWallOverlay();
        renderRoofOverlay();
        renderShadingPass();

        // Goal indicator - drawn before UI so HUD sits on top
        if (playingCustomMap_ && customGoalOpen_) {
            MapTrigger* goal = customMap_.findEndTrigger();
            if (goal) {
                Vec2 gp = camera_.worldToScreen({goal->x, goal->y});
                SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 150);
                SDL_Rect gr = {(int)(gp.x - goal->width/2), (int)(gp.y - goal->height/2),
                              (int)goal->width, (int)goal->height};
                SDL_RenderFillRect(renderer_, &gr);
                ui_.drawText("EXIT", (int)gp.x - ui_.textWidth("EXIT", 16)/2, (int)gp.y - 8, 16, {50, 255, 100, 255});
            }
        }

        // Story cutscene world-space actors (between world and HUD).
        // Actors live in map-world coordinates, so render them with the same
        // camera as the world (cutscene CameraShake adds its own offset).
        if (csPlay_.active) {
            SDL_Texture* pbody = playerSprites_.empty() ? nullptr : playerSprites_[0];
            // Pick the animated leg frame for the Player actor.
            SDL_Texture* plegs = legSprites_.empty() ? nullptr : legSprites_[0];
            if (!legSprites_.empty() && csPlay_.cutscene) {
                const auto& actors = csPlay_.cutscene->actors;
                const auto& states = csPlay_.actorStates;
                for (int i = 0; i < (int)actors.size() && i < (int)states.size(); i++) {
                    if (actors[i].type == CsActorType::Player) {
                        int fr = states[i].legAnimFrame % (int)legSprites_.size();
                        plegs = legSprites_[fr];
                        break;
                    }
                }
            }
            float ccx = camera_.pos.x - camera_.shakeOffset.x + csPlay_.cam.shakeX;
            float ccy = camera_.pos.y - camera_.shakeOffset.y + csPlay_.cam.shakeY;
            csPlay_.renderActors(renderer_, ccx, ccy, 1.0f,
                                 pbody, plegs, enemySprite_);
        }

        renderUI();

        // Story cutscene full-screen overlay (cinematic bars, dialog, fade) over HUD
        if (csPlay_.active)
            csPlay_.renderOverlay(renderer_, SCREEN_W, SCREEN_H, storyCutscenes_);

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
            if (tex) {
                auto& eLegs = !enemyLegSprites_.empty() ? enemyLegSprites_ : legSprites_;
                if (!eLegs.empty()) {
                    int li = e.legAnimFrame % (int)eLegs.size();
                    renderSpriteEx(eLegs[li], e.pos, e.legRotation+(float)M_PI/2, 1.5f, enemyBaseTint(e.type));
                }
                bool showDash = e.isDashing || e.netIsDashing;
                bool showCharge = e.dashCharging || e.netDashCharging;
                if (showDash) {
                    Uint8 fl = (Uint8)(180+(int)(sinf(gameTime_*80.0f)*75.0f+75.0f)/2);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale, {255,fl,60,255});
                } else if (showCharge) {
                    Uint8 pulse = (Uint8)(80+(int)(sinf(gameTime_*28.0f)*80.0f+80.0f)/2);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale, {255,pulse,pulse,255});
                } else {
                    float hpRatio = std::max(0.0f,std::min(1.0f,e.maxHp>0?e.hp/e.maxHp:1.0f));
                    SDL_Color base = enemyBaseTint(e.type);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale,
                                   {base.r,(Uint8)(base.g*hpRatio),(Uint8)(base.b*hpRatio),255});
                }
            }
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

        // Goal indicator - drawn before UI so HUD sits on top
        if (customGoalOpen_) {
            MapTrigger* goal = customMap_.findEndTrigger();
            if (goal) {
                Vec2 gp = camera_.worldToScreen({goal->x, goal->y});
                SDL_SetRenderDrawColor(renderer_, 50, 255, 100, 150);
                SDL_Rect gr = {(int)(gp.x - goal->width/2), (int)(gp.y - goal->height/2),
                              (int)goal->width, (int)goal->height};
                SDL_RenderFillRect(renderer_, &gr);
                ui_.drawText("EXIT", (int)gp.x - ui_.textWidth("EXIT", 16)/2, (int)gp.y - 8, 16, {50, 255, 100, 255});
            }
        }

        renderUI();

        if (state_ == GameState::PackPaused) renderPauseMenu();
        if (state_ == GameState::PackDead) renderDeathScreen();
        if (state_ == GameState::PackLevelWin) renderPackLevelWin();
        break;

    case GameState::PackComplete:
        renderPackComplete();
        break;

    // Multiplayer states
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
            // Splitscreen multiplayer rendering
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
            if (tex) {
                auto& eLegs = !enemyLegSprites_.empty() ? enemyLegSprites_ : legSprites_;
                if (!eLegs.empty()) {
                    int li = e.legAnimFrame % (int)eLegs.size();
                    renderSpriteEx(eLegs[li], e.pos, e.legRotation+(float)M_PI/2, 1.5f, enemyBaseTint(e.type));
                }
                bool showDash = e.isDashing || e.netIsDashing;
                bool showCharge = e.dashCharging || e.netDashCharging;
                if (showDash) {
                    Uint8 fl = (Uint8)(180+(int)(sinf(gameTime_*80.0f)*75.0f+75.0f)/2);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale, {255,fl,60,255});
                } else if (showCharge) {
                    Uint8 pulse = (Uint8)(80+(int)(sinf(gameTime_*28.0f)*80.0f+80.0f)/2);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale, {255,pulse,pulse,255});
                } else {
                    float hpRatio = std::max(0.0f,std::min(1.0f,e.maxHp>0?e.hp/e.maxHp:1.0f));
                    SDL_Color base = enemyBaseTint(e.type);
                    renderSpriteEx(tex, e.pos, e.rotation+(float)M_PI/2, drawScale,
                                   {base.r,(Uint8)(base.g*hpRatio),(Uint8)(base.b*hpRatio),255});
                }
            }
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
        renderVehicles();
        renderCrates();
        renderPickups();
        renderBystanders();
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
            drawTextCentered("SPECTATING  -  Press ESC/START to pause", 14, 18, bannerCol);
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

    case GameState::OnlineWorkshop:
        renderOnlineWorkshop();
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

    // Mod-save dialog overlay - rendered on top of everything
    if (modSaveDialog_.isOpen())
        renderModSaveDialog();

    if (ui_.buttonFired && sfxClick_) playSFX(sfxClick_, config_.sfxVolume);

    ui_.endFrame();
    if (usePostFX) {
        SDL_SetRenderTarget(renderer_, nullptr);
        renderPostFXComposite(gameplayView);
    }

    // Dev console overlay - Windows CMD style, rendered last so it's always on top
    if (consoleOpen_) {
        const int conH   = 240;
        const int titleH = 20;
        const int padX   = 6;
        const int lineH  = 14;

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

        // Title bar - classic Windows CMD navy blue
        SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
        SDL_Rect titleBar = {0, 0, SCREEN_W, titleH};
        SDL_RenderFillRect(renderer_, &titleBar);
        ui_.drawText("streameditor v0.3", padX, 3, 11, {255, 255, 255, 255});

        // Black content area
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 245);
        SDL_Rect bg = {0, titleH, SCREEN_W, conH - titleH};
        SDL_RenderFillRect(renderer_, &bg);

        // Bottom separator
        SDL_SetRenderDrawColor(renderer_, 80, 80, 80, 255);
        SDL_RenderDrawLine(renderer_, 0, conH, SCREEN_W, conH);

        // Log lines (newest at bottom), reserve one row for input
        int maxLines = (conH - titleH - lineH - 4) / lineH;
        int start = (int)consoleLog_.size() - maxLines;
        if (start < 0) start = 0;
        for (int i = start; i < (int)consoleLog_.size(); i++) {
            int row = i - start;
            // Commands echoed (no leading space): bright white; output: light gray
            SDL_Color col = (!consoleLog_[i].empty() && consoleLog_[i][0] != ' ')
                            ? SDL_Color{255, 255, 255, 255}
                            : SDL_Color{192, 192, 192, 255};
            ui_.drawText(consoleLog_[i].c_str(), padX, titleH + 2 + row * lineH, 11, col);
        }

        // Input prompt - CMD style
        char inputLine[280];
        snprintf(inputLine, sizeof(inputLine), "C:\\COLDSTART> %s_", consoleBuf_);
        ui_.drawText(inputLine, padX, conH - lineH - 2, 11, {255, 255, 255, 255});
    }

#ifdef __ANDROID__
    touchControls_.render(renderer_, config_.uiScale);
#endif

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
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
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
        float csRot = csPlay_.active ? csPlay_.cam.rotation : 0.0f;
        if (fabsf(csRot) > 0.01f) {
            SDL_Point center = { SCREEN_W / 2, SCREEN_H / 2 };
            SDL_RenderCopyEx(renderer_, sceneTarget_, nullptr, &full,
                             (double)csRot, &center, SDL_FLIP_NONE);
        } else {
            SDL_RenderCopy(renderer_, sceneTarget_, nullptr, &full);
        }
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

    Vec2 chWorld  = player.pos + aimDir.normalized() * distance;
    Vec2 chScreen = camera.worldToScreen(chWorld);
    int  cx = (int)chScreen.x, cy = (int)chScreen.y;

    const int gap = 4, len = size, half = 1;
    SDL_Rect arms[4] = {
        {cx - half, cy - gap - len, 2, len},   // up
        {cx - half, cy + gap,       2, len},   // down
        {cx - gap - len, cy - half, len, 2},   // left
        {cx + gap,       cy - half, len, 2},   // right
    };
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 210);
    for (auto& r : arms) { SDL_Rect o = {r.x-1,r.y-1,r.w+2,r.h+2}; SDL_RenderFillRect(renderer_, &o); }
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 235);
    for (auto& r : arms) SDL_RenderFillRect(renderer_, &r);
}

// Bypass SDL_RenderCopyExF entirely - compute corners on CPU and submit raw
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
        case EnemyType::Brute:      return bruteSprite_   ? bruteSprite_   : enemySprite_;
        case EnemyType::Scout:      return scoutSprite_   ? scoutSprite_   : enemySprite_;
        case EnemyType::Sniper:     return sniperSprite_  ? sniperSprite_  : shooterSprite_;
        case EnemyType::Gunner:     return gunnerSprite_  ? gunnerSprite_  : shooterSprite_;
        case EnemyType::Shooter:    return shooterSprite_;
        // Bosses use their dedicated sprites, falling back to the base type
        case EnemyType::BossBrute:  return bossBruteSprite_  ? bossBruteSprite_  : (bruteSprite_  ? bruteSprite_  : enemySprite_);
        case EnemyType::BossSniper: return bossSniperSprite_ ? bossSniperSprite_ : (sniperSprite_ ? sniperSprite_ : shooterSprite_);
        case EnemyType::BossGunner: return bossGunnerSprite_ ? bossGunnerSprite_ : (gunnerSprite_ ? gunnerSprite_ : shooterSprite_);
        case EnemyType::Melee:
        default:                    return enemySprite_;
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
    // Full-map background image: place the entire image scaled to the full world
    // area, positioned so world (0,0) aligns with screen. SDL clips to viewport.
    // Using float dst (not integer src sub-rect) avoids per-frame pixel jitter.
    if (bgImageTex_) {
        float wx = -camera_.pos.x + camera_.shakeOffset.x;
        float wy = -camera_.pos.y + camera_.shakeOffset.y;
        SDL_FRect dst = {wx, wy,
                         (float)(map_.width  * TILE_SIZE),
                         (float)(map_.height * TILE_SIZE)};
        SDL_SetTextureBlendMode(bgImageTex_, SDL_BLENDMODE_NONE);
        SDL_RenderCopyF(renderer_, bgImageTex_, nullptr, &dst);
        return;
    }

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

    // Per-tile rotations from custom map (empty on procedural maps)
    const auto& tileRots = customMap_.tileRotations;
    int mapStride = map_.width;

    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            uint8_t tile = map_.get(x, y);
            if (map_.isSolid(x, y)) continue; // solids are drawn in renderWallOverlay()
            SDL_Texture* tex = nullptr;
            double tileAngle = 0.0;
            SDL_RendererFlip tileFlip = SDL_FLIP_NONE;

            // Per-tile editor rotation (overrides hash-based variety for that tile)
            int tidx = y * mapStride + x;
            if (!tileRots.empty() && tidx < (int)tileRots.size() && tileRots[tidx] > 0)
                tileAngle = tileRots[tidx] * 90.0;

            // Deterministic hash for per-tile randomization
            unsigned int tileHash = (unsigned int)(x * 73856093u ^ y * 19349663u);

            if (tile == TILE_FLOOR) {
                // Hard floor - render directly with floorTex_, no gravel transitions
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
                // Grass - use gravel transition sprites
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

            // Permanent blood tint accumulated from decals
            if (!tileBlood_.empty()) {
                int idx = y * map_.width + x;
                if (idx < (int)tileBlood_.size()) {
                    float tb = tileBlood_[idx];
                    if (tb > 0.005f) {
                        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(renderer_, 110, 0, 0, (Uint8)(tb * 60.0f));
                        SDL_RenderFillRectF(renderer_, &dst);
                    }
                }
            }
        }
    }

    // Draw free-placed props from custom map
    if (!customMap_.props.empty()) {
        for (auto& ps : customMap_.props) {
            // Resolve texture the same way the main tile loop does
            SDL_Texture* ptex = nullptr;
            if      (ps.tileType == TILE_DESK) ptex = deskTex_;
            else if (ps.tileType == TILE_BOX)  ptex = boxTex_;
            else if (ps.tileType >= TILE_CUSTOM_0 && ps.tileType <= TILE_CUSTOM_7)
                ptex = customTileTextures_[ps.tileType - TILE_CUSTOM_0];
            if (!ptex) continue;
            Vec2 sp = camera_.worldToScreen({ps.x, ps.y});
            float half = TILE_SIZE * 0.5f;
            double angle = ps.rotation * 90.0;
            renderRotatedQuad(renderer_, ptex,
                sp.x, sp.y, half, half,
                (float)(angle * M_PI / 180.0), SDL_FLIP_NONE);
        }
    }
}

void Game::renderWallOverlay() {
    if (bgImageTex_) return; // image-based map: walls are baked into the image
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
            SDL_FRect dst = {sp.x - TILE_SIZE * 0.5f, sp.y - TILE_SIZE * 0.5f,
                             (float)TILE_SIZE, (float)TILE_SIZE};

            if (tex) {
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
                SDL_RenderCopyF(renderer_, tex, nullptr, &dst);
            } else {
                SDL_SetRenderDrawColor(renderer_, 100, 90, 80, 255);
                SDL_RenderFillRectF(renderer_, &dst);
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
            SDL_SetTextureAlphaMod(bloodTex_, (Uint8)(bd.alpha * 180.0f));
            renderRotatedQuad(renderer_, bloodTex_, sp.x, sp.y, half, half, bd.rotation);
            SDL_SetTextureColorMod(bloodTex_, 255, 255, 255);
            SDL_SetTextureAlphaMod(bloodTex_, 255);
        }
    }
}

void Game::renderRoofOverlay() {
    // Full-map top-layer image (building rooftops etc., rendered above entities)
    if (topImageTex_) {
        float wx = -camera_.pos.x + camera_.shakeOffset.x;
        float wy = -camera_.pos.y + camera_.shakeOffset.y;
        SDL_FRect dst = {wx, wy,
                         (float)(map_.width  * TILE_SIZE),
                         (float)(map_.height * TILE_SIZE)};
        SDL_SetTextureBlendMode(topImageTex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(topImageTex_, (Uint8)(topLayerAlpha_ * 255.0f));
        SDL_RenderCopyF(renderer_, topImageTex_, nullptr, &dst);
        SDL_SetTextureAlphaMod(topImageTex_, 255);
        return;
    }
    // If a background image is in use, ceiling tiles are already handled visually
    if (bgImageTex_) return;

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
            SDL_FRect dst = {sp.x - TILE_SIZE * 0.5f, sp.y - TILE_SIZE * 0.5f,
                             (float)TILE_SIZE, (float)TILE_SIZE};

            if (glassTileTex_) {
                SDL_SetTextureAlphaMod(glassTileTex_, 100);
                SDL_RenderCopyF(renderer_, glassTileTex_, nullptr, &dst);
                SDL_SetTextureAlphaMod(glassTileTex_, 255);
            } else {
                SDL_SetRenderDrawColor(renderer_, 140, 180, 220, 35);
                SDL_RenderFillRectF(renderer_, &dst);
            }
        }
    }
}

void Game::renderShadingPass() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    // Wall shadow / ambient occlusion pass
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
                SDL_FRect dst = {sp.x - TILE_SIZE * 0.5f, sp.y - TILE_SIZE * 0.5f,
                                 (float)TILE_SIZE, (float)TILE_SIZE};
                SDL_RenderFillRectF(renderer_, &dst);
            }
        }
    }

    // Vignette overlay (proper radial)
    if (vignetteTex_) {
        // Use camera view dimensions so the vignette fills the viewport correctly
        // even when SDL_RenderSetScale is active during splitscreen.
        SDL_Rect full = {0, 0, (int)camera_.viewW, (int)camera_.viewH};
        SDL_RenderCopy(renderer_, vignetteTex_, nullptr, &full);
    }

    // Low-HP red tint
    if (lowHpTint_ > 0.001f) {
        // Pulse alpha when critically low (hp <= 20%), steady when moderate
        float hpRatio = (player_.maxHp > 0) ? (float)player_.hp / player_.maxHp : 1.0f;
        float pulse = 1.0f;
        if (hpRatio < 0.2f) {
            float t = (float)SDL_GetTicks() * 0.003f;
            pulse = 0.6f + 0.4f * sinf(t);
        }
        Uint8 alpha = (Uint8)(lowHpTint_ * pulse * 90.0f);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 180, 0, 0, alpha);
        SDL_Rect full = {0, 0, (int)camera_.viewW, (int)camera_.viewH};
        SDL_RenderFillRect(renderer_, &full);
    }
}

void Game::invalidateMinimapCache() {
    minimapCacheDirty_ = true;
}

void Game::renderMinimap() {
    // Minimap - bottom-right corner
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

    // Upgrade crates (yellow diamonds - just squares for simplicity)
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
    // • Teammate  - team-colored, normal size (5×5)
    // • Enemy player - bright red/team-color, larger (7×7) + white outline
    {
        auto& net = NetworkManager::instance();
        if (net.isOnline()) {
            uint8_t localId = net.localPlayerId();
            for (auto& rp : net.players()) {
                if (rp.id == localId) continue;
                if (!rp.alive) continue;

                bool isEnemy = (localTeam_ < 0)              // FFA - all others are enemies
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
                    // Teammate - small blip in their team color
                    drawBlip(rp.pos.x, rp.pos.y, 2, blipColor(rp.team));
                }
            }
        }
    }

    // Local player blip (cyan, drawn on top of everything)
    drawBlip(player_.pos.x, player_.pos.y, 2, {0, 255, 228, 255});
}

void Game::renderUI() {
    // Win98 STATUS panel (top-left)
    {
        const int panW = 210, panH = 124;
        const int panX = 8,   panY = 8;
        const int tH   = UI::W98::TitleH;
        const int cx   = panX + 10;
        const int iW   = panW - 20;
        int cy = panY + tH + 6;

        ui_.drawWin98Window(panX, panY, panW, panH, "STATUS");

        // HP bar
        {
            ui_.drawWin98Bevel(cx, cy, iW, 16, false);
            int hpMax = std::max(1, player_.maxHp);
            int hpNow = std::max(0, player_.hp);
            int fillW = (iW - 2) * hpNow / hpMax;
            SDL_Color hpC = (player_.hp <= 10)       ? SDL_Color{200, 40,  40,  255} :
                            (player_.hp * 3 < hpMax) ? SDL_Color{200, 130, 30,  255} :
                                                       SDL_Color{30,  140, 60,  255};
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, hpC.r, hpC.g, hpC.b, 255);
            SDL_Rect fill = {cx + 1, cy + 1, fillW, 14};
            SDL_RenderFillRect(renderer_, &fill);
            char hpStr[16]; snprintf(hpStr, sizeof(hpStr), "%d / %d", hpNow, hpMax);
            int tw = ui_.textWidth(hpStr, 11);
            ui_.drawText(hpStr, cx + (iW - tw) / 2, cy + 2, 11, UI::W98::White);
            cy += 20;
        }

        // Weapon + ammo
        {
            char wpnBuf[32];
            if (player_.activeWeapon == 0) {
                if (player_.reloading)
                    snprintf(wpnBuf, sizeof(wpnBuf), "GUN  RELOADING");
                else
                    snprintf(wpnBuf, sizeof(wpnBuf), "GUN  %d/%d", player_.ammo, player_.maxAmmo);
            } else {
                snprintf(wpnBuf, sizeof(wpnBuf), "AXE");
            }
            ui_.drawText(wpnBuf, cx, cy, 13, UI::W98::Black);
            cy += 17;
        }

        // Bombs
        {
            int orbiting = 0;
            for (auto& b : bombs_) if (b.alive && !b.hasDashed) orbiting++;
            int total = orbiting + player_.bombCount;
            char bombBuf[24]; snprintf(bombBuf, sizeof(bombBuf), "Bombs: %d", total);
            ui_.drawText(bombBuf, cx, cy, 13, UI::W98::Black);
            cy += 17;
        }

        // Kill counter (shown when bomb-progress tracking is active)
        if (player_.killCounter > 0) {
            char killBuf[32];
            snprintf(killBuf, sizeof(killBuf), "Kills: %d/%d",
                     player_.killCounter, upgrades_.killsPerBomb);
            ui_.drawText(killBuf, cx, cy, 11, UI::W98::Shadow);
            cy += 15;
        }

        // Parry cooldown bar
        {
            bool ready = player_.canParry;
            float cdFill = ready ? 1.0f
                                 : 1.0f - (player_.parryCdTimer / PARRY_COOLDOWN);
            cdFill = std::max(0.0f, std::min(1.0f, cdFill));
            ui_.drawWin98Bevel(cx, cy, iW, 14, false);
            SDL_Color barC = ready ? SDL_Color{50, 180, 255, 255}
                                   : SDL_Color{80, 80, 140, 255};
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, barC.r, barC.g, barC.b, 255);
            SDL_Rect pFill = {cx + 1, cy + 1, (int)((iW - 2) * cdFill), 12};
            if (pFill.w > 0) SDL_RenderFillRect(renderer_, &pFill);
            const char* parryLabel = ready ? "PARRY  READY" : "PARRY";
            int tw = ui_.textWidth(parryLabel, 10);
            ui_.drawText(parryLabel, cx + (iW - tw) / 2, cy + 2, 10, UI::W98::White);
        }
    }

    // Win98 GAME panel (top-right) - timer + FPS
    {
        const int panW = 180, panH = 90;
        const int panX = SCREEN_W - panW - 8, panY = 8;
        const int cx   = panX + 10;
        int cy = panY + UI::W98::TitleH + 6;

        ui_.drawWin98Window(panX, panY, panW, panH, "GAME");

        int mins = (int)gameTime_ / 60;
        int secs = (int)gameTime_ % 60;
        char timeBuf[16]; snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", mins, secs);
        ui_.drawText(timeBuf, cx, cy, 22, UI::W98::Navy);
        cy += 26;

        int fps = (dt_ > 0.0001f) ? (int)(1.0f / dt_) : 0;
        char fpsBuf[16]; snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", fps);
        ui_.drawText(fpsBuf, cx, cy, 13, UI::W98::Shadow);
    }

    // Minimap (bottom-right, unchanged)
    renderMinimap();

    // Hit vignette
    if (player_.invulnerable) {
        float alpha = player_.invulnTimer / PLAYER_INVULN_TIME * 0.3f;
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 255, 0, 0, (Uint8)(alpha * 255));
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(renderer_, &full);
    }

    // Screen flash (explosions etc.)
    if (screenFlashTimer_ > 0) {
        float a = (screenFlashTimer_ / 0.12f) * 0.35f;
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, (Uint8)screenFlashR_, (Uint8)screenFlashG_,
                               (Uint8)screenFlashB_, (Uint8)(a * 255));
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(renderer_, &full);
    }

    // Muzzle flash glow
    if (muzzleFlashTimer_ > 0) {
        Vec2 sp = camera_.worldToScreen(muzzleFlashPos_);
        float flashAlpha = muzzleFlashTimer_ / 0.06f;
        int flashSize = (int)(8 + flashAlpha * 6);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 255, 240, 150, (Uint8)(flashAlpha * 160));
        SDL_Rect fd = {(int)sp.x - flashSize/2, (int)sp.y - flashSize/2, flashSize, flashSize};
        SDL_RenderFillRect(renderer_, &fd);
    }

    // System announcements - Win98-style bars sliding in from top
    // Helper: compute slide+fade alpha for a timer in [0, maxT]
    // Slides in over 0.25s, holds, slides out over 0.25s.
    auto annoAlpha = [](float t, float maxT) -> float {
        const float slide = 0.25f;
        float a = 1.0f;
        if (t > maxT - slide) a = (maxT - t) / slide;   // fade in
        if (t < slide)        a = t / slide;              // fade out
        return fminf(1.0f, fmaxf(0.0f, a));
    };
    auto annoSlideY = [](float t, float maxT, int baseY) -> int {
        const float slide = 0.25f;
        float frac = (t > maxT - slide) ? (maxT - t) / slide : 1.0f;
        return baseY - (int)((1.0f - frac) * 60.0f);
    };

    const float waveMaxT  = 2.5f;
    const float otherMaxT = 2.5f;
    const int   notifW    = 340;
    const int   notifH    = 48; // title bar(22) + body(26)
    const int   notifX    = (SCREEN_W - notifW) / 2;

    // Slot 0 (y=4): wave announcement
    // Slot 1 (y=58): pickup / supply drop
    bool waveActive = (waveAnnounceTimer_ > 0);
    int  slot1Y     = waveActive ? 58 : 4;

    // Wave announcement
    if (waveAnnounceTimer_ > 0) {
        float t     = waveAnnounceTimer_;
        float alpha = annoAlpha(t, waveMaxT);
        int   y     = annoSlideY(t, waveMaxT, 4);

        bool isBoss = (waveAnnounceNum_ == 25 || waveAnnounceNum_ == 35 || waveAnnounceNum_ == 45);
        bool isMilestone = (waveAnnounceNum_ == MILESTONE_SNIPER_WAVE ||
                            waveAnnounceNum_ == MILESTONE_GUNNER_WAVE);

        // Win98 window frame with coloured title
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

        // Silver body
        SDL_SetRenderDrawColor(renderer_, 192, 192, 192, (Uint8)(alpha * 255));
        SDL_Rect body = {notifX, y, notifW, notifH};
        SDL_RenderFillRect(renderer_, &body);
        ui_.drawWin98Bevel(notifX, y, notifW, notifH, true);

        // Title bar
        SDL_Color barCol = isBoss      ? SDL_Color{140, 20,  20,  255} :
                           isMilestone ? SDL_Color{120, 60,  0,   255} :
                                         SDL_Color{0,   0,   128, 255};
        SDL_SetRenderDrawColor(renderer_, barCol.r, barCol.g, barCol.b, (Uint8)(alpha * 255));
        SDL_Rect titleBar = {notifX + 3, y + 3, notifW - 6, UI::W98::TitleH - 4};
        SDL_RenderFillRect(renderer_, &titleBar);

        // Title text
        const char* titleTxt = isBoss ? "!! SYSTEM ALERT !!" : (isMilestone ? "! SYSTEM ALERT !" : "SYSTEM ALERT");
        int tw = ui_.textWidth(titleTxt, 11);
        SDL_Color titleCol = {255, 255, 255, (Uint8)(alpha * 255)};
        ui_.drawText(titleTxt, notifX + (notifW - tw) / 2, y + 5, 11, titleCol);

        // Body content
        char waveTxt[64];
        int  bodyY = y + UI::W98::TitleH + 4;
        if (isBoss) {
            const char* bossName = (waveAnnounceNum_ == 25)  ? "BRUTE" :
                                   (waveAnnounceNum_ == 50)  ? "SNIPER PRIME" : "CHAINGUNNER";
            snprintf(waveTxt, sizeof(waveTxt), "Wave %d - BOSS: %s", waveAnnounceNum_, bossName);
            SDL_Color c = {220, 60, 60, (Uint8)(alpha * 255)};
            int bw = ui_.textWidth(waveTxt, 13);
            ui_.drawText(waveTxt, notifX + (notifW - bw) / 2, bodyY, 13, c);
        } else if (isMilestone) {
            const char* eliteName = (waveAnnounceNum_ == MILESTONE_SNIPER_WAVE) ? "SNIPER" : "GUNNER";
            snprintf(waveTxt, sizeof(waveTxt), "Wave %d - ELITE: %s", waveAnnounceNum_, eliteName);
            SDL_Color c = {220, 140, 40, (Uint8)(alpha * 255)};
            int bw = ui_.textWidth(waveTxt, 13);
            ui_.drawText(waveTxt, notifX + (notifW - bw) / 2, bodyY, 13, c);
        } else {
            snprintf(waveTxt, sizeof(waveTxt), "Wave %d beginning", waveAnnounceNum_);
            SDL_Color c = {0, 0, 128, (Uint8)(alpha * 255)};
            int bw = ui_.textWidth(waveTxt, 13);
            ui_.drawText(waveTxt, notifX + (notifW - bw) / 2, bodyY, 13, c);
        }
    }

    // Pickup popup - slot 1
    if (pickupPopupTimer_ > 0) {
        float t     = pickupPopupTimer_;
        float alpha = annoAlpha(t, otherMaxT);
        int   y     = annoSlideY(t, otherMaxT, slot1Y);

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(renderer_, 192, 192, 192, (Uint8)(alpha * 255));
        SDL_Rect body = {notifX, y, notifW, notifH};
        SDL_RenderFillRect(renderer_, &body);
        ui_.drawWin98Bevel(notifX, y, notifW, notifH, true);

        // Title bar coloured by pickup type
        SDL_Color ac = pickupPopupColor_;
        SDL_SetRenderDrawColor(renderer_, ac.r / 2, ac.g / 2, ac.b / 2, (Uint8)(alpha * 255));
        SDL_Rect titleBar = {notifX + 3, y + 3, notifW - 6, UI::W98::TitleH - 4};
        SDL_RenderFillRect(renderer_, &titleBar);

        // Title = upgrade name
        int tw = ui_.textWidth(pickupPopupName_.c_str(), 11);
        SDL_Color titleCol = {ac.r, ac.g, ac.b, (Uint8)(alpha * 255)};
        ui_.drawText(pickupPopupName_.c_str(), notifX + (notifW - tw) / 2, y + 5, 11, titleCol);

        // Body = description
        int bodyY = y + UI::W98::TitleH + 4;
        SDL_Color descCol = {40, 40, 40, (Uint8)(alpha * 255)};
        int dw = ui_.textWidth(pickupPopupDesc_.c_str(), 12);
        ui_.drawText(pickupPopupDesc_.c_str(), notifX + (notifW - dw) / 2, bodyY, 12, descCol);
    }


    if (cratePopupTimer_ > 0) {
        float t     = cratePopupTimer_;
        float alpha = annoAlpha(t, otherMaxT);
        int   crateSlotY = (pickupPopupTimer_ > 0) ? slot1Y + 54 : slot1Y;
        int   y     = annoSlideY(t, otherMaxT, crateSlotY);

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(renderer_, 192, 192, 192, (Uint8)(alpha * 255));
        SDL_Rect body = {notifX, y, notifW, notifH};
        SDL_RenderFillRect(renderer_, &body);
        ui_.drawWin98Bevel(notifX, y, notifW, notifH, true);

        // Orange title bar
        SDL_SetRenderDrawColor(renderer_, 128, 80, 0, (Uint8)(alpha * 255));
        SDL_Rect titleBar = {notifX + 3, y + 3, notifW - 6, UI::W98::TitleH - 4};
        SDL_RenderFillRect(renderer_, &titleBar);

        const char* supplyTitle = "SUPPLY DROP";
        int tw = ui_.textWidth(supplyTitle, 11);
        SDL_Color titleCol = {255, 200, 60, (Uint8)(alpha * 255)};
        ui_.drawText(supplyTitle, notifX + (notifW - tw) / 2, y + 5, 11, titleCol);

        int bodyY = y + UI::W98::TitleH + 4;
        const char* bodyTxt = "Supply crate detected nearby";
        int bw = ui_.textWidth(bodyTxt, 11);
        SDL_Color bodyCol = {40, 40, 40, (Uint8)(alpha * 255)};
        ui_.drawText(bodyTxt, notifX + (notifW - bw) / 2, bodyY, 11, bodyCol);
    }

    // Boss HP bar - Win98-style panel at bottom-center
    if (bossWaveActive_) {
        const Enemy* boss = nullptr;
        for (auto& be : enemies_) {
            if (!be.alive || !isBossType(be.type)) continue;
            if (!boss || be.hp > boss->hp) boss = &be;
        }
        if (boss) {
            float hpFrac = (boss->maxHp > 0.0f) ?
                           std::max(0.0f, boss->hp / boss->maxHp) : 0.0f;
            const char* bossLabel =
                (boss->type == EnemyType::BossBrute)  ? "BRUTE" :
                (boss->type == EnemyType::BossSniper) ? "SNIPER PRIME" : "CHAINGUNNER";

            const int panW = SCREEN_W / 2;
            const int panH = 46;
            const int panX = (SCREEN_W - panW) / 2;
            const int panY = SCREEN_H - panH - 8;

            // Win98 window chrome
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, 192, 192, 192, 255);
            SDL_Rect panBg = {panX, panY, panW, panH};
            SDL_RenderFillRect(renderer_, &panBg);
            ui_.drawWin98Bevel(panX, panY, panW, panH, true);

            // Red title bar
            SDL_SetRenderDrawColor(renderer_, 140, 10, 10, 255);
            SDL_Rect tBar = {panX + 3, panY + 3, panW - 6, UI::W98::TitleH - 4};
            SDL_RenderFillRect(renderer_, &tBar);
            int lw = ui_.textWidth(bossLabel, 11);
            ui_.drawText(bossLabel, panX + (panW - lw) / 2, panY + 5, 11, UI::W98::White);

            // HP bar (sunken inside panel)
            const int barX = panX + 8;
            const int barY = panY + UI::W98::TitleH + 4;
            const int barW = panW - 16;
            const int barH = 14;
            ui_.drawWin98Bevel(barX, barY, barW, barH, false);

            Uint8 fr = (Uint8)(255 * (1.0f - hpFrac * 0.5f));
            Uint8 fg = (Uint8)(160 * hpFrac);
            SDL_SetRenderDrawColor(renderer_, fr, fg, 20, 255);
            SDL_Rect hpFill = {barX + 1, barY + 1, (int)((barW - 2) * hpFrac), barH - 2};
            SDL_RenderFillRect(renderer_, &hpFill);

            char hpPct[8]; snprintf(hpPct, sizeof(hpPct), "%d%%", (int)(hpFrac * 100));
            int pw = ui_.textWidth(hpPct, 10);
            ui_.drawText(hpPct, barX + (barW - pw) / 2, barY + 2, 10, UI::W98::White);
        }
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
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        const int gap = 4, len = 8, half = 1;
        SDL_Rect arms[4] = {
            {mx - half, my - gap - len, 2, len},
            {mx - half, my + gap,       2, len},
            {mx - gap - len, my - half, len, 2},
            {mx + gap,       my - half, len, 2},
        };
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 210);
        for (auto& r : arms) { SDL_Rect o = {r.x-1,r.y-1,r.w+2,r.h+2}; SDL_RenderFillRect(renderer_, &o); }
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 235);
        for (auto& r : arms) SDL_RenderFillRect(renderer_, &r);
    }
#endif

}

// BIOS Intro

struct BiosEntry { float t; const char* text; };
static const BiosEntry kBiosLines[] = {
    {0.00f, "AVA BIOS v2.06"},
    {0.18f, "Copyright (C) 1998-2006  AVA Ltd."},
    {0.36f, ""},
    {0.54f, "CPU : Cold Start 486DX2-66  [OC: 75000MHz]"},
    {0.90f, "Testing base memory...    640K OK"},
    {1.50f, "Testing extended memory... 64512000K OK"},
    {2.10f, ""},
    {2.28f, "Checking IDE controller... OK"},
    {2.64f, "  Primary Master : Cold Start HD     4311MB"},
    {2.82f, "  Primary Slave  : Not Detected"},
    {3.00f, ""},
    {3.18f, "Initializing Plug and Play..."},
    {3.54f, "  PCI Bus No.0  Dev No.1  Func 0 : SERVO controller"},
    {3.72f, "  PCI Bus No.0  Dev No.2  Func 0 : CAM bridge"},
    {3.90f, ""},
    {4.08f, "ESCD updated successfully."},
    {4.35f, ""},
    {4.53f, "Boot device: Hard Disk (C:)"},
    {4.80f, "Loading entry..."},
    {5.10f, ""},
    {5.40f, "  Press [DEL] to enter SETUP,  [F8] for boot menu"},
};
static constexpr int kBiosLineCount = (int)(sizeof(kBiosLines)/sizeof(kBiosLines[0]));
static constexpr float kBiosAnyKeyT = 5.80f;  // when "press any key" appears
static constexpr float kBiosAutoT   = 8.50f;  // auto-advance if no key pressed

void Game::renderBiosIntro() {
    if (!biosBootPlayed_) {
        biosBootPlayed_ = true;
        if (sfxBoot_) playSFX(sfxBoot_, config_.sfxVolume);
    }
    biosTimer_ += dt_;

    // Black background
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    const SDL_Color textColor  = {170, 170, 170, 255};  // classic CGA gray
    const SDL_Color brightText = {255, 255, 255, 255};
    const int fs = 13;
    const int lineH = 17;
    int y = 18;

    for (int i = 0; i < kBiosLineCount; i++) {
        if (biosTimer_ < kBiosLines[i].t) break;
        const char* txt = kBiosLines[i].text;
        if (txt[0]) {
            // First line is bright (header)
            SDL_Color c = (i == 0) ? brightText : textColor;
            ui_.drawText(txt, 24, y, fs, c);
        }
        y += lineH;
    }

    if (biosTimer_ >= kBiosAnyKeyT) {
        bool blink = ((int)(biosTimer_ * 2) % 2 == 0);
        if (blink) {
            ui_.drawText("_", 24, y + lineH, fs, brightText);
        }
    }

    // Auto-advance
    if (biosTimer_ >= kBiosAutoT) {
        biosTimer_ = 0;
        biosLine_  = 0;
        loginUsername_ = config_.username;
        loginPassword_.clear();
        loginField_  = 0;
        loginBlinkT_ = 0;
        state_ = GameState::LoginScreen;
    }
}


std::string getrandomhint() {
    std::vector<std::string> hints = {"also try hotline miami!", "we hit 100 downloads!", "have a good day", "never stop trying", "i hate this codebase", "trololo"};
    std::random_device  rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<std::size_t> dist(0, hints.size() - 1);
    std::size_t randomIndex = dist(gen);
    return hints[randomIndex];
}

std::string hint = "Hint: " + getrandomhint() ;
// Login Screen

void Game::renderLoginScreen() {
    loginBlinkT_ += dt_;

    // Teal Win98 desktop
    ui_.drawDesktop();

    // Window dimensions
    const int winW = 380;
    const int winH = 240;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Log On");

    const int contentX = winX + 16;
    int       cy       = winY + UI::W98::TitleH + 20;
    const int labelW   = 100;
    const int fieldW   = winW - labelW - 32;
    const int fieldH   = 24;
    const int rowGap   = 36;

    // Instructional text
    ui_.drawText("Enter your network username and password.", contentX, cy, 12, UI::W98::Black);
    cy += 22;
    ui_.drawText(hint.c_str(), contentX, cy, 11, UI::W98::Shadow);
    cy += 28;

    // Username row - clicking field switches focus and opens keyboard
    ui_.drawText("User name:", contentX, cy + (fieldH - 14) / 2, 13, UI::W98::Black);
    if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, contentX + labelW, cy, fieldW, fieldH)) {
        loginField_ = 0; loginBlinkT_ = 0; ui_.mouseClicked = false;
#ifdef __SWITCH__
        softKB_.open(&loginUsername_, 32, nullptr);
#else
        SDL_StartTextInput();
#endif
    }
    bool unFocused = (loginField_ == 0);
    ui_.drawWin98TextField(contentX + labelW, cy, fieldW, fieldH,
                           loginUsername_.c_str(), unFocused, false, loginBlinkT_);
    cy += rowGap;

    // Password row - clicking field switches focus and opens keyboard
    ui_.drawText("Password:", contentX, cy + (fieldH - 14) / 2, 13, UI::W98::Black);
    if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, contentX + labelW, cy, fieldW, fieldH)) {
        loginField_ = 1; loginBlinkT_ = 0; ui_.mouseClicked = false;
#ifdef __SWITCH__
        softKB_.open(&loginPassword_, 32, nullptr);
#else
        SDL_StartTextInput();
#endif
    }
    bool pwFocused = (loginField_ == 1);
    ui_.drawWin98TextField(contentX + labelW, cy, fieldW, fieldH,
                           loginPassword_.c_str(), pwFocused, true, loginBlinkT_);
    cy += rowGap + 8;

    // Separator
    ui_.drawWin98Bevel(contentX, cy, winW - 32, 2, false);
    cy += 12;

    // OK / Cancel buttons (centered)
    const int btnW = 80;
    const int btnH = 26;
    const int gap  = 12;
    int bx = winX + (winW - btnW * 2 - gap) / 2;

    if (ui_.win98Button(0, "OK",     bx,        cy, btnW, btnH, loginField_ == 2)) {
        // Commit username and go to main menu
        config_.username = loginUsername_.empty() ? "Player" : loginUsername_;
        saveConfig();
#ifndef __SWITCH__
        SDL_StopTextInput();
#endif
        state_ = GameState::MainMenu;
        menuSelection_ = 0;
        playMenuMusic();
    }
    if (ui_.win98Button(1, "Cancel", bx+btnW+gap, cy, btnW, btnH, false)) {
        // Skip login, keep existing username
#ifndef __SWITCH__
        SDL_StopTextInput();
#endif
        state_ = GameState::MainMenu;
        menuSelection_ = 0;
        playMenuMusic();
    }

    // Keyboard hint
    ui_.drawWin98StatusBar(SCREEN_H - 26,
        "Tab: switch field    Enter: OK    Esc: Cancel");
}

// Main Menu

void Game::renderMainMenu() {
    // Win98 desktop background
    ui_.drawDesktop();

    // AVA Explorer desktop icon (drawn on desktop, under all windows)
    {
        SDL_Texture* bic = Assets::instance().loadRelTex("sprites/ui/browser_icon.png");
        const int icSz = 32, icX = 18, icY = 18;
        const char* icLabel = "AVA Explorer";
        if (bic) {
            SDL_Rect dst = {icX, icY, icSz, icSz};
            SDL_RenderCopy(renderer_, bic, nullptr, &dst);
        } else {
            SDL_SetRenderDrawColor(renderer_, 0, 80, 180, 255);
            SDL_Rect fb = {icX, icY, icSz, icSz}; SDL_RenderFillRect(renderer_, &fb);
        }
        int lw = ui_.textWidth(icLabel, 10);
        int labelX = std::max(2, icX + icSz/2 - lw/2);
        ui_.drawText(icLabel, labelX + 1, icY + icSz + 3 + 1, 10, {0,0,0,200});
        ui_.drawText(icLabel, labelX,     icY + icSz + 3,     10, {255,255,255,255});

        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, icX - 4, icY - 4, icSz + 8, icSz + 20);
        if (hovered && ui_.mouseClicked) {
            uint32_t now = SDL_GetTicks();
            if (now - browserIconClickT_ < 500) {
                browserOpen_ = true;
                if (!browserInit_) {
                    browserInit_ = true;
                    browserWinX_ = (SCREEN_W - 720) / 2;
                    browserWinY_ = (SCREEN_H - 480) / 2 - 20;
                    browserPage_ = 0;
                    browserHist_[0] = 0; browserHistLen_ = 1; browserHistPos_ = 0;
                }
            }
            browserIconClickT_ = now;
            ui_.mouseClicked = false;
        }
    }

    // Build item list
    struct Item { const char* label; bool enabled; };
#ifdef __SWITCH__
    Item items[] = {
        {"Play"},         // 0
        {"Multiplayer"},  // 1
        {"Tools"},        // 2
        {"Maps"},         // 3
        {"Packs"},        // 4
        {"Character"},    // 5
        {"Mods"},         // 6
        {"Config"},       // 7
        {"Credits"},      // 8
        {"Workshop"},     // 9
        {"Log Off"},      // 10
    };
    constexpr int count = 11;
#else
    Item items[] = {
        {"Play"},         // 0
        {"Multiplayer"},  // 1
        {"Tools"},        // 2
        {"Maps"},         // 3
        {"Packs"},        // 4
        {"Character"},    // 5
        {"Mods"},         // 6
        {"Config"},       // 7
        {updateAvailable_ ? "Update (available!)" : "Update", true}, // 8
        {"Credits"},      // 9
        {"Workshop"},     // 10
        {"Log Off"},      // 11
    };
    constexpr int count = 12;
#endif

    // Window sizing: fit all buttons
    const int btnH   = 26;
    const int btnGap = 4;
    const int btnW   = 220;
    const int padX   = 16;
    const int padTop = UI::W98::TitleH + 14;
    const int padBot = 12;

    // Right info panel width
    const int infoPanelW = 350;
    const int totalW = padX + btnW + padX + infoPanelW + padX;
    const int totalH = padTop + count * (btnH + btnGap) - btnGap + padBot;

    int winX = (SCREEN_W - totalW) / 2;
    int winY = (SCREEN_H - totalH) / 2;
    ui_.drawWin98Window(winX, winY, totalW, totalH, "COLD START");

    // Suppress clicks on menu buttons when a floating window sits over them.
    // Save and restore so the floating windows (drawn later) can still use the click.
    bool savedClick = ui_.mouseClicked;
    {
        const int mpW = 268, mpH = 148;
        if (ui_.pointInRect(ui_.mouseX, ui_.mouseY, musicWinX_, musicWinY_, mpW, mpH) ||
            (browserOpen_  && ui_.pointInRect(ui_.mouseX, ui_.mouseY, browserWinX_,  browserWinY_,  720, 560)) ||
            (creditsOpen_  && ui_.pointInRect(ui_.mouseX, ui_.mouseY, creditsWinX_,  creditsWinY_,  420, 380)))
            ui_.mouseClicked = false;
    }

    // Buttons
    int bx = winX + padX;
    int by = winY + padTop;
    for (int i = 0; i < count; i++) {
        bool sel = (menuSelection_ == i);
        if (ui_.win98Button(i, items[i].label, bx, by, btnW, btnH, sel)) {
            menuSelection_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) menuSelection_ = i;
        by += btnH + btnGap;
    }

    ui_.mouseClicked = savedClick; // restore for floating windows drawn below

    // Right info panel
    int ipX = winX + padX + btnW + padX;
    int ipY = winY + padTop;
    int ipW = infoPanelW;
    int ipH = totalH - padTop - padBot;

    // Draw sunken info box
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_Rect infoBg = {ipX, ipY, ipW, ipH};
    SDL_RenderFillRect(renderer_, &infoBg);
    ui_.drawWin98Bevel(ipX, ipY, ipW, ipH, false);

    int iy = ipY + 8;
    // Version
    char verBuf[32];
    snprintf(verBuf, sizeof(verBuf), "v%s", GAME_VERSION);
    ui_.drawText("COLD START", ipX + 6, iy, 14, UI::W98::Navy);
    iy += 18;
    ui_.drawText(verBuf, ipX + 6, iy, 12, UI::W98::Shadow);
    iy += 18;

    // Separator
    ui_.drawWin98Bevel(ipX + 4, iy, ipW - 8, 2, false);
    iy += 10;


    // User
    ui_.drawText("Welcome", ipX + 6, iy, 11, UI::W98::Shadow);
    iy += 14;
    ui_.drawText(config_.username.c_str(), ipX + 6, iy, 12, UI::W98::Black);
    iy += 20;

    // News
    ui_.drawWin98Bevel(ipX + 4, iy, ipW - 8, 2, false);
    iy += 7;
    ui_.drawText("News", ipX + 6, iy, 10, UI::W98::Shadow);
    iy += 13;
    {
        struct Article { const char* head; const char* body; bool glitch; };
        static const Article kArt[] = {
            { "NEURAL INTEGRATION STANDARDS PASSED",
              "Department of Health certifies 340 new clinics for elective "
              "optic and motor link micro-procedural updates.", false },
            { "RE-WILDING INITIATIVE COMPLETES PHASE IV",
              "Agricultural zoning reduced by 12% as automated synthetic "
              "caloric yields hit record highs.", false },
            { "GRID STABILITY ATTAINS TOTAL BALANCE",
              "Council reports zero emissions across all municipal "
              "sectors following localized fusion rollout.", false },
            { "AQUANET WORKSTATION 3.0 ROLLOUT",
              "Latest business solution for finance and logistics"
              "more info at get.aqua:domain5", false },
            { "EXO-LIFT APPARATUS APPROVED FOR CIVIL USE",
              "Department of Labor clears lightweight assistive frames for "
              "domestic, agricultural, and architectural applications.", false },
            { "REGIONAL NETWORK ARCHIVE NO LONGER RESPONDING",
              "Data routing to Sector 7 remains temporarily paused. "
              "Engineering teams report scheduled infrastructural tuning.", false },
            { "SYSTEM: [CARRIER SIGNAL FLUCTUATION]",
              "Inbound data packet structural mismatch detected. "
              "Recalibrating interface array. Please maintain connection.", true },
            { "MUNICIPAL DESIGN ARCHITECTURE UPDATE",
              "Unified Gloss-Glass tactile surface layouts adopted as the "
              "mandatory public terminal framework worldwide.", false },
            { "OCULAR ENHANCEMENT CLEARANCE EXPANDED",
              "Low-light adaptive lens updates approved for transit and "
              "civilian night-cycle navigation across 47 districts.", false },
            { "ALERT: SECURITY COMPLIANCE LEVEL AMBER",
              "All regional assets utilizing unverified heavy hydraulic or "
              "ballistic frameworks must report to local authorities.", false },
            { "GLOBAL CALORIC COST CONTINUES TO DECREASE",
              "Synthesis plants mark a historic 61% drop in basic nutritional "
              "production overhead since the 2003 transition.", false },
            { "ACTUARIAL TABLES SHOW RECORD LONGEVITY",
              "Department of Well-Being notes a median lifespan of 94.2 years, "
              "attributing growth to early-stage gene care.", false },
            { "MEDIGEN WELLNESS SHUTTLES DEPLOYED",
              "Fleet of forty mobile therapeutic units departs for specialized "
              "low-density rural community support.", false },
            { "REGIONAL OPTIC BACKBONE HITS MILESTONE",
              "High-density subterranean data fibers now service two billion "
              "citizens ahead of the winter solstice target.", false },
            { "STRUCTURAL DISRUPTION IN LOWER GARDENS",
              "Emergency personnel respond to a heavy mechanical anomaly. "
              "Public transit diverted. Citizens advised to avoid the area.", false },
            { "INDUSTRIAL CHASSIS MISPLACEMENT REPORTED",
              "Logistics networks tracking an unregistered heavy-lifting unit. "
              "Engineers cite an unverified autonomous routing error.", false },
            { "FFFFFFFFFFF",
              "FFFFFFFFFFFFFFFFF"
              "FFFFFFFFFFF", false },
            { "ARCHITECTURAL INTEGRITY WARNING",
              "Severe structural fractures reported at the High-Glass Pavilion. "
              "Public works suspects a localized tectonic or ballast failure.", false },
            { "AGRICULTURAL AUTOMATION DISCONNECTED",
              "Dozens of autonomous harvesting units fail to check in. "
              "Central routing exploring localized wireless dropouts.", false },
            { "PUBLIC SAFETY ALERT",
              "An unidentified heavy-lifting unit has been reported rogue"
              " outside its registered zone. Casualties are yet to be reported, AVOID ZONE 9", true },
              {"AVA ANNOUNCES ``PROJECT LONGHORN``",
              "AVA users have been waiting long for a new OS, and it's finally announced!  "    
            "AVA promises a new kernel and hydraulics programming support, read more on AVA:CORP", false },
        };
        static const int kArtN = 21;
        static const int headH = 10;
        static const int bodyH = 10;
        static const int bodyMaxW = 300; // px, inside clip area

        // Pre-compute per-article heights once (body wrapping is deterministic)
        static float artHeights[kArtN];
        static float totalNewsH = 0;
        static bool  heightsReady = false;
        if (!heightsReady) {
            heightsReady = true;
            totalNewsH = 0;
            for (int i = 0; i < kArtN; i++) {
                int bh = ui_.drawTextWrapped(kArt[i].body, 0, 0, bodyH,
                                             bodyMaxW, UI::W98::Black, false);
                artHeights[i] = (float)(headH + bh + 10);
                totalNewsH   += artHeights[i];
            }
        }

        if (totalNewsH <= 0.0f) totalNewsH = 1.0f; // guard against zero-divide
        float scrollOff = fmodf(SDL_GetTicks() * 0.001f * 10.f, totalNewsH);

        // Reserve space below the news box for the weather widget
        // (separator 9 + label 13 + temp 14 + condition 13 + wind 12 + margin 4 = 65px)
        const int weatherReserve = 65;
        int clipH = (ipY + ipH) - iy - weatherReserve;
        if (clipH < 40) clipH = 40;
        SDL_Rect clip = { ipX + 4, iy, ipW - 8, clipH };
        SDL_RenderSetClipRect(renderer_, &clip);

        for (int pass = 0; pass < 2; pass++) {
            float ay = (float)iy - scrollOff + pass * totalNewsH;
            for (int i = 0; i < kArtN; i++) {
                float bot = ay + artHeights[i];
                if (bot > (float)iy && ay < (float)(iy + clipH)) {
                    SDL_Color headCol = kArt[i].glitch
                        ? SDL_Color{180, 0, 0, 255} : UI::W98::Navy;
                    SDL_Color bodyCol = kArt[i].glitch
                        ? SDL_Color{120, 0, 0, 255} : UI::W98::Black;
                    int jx = kArt[i].glitch
                        ? (int)(sinf(SDL_GetTicks() * 0.037f) * 2.f) : 0;
                    int iay = (int)ay;
                    ui_.drawText(kArt[i].head, ipX + 6 + jx, iay, headH, headCol);
                    ui_.drawTextWrapped(kArt[i].body, ipX + 6, iay + headH + 1,
                                        bodyH, bodyMaxW, bodyCol);
                }
                ay += artHeights[i];
            }
        }
        SDL_RenderSetClipRect(renderer_, nullptr);
        iy += clipH + 4;
    }

    // Weather widget
    ui_.drawWin98Bevel(ipX + 4, iy, ipW - 8, 2, false);
    iy += 7;
    ui_.drawText("Weather", ipX + 6, iy, 10, UI::W98::Shadow);
    iy += 13;
    {
        int cycle = (SDL_GetTicks() / 30000) % 4;
        static const int   kTemp[]  = { 19, 23, 16, 14 };
        static const char* kCond[]  = { "Partly Cloudy", "Clear Sky",
                                        "Overcast",      "Light Rain" };
        static const char* kWind[]  = { "12 km/h NW", "8 km/h N",
                                        "22 km/h W",  "18 km/h SW" };
        static const int   kHum[]   = { 65, 45, 80, 92 };

        char tbuf[48];
        snprintf(tbuf, sizeof(tbuf), "Neo Haven  %d\xc2\xb0""C", kTemp[cycle]);
        ui_.drawText(tbuf, ipX + 6, iy, 11, UI::W98::Black);
        iy += 14;

        ui_.drawText(kCond[cycle], ipX + 6, iy, 11, UI::W98::Black);
        iy += 13;

        char wbuf[40];
        snprintf(wbuf, sizeof(wbuf), "Wind %s  Hum %d%%", kWind[cycle], kHum[cycle]);
        ui_.drawText(wbuf, ipX + 6, iy, 10, UI::W98::Shadow);
        iy += 12;
    }

#ifndef __SWITCH__
    if (updateAvailable_ && !latestVersion_.empty()) {
        ui_.drawWin98Bevel(ipX + 4, iy + 2, ipW - 8, 2, false);
        iy += 12;
        char ubuf[48];
        snprintf(ubuf, sizeof(ubuf), "Update: v%s", latestVersion_.c_str());
        ui_.drawText(ubuf, ipX + 6, iy, 11, {0, 128, 0, 255});
    }
#endif

    // Status bar
    char statusBuf[80];
    snprintf(statusBuf, sizeof(statusBuf), "AVAOS v1.6");
    ui_.drawWin98StatusBar(SCREEN_H - 26, statusBuf);

    // Discord button - bottom-right of status bar (Win98-style button with icon)
#ifndef __SWITCH__
    {
        const int btnH  = 22;
        const int btnW  = discordTex_ ? 26 : 70;
        const int btnX  = SCREEN_W - btnW - 2;
        const int btnY2 = SCREEN_H - 26 + (26 - btnH) / 2;
        if (ui_.win98Button(300, "", btnX, btnY2, btnW, btnH, false)) {
            SDL_OpenURL("https://discord.gg/dv28MgtaNn");
        }
        if (discordTex_) {
            const int iconSz = 16;
            SDL_Rect iconDst = {btnX + (btnW - iconSz) / 2, btnY2 + (btnH - iconSz) / 2, iconSz, iconSz};
            SDL_SetTextureBlendMode(discordTex_, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(renderer_, discordTex_, nullptr, &iconDst);
        }
    }
#endif

    // Music Player Window
    const int mpW = 268, mpH = 148;
    if (!musicWinInit_) {
        musicWinInit_ = true;
        musicWinX_ = winX + totalW + 16;
        musicWinY_ = winY;
        if (musicWinX_ + mpW > SCREEN_W) musicWinX_ = winX - mpW - 16;
    }
    musicWinX_ = std::max(0, std::min(SCREEN_W - mpW, musicWinX_));
    musicWinY_ = std::max(0, std::min(SCREEN_H - mpH, musicWinY_));

    // Title bar drag (exclude the 20px close-button zone at right edge)
    bool overTitle = ui_.pointInRect(ui_.mouseX, ui_.mouseY,
        musicWinX_, musicWinY_, mpW - 22, UI::W98::TitleH);
    if (overTitle && ui_.mouseClicked) {
        musicWinDragging_ = true;
        musicWinDragOffX_ = ui_.mouseX - musicWinX_;
        musicWinDragOffY_ = ui_.mouseY - musicWinY_;
        ui_.mouseClicked = false;
    }
    if (!ui_.mouseDown) musicWinDragging_ = false;
    if (musicWinDragging_) {
        musicWinX_ = ui_.mouseX - musicWinDragOffX_;
        musicWinY_ = ui_.mouseY - musicWinDragOffY_;
        musicWinX_ = std::max(0, std::min(SCREEN_W - mpW, musicWinX_));
        musicWinY_ = std::max(0, std::min(SCREEN_H - mpH, musicWinY_));
    }

    ui_.drawWin98Window(musicWinX_, musicWinY_, mpW, mpH, "Media Player");

    int mpx = musicWinX_ + 8;
    int mpy = musicWinY_ + UI::W98::TitleH + 8;

    // Track info
    ui_.drawText("", mpx, mpy, 11, UI::W98::Shadow);
    mpy += 14;
    ui_.drawText("FOTOSHOPPE CO. - Home Screen", mpx, mpy, 12, UI::W98::Black);
    mpy += 18;

    bool trackPlaying = Mix_PlayingMusic() && !Mix_PausedMusic();
    ui_.drawText(trackPlaying ? "[ Playing ]" : "[ Paused  ]", mpx, mpy, 11,
        trackPlaying ? UI::W98::Navy : UI::W98::Shadow);
    mpy += 18;

    ui_.drawWin98Bevel(musicWinX_ + 4, mpy, mpW - 8, 2, false);
    mpy += 8;

    // Volume row
    ui_.drawText("Volume:", mpx, mpy + 3, 11, UI::W98::Shadow);
    int vx = mpx + 56;
    if (ui_.win98Button(202, "<", vx, mpy, 22, 20, false)) {
        config_.musicVolume = std::max(0, config_.musicVolume - 8);
        Mix_VolumeMusic(config_.musicVolume);
        saveConfig();
    }
    vx += 24;
    char volBuf[10];
    snprintf(volBuf, sizeof(volBuf), "%d%%", config_.musicVolume * 100 / 128);
    ui_.drawText(volBuf, vx, mpy + 3, 11, UI::W98::Black);
    vx += 34;
    if (ui_.win98Button(203, ">", vx, mpy, 22, 20, false)) {
        config_.musicVolume = std::min(128, config_.musicVolume + 8);
        Mix_VolumeMusic(config_.musicVolume);
        saveConfig();
    }

    // AVA Explorer window
    if (browserOpen_) {
        const int BW = 720, BH = 560;
        const int TOOLBAR_H = 28;
        const int ADDR_H    = 24;
        const int STATUS_H  = 20;
        const int CONTENT_H = BH - UI::W98::TitleH - TOOLBAR_H - ADDR_H - STATUS_H;

        browserWinX_ = std::max(0, std::min(SCREEN_W - BW, browserWinX_));
        browserWinY_ = std::max(0, std::min(SCREEN_H - BH, browserWinY_));

        // drag
        bool oTit = ui_.pointInRect(ui_.mouseX, ui_.mouseY,
                        browserWinX_, browserWinY_, BW - 22, UI::W98::TitleH);
        if (oTit && ui_.mouseClicked) {
            browserDragging_ = true;
            browserDragOX_ = ui_.mouseX - browserWinX_;
            browserDragOY_ = ui_.mouseY - browserWinY_;
            ui_.mouseClicked = false;
        }
        if (!ui_.mouseDown) browserDragging_ = false;
        if (browserDragging_) {
            browserWinX_ = std::max(0, std::min(SCREEN_W - BW, ui_.mouseX - browserDragOX_));
            browserWinY_ = std::max(0, std::min(SCREEN_H - BH, ui_.mouseY - browserDragOY_));
        }

        // page metadata
        struct PageMeta { const char* url; const char* title; };
        static const PageMeta kPages[] = {
            { "start.ava",           "AVA Home - Welcome Portal"        },
            { "news.AVA:CORP",            "AVA NetNews - Today's Feed"        },
            { "corp.ava:online",     "AVA Corporation - Official"        },
            { "search.ava",          "SearchNet - The AVA Search Engine" },
            { "sector7.dark:relay4", "// SIGNAL RECOVERED //"            },
            { "proj.bliss:GOV",      "Project BLISS - City Administration" },
        };
        int pg = std::max(0, std::min(5, browserPage_));

        // close button check (before drawing so we can bail)
        const int cbSz = UI::W98::TitleH - 4;
        if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY,
                browserWinX_ + BW - 3 - cbSz, browserWinY_ + 2, cbSz, cbSz)) {
            browserOpen_ = false;
            ui_.mouseClicked = false;
        }

        if (browserOpen_) {
            char winTitle[80];
            snprintf(winTitle, sizeof(winTitle), "AVA Explorer - %s", kPages[pg].title);
            ui_.drawWin98Window(browserWinX_, browserWinY_, BW, BH, winTitle);

            int cx = browserWinX_;
            int cy = browserWinY_ + UI::W98::TitleH;

            // Toolbar
            SDL_SetRenderDrawColor(renderer_, 212, 208, 200, 255);
            SDL_Rect tbBg = {cx, cy, BW, TOOLBAR_H};
            SDL_RenderFillRect(renderer_, &tbBg);

            int tx = cx + 4;
            bool canBack = browserHistPos_ > 0;
            bool canFwd  = browserHistPos_ < browserHistLen_ - 1;

            // Back
            if (ui_.win98Button(500, "<", tx, cy + 4, 26, 20, false) && canBack) {
                browserHistPos_--;
                browserPage_ = browserHist_[browserHistPos_];
                browserScrollY_ = 0;
            }
            tx += 28;
            // Forward
            if (ui_.win98Button(501, ">", tx, cy + 4, 26, 20, false) && canFwd) {
                browserHistPos_++;
                browserPage_ = browserHist_[browserHistPos_];
                browserScrollY_ = 0;
            }
            tx += 32;

            // Home
            if (ui_.win98Button(502, "Home", tx, cy + 4, 60, 20, false)) {
                browserPage_ = 0; browserScrollY_ = 0;
                if (browserHistPos_ < 31) {
                    browserHistPos_++; browserHistLen_ = browserHistPos_ + 1;
                    browserHist_[browserHistPos_] = 0;
                }
            }
            tx += 62;

            // separator
            SDL_SetRenderDrawColor(renderer_, 128, 128, 128, 255);
            SDL_RenderDrawLine(renderer_, cx + tx - cx, cy + 4, cx + tx - cx, cy + TOOLBAR_H - 4);
            tx += 8;

            // address bar
            int addrX = tx, addrW = BW - (tx - cx) - 50;
            ui_.drawWin98Bevel(cx + addrX - cx, cy + 4, addrW, 20, false);
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
            SDL_Rect addrBg = {cx + addrX - cx + 2, cy + 6, addrW - 4, 16};
            SDL_RenderFillRect(renderer_, &addrBg);
            // clip + draw URL
            {
                SDL_Rect addrClip = {cx + addrX - cx + 2, cy + 4, addrW - 4, 20};
                SDL_Rect prevClip; SDL_RenderGetClipRect(renderer_, &prevClip);
                SDL_RenderSetClipRect(renderer_, &addrClip);
                ui_.drawText(kPages[pg].url, cx + addrX - cx + 5, cy + 8, 11, UI::W98::Black);
                SDL_RenderSetClipRect(renderer_, prevClip.w > 0 ? &prevClip : nullptr);
            }
            tx += addrW + 2;
            if (ui_.win98Button(503, "Go", cx + tx - cx, cy + 4, 40, 20, false)) { /* no-op */ }

            cy += TOOLBAR_H;

            // Content area
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
            SDL_Rect caBg = {cx + 2, cy, BW - 4, CONTENT_H};
            SDL_RenderFillRect(renderer_, &caBg);
            ui_.drawWin98Bevel(cx, cy - 1, BW, CONTENT_H + 2, false);

            SDL_Rect contentClip = {cx + 3, cy + 1, BW - 6, CONTENT_H - 2};
            SDL_Rect prevClip; SDL_RenderGetClipRect(renderer_, &prevClip);
            SDL_RenderSetClipRect(renderer_, &contentClip);

            // scroll when wheel moves over content area
            if (ui_.mouseWheelY != 0 &&
                ui_.pointInRect(ui_.mouseX, ui_.mouseY, cx + 3, cy + 1, BW - 6, CONTENT_H - 2)) {
                browserScrollY_ -= ui_.mouseWheelY * 24;
                if (browserScrollY_ < 0) browserScrollY_ = 0;
            }

            // nav helper - returns true if clicked, navigates to dest
            auto navTo = [&](int dest) {
                browserPage_ = dest; browserScrollY_ = 0;
                browserLoading_ = true; browserLoadTimer_ = 0.55f;
                if (browserHistPos_ < 31) {
                    browserHistPos_++; browserHistLen_ = browserHistPos_ + 1;
                    browserHist_[browserHistPos_] = dest;
                }
                if (sfxClick_) playSFX(sfxClick_, config_.sfxVolume);
            };
            auto linkBtn = [&](int id, const char* label, int x, int y, int w, int dest) {
                if (ui_.win98Button(id, label, x, y, w, 18, false)) navTo(dest);
            };

            SDL_Texture* icPage    = Assets::instance().loadRelTex("sprites/ui/browser_page.png");
            SDL_Texture* icConn    = Assets::instance().loadRelTex("sprites/ui/browser_connect.png");
            SDL_Texture* icSearch  = Assets::instance().loadRelTex("sprites/ui/browser_search.png");
            SDL_Texture* icDoc     = Assets::instance().loadRelTex("sprites/ui/browser_doc.png");

            auto drawIcon = [&](SDL_Texture* t, int x, int y, int sz = 16) {
                if (!t) return;
                SDL_Rect d = {x, y, sz, sz}; SDL_RenderCopy(renderer_, t, nullptr, &d);
            };

            // tick loading timer
            if (browserLoading_) {
                browserLoadTimer_ -= dt_;
                if (browserLoadTimer_ <= 0.0f) browserLoading_ = false;
            }

            int px = cx + 12;
            int py = cy + 10 - browserScrollY_;

            if (browserLoading_) {
                int midX = cx + BW / 2, midY = cy + CONTENT_H / 2 - 20;
                ui_.drawText("Opening page...", midX - 50, midY, 12, UI::W98::Shadow);
                float prog = 1.0f - (browserLoadTimer_ / 0.55f);
                int barW = 240, barH = 16;
                int barX = midX - barW / 2, barY = midY + 22;
                ui_.drawWin98Bevel(barX - 2, barY - 2, barW + 4, barH + 4, false);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
                SDL_Rect barBg = {barX, barY, barW, barH}; SDL_RenderFillRect(renderer_, &barBg);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
                int fillW = std::max(0, (int)(barW * prog));
                SDL_Rect barFg = {barX, barY, fillW, barH}; SDL_RenderFillRect(renderer_, &barFg);
                int dots = (int)(SDL_GetTicks() / 300) % 4;
                char dotBuf[8]; snprintf(dotBuf, sizeof(dotBuf), "%.*s", dots, "...");
                ui_.drawText(dotBuf, midX + 54, midY, 12, UI::W98::Shadow);
            } else switch (pg) {
            // PAGE 0: AVA Home
            case 0: {
                // banner
                SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
                SDL_Rect banner = {cx + 2, cy, BW - 4, 44};
                SDL_RenderFillRect(renderer_, &banner);
                ui_.drawText("AVA ONLINE PORTAL", cx + 12, cy + 6, 16, {255, 255, 255, 255});
                ui_.drawText("Your gateway to the connected world.", cx + 12, cy + 26, 11, {180, 210, 255, 255});
                py = cy + 56;

                ui_.drawWin98Bevel(cx + 4, py, BW - 8, 2, false); py += 8;
                ui_.drawText("Quick Access", cx + 12, py, 12, UI::W98::Navy); py += 18;

                drawIcon(icPage, cx + 14, py + 1); ui_.drawText("AVA NetNews", cx + 34, py + 2, 11, {0, 0, 200, 255});
                if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, cx + 14, py, 200, 16)) { navTo(1); ui_.mouseClicked=false; }
                py += 20;
                drawIcon(icConn, cx + 14, py + 1); ui_.drawText("AVA Corporation", cx + 34, py + 2, 11, {0, 0, 200, 255});
                if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, cx + 14, py, 200, 16)) { navTo(2); ui_.mouseClicked=false; }
                py += 20;
                drawIcon(icSearch, cx + 14, py + 1); ui_.drawText("SearchNet", cx + 34, py + 2, 11, {0, 0, 200, 255});
                if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, cx + 14, py, 200, 16)) { navTo(3); ui_.mouseClicked=false; }
                py += 20;
                drawIcon(icDoc, cx + 14, py + 1); ui_.drawText("sector7.dark:relay4", cx + 34, py + 2, 11, {80, 0, 0, 255});
                if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, cx + 14, py, 240, 16)) { navTo(4); ui_.mouseClicked=false; }
                py += 20;
                drawIcon(icConn, cx + 14, py + 1); ui_.drawText("proj.bliss:GOV", cx + 34, py + 2, 11, {0, 0, 200, 255});
                if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY, cx + 14, py, 240, 16)) { navTo(5); ui_.mouseClicked=false; }
                py += 28;

                ui_.drawWin98Bevel(cx + 4, py, BW - 8, 2, false); py += 8;
                ui_.drawText("AVA Network Status", cx + 12, py, 12, UI::W98::Navy); py += 16;
                static const char* kStatus[] = {
                    "Sector 1-6: ONLINE", "Sector 7: DISRUPTED", "Sector 8-12: ONLINE",
                    "Backbone fiber: 100%", "Public relay uptime: 99.1%"
                };
                for (auto& s : kStatus) {
                    SDL_Color sc = (std::string(s).find("DISRUPTED") != std::string::npos)
                        ? SDL_Color{180,0,0,255} : SDL_Color{0,120,0,255};
                    ui_.drawText(s, cx + 12, py, 11, sc);
                    py += 14;
                }
                break;
            }
            // PAGE 1: NetNews
            case 1: {
                SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
                SDL_Rect banner = {cx + 2, cy, BW - 4, 36};
                SDL_RenderFillRect(renderer_, &banner);
                ui_.drawText("AVA NETNEWS", cx + 12, cy + 4, 16, {255,255,255,255});
                ui_.drawText("Trusted. Verified. Delivered.", cx + 12, cy + 22, 10, {180,210,255,255});
                py = cy + 48;

                struct Headline { const char* cat; const char* head; const char* body; bool alert; };
                static const Headline kNews[] = {
                    {"TECHNOLOGY", "PROJECT LONGHORN: NEW AVA OS CONFIRMED",
                     "AVA Corp. announces a next-generation operating system kernel targeting hydraulics and bio-link support. Expected rollout: Q3.", false},
                    {"INFRASTRUCTURE", "SECTOR 7 RELAY STATION REMAINS OFFLINE",
                     "Engineers report ongoing interference. Traffic is being rerouted through Sector 6-B. Citizens should expect 12% latency increase.", false},
                    {"HEALTH", "OCULAR LINK UPDATE 2.4 NOW AVAILABLE",
                     "Department of Health clears the latest optic firmware for all registered citizens. Visit your nearest clinic.", false},
                    {"ALERT", "UNREGISTERED UNIT DETECTED IN ZONE 9",
                     "Authorities have issued a level-2 advisory. All citizens are advised to avoid the lower industrial district until further notice.", true},
                    {"CULTURE", "FOTOSHOPPE CO. RELEASES NEW AMBIENT COMPILATION",
                     "The popular music label drops 'Home Screen (Extended)', available on AVA Music.", false},
                };
                for (int ni = 0; ni < 5; ni++) {
                    SDL_Color catC = kNews[ni].alert ? SDL_Color{200,0,0,255} : UI::W98::Navy;
                    ui_.drawText(kNews[ni].cat, cx + 12, py, 9, catC); py += 12;
                    ui_.drawText(kNews[ni].head, cx + 12, py, 12, UI::W98::Black); py += 14;
                    int bh = ui_.drawTextWrapped(kNews[ni].body, cx + 12, py, 10, BW - 30, UI::W98::Shadow);
                    py += bh + 12;
                    SDL_SetRenderDrawColor(renderer_, 200, 200, 200, 255);
                    SDL_RenderDrawLine(renderer_, cx + 8, py, cx + BW - 12, py);
                    py += 8;
                }
                break;
            }
            // PAGE 2: AVA Corp
            case 2: {
                SDL_SetRenderDrawColor(renderer_, 20, 20, 60, 255);
                SDL_Rect banner = {cx + 2, cy, BW - 4, 52};
                SDL_RenderFillRect(renderer_, &banner);
                ui_.drawText("AVA CORPORATION", cx + 12, cy + 6, 18, {200, 220, 255, 255});
                ui_.drawText("Building Tomorrow's World - Today.", cx + 12, cy + 30, 11, {140, 160, 220, 255});
                ui_.drawText("corp.ava:online | Est. 1987 | Citizens served: 2.1B", cx + 12, cy + 42, 9, {100,120,180,255});
                py = cy + 64;

                ui_.drawText("OUR DIVISIONS", cx + 12, py, 13, UI::W98::Navy); py += 18;

                struct Div { const char* name; const char* desc; };
                static const Div kDivs[] = {
                    {"AVA OS", "The operating system powering 94% of the world's public infrastructure."},
                    {"AVA Med", "Bio-link and neural interface healthcare solutions for every citizen."},
                    {"AVA Net", "Global backbone fiber and relay network management."},
                    {"AVA Sec", "Autonomous security and civil enforcement framework licensing."},
                    {"AVA Agri", "Automated agricultural synthesis and caloric supply chain."},
                };
                for (auto& d : kDivs) {
                    drawIcon(icConn, cx + 12, py + 1);
                    ui_.drawText(d.name, cx + 32, py + 1, 12, UI::W98::Black); py += 14;
                    int bh = ui_.drawTextWrapped(d.desc, cx + 32, py, 10, BW - 50, UI::W98::Shadow);
                    py += bh + 10;
                }

                py += 4;
                ui_.drawWin98Bevel(cx + 4, py, BW - 8, 2, false); py += 8;
                ui_.drawText("AVA Corp. All rights reserved. Citizens are reminded that use of non-AVA networking tools",
                    cx + 12, py, 9, UI::W98::Shadow); py += 12;
                ui_.drawText("may violate Compliance Order 77-B. Report violations to Sector Authority.",
                    cx + 12, py, 9, UI::W98::Shadow);
                break;
            }
            // PAGE 3: SearchNet
            case 3: {
                py = cy + 30;
                // Logo
                ui_.drawText("Search", cx + BW/2 - 80, py, 24, UI::W98::Navy);
                ui_.drawText("Net", cx + BW/2 + 10, py, 24, {180, 0, 0, 255}); py += 34;
                ui_.drawText("The AVA Search Engine", cx + BW/2 - 70, py, 10, UI::W98::Shadow); py += 24;

                // search box (decorative)
                ui_.drawWin98Bevel(cx + BW/2 - 200, py, 340, 24, false);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
                SDL_Rect sb = {cx + BW/2 - 198, py + 2, 336, 20}; SDL_RenderFillRect(renderer_, &sb);
                ui_.drawText("_", cx + BW/2 - 194, py + 5, 11, UI::W98::Shadow);
                py += 32;
                ui_.win98Button(510, "Search", cx + BW/2 - 36, py, 72, 22, false);
                ui_.win98Button(511, "Feeling Lucky", cx + BW/2 + 44, py, 100, 22, false);
                py += 40;

                ui_.drawWin98Bevel(cx + 4, py, BW - 8, 2, false); py += 10;
                ui_.drawText("RECENT SEARCHES", cx + 12, py, 10, UI::W98::Shadow); py += 14;
                static const char* kSearches[] = {
                    "avacorp sector 7 incident", "\"PROJECT LONGHORN\" leak",
                    "zone 9 what happened", "relay4 signal source",
                    "how to remove neural link", "underground net access"
                };
                for (auto& s : kSearches) {
                    drawIcon(icSearch, cx + 12, py);
                    ui_.drawText(s, cx + 32, py + 1, 11, {0,0,180,255});
                    py += 16;
                }
                break;
            }
            // PAGE 4: Underground
            case 4: {
                int glitch = (int)(sinf(SDL_GetTicks() * 0.031f) * 3);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
                SDL_Rect bg4 = {cx + 2, cy, BW - 4, CONTENT_H}; SDL_RenderFillRect(renderer_, &bg4);

                static const char* kLines[] = {
                    "UNSECURE CONNECTION",
                    "",
                    "if you are reading this, your terminal is made by AVA Corp. and running AVA OS.",
                    "don't use your real name. don't use AVA-linked hardware.",
                    "",
                    "INCIDENT LOG - ZONE 9 - [CLASSIFIED]",
                    "  unit count:      1",
                    "  casualty fig.:   0",
                    "  authorization:   forgotten war hardware, trivial bypass",
                    "  status:          locked by operator N1",
                    "",
                    "If possible, avoid killing civilians, we don't need serious attention",
                    "",
                    "",
                    "",
                    "",
                    "",
                    "this relay will be taken down, good luck, N1.",
                    "",
                    "EOF",
                };
                py = cy + 12;
                for (int li = 0; li < 19; li++) {
                    SDL_Color lc = (li == 0) ? SDL_Color{0,255,100,255}
                                 : (kLines[li][0] == ' ') ? SDL_Color{160,160,160,255}
                                 : SDL_Color{0,220,80,255};
                    if (li == 0 || li == 5 || li == 13)
                        lc = {0, 255, 100, 255};
                    int jx = (li % 3 == 0) ? glitch : 0;
                    ui_.drawText(kLines[li], cx + 14 + jx, py, 11, lc);
                    py += 15;
                }
                break;
            }
            // PAGE 5: proj.bliss
            case 5: {
                // header bar - calm government green
                SDL_SetRenderDrawColor(renderer_, 30, 100, 50, 255);
                SDL_Rect hdr5 = {cx + 2, cy, BW - 4, 32};
                SDL_RenderFillRect(renderer_, &hdr5);
                ui_.drawText("CITY ADMINISTRATION PORTAL", cx + 12, cy + 8, 14, {255, 255, 255, 255});
                py = cy + 44;

                // image - capped at 220px tall, aspect-ratio preserved
                SDL_Texture* blissTex = Assets::instance().loadRelTex("sprites/projbliss.png");
                if (blissTex) {
                    int tw = 0, th = 0;
                    SDL_QueryTexture(blissTex, nullptr, nullptr, &tw, &th);
                    int dw = BW - 24;
                    int dh = (th > 0) ? dw * th / tw : dw;
                    if (dh > 220) { dh = 220; dw = (tw > 0) ? dh * tw / th : 220; }
                    SDL_Rect dst = {cx + (BW - dw) / 2, py, dw, dh};
                    SDL_RenderCopy(renderer_, blissTex, nullptr, &dst);
                    py += dh + 10;
                } else {
                    // fallback placeholder
                    SDL_SetRenderDrawColor(renderer_, 160, 200, 160, 255);
                    SDL_Rect ph = {cx + 12, py, BW - 30, 80};
                    SDL_RenderFillRect(renderer_, &ph);
                    ui_.drawText("[image: projbliss.png]", cx + 20, py + 30, 11, UI::W98::Shadow);
                    py += 90;
                }

                // announcement text
                ui_.drawText("New zone is soon to be opened!", cx + 12, py, 13, {30, 100, 50, 255});
                py += 18;
                ui_.drawWin98Bevel(cx + 4, py, BW - 8, 2, false); py += 8;

                static const char* kBlissText[] = {
                    "Hello, fellow citizens! The city and AVA Corp are happy to announce",
                    "that subzone 12F - BLISS - is about to be opened for public use",
                    "and housing!",
                    "",
                    "The new beautiful park and expanded housing space will allow more",
                    "people to enjoy our city. Opening day is speculated to be set as",
                    "the date of the AVA Longhorn release, which will be announced",
                    "tomorrow.",
                    "",
                    "Thank you for reading this.",
                    "- City Administration",
                };
                for (auto& line : kBlissText) {
                    if (line[0] == '\0') { py += 6; continue; }
                    SDL_Color lc = (line[0] == '\xe2' || std::string(line).find("City Administration") != std::string::npos)
                        ? UI::W98::Shadow : UI::W98::Black;
                    ui_.drawText(line, cx + 12, py, 11, lc);
                    py += 14;
                }
                break;
            }
            }

            SDL_RenderSetClipRect(renderer_, prevClip.w > 0 ? &prevClip : nullptr);

            // Status bar
            int sbY = browserWinY_ + BH - STATUS_H;
            SDL_SetRenderDrawColor(renderer_, 212, 208, 200, 255);
            SDL_Rect sbBg = {cx + 2, sbY, BW - 4, STATUS_H};
            SDL_RenderFillRect(renderer_, &sbBg);
            ui_.drawWin98Bevel(cx + 2, sbY, BW - 4, STATUS_H, true);
            char statusLine[80];
            snprintf(statusLine, sizeof(statusLine), "Done    %s", kPages[pg].url);
            ui_.drawText(statusLine, cx + 6, sbY + 4, 10, UI::W98::Black);
        }
    }

    // Credits window
    if (creditsOpen_) {
        const int CW = 420, CH = 380;
        if (!creditsInit_) {
            creditsInit_ = true;
            creditsWinX_ = (SCREEN_W - CW) / 2;
            creditsWinY_ = (SCREEN_H - CH) / 2;
        }
        creditsWinX_ = std::max(0, std::min(SCREEN_W - CW, creditsWinX_));
        creditsWinY_ = std::max(0, std::min(SCREEN_H - CH, creditsWinY_));

        bool oTit = ui_.pointInRect(ui_.mouseX, ui_.mouseY,
                        creditsWinX_, creditsWinY_, CW - 22, UI::W98::TitleH);
        if (oTit && ui_.mouseClicked) {
            creditsDragging_ = true;
            creditsDragOX_ = ui_.mouseX - creditsWinX_;
            creditsDragOY_ = ui_.mouseY - creditsWinY_;
            ui_.mouseClicked = false;
        }
        if (!ui_.mouseDown) creditsDragging_ = false;
        if (creditsDragging_) {
            creditsWinX_ = std::max(0, std::min(SCREEN_W - CW, ui_.mouseX - creditsDragOX_));
            creditsWinY_ = std::max(0, std::min(SCREEN_H - CH, ui_.mouseY - creditsDragOY_));
        }

        // close button
        const int cbSz = UI::W98::TitleH - 4;
        if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY,
                creditsWinX_ + CW - 3 - cbSz, creditsWinY_ + 2, cbSz, cbSz)) {
            creditsOpen_ = false;
            ui_.mouseClicked = false;
        }

        if (creditsOpen_) {
            ui_.drawWin98Window(creditsWinX_, creditsWinY_, CW, CH, "Credits");
            int cx = creditsWinX_ + 10;
            int cy = creditsWinY_ + UI::W98::TitleH + 10;


            struct Section { const char* title; const char* entries[6]; int n; };
            static const Section kSections[] = {
                { "DEVELOPMENT", {
                    "etonedemid",
                    nullptr, nullptr, nullptr, nullptr, nullptr
                }, 1 },
                { "MUSIC", {
                    "FOTOSHOPPE CO.",
                    "kyledeadman", nullptr, nullptr, nullptr, nullptr
                }, 2 },
                { "TECHNOLOGY", {
                    "SDL2 - libsdl.org",
                    "SDL2_image, SDL2_ttf, SDL2_mixer",
                    "ENet - sauerbraten.org/enet",
                    nullptr, nullptr, nullptr
                }, 3 },
                { "SPECIAL THANKS", {
                    "github.com/1j01/98 (Win98 icons)",
                    "pixabay.com",
                    "deav",
                    nullptr, nullptr, nullptr
                }, 3 },
                { "Misc", {
                    "",
                    "",
                    nullptr, nullptr, nullptr, nullptr
                }, 2 },
            };

            for (auto& sec : kSections) {
                // section header
                SDL_SetRenderDrawColor(renderer_, 200, 200, 200, 255);
                SDL_RenderDrawLine(renderer_, cx, cy + 7, creditsWinX_ + CW - 10, cy + 7);
                ui_.drawText(sec.title, cx, cy, 10, UI::W98::Shadow);
                cy += 13;
                for (int e = 0; e < sec.n; e++) {
                    if (!sec.entries[e]) break;
                    ui_.drawText(sec.entries[e], cx + 8, cy, 11, UI::W98::Black);
                    cy += 14;
                }
                cy += 4;
            }

            // bottom close button
            const int btnW2 = 80, btnH2 = 22;
            int bbX = creditsWinX_ + (CW - btnW2) / 2;
            int bbY = creditsWinY_ + CH - btnH2 - 8;
            if (ui_.win98Button(600, "Close", bbX, bbY, btnW2, btnH2, false))
                creditsOpen_ = false;
        }
    }

    // Log-off confirmation dialog (modal overlay)
    if (logOffConfirm_) {
        ui_.drawDarkOverlay(160);
        const int dlgW = 320, dlgH = 116;
        const int dlgX = (SCREEN_W - dlgW) / 2;
        const int dlgY = (SCREEN_H - dlgH) / 2;
        ui_.drawWin98Window(dlgX, dlgY, dlgW, dlgH, "Log Off");
        ui_.drawText("Are you sure?", dlgX + 16, dlgY + UI::W98::TitleH + 14, 13, UI::W98::Black);
        const int btnY = dlgY + dlgH - 40;
        if (ui_.win98Button(90, "Yes", dlgX + dlgW/2 - 86, btnY, 76, 26, menuSelection_ == 90)) {
            menuSelection_ = 90; confirmInput_ = true;
        }
        if (ui_.win98Button(91, "No", dlgX + dlgW/2 + 10, btnY, 76, 26, menuSelection_ == 91)) {
            menuSelection_ = 91; confirmInput_ = true;
        }
        if (ui_.hoveredItem == 90 && !usingGamepad_) menuSelection_ = 90;
        if (ui_.hoveredItem == 91 && !usingGamepad_) menuSelection_ = 91;
    }

    // Disconnect reason popup
    if (!disconnectReason_.empty()) {
        ui_.drawDarkOverlay(160);
        const int dw = 360, dh = 100;
        const int dx = (SCREEN_W - dw) / 2, dy = (SCREEN_H - dh) / 2;
        ui_.drawWin98Window(dx, dy, dw, dh, "Disconnected");
        ui_.drawText(disconnectReason_.c_str(), dx + 14, dy + UI::W98::TitleH + 14, 13, UI::W98::Black);
        int btnW = 80, btnY2 = dy + dh - 36;
        if (ui_.win98Button(700, "OK", dx + dw/2 - btnW/2, btnY2, btnW, 26, false)) {
            disconnectReason_.clear();
        }
    }

    if (mainMenuSub_ == 1) renderToolsMenu();
}

void Game::renderToolsMenu() {
    const int padX   = 14;
    const int btnH   = 26;
    const int btnGap = 6;
    const int btnW   = 200;
    const int winW   = btnW + padX * 2;
    const int winH   = UI::W98::TitleH + 14 + 3 * (btnH + btnGap) + 4;
    const int winX   = (SCREEN_W - winW) / 2;
    const int winY   = (SCREEN_H - winH) / 2;

    ui_.drawDarkOverlay(80);
    ui_.drawWin98Window(winX, winY, winW, winH, "Tools");

    const int cbSz = UI::W98::TitleH - 4;
    if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY,
            winX + winW - 3 - cbSz, winY + 2, cbSz, cbSz)) {
        mainMenuSub_ = 0;
        menuSelection_ = 2;
        ui_.mouseClicked = false;
        return;
    }

    const char* labels[] = {"Map Editor", "Character Editor", "Sprite Editor"};
    int by = winY + UI::W98::TitleH + 14;
    for (int i = 0; i < 3; i++) {
        bool sel = (menuSelection_ == i);
        if (ui_.win98Button(i, labels[i], winX + padX, by, btnW, btnH, sel)) {
            menuSelection_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) menuSelection_ = i;
        by += btnH + btnGap;
    }
    ui_.drawWin98StatusBar(SCREEN_H - 26, "Esc: Back");
}

void Game::renderPlayModeMenu() {
    ui_.drawDesktop();

    // Window layout
    const int winW  = 500;
    const int padX  = 14;
    const int padTY = UI::W98::TitleH + 14;
    const int btnH  = 26;
    const int rowH  = 24;
    const int gap   = 6;

    // Count rows for height: 3 mode buttons + separator label + 6 sliders + back
    const int totalRows = 3 + 1 + 6 + 1;
    const int winH  = padTY + totalRows * (btnH + gap) + 30;
    const int winX  = (SCREEN_W - winW) / 2;
    const int winY  = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Play");

    // X button returns to main menu
    {
        const int cbSz = UI::W98::TitleH - 4;
        if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY,
                winX + winW - 3 - cbSz, winY + 5, cbSz, cbSz)) {
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
            return;
        }
    }

    const int labelColW = 200;
    const int ctrlX     = winX + padX + labelColW;
    const int ctrlW     = winW - padX - labelColW - padX;
    int bx = winX + padX;
    int by = winY + padTY;

    // Mode buttons (0-2)
    const char* modeLabels[] = {"Generated Map", "Custom Map", "Map Pack"};
    for (int i = 0; i < 3; i++) {
        bool sel = (playModeSelection_ == i);
        if (ui_.win98Button(i, modeLabels[i], bx, by, winW - padX*2, btnH, sel)) {
            playModeSelection_ = i; menuSelection_ = i; confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) { playModeSelection_ = i; menuSelection_ = i; }
        by += btnH + gap;
    }

    // Settings group separator
    by += 4;
    ui_.drawWin98Bevel(bx, by, winW - padX*2, 2, false);
    by += 4;
    ui_.drawText("Settings", bx, by, 12, UI::W98::Shadow);
    by += 16;

    // Slider rows (3-8)
    char valBuf[64];
    struct PMRow { const char* label; int idx; };
    PMRow rows[] = {
        {"Map Width:",      3}, {"Map Height:",    4},
        {"Player HP:",      5}, {"Spawnrate:",     6},
        {"Enemy HP:",       7}, {"Enemy Speed:",   8},
    };

    auto fmtVal = [&](int idx) -> const char* {
        switch (idx) {
            case 3: snprintf(valBuf,sizeof(valBuf),"%d",   config_.mapWidth);        break;
            case 4: snprintf(valBuf,sizeof(valBuf),"%d",   config_.mapHeight);       break;
            case 5:
                if (hpTyping_) snprintf(valBuf,sizeof(valBuf),"%s%c", hpStr_.c_str(), (int)(gameTime_*3)%2==0?'_':' ');
                else           snprintf(valBuf,sizeof(valBuf),"%d",   config_.playerMaxHp);
                break;
            case 6: snprintf(valBuf,sizeof(valBuf),"%.1fx",config_.spawnRateScale);  break;
            case 7: snprintf(valBuf,sizeof(valBuf),"%.1fx",config_.enemyHpScale);    break;
            case 8: snprintf(valBuf,sizeof(valBuf),"%.1fx",config_.enemySpeedScale); break;
            default: valBuf[0]=0; break;
        }
        return valBuf;
    };

    for (auto& row : rows) {
        bool sel = (playModeSelection_ == row.idx);
        // Label
        ui_.drawText(row.label, bx, by + (rowH - 14) / 2, 13, UI::W98::Black);
        // Value field + arrow buttons: [<] [value field] [>]
        const int arrowW = 22;
        int fx   = ctrlX;                       // < button starts here
        int fw   = ctrlW - arrowW * 2 - 4;     // value field width
        int fldX = fx + arrowW + 2;             // value field x
        int rgtX = fldX + fw + 2;              // > button x
        // Left arrow
        if (ui_.win98Button(row.idx*10+50, "<", fx, by, arrowW, rowH, false)) {
            switch (row.idx) {
                case 3: config_.mapWidth        = std::max(20,   config_.mapWidth        - 2);    break;
                case 4: config_.mapHeight       = std::max(14,   config_.mapHeight       - 2);    break;
                case 5: config_.playerMaxHp     = std::max(1,    config_.playerMaxHp     - 1);    break;
                case 6: config_.spawnRateScale  = std::max(0.3f, config_.spawnRateScale  - 0.1f); break;
                case 7: config_.enemyHpScale    = std::max(0.3f, config_.enemyHpScale    - 0.1f); break;
                case 8: config_.enemySpeedScale = std::max(0.5f, config_.enemySpeedScale - 0.1f); break;
            }
        }
        // Value display field
        ui_.drawWin98TextField(fldX, by, fw, rowH, fmtVal(row.idx), sel, false, 0.f);
        // Right arrow
        if (ui_.win98Button(row.idx*10+60, ">", rgtX, by, arrowW, rowH, false)) {
            switch (row.idx) {
                case 3: config_.mapWidth        = std::min(120,  config_.mapWidth        + 2);    break;
                case 4: config_.mapHeight       = std::min(80,   config_.mapHeight       + 2);    break;
                case 5: config_.playerMaxHp     = std::min(1000, config_.playerMaxHp     + 1);    break;
                case 6: config_.spawnRateScale  = std::min(3.0f, config_.spawnRateScale  + 0.1f); break;
                case 7: config_.enemyHpScale    = std::min(3.0f, config_.enemyHpScale    + 0.1f); break;
                case 8: config_.enemySpeedScale = std::min(2.5f, config_.enemySpeedScale + 0.1f); break;
            }
        }
        if (ui_.hoveredItem == row.idx && !usingGamepad_) { playModeSelection_ = row.idx; menuSelection_ = row.idx; }
        by += rowH + gap;
    }

    // Back button (9)
    by += 4;
    bool backSel = (playModeSelection_ == 9);
    if (ui_.win98Button(9, "Cancel", bx, by, 80, btnH, backSel)) {
        playModeSelection_ = 9; menuSelection_ = 9; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 9 && !usingGamepad_) { playModeSelection_ = 9; menuSelection_ = 9; }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Select a play mode or adjust settings.");
}

void Game::renderConfigMenu() {
    ui_.drawDesktop();

    // Two-column layout: left = labels, right = controls
    const int winW   = 560;
    const int padX   = 14;
    const int padTY  = UI::W98::TitleH + 14;
    const int rowH   = 22;
    const int rowGap = 6;
    const int labelW = 180;
    const int arrowW = 22;

    // Count rows to size window
    // sliders: 5 (or 6 on PC), toggles: 7, username: 1, back: 1 = 14-15
#ifndef __SWITCH__
    const int numSliders = 6;
#else
    const int numSliders = 5;
#endif
    const int numToggles = 7;
    const int numRows = numSliders + 1 + numToggles + 1 + 1 + 1 + 1; // +sep+user+uiscale+back
    const int winH = padTY + numRows * (rowH + rowGap) + 16;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = std::max(4, (SCREEN_H - winH) / 2);
    ui_.drawWin98Window(winX, winY, winW, winH, "Config");

    // X button saves and returns to main menu
    {
        const int cbSz = UI::W98::TitleH - 4;
        if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX, ui_.mouseY,
                winX + winW - 3 - cbSz, winY + 5, cbSz, cbSz)) {
            saveConfig();
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
            return;
        }
    }

    const int lx  = winX + padX;
    const int fx  = lx + labelW;
    const int fwA = winW - padX - labelW - arrowW*2 - 8 - padX; // field width with arrows
    const int fwN = winW - padX - labelW - padX;                  // field width no arrows
    int y = winY + padTY;

    char valBuf[64];

    // Slider rows
    struct SliderRow { const char* label; int idx; };
    SliderRow srows[] = {
        {"Music Volume:", 4}, {"SFX Volume:", 5},
        {"Player HP:", 0}, {"Spawnrate:", 1},
        {"Enemy HP:", 2}, {"Enemy Speed:", 3},
    };
    auto fmtS = [&](int idx) -> const char* {
        switch (idx) {
            case 0:
                if (hpTyping_) snprintf(valBuf,sizeof(valBuf),"%s%c", hpStr_.c_str(), (int)(gameTime_*3)%2==0?'_':' ');
                else           snprintf(valBuf,sizeof(valBuf),"%d",   config_.playerMaxHp);
                break;
            case 1: snprintf(valBuf,sizeof(valBuf),"%.1fx", config_.spawnRateScale); break;
            case 2: snprintf(valBuf,sizeof(valBuf),"%.1fx", config_.enemyHpScale);  break;
            case 3: snprintf(valBuf,sizeof(valBuf),"%.1fx", config_.enemySpeedScale); break;
            case 4: snprintf(valBuf,sizeof(valBuf),"%d%%",  config_.musicVolume*100/128); break;
            case 5: snprintf(valBuf,sizeof(valBuf),"%d%%",  config_.sfxVolume*100/128);   break;
            default: valBuf[0]=0; break;
        }
        return valBuf;
    };

    for (auto& row : srows) {
        bool sel = (configSelection_ == row.idx);
        ui_.drawText(row.label, lx, y+(rowH-12)/2, 12, UI::W98::Black);

        // < button
        if (ui_.win98Button(row.idx*10+100, "<", fx, y, arrowW, rowH, false)) {
            int d=-1;
            switch(row.idx){
                case 0: config_.playerMaxHp    =std::max(1,   std::min(1000,config_.playerMaxHp    +d));       break;
                case 1: config_.spawnRateScale =std::max(0.3f,std::min(3.0f,config_.spawnRateScale +d*0.1f));  break;
                case 2: config_.enemyHpScale   =std::max(0.3f,std::min(3.0f,config_.enemyHpScale   +d*0.1f));  break;
                case 3: config_.enemySpeedScale=std::max(0.5f,std::min(2.5f,config_.enemySpeedScale+d*0.1f));  break;
                case 4: config_.musicVolume    =std::max(0,   std::min(128, config_.musicVolume    +d*8));  Mix_VolumeMusic(config_.musicVolume); break;
                case 5: config_.sfxVolume      =std::max(0,   std::min(128, config_.sfxVolume      +d*8));  break;
            }
        }
        ui_.drawWin98TextField(fx+arrowW+2, y, fwA, rowH, fmtS(row.idx), sel, false, 0.f);
        // > button
        if (ui_.win98Button(row.idx*10+110, ">", fx+arrowW+2+fwA+2, y, arrowW, rowH, false)) {
            int d=1;
            switch(row.idx){
                case 0: config_.playerMaxHp    =std::max(1,   std::min(1000,config_.playerMaxHp    +d));       break;
                case 1: config_.spawnRateScale =std::max(0.3f,std::min(3.0f,config_.spawnRateScale +d*0.1f));  break;
                case 2: config_.enemyHpScale   =std::max(0.3f,std::min(3.0f,config_.enemyHpScale   +d*0.1f));  break;
                case 3: config_.enemySpeedScale=std::max(0.5f,std::min(2.5f,config_.enemySpeedScale+d*0.1f));  break;
                case 4: config_.musicVolume    =std::max(0,   std::min(128, config_.musicVolume    +d*8));  Mix_VolumeMusic(config_.musicVolume); break;
                case 5: config_.sfxVolume      =std::max(0,   std::min(128, config_.sfxVolume      +d*8));  break;
            }
        }
        if (ui_.hoveredItem == row.idx && !usingGamepad_) { configSelection_=row.idx; menuSelection_=row.idx; }
        y += rowH + rowGap;
    }

    // Separator
    ui_.drawWin98Bevel(lx, y, winW-padX*2, 2, false);
    y += 8;

    // Toggle rows
    struct TogRow { const char* label; int idx; bool* val; };
    TogRow toggleRows[] = {
        {"CRT Filter",        CONFIG_SHADER_CRT_INDEX,        &config_.shaderCRT},
        {"Chromatic Aberr.",  CONFIG_SHADER_CHROMATIC_INDEX,  &config_.shaderChromatic},
        {"Scanlines",         CONFIG_SHADER_SCANLINES_INDEX,  &config_.shaderScanlines},
        {"Glow Pass",         CONFIG_SHADER_GLOW_INDEX,       &config_.shaderGlow},
        {"Glitch Strips",     CONFIG_SHADER_GLITCH_INDEX,     &config_.shaderGlitch},
        {"Neon Edge",         CONFIG_SHADER_NEON_INDEX,       &config_.shaderNeonEdge},
        {"Save Incoming Mods",CONFIG_SAVE_INCOMING_MODS_INDEX,&config_.saveIncomingModsPermanently},
    };
    for (auto& row : toggleRows) {
        bool sel = (configSelection_ == row.idx);
        ui_.drawText(row.label, lx, y+(rowH-12)/2, 12, UI::W98::Black);
        // Checkbox-style button showing ON/OFF
        char tbuf[8]; snprintf(tbuf,sizeof(tbuf),*row.val?"ON":"OFF");
        if (ui_.win98Button(row.idx, tbuf, fx, y, 50, rowH, sel)) {
            configSelection_=row.idx; menuSelection_=row.idx;
            confirmInput_=true;
        }
        if (ui_.hoveredItem==row.idx && !usingGamepad_) { configSelection_=row.idx; menuSelection_=row.idx; }
        y += rowH + rowGap;
    }

    // Screen Shake slider
    {
        bool sel = (configSelection_ == CONFIG_SHAKE_INDEX);
        ui_.drawText("Screen Shake:", lx, y+(rowH-12)/2, 12, UI::W98::Black);
        if (ui_.win98Button(CONFIG_SHAKE_INDEX*10+100, "<", fx, y, arrowW, rowH, false))
            config_.shakeScale = std::max(0.0f, config_.shakeScale - 0.1f);
        char shakeBuf[16]; snprintf(shakeBuf, sizeof(shakeBuf), "%.0f%%", config_.shakeScale * 100.f);
        ui_.drawWin98TextField(fx + arrowW + 2, y, fwA, rowH, shakeBuf, sel);
        if (ui_.win98Button(CONFIG_SHAKE_INDEX*10+101, ">", fx + arrowW + 2 + fwA + 2, y, arrowW, rowH, false))
            config_.shakeScale = std::min(1.0f, config_.shakeScale + 0.1f);
        if (ui_.hoveredItem == CONFIG_SHAKE_INDEX*10+100 || ui_.hoveredItem == CONFIG_SHAKE_INDEX*10+101)
            { configSelection_=CONFIG_SHAKE_INDEX; menuSelection_=CONFIG_SHAKE_INDEX; }
        y += rowH + rowGap;
    }


    // Back button
    {
        bool sel = (configSelection_ == CONFIG_BACK_INDEX);
        if (ui_.win98Button(CONFIG_BACK_INDEX, "OK", lx, y, 70, 26, sel)) {
            configSelection_=CONFIG_BACK_INDEX; menuSelection_=CONFIG_BACK_INDEX; confirmInput_=true;
        }
        if (ui_.hoveredItem==CONFIG_BACK_INDEX && !usingGamepad_) { configSelection_=CONFIG_BACK_INDEX; menuSelection_=CONFIG_BACK_INDEX; }
    }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Click < > to adjust values.  OK to save and close.");
}

void Game::renderPauseMenu() {
    // Keep gameplay visible underneath with a dark tint
    ui_.drawDarkOverlay(140, 4, 6, 14);

    // Win98 dialog window on top
    const int winW = 300;
    const int btnH = 26;
    const int rowH = 22;
    const int padX = 14;
    const int padTY = UI::W98::TitleH + 14;
    const int gap = 6;

#ifndef __SWITCH__
    const int numRows = 5; // Resume, Music, SFX, Fullscreen, Main Menu
#else
    const int numRows = 4; // Resume, Music, SFX, Main Menu
#endif
    const int winH = padTY + numRows * (btnH + gap) + 16;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Paused");

    const int labelW = 90;
    const int arrowW = 20;
    const int bx = winX + padX;
    const int fx = bx + labelW;
    const int fwA = winW - padX - labelW - arrowW*2 - 6 - padX;
    int by = winY + padTY;

    // 0: Resume
    if (ui_.win98Button(0, "Resume", bx, by, winW - padX*2, btnH, menuSelection_ == 0)) {
        menuSelection_ = 0; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 0 && !usingGamepad_) menuSelection_ = 0;
    by += btnH + gap;

    // 1: Music volume
    {
        char musBuf[16]; snprintf(musBuf, sizeof(musBuf), "%d%%", config_.musicVolume * 100 / 128);
        ui_.drawText("Music:", bx, by+(rowH-12)/2, 12, UI::W98::Black);
        if (ui_.win98Button(101, "<", fx, by, arrowW, rowH, false))
            { config_.musicVolume = std::max(0, config_.musicVolume - 8); Mix_VolumeMusic(config_.musicVolume); }
        ui_.drawWin98TextField(fx+arrowW+2, by, fwA, rowH, musBuf, menuSelection_==1, false, 0.f);
        if (ui_.win98Button(111, ">", fx+arrowW+2+fwA+2, by, arrowW, rowH, false))
            { config_.musicVolume = std::min(128, config_.musicVolume + 8); Mix_VolumeMusic(config_.musicVolume); }
        if (menuSelection_==1) {
            if (leftInput_)  { config_.musicVolume=std::max(0,  config_.musicVolume-8); Mix_VolumeMusic(config_.musicVolume); }
            if (rightInput_) { config_.musicVolume=std::min(128,config_.musicVolume+8); Mix_VolumeMusic(config_.musicVolume); }
        }
        if (ui_.hoveredItem == 1 && !usingGamepad_) menuSelection_ = 1;
        by += rowH + gap;
    }

    // 2: SFX volume
    {
        char sfxBuf[16]; snprintf(sfxBuf, sizeof(sfxBuf), "%d%%", config_.sfxVolume * 100 / 128);
        ui_.drawText("SFX:", bx, by+(rowH-12)/2, 12, UI::W98::Black);
        if (ui_.win98Button(102, "<", fx, by, arrowW, rowH, false))
            config_.sfxVolume = std::max(0, config_.sfxVolume - 8);
        ui_.drawWin98TextField(fx+arrowW+2, by, fwA, rowH, sfxBuf, menuSelection_==2, false, 0.f);
        if (ui_.win98Button(112, ">", fx+arrowW+2+fwA+2, by, arrowW, rowH, false))
            config_.sfxVolume = std::min(128, config_.sfxVolume + 8);
        if (menuSelection_==2) {
            if (leftInput_)  config_.sfxVolume=std::max(0,  config_.sfxVolume-8);
            if (rightInput_) config_.sfxVolume=std::min(128,config_.sfxVolume+8);
        }
        if (ui_.hoveredItem == 2 && !usingGamepad_) menuSelection_ = 2;
        by += rowH + gap;
    }

#ifndef __SWITCH__
    // 3: Fullscreen toggle
    {
        char fsBuf[20]; snprintf(fsBuf, sizeof(fsBuf), "Fullscreen: %s", config_.fullscreen ? "ON" : "OFF");
        if (ui_.win98Button(3, fsBuf, bx, by, winW - padX*2, btnH, menuSelection_ == 3)) {
            menuSelection_ = 3;
            config_.fullscreen = !config_.fullscreen;
            SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            saveConfig();
        }
        if (ui_.hoveredItem == 3 && !usingGamepad_) menuSelection_ = 3;
        by += btnH + gap;
    }

    // 4: Main Menu
    if (ui_.win98Button(4, "Main Menu", bx, by, winW - padX*2, btnH, menuSelection_ == 4)) {
        menuSelection_ = 4; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 4 && !usingGamepad_) menuSelection_ = 4;
#else
    // 3: Main Menu (Switch)
    if (ui_.win98Button(3, "Main Menu", bx, by, winW - padX*2, btnH, menuSelection_ == 3)) {
        menuSelection_ = 3; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 3 && !usingGamepad_) menuSelection_ = 3;
#endif

    // Music Player window (movable)
    {
        const int mpW = 232, mpH = 106;
        const int tbH = UI::W98::TitleH;

        // Lazy-init position to bottom-left, clear of HUD
        if (pauseMusicWinY_ < 0) { pauseMusicWinX_ = 20; pauseMusicWinY_ = SCREEN_H - mpH - 28; }

        // Clamp to screen
        pauseMusicWinX_ = std::max(0, std::min(SCREEN_W - mpW, pauseMusicWinX_));
        pauseMusicWinY_ = std::max(0, std::min(SCREEN_H - mpH, pauseMusicWinY_));

        // Title-bar drag (start on click, continue while held, stop on release)
        if (ui_.mouseClicked &&
            ui_.pointInRect(ui_.mouseX, ui_.mouseY, pauseMusicWinX_, pauseMusicWinY_, mpW, tbH)) {
            pauseMusicWinDragging_ = true;
            pauseMusicWinDragOffX_ = ui_.mouseX - pauseMusicWinX_;
            pauseMusicWinDragOffY_ = ui_.mouseY - pauseMusicWinY_;
            ui_.mouseClicked = false;
        }
        if (pauseMusicWinDragging_) {
            if (ui_.mouseDown) {
                pauseMusicWinX_ = ui_.mouseX - pauseMusicWinDragOffX_;
                pauseMusicWinY_ = ui_.mouseY - pauseMusicWinDragOffY_;
            } else {
                pauseMusicWinDragging_ = false;
            }
        }

        ui_.drawWin98Window(pauseMusicWinX_, pauseMusicWinY_, mpW, mpH, "Music Player");

        const int mcx = pauseMusicWinX_ + 8;
        const int miW = mpW - 16;
        int mcy = pauseMusicWinY_ + tbH + 6;

        // Current track name + author
        const char* curName = (lastActionTrack_ >= 0 &&
                               lastActionTrack_ < (int)bgMusicTrackNames_.size())
                              ? bgMusicTrackNames_[lastActionTrack_].c_str() : "---";
        const char* curAuthor = (lastActionTrack_ >= 0 &&
                                 lastActionTrack_ < (int)bgMusicTrackAuthors_.size())
                                ? bgMusicTrackAuthors_[lastActionTrack_].c_str() : "";
        {
            char nowBuf[64]; snprintf(nowBuf, sizeof(nowBuf), "Now: %s", curName);
            ui_.drawText(nowBuf, mcx, mcy, 11, UI::W98::Navy);
            mcy += 14;
        }
        if (curAuthor && curAuthor[0])
            ui_.drawText(curAuthor, mcx, mcy, 10, UI::W98::Shadow);
        mcy += 13;

        // Track N of M  +  loop indicator
        {
            int total = (int)bgMusicTracks_.size();
            int cur   = lastActionTrack_ >= 0 ? lastActionTrack_ + 1 : 0;
            char infoBuf[32]; snprintf(infoBuf, sizeof(infoBuf), "Track %d / %d", cur, total);
            ui_.drawText(infoBuf, mcx, mcy, 10, UI::W98::Shadow);
            if (musicLoopCurrent_) {
                int iw = ui_.textWidth(infoBuf, 10);
                ui_.drawText(" [loop]", mcx + iw, mcy, 10, UI::W98::Navy);
            }
            mcy += 14;
        }

        const int bW1 = 62, bW2 = 62, bW3 = miW - bW1 - bW2 - 8;
        const int bH  = 22;

        // Prev Button
// Prev Button
        // Static variable retains its value across frames
        static Uint32 lastMusicSkipTime = 0; 
        Uint32 currentTime = SDL_GetTicks();

        if (ui_.win98Button(200, "Prev", mcx, mcy, bW1, bH, false)) {
            // Only allow skip if 250ms have passed since the last skip
            if (!bgMusicTracks_.empty() && (currentTime - lastMusicSkipTime > 250)) {
                int n    = (int)bgMusicTracks_.size();
                int prev = (lastActionTrack_ <= 0 ? n : lastActionTrack_) - 1;
                prev = (prev + n) % n;
                lastActionTrack_   = prev;
                actionMusicActive_ = true;
                Mix_PlayMusic(bgMusicTracks_[prev], 0);
                Mix_VolumeMusic(config_.musicVolume);
                
                lastMusicSkipTime = currentTime; // Reset the timer
            }
        }

        // Next Button
        if (ui_.win98Button(201, "Next", mcx + bW1 + 4, mcy, bW2, bH, false)) {
            if (!bgMusicTracks_.empty() && (currentTime - lastMusicSkipTime > 250)) {
                int n    = (int)bgMusicTracks_.size();
                int next = (lastActionTrack_ + 1) % n;
                lastActionTrack_   = next;
                actionMusicActive_ = true;
                Mix_PlayMusic(bgMusicTracks_[next], 0);
                Mix_VolumeMusic(config_.musicVolume);
                
                lastMusicSkipTime = currentTime; // Reset the timer
            }
        }

{
            // Static timer for debouncing the loop button
            static Uint32 lastLoopClickTime = 0; 
            
            char loopLbl[16]; 
            snprintf(loopLbl, sizeof(loopLbl), "Loop: %s", musicLoopCurrent_ ? "O" : "X");
            
            if (ui_.win98Button(202, loopLbl, mcx + bW1 + bW2 + 8, mcy, bW3, bH, false)) {
                Uint32 currentTime = SDL_GetTicks();
                
                // Only toggle if 250ms have passed since the last click
                if (currentTime - lastLoopClickTime > 250) {
                    musicLoopCurrent_ = !musicLoopCurrent_;
                    lastLoopClickTime = currentTime; // Reset the timer
                }
            }
        }
    }
}

void Game::renderWorkshopMenu() {
    // Keep gameplay visible underneath with a dark tint
    ui_.drawDarkOverlay(140, 4, 6, 14);

    // Full-screen workshop browser mirroring the web layout
    const int winW = 860, winH = 520;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    const int pad  = 12;
    ui_.drawWin98Window(winX, winY, winW, winH, "Workshop");

    // ── Menubar ───────────────────────────────────────────────────────────────
    const int mbY = winY + UI::W98::TitleH;
    const int mbH = 20;
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect mbBg = {winX, mbY, winW, mbH};
    SDL_RenderFillRect(renderer_, &mbBg);
    ui_.drawText("Browse", winX + 8, mbY + 3, 11, UI::W98::Black);
    ui_.drawText("Forge / Salvage", winX + 70, mbY + 3, 11, UI::W98::Shadow);

    // ── Toolbar row (filter buttons mirroring web) ────────────────────────────
    static int wsFilter = 0; // 0=all,1=map,2=pack,3=character,4=item
    const char* filterKeys[] = { "", "map", "pack", "character", "item" };
    const char* filterLabels[] = { "All", "Maps", "Packs", "Characters", "Items" };
    const int tbY = mbY + mbH + 2;
    const int tbH = 26;
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect tbBg = {winX, tbY, winW, tbH};
    SDL_RenderFillRect(renderer_, &tbBg);
    int tbx = winX + 4;
    for (int fi = 0; fi < 5; fi++) {
        bool active = (wsFilter == fi);
        if (ui_.win98Button(200 + fi, filterLabels[fi], tbx, tbY + 1, 80, tbH - 2, active)) {
            wsFilter = fi;
        }
        tbx += 84;
    }
    // Resume button on the right side of toolbar
    if (ui_.win98Button(210, "Resume Game", winX + winW - pad - 100, tbY + 1, 100, tbH - 2, false)) {
        menuSelection_ = 0; confirmInput_ = true;
    }

    // ── Installed mods list ───────────────────────────────────────────────────
    const int contentY = tbY + tbH + 2;
    const int statusH  = 20;
    const int contentH = winH - (contentY - winY) - statusH - 2;
    ui_.drawWin98Bevel(winX + pad, contentY, winW - 2*pad, contentH, false);

    const int listX = winX + pad + 3;
    const int listY = contentY + 3;
    const int listW = winW - 2*pad - 6;
    const int listH = contentH - 6;

    // Filter & gather installed mods
    const auto& allMods = ModManager::instance().mods();
    std::vector<const Mod*> filtered;
    for (auto& m : allMods) {
        if (!m.enabled) continue;
        const std::string& ft = filterKeys[wsFilter];
        if (!ft.empty()) {
            // Match type by content flags
            if (ft == "map"       && !m.content.maps)       continue;
            if (ft == "pack"      && !m.content.packs)      continue;
            if (ft == "character" && !m.content.characters) continue;
            if (ft == "item"      && !m.content.items)      continue;
        }
        filtered.push_back(&m);
    }

    SDL_Rect clip = {listX, listY, listW, listH};
    SDL_RenderSetClipRect(renderer_, &clip);

    static int wsScroll = 0;
    const int rowH = 48;
    int maxVis = listH / rowH;
    if (maxVis < 1) maxVis = 1;
    if (wsScroll > std::max(0, (int)filtered.size() - maxVis))
        wsScroll = std::max(0, (int)filtered.size() - maxVis);

    if (filtered.empty()) {
        SDL_RenderSetClipRect(renderer_, nullptr);
        const char* msg = allMods.empty()
            ? "No mods installed. Use Online Workshop to download mods."
            : "No mods match this filter.";
        ui_.drawText(msg, listX + 8, listY + listH/2 - 8, 12, UI::W98::Shadow);
    } else {
        for (int i = wsScroll; i < (int)filtered.size() && (i - wsScroll) < maxVis; i++) {
            const Mod* m = filtered[i];
            int ry = listY + (i - wsScroll) * rowH;
            bool sel = (menuSelection_ == i + 10);
            bool hov = ui_.pointInRect(ui_.mouseX, ui_.mouseY, listX, ry, listW, rowH);
            if (hov && !usingGamepad_) { menuSelection_ = i + 10; sel = true; }
            if (hov) ui_.hoveredItem = (i + 10) % 60;

            if (sel) {
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer_, UI::W98::Navy.r, UI::W98::Navy.g, UI::W98::Navy.b, 255);
                SDL_Rect row = {listX, ry, listW, rowH};
                SDL_RenderFillRect(renderer_, &row);
            }

            SDL_Color tc = sel ? UI::W98::White : UI::W98::Black;
            SDL_Color dc = sel ? UI::W98::Silver : UI::W98::Shadow;

            char line1[256], line2[256];
            snprintf(line1, sizeof(line1), "%s  v%s  by %s", m->name.c_str(), m->version.c_str(), m->author.c_str());
            snprintf(line2, sizeof(line2), "[%s]  chars:%zu  maps:%zu  packs:%zu",
                m->id.c_str(), m->characterPaths.size(), m->mapPaths.size(), m->packPaths.size());
            ui_.drawText(line1, listX + 6, ry + 6, 13, tc);
            ui_.drawText(m->description.empty() ? "(no description)" : m->description.c_str(), listX + 6, ry + 22, 11, dc);
            ui_.drawText(line2, listX + 6, ry + 34, 10, dc);
        }

        // Scrollbar
        if ((int)filtered.size() > maxVis) {
            float ratio = (float)maxVis / (float)filtered.size();
            float sRatio = (filtered.size() > 1) ? (float)wsScroll / (float)(filtered.size() - maxVis) : 0.f;
            int sbH = std::max(20, (int)(listH * ratio));
            int sbY = listY + (int)((listH - sbH) * sRatio);
            SDL_SetRenderDrawColor(renderer_, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
            SDL_Rect sb = {listX + listW - 5, sbY, 4, sbH};
            SDL_RenderFillRect(renderer_, &sb);
        }
    }

    SDL_RenderSetClipRect(renderer_, nullptr);

    // Scroll on mouse wheel
    if (ui_.mouseWheelY != 0 && ui_.pointInRect(ui_.mouseX, ui_.mouseY, listX, listY, listW, listH)) {
        wsScroll = std::max(0, std::min(wsScroll - ui_.mouseWheelY,
                             std::max(0, (int)filtered.size() - maxVis)));
    }

    // ── Statusbar ─────────────────────────────────────────────────────────────
    const int sbY2 = winY + winH - statusH;
    char countMsg[80];
    snprintf(countMsg, sizeof(countMsg), "%d mod%s installed", (int)filtered.size(), filtered.size() != 1 ? "s" : "");
    ui_.drawWin98StatusBar(sbY2, countMsg);

    { UI::HintPair hints[] = { {UI::Action::Back, "Resume"} };
      ui_.drawHintBar(hints, 1); }
}

void Game::renderDeathScreen() {
    ui_.drawDarkOverlay(160, 30, 4, 4);

    const int winW    = 340;
    const int btnH    = 26;
    const int btnGap  = 6;
    const int padX    = 14;
    const int padTop  = 14;
    const int rowH    = 20;
    const int rowGap  = 6;

    const bool hasBest   = bestRun_.valid();
    const int  bestStatH = hasBest ? (3 * 18 + 2 * 4) : 18;
    const int  newBestH  = newBestRun_ ? (rowH + 4) : 0;

    // current run: time + wave + kills (3 rows, 2 gaps)
    const int curH = 3 * rowH + 2 * rowGap;

    const int winH = UI::W98::TitleH + padTop
                   + curH + rowGap          // current stats + gap to separator
                   + 14                     // first separator
                   + newBestH               // "NEW BEST!" badge (0 if not new)
                   + rowH + rowGap          // "Best Run:" header
                   + bestStatH              // best run stat rows
                   + 8                      // gap before second separator
                   + 14                     // second separator
                   + 2 * btnH + btnGap      // two buttons
                   + padTop;

    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Packet loss: 100%");

    // --- Current run stats ---
    char timeBuf[64], waveBuf[64], killsBuf[64];
    int mins = (int)gameTime_ / 60, secs = (int)gameTime_ % 60;
    snprintf(timeBuf,  sizeof(timeBuf),  "Time: %d:%02d", mins, secs);
    snprintf(waveBuf,  sizeof(waveBuf),  "Wave: %d",      waveNumber_);
    snprintf(killsBuf, sizeof(killsBuf), "Kills: %d",     coopSlots_[0].kills);

    int cy = winY + UI::W98::TitleH + padTop;
    ui_.drawTextCentered(timeBuf,  cy, 16, UI::W98::Black); cy += rowH + rowGap;
    ui_.drawTextCentered(waveBuf,  cy, 16, UI::W98::Black); cy += rowH + rowGap;
    ui_.drawTextCentered(killsBuf, cy, 16, UI::W98::Black); cy += rowH + rowGap;

    // First separator
    ui_.drawWin98Bevel(winX + padX, cy, winW - padX * 2, 2, false);
    cy += 14;

    // NEW BEST badge
    if (newBestRun_) {
        constexpr SDL_Color kGold = {204, 160, 0, 255};
        ui_.drawTextCentered(">> NEW BEST! <<", cy, 16, kGold);
        cy += rowH + 4;
    }

    // --- Best Run section ---
    ui_.drawTextCentered("Best Run:", cy, 16, UI::W98::Black);
    cy += rowH + rowGap;

    if (hasBest) {
        int bm = (int)bestRun_.time / 60, bs = (int)bestRun_.time % 60;
        char bwBuf[64], bkBuf[64], btBuf[64];
        snprintf(bwBuf, sizeof(bwBuf), "Wave: %d",      bestRun_.wave);
        snprintf(bkBuf, sizeof(bkBuf), "Kills: %d",     bestRun_.kills);
        snprintf(btBuf, sizeof(btBuf), "Time: %d:%02d", bm, bs);
        ui_.drawTextCentered(bwBuf, cy, 14, UI::W98::Black); cy += 18 + 4;
        ui_.drawTextCentered(bkBuf, cy, 14, UI::W98::Black); cy += 18 + 4;
        ui_.drawTextCentered(btBuf, cy, 14, UI::W98::Black); cy += 18;
    } else {
        ui_.drawTextCentered("No record yet", cy, 14, UI::W98::Shadow);
        cy += 18;
    }

    // Second separator
    cy += 8;
    ui_.drawWin98Bevel(winX + padX, cy, winW - padX * 2, 2, false);
    cy += 14;

    // --- Buttons ---
    int bx = winX + padX;
    if (ui_.win98Button(0, "Retry", bx, cy, winW - padX * 2, btnH, menuSelection_ == 0)) {
        menuSelection_ = 0; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 0 && !usingGamepad_) menuSelection_ = 0;
    cy += btnH + btnGap;

    if (ui_.win98Button(1, "Main Menu", bx, cy, winW - padX * 2, btnH, menuSelection_ == 1)) {
        menuSelection_ = 1; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 1 && !usingGamepad_) menuSelection_ = 1;

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Select an option");
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

// Custom Map Play

void Game::startCustomMap(const std::string& path, int modeOverride) {
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

    // Auto-detect layer images from map filename when not stored in CSM
    {
        std::string base = path;
        size_t sl = base.find_last_of("/\\");
        if (sl != std::string::npos) base = base.substr(sl + 1);
        size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        if (customMap_.bgImagePath.empty()) {
            std::string cand = "sprites/" + base + ".png";
            if (Assets::instance().loadRelTex(cand)) customMap_.bgImagePath = cand;
        }
        if (customMap_.topImagePath.empty()) {
            std::string cand = "sprites/" + base + "top.png";
            if (Assets::instance().loadRelTex(cand)) customMap_.topImagePath = cand;
        }
    }

    // Load full-map layer images
    bgImageTex_   = customMap_.bgImagePath.empty()  ? nullptr : Assets::instance().loadRelTex(customMap_.bgImagePath);
    topImageTex_  = customMap_.topImagePath.empty() ? nullptr : Assets::instance().loadRelTex(customMap_.topImagePath);
    topLayerAlpha_ = 1.0f;

    state_ = GameState::PlayingCustom;
    playingCustomMap_ = true;
    customGoalOpen_ = false;
    gameTime_ = 0;

    // Story: start a fresh campaign run and load the map's cutscene library
    resetStoryRun();
    loadStoryCutsceneLib(path);

    // Apply custom map tiles to the game tilemap
    map_.width  = customMap_.width;
    map_.height = customMap_.height;
    map_.tiles  = customMap_.tiles;
    map_.ceiling = customMap_.ceiling;
    map_.noCollide = customMap_.tileNoCollide;
    map_.noCollide.resize(map_.tiles.size(), 0);
    invalidateMinimapCache();

    // Reset entities
    enemies_.clear();
    bystanders_.clear();
    bullets_.clear();
    enemyBullets_.clear();
    bombs_.clear();
    explosions_.clear();
    debris_.clear();
    blood_.clear(); tileBlood_.clear();
    boxFragments_.clear();
    crates_.clear();
    pickups_.clear();
    vehicles_.clear(); inVehicle_ = false; vehicleIdx_ = -1;
    upgrades_.reset();
    crateSpawnTimer_ = 0;

    // Apply game mode: explicit override from launcher takes priority over map's stored value
    sandboxMode_ = (modeOverride >= 0) ? (modeOverride == 1) : (customMap_.gameMode == 1);
    lobbySettings_.isPvp     = false; // custom map is always PvE
    lobbySettings_.pvpEnabled = false;

    // Reset wave state
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    bossWaveActive_ = false;
    lastBossWaveNum_ = -1;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;

    // Reset player
    player_ = Player{};
    player_.maxHp = config_.playerMaxHp;
    player_.hp = config_.playerMaxHp;
    player_.bombCount = 1;
    applyCharacterStatsToPlayer(player_);

    // Apply map player config restrictions
    if (customMap_.playerConfig.enabled) {
        const auto& pc = customMap_.playerConfig;
        if (pc.maxHp > 0) { player_.maxHp = pc.maxHp; player_.hp = pc.maxHp; }
        player_.bombCount  = pc.startBombs;
        player_.canShoot   = pc.hasGun;
        player_.canMelee   = pc.hasMelee;
        player_.canBomb    = pc.hasBombs;
        player_.canParry   = pc.hasParry;
        if (pc.speedPct != 100)
            upgrades_.speedBonus = (pc.speedPct / 100.0f - 1.0f) * player_.speed;
        if (pc.damagePct != 100)
            upgrades_.damageMulti = pc.damagePct / 100.0f;
    }

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
    }

    // Also generate spawn points for wave spawning if map has them
    map_.findSpawnPoints();

    {
        std::string mf;
        size_t sl = path.find_last_of('/');
        mf = (sl != std::string::npos) ? path.substr(0, sl + 1) : "./";
        playMapMusic(mf, customMap_.musicPath);
    }

    // Auto-play an intro cutscene (id "intro") if the library provides one.
    if (storyCutscenes_.findById("intro"))
        startStoryCutscene("intro");
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
    // Auto-detect layer images from map filename when not stored in CSM
    {
        std::string base = path;
        size_t sl = base.find_last_of("/\\");
        if (sl != std::string::npos) base = base.substr(sl + 1);
        size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        if (customMap_.bgImagePath.empty()) {
            std::string cand = "sprites/" + base + ".png";
            if (Assets::instance().loadRelTex(cand)) customMap_.bgImagePath = cand;
        }
        if (customMap_.topImagePath.empty()) {
            std::string cand = "sprites/" + base + "top.png";
            if (Assets::instance().loadRelTex(cand)) customMap_.topImagePath = cand;
        }
    }

    bgImageTex_   = customMap_.bgImagePath.empty()  ? nullptr : Assets::instance().loadRelTex(customMap_.bgImagePath);
    topImageTex_  = customMap_.topImagePath.empty() ? nullptr : Assets::instance().loadRelTex(customMap_.topImagePath);
    topLayerAlpha_ = 1.0f;

    playingCustomMap_ = true;
    customGoalOpen_ = false;
    gameTime_ = 0;

    // Apply custom map tiles to the game tilemap
    map_.width  = customMap_.width;
    map_.height = customMap_.height;
    map_.tiles  = customMap_.tiles;
    map_.ceiling = customMap_.ceiling;
    map_.noCollide = customMap_.tileNoCollide;
    map_.noCollide.resize(map_.tiles.size(), 0);
    invalidateMinimapCache();

    // Reset entities
    enemies_.clear();
    bystanders_.clear();
    bullets_.clear();
    enemyBullets_.clear();
    bombs_.clear();
    explosions_.clear();
    debris_.clear();
    blood_.clear(); tileBlood_.clear();
    boxFragments_.clear();
    crates_.clear();
    pickups_.clear();
    vehicles_.clear(); inVehicle_ = false; vehicleIdx_ = -1;
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    sandboxMode_ = false;
    customEnemiesTotal_ = 0;

    // Reset wave state
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    bossWaveActive_ = false;
    lastBossWaveNum_ = -1;
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
        customGoalOpen_ = (alive == 0);
        break;
    }
    case GoalCondition::Immediate:
        customGoalOpen_ = true;
        break;
    case GoalCondition::OnTrigger:
        // Check if some condition met (simple: after X seconds or enemies < half)
        customGoalOpen_ = (gameTime_ > 30.0f);
        break;
    case GoalCondition::OnFlag: {
        // Open when the story "goal_open" flag is set (by an Objective/Waypoint).
        auto it = storyFlags_.find("goal_open");
        customGoalOpen_ = (it != storyFlags_.end() && it->second);
        break;
    }
    }

    // Check if player reached the goal
    if (customGoalOpen_) {
        float dx = player_.pos.x - goal->x;
        float dy = player_.pos.y - goal->y;
        if (fabsf(dx) < goal->width/2 && fabsf(dy) < goal->height/2) {
            // Story: reward sparing surrendered operators who survived the mission.
            for (auto& b : bystanders_) {
                if (b.alive && b.kind == Bystander::Kind::Operator)
                    adjustSignal(+12, "operator spared");
            }
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

