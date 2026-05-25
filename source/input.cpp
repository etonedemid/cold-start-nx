// ─── input.cpp ─── Input handling and soft keyboard
#include "game.h"
#include "game_internal.h"
#include <ctime>
void Game::SoftKeyboard::open(const char* pal, int c, std::string* tgt, int max,
                              std::function<void(bool confirmed)> done) {
    active = true; palette = pal; cols = c; charIdx = 0;
    heldButton = -1; repeatAt = 0; target = tgt; maxLen = max;
    onDone = done;
    renderX = renderY = cellW = cellH = rows = 0;
    delRect = {0, 0, 0, 0};
    okRect = {0, 0, 0, 0};
    cancelRect = {0, 0, 0, 0};
#ifndef __SWITCH__
    SDL_StartTextInput();
#endif
}

void Game::SoftKeyboard::close(bool confirmed) {
    active = false;
#ifndef __SWITCH__
    SDL_StopTextInput();
#endif
    auto cb = onDone;
    target = nullptr;
    onDone = nullptr;
    heldButton = -1;
    if (cb) cb(confirmed);
}

void Game::updateSoftKBRepeat() {
    auto& kb = softKB_;
    if (!kb.active || !kb.palette || kb.heldButton < 0) return;
    if (SDL_GetTicks() < kb.repeatAt) return;
    int palLen = (int)strlen(kb.palette);
    int delta = 0;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_LEFT)  delta = -1;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) delta = +1;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_UP)    delta = -kb.cols;
    if (kb.heldButton == SDL_CONTROLLER_BUTTON_DPAD_DOWN)  delta = +kb.cols;
    if (delta != 0) kb.charIdx = (kb.charIdx + delta + palLen * 4) % palLen;
    kb.repeatAt = SDL_GetTicks() + 80;
}

bool Game::handleSoftKBEvent(SDL_Event& e) {
    auto& kb = softKB_;
    if (!kb.active || !kb.target || !kb.palette) return false;
    int palLen = (int)strlen(kb.palette);

    auto logicalPointFromMouse = [&](int px, int py, int& lx, int& ly) {
        float fx = (float)px;
        float fy = (float)py;
        SDL_RenderWindowToLogical(renderer_, px, py, &fx, &fy);
        lx = (int)fx;
        ly = (int)fy;
    };

    auto handlePointer = [&](int x, int y) -> bool {
        if (kb.delRect.w > 0 && ui_.pointInRect(x, y, kb.delRect.x, kb.delRect.y, kb.delRect.w, kb.delRect.h)) {
            if (!kb.target->empty()) kb.target->pop_back();
            return true;
        }
        if (kb.okRect.w > 0 && ui_.pointInRect(x, y, kb.okRect.x, kb.okRect.y, kb.okRect.w, kb.okRect.h)) {
            kb.close(true);
            return true;
        }
        if (kb.cancelRect.w > 0 && ui_.pointInRect(x, y, kb.cancelRect.x, kb.cancelRect.y, kb.cancelRect.w, kb.cancelRect.h)) {
            kb.close(false);
            return true;
        }
        if (kb.cellW <= 0 || kb.cellH <= 0 || kb.rows <= 0) return false;
        int totalW = kb.cols * kb.cellW;
        int totalH = kb.rows * kb.cellH;
        if (!ui_.pointInRect(x, y, kb.renderX, kb.renderY, totalW, totalH)) return false;
        int col = (x - kb.renderX) / kb.cellW;
        int row = (y - kb.renderY) / kb.cellH;
        int idx = row * kb.cols + col;
        if (idx < 0 || idx >= palLen) return true;
        kb.charIdx = idx;
        if ((int)kb.target->size() < kb.maxLen) *kb.target += kb.palette[idx];
        return true;
    };

    if (e.type == SDL_TEXTINPUT) {
        for (const char* p = e.text.text; *p; p++) {
            if (*p >= ' ' && *p <= '~' && *p != '=' && *p != '\n')
                if ((int)kb.target->size() < kb.maxLen) *kb.target += *p;
        }
        return true;
    }
    if (e.type == SDL_KEYDOWN) {
        auto sym = e.key.keysym.sym;
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) { kb.close(true); return true; }
        if (sym == SDLK_ESCAPE) { kb.close(false); return true; }
        if (sym == SDLK_BACKSPACE && !kb.target->empty()) { kb.target->pop_back(); return true; }
        // Ctrl+V paste
        if (sym == SDLK_v && (e.key.keysym.mod & KMOD_CTRL)) {
            if (SDL_HasClipboardText()) {
                char* clip = SDL_GetClipboardText();
                if (clip) {
                    for (const char* p = clip; *p; p++) {
                        if (*p >= ' ' && *p <= '~' && *p != '=' && *p != '\n')
                            if ((int)kb.target->size() < kb.maxLen) *kb.target += *p;
                    }
                    SDL_free(clip);
                }
            }
            return true;
        }
        return true; // consume all keydowns while keyboard active
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int x = 0, y = 0;
        logicalPointFromMouse(e.button.x, e.button.y, x, y);
        return handlePointer(x, y);
    }
    if (e.type == SDL_FINGERDOWN) {
        int x = (int)(e.tfinger.x * SCREEN_W);
        int y = (int)(e.tfinger.y * SCREEN_H);
        return handlePointer(x, y);
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = remapButton(e.cbutton.button);
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                kb.charIdx = (kb.charIdx - 1 + palLen) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                kb.charIdx = (kb.charIdx + 1) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                kb.charIdx = (kb.charIdx - kb.cols + palLen) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                kb.charIdx = (kb.charIdx + kb.cols) % palLen;
                kb.heldButton = btn; kb.repeatAt = SDL_GetTicks() + 350;
                break;
            case SDL_CONTROLLER_BUTTON_A:
                if ((int)kb.target->size() < kb.maxLen) *kb.target += kb.palette[kb.charIdx];
                break;
            case SDL_CONTROLLER_BUTTON_Y:
                if (!kb.target->empty()) kb.target->pop_back();
                break;
            case SDL_CONTROLLER_BUTTON_X:
            case SDL_CONTROLLER_BUTTON_START:
                kb.close(true);
                break;
            case SDL_CONTROLLER_BUTTON_B:
                kb.close(false);
                break;
        }
        return true;
    }
    if (e.type == SDL_CONTROLLERBUTTONUP) {
        Uint8 btn = remapButton(e.cbutton.button);
        if (btn == (Uint8)kb.heldButton) kb.heldButton = -1;
        return true;
    }
    return false;
}

void Game::renderSoftKB(int centerY) {
#ifndef __SWITCH__
    if (!usingGamepad_ && !ui_.touchActive) return;
#endif
    auto& kb = softKB_;
    if (!kb.active || !kb.palette) return;
    int palLen = (int)strlen(kb.palette);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {120, 120, 130, 255};

    bool singleRow = (palLen <= kb.cols);
    int cellW = singleRow ? 36 : 20;
    int cellH = singleRow ? 36 : 26;
    int cols = kb.cols;
    int rows = (palLen + cols - 1) / cols;
    int totalW = cols * cellW;
    int startX = (SCREEN_W - totalW) / 2;
    int palY = centerY;
    kb.renderX = startX;
    kb.renderY = palY;
    kb.cellW = cellW;
    kb.cellH = cellH;
    kb.rows = rows;

    // Opaque background
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 15, 16, 28, 255);
    SDL_Rect palBg = {startX - 8, palY - 8, totalW + 16, rows * cellH + 80};
    SDL_RenderFillRect(renderer_, &palBg);
    SDL_SetRenderDrawColor(renderer_, 0, 120, 110, 180);
    SDL_RenderDrawRect(renderer_, &palBg);

    for (int i = 0; i < palLen; i++) {
        int col = i % cols, row = i / cols;
        int cx = startX + col * cellW;
        int cy = palY + row * cellH;
        bool sel = (i == kb.charIdx);
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 255);
            SDL_Rect bg = {cx, cy, cellW - 2, cellH - 2};
            SDL_RenderFillRect(renderer_, &bg);
        }
        char ch[2] = { kb.palette[i], 0 };
        int fontSize = singleRow ? (sel ? 22 : 18) : (sel ? 18 : 14);
        int ox = singleRow ? 10 : 4;
        int oy = singleRow ? 6 : 3;
        drawText(ch, cx + ox, cy + oy, fontSize, sel ? white : gray);
    }

    int btnY = palY + rows * cellH + 12;
    auto drawKbButton = [&](SDL_Rect rect, const char* label, SDL_Color color) {
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, rect.x, rect.y, rect.w, rect.h);
        SDL_SetRenderDrawColor(renderer_, 18, 24, 34, 255);
        SDL_RenderFillRect(renderer_, &rect);
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, hovered ? 220 : 170);
        SDL_RenderDrawRect(renderer_, &rect);
        drawText(label, rect.x + 12, rect.y + 6, 14, color);
    };
    kb.delRect = {startX, btnY, 88, 28};
    kb.okRect = {startX + totalW / 2 - 44, btnY, 88, 28};
    kb.cancelRect = {startX + totalW - 88, btnY, 88, 28};
    drawKbButton(kb.delRect, "DEL", UI::Color::Yellow);
    drawKbButton(kb.okRect, "OK", UI::Color::Green);
    drawKbButton(kb.cancelRect, "CANCEL", {255, 120, 120, 255});

    { UI::HintPair hints[] = { {UI::Action::Navigate, "Navigate"}, {UI::Action::Confirm, "Insert"}, {UI::Action::Tab, "Delete"}, {UI::Action::Back, "Close"}, {UI::Action::Bomb, "Confirm"} };
      ui_.drawHintBar(hints, 5, palY + rows * cellH + 8); }
}



void Game::handleInput() {
    // Carry over confirmInput_ set by win98Button in the previous frame's render()
    // (it would be wiped by the reset below before the state machine can see it)
    bool renderConfirm = confirmInput_;
    bool renderBack    = backInput_;

    // Reset per-frame triggers
    bombInput_ = false;
    bombLaunchInput_ = false;
    parryInput_ = false;
    meleeInput_ = false;
    weaponSwitchDelta_ = 0;
    pauseInput_ = false;
    confirmInput_ = false;
    backInput_ = false;
    leftInput_ = false;
    rightInput_ = false;
    tabInput_ = false;

    // Soft keyboard hold-repeat
    updateSoftKBRepeat();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { running_ = false; return; }

        // Mod-save dialog gets first pick of all events when open
        if (modSaveDialog_.isOpen()) {
            if (modSaveDialog_.phase == ModSaveDialogState::NameNewMod && e.type == SDL_KEYDOWN) {
                SDL_Keycode sym = e.key.keysym.sym;
                if ((sym == SDLK_RETURN || sym == SDLK_KP_ENTER) && !modSaveDialog_.newModId.empty()) {
                    modSaveDialog_.confirmedModFolder = modBuildFolder(modSaveDialog_.newModId, modSaveDialog_.newModId);
                    modSaveDialog_.confirmed = true;
                    if (softKB_.active) softKB_.close(false);
                    continue;
                }
            }
            if (softKB_.active && handleSoftKBEvent(e)) continue;
            handleModSaveDialogEvent(e);
            continue;
        }

        // Clear D-pad hold-repeat when button released
        if (e.type == SDL_CONTROLLERBUTTONUP) {
            Uint8 btn = remapButton(e.cbutton.button);
            if (btn == (Uint8)kbNavHeldButton_)
                kbNavHeldButton_ = -1;
            if (btn == (Uint8)menuNavHeldBtn_)
                menuNavHeldBtn_ = -1;
        }

        // Pass events to editor if active
        if ((state_ == GameState::Editor || state_ == GameState::EditorConfig) && editor_.isActive()) {
            editor_.handleInput(e);
        }

        // Centralized soft keyboard handles ALL text input
        if (softKB_.active) {
            if (handleSoftKBEvent(e)) continue;
            continue;
        }

        // Login screen text input
        if (state_ == GameState::LoginScreen) {
            std::string* target = (loginField_ == 0) ? &loginUsername_ : &loginPassword_;
            if (e.type == SDL_TEXTINPUT) {
                for (const char* p = e.text.text; *p; p++) {
                    if (*p >= ' ' && *p <= '~' && (int)target->size() < 32)
                        *target += *p;
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode sym = e.key.keysym.sym;
                if (sym == SDLK_BACKSPACE && !target->empty()) { target->pop_back(); continue; }
                if (sym == SDLK_TAB) { loginField_ = (loginField_ == 0) ? 1 : 0; loginBlinkT_ = 0; continue; }
                if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) { confirmInput_ = true; continue; }
                if (sym == SDLK_ESCAPE) { backInput_ = true; continue; }
                // Ctrl+V paste
                if (sym == SDLK_v && (e.key.keysym.mod & KMOD_CTRL)) {
                    if (SDL_HasClipboardText()) {
                        char* clip = SDL_GetClipboardText();
                        if (clip) {
                            for (const char* p = clip; *p; p++)
                                if (*p >= ' ' && *p <= '~' && (int)target->size() < 32)
                                    *target += *p;
                            SDL_free(clip);
                        }
                    }
                    continue;
                }
                continue;
            }
        }

        // ── Lobby chat text input ──
        if (e.type == SDL_TEXTINPUT && chatTyping_ && state_ == GameState::Lobby) {
            size_t cur = strlen(chatInputBuf_);
            size_t len = strlen(e.text.text);
            if (cur + len < 255) { strcat(chatInputBuf_, e.text.text); }
            continue;
        }
        if (e.type == SDL_KEYDOWN && chatTyping_ && state_ == GameState::Lobby) {
            if (e.key.keysym.sym == SDLK_BACKSPACE && chatInputBuf_[0] != '\0') {
                chatInputBuf_[strlen(chatInputBuf_) - 1] = '\0';
            } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                confirmInput_ = true;
            } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                backInput_ = true;
            }
            continue;
        }

        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 btn = remapButton(e.cbutton.button);
            lastGamepadInputId_ = e.cbutton.which;
            // ── Local join/leave handling in lobby screens ──
            if (state_ == GameState::LocalCoopLobby || state_ == GameState::Lobby) {
                bool isMultiplayerLobby = (state_ == GameState::Lobby);
                SDL_JoystickID iid = e.cbutton.which;
                // Check if this gamepad is already assigned to a slot
                int existingSlot = -1;
                for (int s = 0; s < 4; s++) {
                    if (coopSlots_[s].joined && coopSlots_[s].joyInstanceId == iid)
                        { existingSlot = s; break; }
                }
                if (btn == SDL_CONTROLLER_BUTTON_A) {
                    bool isLobbyPrimaryPad = (isMultiplayerLobby && iid == lobbyPrimaryPadId_);
                    if (existingSlot < 0) {
                        if (isLobbyPrimaryPad) {
                            // This pad opened host/join flow; allow A to act as normal confirm/ready,
                            // but never treat it as a local sub-player join.
                        } else {
#ifdef __SWITCH__
                            // On Switch, gamepads can join slot 0-3
                            int startSlot = 0;
#else
                            // On PC, slot 0 is keyboard+mouse, gamepads join 1-3
                            int startSlot = 1;
#endif
                            for (int s = startSlot; s < 4; s++) {
                                if (!coopSlots_[s].joined) {
                                    int gpNum = 0;
                                    for (int k = startSlot; k < 4; k++) if (coopSlots_[k].joined) gpNum++;
                                    coopSlots_[s].joined = true;
                                    coopSlots_[s].joyInstanceId = iid;
#ifdef __SWITCH__
                                    char uname[16];
                                    if (s == 0) {
                                        snprintf(uname, sizeof(uname), "%s", config_.username.c_str());
                                    } else {
                                        snprintf(uname, sizeof(uname), "nx-%d", gpNum);
                                    }
#else
                                    char uname[16]; snprintf(uname, sizeof(uname), "pc-%d", gpNum + 1);
#endif
                                    coopSlots_[s].username = uname;
                                    if (isMultiplayerLobby) {
                                        int localSubPlayers = 0;
                                        for (int si = startSlot; si < 4; si++) if (coopSlots_[si].joined && si > 0) localSubPlayers++;
                                        NetworkManager::instance().setLocalSubPlayers((uint8_t)localSubPlayers);
                                        lobbySubPlayersSent_ = localSubPlayers;
                                    }
                                    if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                                    break;
                                }
                            }
                            continue;
                        }
                    }
                    // In multiplayer lobby, let already joined gamepads still use A as confirm/ready.
                    if (!isMultiplayerLobby || existingSlot < 0) continue;
                } else if (btn == SDL_CONTROLLER_BUTTON_B) {
#ifdef __SWITCH__
                    int minSlot = 0; // On Switch, slot 0 can leave
#else
                    int minSlot = 1; // On PC, slot 0 is keyboard+mouse
#endif
                    if (existingSlot >= minSlot) {
                        coopSlots_[existingSlot] = CoopSlot{};
                        // Renumber remaining gamepad usernames
                        int gpNum = 1;
#ifdef __SWITCH__
                        // On Switch: slot 0 gets config name, others get nx-N
                        for (int s = 0; s < 4; s++) {
                            if (!coopSlots_[s].joined) continue;
                            if (s == 0) {
                                coopSlots_[s].username = config_.username;
                            } else {
                                char uname[16]; snprintf(uname, sizeof(uname), "nx-%d", gpNum++);
                                coopSlots_[s].username = uname;
                            }
                        }
#else
                        // On PC: all gamepads get pc-N
                        for (int s = 1; s < 4; s++) {
                            if (!coopSlots_[s].joined) continue;
                            char uname[16]; snprintf(uname, sizeof(uname), "pc-%d", gpNum++);
                            coopSlots_[s].username = uname;
                        }
#endif
                        if (isMultiplayerLobby) {
                            int localSubPlayers = 0;
                            for (int s = 1; s < 4; s++) if (coopSlots_[s].joined) localSubPlayers++;
                            NetworkManager::instance().setLocalSubPlayers((uint8_t)localSubPlayers);
                            lobbySubPlayersSent_ = localSubPlayers;
                        }
                    } else if (existingSlot < 0 && !isMultiplayerLobby) {
                        // Unassigned gamepad pressed B — treat as global back
                        backInput_ = true;
                    }
#ifdef __SWITCH__
                    if (existingSlot >= 0 || !isMultiplayerLobby) continue;
#else
                    if (existingSlot > 0 || !isMultiplayerLobby) continue;
#endif
                } else if (btn == SDL_CONTROLLER_BUTTON_START) {
#ifdef __SWITCH__
                    // On Switch, any joined gamepad can start if at least one player is joined
                    bool anyJoined = false;
                    for (int s = 0; s < 4; s++) if (coopSlots_[s].joined) { anyJoined = true; break; }
                    if (existingSlot >= 0 || anyJoined) {
#else
                    // On PC, any joined gamepad can start if slot 0 (keyboard+mouse) is joined
                    if (existingSlot >= 0 || coopSlots_[0].joined) {
#endif
                        confirmInput_ = true;
                        if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    }
                    continue; // consumed by lobby
                }
                // Other buttons in lobby — fall through to normal handling
            }
            usingGamepad_ = true;
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_START:    pauseInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_A:        confirmInput_ = true; if (sfxPress_) { int ch = Mix_PlayChannel(-1, sfxPress_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); } break;
                case SDL_CONTROLLER_BUTTON_B:        backInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: parryInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: weaponSwitchDelta_ += 1; break;
                case SDL_CONTROLLER_BUTTON_X:        bombInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_Y:        tabInput_ = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_UP;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    menuSelection_--;
                    if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    menuSelection_++;
                    if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    leftInput_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    menuNavHeldBtn_ = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                    menuNavRepeatAt_ = SDL_GetTicks() + 320;
                    rightInput_ = true;
                    break;
            }
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            usingGamepad_ = false;
            {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: pauseInput_ = true; break;
                    case SDLK_RETURN: confirmInput_ = true; break;
                    case SDLK_BACKSPACE: backInput_ = true; break;
                    case SDLK_SPACE:  parryInput_ = true; break;
                    case SDLK_e:     meleeInput_ = true; break;
                    case SDLK_1:     player_.activeWeapon = 0; break;
                    case SDLK_2:     player_.activeWeapon = 1; break;
                    case SDLK_q:     bombInput_ = true; break;
                    case SDLK_TAB:   tabInput_ = true; break;
                    case SDLK_F11:
                        config_.fullscreen = !config_.fullscreen;
                        SDL_SetWindowFullscreen(window_, config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        break;
                }
            }
        }
        // Arrow keys with OS key-repeat (fires on both initial press and held repeats)
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:    menuSelection_--; break;
                case SDLK_DOWN:  menuSelection_++; break;
                case SDLK_LEFT:  leftInput_  = true; break;
                case SDLK_RIGHT: rightInput_ = true; break;
            }
        }

        if (e.type == SDL_MOUSEWHEEL) {
            if (state_ == GameState::HostSetup)
                hostSetupScrollY_ = std::max(0, std::min(206, hostSetupScrollY_ - e.wheel.y * 30));
            else if (state_ == GameState::Lobby)
                lobbySettingsScrollY_ = std::max(0, lobbySettingsScrollY_ - e.wheel.y * 34);
            else
                weaponSwitchDelta_ += (e.wheel.y > 0) ? 1 : (e.wheel.y < 0 ? -1 : 0);
        }

        // Mouse movement switches to mouse/keyboard mode
        if (e.type == SDL_MOUSEMOTION) {
            usingGamepad_ = false;
        }

        // Mouse click — switch to mouse mode; button activation is handled by win98Button in render.
        // Also set mouseClicked here so fast clicks (press+release within one frame) aren't missed.
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            usingGamepad_ = false;
            ui_.mouseClicked = true;
        }

        // Touch events — convert to mouse-like behaviour for menu navigation
        if (e.type == SDL_FINGERDOWN) {
            usingGamepad_ = false;
            ui_.touchActive = true;
            ui_.mouseX = (int)(e.tfinger.x * SCREEN_W);
            ui_.mouseY = (int)(e.tfinger.y * SCREEN_H);
            ui_.mouseDown = true;
            ui_.mouseClicked = true;
        }
        if (e.type == SDL_FINGERMOTION) {
            ui_.touchActive = true;
            ui_.mouseX = (int)(e.tfinger.x * SCREEN_W);
            ui_.mouseY = (int)(e.tfinger.y * SCREEN_H);
        }
        if (e.type == SDL_FINGERUP) {
            ui_.touchActive = false;
            ui_.mouseX = (int)(e.tfinger.x * SCREEN_W);
            ui_.mouseY = (int)(e.tfinger.y * SCREEN_H);
            ui_.mouseDown = false;
            ui_.mouseReleased = true;
        }
    }

    // ── D-pad hold-repeat for menu navigation ──
    {
        Uint32 now = SDL_GetTicks();
        if (menuNavHeldBtn_ >= 0 && now >= menuNavRepeatAt_) {
            menuNavRepeatAt_ = now + 110;
            switch (menuNavHeldBtn_) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                menuSelection_--;
                if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                menuSelection_++;
                if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  leftInput_  = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: rightInput_ = true; break;
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

    // Controller left stick — use the primary gameplay pad and avoid pads owned by local sub-players.
    SDL_GameController* gc = getPrimaryGameplayController();

    // Gamepad stick input (movement + aim)
    bool gcAimActive = false;
    bool gcFireActive = false;
    if (gc) {
        float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        if (fabsf(lx) > 0.15f || fabsf(ly) > 0.15f) {
            moveInput_ = {lx, ly};
            usingGamepad_ = true;
        }

        // Right stick for aiming
        float rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
        if (fabsf(rx) > 0.2f || fabsf(ry) > 0.2f) {
            gcAimActive = true;
            usingGamepad_ = true;
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

        // ZR = fire
        gcFireActive = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        // ZL = launch bomb (one-shot)
        bool zlDown = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        if (zlDown && !bombLaunchHeld_) bombLaunchInput_ = true;
        bombLaunchHeld_ = zlDown;
    }

    // ── Analog-stick menu navigation (non-gameplay states) ──
    {
        bool inGameplay = (state_ == GameState::Playing  || state_ == GameState::Paused ||
            state_ == GameState::Dead            || state_ == GameState::LocalCoopGame  ||
            state_ == GameState::LocalCoopPaused || state_ == GameState::LocalCoopDead ||
            state_ == GameState::MultiplayerGame  || state_ == GameState::MultiplayerPaused ||
            state_ == GameState::MultiplayerDead  || state_ == GameState::MultiplayerSpectator ||
            state_ == GameState::PlayingPack      || state_ == GameState::PackPaused ||
            state_ == GameState::PackDead         || state_ == GameState::PackLevelWin ||
            state_ == GameState::Editor           || state_ == GameState::EditorConfig);
        if (!inGameplay && gc) {
            Uint32 now = SDL_GetTicks();
            float lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            const float THRESH = 0.35f;
            int snY = (ly < -THRESH) ? -1 : (ly > THRESH) ? 1 : 0;
            int snX = (lx < -THRESH) ? -1 : (lx > THRESH) ? 1 : 0;
            int snCur = (snY != 0) ? snY : snX;
            if (snCur == 0) {
                menuNavStickPrev_ = 0;
            } else if (menuNavHeldBtn_ < 0) {
                bool isNew = (menuNavStickPrev_ == 0);
                if (isNew || (snCur == menuNavStickPrev_ && now >= menuNavRepeatAt_)) {
                    menuNavRepeatAt_ = now + (isNew ? 300U : 110U);
                    if (snY != 0) {
                        menuSelection_ += snY;
                        if (sfxBeep_) { int ch = Mix_PlayChannel(-1, sfxBeep_, 0); if (ch >= 0) Mix_Volume(ch, config_.sfxVolume); }
                    } else {
                        leftInput_  = snX < 0;
                        rightInput_ = snX > 0;
                    }
                }
                menuNavStickPrev_ = snCur;
                usingGamepad_ = true;
            }
        }
    }

    // Mouse aiming — always active as fallback when gamepad right stick is idle
    {
        int mx, my;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        bool allowMouseAim = (!gc || !usingGamepad_);
    #ifdef __SWITCH__
        allowMouseAim = false;
    #endif
        if (!gcAimActive && allowMouseAim) {
            Vec2 mouseWorld = camera_.screenToWorld({(float)mx, (float)my});
            Vec2 diff = mouseWorld - player_.pos;
            if (diff.length() > 5.0f) {
                aimInput_ = diff.normalized();
            } else {
                aimInput_ = moveInput_;
            }
        }
        // Mouse/keyboard fire — always available alongside gamepad
        fireInput_ = gcFireActive || (mb & SDL_BUTTON_LMASK) || keys[SDL_SCANCODE_J] || keys[SDL_SCANCODE_Z];
        bool rmbDown = (mb & SDL_BUTTON_RMASK) != 0;
        if (rmbDown && !bombLaunchHeld_) bombLaunchInput_ = true;
        if (!gc) bombLaunchHeld_ = rmbDown;  // only track RMB hold when no gamepad (ZL tracks its own)
    }

    // Normalize move
    if (moveInput_.length() > 1.0f) moveInput_ = moveInput_.normalized();

    // 100ms debounce: suppress all menu inputs for one cool-down window after
    // any activation, preventing cross-frame double-fire from renderConfirm.
    if (menuInputCooldown_ > 0.0f) {
        menuInputCooldown_ -= dt_;
        if (menuInputCooldown_ < 0.0f) menuInputCooldown_ = 0.0f;
        renderConfirm = false;
        renderBack    = false;
        confirmInput_ = false;
        backInput_    = false;
    }

    // Merge render()-sourced inputs (mouse clicks) with event-sourced inputs
    confirmInput_ |= renderConfirm;
    backInput_    |= renderBack;

    // Start debounce window whenever any activation fires
    if (confirmInput_ || backInput_) menuInputCooldown_ = 0.1f;

    // Handle menu state transitions
    if (state_ == GameState::BiosIntro) {
        // Any key / button / click advances to login screen
        if (confirmInput_ || backInput_ || pauseInput_ || ui_.mouseClicked) {
            biosTimer_ = 0;
            biosLine_  = 0;
            loginUsername_ = config_.username;
            loginPassword_.clear();
            loginField_  = 0;
            loginBlinkT_ = 0;
            state_ = GameState::LoginScreen;
#ifndef __SWITCH__
            SDL_StartTextInput();
#endif
        }
    }
    else if (state_ == GameState::LoginScreen) {
        // OK button or Enter confirms login
        if (confirmInput_) {
            config_.username = loginUsername_.empty() ? "Player" : loginUsername_;
            saveConfig();
#ifndef __SWITCH__
            SDL_StopTextInput();
#endif
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
            playMenuMusic();
        }
        // Esc / Back skips login
        if (backInput_ || pauseInput_) {
#ifndef __SWITCH__
            SDL_StopTextInput();
#endif
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
            playMenuMusic();
        }
        // Tab switches between username (0) and password (1) fields
        if (tabInput_) {
            loginField_ = (loginField_ == 0) ? 1 : 0;
            loginBlinkT_ = 0;
        }
    }
    else if (state_ == GameState::MainMenu) {
        // ── Log-off confirmation dialog intercept ──
        // Must run before the clamp so menuSelection_ 90/91 aren't wiped.
        if (logOffConfirm_) {
            if (confirmInput_) {
                if (menuSelection_ == 90) { running_ = false; }
                else { logOffConfirm_ = false; menuSelection_ = 10; }
            }
            if (backInput_) { logOffConfirm_ = false; menuSelection_ = 10; backInput_ = false; }
            return; // block all other menu input while dialog is open
        }

        if (menuSelection_ < 0) menuSelection_ = 0;
#ifdef __SWITCH__
        if (menuSelection_ > 9) menuSelection_ = 9;
#else
        if (menuSelection_ > 10) menuSelection_ = 10;
#endif

        if (confirmInput_) {
            if (menuSelection_ == 0) {
                state_ = GameState::PlayModeMenu;
                playModeSelection_ = 0;
                menuSelection_ = 0;
            }
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
                scanMapFiles();
                prevMenuState_ = GameState::MainMenu;
                state_ = GameState::MapSelect;
                mapSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 4) {
                scanMapPacks();
                prevMenuState_ = GameState::MainMenu;
                state_ = GameState::PackSelect;
                packSelectIdx_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 5) {
                scanCharacters();
                state_ = GameState::CharSelect;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 6) {
                // Create Character — scan first so we can edit existing ones
                scanCharacters();
                charCreator_ = CharCreatorState{};
                state_ = GameState::CharCreator;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 7) {
                // Mods
                ModManager::instance().scanMods();
                state_ = GameState::ModMenu;
                modMenuSelection_ = 0;
                menuSelection_ = 0;
            }
            else if (menuSelection_ == 8) {
                state_ = GameState::ConfigMenu;
                configSelection_ = 0;
                menuSelection_ = 0;
            }
#ifndef __SWITCH__
            else if (menuSelection_ == 9) {
                // UPDATE — open release page
                #if defined(_WIN32)
                    system("start https://github.com/etonedemid/cold-start-nx/releases/latest");
                #else
                    system("xdg-open https://github.com/etonedemid/cold-start-nx/releases/latest 2>/dev/null || "
                           "firefox https://github.com/etonedemid/cold-start-nx/releases/latest 2>/dev/null || "
                           "google-chrome https://github.com/etonedemid/cold-start-nx/releases/latest 2>/dev/null");
                #endif
            }
            else if (menuSelection_ == 10) {
                logOffConfirm_ = true;
                menuSelection_ = 91; // pre-select "No" (safe default)
            }
#else
            else if (menuSelection_ == 9) {
                logOffConfirm_ = true;
                menuSelection_ = 91;
            }
#endif
        }
    }
    else if (state_ == GameState::PlayModeMenu) {
        playModeSelection_ = menuSelection_;   // propagate DPad nav
        if (playModeSelection_ < 0) playModeSelection_ = 0;
        if (playModeSelection_ > 9) playModeSelection_ = 9;
        menuSelection_ = playModeSelection_;

        auto adjustIntPM = [&](int& value, int minV, int maxV, int step) {
            if (leftInput_)  value = std::max(minV, value - step);
            if (rightInput_) value = std::min(maxV, value + step);
        };
        auto adjustFloatPM = [&](float& value, float minV, float maxV, float step) {
            if (leftInput_)  value = std::max(minV, value - step);
            if (rightInput_) value = std::min(maxV, value + step);
        };

        if      (playModeSelection_ == 3) adjustIntPM  (config_.mapWidth,        20,   120, 2);
        else if (playModeSelection_ == 4) adjustIntPM  (config_.mapHeight,        14,    80, 2);
        else if (playModeSelection_ == 5) adjustIntPM  (config_.playerMaxHp,       1,    20, 1);
        else if (playModeSelection_ == 6) adjustFloatPM(config_.spawnRateScale,  0.3f,  3.0f, 0.1f);
        else if (playModeSelection_ == 7) adjustFloatPM(config_.enemyHpScale,    0.3f,  3.0f, 0.1f);
        else if (playModeSelection_ == 8) adjustFloatPM(config_.enemySpeedScale, 0.5f,  2.5f, 0.1f);

        if (confirmInput_) {
            if (playModeSelection_ == 0) {
                startGame();
            } else if (playModeSelection_ == 1) {
                scanMapFiles();
                prevMenuState_ = GameState::PlayModeMenu;
                state_ = GameState::MapSelect;
                mapSelectIdx_ = 0;
                menuSelection_ = 0;
            } else if (playModeSelection_ == 2) {
                scanMapPacks();
                prevMenuState_ = GameState::PlayModeMenu;
                state_ = GameState::PackSelect;
                packSelectIdx_ = 0;
                menuSelection_ = 0;
            } else if (playModeSelection_ == 9) {
                saveConfig();
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }
        if (backInput_ || pauseInput_) {
            saveConfig();
            state_ = GameState::MainMenu;
            menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::LocalCoopLobby) {
#ifndef __SWITCH__
        // P1 (keyboard+mouse) is always joined on PC
        if (!coopSlots_[0].joined) {
            coopSlots_[0].joined = true;
            coopSlots_[0].joyInstanceId = -1;
            coopSlots_[0].username = config_.username;
        }
#endif
        // Toggle mode with TAB
        if (tabInput_) {
            lobbySettings_.isPvp = !lobbySettings_.isPvp;
            lobbySettings_.pvpEnabled = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
        }
        if (confirmInput_) {
            // Only P1 (Enter on keyboard, START on P1 gamepad) starts the game
            coopPlayerCount_ = 0;
            for (int i = 0; i < 4; i++) if (coopSlots_[i].joined) coopPlayerCount_++;
            startLocalCoopGame();
        }
        if (backInput_ || pauseInput_) {
            for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
            coopPlayerCount_ = 0;
            state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
        }
        // Allow gamepad join/unjoin
        for (int ci = 0; ci < 4; ci++) {
            auto& slot = coopSlots_[ci];
            (void)slot; // handled by joy events
        }
    }
    else if (state_ == GameState::LocalCoopGame) {
        if (pauseInput_) { state_ = GameState::LocalCoopPaused; menuSelection_ = 0; }
    }
    else if (state_ == GameState::LocalCoopPaused) {
        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > 1) menuSelection_ = 1;
        if (pauseInput_ || backInput_) state_ = GameState::LocalCoopGame;
        if (confirmInput_) {
            if (menuSelection_ == 0) state_ = GameState::LocalCoopGame;
            else {
                for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
                coopPlayerCount_ = 0;
                state_ = GameState::MainMenu; menuSelection_ = 0;
                playMenuMusic();
            }
        }
    }
    else if (state_ == GameState::LocalCoopDead) {
        if (confirmInput_ || backInput_) {
            for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
            coopPlayerCount_ = 0;
            state_ = GameState::MainMenu; menuSelection_ = 0;
            playMenuMusic();
        }
    }
    else if (state_ == GameState::ConfigMenu) {
        if (configSelection_ < 0) configSelection_ = 0;
        if (configSelection_ > CONFIG_BACK_INDEX) configSelection_ = CONFIG_BACK_INDEX;

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > CONFIG_BACK_INDEX) menuSelection_ = CONFIG_BACK_INDEX;

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
            auto toggleBool = [&](bool& value) {
                if (confirmInput_) value = !value;
            };
#ifndef __SWITCH__
            auto adjustResolution = [&](int dir) {
                int idx = findResolutionPresetIndex(config_.windowWidth, config_.windowHeight);
                idx = std::max(0, std::min((int)(sizeof(kResolutionPresets) / sizeof(kResolutionPresets[0])) - 1, idx + dir));
                config_.windowWidth = kResolutionPresets[idx].w;
                config_.windowHeight = kResolutionPresets[idx].h;
                applyResolutionSettings(true);
            };
#endif

            if      (configSelection_ == 0) adjustInt  (config_.playerMaxHp,    1,    20, 1);
            else if (configSelection_ == 1) adjustFloat(config_.spawnRateScale, 0.3f, 3.0f, 0.1f);
            else if (configSelection_ == 2) adjustFloat(config_.enemyHpScale,   0.3f, 3.0f, 0.1f);
            else if (configSelection_ == 3) adjustFloat(config_.enemySpeedScale,0.5f, 2.5f, 0.1f);
            else if (configSelection_ == 4) { adjustInt(config_.musicVolume, 0, 128, 8); Mix_VolumeMusic(config_.musicVolume); }
            else if (configSelection_ == 5) { adjustInt(config_.sfxVolume, 0, 128, 8); }
#ifndef __SWITCH__
            else if (configSelection_ == CONFIG_RESOLUTION_INDEX) {
                if (leftInput_) adjustResolution(-1);
                if (rightInput_) adjustResolution(1);
            }
#endif
            else if (configSelection_ == CONFIG_SHADER_CRT_INDEX) toggleBool(config_.shaderCRT);
            else if (configSelection_ == CONFIG_SHADER_CHROMATIC_INDEX) toggleBool(config_.shaderChromatic);
            else if (configSelection_ == CONFIG_SHADER_SCANLINES_INDEX) toggleBool(config_.shaderScanlines);
            else if (configSelection_ == CONFIG_SHADER_GLOW_INDEX) toggleBool(config_.shaderGlow);
            else if (configSelection_ == CONFIG_SHADER_GLITCH_INDEX) toggleBool(config_.shaderGlitch);
            else if (configSelection_ == CONFIG_SHADER_NEON_INDEX) toggleBool(config_.shaderNeonEdge);
            else if (configSelection_ == CONFIG_SAVE_INCOMING_MODS_INDEX) toggleBool(config_.saveIncomingModsPermanently);
            else if (configSelection_ == CONFIG_USERNAME_INDEX && confirmInput_) {
                // Edit username
                usernameTyping_ = true;
                softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", 16,
                             &config_.username, 32, [this](bool) {
                    usernameTyping_ = false;
                    if (config_.username.empty()) config_.username = "Player";
                    NetworkManager::instance().setUsername(config_.username);
                });
            }
            else if (configSelection_ == CONFIG_BACK_INDEX && (confirmInput_ || backInput_ || pauseInput_)) {
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
            else if (menuSelection_ == 4) {
                if (testPlayFromEditor_) {
                    testPlayFromEditor_ = false;
                    state_ = GameState::CharCreator;
                    playMenuMusic();
                    menuSelection_ = 0;
                } else {
                    state_ = GameState::MainMenu; playMenuMusic();
                }
            }
#else
            else if (menuSelection_ == 3) {
                if (testPlayFromEditor_) {
                    testPlayFromEditor_ = false;
                    state_ = GameState::CharCreator;
                    playMenuMusic();
                    menuSelection_ = 0;
                } else {
                    state_ = GameState::MainMenu; playMenuMusic();
                }
            }
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
            if (menuSelection_ == 0) {
                if (testPlayFromEditor_) {
                    // Restart test with same character
                    testCharacter();
                } else {
                    startGame();
                }
            } else {
                if (testPlayFromEditor_) {
                    testPlayFromEditor_ = false;
                    state_ = GameState::CharCreator;
                    playMenuMusic();
                    menuSelection_ = 0;
                } else {
                    state_ = GameState::MainMenu; playMenuMusic(); menuSelection_ = 0;
                }
            }
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
        if (backInput_) {
            if (prevMenuState_ == GameState::PlayModeMenu) {
                state_ = GameState::PlayModeMenu; playModeSelection_ = 1; menuSelection_ = 1;
            } else {
                state_ = GameState::MainMenu; menuSelection_ = 0;
            }
        }
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
                if (prevMenuState_ == GameState::PlayModeMenu) {
                    state_ = GameState::PlayModeMenu; playModeSelection_ = 1; menuSelection_ = 1;
                } else {
                    state_ = GameState::MainMenu; menuSelection_ = 0;
                }
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
                resetToDefaultCharacter();
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
        if (modSaveDialog_.isOpen()) {
            if (cc.statusTimer > 0) cc.statusTimer -= dt_;
        }
        else if (cc.textEditing) {
            // Text input handled via SDL_TEXTINPUT events already processed
        } else {
            cc.field = menuSelection_;
            if (cc.field < 0) cc.field = 0;
            if (cc.field > 10) cc.field = 10;
            menuSelection_ = cc.field;
            
            // Tab/Shift+Tab for preview sections
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (cc.field < 1 || cc.field > 5) {
                static bool tabWasPressed = false;
                static bool shiftTabWasPressed = false;
                bool tabPressed = keys[SDL_SCANCODE_TAB] && !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                bool shiftTabPressed = keys[SDL_SCANCODE_TAB] && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                
                if (tabPressed && !tabWasPressed) {
                    cc.previewSection++;
                    if (cc.previewSection > 4) cc.previewSection = 0;
                    cc.previewFrame = 0; cc.animTime = 0.0f;
                }
                if (shiftTabPressed && !shiftTabWasPressed) {
                    cc.previewSection--;
                    if (cc.previewSection < 0) cc.previewSection = 4;
                    cc.previewFrame = 0; cc.animTime = 0.0f;
                }
                tabWasPressed = tabPressed;
                shiftTabWasPressed = shiftTabPressed;
                
                // P key to toggle play/pause, R key to hot-reload
                static bool pWasPressed = false, rWasPressed = false;
                bool pPressed = keys[SDL_SCANCODE_P];
                bool rPressed = keys[SDL_SCANCODE_R];
                if (pPressed && !pWasPressed) cc.playAnimation = !cc.playAnimation;
                if (rPressed && !rWasPressed && cc.loaded) {
                    // Hot-reload sprites
                    cc.charDef.reloadSprites(renderer_);
                    cc.warnings = cc.charDef.validate();
                    cc.statusMsg = "Sprites reloaded!";
                    cc.statusTimer = 2.0f;
                }
                pWasPressed = pPressed;
                rWasPressed = rPressed;
            }

            // Adjust values with left/right (fields 1-5 = stats)
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

            auto previewFrameCount = [&]() -> int {
                switch (cc.previewSection) {
                    case 1: return cc.loaded ? (int)cc.charDef.bodySprites.size() : (int)playerSprites_.size();
                    case 2: return cc.loaded ? (int)cc.charDef.legSprites.size() : (int)legSprites_.size();
                    case 3: return cc.loaded ? (int)cc.charDef.deathSprites.size() : (int)playerDeathSprites_.size();
                    default: return 1;
                }
            };
            if ((cc.field == 0 || cc.field >= 6) && (leftInput_ || rightInput_)) {
                int frameCount = previewFrameCount();
                if (frameCount > 1) {
                    cc.playAnimation = false;
                    cc.previewFrame += rightInput_ ? 1 : -1;
                    if (cc.previewFrame < 0) cc.previewFrame = frameCount - 1;
                    if (cc.previewFrame >= frameCount) cc.previewFrame = 0;
                }
            }

            // Mouse click handling for buttons (must happen before keyboard confirmInput_ check)
            if (ui_.mouseClicked && !usingGamepad_) {
                // Check if any button was clicked
                if (ui_.prevHoveredItem == 6) { cc.field = 6; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 7) { cc.field = 7; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 8) { cc.field = 8; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 9) { cc.field = 9; confirmInput_ = true; }
                if (ui_.prevHoveredItem == 10) { cc.field = 10; confirmInput_ = true; }
            }

            // Name field: start text editing on confirm
            if (cc.field == 0 && confirmInput_) {
                cc.textEditing = true;
                cc.textBuf = cc.name;
                softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-!@#.", 16,
                             &cc.textBuf, 16, [this](bool confirmed) {
                    charCreator_.textEditing = false;
                    if (confirmed) charCreator_.name = charCreator_.textBuf;
                });
            }
            // Reload button
            if (cc.field == 6 && confirmInput_) {
                if (cc.loaded) {
                    cc.charDef.reloadSprites(renderer_);
                    cc.warnings = cc.charDef.validate();
                    cc.statusMsg = "Sprites reloaded!";
                    cc.statusTimer = 2.0f;
                } else if (!cc.folderPath.empty()) {
                    loadCharacterIntoCreator(cc.folderPath);
                } else {
                    // Try characters/<name>
                    std::string tryPath = "characters/" + cc.name;
                    loadCharacterIntoCreator(tryPath);
                }
            }
            // Test Play button
            if (cc.field == 7 && confirmInput_) {
                testCharacter();
            }
            // Save button
            if (cc.field == 8 && confirmInput_) {
                if (!modSaveDialog_.isOpen()) {
                    openModSaveDialog(ModSaveDialogState::AssetCharacter);
                }
            }
            // Save local button
            if (cc.field == 9 && confirmInput_) {
                std::string folder = cc.folderPath.empty() ? 
                    "characters/" + cc.name : cc.folderPath;
                saveCharacterToFolder(folder);
                cc.statusMsg = "Saved to " + folder + "/";
                cc.statusTimer = 3.0f;
            }
            // Back button
            if (cc.field == 10 && confirmInput_) {
                cc.clearPreviews(renderer_);
                state_ = GameState::MainMenu;
                menuSelection_ = 0;
            }
        }
        // Status timer
        if (cc.statusTimer > 0) cc.statusTimer -= dt_;

        if (backInput_ && !cc.textEditing) {
            cc.clearPreviews(renderer_);
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
        if (backInput_) {
            if (prevMenuState_ == GameState::PlayModeMenu) {
                state_ = GameState::PlayModeMenu; playModeSelection_ = 2; menuSelection_ = 2;
            } else {
                state_ = GameState::MainMenu; menuSelection_ = 0;
            }
        }
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
                if (prevMenuState_ == GameState::PlayModeMenu) {
                    state_ = GameState::PlayModeMenu; playModeSelection_ = 2; menuSelection_ = 2;
                } else {
                    state_ = GameState::MainMenu; menuSelection_ = 0;
                }
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
                hostSetupScrollY_   = 0;
                gamemodeSelectIdx_ = 0;
                hostMapSelectIdx_ = 0;
                lobbySettings_.isPvp            = false;
                lobbySettings_.friendlyFire     = false;
                lobbySettings_.pvpEnabled       = false;
                lobbySettings_.teamCount        = 0;
                lobbySettings_.upgradesShared   = false;
                lobbySettings_.mapWidth         = config_.mapWidth;
                lobbySettings_.mapHeight        = config_.mapHeight;
                lobbySettings_.enemyHpScale     = config_.enemyHpScale;
                lobbySettings_.enemySpeedScale  = config_.enemySpeedScale;
                lobbySettings_.spawnRateScale   = config_.spawnRateScale;
                lobbySettings_.playerMaxHp      = config_.playerMaxHp;
                lobbySettings_.livesPerPlayer   = 0;
                lobbySettings_.livesShared      = false;
                lobbySettings_.crateInterval    = 25.0f;
                lobbySettings_.pvpMatchDuration = 300.0f;
                lobbySettings_.waveCount        = 0;
                lobbySettings_.maxPlayers       = hostMaxPlayers_;
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
        if (mpUsernameTyping_ || portTyping_ || hostPasswordTyping_) {
            // Editing consumes events; nothing else to do
        } else {
        constexpr int HOST_SETUP_ITEMS = 14;
        auto syncHostSetupRules = [this]() {
            lobbySettings_.maxPlayers = hostMaxPlayers_;
            if (lobbySettings_.isPvp) {
                currentRules_ = (lobbySettings_.teamCount >= 2)
                    ? createTeamDeathmatchRules(lobbySettings_.teamCount, 20, hostMaxPlayers_)
                    : createDeathmatchRules(20, hostMaxPlayers_);
            } else {
                currentRules_ = createCoopArenaRules(hostMaxPlayers_);
            }
            currentRules_.friendlyFire   = lobbySettings_.friendlyFire;
            currentRules_.pvpEnabled     = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
            currentRules_.upgradesShared = lobbySettings_.upgradesShared;
            currentRules_.teamCount      = lobbySettings_.teamCount;
            currentRules_.lives          = lobbySettings_.livesPerPlayer;
            currentRules_.sharedLives    = lobbySettings_.livesShared;

            auto& net = NetworkManager::instance();
            if (lobbySettings_.isPvp) {
                net.setGamemode(lobbySettings_.teamCount >= 2 ? "team_deathmatch" : "deathmatch");
            } else {
                net.setGamemode("coop_arena");
            }
        };

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ >= HOST_SETUP_ITEMS) menuSelection_ = HOST_SETUP_ITEMS - 1;
        hostSetupSelection_ = menuSelection_;

        if (backInput_ || pauseInput_) { state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0; }

        if (hostSetupSelection_ == 0) {
            if (leftInput_)  hostMaxPlayers_ = std::max(2, hostMaxPlayers_ - 1);
            if (rightInput_) hostMaxPlayers_ = std::min(128, hostMaxPlayers_ + 1);
        }
        else if (hostSetupSelection_ == 1 && confirmInput_) {
            portStr_ = std::to_string(hostPort_);
            portTyping_ = true;
            softKB_.open("0123456789", 10, &portStr_, 5, [this](bool) {
                portTyping_ = false;
                int v = portStr_.empty() ? 7777 : std::stoi(portStr_);
                hostPort_ = std::max(1024, std::min(65535, v));
                portStr_ = std::to_string(hostPort_);
            });
        }
        else if (hostSetupSelection_ == 2 && confirmInput_) {
            mpUsernameTyping_ = true;
            softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", 16,
                         &config_.username, 32, [this](bool) {
                mpUsernameTyping_ = false;
                if (config_.username.empty()) config_.username = "Player";
                NetworkManager::instance().setUsername(config_.username);
            });
        }
        else if (hostSetupSelection_ == 3 && confirmInput_) {
            hostPasswordTyping_ = true;
            softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-!@#$", 16,
                         &lobbyPassword_, 32, [this](bool) {
                hostPasswordTyping_ = false;
            });
        }

        if (hostSetupSelection_ == 4 && (leftInput_ || rightInput_ || confirmInput_)) {
            lobbySettings_.isPvp = !lobbySettings_.isPvp;
        }
        if (hostSetupSelection_ == 5 && (leftInput_ || rightInput_ || confirmInput_)) {
            int count = (int)mapFiles_.size();
            if (count > 0) {
                int dir = rightInput_ ? 1 : -1;
                if (confirmInput_ && !leftInput_ && !rightInput_) dir = 1;
                hostMapSelectIdx_ += dir;
                if (hostMapSelectIdx_ < 0) hostMapSelectIdx_ = count;
                if (hostMapSelectIdx_ > count) hostMapSelectIdx_ = 0;
            }
        }
        if (hostSetupSelection_ == 6 && (leftInput_ || rightInput_ || confirmInput_)) {
            int dir = rightInput_ ? 1 : -1;
            if (confirmInput_ && !leftInput_ && !rightInput_) dir = 1;
            int tc = lobbySettings_.teamCount;
            if (dir > 0) tc = (tc == 0) ? 2 : (tc == 2) ? 4 : 0;
            else         tc = (tc == 0) ? 4 : (tc == 4) ? 2 : 0;
            lobbySettings_.teamCount = tc;
        }
        if (hostSetupSelection_ == 7) {
            if (leftInput_)  lobbySettings_.playerMaxHp = std::max(1, lobbySettings_.playerMaxHp - 1);
            if (rightInput_) lobbySettings_.playerMaxHp = std::min(100, lobbySettings_.playerMaxHp + 1);
        }
        if (hostSetupSelection_ == 8) {
            if (leftInput_)  lobbySettings_.livesPerPlayer = std::max(0, lobbySettings_.livesPerPlayer - 1);
            if (rightInput_) lobbySettings_.livesPerPlayer = std::min(100, lobbySettings_.livesPerPlayer + 1);
            if (lobbySettings_.livesPerPlayer == 0) lobbySettings_.livesShared = false;
        }
        if (hostSetupSelection_ == 9) {
            int dir = rightInput_ ? 1 : -1;
            if (lobbySettings_.isPvp) {
                if (leftInput_ || rightInput_)
                    lobbySettings_.pvpMatchDuration = std::max(0.0f, std::min(3600.0f, lobbySettings_.pvpMatchDuration + dir * 30.0f));
            } else {
                if (leftInput_ || rightInput_) {
                    int step = (lobbySettings_.waveCount >= 10) ? 10 : 1;
                    lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * step));
                }
            }
        }
        if (hostSetupSelection_ == 10 && (leftInput_ || rightInput_ || confirmInput_)) {
            bool forceOn = lobbySettings_.isPvp && lobbySettings_.teamCount == 0;
            if (!forceOn) {
                lobbySettings_.friendlyFire = !lobbySettings_.friendlyFire;
            }
        }
        if (hostSetupSelection_ == 11) {
            if ((leftInput_ || rightInput_) && !serverPresets_.empty()) {
                int n = (int)serverPresets_.size();
                if (leftInput_)  presetSelection_ = (presetSelection_ - 1 + n) % n;
                if (rightInput_) presetSelection_ = (presetSelection_ + 1) % n;
            }
            if (confirmInput_ && !serverPresets_.empty()) {
                applyServerPreset(presetSelection_);
            }
        }

        syncHostSetupRules();

        if (confirmInput_) {
            if (hostSetupSelection_ == 12) {
                hostGame();
            }
            else if (hostSetupSelection_ == 13) {
                state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
            }
        }
        } // end !mpUsernameTyping_
    }
    else if (state_ == GameState::JoinMenu) {
        if (!ipTyping_ && !mpUsernameTyping_ && !joinPortTyping_ && !joinPasswordTyping_) {
            if (menuSelection_ < 0) menuSelection_ = 0;
            if (menuSelection_ > 6) menuSelection_ = 6;
            joinMenuSelection_ = menuSelection_;

            if (backInput_ || pauseInput_) {
                connectStatus_.clear();
                state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
            }
            if (confirmInput_) {
                if (joinMenuSelection_ == 0) {
                    // Edit address
                    ipTyping_ = true;
                    softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-:", 11, &joinAddress_, 64, [this](bool confirmed) {
                        ipTyping_ = false;
                        if (confirmed && !joinAddress_.empty()) joinGame();
                    });
                } else if (joinMenuSelection_ == 1) {
                    // Edit port
                    joinPortStr_ = std::to_string(joinPort_);
                    joinPortTyping_ = true;
                    softKB_.open("0123456789", 10, &joinPortStr_, 5, [this](bool) {
                        joinPortTyping_ = false;
                        int v = joinPortStr_.empty() ? 7777 : std::stoi(joinPortStr_);
                        joinPort_ = std::max(1024, std::min(65535, v));
                        joinPortStr_ = std::to_string(joinPort_);
                    });
                } else if (joinMenuSelection_ == 2) {
                    // Edit username
                    mpUsernameTyping_ = true;
                    softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", 16,
                                 &config_.username, 32, [this](bool) {
                        mpUsernameTyping_ = false;
                        if (config_.username.empty()) config_.username = "Player";
                        NetworkManager::instance().setUsername(config_.username);
                    });
                } else if (joinMenuSelection_ == 3) {
                    // Edit password
                    joinPasswordTyping_ = true;
                    softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-!@#$", 16,
                                 &joinPassword_, 32, [this](bool) {
                        joinPasswordTyping_ = false;
                    });
                } else if (joinMenuSelection_ == 4) {
                    // Connect
                    joinGame();
                } else if (joinMenuSelection_ == 5) {
                    // Save server
                    addSavedServer(joinAddress_, joinAddress_, joinPort_);
                    connectStatus_ = "Server saved!";
                } else if (joinMenuSelection_ == 6) {
                    // Back
                    connectStatus_.clear();
                    state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
                }
            }
        }
        // (while ipTyping_, joinPortTyping_, mpUsernameTyping_, or joinPasswordTyping_, events are consumed above)
    }
    else if (state_ == GameState::Lobby) {
        auto& net = NetworkManager::instance();

#ifndef __SWITCH__
        // On PC, auto-join keyboard+mouse as P1
        if (!coopSlots_[0].joined) {
            coopSlots_[0].joined = true;
            coopSlots_[0].joyInstanceId = -1;
        }
        coopSlots_[0].username = config_.username;
#endif
        int localSubPlayers = 0;
        for (int i = 1; i < 4; i++) if (coopSlots_[i].joined) localSubPlayers++;
        if (localSubPlayers != lobbySubPlayersSent_) {
            net.setLocalSubPlayers((uint8_t)localSubPlayers);
            lobbySubPlayersSent_ = localSubPlayers;
        }

        bool canManageLobby = net.isLobbyHost();

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

        // ── Lobby kick mode (host only: TAB/Y toggles, LEFT/RIGHT selects, A kicks) ─────
        if (canManageLobby && tabInput_) {
            lobbyKickCursor_ = (lobbyKickCursor_ < 0) ? 0 : -1;
        }
        if (lobbyKickCursor_ >= 0) {
            // Navigate the vertical player list with UP/DOWN (menuSelection_ delta)
            int pCount = (int)net.players().size();
            if (pCount > 0) {
                int menuDelta = menuSelection_ - lobbySettingsSel_;
                if (menuDelta != 0) {
                    lobbyKickCursor_ = ((lobbyKickCursor_ + menuDelta) % pCount + pCount) % pCount;
                }
                lobbyKickCursor_ = std::max(0, std::min(pCount - 1, lobbyKickCursor_));
            }
            // Restore menuSelection_ so the lobby settings cursor doesn't move
            menuSelection_ = lobbySettingsSel_;

            if (confirmInput_ && pCount > 0) {
                // A button = kick
                uint8_t tid = net.players()[lobbyKickCursor_].id;
                if (tid != net.lobbyHostId())
                    net.sendAdminKick(tid);
                lobbyKickCursor_ = -1;
                confirmInput_ = false;
            }
            if (bombInput_ && pCount > 0) {
                // X button = transfer lobby host permissions
                uint8_t tid = net.players()[lobbyKickCursor_].id;
                if (tid != net.lobbyHostId()) net.sendLobbyHostTransfer(tid);
                lobbyKickCursor_ = -1;
                bombInput_ = false;
            }
            if (backInput_) { lobbyKickCursor_ = -1; backInput_ = false; }
            // Consume left/right so settings don't change while in kick mode
            leftInput_ = false; rightInput_ = false;
        }

        if (backInput_ || pauseInput_) {
            net.disconnect();
            connectStatus_.clear();
            lobbyKickCursor_ = -1;
            bannedPlayerIds_.clear();
            for (int i = 0; i < 4; i++) coopSlots_[i] = CoopSlot{};
            lobbySubPlayersSent_ = -1;
            state_ = GameState::MultiplayerMenu; multiMenuSelection_ = 0; menuSelection_ = 0;
        }
        // Compute setting count here so we can check preset indices before the start-game confirm
        if (canManageLobby) {
            int _SC = 13;
            if (lobbySettings_.livesPerPlayer == 0) _SC = 12;
            _SC++; // CrateInterval or WaveCount
            if (lobbySettings_.isPvp) _SC++; // Match Time (PVP only)
            int _PSave = _SC;      // "Save as Preset" row index
            int _PLoad = _SC + 1;  // "Load Preset" row index
            // Intercept confirm when cursor is on a preset row (prevents starting the game)
            if (confirmInput_ && lobbyKickCursor_ < 0 && lobbySettingsSel_ == _PSave) {
                presetNameBuf_ = "My Preset";
                softKB_.open("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-", 16,
                             &presetNameBuf_, 32, [this](bool confirmed) {
                    if (confirmed && !presetNameBuf_.empty())
                        addServerPreset(presetNameBuf_, "arena", lobbySettings_.maxPlayers, hostPort_, 0, lobbySettings_);
                });
                confirmInput_ = false;
            } else if (confirmInput_ && lobbyKickCursor_ < 0 && lobbySettingsSel_ == _PLoad) {
                if (!serverPresets_.empty()) {
                    applyServerPreset(presetSelection_);
                    net.sendConfigSync(lobbySettings_);
                }
                confirmInput_ = false;
            }
        }
        if (confirmInput_ && lobbyKickCursor_ < 0) {
            if (canManageLobby) {
                if (net.isHost()) startMultiplayerGame();
                else net.requestStartGame();
            } else {
                // Only allow ready-up once actually connected to lobby
                if (net.state() == NetState::InLobby) {
                    lobbyReady_ = !lobbyReady_;
                    net.setReady(lobbyReady_);
                }
            }
        }
        // Host can adjust lobby settings with UP/DOWN/LEFT/RIGHT
        if (canManageLobby) {
            // Settings row list:
            //   0=Gamemode, 1=FriendlyFire, 2=Upgrades, 3=Map, 4=MapWidth, 5=MapHeight,
            //   6=EnemyHP, 7=EnemySpeed, 8=SpawnRate, 9=PlayerHP,
            //   10=TeamCount, 11=Lives, 12=LivesMode, 13=CrateInterval(PVP)/WaveCount(PVE)
            // Compute effective setting count based on current mode
            int SETTING_COUNT = 13; // base: 0-12 (includes Map selector)
            if (lobbySettings_.livesPerPlayer == 0) SETTING_COUNT = 12; // hide LivesMode
            // Add conditional setting
            if (lobbySettings_.isPvp) SETTING_COUNT++; // CrateInterval
            else SETTING_COUNT++; // WaveCount
            // Match Time: PVP only
            if (lobbySettings_.isPvp) SETTING_COUNT++; // pvpMatchDuration
            // Add preset rows (always at end)
            int presetSaveIdx = SETTING_COUNT;     // save current settings as preset
            int presetLoadIdx = SETTING_COUNT + 1; // load / apply a saved preset
            SETTING_COUNT += 2;

            if (menuSelection_ < 0) menuSelection_ = SETTING_COUNT - 1;
            if (menuSelection_ >= SETTING_COUNT) menuSelection_ = 0;
            lobbySettingsSel_ = menuSelection_;

            if (leftInput_ || rightInput_) {
                int dir = rightInput_ ? 1 : -1;
                switch (lobbySettingsSel_) {
                    case 0: // Gamemode (PVP vs PVE)
                        lobbySettings_.isPvp = !lobbySettings_.isPvp;
                        lobbySettings_.pvpEnabled   = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
                        currentRules_.pvpEnabled    = lobbySettings_.pvpEnabled;
                        break;
                    case 1: // PvP/Friendly fire — disabled when PvP-no-teams (always-on)
                        if (!(lobbySettings_.isPvp && lobbySettings_.teamCount == 0)) {
                            lobbySettings_.friendlyFire = !lobbySettings_.friendlyFire;
                            lobbySettings_.pvpEnabled   = lobbySettings_.isPvp || lobbySettings_.friendlyFire;
                            currentRules_.friendlyFire  = lobbySettings_.friendlyFire;
                            currentRules_.pvpEnabled    = lobbySettings_.pvpEnabled;
                        }
                        break;
                    case 2: // Upgrades shared
                        lobbySettings_.upgradesShared = !lobbySettings_.upgradesShared;
                        break;
                    case 3: { // Map selection (0=Generated, 1+=custom map files)
                        int nm = (int)mapFiles_.size();
                        if (nm > 0) {
                            int newIdx = lobbyMapIdx_ + dir;
                            if (newIdx < 0) newIdx = nm;   // wrap to last custom
                            if (newIdx > nm) newIdx = 0;   // wrap to Generated
                            lobbyMapIdx_ = newIdx;
                            if (lobbyMapIdx_ == 0) {
                                net.setMap("", "");
                            } else {
                                const std::string& mf = mapFiles_[lobbyMapIdx_ - 1];
                                size_t sl = mf.rfind('/'); if (sl == std::string::npos) sl = mf.rfind('\\');
                                std::string mname = (sl != std::string::npos) ? mf.substr(sl + 1) : mf;
                                size_t dot = mname.rfind('.'); if (dot != std::string::npos) mname = mname.substr(0, dot);
                                net.setMap(mf, mname);
                            }
                        }
                        break;
                    }
                    case 4: // Map width (only when generated map)
                        if (lobbyMapIdx_ == 0)
                            lobbySettings_.mapWidth = std::max(10, std::min(200, lobbySettings_.mapWidth + dir * 10));
                        break;
                    case 5: // Map height (only when generated map)
                        if (lobbyMapIdx_ == 0)
                            lobbySettings_.mapHeight = std::max(10, std::min(200, lobbySettings_.mapHeight + dir * 10));
                        break;
                    case 6: // Enemy HP (disabled in PVP)
                        if (!lobbySettings_.isPvp)
                            lobbySettings_.enemyHpScale = std::max(0.1f, std::min(5.0f, lobbySettings_.enemyHpScale + dir * 0.1f));
                        break;
                    case 7: // Enemy speed (disabled in PVP)
                        if (!lobbySettings_.isPvp)
                            lobbySettings_.enemySpeedScale = std::max(0.1f, std::min(5.0f, lobbySettings_.enemySpeedScale + dir * 0.1f));
                        break;
                    case 8: // Spawn rate (disabled in PVP)
                        if (!lobbySettings_.isPvp)
                            lobbySettings_.spawnRateScale = std::max(0.1f, std::min(5.0f, lobbySettings_.spawnRateScale + dir * 0.1f));
                        break;
                    case 9: // Player HP
                        lobbySettings_.playerMaxHp = std::max(1, std::min(100, lobbySettings_.playerMaxHp + dir));
                        break;
                    case 10: { // Team count
                        int tc = lobbySettings_.teamCount;
                        if (dir > 0) tc = (tc == 0) ? 2 : (tc == 2) ? 4 : 0;
                        else         tc = (tc == 0) ? 4 : (tc == 4) ? 2 : 0;
                        lobbySettings_.teamCount = tc;
                        currentRules_.teamCount = tc;
                        break;
                    }
                    case 11: // Lives per player (0-100)
                        lobbySettings_.livesPerPlayer = std::max(0, std::min(100, lobbySettings_.livesPerPlayer + dir));
                        break;
                    case 12: // Lives mode OR conditional setting
                        if (lobbySettings_.livesPerPlayer > 0) {
                            // LivesMode
                            lobbySettings_.livesShared = !lobbySettings_.livesShared;
                        } else if (lobbySettings_.isPvp) {
                            // CrateInterval (no livesMode visible, so 12 = crateInterval)
                            lobbySettings_.crateInterval = std::max(5.0f, std::min(120.0f, lobbySettings_.crateInterval + dir * 5.0f));
                        } else {
                            // WaveCount (no livesMode visible, so 12 = waveCount)
                            lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * (lobbySettings_.waveCount >= 10 ? 10 : 1)));
                        }
                        break;
                    case 13: // CrateInterval/WaveCount (when lives>0) OR MatchTime (when lives==0, isPvp)
                        if (lobbySettings_.livesPerPlayer > 0) {
                            // condIdx=13: CrateInterval or WaveCount
                            if (lobbySettings_.isPvp) {
                                lobbySettings_.crateInterval = std::max(5.0f, std::min(120.0f, lobbySettings_.crateInterval + dir * 5.0f));
                            } else {
                                lobbySettings_.waveCount = std::max(0, std::min(1000, lobbySettings_.waveCount + dir * (lobbySettings_.waveCount >= 10 ? 10 : 1)));
                            }
                        } else if (lobbySettings_.isPvp) {
                            // lives==0, isPvp → matchTimeIdx=13
                            lobbySettings_.pvpMatchDuration = std::max(0.0f, std::min(3600.0f, lobbySettings_.pvpMatchDuration + dir * 30.0f));
                        }
                        // else lives==0, !isPvp → presetSaveIdx=13 (no left/right effect)
                        break;
                    default:
                        // lives>0, isPvp → matchTimeIdx=14
                        if (lobbySettings_.isPvp && lobbySettings_.livesPerPlayer > 0 && lobbySettingsSel_ == 14) {
                            lobbySettings_.pvpMatchDuration = std::max(0.0f, std::min(3600.0f, lobbySettings_.pvpMatchDuration + dir * 30.0f));
                        }
                        // presetLoadIdx: cycle through presets with left/right
                        if (lobbySettingsSel_ == presetLoadIdx && !serverPresets_.empty()) {
                            int n = (int)serverPresets_.size();
                            if (leftInput_)  presetSelection_ = (presetSelection_ - 1 + n) % n;
                            if (rightInput_) presetSelection_ = (presetSelection_ + 1) % n;
                        }
                        break;
                }
                // Sync to all clients after any change
                net.sendConfigSync(lobbySettings_);
            }
        }

        // ── Chat input ──
        if (chatTyping_) {
            if (confirmInput_) {
                std::string msg(chatInputBuf_);
                if (!msg.empty() && net.isOnline()) {
                    net.sendChat(msg);
                    chatInputBuf_[0] = '\0';
                }
                chatTyping_ = false;
                confirmInput_ = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            }
            if (backInput_) {
                chatTyping_ = false;
                chatInputBuf_[0] = '\0';
                backInput_ = false;
#ifndef __SWITCH__
                SDL_StopTextInput();
#endif
            }
        } else {
            // T key starts typing (keyboard only)
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_T] && !usingGamepad_) {
                chatTyping_ = true;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
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
        bool isHostPlayer = net2.isLobbyHost();

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
        //  1 = Music Volume
        //  2 = SFX Volume
        //  3 = Change Team   (if hasTeams)
        //  next = Admin Menu  (if isHostPlayer)
        //  next = End Game   (if isHostPlayer)
        //  last = Disconnect / Back to Lobby
        int resumeIdx = 0;
        int musicIdx  = 1;
        int sfxIdx    = 2;
        int teamIdx   = hasTeams ? 3 : -1;
        int adminIdx  = isHostPlayer ? (hasTeams ? 4 : 3) : -1;
        int endGameIdx = -1;
        int nextIdx   = 3 + (hasTeams ? 1 : 0) + (isHostPlayer ? 1 : 0);
        if (isHostPlayer) { endGameIdx = nextIdx; nextIdx++; }
        // Host cannot disconnect mid-game (must use End Game instead)
        int dcIdx     = isHostPlayer ? -1 : nextIdx;
        int maxSel    = isHostPlayer ? (nextIdx - 1) : nextIdx;

        if (menuSelection_ < 0) menuSelection_ = 0;
        if (menuSelection_ > maxSel) menuSelection_ = maxSel;

        // Volume adjustment with left/right
        if (menuSelection_ == musicIdx) {
            if (leftInput_) config_.musicVolume = std::max(0, config_.musicVolume - 8);
            if (rightInput_) config_.musicVolume = std::min(128, config_.musicVolume + 8);
            Mix_VolumeMusic(config_.musicVolume);
        }
        if (menuSelection_ == sfxIdx) {
            if (leftInput_) config_.sfxVolume = std::max(0, config_.sfxVolume - 8);
            if (rightInput_) config_.sfxVolume = std::min(128, config_.sfxVolume + 8);
        }

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
                net2.sendGameEnd((uint8_t)MatchEndReason::HostEnded);
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
    else if (state_ == GameState::WinLoss) {
        // Any confirm or back → proceed to full scoreboard
        if (confirmInput_ || backInput_) {
            state_ = GameState::Scoreboard;
            menuSelection_ = 0;
        }
    }
    else if (state_ == GameState::Scoreboard) {
        if (confirmInput_ || backInput_) {
            // Return to lobby instead of main menu
            state_ = GameState::Lobby;
            menuSelection_ = 0;
            lobbySettingsScrollY_ = 0;
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
                            player_.pos = pickSpawnPos(); // team corner or random spawn
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
            lobbySettingsScrollY_ = 0;
            lobbyKickCursor_ = -1;
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
                    applyModOverrides();
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

    // Consume render()-sourced inputs so they don't cascade into the next state.
    confirmInput_ = false;
    backInput_    = false;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Update
// ═════════════════════════════════════════════════════════════════════════════

