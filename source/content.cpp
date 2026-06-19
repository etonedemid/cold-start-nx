#include "game.h"
#include "game_internal.h"


void Game::scanCharacters() {
    for (auto& cd : availableChars_) cd.unload();
    availableChars_.clear();

    // Scan standard character directories
    const char* dirs[] = {"characters", "romfs/characters", "romfs:/characters", "fs:/vol/content/characters"};
    for (const char* dir : dirs) {
        auto found = ::scanCharacters(dir, renderer_);
        for (auto& cd : found)
            availableChars_.push_back(std::move(cd));
    }

    // Also scan mod character directories
    const auto& mods = ModManager::instance().mods();
    for (const auto& mod : mods) {
        if (!mod.enabled) continue;
        std::string modCharDir = "mods/" + mod.id + "/characters";
        auto found = ::scanCharacters(modCharDir, renderer_);
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

    // Store active character for stat application
    activeCharDef_ = CharacterDef{};
    activeCharDef_.folder = cd.folder;
    activeCharDef_.name = cd.name;
    activeCharDef_.speed = cd.speed;
    activeCharDef_.hp = cd.hp;
    activeCharDef_.ammo = cd.ammo;
    activeCharDef_.fireRate = cd.fireRate;
    activeCharDef_.reloadTime = cd.reloadTime;
    activeCharDef_.shootOffsetX = cd.shootOffsetX;
    activeCharDef_.shootOffsetY = cd.shootOffsetY;
    hasActiveChar_ = true;
    printf("Applied character: %s (spd:%.0f hp:%d ammo:%d)\n",
           cd.name.c_str(), cd.speed, cd.hp, cd.ammo);
}

void Game::applyCharacterStatsToPlayer(Player& p) {
    if (!hasActiveChar_) return;
    p.speed      = activeCharDef_.speed;
    p.hp         = activeCharDef_.hp;
    p.maxHp      = activeCharDef_.hp;
    p.ammo       = activeCharDef_.ammo;
    p.maxAmmo    = activeCharDef_.ammo;
    p.fireRate   = activeCharDef_.fireRate;
    p.reloadTime = activeCharDef_.reloadTime;
}

void Game::resetToDefaultCharacter() {
    hasActiveChar_ = false;
    selectedChar_ = -1;
    activeCharDef_ = CharacterDef{};
    playerSprites_ = defaultPlayerSprites_;
    legSprites_ = defaultLegSprites_;
    playerDeathSprites_ = defaultPlayerDeathSprites_;
}

void Game::clearSyncedCharacter(uint8_t playerId) {
    auto it = syncedCharacters_.find(playerId);
    if (it == syncedCharacters_.end()) return;
    if (it->second.visualLoaded) {
        it->second.visual.unload();
        it->second.visualLoaded = false;
    }
    syncedCharacters_.erase(it);
}

void Game::clearSyncedCharacters() {
    for (auto& entry : syncedCharacters_) {
        if (entry.second.visualLoaded) entry.second.visual.unload();
    }
    syncedCharacters_.clear();
    lastCharacterSyncKey_.clear();
}

std::vector<uint8_t> Game::buildCharacterSyncBundle(const CharacterDef& cd) const {
    std::vector<uint8_t> bundle;
    if (cd.folder.empty()) return bundle;

    DIR* dir = opendir(cd.folder.c_str());
    if (!dir) return bundle;

    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (!isAllowedSyncedCharacterFile(name)) continue;

        std::string path = cd.folder;
        if (!path.empty() && path.back() != '/') path += '/';
        path += name;

        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) continue;

        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        std::vector<uint8_t> bytes((size_t)st.st_size);
        size_t got = bytes.empty() ? 0 : fread(bytes.data(), 1, bytes.size(), f);
        fclose(f);
        if (got != bytes.size()) continue;
        files.push_back({name, std::move(bytes)});
    }
    closedir(dir);

    if (files.empty()) return {};
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    auto appendU16 = [&](uint16_t value) {
        bundle.push_back((uint8_t)(value & 0xFF));
        bundle.push_back((uint8_t)((value >> 8) & 0xFF));
    };
    auto appendU32 = [&](uint32_t value) {
        bundle.push_back((uint8_t)(value & 0xFF));
        bundle.push_back((uint8_t)((value >> 8) & 0xFF));
        bundle.push_back((uint8_t)((value >> 16) & 0xFF));
        bundle.push_back((uint8_t)((value >> 24) & 0xFF));
    };

    bundle.push_back('C');
    bundle.push_back('S');
    bundle.push_back('C');
    bundle.push_back('B');
    appendU16((uint16_t)files.size());
    for (const auto& file : files) {
        appendU16((uint16_t)file.first.size());
        appendU32((uint32_t)file.second.size());
        bundle.insert(bundle.end(), file.first.begin(), file.first.end());
        bundle.insert(bundle.end(), file.second.begin(), file.second.end());
    }
    return bundle;
}

bool Game::installSyncedCharacterVisual(uint8_t playerId, const std::string& characterName,
                                        const std::vector<uint8_t>& data) {
    if (data.size() < 6) return false;
    if (!(data[0] == 'C' && data[1] == 'S' && data[2] == 'C' && data[3] == 'B')) return false;

    auto readU16 = [&](size_t off) -> uint16_t {
        return (uint16_t)data[off] | ((uint16_t)data[off + 1] << 8);
    };
    auto readU32 = [&](size_t off) -> uint32_t {
        return (uint32_t)data[off] |
               ((uint32_t)data[off + 1] << 8) |
               ((uint32_t)data[off + 2] << 16) |
               ((uint32_t)data[off + 3] << 24);
    };

    size_t offset = 4;
    uint16_t fileCount = readU16(offset);
    offset += 2;
    if (fileCount == 0) return false;

    mkdir("cache", 0755);
    mkdir("cache/remote_chars", 0755);
    std::string cacheFolder = "cache/remote_chars/p" + std::to_string((int)playerId) + "_" + sanitizeNetCharacterName(characterName);
    mkdir(cacheFolder.c_str(), 0755);

    DIR* dir = opendir(cacheFolder.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (name == "." || name == "..") continue;
            std::string path = cacheFolder + "/" + name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) remove(path.c_str());
        }
        closedir(dir);
    }

    for (uint16_t i = 0; i < fileCount; i++) {
        if (offset + 6 > data.size()) return false;
        uint16_t nameLen = readU16(offset);
        offset += 2;
        uint32_t fileSize = readU32(offset);
        offset += 4;
        if (nameLen == 0 || offset + nameLen > data.size()) return false;

        std::string fileName((const char*)data.data() + offset, nameLen);
        offset += nameLen;
        if (!isAllowedSyncedCharacterFile(fileName) || offset + fileSize > data.size()) return false;

        std::string outPath = cacheFolder + "/" + fileName;
        FILE* out = fopen(outPath.c_str(), "wb");
        if (!out) return false;
        if (fileSize > 0) fwrite(data.data() + offset, 1, fileSize, out);
        fclose(out);
        offset += fileSize;
    }

    CharacterDef loaded;
    if (!loaded.loadFromFolder(cacheFolder, renderer_)) return false;
    loaded.name = characterName;

    auto& synced = syncedCharacters_[playerId];
    if (synced.visualLoaded) synced.visual.unload();
    synced.visual = std::move(loaded);
    synced.visualLoaded = true;
    synced.cacheFolder = cacheFolder;
    return true;
}

void Game::syncLocalCharacterSelection(bool force) {
    auto& net = NetworkManager::instance();
    if (!net.isOnline() || net.state() == NetState::Connecting) return;

    bool isDefault = true;
    std::string characterName = "Default";
    std::vector<uint8_t> bundle;

    if (selectedChar_ >= 0 && selectedChar_ < (int)availableChars_.size()) {
        const CharacterDef& cd = availableChars_[selectedChar_];
        isDefault = false;
        characterName = cd.name;
        bundle = buildCharacterSyncBundle(cd);
        if (bundle.empty()) {
            printf("[NET] Character sync bundle empty for %s, using default instead\n", cd.name.c_str());
            isDefault = true;
            characterName = "Default";
        }
    }

    std::string key = std::string(isDefault ? "D:" : "C:") + characterName + ":" + std::to_string(bundle.size());
    if (!force && key == lastCharacterSyncKey_) return;

    net.sendLocalCharacterSync(characterName, isDefault, bundle);
    lastCharacterSyncKey_ = key;
}

void Game::renderMapSelectMenu() {
    ui_.drawDesktop();

    const int padX   = 14;
    const int btnH   = 26;
    const int btnGap = 4;
    const int winW   = 500;
    const int winX   = (SCREEN_W - winW) / 2;
    const int winY   = 60;
    const int winH   = SCREEN_H - winY - 60;
    ui_.drawWin98Window(winX, winY, winW, winH, "Select Map");

    // Reserve space for: separator + mode row + play+back row + padding
    const int bottomH = 6 + btnH + btnGap + btnH + 10;
    int baseY = winY + UI::W98::TitleH + 10;
    int listH = winH - UI::W98::TitleH - 20 - bottomH;
    int maxVisible = std::max(3, listH / (btnH + btnGap));

    if (mapFiles_.empty()) {
        ui_.drawText("No .csm maps found in maps/ folder",
                     winX + padX, baseY + 10, 14, UI::W98::Black);
        ui_.drawText("Use the Editor to create maps!",
                     winX + padX, baseY + 30, 12, UI::W98::Shadow);
    } else {
        int scrollOff = std::max(0, mapSelectIdx_ - maxVisible + 1);
        for (int i = scrollOff; i < (int)mapFiles_.size() && (i - scrollOff) < maxVisible; i++) {
            int y = baseY + (i - scrollOff) * (btnH + btnGap);
            bool sel = (mapSelectIdx_ == i);
            std::string fname = mapFiles_[i];
            size_t slash = fname.find_last_of('/');
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            int animIdx = i - scrollOff;
            // Click selects; Play button below launches
            if (ui_.win98Button(animIdx, fname.c_str(), winX + padX, y, winW - padX * 2, btnH, sel)) {
                mapSelectIdx_ = i; menuSelection_ = i;
            }
            if (ui_.hoveredItem == animIdx && !usingGamepad_) { mapSelectIdx_ = i; menuSelection_ = i; }
        }
        if ((int)mapFiles_.size() > maxVisible) {
            float ratio = (float)maxVisible / mapFiles_.size();
            float scrollRatio = mapFiles_.size() > 1
                ? (float)scrollOff / (mapFiles_.size() - maxVisible) : 0;
            int trackH = maxVisible * (btnH + btnGap);
            int barH   = std::max(16, (int)(trackH * ratio));
            int barY   = baseY + (int)((trackH - barH) * scrollRatio);
            SDL_SetRenderDrawColor(renderer_, 128, 128, 128, 255);
            SDL_Rect sb = {winX + winW - padX - 4, barY, 4, barH};
            SDL_RenderFillRect(renderer_, &sb);
        }
    }

    // ── Bottom controls ──
    const int innerW  = winW - padX * 2;
    const int halfW   = (innerW - btnGap) / 2;
    int       botY    = winY + winH - bottomH;

    ui_.drawWin98Bevel(winX + padX, botY - 2, innerW, 2, false);
    botY += 6;

    // Arena / Sandbox mode toggle
    if (ui_.win98Button(63, "Arena",   winX + padX,                     botY, halfW, btnH, mapSelectMode_ == 0))
        mapSelectMode_ = 0;
    if (ui_.win98Button(64, "Sandbox", winX + padX + halfW + btnGap,    botY, halfW, btnH, mapSelectMode_ == 1))
        mapSelectMode_ = 1;
    botY += btnH + btnGap;

    // Play (launches selected map) + Back
    const int playW = (innerW - btnGap) * 2 / 3;
    const int backW = innerW - playW - btnGap;
    bool hasMap = !mapFiles_.empty() && mapSelectIdx_ < (int)mapFiles_.size();
    if (ui_.win98Button(60, "Play \xbb", winX + padX, botY, playW, btnH, false) && hasMap)
        confirmInput_ = true;
    if (ui_.win98Button(62, "Back", winX + padX + playW + btnGap, botY, backW, btnH, false))
        backInput_ = true;

    char hint[64];
    snprintf(hint, sizeof(hint), "Select a map  |  Mode: %s",
             mapSelectMode_ == 0 ? "Arena" : "Sandbox");
    ui_.drawWin98StatusBar(SCREEN_H - 26, hint);
}

void Game::renderMapConfigMenu() {
    // No longer shown - game mode is defined in the editor and applied automatically.
}

void Game::renderCharSelectMenu() {
    ui_.drawDesktop();

    // Left window: character list
    SDL_Rect listPanel = {90, 60, 440, SCREEN_H - 120};
    ui_.drawWin98Window(listPanel.x, listPanel.y, listPanel.w, listPanel.h, "Select Character");

    // Right window: preview
    SDL_Rect detailPanel = {560, 60, SCREEN_W - 580, SCREEN_H - 120};
    ui_.drawWin98Window(detailPanel.x, detailPanel.y, detailPanel.w, detailPanel.h, "Preview");

    // (color aliases removed - lambda now uses UI::W98 constants)

    auto drawWalkPreview = [&](const CharacterDef* cd, bool useDefault) {
        const std::vector<SDL_Texture*>* bodyFrames = nullptr;
        const std::vector<SDL_Texture*>* legFrames = nullptr;
        const char* previewName = "Default";
        int hp = config_.playerMaxHp;
        float speed = 520.0f;
        int ammo = 10;

        if (!useDefault && cd) {
            bodyFrames = &cd->bodySprites;
            legFrames = &cd->legSprites;
            previewName = cd->name.c_str();
            hp = cd->hp;
            speed = cd->speed;
            ammo = cd->ammo;
        } else {
            bodyFrames = &defaultPlayerSprites_;
            legFrames = &defaultLegSprites_;
        }

        drawText(previewName, detailPanel.x + 14, detailPanel.y + UI::W98::TitleH + 8, 14, UI::W98::Black);

        SDL_Rect previewBox = {detailPanel.x + 14, detailPanel.y + UI::W98::TitleH + 30, detailPanel.w - 28, 280};
        ui_.drawWin98Bevel(previewBox.x, previewBox.y, previewBox.w, previewBox.h, false);

        SDL_Rect previewGrid = {previewBox.x + 18, previewBox.y + 18, previewBox.w - 36, previewBox.h - 36};
        SDL_SetRenderDrawColor(renderer_, 16, 20, 32, 255);
        SDL_RenderFillRect(renderer_, &previewGrid);
        SDL_SetRenderDrawColor(renderer_, 28, 34, 48, 255);
        for (int gx = 0; gx <= previewGrid.w; gx += 36)
            SDL_RenderDrawLine(renderer_, previewGrid.x + gx, previewGrid.y, previewGrid.x + gx, previewGrid.y + previewGrid.h);
        for (int gy = 0; gy <= previewGrid.h; gy += 36)
            SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + gy, previewGrid.x + previewGrid.w, previewGrid.y + gy);
        SDL_SetRenderDrawColor(renderer_, 0, 100, 95, 110);
        SDL_RenderDrawLine(renderer_, previewGrid.x + previewGrid.w / 2, previewGrid.y, previewGrid.x + previewGrid.w / 2, previewGrid.y + previewGrid.h);
        SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + previewGrid.h / 2, previewGrid.x + previewGrid.w, previewGrid.y + previewGrid.h / 2);

        SDL_Texture* bodyTex = nullptr;
        SDL_Texture* legTex = nullptr;
        if (bodyFrames && !bodyFrames->empty()) {
            int idx = ((int)(gameTime_ * 8.0f)) % std::max(1, (int)bodyFrames->size());
            bodyTex = (*bodyFrames)[idx];
        }
        if (legFrames && !legFrames->empty()) {
            int idx = ((int)(gameTime_ * 10.0f)) % std::max(1, (int)legFrames->size());
            legTex = (*legFrames)[idx];
        }

        auto drawCenteredTex = [&](SDL_Texture* tex, float scaleMul, int yOffset) {
            if (!tex) return;
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            float scale = std::min((float)previewGrid.w / (float)std::max(1, tw),
                                   (float)previewGrid.h / (float)std::max(1, th)) * scaleMul;
            SDL_Rect dst;
            dst.w = (int)(tw * scale);
            dst.h = (int)(th * scale);
            dst.x = previewGrid.x + (previewGrid.w - dst.w) / 2;
            dst.y = previewGrid.y + (previewGrid.h - dst.h) / 2 + yOffset;
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        };

        drawCenteredTex(legTex, 0.88f, 12);
        drawCenteredTex(bodyTex, 0.88f, -8);

        char stats[128];
        snprintf(stats, sizeof(stats), "HP:%d   SPD:%.0f   AMMO:%d", hp, speed, ammo);
    };

    const int padX = 14;
    const int btnH = 26;
    const int btnGap = 4;
    int baseY = listPanel.y + UI::W98::TitleH + 10;
    int bx = listPanel.x + padX;
    int bw = listPanel.w - padX * 2;

    if (menuSelection_ == 0) {
        drawWalkPreview(nullptr, true);
    }

    // Default option
    {
        bool sel = (menuSelection_ == 0);
        if (ui_.win98Button(0, "Default", bx, baseY, bw, btnH, sel)) {
            menuSelection_ = 0;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == 0 && !usingGamepad_) menuSelection_ = 0;
    }

    for (int i = 0; i < (int)availableChars_.size(); i++) {
        int y = baseY + (i + 1) * (btnH + btnGap);
        bool sel = (menuSelection_ == i + 1);
        int animIdx = i + 1;
        if (ui_.win98Button(animIdx, availableChars_[i].name.c_str(), bx, y, bw, btnH, sel)) {
            menuSelection_ = i + 1;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == animIdx && !usingGamepad_) menuSelection_ = i + 1;

        if (sel) {
            drawWalkPreview(&availableChars_[i], false);
        }
    }

    int backIdx = (int)availableChars_.size() + 1;
    bool backSel = (menuSelection_ == backIdx);
    // BACK pinned near bottom of list panel
    int backY = listPanel.y + listPanel.h - btnH - 10;
    ui_.drawWin98Bevel(bx, backY - 6, bw, 2, false);
    if (ui_.win98Button(63, "Back", bx, backY, bw, btnH, backSel)) {
        menuSelection_ = backIdx;
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 63 && !usingGamepad_) menuSelection_ = backIdx;

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Choose a character");
}

void Game::renderCustomWinScreen() {
    ui_.drawDarkOverlay(160);

    const int padX = 14;
    const int btnH = 26;
    const int winW = 340;
    const int winH = UI::W98::TitleH + 14 + 20 + 14 + 2 + 14 + btnH + 14;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Status update");

    char timeStr[64];
    int mins = (int)gameTime_ / 60;
    int secs = (int)gameTime_ % 60;
    snprintf(timeStr, sizeof(timeStr), "Time: %d:%02d", mins, secs);

    int cy = winY + UI::W98::TitleH + 14;
    ui_.drawTextCentered(timeStr, cy, 16, UI::W98::Black);
    cy += 20;
    ui_.drawWin98Bevel(winX + padX, cy, winW - padX * 2, 2, false);
    cy += 14;

    if (ui_.win98Button(0, "Continue", winX + padX, cy, winW - padX * 2, btnH, true)) {
        confirmInput_ = true;
    }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Level complete - press Continue");
}

// Character Creator

void Game::renderCharCreator() {
    auto& cc = charCreator_;
    bool modalOpen = modSaveDialog_.isOpen();
    char buf[256];

    // Only let hovering change the selected field when the mouse actually moves.
    // A parked cursor must not override keyboard/gamepad navigation every frame
    // (that desync made the highlighted row differ from the row the arrow keys
    // adjusted, which felt broken).
    static int csLastMouseX = -99999, csLastMouseY = -99999;
    bool mouseMoved = (ui_.mouseX != csLastMouseX || ui_.mouseY != csLastMouseY);
    csLastMouseX = ui_.mouseX;
    csLastMouseY = ui_.mouseY;

    // Win98 desktop background
    ui_.drawDesktop();

    // Layout constants
    // Left window: "Character Creator" - stats, buttons
    const int leftWinX = 20,  leftWinY = 20;
    const int leftWinW = 440, leftWinH = 660;
    // Right window: "Preview" - tabs, canvas, frame controls, info
    const int rightWinX = 472, rightWinY = 20;
    const int rightWinW = 788, rightWinH = 660;

    const char* winTitle = cc.isEditing ? "Character Workbench" : "Character Creator";
    ui_.drawWin98Window(leftWinX,  leftWinY,  leftWinW,  leftWinH,  winTitle);
    ui_.drawWin98Window(rightWinX, rightWinY, rightWinW, rightWinH, "Preview");

    // LEFT WINDOW content
    const int lPad  = 12;
    const int lCX   = leftWinX + lPad;
    const int lRW   = leftWinW - lPad * 2;
    const int rowH  = 26;
    const int rowGap = 4;
    int contentY = leftWinY + UI::W98::TitleH + 8;

    // Helper: draw a Win98 section separator with a label
    auto drawSection = [&](const char* label) {
        ui_.drawText(label, lCX, contentY, 11, UI::W98::Shadow);
        int lineY = contentY + 10;
        SDL_SetRenderDrawColor(renderer_, 128, 128, 128, 255);
        SDL_RenderDrawLine(renderer_, lCX + ui_.textWidth(label, 11) + 4, lineY,
                           lCX + lRW, lineY);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer_, lCX + ui_.textWidth(label, 11) + 4, lineY + 1,
                           lCX + lRW, lineY + 1);
        contentY += 16;
    };

    // Helper: compact stat row - label (80px) | editable value field (rest)
    auto drawStatRow = [&](int idx, const char* label, const char* value, bool adjustable) {
        bool selected = (cc.field == idx);
        bool hovered  = ui_.pointInRect(ui_.mouseX, ui_.mouseY, lCX, contentY, lRW, rowH);

        const int lblW = 80;

        // Background fill for whole row
        SDL_Color rowBg = selected ? UI::W98::Navy
                        : (hovered ? SDL_Color{210, 210, 210, 255} : UI::W98::Silver);
        ui_.drawWin98Bevel(lCX, contentY, lRW, rowH, false);
        SDL_SetRenderDrawColor(renderer_, rowBg.r, rowBg.g, rowBg.b, rowBg.a);
        SDL_Rect fillR = {lCX + 2, contentY + 2, lRW - 4, rowH - 4};
        SDL_RenderFillRect(renderer_, &fillR);

        SDL_Color labelCol = selected ? UI::W98::White : UI::W98::Black;
        ui_.drawText(label, lCX + 6, contentY + 5, 13, labelCol);

        if (adjustable) {
            int valX = lCX + lblW;
            int valW = lRW - lblW - 4;
            bool editing = (cc.statEditField == idx);
            const char* disp = editing ? cc.statEditBuf.c_str() : value;
            float blink = editing ? (float)fmod(SDL_GetTicks() * 0.001, 1.0) : 0.0f;
            ui_.drawWin98TextField(valX, contentY + 2, valW, rowH - 4, disp, editing, false, blink);

            // Click to start editing
            if (!modalOpen && ui_.mouseClicked &&
                ui_.pointInRect(ui_.mouseX, ui_.mouseY, valX, contentY + 2, valW, rowH - 4)) {
                ui_.mouseClicked = false;
                ui_.clickCooldownFrames = 2;
                if (cc.statEditField != idx) {
                    cc.statEditField = idx;
                    cc.statEditBuf   = value;
                    cc.field = idx; menuSelection_ = idx;
#ifndef __SWITCH__
                    SDL_StartTextInput();
#endif
                }
            }
        } else {
            SDL_Color valueCol = selected ? SDL_Color{180, 210, 255, 255} : UI::W98::Black;
            ui_.drawText(value, lCX + lblW, contentY + 5, 13, valueCol);
        }

        if (!modalOpen && hovered && mouseMoved) { cc.field = idx; menuSelection_ = idx; }
        if (hovered) ui_.hoveredItem = idx;
        if (!modalOpen && hovered && ui_.mouseClicked) {
            cc.field = idx; menuSelection_ = idx;
            if (idx == 0) confirmInput_ = true;
        }
        contentY += rowH + rowGap;
    };

    // SECTION: NAME
    drawSection("NAME");
    if (cc.textEditing) {
        static float ccBlink = 0.0f;
        ccBlink += 0.016f;
        std::string display = cc.textBuf + (((int)(ccBlink * 1.5f) % 2) == 0 ? "_" : " ");
        ui_.drawWin98TextField(lCX, contentY, lRW, rowH, display.c_str(), true);
        contentY += rowH + rowGap;
        renderSoftKB();
        contentY += 150;
    } else {
        drawStatRow(0, "Name", cc.name.c_str(), false);
    }

    // SECTION: STATS
    drawSection("STATS");
    snprintf(buf, sizeof(buf), "%.0f", cc.speed);
    drawStatRow(1, "Speed", buf, true);
    snprintf(buf, sizeof(buf), "%d", cc.hp);
    drawStatRow(2, "HP", buf, true);
    snprintf(buf, sizeof(buf), "%d", cc.ammo);
    drawStatRow(3, "Ammo", buf, true);
    snprintf(buf, sizeof(buf), "%.1f", cc.fireRate);
    drawStatRow(4, "Fire Rate", buf, true);
    snprintf(buf, sizeof(buf), "%.1f", cc.reloadTime);
    drawStatRow(5, "Reload", buf, true);

    // Sprite info inlined below stats
    contentY += 2;
    const int spriteInfoH = 62;
    ui_.drawWin98Bevel(lCX, contentY, lRW, spriteInfoH, false);
    SDL_SetRenderDrawColor(renderer_, 192, 192, 192, 255);
    SDL_Rect siFill = {lCX + 2, contentY + 2, lRW - 4, spriteInfoH - 4};
    SDL_RenderFillRect(renderer_, &siFill);
    ui_.drawText("Sprite Set", lCX + 6, contentY + 4, 12, UI::W98::Shadow);
    if (cc.loaded) {
        snprintf(buf, sizeof(buf), "Body: %d   Legs: %d   Death: %d",
                 (int)cc.charDef.bodySprites.size(),
                 (int)cc.charDef.legSprites.size(),
                 (int)cc.charDef.deathSprites.size());
        SDL_Color sprCol = cc.charDef.bodySprites.empty() ? UI::Color::Red : UI::Color::Green;
        ui_.drawText(buf, lCX + 6, contentY + 22, 11, sprCol);
        SDL_Color detCol = cc.charDef.hasDetail ? UI::Color::Green : UI::W98::Shadow;
        ui_.drawText(cc.charDef.hasDetail ? "Detail sprite loaded" : "No detail sprite",
                     lCX + 6, contentY + 40, 11, detCol);
    } else {
        ui_.drawText("No character folder loaded yet.", lCX + 6, contentY + 22, 11, UI::W98::Shadow);
        ui_.drawText("Sprites from characters/<name>/", lCX + 6, contentY + 40, 11, UI::W98::Shadow);
    }
    contentY += spriteInfoH + 6;

    // SECTION: CHARACTERS
    drawSection("CHARACTERS");
    {
        // 2-per-row grid, min 80px wide, 28px tall
        const int chipW = (lRW - 8) / 2;
        const int chipH = 28;
        const int chipGap = 4;
        int shown = std::min((int)availableChars_.size(), 6);
        if (availableChars_.empty()) {
            ui_.drawText("No saved characters found.", lCX + 4, contentY + 4, 11, UI::W98::Shadow);
            contentY += chipH + chipGap;
        } else {
            for (int i = 0; i < shown; i++) {
                int col = i % 2;
                int row = i / 2;
                int cx2 = lCX + col * (chipW + chipGap);
                int cy2 = contentY + row * (chipH + chipGap);
                if (ui_.win98Button(150 + i, availableChars_[i].name.c_str(),
                                    cx2, cy2, chipW, chipH, false)) {
                    if (!modalOpen) loadCharacterIntoCreator(availableChars_[i].folder);
                }
            }
            int rows = (shown + 1) / 2;
            contentY += rows * (chipH + chipGap) + 2;
        }
    }

    // SECTION: ACTIONS
    drawSection("ACTIONS");
    {
        const int btnH  = 26;
        const int half  = (lRW - 4) / 2;
        const int btnGp = 4;

        // Row 1: Reload | Test Play
        const char* reloadLabel = cc.loaded ? "Reload" : "Load Sprites";
        bool reloadPressed  = ui_.win98Button(6, reloadLabel,  lCX,             contentY, half, btnH, cc.field == 6);
        bool testPressed    = ui_.win98Button(7, "Test Play",  lCX + half + btnGp, contentY, half, btnH, cc.field == 7);
        if (ui_.hoveredItem == 6 && mouseMoved) { cc.field = 6; menuSelection_ = 6; }
        if (ui_.hoveredItem == 7 && mouseMoved) { cc.field = 7; menuSelection_ = 7; }
        contentY += btnH + btnGp;

        // Row 2: Save to Mod | Save Local
        bool modSavePressed  = ui_.win98Button(8, "Save to Mod",  lCX,             contentY, half, btnH, cc.field == 8);
        bool localSavePressed = ui_.win98Button(9, "Save Local",  lCX + half + btnGp, contentY, half, btnH, cc.field == 9);
        if (ui_.hoveredItem == 8 && mouseMoved) { cc.field = 8; menuSelection_ = 8; }
        if (ui_.hoveredItem == 9 && mouseMoved) { cc.field = 9; menuSelection_ = 9; }
        contentY += btnH + btnGp;

        // Row 3: Back (full width)
        bool backPressed = ui_.win98Button(10, "Back", lCX, contentY, lRW, btnH, cc.field == 10);
        if (ui_.hoveredItem == 10 && mouseMoved) { cc.field = 10; menuSelection_ = 10; }

        // win98Button already consumed mouseClicked and returns true only on click
        if (!modalOpen) {
            if (reloadPressed) {
                cc.field = 6;
                if (cc.loaded) {
                    cc.charDef.reloadSprites(renderer_);
                    cc.warnings = cc.charDef.validate();
                    cc.statusMsg = "Sprites reloaded!";
                    cc.statusTimer = 2.0f;
                } else if (!cc.folderPath.empty()) {
                    loadCharacterIntoCreator(cc.folderPath);
                } else {
                    loadCharacterIntoCreator("characters/" + cc.name);
                }
            } else if (testPressed) {
                cc.field = 7;
                testCharacter();
            } else if (modSavePressed) {
                cc.field = 8;
                if (!modSaveDialog_.isOpen()) {
                    openModSaveDialog(ModSaveDialogState::AssetCharacter);
                }
            } else if (localSavePressed) {
                cc.field = 9;
                std::string folder = cc.folderPath.empty() ? "characters/" + cc.name : cc.folderPath;
                saveCharacterToFolder(folder);
                cc.statusMsg = "Saved to " + folder + "/";
                cc.statusTimer = 3.0f;
            } else if (backPressed) {
                cc.field = 10;
                cc.clearPreviews(renderer_);
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
                updateAspectMode();
            }
        }
    }

    // RIGHT WINDOW content
    const int rPad  = 12;
    const int rCX   = rightWinX + rPad;

    // -- Section tabs as win98Buttons (IDLE/BODY/LEGS/DEATH/DETAIL) --
    const char* sections[] = {"IDLE", "BODY", "LEGS", "DEATH", "DETAIL"};
    int tabY = rightWinY + UI::W98::TitleH + 8;
    int tabW = 86, tabH = 24;
    for (int i = 0; i < 5; i++) {
        bool sel = (cc.previewSection == i);
        if (ui_.win98Button(50 + i, sections[i], rCX + i * (tabW + 2), tabY, tabW, tabH, sel)) {
            if (!modalOpen) {
                cc.previewSection = i;
                cc.previewFrame   = 0;
                cc.playAnimation  = false;
            }
        }
    }

    // -- Preview canvas (420x420) in a sunken bevel --
    int previewBevelX = rCX;
    int previewBevelY = tabY + tabH + 6;
    int previewBevelW = 420, previewBevelH = 420;
    ui_.drawWin98Bevel(previewBevelX, previewBevelY, previewBevelW, previewBevelH, false);
    SDL_Rect previewGrid = {previewBevelX + 2, previewBevelY + 2, previewBevelW - 4, previewBevelH - 4};
    // Grid background (keep raw SDL drawing exactly as before)
    SDL_SetRenderDrawColor(renderer_, 18, 22, 34, 255);
    SDL_RenderFillRect(renderer_, &previewGrid);
    SDL_SetRenderDrawColor(renderer_, 32, 38, 50, 255);
    for (int gx = 0; gx <= previewGrid.w; gx += 38)
        SDL_RenderDrawLine(renderer_, previewGrid.x + gx, previewGrid.y,
                           previewGrid.x + gx, previewGrid.y + previewGrid.h);
    for (int gy = 0; gy <= previewGrid.h; gy += 38)
        SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + gy,
                           previewGrid.x + previewGrid.w, previewGrid.y + gy);
    SDL_SetRenderDrawColor(renderer_, 0, 100, 95, 100);
    SDL_RenderDrawLine(renderer_, previewGrid.x + previewGrid.w / 2, previewGrid.y,
                       previewGrid.x + previewGrid.w / 2, previewGrid.y + previewGrid.h);
    SDL_RenderDrawLine(renderer_, previewGrid.x, previewGrid.y + previewGrid.h / 2,
                       previewGrid.x + previewGrid.w, previewGrid.y + previewGrid.h / 2);

    // Sprite selection (unchanged logic)
    SDL_Texture* previewTex = nullptr;
    int frameCount = 1;
    auto& ccd = cc.charDef;
    switch (cc.previewSection) {
        case 0:
            if (cc.loaded && !ccd.bodySprites.empty()) previewTex = ccd.bodySprites[0];
            else if (!playerSprites_.empty()) previewTex = playerSprites_[0];
            break;
        case 1:
            if (cc.loaded && !ccd.bodySprites.empty()) {
                frameCount = (int)ccd.bodySprites.size();
                previewTex = ccd.bodySprites[cc.previewFrame % frameCount];
            } else if (!playerSprites_.empty()) {
                frameCount = (int)playerSprites_.size();
                previewTex = playerSprites_[cc.previewFrame % frameCount];
            }
            break;
        case 2:
            if (cc.loaded && !ccd.legSprites.empty()) {
                frameCount = (int)ccd.legSprites.size();
                previewTex = ccd.legSprites[cc.previewFrame % frameCount];
            } else if (!legSprites_.empty()) {
                frameCount = (int)legSprites_.size();
                previewTex = legSprites_[cc.previewFrame % frameCount];
            }
            break;
        case 3:
            if (cc.loaded && !ccd.deathSprites.empty()) {
                frameCount = (int)ccd.deathSprites.size();
                previewTex = ccd.deathSprites[cc.previewFrame % frameCount];
            } else if (!playerDeathSprites_.empty()) {
                frameCount = (int)playerDeathSprites_.size();
                previewTex = playerDeathSprites_[cc.previewFrame % frameCount];
            }
            break;
        case 4:
            if (cc.loaded && ccd.detailSprite) previewTex = ccd.detailSprite;
            break;
    }

    // Sprite rendering + shoot-point click detection (keep exactly as before)
    SDL_Rect spriteDst = {0, 0, 0, 0};
    float spriteScale = 1.0f;
    int texW = 0, texH = 0;
    if (previewTex) {
        SDL_QueryTexture(previewTex, nullptr, nullptr, &texW, &texH);
        spriteScale = std::min((float)previewGrid.w / texW, (float)previewGrid.h / texH) * 0.78f;
        spriteDst.w = (int)(texW * spriteScale);
        spriteDst.h = (int)(texH * spriteScale);
        spriteDst.x = previewGrid.x + (previewGrid.w - spriteDst.w) / 2;
        spriteDst.y = previewGrid.y + (previewGrid.h - spriteDst.h) / 2;
        SDL_RenderCopy(renderer_, previewTex, nullptr, &spriteDst);

        if (!modalOpen && ui_.mouseClicked &&
            ui_.pointInRect(ui_.mouseX, ui_.mouseY, spriteDst.x, spriteDst.y, spriteDst.w, spriteDst.h)) {
            cc.shootOffsetX = roundf(((float)(ui_.mouseX - spriteDst.x) / spriteScale) - texW * 0.5f);
            cc.shootOffsetY = roundf(((float)(ui_.mouseY - spriteDst.y) / spriteScale) - texH * 0.5f);
            cc.statusMsg  = "Shoot point updated";
            cc.statusTimer = 1.5f;
        }

        int shootX = spriteDst.x + (int)((cc.shootOffsetX + texW * 0.5f) * spriteScale);
        int shootY = spriteDst.y + (int)((cc.shootOffsetY + texH * 0.5f) * spriteScale);
        SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 230);
        SDL_RenderDrawLine(renderer_, shootX - 10, shootY, shootX + 10, shootY);
        SDL_RenderDrawLine(renderer_, shootX, shootY - 10, shootX, shootY + 10);
        SDL_Rect dot = {shootX - 2, shootY - 2, 5, 5};
        SDL_RenderFillRect(renderer_, &dot);
        SDL_SetRenderDrawColor(renderer_, 0, 255, 228, 120);
        SDL_RenderDrawLine(renderer_, previewGrid.x + previewGrid.w / 2,
                           previewGrid.y + previewGrid.h / 2, shootX, shootY);
    } else {
        ui_.drawText("NO SPRITE PREVIEW", previewGrid.x + 80, previewGrid.y + 162, 16, UI::W98::Shadow);
        ui_.drawText("Load a character folder to preview art.",
                     previewGrid.x + 40, previewGrid.y + 190, 12, UI::W98::Shadow);
    }

    // -- Side card (frame control + shoot point info) --
    int scX  = rCX + previewBevelW + 8;
    int scY  = tabY + tabH + 6;
    int scW  = rightWinW - rPad * 2 - previewBevelW - 8;
    int scH  = previewBevelH;
    ui_.drawWin98Bevel(scX, scY, scW, scH, false);
    SDL_SetRenderDrawColor(renderer_, 192, 192, 192, 255);
    SDL_Rect scFill = {scX + 2, scY + 2, scW - 4, scH - 4};
    SDL_RenderFillRect(renderer_, &scFill);

    int sy = scY + 8;
    ui_.drawText("Frame Control", scX + 8, sy, 14, UI::W98::Black);
    sy += 20;
    snprintf(buf, sizeof(buf), "Frame %d / %d",
             frameCount > 0 ? cc.previewFrame + 1 : 1, std::max(1, frameCount));
    ui_.drawText(buf, scX + 8, sy, 13, UI::W98::Black);
    sy += 18;
    ui_.drawText("L/R arrows step frames.", scX + 8, sy, 11, UI::W98::Shadow);
    sy += 18;

    // Frame control buttons: < ANIM ON/OFF >  (win98Buttons)
    int fcBtnH = 24;
    bool prevClicked   = ui_.win98Button(60, "<",
                             scX + 4,        sy, 26,          fcBtnH, false);
    bool toggleClicked = ui_.win98Button(61, cc.playAnimation ? "ANIM ON" : "ANIM OFF",
                             scX + 34,       sy, scW - 64,    fcBtnH, cc.playAnimation);
    bool nextClicked   = ui_.win98Button(62, ">",
                             scX + scW - 28, sy, 26,          fcBtnH, false);
    if (!modalOpen && frameCount > 1) {
        if (prevClicked) { cc.playAnimation = false; cc.previewFrame = (cc.previewFrame + frameCount - 1) % frameCount; }
        if (nextClicked) { cc.playAnimation = false; cc.previewFrame = (cc.previewFrame + 1) % frameCount; }
    }
    if (!modalOpen && toggleClicked) cc.playAnimation = !cc.playAnimation;
    sy += fcBtnH + 10;

    // Thin separator
    SDL_SetRenderDrawColor(renderer_, 128, 128, 128, 255);
    SDL_RenderDrawLine(renderer_, scX + 4, sy, scX + scW - 4, sy);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer_, scX + 4, sy + 1, scX + scW - 4, sy + 1);
    sy += 10;

    ui_.drawText("Shoot Point", scX + 8, sy, 14, UI::W98::Black);
    sy += 20;
    snprintf(buf, sizeof(buf), "X: %.0f px", cc.shootOffsetX);
    ui_.drawText(buf, scX + 8, sy, 13, UI::W98::Black);
    sy += 16;
    snprintf(buf, sizeof(buf), "Y: %.0f px", cc.shootOffsetY);
    ui_.drawText(buf, scX + 8, sy, 13, UI::W98::Black);
    sy += 18;
    ui_.drawText("Click sprite to place", scX + 8, sy, 11, UI::W98::Shadow);
    sy += 14;
    ui_.drawText("bullet spawn point.", scX + 8, sy, 11, UI::W98::Shadow);
    sy += 14;
    ui_.drawText("Neg Y = forward/up.", scX + 8, sy, 11, UI::W98::Shadow);
    sy += 14;
    ui_.drawText("Yellow cross = saved.", scX + 8, sy, 11, UI::W98::Shadow);
    sy += 20;

    SDL_SetRenderDrawColor(renderer_, 128, 128, 128, 255);
    SDL_RenderDrawLine(renderer_, scX + 4, sy, scX + scW - 4, sy);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer_, scX + 4, sy + 1, scX + scW - 4, sy + 1);
    sy += 10;

    ui_.drawText("Output Folder", scX + 8, sy, 13, UI::W98::Black);
    sy += 18;
    std::string folderLabel = cc.folderPath.empty()
        ? ("mods/<choose>/characters/" + cc.name + "/") : cc.folderPath;
    // Wrap long path across two lines if needed
    if ((int)folderLabel.size() > 22) {
        ui_.drawText(folderLabel.substr(0, 22).c_str(), scX + 8, sy, 10, UI::Color::Green);
        ui_.drawText(folderLabel.substr(22).c_str(),    scX + 8, sy + 12, 10, UI::Color::Green);
        sy += 12;
    } else {
        ui_.drawText(folderLabel.c_str(), scX + 8, sy, 10, UI::Color::Green);
    }
    sy += 18;
    ui_.drawText(cc.loaded ? "Editing existing folder" : "SAVE TO MOD exports here",
                 scX + 8, sy, 10, UI::W98::Shadow);

    // -- Warnings / status panel at bottom of right window --
    int warnY  = previewBevelY + previewBevelH + 6;
    int warnH  = rightWinY + rightWinH - UI::W98::TitleH - warnY - 6;
    if (warnH > 20) {
        ui_.drawWin98Bevel(rCX, warnY, rightWinW - rPad * 2, warnH, false);
        SDL_SetRenderDrawColor(renderer_, 192, 192, 192, 255);
        SDL_Rect wFill = {rCX + 2, warnY + 2, rightWinW - rPad * 2 - 4, warnH - 4};
        SDL_RenderFillRect(renderer_, &wFill);
        SDL_Color wHead = cc.warnings.empty() ? UI::Color::Green : UI::Color::Yellow;
        ui_.drawText(cc.warnings.empty() ? "READY" : "WARNINGS", rCX + 8, warnY + 6, 13, wHead);
        if (cc.warnings.empty()) {
            ui_.drawText("Looks good. Save writes stats and shoot point to character.cfg.",
                         rCX + 8, warnY + 24, 11, UI::W98::Shadow);
            ui_.drawText("Tips: Tab=preview set, R=reload sprites, P=toggle animation.",
                         rCX + 8, warnY + 38, 11, UI::W98::Shadow);
        } else {
            int wy = warnY + 24;
            for (size_t i = 0; i < cc.warnings.size() && i < 3; i++) {
                ui_.drawText(cc.warnings[i].c_str(), rCX + 8, wy, 11, UI::Color::Yellow);
                wy += 16;
            }
        }
    }

    // Status message centered at bottom
    if (cc.statusTimer > 0)
        ui_.drawTextCentered(cc.statusMsg.c_str(), SCREEN_H - 44, 15, UI::Color::Green);

    // Win98 status bar at very bottom
    ui_.drawWin98StatusBar(SCREEN_H - 24,
        "Navigate: arrow keys / gamepad   Adjust: < >   Confirm: Enter   Back: Esc");
}

// Mod build folder helper

/*static*/ std::string Game::modBuildFolder(const std::string& modId, const std::string& displayName, const std::string& author) {
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
    if (f) { fclose(f); return base; }  // already exists - don't overwrite
    f = fopen(cfgPath.c_str(), "w");
    if (f) {
        fprintf(f, "[mod]\n");
        fprintf(f, "id=%s\n",          modId.c_str());
        fprintf(f, "name=%s\n",        displayName.c_str());
        fprintf(f, "author=%s\n",     author.c_str());
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

// Character save/load helpers

void Game::saveCharacterToFolder(const std::string& folderPath) {
    auto& cc = charCreator_;

    // Create folder
    mkdir("characters", 0755);
    mkdir(folderPath.c_str(), 0755);

    // Write character.cfg
    std::string cfgPath = folderPath + "/character.cfg";
    FILE* f = fopen(cfgPath.c_str(), "w");
    if (!f) { printf("Failed to save: %s\n", cfgPath.c_str()); return; }
    fprintf(f, "# Character config - sprites are auto-detected from PNGs\n");
    fprintf(f, "name=%s\n\n", cc.name.c_str());
    fprintf(f, "speed=%.0f\n",       cc.speed);
    fprintf(f, "hp=%d\n",            cc.hp);
    fprintf(f, "ammo=%d\n",          cc.ammo);
    fprintf(f, "fire_rate=%.1f\n",   cc.fireRate);
    fprintf(f, "reload_time=%.1f\n", cc.reloadTime);
    fprintf(f, "shoot_x=%.0f\n",     cc.shootOffsetX);
    fprintf(f, "shoot_y=%.0f\n",     cc.shootOffsetY);
    fclose(f);

    auto copyFile = [](const std::string& src, const std::string& dst) {
        FILE* check = fopen(dst.c_str(), "rb");
        if (check) { fclose(check); return; }
        FILE* sf = fopen(src.c_str(), "rb");
        if (!sf) return;
        FILE* df = fopen(dst.c_str(), "wb");
        if (df) {
            char buf[4096]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), sf)) > 0)
                fwrite(buf, 1, n, df);
            fclose(df);
        }
        fclose(sf);
    };

    // Determine sprite source: loaded character folder, or default player templates
    std::string srcFolder = (cc.loaded && !cc.charDef.folder.empty())
        ? cc.charDef.folder   // ends with '/'
        : std::string();

    std::string normDest = folderPath;
    if (!normDest.empty() && normDest.back() != '/') normDest += '/';

    bool srcIsDest = (!srcFolder.empty() && srcFolder == normDest);

    if (!srcIsDest) {
        if (!srcFolder.empty()) {
            // Copy all PNGs from the loaded character folder to the new location
            DIR* sd = opendir(srcFolder.c_str());
            if (sd) {
                struct dirent* ent;
                while ((ent = readdir(sd)) != nullptr) {
                    if (ent->d_name[0] == '.') continue;
                    std::string fn(ent->d_name);
                    if (fn.size() < 5) continue;
                    std::string ext = fn.substr(fn.size() - 4);
                    for (char& c : ext) c = (char)tolower((unsigned char)c);
                    if (ext != ".png") continue;
                    copyFile(srcFolder + fn, normDest + fn);
                }
                closedir(sd);
                printf("Copied sprites from %s to %s\n", srcFolder.c_str(), normDest.c_str());
            }
        } else {
            // No loaded character - copy default player sprite templates
            auto copySprite = [&](const char* srcPat, const char* dstPat, int count) {
                for (int i = 1; i <= count; i++) {
                    char src[256], dst[256];
                    snprintf(src, sizeof(src), srcPat, i);
                    snprintf(dst, sizeof(dst), dstPat, i);
                    const char* srcDirs[] = {"romfs/sprites/player/", "sprites/player/",
                                             "romfs:/sprites/player/", "fs:/vol/content/sprites/player/"};
                    for (const char* dir : srcDirs) {
                        copyFile(std::string(dir) + src, normDest + dst);
                    }
                }
            };
            copySprite("body-%04d.png", "body-%04d.png", 11);
            copySprite("legs-%04d.png", "legs-%04d.png", 8);
            copySprite("death-%d.png",  "death-%d.png",  12);
            printf("Copied default sprite templates to %s\n", normDest.c_str());
        }
    }

    printf("Character saved: %s\n", cfgPath.c_str());

    // If this was the active character, reload it from disk and re-apply.
    // Falls back to default if the saved data turned out to be invalid.
    if (selectedChar_ >= 0) {
        std::string normFolder = folderPath;
        if (!normFolder.empty() && normFolder.back() != '/') normFolder += '/';
        if (selectedChar_ < (int)availableChars_.size() &&
            availableChars_[selectedChar_].folder == normFolder) {
            scanCharacters();
            int newIdx = -1;
            for (int i = 0; i < (int)availableChars_.size(); i++) {
                if (availableChars_[i].folder == normFolder) { newIdx = i; break; }
            }
            if (newIdx >= 0) {
                selectedChar_ = newIdx;
                applyCharacter(availableChars_[newIdx]);
            } else {
                resetToDefaultCharacter();
            }
        }
    }
}

void Game::loadCharacterIntoCreator(const std::string& folderPath) {
    auto& cc = charCreator_;
    cc.charDef.unload();
    cc.charDef = CharacterDef{};

    if (cc.charDef.loadFromFolder(folderPath, renderer_)) {
        cc.loaded = true;
        cc.folderPath = folderPath;
        cc.name = cc.charDef.name;
        cc.speed = cc.charDef.speed;
        cc.hp = cc.charDef.hp;
        cc.ammo = cc.charDef.ammo;
        cc.fireRate = cc.charDef.fireRate;
        cc.reloadTime = cc.charDef.reloadTime;
        cc.shootOffsetX = cc.charDef.shootOffsetX;
        cc.shootOffsetY = cc.charDef.shootOffsetY;
        cc.isEditing = true;
        cc.warnings = cc.charDef.validate();
        cc.previewFrame = 0;
        cc.animTime = 0.0f;
        cc.statusMsg = "Loaded: " + cc.name;
        cc.statusTimer = 2.0f;
        printf("Loaded character into creator: %s\n", cc.name.c_str());
    } else {
        cc.loaded = false;
        cc.statusMsg = "Failed to load from " + folderPath;
        cc.statusTimer = 3.0f;
    }
}

void Game::testCharacter() {
    auto& cc = charCreator_;

    // Save current state first
    std::string folder = cc.folderPath.empty() ? "characters/" + cc.name : cc.folderPath;
    saveCharacterToFolder(folder);

    // Load/reload character
    if (!cc.loaded) {
        loadCharacterIntoCreator(folder);
    }

    // Build a temporary CharacterDef with current stats
    CharacterDef testDef;
    testDef.name = cc.name;
    testDef.speed = cc.speed;
    testDef.hp = cc.hp;
    testDef.ammo = cc.ammo;
    testDef.fireRate = cc.fireRate;
    testDef.reloadTime = cc.reloadTime;
    testDef.shootOffsetX = cc.shootOffsetX;
    testDef.shootOffsetY = cc.shootOffsetY;
    if (cc.loaded) {
        testDef.bodySprites = cc.charDef.bodySprites;
        testDef.legSprites = cc.charDef.legSprites;
        testDef.deathSprites = cc.charDef.deathSprites;
    }
    applyCharacter(testDef);

    // Start sandbox game for testing
    sandboxMode_ = true;
    state_ = GameState::Playing;
    testPlayFromEditor_ = true;  // return to creator when done
    gameTime_ = 0;
    lobbySettings_.isPvp = false;
    waveNumber_ = 0;
    waveEnemiesLeft_ = 0;
    waveActive_ = false;
    wavePauseTimer_ = WAVE_PAUSE_BASE;
    waveSpawnTimer_ = 0;
    enemies_.clear(); bullets_.clear(); enemyBullets_.clear();
    bombs_.clear(); explosions_.clear(); debris_.clear();
    blood_.clear(); tileBlood_.clear(); boxFragments_.clear();
    crates_.clear(); pickups_.clear();
    upgrades_.reset();
    crateSpawnTimer_ = 0;
    map_.generate(config_.mapWidth, config_.mapHeight);
    invalidateMinimapCache();

    player_ = Player{};
    player_.maxHp = cc.hp;
    player_.hp = cc.hp;
    player_.speed = cc.speed;
    player_.ammo = cc.ammo;
    player_.maxAmmo = cc.ammo;
    player_.fireRate = cc.fireRate;
    player_.reloadTime = cc.reloadTime;
    player_.pos = {map_.worldWidth() / 2.0f, map_.worldHeight() / 2.0f};
    player_.bombCount = 1;

    camera_.pos = {player_.pos.x - SCREEN_W/2, player_.pos.y - SCREEN_H/2};
    camera_.worldW = map_.worldWidth();
    camera_.worldH = map_.worldHeight();

    playActionMusic();

    printf("Test playing character: %s\n", cc.name.c_str());
}

void Game::openModSaveDialog(ModSaveDialogState::Asset asset) {
    auto& d = modSaveDialog_;
    d.phase         = ModSaveDialogState::ChooseMod;
    d.asset         = asset;
    d.selIdx        = 0;
    d.confirmed     = false;
    d.newModId.clear();
    d.newModAuthor.clear();
    d.editingAuthor = false;
    d.textEditing   = false;
    d.gpCharIdx     = 0;
    d.catIdx        = 0;
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
    if (softKB_.active) softKB_.close(false);
}

void Game::handleModSaveDialogEvent(const SDL_Event& e) {
    auto& d = modSaveDialog_;
    static const char modNamePal[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    auto beginNewModName = [&]() {
        d.phase = ModSaveDialogState::NameNewMod;
        d.editingAuthor = false;
        d.textEditing = true;
        d.gpCharIdx = 0;
        softKB_.open(&d.newModId, 24, [this](bool confirmed) {
            auto& dialog = modSaveDialog_;
            dialog.textEditing = false;
            if (!dialog.isOpen()) return;
            if (confirmed && !dialog.newModId.empty()) {
                dialog.editingAuthor = true;
                dialog.textEditing = true;
                softKB_.open(&dialog.newModAuthor, 32, [this](bool confirmed2) {
                    auto& dlg = modSaveDialog_;
                    dlg.editingAuthor = false;
                    dlg.textEditing = false;
                    if (!dlg.isOpen()) return;
                    if (confirmed2) {
                        if (dlg.newModAuthor.empty()) dlg.newModAuthor = "Unknown";
                        dlg.confirmedModFolder = modBuildFolder(dlg.newModId, dlg.newModId, dlg.newModAuthor);
                        dlg.confirmed = true;
                    } else {
                        dlg.phase = ModSaveDialogState::ChooseMod;
                    }
                });
            } else {
                dialog.phase = ModSaveDialogState::ChooseMod;
            }
        });
    };
    // Total entries in ChooseMod list = "New Mod" + existing mods
    int total = 1 + (int)d.modIds.size();

    if (d.phase == ModSaveDialogState::ChooseMod) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:    d.selIdx = (d.selIdx - 1 + total) % total; break;
                case SDLK_DOWN:  d.selIdx = (d.selIdx + 1) % total; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (d.selIdx == 0) {
                        beginNewModName();
                    } else {
                        // Existing mod selected -- destination folder is implied
                        // by the asset type, so save immediately.
                        d.confirmedModFolder = "mods/" + d.modIds[d.selIdx - 1];
                        d.confirmed = true;
                    }
                    break;
                case SDLK_ESCAPE: d.close(); break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    d.selIdx = (d.selIdx - 1 + total) % total; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    d.selIdx = (d.selIdx + 1) % total; break;
                case SDL_CONTROLLER_BUTTON_A:
                    if (d.selIdx == 0) {
                        beginNewModName();
                    } else {
                        d.confirmedModFolder = "mods/" + d.modIds[d.selIdx - 1];
                        d.confirmed = true;
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    d.close(); break;
            }
        }
    }
    else if (d.phase == ModSaveDialogState::NameNewMod) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_BACKSPACE: {
                    std::string& cur = d.editingAuthor ? d.newModAuthor : d.newModId;
                    if (!cur.empty()) cur.pop_back();
                    break;
                }
                case SDLK_ESCAPE:
                    if (softKB_.active) softKB_.close(false);
                    else d.phase = ModSaveDialogState::ChooseMod;
                    break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_B:
                    if (softKB_.active) softKB_.close(false);
                    else d.phase = ModSaveDialogState::ChooseMod;
                    break;
            }
        }
    }
    else if (d.phase == ModSaveDialogState::ChooseCategory) {
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:   d.catIdx = (d.catIdx - 1 + ModSaveDialogState::CAT_COUNT) % ModSaveDialogState::CAT_COUNT; break;
                case SDLK_DOWN: d.catIdx = (d.catIdx + 1) % ModSaveDialogState::CAT_COUNT; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    d.confirmedCat = d.catIdx;
                    d.confirmed    = true;
                    break;
                case SDLK_ESCAPE:
                    d.phase = ModSaveDialogState::ChooseMod; break;
            }
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            switch (btn) {
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
    static const char modNamePal[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";

    // Dim overlay
    ui_.drawDarkOverlay(190, 0, 0, 0);

    // Win98 window
    int panW = 640, panH = 420;
    int panX = (SCREEN_W - panW) / 2;
    int panY = (SCREEN_H - panH) / 2;

    static const char* ASSET_NAMES[] = {"MAP", "CHARACTER"};
    char title[64];
    snprintf(title, sizeof(title), "Save %s to Mod", ASSET_NAMES[(int)d.asset]);
    ui_.drawWin98Window(panX, panY, panW, panH, title);

    int y = panY + UI::W98::TitleH + 14;

    // Phase: choose mod
    if (d.phase == ModSaveDialogState::ChooseMod) {
        ui_.drawTextCentered("Choose or create a mod to save into:", y, 13, UI::W98::Shadow);
        y += 24;

        int total = 1 + (int)d.modIds.size();
        for (int i = 0; i < total; i++) {
            bool sel = (i == d.selIdx);
            if (i == 0) {
                if (ui_.win98Button(40 + i, "+ New Mod", panX + 20, y, panW - 40, 28, sel)) {
                    d.selIdx = i;
                    d.phase = ModSaveDialogState::NameNewMod;
                    d.editingAuthor = false;
                    d.textEditing = true;
                    softKB_.open(&d.newModId, 24, [this](bool confirmed) {
                        auto& dialog = modSaveDialog_;
                        dialog.textEditing = false;
                        if (!dialog.isOpen()) return;
                        if (confirmed && !dialog.newModId.empty()) {
                            dialog.editingAuthor = true;
                            dialog.textEditing = true;
                            softKB_.open(&dialog.newModAuthor, 32, [this](bool confirmed2) {
                                auto& dlg = modSaveDialog_;
                                dlg.editingAuthor = false;
                                dlg.textEditing = false;
                                if (!dlg.isOpen()) return;
                                if (confirmed2) {
                                    if (dlg.newModAuthor.empty()) dlg.newModAuthor = "Unknown";
                                    dlg.confirmedModFolder = modBuildFolder(dlg.newModId, dlg.newModId, dlg.newModAuthor);
                                    dlg.confirmed = true;
                                } else {
                                    dlg.phase = ModSaveDialogState::ChooseMod;
                                }
                            });
                        } else {
                            dialog.phase = ModSaveDialogState::ChooseMod;
                        }
                    });
                }
            } else {
                char buf[80];
                const char* nm = d.modNames[i-1].c_str();
                const char* id = d.modIds[i-1].c_str();
                if (d.modNames[i-1] != d.modIds[i-1])
                    snprintf(buf, sizeof(buf), "%s  (%s)", nm, id);
                else
                    snprintf(buf, sizeof(buf), "%s", nm);
                if (ui_.win98Button(40 + i, buf, panX + 20, y, panW - 40, 28, sel)) {
                    d.selIdx = i;
                    d.confirmedModFolder = "mods/" + d.modIds[i - 1];
                    d.confirmed = true;
                }
            }
            y += 32;
        }

        y = panY + panH - 40;
        { UI::HintPair hints[] = { {UI::Action::Navigate, "Navigate"}, {UI::Action::Confirm, "Confirm"}, {UI::Action::Back, "Cancel"} };
          ui_.drawHintBar(hints, 3, y); }
    }
    // Phase: name new mod (two steps: ID then author)
    else if (d.phase == ModSaveDialogState::NameNewMod) {
        if (!d.editingAuthor) {
            ui_.drawTextCentered("Step 1 of 2: Enter mod folder name", y, 13, UI::W98::Shadow);
        } else {
            ui_.drawTextCentered("Step 2 of 2: Enter author name (Enter to skip)", y, 13, UI::W98::Shadow);
        }
        y += 30;

        const std::string& display = d.editingAuthor ? d.newModAuthor : d.newModId;
        int bx = panX + 80, bw = panW - 160, bh = 30;
        ui_.drawWin98TextField(bx, y, bw, bh, display.c_str(), true, false,
                               (float)fmod(gameTime_ * 1.0, 1.0));
        y += bh + 16;

        if (!d.editingAuthor)
            ui_.drawTextCentered("Letters, digits, _ and - only", y, 11, UI::W98::Shadow);
        else
            ui_.drawTextCentered("Your name or handle", y, 11, UI::W98::Shadow);
        renderSoftKB();
        y = panY + panH - 40;
        ui_.drawTextCentered("ENTER / OK confirms   ESC / CANCEL goes back", y, 11, UI::W98::Shadow);
    }
    // Phase: choose sprite category
    else if (d.phase == ModSaveDialogState::ChooseCategory) {
        ui_.drawTextCentered("Where should this sprite go?", y, 13, UI::W98::Shadow);
        y += 26;

        for (int i = 0; i < ModSaveDialogState::CAT_COUNT; i++) {
            bool sel = (i == d.catIdx);
            if (ui_.win98Button(20 + i, ModSaveDialogState::CAT_NAMES[i],
                                panX + 20, y - 2, panW - 40, 26, sel)) {
                d.catIdx = i;
                d.confirmedCat = i;
                d.confirmed = true;
            }
            y += 30;
        }

        y = panY + panH - 40;
        { UI::HintPair hints[] = { {UI::Action::Navigate, "Navigate"}, {UI::Action::Confirm, "Confirm"}, {UI::Action::Back, "Back"} };
          ui_.drawHintBar(hints, 3, y); }
    }
}

// Config persistence

