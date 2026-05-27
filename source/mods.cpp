// ─── mods.cpp ─── Mod loading and content overrides
#include "game.h"
#include "game_internal.h"

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
        state_ == GameState::PackSelect) {
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

