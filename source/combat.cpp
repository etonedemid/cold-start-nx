#include "game.h"
#include "game_internal.h"
#include <cmath>
#include <algorithm>

// Rotated-rect vs circle pushout - transforms circle into rect's local frame,
// finds nearest surface point, returns the world-space separation vector.
static Vec2 collisionZonePushout(Vec2 pos, float radius, Vec2 center, float hw, float hh, float angle) {
    float ca = cosf(-angle), sa = sinf(-angle);
    float dx = pos.x - center.x, dy = pos.y - center.y;
    float lx = dx*ca - dy*sa, ly = dx*sa + dy*ca;
    float nx = std::max(-hw, std::min(hw, lx));
    float ny = std::max(-hh, std::min(hh, ly));
    float distSq = (lx-nx)*(lx-nx) + (ly-ny)*(ly-ny);
    if (distSq >= radius*radius) return {0.0f, 0.0f};
    float dist = sqrtf(distSq);
    float normalX, normalY;
    if (dist < 0.001f) { // center inside rect - push out along shortest axis
        float px = hw - fabsf(lx), py = hh - fabsf(ly);
        if (px < py) { normalX = lx < 0 ? -1.0f :  1.0f; normalY = 0.0f; }
        else         { normalX = 0.0f; normalY = ly < 0 ? -1.0f : 1.0f; }
    } else {
        normalX = (lx - nx) / dist;
        normalY = (ly - ny) / dist;
    }
    float push = radius - dist;
    float cb = cosf(angle), sb = sinf(angle);
    return { (normalX*cb - normalY*sb)*push, (normalX*sb + normalY*cb)*push };
}

void Game::pushOutCollisionZones(Vec2& pos, float radius) {
    for (auto& t : customMap_.triggers) {
        if (t.type != TriggerType::CollisionZone) continue;
        float angle = triggerGetAngle(t);
        Vec2  center = { t.x, t.y }; // t.x/t.y is the center, not the top-left
        Vec2  push   = collisionZonePushout(pos, radius, center,
                                             t.width*0.5f, t.height*0.5f, angle);
        pos.x += push.x;
        pos.y += push.y;
    }
}

bool Game::posInCollisionZone(Vec2 pos, float radius) const {
    for (const auto& t : customMap_.triggers) {
        if (t.type != TriggerType::CollisionZone) continue;
        if (collisionZonePushout(pos, radius, { t.x, t.y },
                t.width * 0.5f, t.height * 0.5f, triggerGetAngle(t)).lengthSq() > 0.0f)
            return true;
    }
    return false;
}


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
                // Check lives - both individual and shared modes can exhaust lives
                if (currentRules_.lives > 0) {
                    bool outOfLives = false;
                    if (currentRules_.sharedLives) {
                        // Host is authoritative; client decrements optimistically
                        if (sharedLives_ > 0) sharedLives_--;
                        outOfLives = (sharedLives_ <= 0);
                    } else {
                        if (localLives_ > 0) localLives_--;
                        outOfLives = (localLives_ <= 0);
                    }
                    if (outOfLives) {
                        // Out of lives - become a spectator ghost
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
            } else if (state_ == GameState::PlayingCustom || state_ == GameState::CustomPaused ||
                       state_ == GameState::PlayingPack    || state_ == GameState::PackPaused) {
                if (!storyCutscenes_.onDeathId.empty() && !csPlay_.active) {
                    // Play ondeath cutscene; DeathScreen event triggers the actual death screen.
                    // Fallback: if cutscene ends without DeathScreen, updateStoryCutscene does it.
                    startStoryCutscene(storyCutscenes_.onDeathId);
                } else if (state_ == GameState::PlayingPack || state_ == GameState::PackPaused) {
                    state_ = GameState::PackDead;
                } else {
                    state_ = GameState::CustomDead;
                }
            } else {
                checkAndSaveBestRun();
                state_ = GameState::Dead;
            }
            menuSelection_ = 0;
        }
        return;
    }

    // Weapon switch
    constexpr int NUM_WEAPONS = 2;
    if (weaponSwitchDelta_ != 0) {
        p.activeWeapon = ((p.activeWeapon + weaponSwitchDelta_) % NUM_WEAPONS + NUM_WEAPONS) % NUM_WEAPONS;
        weaponSwitchDelta_ = 0;
        if (p.activeWeapon == 0) {
            // Switching back to gun: clear axe-pose so gun animation resumes
            p.hadMeleeSwing = false;
        }
    }

    // Movement - skip entirely when in a vehicle (vehicle owns position/velocity)
    if (inVehicle_ && vehicleIdx_ >= 0 && vehicleIdx_ < (int)vehicles_.size()) {
        auto& veh = vehicles_[vehicleIdx_];
        p.pos = veh.pos;
        p.vel = veh.vel;
        p.moving = false;
    } else {
        Vec2 targetVel = {0, 0};
        p.moving = moveInput_.lengthSq() > 0.01f;
        if (p.moving) {
            const float lastStandSpeedMult = (upgrades_.hasLastStand && p.hp <= 10) ? (1.0f + 0.3f * upgrades_.hasLastStand) : 1.0f;
            targetVel = moveInput_.normalized() * p.speed * moveInput_.length() * lastStandSpeedMult;
        }

        p.vel = Vec2::lerp(p.vel, targetVel, dt * PLAYER_SMOOTHING);

        if (p.isParrying && p.parryDashTimer > 0) {
            p.vel = p.parryDir * PARRY_DASH_SPEED;
        }

        Vec2 newPos = p.pos + p.vel * dt;

        if (!spectatorMode_) {
            if (!map_.worldCollides(newPos.x, p.pos.y, PLAYER_SIZE * 0.4f))
                p.pos.x = newPos.x;
            if (!map_.worldCollides(p.pos.x, newPos.y, PLAYER_SIZE * 0.4f))
                p.pos.y = newPos.y;
            pushOutCollisionZones(p.pos, PLAYER_SIZE * 0.4f);
        } else {
            p.pos = newPos;
        }

        p.pos.x = fmaxf(PLAYER_SIZE, fminf(map_.worldWidth() - PLAYER_SIZE, p.pos.x));
        p.pos.y = fmaxf(PLAYER_SIZE, fminf(map_.worldHeight() - PLAYER_SIZE, p.pos.y));

        if (!spectatorMode_ && map_.worldCollides(p.pos.x, p.pos.y, PLAYER_SIZE * 0.4f)) {
            for (float r = 6.0f; r <= TILE_SIZE * 2.0f; r += 6.0f) {
                bool freed = false;
                for (int a = 0; a < 8; a++) {
                    float ang = a * (float)M_PI * 0.25f;
                    float tx = p.pos.x + cosf(ang) * r;
                    float ty = p.pos.y + sinf(ang) * r;
                    if (!map_.worldCollides(tx, ty, PLAYER_SIZE * 0.4f)) {
                        p.pos = {tx, ty};
                        p.vel = {0.0f, 0.0f};
                        freed = true;
                        break;
                    }
                }
                if (freed) break;
            }
        }
    }

    // Rotation (aim)
    if (aimInput_.lengthSq() > 0.04f) {
        p.rotation = atan2f(aimInput_.y, aimInput_.x);
    } else if (p.moving) {
        p.rotation = atan2f(moveInput_.y, moveInput_.x);
    }

    // Leg rotation (movement direction)
    if (p.moving) {
        p.legRotation = atan2f(p.vel.y, p.vel.x);
    }

    // Body animation: melee takes priority, then shooting anim
    if (p.isMeleeSwinging) {
        // Axe swing: sprites 0004-0010 (frames 3-9)
        float prog = std::min(1.0f, p.meleeTimer / MELEE_DURATION);
        int range  = MELEE_ANIM_LAST - MELEE_ANIM_FIRST;             // 6
        int offset = std::min(range, (int)(prog * (range + 1)));      // 0..6
        p.animFrame = p.meleeSwingReverse
                      ? (MELEE_ANIM_LAST  - offset)   // reverse: 9->3
                      : (MELEE_ANIM_FIRST + offset);  // forward: 3->9
    } else if (p.hadMeleeSwing) {
        // Hold end-of-swing pose between swings (meleeSwingReverse was toggled
        // when swing completed, so: next=reverse -> last was forward -> idle at LAST=9;
        //                          next=forward -> last was reverse -> idle at FIRST=3)
        p.animFrame = p.meleeSwingReverse ? MELEE_ANIM_LAST : MELEE_ANIM_FIRST;
    } else if (p.activeWeapon == 1) {
        // Axe equipped but never swung yet - hold the axe-ready pose (sprite 0004)
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

    // Invulnerability (used by Blindness pickup and respawn only)
    if (p.invulnerable) {
        p.invulnTimer -= dt;
        if (p.invulnTimer <= 0) p.invulnerable = false;
    }
    p.bodyContactCooldown = std::max(0.0f, p.bodyContactCooldown - dt);
    // Shooting
    p.fireCooldown -= dt;
    if (!spectatorMode_ && !inVehicle_ && p.activeWeapon == 0 && p.canShoot) {  // gun slot only
    if (p.reloading) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0) {
            p.ammo = p.maxAmmo;
            p.reloading = false;
        }
    } else if (fireInput_ && p.fireCooldown <= 0 && p.ammo > 0) {
        spawnBullet(p.pos, p.rotation);
        // Triple shot - two extra bullets per stack at increasing spreads
        if (upgrades_.hasTripleShot) {
            const float spread = 0.26f; // ~15 degrees per step
            for (int s = 0; s < upgrades_.hasTripleShot; s++) {
                spawnBullet(p.pos, p.rotation + spread * (s + 1));
                spawnBullet(p.pos, p.rotation - spread * (s + 1));
            }
            p.ammo = std::max(0, p.ammo - (1 + 2 * upgrades_.hasTripleShot));
            p.fireCooldown = 2.0f / p.fireRate; // half fire rate
        } else {
            p.ammo--;
            p.fireCooldown = 1.0f / p.fireRate;
        }
        p.hasFiredOnce = true;
        p.shootAnimTimer = 0.12f;
        camera_.addShake(1.2f);
        rumble(0.05f, 16, 0.22f, 0.92f);  // Even softer gun kick
        if (sfxShoot_) playSFX(sfxShoot_, config_.sfxVolume);
    } else if ((fireInput_ || upgrades_.hasAutoReload) && p.ammo <= 0 && !p.reloading) {
        // Auto-reload (fireInput or hasAutoReload flag)
        p.reloading = true;
        p.reloadTimer = p.reloadTime;
        if (sfxReload_) playSFX(sfxReload_, config_.sfxVolume);
    }
    } // gun slot only (!spectatorMode_ && activeWeapon==0)

    // Parry
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

    // Melee (axe swing)
    const float meleeRange = getMeleeRange(p, upgrades_);
    const float meleeArc = getMeleeArc(upgrades_);
    const int meleePlayerDamage = getMeleePlayerDamage(upgrades_);
    const float meleeCooldownTime = getMeleeCooldownTime(upgrades_);
    if (p.meleeCooldown > 0) p.meleeCooldown -= dt;

    // E key: enter/exit vehicle takes priority over melee
    if (meleeInput_ && !spectatorMode_) {
        if (inVehicle_ && vehicleIdx_ >= 0 && vehicleIdx_ < (int)vehicles_.size()) {
            auto& ev = vehicles_[vehicleIdx_];
            ev.occupantSlot = -1;
            inVehicle_  = false;
            vehicleIdx_ = -1;
            meleeInput_ = false;
            // Eject player to side of car, trying left then right then behind
            {
                Vec2 fwd = {cosf(ev.rotation), sinf(ev.rotation)};
                Vec2 lat = {-fwd.y, fwd.x};
                float ejectDist = (ev.spriteW > 0 ? ev.spriteW * 0.75f : ev.size) + 28.0f;
                const Vec2 candidates[] = {
                    ev.pos + lat *  ejectDist,
                    ev.pos - lat *  ejectDist,
                    ev.pos - fwd *  ejectDist,
                };
                for (auto& cand : candidates) {
                    if (!map_.worldCollides(cand.x, cand.y, 16.0f)) {
                        p.pos = cand;
                        p.vel = {0.0f, 0.0f};
                        break;
                    }
                }
            }
        } else {
            for (int vi = 0; vi < (int)vehicles_.size(); vi++) {
                auto& v = vehicles_[vi];
                if (!v.alive || v.occupantSlot >= 0) continue;
                if (Vec2::dist(p.pos, v.pos) < Vehicle::ENTER_RADIUS) {
                    inVehicle_  = true;
                    vehicleIdx_ = vi;
                    v.occupantSlot = 0;
                    meleeInput_ = false;
                    break;
                }
            }
        }
    }

    if (!spectatorMode_) {
        // E key while gun is equipped -> quick-switch to axe
        if (meleeInput_ && p.activeWeapon == 0) {
            p.activeWeapon = 1;
            p.hadMeleeSwing = false;
        }
        // Trigger: axe must be equipped; E key or fire button both swing
        bool doMelee = p.canMelee && p.activeWeapon == 1 && (meleeInput_ || fireInput_);
        if (doMelee && !p.isMeleeSwinging && p.meleeCooldown <= 0) {
            p.isMeleeSwinging = true;
            p.meleeTimer      = 0.0f;
            p.meleeHit        = false;
            p.hadMeleeSwing   = true;
            p.meleeBloodlustProc = false;
            p.vel = p.vel + Vec2::fromAngle(p.rotation) * (140.0f + std::min(60.0f, upgrades_.speedBonus * 0.12f));
            camera_.addShake(0.45f);
            if (sfxSwoosh_) playSFX(sfxSwoosh_, config_.sfxVolume);
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
                    if (c.hasContents) {
                        Pickup pu;
                        pu.pos = c.pos;
                        pu.type = c.contents;
                        pickups_.push_back(pu);
                    }
                    spawnMeleeImpact(c.pos, {170, 110, 55, 255}, heavyBreak ? 18 : 14, heavyBreak ? 1.35f : 1.05f);
                    queueMeleeImpactRumble(heavyBreak ? 0.34f : 0.28f,
                                           heavyBreak ? 105 : 82,
                                           heavyBreak ? 1.35f : 1.05f,
                                           heavyBreak ? 0.70f : 0.60f);
                    screenFlashTimer_ = 0.06f;
                    screenFlashR_ = 255; screenFlashG_ = 200; screenFlashB_ = 50;
                    if (sfxBreak_) playSFX(sfxBreak_, config_.sfxVolume);
                    if (upgrades_.hasBloodlust) {
                        p.meleeBloodlustProc = true;
                        p.ammo = std::min(p.maxAmmo, p.ammo + 1);
                    }
                }
            };
            auto emitShockPulse = [&](Vec2 pos) {
                if (!(upgrades_.hasShockEdge || upgrades_.hasStunRounds)) return;
                float shockRadius = upgrades_.hasShockEdge ? (80.0f + 30.0f * upgrades_.hasShockEdge) : 80.0f;
                for (int i = 0; i < (upgrades_.hasShockEdge ? (4 + 6 * upgrades_.hasShockEdge) : 6); i++) {
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
                    if (simAuth) e.stunTimer = std::max(e.stunTimer, upgrades_.hasShockEdge ? (0.6f + 0.5f * upgrades_.hasShockEdge) : 0.7f);
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
                // All peers apply damage locally for instant feedback; whoever delivers
                // the killing blow broadcasts it so all peers sync the death.
                e.hp -= meleePlayerDamage;
                if (e.hp <= 0) {
                    killEnemy(e);
                    queueMeleeImpactRumble(0.64f, 130, 1.35f, 0.70f);
                    if (net.isInGame()) {
                        uint32_t eIdx = (uint32_t)(&e - &enemies_[0]);
                        net.sendEnemyKilled(eIdx, net.localPlayerId());
                        enemyStatesNeedUpdate_ = true;
                    }
                }
                if (upgrades_.hasBloodlust) {
                    p.meleeBloodlustProc = true;
                    p.ammo = std::min(p.maxAmmo, p.ammo + 1 + upgrades_.hasScavenger);
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
            // Hitting solid walls - clank feedback
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
                if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                if (target.dead) {
                    if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
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

    // Bombs: spawn one queued bomb per frame so orbit angles spread naturally
    if (!spectatorMode_ && p.bombCount > 0) {
        spawnBomb();
        p.bombCount--;
    }
    // ZL / RMB = Launch the nearest orbiting bomb
    if (!spectatorMode_ && bombLaunchInput_ && player_.canBomb) {
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
            // Sync bomb launch to other players
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

// Enemy Update

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

        // Stun (bosses resist stunlock - cap max accumulated stun)
        if (isBossType(e.type) && e.stunTimer > BOSS_MAX_STUN) e.stunTimer = BOSS_MAX_STUN;
        if (e.stunTimer > 0) { e.stunTimer -= dt; continue; }

        // Boss enrage at 50% HP
        if (isBossType(e.type) && !e.bossEnraged && e.hp < e.maxHp * 0.5f) {
            e.bossEnraged = true;
            e.speed *= 1.30f;
            if (isShooterEnemyType(e.type)) {
                e.shootCooldownBase *= 0.60f;
                e.shootCooldown = std::min(e.shootCooldown, e.shootCooldownBase);
            }
            if (e.type == EnemyType::BossGunner) {
                e.shotsPerBurst = 12;
                e.burstGap = BOSS_GUNNER_BURST_GAP * 0.55f;
            } else if (e.type == EnemyType::BossSniper) {
                e.shotsPerBurst = 3;
                e.burstGap = 0.18f;
                e.shootSpread = 0.10f;
            } else if (e.type == EnemyType::BossBrute) {
                e.dashCooldown       *= 0.55f;
                e.dashDelay          *= 0.65f;
                e.bossChargeCdTimer   = std::min(e.bossChargeCdTimer, BOSS_BRUTE_CHARGE_CD * 0.5f);
            }
        }

        // Damage flash decay
        if (e.damageFlash > 0) e.damageFlash -= dt * 2.5f;

        // Vision check - sets e.targetPlayerId if a player is seen
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
        if (e.bossCharging) {
            // BossBrute long-range charge: drive straight at locked direction
            e.vel = e.bossChargeDir * BOSS_BRUTE_CHARGE_SPEED;
            e.dashTimer -= dt;
            if (e.dashTimer <= 0) {
                e.bossCharging = false;
                e.bossChargeCdTimer = BOSS_BRUTE_CHARGE_CD;
                e.vel = {0, 0};
            }
        } else if (e.isDashing) {
            enemyDash(e, dt);
        } else if (e.dashCharging) {
            e.dashDelayTimer -= dt;
            e.flashTimer -= dt;
            if (e.dashDelayTimer <= 0) {
                // Initiate actual dash (direction was locked when charging began)
                e.isDashing = true;
                e.dashTimer = e.dashDuration;
                e.dashCharging = false;
                if (sfxSwoosh_) playSFX(sfxSwoosh_, config_.sfxVolume);
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
        if (e.bossChargeCdTimer > 0) e.bossChargeCdTimer -= dt;

        // Body rotation: melee enemies gaze at player, clamped to ±30° of movement direction.
        // Shooters track the player freely inside enemyChase.
        bool shooterChasing = isShooterEnemyType(e.type) && e.state == EnemyState::Chase;
        if (isMeleeEnemyType(e.type) && e.state == EnemyState::Chase) {
            Vec2 tgt = getEnemyTargetPos(e);
            float want = atan2f(tgt.y - e.pos.y, tgt.x - e.pos.x);
            float diff = want - e.rotation;
            while (diff >  (float)M_PI) diff -= 2.0f * (float)M_PI;
            while (diff < -(float)M_PI) diff += 2.0f * (float)M_PI;
            e.rotation += diff * std::min(1.0f, dt * 7.0f);

            // Clamp body rotation to ±30° of movement (leg) direction when actually moving
            if (e.vel.lengthSq() > 100.0f) {
                constexpr float kMaxBodyTurn = (float)M_PI / 6.0f;  // 30 degrees
                float legDir = e.legRotation;
                float bodyOff = e.rotation - legDir;
                while (bodyOff >  (float)M_PI) bodyOff -= 2.0f * (float)M_PI;
                while (bodyOff < -(float)M_PI) bodyOff += 2.0f * (float)M_PI;
                if (bodyOff >  kMaxBodyTurn) e.rotation = legDir + kMaxBodyTurn;
                if (bodyOff < -kMaxBodyTurn) e.rotation = legDir - kMaxBodyTurn;
            }
        } else if (!shooterChasing && e.vel.lengthSq() > 1.0f && !e.isDashing && !e.dashCharging) {
            e.rotation = atan2f(e.vel.y, e.vel.x);
        }

        // Leg animation - all enemy types
        if (!legSprites_.empty()) {
            float spd = e.vel.length();
            if (spd > 1.0f) {
                e.legRotation = atan2f(e.vel.y, e.vel.x);
                e.legAnimTimer += dt * std::min(1.0f, spd / e.speed);
                if (e.legAnimTimer > 0.08f) {
                    e.legAnimTimer = 0;
                    e.legAnimFrame = (e.legAnimFrame + 1) % (int)legSprites_.size();
                }
            } else {
                e.legAnimTimer = 0;
                e.legAnimFrame = 0;
            }
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
        // Collision zone triggers
        pushOutCollisionZones(e.pos, e.size * 0.4f);

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
        // Raycast (no angle limit - 360° awareness)
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

// Choose a fresh roam point: biased toward the current heading so movement reads
// as calm strolling rather than spinning in place, and validated so the point is
// not inside a wall and is reachable in a straight line.
void Game::pickWanderTarget(Enemy& e) {
    float baseHeading = (e.vel.lengthSq() > 1.0f)
        ? atan2f(e.vel.y, e.vel.x)
        : (float)(rand() % 360) * (float)M_PI / 180.0f;

    for (int attempt = 0; attempt < 8; attempt++) {
        // Cone around the current heading; widen it as candidates fail so the
        // enemy can still turn around when boxed in, but prefers gentle curves.
        float spread = 0.5f + attempt * 0.35f;
        float jitter = (((rand() % 1000) / 1000.0f) * 2.0f - 1.0f) * spread;
        float angle  = baseHeading + jitter;
        float dist   = WANDER_DIST_MIN + (rand() % (int)(WANDER_DIST_MAX - WANDER_DIST_MIN));
        Vec2  cand   = e.pos + Vec2::fromAngle(angle) * dist;
        cand.x = fmaxf(TILE_SIZE * 1.5f, fminf(map_.worldWidth()  - TILE_SIZE * 1.5f, cand.x));
        cand.y = fmaxf(TILE_SIZE * 1.5f, fminf(map_.worldHeight() - TILE_SIZE * 1.5f, cand.y));
        if (map_.isSolid(TileMap::toTile(cand.x), TileMap::toTile(cand.y))) continue;
        if (!tileRayClear(e.pos, cand)) continue;
        e.wanderTarget = cand;
        return;
    }
    // Nothing clear found - take a short step ahead and let steering nudge us free.
    e.wanderTarget = e.pos + Vec2::fromAngle(baseHeading) * WANDER_DIST_MIN;
}

void Game::enemyWander(Enemy& e, float dt) {
    bool arrived = (e.wanderTarget - e.pos).lengthSq() < (WANDER_ARRIVE_DIST * WANDER_ARRIVE_DIST);
    bool blocked = !tileRayClear(e.pos, e.wanderTarget);  // a wall slid between us and the goal
    if (gameTime_ >= e.nextWanderTime || arrived || blocked) {
        pickWanderTarget(e);
        // Stagger re-picks so a pack does not turn in unison.
        e.nextWanderTime = gameTime_ + WANDER_INTERVAL + ((rand() % 100) / 100.0f) * WANDER_INTERVAL;
    }
    Vec2 desired = steerToward(e.pos, e.wanderTarget, e.speed * WANDER_SPEED_FRAC, dt);
    // Smooth velocity for every type so roaming looks unhurried (no instant turns).
    float inertia = isShooterEnemyType(e.type) ? SHOOTER_INERTIA : MELEE_INERTIA;
    e.vel = Vec2::lerp(e.vel, desired, dt * inertia * 0.6f);
}

void Game::enemyChase(Enemy& e, float dt) {
    Vec2 targetPos = getEnemyTargetPos(e);
    Vec2 toPlayer  = targetPos - e.pos;
    float dist     = toPlayer.length();

    // Navigation target: head straight at the player when we have line of sight,
    // otherwise route around walls with a throttled BFS instead of mashing into
    // the geometry between us and them. navTarget feeds the approach steering;
    // targetPos stays the real player for facing, ranging and dash decisions.
    Vec2 navTarget = targetPos;
    bool los = tileRayClear(e.pos, targetPos);
    if (los) {
        e.pathValid = false;  // clear any stale route the moment we can see them
    } else {
        e.pathRepathTimer -= dt;
        if (e.pathRepathTimer <= 0 || !e.pathValid) {
            e.pathValid = computeEnemyPath(e.pos, targetPos, e.pathWaypoint);
            e.pathRepathTimer = ENEMY_REPATH_INTERVAL;
        }
        if (e.pathValid) navTarget = e.pathWaypoint;
    }

    // Shooter: keep range and strafe while shooting
    if (isShooterEnemyType(e.type)) {
        // No line of sight: cannot shoot through walls, so just route to the player.
        if (!los) {
            Vec2 desired = steerToward(e.pos, navTarget, e.speed, dt);
            e.vel = Vec2::lerp(e.vel, desired, dt * SHOOTER_INERTIA);
            e.rotation = atan2f(navTarget.y - e.pos.y, navTarget.x - e.pos.x);
            return;
        }
        // Sniper/BossSniper: panic dash-sprint away if player is dangerously close
        bool isSniper = (e.type == EnemyType::Sniper || e.type == EnemyType::BossSniper);
        float panicRange = isSniper ? (e.type == EnemyType::BossSniper ? BOSS_SNIPER_PANIC_RANGE : SNIPER_PANIC_RANGE) : 0.0f;
        if (isSniper && dist < panicRange) {
            Vec2 away = (e.pos - targetPos).normalized();
            float panicSpeed = e.speed * 3.2f;
            Vec2 desired = steerToward(e.pos, e.pos + away * 300.0f, panicSpeed, dt);
            e.vel = Vec2::lerp(e.vel, desired, dt * 16.0f);  // snap quickly
            e.rotation = atan2f(toPlayer.y, toPlayer.x);     // still face player while fleeing
            enemyShoot(e, dt);  // keep firing while panicking
            return;
        }
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
            Vec2 tn = toPlayer.normalized();
            Vec2 perp = Vec2{-tn.y, tn.x};
            float sideSign = (sinf(gameTime_ * 0.9f) > 0) ? 1.0f : -1.0f;
            e.vel = Vec2::lerp(e.vel, perp * sideSign * e.speed * 0.5f, dt * 4.0f);
        }
        enemyShoot(e, dt);
        e.rotation = atan2f(toPlayer.y, toPlayer.x);
        return;
    }

    // BossBrute: long-range charge - highest priority, always available
    if (e.type == EnemyType::BossBrute && !e.bossCharging
        && e.bossChargeCdTimer <= 0 && !e.dashCharging && !e.isDashing
        && dist > e.dashDistance && dist < BOSS_BRUTE_CHARGE_RANGE) {
        e.bossCharging  = true;
        e.bossChargeDir = toPlayer.normalized();
        e.dashTimer     = BOSS_BRUTE_CHARGE_DUR;
        e.flashTimer    = BOSS_BRUTE_CHARGE_DUR * 0.6f;
        e.vel = {0, 0};
        return;
    }

    // Movement: close orbit -> boss wide strafe -> straight charge
    const float strafeInner = e.dashDistance + e.strafeOrbitOffset;
    const float strafeOuter = e.dashDistance + 100.0f + e.strafeOrbitOffset;
    bool inOrbitBand = los && e.dashOnCd && dist > strafeInner && dist < strafeOuter;

    if (inOrbitBand) {
        // Close-range prowl - mostly forward, slight sidestep
        Vec2 toTarget = toPlayer.normalized();
        Vec2 perp = Vec2{-toTarget.y, toTarget.x};
        float sideSign = (sinf(gameTime_ * 1.3f + e.pos.x * 0.01f) > 0) ? 1.0f : -1.0f;
        Vec2 orbit = (toTarget * 0.78f + perp * sideSign * 0.40f).normalized();
        Vec2 desired = steerToward(e.pos, e.pos + orbit * 200.0f, e.speed * 0.85f, dt);
        e.vel = Vec2::lerp(e.vel, desired, dt * MELEE_INERTIA);
    } else if (los && e.type == EnemyType::BossBrute && e.bossChargeCdTimer > 0
               && dist > strafeOuter && dist < BOSS_BRUTE_CHARGE_RANGE
               && !e.dashCharging && !e.isDashing) {
        // Wide strafe while charge recharges - circle with light inward pressure
        Vec2 toTarget = toPlayer.normalized();
        Vec2 perp = Vec2{-toTarget.y, toTarget.x};
        float sideSign = (sinf(gameTime_ * 0.65f + e.pos.y * 0.007f) > 0) ? 1.0f : -1.0f;
        float fwd = (dist > BOSS_BRUTE_CHARGE_RANGE * 0.75f) ? 0.5f : 0.2f;
        Vec2 orbit = (toTarget * fwd + perp * sideSign * 0.85f).normalized();
        Vec2 desired = steerToward(e.pos, e.pos + orbit * 350.0f, e.speed, dt);
        e.vel = Vec2::lerp(e.vel, desired, dt * MELEE_INERTIA);
    } else {
        // Straight approach (LOS) or routed approach (navTarget hugs corners around walls).
        Vec2 desired = steerToward(e.pos, navTarget, e.speed, dt);
        e.vel = Vec2::lerp(e.vel, desired, dt * MELEE_INERTIA);
    }

    // Start dash when close - lock direction immediately (never wind up through a wall)
    if (los && dist < e.dashDistance && !e.dashOnCd && !e.dashCharging) {
        e.dashCharging   = true;
        e.dashDelayTimer = e.dashDelay;
        e.flashTimer     = e.dashDelay;
        e.dashDir        = toPlayer.normalized(); // lock dash direction now
        e.vel = {0, 0}; // stop for wind-up
    }
}

void Game::enemyDash(Enemy& e, float dt) {
    // Short lunge forward - not a screen-crossing dash, just a step into the swing
    e.vel = e.dashDir * e.dashForce;
    e.dashTimer -= dt;
    if (e.dashTimer <= 0) {
        e.isDashing = false;
        e.dashOnCd = true;
        e.dashCdTimer = e.dashCooldown;
        e.vel = {0, 0};
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
        Vec2 muzzle = e.pos + eFwd * 16.0f + eRight * 22.0f;
        float bulletSpeed = ENEMY_BULLET_SPEED;
        if (e.type == EnemyType::Sniper || e.type == EnemyType::BossSniper) {
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

    // Obstacle check: tile walls + CollisionZone triggers
    const float probeR = TILE_SIZE * 0.35f;
    auto isBlocked = [&](Vec2 pt) -> bool {
        if (map_.isSolid(TileMap::toTile(pt.x), TileMap::toTile(pt.y))) return true;
        for (const auto& t : customMap_.triggers) {
            if (t.type != TriggerType::CollisionZone) continue;
            if (collisionZonePushout(pt, probeR, {t.x, t.y},
                    t.width*0.5f, t.height*0.5f, triggerGetAngle(t)).lengthSq() > 0.0f)
                return true;
        }
        return false;
    };

    // Two lookahead distances: close catches immediate obstacles, far allows early steering
    const float close = TILE_SIZE * 0.7f;
    const float far   = TILE_SIZE * 2.0f;
    bool closeBlocked = isBlocked(from + dir * close);
    bool farBlocked   = isBlocked(from + dir * far);

    if (closeBlocked || farBlocked) {
        float baseAngle  = atan2f(dir.y, dir.x);
        float lookDist   = closeBlocked ? close : far;
        // Finer increments (0.25 rad) over wider sweep to find open direction
        for (float offset = 0.25f; offset <= (float)M_PI; offset += 0.25f) {
            float aR = baseAngle + offset;
            if (!isBlocked(from + Vec2{cosf(aR), sinf(aR)} * lookDist))
                return Vec2{cosf(aR), sinf(aR)} * spd;
            float aL = baseAngle - offset;
            if (!isBlocked(from + Vec2{cosf(aL), sinf(aL)} * lookDist))
                return Vec2{cosf(aL), sinf(aL)} * spd;
        }
        // All feelers blocked: slide perpendicular
        return Vec2{-dir.y, dir.x} * spd * 0.5f;
    }

    return dir * spd;
}

// True if a straight segment from a to b crosses no solid tile.
bool Game::tileRayClear(Vec2 a, Vec2 b) const {
    Vec2 d = b - a;
    float dist = d.length();
    if (dist < 1.0f) return true;
    Vec2 dir = d / dist;
    const float step = TILE_SIZE * 0.4f;
    for (float t = step; t < dist; t += step) {
        Vec2 pt = a + dir * t;
        if (map_.isSolid(TileMap::toTile(pt.x), TileMap::toTile(pt.y))) return false;
    }
    return true;
}

// Breadth-first search a walkable route from 'from' to 'to' over the tile grid.
// Returns the furthest tile along that route the enemy can head straight for
// (string-pulling), so movement cuts corners naturally instead of stepping
// tile-by-tile. Returns false when no route exists.
bool Game::computeEnemyPath(Vec2 from, Vec2 to, Vec2& outWaypoint) const {
    const int W = map_.width, H = map_.height;
    int sx = TileMap::toTile(from.x), sy = TileMap::toTile(from.y);
    int gx = TileMap::toTile(to.x),   gy = TileMap::toTile(to.y);
    if (!map_.isInBounds(sx, sy) || !map_.isInBounds(gx, gy)) return false;
    if (sx == gx && sy == gy) { outWaypoint = to; return true; }

    // If the player stands on/over a solid tile, aim for the nearest open tile.
    if (map_.isSolid(gx, gy)) {
        bool found = false;
        for (int r = 1; r <= 2 && !found; r++)
            for (int dy = -r; dy <= r && !found; dy++)
                for (int dx = -r; dx <= r && !found; dx++) {
                    int nx = gx + dx, ny = gy + dy;
                    if (map_.isInBounds(nx, ny) && !map_.isSolid(nx, ny)) { gx = nx; gy = ny; found = true; }
                }
        if (!found) return false;
    }

    auto idx = [&](int x, int y) { return y * W + x; };
    pathScratch_.assign((size_t)W * H, -1);
    std::vector<int> queue;
    queue.reserve(256);
    int start = idx(sx, sy);
    pathScratch_[start] = start;  // self-parent marks the visited root
    queue.push_back(start);

    const int dx4[4] = {1, -1, 0, 0};
    const int dy4[4] = {0, 0, 1, -1};
    const int kMaxNodes = 2200;   // cap work on large maps
    int head = 0, expanded = 0;
    bool reached = false;
    while (head < (int)queue.size() && expanded < kMaxNodes) {
        int cur = queue[head++]; expanded++;
        int cx = cur % W, cy = cur / W;
        if (cx == gx && cy == gy) { reached = true; break; }
        for (int i = 0; i < 4; i++) {
            int nx = cx + dx4[i], ny = cy + dy4[i];
            if (!map_.isInBounds(nx, ny) || map_.isSolid(nx, ny)) continue;
            int ni = idx(nx, ny);
            if (pathScratch_[ni] != -1) continue;
            pathScratch_[ni] = cur;
            queue.push_back(ni);
        }
    }
    if (!reached) return false;

    // Walk parents from goal back to start: path[0] = goal ... path.back() = start.
    std::vector<int> path;
    int cur = idx(gx, gy);
    while (true) {
        path.push_back(cur);
        int p = pathScratch_[cur];
        if (p == cur) break;            // reached the self-parented start
        cur = p;
        if ((int)path.size() > W * H) break;  // paranoia guard
    }
    // Furthest goal-ward tile we can reach in a straight line is the best waypoint.
    for (int t : path) {
        Vec2 wp{ TileMap::toWorld(t % W), TileMap::toWorld(t / W) };
        if (tileRayClear(from, wp)) { outWaypoint = wp; return true; }
    }
    // Fallback: the first step off the start tile.
    if (path.size() >= 2) {
        int t = path[path.size() - 2];
        outWaypoint = Vec2{ TileMap::toWorld(t % W), TileMap::toWorld(t / W) };
        return true;
    }
    outWaypoint = to;
    return true;
}

// Bullets

void Game::updateBullets(float dt) {
    const bool bulletSimAuth = !NetworkManager::instance().isOnline() || NetworkManager::instance().isHost() ||
        (NetworkManager::instance().isConnectedToDedicated() && NetworkManager::instance().isLobbyHost());
    for (auto& b : bullets_) {
        b.tick(dt);
        // Magnet upgrade - gently steer bullets toward the nearest alive enemy
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
                    const float turnRate = std::min(3.5f * (float)upgrades_.hasMagnet, 10.0f); // rad/s, scales with stacks
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
            } else if (upgrades_.hasRicochet && b.bounces < 3 * upgrades_.hasRicochet) {
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
        // CollisionZone trigger
        if (b.alive) {
            for (const auto& t : customMap_.triggers) {
                if (t.type != TriggerType::CollisionZone) continue;
                if (collisionZonePushout(b.pos, BULLET_SIZE * 0.3f, {t.x, t.y},
                        t.width * 0.5f, t.height * 0.5f, triggerGetAngle(t)).lengthSq() > 0.0f) {
                    if (b.explosive) spawnBulletExplosion(b.pos, b.damage, b.ownerId, b.ownerSubSlot, -1, bulletSimAuth);
                    b.alive = false;
                    break;
                }
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
        // CollisionZone trigger
        if (b.alive) {
            for (const auto& t : customMap_.triggers) {
                if (t.type != TriggerType::CollisionZone) continue;
                if (collisionZonePushout(b.pos, BULLET_SIZE * 0.3f, {t.x, t.y},
                        t.width * 0.5f, t.height * 0.5f, triggerGetAngle(t)).lengthSq() > 0.0f) {
                    b.alive = false;
                    break;
                }
            }
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
    b.damage = std::max(10, (int)roundf(10.0f * upgrades_.damageMulti *
        (upgrades_.hasLastStand && player_.hp <= 10 ? (1.0f + (float)upgrades_.hasLastStand) : 1.0f)));
    b.piercing = false;
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

    // Visual polish: muzzle flash
    muzzleFlashTimer_ = 0.06f;
    muzzleFlashPos_ = b.pos; // store exact bullet spawn point
    camera_.addShake(1.5f);  // subtle recoil shake

    // Sync bullet to other players
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
    b.damage = 10;
    enemyBullets_.push_back(b);

    // Enemy shoot SFX (quieter)
    if (sfxEnemyShoot_) playSFX(sfxEnemyShoot_, config_.sfxVolume * 2 / 5);

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

    // Story: explosions also harm civilians / infrastructure / operators caught
    // in the blast (radius only; bystander radius is added inside the helper).
    if (playingCustomMap_ && !bystanders_.empty())
        damageBystandersAt(pos, radius, (float)splashDamage);

    for (size_t i = 0; i < enemies_.size(); ++i) {
        if ((int)i == skipEnemyIdx) continue;
        auto& e = enemies_[i];
        if (!e.alive) continue;
        if (Vec2::dist(pos, e.pos) > radius + e.size) continue;
        e.damageFlash = 1.0f;
        e.hp -= splashDamage;
        if (upgrades_.hasStunRounds) e.stunTimer = std::max(e.stunTimer, 0.4f * (float)upgrades_.hasStunRounds);
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

// Bombs & Explosions

void Game::updateBombs(float dt) {
    auto& netBombs = NetworkManager::instance();
    // Only the bomb owner (or offline) spawns the explosion.
    // Remote machines skip the spawn and wait for the network explosion packet,
    // preventing the double-explosion where both sides fire spawnExplosion.
    auto detonateIfOwner = [&](Bomb& b) {
        bool isOwner = !netBombs.isOnline()
                    || b.ownerId == 255
                    || b.ownerId == netBombs.localPlayerId();
        if (isOwner) spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
        b.alive = false;
    };

    for (auto& b : bombs_) {
        if (!b.alive) continue;
        b.age += dt;

        // Animation - blink between bomb1 and bomb2
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
                } else {
                    detonateIfOwner(b);
                }
            }
            // CollisionZone trigger => explode
            if (b.alive) {
                for (const auto& t : customMap_.triggers) {
                    if (t.type != TriggerType::CollisionZone) continue;
                    if (collisionZonePushout(b.pos, BOMB_SIZE * 0.3f, {t.x, t.y},
                            t.width*0.5f, t.height*0.5f, triggerGetAngle(t)).lengthSq() > 0.0f) {
                        detonateIfOwner(b);
                        break;
                    }
                }
            }
            // Explode if speed drops too low
            if (b.alive && b.vel.length() < 30.0f) {
                detonateIfOwner(b);
            }
            // Proximity: explode on contact with any enemy
            if (b.alive) {
                for (auto& e : enemies_) {
                    if (!e.alive) continue;
                    if (Vec2::dist(b.pos, e.pos) < BOMB_SIZE + 20.0f) {
                        detonateIfOwner(b);
                        break;
                    }
                }
            }
            // Proximity: explode on contact with any remote enemy player (PvP)
            if (b.alive && (lobbySettings_.isPvp || currentRules_.pvpEnabled)) {
                uint8_t localId = netBombs.localPlayerId();
                for (auto& rp : netBombs.players()) {
                    if (rp.id == localId || !rp.alive || rp.spectating) continue;
                    if (localTeam_ >= 0 && rp.team == localTeam_) continue;
                    if (Vec2::dist(b.pos, rp.targetPos) < BOMB_SIZE + 20.0f) {
                        detonateIfOwner(b);
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
            // Story: explosions also harm bystanders caught in the blast.
            if (playingCustomMap_ && !bystanders_.empty())
                damageBystandersAt(ex.pos, ex.radius, ex.damage);
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
            // In PvP, explosions deal 3 HP damage to the local player - host-validated
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
                    int newHp = std::max(0, player_.hp - PVP_EXPLOSION_DAMAGE);
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
                            rpM->hp -= PVP_EXPLOSION_DAMAGE;
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
                        target.hp = std::max(0, target.hp - PVP_EXPLOSION_DAMAGE);
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
                        slot.player.takeDamage(30);  // 30 damage from explosion
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
    hitStopTimer_ = std::max(hitStopTimer_, 0.055f);  // freeze-frame for impact weight
    playExplosionFeedback(pos, ex.radius * 8.4f, 0.18f, 0.62f, 130, 320, 1.45f, 0.74f,
                          config_.sfxVolume, config_.sfxVolume / 12);

    // Sync explosion to other players (guard prevents echo-loop from network callback)
    auto& net = NetworkManager::instance();
    if (net.isOnline() && !suppressNetExplosion_) {
        net.sendExplosion(pos, ownerId, ownerSlot);
    }

    // Visual polish: screen flash on explosion
    screenFlashTimer_ = 0.15f;
    screenFlashR_ = 255; screenFlashG_ = 200; screenFlashB_ = 80;

    // Spawn fire/debris particles
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

    // Scorch marks on ground
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

    // Destroy boxes caught in explosion radius
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
        if (posInCollisionZone(sp, ENEMY_SIZE * 0.5f)) return false;
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
    b.orbitRadius = (55.0f + (float)(rand() % 20)) * (inVehicle_ ? 2.0f : 1.0f);
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

// Spawning

void Game::updateSpawning(float dt) {
    // In multiplayer the host is authoritative - clients receive enemy spawns via state packets.
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
    // Note: pvpActive here means "PvP arena gamemode" only - friendly fire in PvE does NOT skip waves.
    bool pvpActive = lobbySettings_.isPvp;
    if (sandboxMode_ || pvpActive) return;
    // Wave-based spawning system
    if (waveActive_) {
        // Spawning enemies within current wave
        waveSpawnTimer_ -= dt;
        if (waveSpawnTimer_ <= 0 && waveEnemiesLeft_ > 0) {
            waveSpawnTimer_ = (lastBossWaveNum_ >= 0) ? WAVE_SPAWN_INTERVAL * 0.5f : WAVE_SPAWN_INTERVAL;
            Vec2 sp = pickEnemySpawnPos();
            if (!map_.worldCollides(sp.x, sp.y, ENEMY_SIZE * 0.5f)) {
                spawnEnemy(sp, rollWaveEnemyType());
                waveEnemiesLeft_--;
            }
        }
        if (waveEnemiesLeft_ <= 0) {
            waveActive_ = false;

            // PVE victory check: all enemies dead + reached wave count
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

            // Block new wave while a boss is still alive
            {
                bool anyBossAlive = false;
                for (auto& be : enemies_)
                    if (be.alive && isBossType(be.type)) { anyBossAlive = true; break; }
                if (anyBossAlive) {
                    bossWaveActive_ = true;
                    wavePauseTimer_ = 2.0f;  // re-check soon
                    return;
                }
                bossWaveActive_ = false;  // boss slain - clear flag
                // If this was the last boss wave, enable endless post-boss scaling
                {
                    constexpr int LAST_BOSS_WAVE = BOSS_WAVES[sizeof(BOSS_WAVES)/sizeof(*BOSS_WAVES) - 1];
                    if (waveNumber_ == LAST_BOSS_WAVE && lastBossWaveNum_ < 0)
                        lastBossWaveNum_ = waveNumber_;
                }
            }

            // Start new wave
            waveNumber_++;

            // Determine if this is a boss wave
            EnemyType bossWaveType = EnemyType::BossBrute;
            bool isBossWave = false;
            for (int bw : BOSS_WAVES) {
                if (waveNumber_ == bw) { isBossWave = true; break; }
            }
            if (waveNumber_ == 35)  bossWaveType = EnemyType::BossSniper;
            else if (waveNumber_ == 45) bossWaveType = EnemyType::BossGunner;

            if (isBossWave) {
                // Spawn a single boss - no normal enemies this wave
                Vec2 bsp = pickEnemySpawnPos();
                spawnEnemy(bsp, bossWaveType);
                waveEnemiesLeft_ = 0;
                waveActive_ = false;
                bossWaveActive_ = true;
                // Extended announcement banner (boss waves get 4 seconds)
                waveAnnounceTimer_ = 4.0f;
                waveAnnounceNum_ = waveNumber_;
                if (sendNetEvents) {
                    auto& net = NetworkManager::instance();
                    net.sendWaveStart(waveNumber_);
                }
                return;
            }

            int waveSize = WAVE_SIZE_BASE + waveNumber_ * WAVE_SIZE_GROWTH;
            if (waveSize > WAVE_MAX_SIZE) waveSize = WAVE_MAX_SIZE;
            // Post-last-boss: 1.3x enemies each consecutive wave, uncapped
            if (lastBossWaveNum_ >= 0) {
                int n = waveNumber_ - lastBossWaveNum_;
                waveSize = (int)(waveSize * powf(1.3f, (float)n));
            }
            waveEnemiesLeft_ = waveSize;
            waveActive_ = true;
            waveSpawnTimer_ = 0;

            // Milestone: guaranteed first appearance of elite types
            if (waveNumber_ == MILESTONE_SNIPER_WAVE) {
                spawnEnemy(pickEnemySpawnPos(), EnemyType::Sniper);
            } else if (waveNumber_ == MILESTONE_GUNNER_WAVE) {
                spawnEnemy(pickEnemySpawnPos(), EnemyType::Gunner);
            }

            // Sync wave start to clients
            if (sendNetEvents) {
                auto& net = NetworkManager::instance();
                net.sendWaveStart(waveNumber_);
            }

            // Visual polish: wave announcement banner
            waveAnnounceTimer_ = (waveNumber_ == MILESTONE_SNIPER_WAVE ||
                                   waveNumber_ == MILESTONE_GUNNER_WAVE) ? 3.5f : 2.5f;
            waveAnnounceNum_ = waveNumber_;
        }
    }
}

void Game::spawnEnemy(Vec2 pos, EnemyType type) {
    // Never embed an enemy in a collision zone, whatever the spawn source
    // (random wave, map-placed marker, cutscene, or network).
    pushOutCollisionZones(pos, ENEMY_SIZE * 0.5f);
    Enemy e;
    e.pos = pos;
    e.type = type;
    e.wanderTarget = pos;
    e.nextWanderTime = gameTime_ + 1.0f;
    e.renderScale = 3.0f;
    // Randomize orbit offset so melee enemies don't all pile up at the same radius
    e.strafeOrbitOffset = ((rand() % 161) - 80);  // -80 .. +80 px
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
        // Boss variants
        case EnemyType::BossBrute:
            e.hp = BOSS_BRUTE_HP * config_.enemyHpScale;
            e.maxHp = BOSS_BRUTE_HP * config_.enemyHpScale;
            e.speed = BOSS_BRUTE_SPEED * config_.enemySpeedScale;
            e.size = BOSS_BRUTE_SIZE;
            e.dashDistance = BOSS_BRUTE_DASH_DIST;
            e.dashForce = BOSS_BRUTE_DASH_FORCE;
            e.dashDelay = BOSS_BRUTE_DASH_DELAY;
            e.dashDuration = BOSS_BRUTE_DASH_DUR;
            e.dashCooldown = BOSS_BRUTE_DASH_CD;
            e.contactDamage = BOSS_BRUTE_DASH_DMG;
            e.renderScale = BOSS_BRUTE_SCALE;
            break;
        case EnemyType::BossSniper:
            e.hp = BOSS_SNIPER_HP * config_.enemyHpScale;
            e.maxHp = BOSS_SNIPER_HP * config_.enemyHpScale;
            e.speed = BOSS_SNIPER_SPEED * config_.enemySpeedScale;
            e.size = BOSS_SNIPER_SIZE;
            e.shootCooldown = BOSS_SNIPER_SHOOT_CD;
            e.shootCooldownBase = BOSS_SNIPER_SHOOT_CD;
            e.preferredMinRange = 460.0f;
            e.preferredMaxRange = 800.0f;
            e.renderScale = BOSS_SNIPER_SCALE;
            break;
        case EnemyType::BossGunner:
            e.hp = BOSS_GUNNER_HP * config_.enemyHpScale;
            e.maxHp = BOSS_GUNNER_HP * config_.enemyHpScale;
            e.speed = BOSS_GUNNER_SPEED * config_.enemySpeedScale;
            e.size = BOSS_GUNNER_SIZE;
            e.shootCooldown = BOSS_GUNNER_SHOOT_CD;
            e.shootCooldownBase = BOSS_GUNNER_SHOOT_CD;
            e.preferredMinRange = 220.0f;
            e.preferredMaxRange = 480.0f;
            e.shotsPerBurst = BOSS_GUNNER_BURST;
            e.burstGap = BOSS_GUNNER_BURST_GAP;
            e.shootSpread = 0.22f;
            e.renderScale = BOSS_GUNNER_SCALE;
            break;
        case EnemyType::Melee:
        default:
            e.hp = ENEMY_HP * config_.enemyHpScale;
            e.maxHp = ENEMY_HP * config_.enemyHpScale;
            e.speed = ENEMY_SPEED * config_.enemySpeedScale;
            e.renderScale = 3.0f;
            break;
    }
    // Push out of CollisionZone triggers before adding so spawn position is clean
    pushOutCollisionZones(e.pos, e.size * 0.4f);
    enemies_.push_back(e);
}

// Collisions

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
        const int maxJumps = 4 * upgrades_.hasChainLightning;
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
        if (jumps > 0 && sfxParry_) playSFX(sfxParry_, config_.sfxVolume / 3);
    };

    // Player bullets vs enemies - all peers resolve locally for instant feedback;
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
                // Blood splatter - visual only, runs on all peers
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
                    stampTileBlood(bd.pos, 0.25f * bd.scale);
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
                        stampTileBlood(extra.pos, 0.15f * extra.scale);
                    }
                }
                if (!b.piercing) {
                    b.alive = false;
                    // Only the simulation authority broadcasts bullet removal.
                    // If a non-host client sent this, the host's copy would be killed
                    // before it could apply damage to the enemy.
                    if (net.isInGame() && b.netId != 0 && simAuth) {
                        net.sendBulletHit(b.netId);
                    }
                }
                triggerExplosiveTip(b, b.pos, (int)ei);
                triggerChainLightning(b, b.pos, (int)ei);
                // State changes are authoritative on: offline, P2P host, or dedicated-server lobby-host
                if (simAuth) {
                    float dmg = b.damage * (1.0f - e.bulletDamageReduction);
                    e.hp -= dmg;
                    if (upgrades_.hasStunRounds) e.stunTimer = std::max(e.stunTimer, 0.75f * (float)upgrades_.hasStunRounds);
                    // Aggro - target the player who shot this bullet
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
        if (!b.alive || p.dead || godMode_) continue;
        if (circleOverlap(b.pos, b.size, p.pos, PLAYER_SIZE * 0.5f)) {
            if (p.isParrying) {
                // Parry reflects - fire back at sniper-bullet speed
                b.vel = b.vel.normalized() * -(ENEMY_BULLET_SPEED * SNIPER_BULLET_SPEED_MULTI);
                b.tag = TAG_BULLET;
                b.damage = PARRY_REFLECT_DAMAGE * (upgrades_.hasParrySurge ? (1 + upgrades_.hasParrySurge) : 1);
                bullets_.push_back(b);
                b.alive = false;
                camera_.addShake(2.2f);
                rumble(0.18f, 45, 0.28f, 0.72f);  // Soft parry-reflect tick
                if (sfxParry_) playSFX(sfxParry_, config_.sfxVolume);
            } else {
                p.takeDamage(b.damage);
                b.alive = false;
                camera_.addShake(1.8f);
                rumble(0.48f, 150, 1.25f, 0.78f);  // Heavier damage thud
                if (sfxHurt_)       playSFX(sfxHurt_, config_.sfxVolume / 4);
                if (sfxPlayerHurt_) playSFX(sfxPlayerHurt_, config_.sfxVolume / 3);
                if (p.dead) {
                    if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
                    camera_.addShake(4.0f);
                    rumble(0.92f, 340, 1.50f, 0.82f);  // Heavy death rumble
                    auto& net = NetworkManager::instance();
                    if (!net.isInGame()) spawnPlayerDeathEffect(p.pos);
                    if (net.isInGame()) net.sendPlayerDied(net.localPlayerId(), 0);
                }
            }
        }
    }

    // Melee enemies vs player - arc-based strike check (front cone + reach)
    if (!p.dead && !godMode_) for (auto& e : enemies_) {
        if (!e.alive || !(e.isDashing || e.bossCharging || e.netIsDashing)) continue;
        Vec2 toP = p.pos - e.pos;
        float dist = toP.length();
        float reach = e.size * 0.5f + PLAYER_SIZE * 0.5f + 28.0f;
        float angDiff = atan2f(toP.y, toP.x) - e.rotation;
        while (angDiff >  (float)M_PI) angDiff -= 2.0f * (float)M_PI;
        while (angDiff < -(float)M_PI) angDiff += 2.0f * (float)M_PI;
        bool inArc = dist < reach && fabsf(angDiff) < 1.65f; // ±95 deg
        if (inArc) {
            if (p.isParrying) {
                // Parry counter!
                float surgeMul = upgrades_.hasParrySurge ? (1.0f + (float)upgrades_.hasParrySurge) : 1.0f;
                e.hp -= PARRY_DMG * surgeMul;
                e.stunTimer = 1.0f;
                e.isDashing = false;
                e.vel = (e.pos - p.pos).normalized() * (500.0f * (upgrades_.hasParrySurge ? (1.0f + 0.5f * upgrades_.hasParrySurge) : 1.0f));
                e.damageFlash = 1.0f;
                camera_.addShake(2.2f);
                rumble(0.52f, 84, 0.65f, 1.45f);  // Sharp parry counter rumble
                if (sfxParry_) playSFX(sfxParry_, config_.sfxVolume);
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
                if (sfxHurt_)       playSFX(sfxHurt_, config_.sfxVolume / 4);
                if (sfxPlayerHurt_) playSFX(sfxPlayerHurt_, config_.sfxVolume / 3);
                if (p.dead) {
                    if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
                    camera_.addShake(4.0f);
                    rumble(0.92f, 340, 1.50f, 0.82f);  // Heavy death rumble
                    auto& net2 = NetworkManager::instance();
                    if (!net2.isInGame()) spawnPlayerDeathEffect(p.pos);
                    if (net2.isInGame()) net2.sendPlayerDied(net2.localPlayerId(), 0);
                }
            }
        }
    }

    // Enemy body contact vs player - any alive enemy that overlaps the player deals 1 damage.
    // takeDamage() is no-op during the invuln window so this can't spam.
    if (!p.dead && !godMode_) {
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            float overlap = e.size * 0.5f + PLAYER_SIZE * 0.5f;
            Vec2  delta   = p.pos - e.pos;
            float dist    = delta.length();
            if (dist < overlap) {
                Vec2  push = (dist > 0.1f) ? delta.normalized() : Vec2{1,0};
                float pen  = overlap - dist;
                e.pos -= push * (pen * 0.4f);
                p.pos += push * (pen * 0.6f);
                if (p.isParrying) {
                    // Parry counter: damage + stun + strong knockback, no player damage
                    float surgeMul = upgrades_.hasParrySurge ? (1.0f + (float)upgrades_.hasParrySurge) : 1.0f;
                    e.hp        -= PARRY_DMG * surgeMul;
                    e.stunTimer  = 0.6f;
                    e.isDashing  = false;
                    e.damageFlash = 1.0f;
                    e.vel        = push * (520.0f * (upgrades_.hasParrySurge ? (1.0f + 0.5f * upgrades_.hasParrySurge) : 1.0f));
                    camera_.addShake(1.8f);
                    rumble(0.52f, 84, 0.65f, 1.45f);
                    if (sfxParry_) playSFX(sfxParry_, config_.sfxVolume);
                    if (e.hp <= 0) {
                        killEnemy(e);
                        auto& net2 = NetworkManager::instance();
                        const bool simAuth2 = net2.isHost() || (net2.isConnectedToDedicated() && net2.isLobbyHost());
                        if (net2.isInGame() && simAuth2) {
                            uint32_t eIdx = (uint32_t)(&e - &enemies_[0]);
                            net2.sendEnemyKilled(eIdx, net2.localPlayerId());
                            enemyStatesNeedUpdate_ = true;
                        }
                    }
                } else {
                    e.vel -= push * 120.0f;
                    if (p.bodyContactCooldown <= 0.0f) {
                        bool hurt = !p.invulnerable;
                        p.takeDamage(10);
                        p.bodyContactCooldown = 0.3f;
                        if (hurt && !p.dead) {
                            camera_.addShake(1.2f);
                            rumble(0.38f, 100, 1.0f, 0.6f);
                            if (sfxHurt_)       playSFX(sfxHurt_, config_.sfxVolume / 4);
                            if (sfxPlayerHurt_) playSFX(sfxPlayerHurt_, config_.sfxVolume / 3);
                        }
                        if (hurt && p.dead) {
                            camera_.addShake(4.0f);
                            rumble(0.92f, 340, 1.50f, 0.82f);
                            if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
                            auto& net2 = NetworkManager::instance();
                            if (!net2.isInGame()) spawnPlayerDeathEffect(p.pos);
                            if (net2.isInGame())  net2.sendPlayerDied(net2.localPlayerId(), 0);
                        }
                    }
                }
            }
        }
    }

    // Local co-op / MP-splitscreen: damage for extra players (slots 1-3)
    bool anyLocalSplitscreen = coopPlayerCount_ > 1 &&
        (state_ == GameState::LocalCoopGame  || state_ == GameState::LocalCoopPaused ||
         state_ == GameState::MultiplayerGame || state_ == GameState::MultiplayerPaused ||
         state_ == GameState::MultiplayerDead || state_ == GameState::MultiplayerSpectator);
    if (anyLocalSplitscreen) {
        for (int ci = 1; ci < 4; ci++) {
            if (!coopSlots_[ci].joined) continue;
            Player& cp = coopSlots_[ci].player;
            if (cp.dead) continue;
            cp.bodyContactCooldown = std::max(0.0f, cp.bodyContactCooldown - dt_);
            for (auto& b : enemyBullets_) {
                if (!b.alive) continue;
                if (cp.invulnerable) continue;
                float eHitR = b.size + PLAYER_SIZE * 0.5f;
                bool ePoint = circleOverlap(b.pos, b.size, cp.pos, PLAYER_SIZE * 0.5f);
                bool eSwept = sweptCircleOverlap(b.pos, b.vel, 1.0f / 30.0f, cp.pos, eHitR);
                if (ePoint || eSwept) {
                    if (cp.isParrying) {
                        b.vel = b.vel.normalized() * -(ENEMY_BULLET_SPEED * SNIPER_BULLET_SPEED_MULTI); b.tag = TAG_BULLET;
                        b.damage = PARRY_REFLECT_DAMAGE;
                        bullets_.push_back(b); b.alive = false;
                        coopSlots_[ci].camera.addShake(2.2f);
                        rumbleForSlot(ci, 0.18f, 45, 0.28f, 0.72f);  // Soft parry-reflect tick
                        if (sfxParry_) playSFX(sfxParry_, config_.sfxVolume);
                    } else {
                        cp.takeDamage(b.damage); b.alive = false;
                        coopSlots_[ci].camera.addShake(1.8f);
                        rumbleForSlot(ci, cp.dead ? 0.92f : 0.48f, cp.dead ? 340 : 150,
                                      cp.dead ? 1.50f : 1.25f, cp.dead ? 0.82f : 0.78f);
                        if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                    }
                }
            }
            for (auto& e : enemies_) {
                if (!e.alive || !(e.isDashing || e.bossCharging || e.netIsDashing)) continue;
                Vec2 toCP = cp.pos - e.pos;
                float cpDist = toCP.length();
                float cpReach = e.size * 0.5f + PLAYER_SIZE * 0.5f + 28.0f;
                float cpAng = atan2f(toCP.y, toCP.x) - e.rotation;
                while (cpAng >  (float)M_PI) cpAng -= 2.0f * (float)M_PI;
                while (cpAng < -(float)M_PI) cpAng += 2.0f * (float)M_PI;
                if (cpDist < cpReach && fabsf(cpAng) < 1.65f) {
                    if (cp.isParrying) {
                        e.hp -= PARRY_DMG; e.stunTimer = 1.0f; e.isDashing = false;
                        e.vel = (e.pos - cp.pos).normalized() * 500.0f; e.damageFlash = 1.0f;
                        coopSlots_[ci].camera.addShake(2.2f);
                        rumbleForSlot(ci, 0.52f, 84, 0.65f, 1.45f);
                        if (sfxParry_) playSFX(sfxParry_, config_.sfxVolume);
                        if (e.hp <= 0) killEnemy(e);
                    } else {
                        cp.takeDamage(e.contactDamage);
                        coopSlots_[ci].camera.addShake(1.8f);
                        rumbleForSlot(ci, cp.dead ? 0.92f : 0.54f, cp.dead ? 340 : 150,
                                      cp.dead ? 1.50f : 1.30f, cp.dead ? 0.82f : 0.72f);
                        if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                    }
                }
            }
            // Enemy body contact vs this co-op slot
            for (auto& e : enemies_) {
                if (!e.alive) continue;
                float overlap = e.size * 0.5f + PLAYER_SIZE * 0.5f;
                Vec2  delta   = cp.pos - e.pos;
                float dist    = delta.length();
                if (dist < overlap) {
                    Vec2 push = (dist > 0.1f) ? delta.normalized() : Vec2{1,0};
                    float pen = overlap - dist;
                    e.pos  -= push * (pen * 0.4f);
                    cp.pos += push * (pen * 0.6f);
                    e.vel  -= push * 120.0f;
                    if (cp.bodyContactCooldown <= 0.0f) {
                        bool hurt = !cp.invulnerable;
                        cp.takeDamage(10);
                        cp.bodyContactCooldown = 0.3f;
                        if (hurt) {
                            coopSlots_[ci].camera.addShake(1.2f);
                            rumbleForSlot(ci, 0.38f, 100, 1.0f, 0.6f);
                            if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                        }
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
                        if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                    }
                }
            }
            // Track co-op death (cp was alive at loop start)
            if (cp.dead) coopSlots_[ci].deaths++;
        }

        // PvP bullets vs slot 0 (P1) in offline local coop
        // (The ci=1..3 loop above covers sub-players; slot 0 is player_ and
        // needs a separate check since it's outside that loop.)
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
                    if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                }
            }
            if (p1WasAlive && player_.dead) coopSlots_[0].deaths++;
        }
    }

    // PVP: Player bullets vs remote players (when friendlyFire/pvp is enabled)
    if (net.isInGame() && (lobbySettings_.isPvp || currentRules_.pvpEnabled)) {
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
                    if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                    if (target.hp <= 0 && sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
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
                    if (localTeam_ >= 0 && rp.team == localTeam_) continue; // same team - no damage
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
                    // Visual feedback on shooter side - damage/kill is handled victim-side
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
                            if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
                            net.sendPlayerDied(net.localPlayerId(), b.ownerId);
                        } else {
                            camera_.addShake(2.0f);
                            if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                            net.sendPlayerHpSync(net.localPlayerId(), player_.hp, player_.maxHp, b.ownerId);
                        }
                    } else {
                        // P2P non-host: report hit to host for validation - host will send back HP/death
                        net.sendHitRequest(b.netId, b.damage, b.ownerId);
                        // Optimistic visual feedback only (no HP deducted yet)
                        camera_.addShake(1.5f);
                        if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
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
                            if (sfxDeath_) playSFX(sfxDeath_, config_.sfxVolume / 5);
                            net.sendSubPlayerDied(net.localPlayerId(), (uint8_t)ci, b.ownerId);
                        } else {
                            if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                            net.sendSubPlayerHpSync(net.localPlayerId(), (uint8_t)ci, cp.hp, cp.maxHp, b.ownerId);
                        }
                    } else {
                        net.sendHitRequest(b.netId, b.damage, b.ownerId, (uint8_t)ci);
                        coopSlots_[ci].camera.addShake(1.5f);
                        if (sfxHurt_) playSFX(sfxHurt_, config_.sfxVolume / 4);
                    }
                    break;
                }
            }
        }
    }

    // Bombs vs enemies (dashed bombs)
    {
        auto& netColl = NetworkManager::instance();
        auto detonateIfOwnerColl = [&](Bomb& b) {
            bool isOwner = !netColl.isOnline()
                        || b.ownerId == 255
                        || b.ownerId == netColl.localPlayerId();
            if (isOwner) spawnExplosion(b.pos, b.ownerId, b.ownerSubSlot);
            b.alive = false;
        };
        for (auto& b : bombs_) {
            if (!b.alive || !b.hasDashed) continue;
            for (auto& e : enemies_) {
                if (!e.alive) continue;
                if (circleOverlap(b.pos, BOMB_SIZE, e.pos, e.size * 0.7f)) {
                    detonateIfOwnerColl(b);
                    break;
                }
            }
        }

        // Bombs vs remote players (PvP, dashed bombs)
        if (lobbySettings_.isPvp || currentRules_.pvpEnabled) {
            uint8_t localId = netColl.localPlayerId();
            for (auto& b : bombs_) {
                if (!b.alive || !b.hasDashed) continue;
                for (const auto& rp : netColl.players()) {
                    if (rp.id == localId || !rp.alive || rp.spectating) continue;
                    if (rp.id == b.ownerId) continue;
                    if (localTeam_ >= 0 && rp.team == localTeam_) continue;
                    if (circleOverlap(b.pos, BOMB_SIZE, rp.targetPos, 18.0f)) {
                        detonateIfOwnerColl(b);
                        break;
                    }
                }
            }
        }
    }
}

void Game::killEnemy(Enemy& e, bool trackKill) {
    if (!e.alive) return;  // already dead - avoid double gib/score effects
    e.alive = false;
    // Story: a destroyed AVA responder leaves its operator crawling free,
    // unarmed and surrendered. Killing them later costs SIGNAL; sparing them
    // is rewarded at mission end.
    if (e.isResponder) {
        Bystander op; op.kind = Bystander::Kind::Operator;
        op.pos = e.pos; op.hp = op.maxHp = 16; op.radius = 18;
        bystanders_.push_back(op);
    }
    // Central blood decal (large)
    BloodDecal bd;
    bd.pos = e.pos;
    bd.rotation = (float)(rand() % 360) * (float)M_PI / 180.0f;
    bd.scale = 1.2f + (float)(rand() % 40) / 100.0f;
    bd.type = DecalType::Blood;
    blood_.push_back(bd);
    stampTileBlood(bd.pos, 0.45f * bd.scale);

    // Bloody explosion - big burst of red particles
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
    // Extra blood splatter decals - varied sizes, spread out
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
        stampTileBlood(extra.pos, 0.20f * extra.scale);
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

    if (sfxEnemyExplode_) playSFX(sfxEnemyExplode_, config_.sfxVolume * 3 / 4);

    // Brief red flash
    screenFlashTimer_ = 0.05f;
    screenFlashR_ = 180; screenFlashG_ = 30; screenFlashB_ = 30;

    // Player kill registration (only for locally-originated kills)
    if (trackKill) {
        player_.killCounter++;
        if (upgrades_.hasScavenger && player_.ammo < player_.maxAmmo)
            player_.ammo = std::min(player_.maxAmmo, player_.ammo + upgrades_.hasScavenger);
        if (upgrades_.hasVampire)
            player_.hp = std::min(player_.maxHp, player_.hp + upgrades_.hasVampire * 10);
        // Kill shaves 1s off parry cooldown
        if (!player_.canParry)
            player_.parryCdTimer = std::max(0.0f, player_.parryCdTimer - 1.0f);
        if (player_.killCounter >= upgrades_.killsPerBomb) {
            player_.killCounter = 0;
            player_.bombCount = std::min(MAX_BOMBS, player_.bombCount + 1);
        }
    }
    // A boss going down is a moment - hold a longer freeze-frame.
    if (isBossType(e.type)) hitStopTimer_ = std::max(hitStopTimer_, 0.13f);
    // Drop 3 upgrade pickups when a boss is killed (not in sandbox)
    if (isBossType(e.type) && !sandboxMode_) {
        for (int i = 0; i < 3; i++) {
            Pickup pu;
            float angle = (float)i / 3.0f * 2.0f * (float)M_PI;
            pu.pos = {e.pos.x + cosf(angle) * 50.0f, e.pos.y + sinf(angle) * 50.0f};
            pu.type = rollRandomUpgrade(upgrades_, waveNumber_);
            pickups_.push_back(pu);
        }
    }

    // Local co-op kills tracking: credit the slot that owns this kill
    // (During resolveCollisions, player_ = slot 0; use ownerId if available)
    if (state_ == GameState::LocalCoopGame || state_ == GameState::LocalCoopPaused) {
        // The kill is credited to slot 0 via player_ above; coopSlots_[0].kills
        // will be synced after resolveCollisions. Nothing else needed here - the
        // per-slot kill count is incremented in resolveCollisions via b.ownerId.
    }
}

void Game::playerParry() {
    Player& p = player_;
    p.canParry = false;
    p.isParrying = true;
    p.parryTimer = PARRY_WINDOW * (upgrades_.hasReactiveParry ? (1.0f + (float)upgrades_.hasReactiveParry) : 1.0f);
    p.parryDashTimer = PARRY_DASH_DURATION;
    p.parryCdTimer = std::max(1.0f, PARRY_COOLDOWN - upgrades_.parryCdReduction);
    rumble(0.16f, 38, 0.32f, 1.20f);  // Softer parry dash startup

    // Dash direction = aim direction
    if (aimInput_.lengthSq() > 0.04f)
        p.parryDir = aimInput_.normalized();
    else
        p.parryDir = Vec2::fromAngle(p.rotation);

    if (sfxSwoosh_) playSFX(sfxSwoosh_, config_.sfxVolume);
}

void Game::destroyBox(int tx, int ty) {
    // Replace the box tile with grass
    map_.set(tx, ty, TILE_GRASS);
    invalidateMinimapCache();
    // Spawn wood-colored fragment particles - explosive burst
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
    if (sfxBreak_) playSFX(sfxBreak_, config_.sfxVolume);
}

void Game::spawnPlayerDeathEffect(Vec2 pos) {
    auto& net = NetworkManager::instance();
    const bool isOnline = net.isOnline();

    // Camera shake - big in singleplayer, moderate in multiplayer
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
    // Scorch marks - many in singleplayer, few in multiplayer
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

void Game::stampTileBlood(Vec2 pos, float strength) {
    int total = map_.width * map_.height;
    if (total <= 0) return;
    if ((int)tileBlood_.size() != total)
        tileBlood_.assign((size_t)total, 0.0f);
    int tx = TileMap::toTile(pos.x);
    int ty = TileMap::toTile(pos.y);
    if (tx >= 0 && tx < map_.width && ty >= 0 && ty < map_.height)
        tileBlood_[ty * map_.width + tx] = std::min(1.0f, tileBlood_[ty * map_.width + tx] + strength);
}

void Game::updateBloodDecals(float dt) {
    for (auto& bd : blood_) {
        if (bd.type != DecalType::Blood) continue;
        bd.age += dt;
        if (bd.age > 12.0f)
            bd.alpha = std::max(0.0f, bd.alpha - dt / 20.0f);
    }
    blood_.erase(std::remove_if(blood_.begin(), blood_.end(), [](const BloodDecal& bd) {
        return bd.type == DecalType::Blood && bd.alpha <= 0.0f;
    }), blood_.end());
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

// Vehicles

void Game::updateVehicles(float dt) {
    for (int vi = 0; vi < (int)vehicles_.size(); vi++) {
        auto& v = vehicles_[vi];
        if (!v.alive) continue;

        const bool occupied = inVehicle_ && vehicleIdx_ == vi;

        // Input
        float gasInput   = 0.0f;
        float steerInput = 0.0f;
        if (occupied) {
            gasInput   = -moveInput_.y;
            steerInput =  moveInput_.x;
        }

        // Constant-rate steering: heading rotates at a fixed angular velocity.
        // Velocity alignment via grip (below) creates the actual turning arc.
        v.rotation += steerInput * Vehicle::STEER_RATE * dt;

        Vec2  fwdDir = {cosf(v.rotation), sinf(v.rotation)};
        Vec2  latDir = {-fwdDir.y, fwdDir.x};
        float fwdSpd = fwdDir.dot(v.vel);
        float speed  = v.vel.length();

        // Thrust / brake / reverse
        if (gasInput > 0.0f) {
            if (fwdSpd < Vehicle::MAX_FWD_SPD) {
                float push = fminf(Vehicle::ACCEL * gasInput * dt,
                                   Vehicle::MAX_FWD_SPD - fwdSpd);
                v.vel += fwdDir * push;
            }
        } else if (gasInput < 0.0f) {
            if (fwdSpd > 0.5f) {
                // Brake while rolling forward
                float removed = fminf(fwdSpd, Vehicle::BRAKE * (-gasInput) * dt);
                v.vel -= fwdDir * removed;
            } else if (fwdSpd > -Vehicle::MAX_REV_SPD) {
                // Reverse (slower to accelerate than forward)
                float push = fminf(Vehicle::REV_ACCEL * (-gasInput) * dt,
                                   Vehicle::MAX_REV_SPD + fwdSpd);
                v.vel -= fwdDir * push;
            }
        }

        // Coast drag - constant decel on total speed
        speed = v.vel.length();
        if (speed > 0.0f) {
            float drag = fminf(speed, Vehicle::COAST_DRAG * dt);
            v.vel = v.vel * ((speed - drag) / speed);
        }

        // Lateral grip - bleed lateral velocity each frame (lower GRIP = more drift)
        fwdDir = {cosf(v.rotation), sinf(v.rotation)};
        latDir = {-fwdDir.y, fwdDir.x};
        float latSpd = latDir.dot(v.vel);
        v.vel -= latDir * (latSpd * fminf(1.0f, Vehicle::GRIP * dt));

        // Move
        v.pos += v.vel * dt;

        // OBB extents (sprite is rendered at scale 1.5× with facing-up convention)
        float halfL = (v.spriteH > 0 ? v.spriteH : v.size) * 0.75f;
        float halfW = (v.spriteW > 0 ? v.spriteW : v.size) * 0.42f;
        fwdDir = {cosf(v.rotation), sinf(v.rotation)};
        latDir = {-fwdDir.y, fwdDir.x};

        // Wall tile collision: 8 OBB probe points (4 corners + 4 edge midpoints)
        {
            auto pushOut = [&](Vec2 pt) {
                if (!map_.worldCollides(pt.x, pt.y, 4.0f)) return;
                Vec2 toCenter = v.pos - pt;
                float len = toCenter.length();
                if (len < 0.1f) { v.pos += Vec2{4.0f, 0.0f}; return; }
                Vec2 pushDir = toCenter * (1.0f / len);
                float velInto = -(v.vel.dot(pushDir));
                if (velInto > 0.0f) v.vel += pushDir * velInto;
                v.pos += pushDir * 6.0f;
            };
            for (int pass = 0; pass < 2; pass++) {
                pushOut(v.pos + fwdDir * halfL + latDir * halfW);
                pushOut(v.pos + fwdDir * halfL - latDir * halfW);
                pushOut(v.pos - fwdDir * halfL + latDir * halfW);
                pushOut(v.pos - fwdDir * halfL - latDir * halfW);
                pushOut(v.pos + fwdDir * halfL);
                pushOut(v.pos - fwdDir * halfL);
                pushOut(v.pos + latDir * halfW);
                pushOut(v.pos - latDir * halfW);
            }
        }

        // Collision zone pushout: probe 4 OBB corners against trigger rects
        {
            const Vec2 corners[4] = {
                v.pos + fwdDir * halfL + latDir * halfW,
                v.pos + fwdDir * halfL - latDir * halfW,
                v.pos - fwdDir * halfL + latDir * halfW,
                v.pos - fwdDir * halfL - latDir * halfW,
            };
            for (auto corner : corners) {
                Vec2 adjusted = corner;
                pushOutCollisionZones(adjusted, 4.0f);
                v.pos.x += adjusted.x - corner.x;
                v.pos.y += adjusted.y - corner.y;
            }
        }

        // Map boundary: keep the farthest OBB corner inside the world
        {
            float extent = fmaxf(halfL, halfW);
            v.pos.x = fmaxf(extent, fminf(map_.worldWidth()  - extent, v.pos.x));
            v.pos.y = fmaxf(extent, fminf(map_.worldHeight() - extent, v.pos.y));
        }

        // Enemy run-over: OBB test instead of circle
        speed = v.vel.length();
        if (speed >= Vehicle::RUNOVER_SPD) {
            for (auto& e : enemies_) {
                if (!e.alive) continue;
                Vec2  delta    = e.pos - v.pos;
                float alongFwd = fabsf(delta.dot(fwdDir));
                float alongLat = fabsf(delta.dot(latDir));
                float margin   = e.size * 0.5f;
                if (alongFwd < halfL + margin && alongLat < halfW + margin) {
                    e.hp -= Vehicle::RUNOVER_DMG;
                    e.damageFlash = 1.0f;
                    e.vel += v.vel.normalized() * 320.0f;
                    if (e.hp <= 0) killEnemy(e);
                }
            }
        }
    }

    // Dead vehicle eviction
    for (auto& v : vehicles_) {
        if (!v.alive && v.occupantSlot == 0) {
            inVehicle_  = false;
            vehicleIdx_ = -1;
        }
    }
}

// Rendering

