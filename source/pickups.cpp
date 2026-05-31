// ─── pickups.cpp ─── Crate and pickup logic
#include "game.h"
#include "game_internal.h"

// ═════════════════════════════════════════════════════════════════════════════
//  Pickup / Crate Update & Render
// ═════════════════════════════════════════════════════════════════════════════

void Game::updateCrates(float dt) {
    // Auto-spawn crates on a timer (every ~20-30 seconds)
    // In multiplayer, only the host spawns crates - clients receive them via network
    auto& net = NetworkManager::instance();
    bool isMultiplayer = net.isOnline();
    bool isSimDelegate = net.isConnectedToDedicated() && net.isLobbyHost(); // lobby-host on dedicated server
    bool shouldSpawn = !sandboxMode_ && (!isMultiplayer || net.isHost() || isSimDelegate)
        && !(playingCustomMap_ && customMap_.playerConfig.enabled && !customMap_.playerConfig.hasPickups);

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
                    // Crate destroyed - spawn pickup
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
                    if (sfxBreak_) playSFX(sfxBreak_, config_.sfxVolume);
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

        // Player collection - walk over it (spectators can't collect)
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
    crate.contents = rollRandomUpgrade(upgrades_, waveNumber_);
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

