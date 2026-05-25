// ─── netmenus.cpp ─── Multiplayer menu rendering
#include "game.h"
#include "game_internal.h"

// ═════════════════════════════════════════════════════════════════════════════
//  Multiplayer Menu Rendering
// ═════════════════════════════════════════════════════════════════════════════

void Game::renderMultiplayerMenu() {
    ui_.drawDesktop();

    // ── Left window: main actions ──
    const int padX = 14;
    const int btnH = 26;
    const int btnGap = 6;
    const int leftWinW = 300;
    const int leftWinH = UI::W98::TitleH + 14 + 3*(btnH+btnGap) + 10;
    const int leftWinX = 60;
    const int leftWinY = (SCREEN_H - leftWinH) / 2;
    ui_.drawWin98Window(leftWinX, leftWinY, leftWinW, leftWinH, "Multiplayer");

    struct MenuItem { const char* label; };
    MenuItem items[] = { {"Host Game"}, {"IP Connect"}, {"Back"} };

    int bx = leftWinX + padX;
    int by = leftWinY + UI::W98::TitleH + 14;
    for (int i = 0; i < 3; i++) {
        if (ui_.win98Button(i, items[i].label, bx, by, leftWinW - padX*2, btnH, multiMenuSelection_ == i)) {
            multiMenuSelection_ = i;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == i && !usingGamepad_) multiMenuSelection_ = i;
        by += btnH + btnGap;
    }

    // ── Right window: Saved Servers ──
    const int rightWinX = leftWinX + leftWinW + 20;
    const int rightWinW = SCREEN_W - rightWinX - 60;
    const int rightWinY = leftWinY;
    const int rightWinH = SCREEN_H - rightWinY - 60;
    ui_.drawWin98Window(rightWinX, rightWinY, rightWinW, rightWinH, "Saved Servers");

    int listY = rightWinY + UI::W98::TitleH + 10;
    int listX = rightWinX + padX;
    int listW = rightWinW - padX*2;
    const int rowH = 34;
    const int rowGap = 2;

    if (savedServers_.empty()) {
        ui_.drawText("No saved servers", listX, listY + 4, 13, UI::W98::Shadow);
        ui_.drawText("Connect to a server and save it from there.",
                     listX, listY + 22, 11, UI::W98::Shadow);
    } else {
        int maxVisible = (rightWinH - UI::W98::TitleH - 20) / (rowH + rowGap);
        if (maxVisible < 2) maxVisible = 2;
        int startIdx = 0;
        if (serverListSelection_ >= maxVisible) startIdx = serverListSelection_ - maxVisible + 1;

        for (int i = startIdx; i < (int)savedServers_.size() && (i - startIdx) < maxVisible; i++) {
            bool sel = (multiMenuSelection_ == 3 + i);
            auto& s = savedServers_[i];
            int rowTop = listY + (i - startIdx) * (rowH + rowGap);

            bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, listX, rowTop, listW, rowH);
            if (hovered && !usingGamepad_) { multiMenuSelection_ = 3 + i; sel = true; }
            if (hovered) ui_.hoveredItem = 10 + (i - startIdx);
            if (hovered && ui_.mouseClicked) { multiMenuSelection_ = 3 + i; confirmInput_ = true; }

            // Draw as a Win98 button (reuses the 3D look)
            ui_.win98Button(10 + (i - startIdx), s.name.c_str(), listX, rowTop, listW, rowH, sel);

            // Address sub-line drawn on top
            char addrBuf[128];
            snprintf(addrBuf, sizeof(addrBuf), "%s:%d", s.address.c_str(), s.port);
            ui_.drawText(addrBuf, listX + 6, rowTop + rowH - 14, 10, UI::W98::Shadow);
        }
    }

    // Username in status bar
    char uname[160];
    snprintf(uname, sizeof(uname), "Playing as: %s", config_.username.c_str());
    if (multiMenuSelection_ >= 3 && !savedServers_.empty()) {
        ui_.drawWin98StatusBar(SCREEN_H - 26, uname);
        UI::HintPair hints[] = { {UI::Action::Confirm, "Connect"}, {UI::Action::Bomb, "Delete"}, {UI::Action::Back, "Back"} };
        ui_.drawHintBar(hints, 3);
    } else {
        ui_.drawWin98StatusBar(SCREEN_H - 26, uname);
        UI::HintPair hints[] = { {UI::Action::Confirm, "Select"}, {UI::Action::Back, "Back"} };
        ui_.drawHintBar(hints, 2);
    }
}

void Game::renderHostSetup() {
    ui_.drawDesktop();

    // Overall window
    int panelW = SCREEN_W - 60;
    int panelH = SCREEN_H - 60;
    int px = 30;
    int py = 30;
    ui_.drawWin98Window(px, py, panelW, panelH, "Host Session");

    int leftW = 260;
    int rightX = px + leftW + 20;
    int rightW = panelW - leftW - 34;

    // ── Left summary panel ──
    int leftContentY = py + UI::W98::TitleH + 10;
    int leftX = px + 10;

    // Info bevel boxes
    auto drawInfoBox = [&](int x, int y, int w, const char* title, const std::string& value) {
        ui_.drawWin98Bevel(x, y, w, 38, false);
        ui_.drawText(title, x + 6, y + 3, 10, UI::W98::Shadow);
        ui_.drawText(value.c_str(), x + 6, y + 18, 14, UI::W98::Black);
    };

    std::string ip = getLocalIP();
    std::string accessLabel = lobbyPassword_.empty() ? "Open lobby" : "Password locked";
    std::string modeLabel = lobbySettings_.isPvp ? "PvP skirmish" : "Co-op survival";

    drawInfoBox(leftX, leftContentY,      leftW - 4, "LOCAL ADDRESS", ip + ":" + std::to_string(hostPort_));
    drawInfoBox(leftX, leftContentY + 46, leftW - 4, "ACCESS",        accessLabel);
    drawInfoBox(leftX, leftContentY + 92, leftW - 4, "PLAYSTYLE",     modeLabel);

    // Snapshot bevel
    int snapY = leftContentY + 146;
    ui_.drawWin98Bevel(leftX, snapY, leftW - 4, 210, false);
    ui_.drawText("SESSION SNAPSHOT", leftX + 6, snapY + 6, 11, UI::W98::Shadow);
    ui_.drawText(config_.username.c_str(), leftX + 6, snapY + 24, 16, UI::W98::Black);

    std::string mapLabel = "Generated arena";
    if (hostMapSelectIdx_ > 0 && hostMapSelectIdx_ <= (int)mapFiles_.size()) {
        mapLabel = mapFiles_[hostMapSelectIdx_ - 1];
        size_t slash = mapLabel.rfind('/'); if (slash == std::string::npos) slash = mapLabel.rfind('\\');
        if (slash != std::string::npos) mapLabel = mapLabel.substr(slash + 1);
        size_t dot = mapLabel.rfind('.'); if (dot != std::string::npos) mapLabel = mapLabel.substr(0, dot);
    }

    char summary1[96];
    const char* teamSummary = (lobbySettings_.teamCount == 4) ? "4 teams" : (lobbySettings_.teamCount == 2 ? "2 teams" : "open teams");
    snprintf(summary1, sizeof(summary1), "%d slots  %s", hostMaxPlayers_, teamSummary);
    ui_.drawText(summary1, leftX + 6, snapY + 46, 12, UI::W98::Shadow);

    std::string livesSummary = (lobbySettings_.livesPerPlayer == 0)
        ? "infinite lives"
        : std::to_string(lobbySettings_.livesPerPlayer) + " lives";
    char summary2[96]; snprintf(summary2, sizeof(summary2), "HP %d  %s", lobbySettings_.playerMaxHp, livesSummary.c_str());
    ui_.drawText(summary2, leftX + 6, snapY + 62, 12, UI::W98::Shadow);
    ui_.drawText("Map:", leftX + 6, snapY + 84, 10, UI::W98::Shadow);
    ui_.drawText(mapLabel.c_str(), leftX + 6, snapY + 98, 13, UI::W98::Black);
    ui_.drawText("Objective:", leftX + 6, snapY + 120, 10, UI::W98::Shadow);
    if (lobbySettings_.isPvp) {
        char obj[64];
        if (lobbySettings_.pvpMatchDuration <= 0.0f) snprintf(obj, sizeof(obj), "Last team alive wins");
        else snprintf(obj, sizeof(obj), "%d:%02d round timer", (int)lobbySettings_.pvpMatchDuration / 60, (int)lobbySettings_.pvpMatchDuration % 60);
        ui_.drawText(obj, leftX + 6, snapY + 134, 13, UI::W98::Black);
    } else {
        char obj[64];
        if (lobbySettings_.waveCount == 0) snprintf(obj, sizeof(obj), "Endless waves");
        else snprintf(obj, sizeof(obj), "%d-wave run", lobbySettings_.waveCount);
        ui_.drawText(obj, leftX + 6, snapY + 134, 13, UI::W98::Black);
    }
    ui_.drawText("Tip: use presets for recurring lobbies.", leftX + 6, snapY + 162, 10, UI::W98::Shadow);
    ui_.drawText("The lobby exposes full tuning after start.", leftX + 6, snapY + 176, 10, UI::W98::Shadow);

    // ── Right options panel ──
    ui_.drawWin98Bevel(rightX, py + UI::W98::TitleH + 10, rightW, panelH - UI::W98::TitleH - 20, false);
    ui_.drawText("HOST OPTIONS", rightX + 8, py + UI::W98::TitleH + 14, 14, UI::W98::Black);
    ui_.drawText("Navigate with arrows/stick. Confirm to edit.", rightX + 8, py + UI::W98::TitleH + 32, 11, UI::W98::Shadow);
    ui_.drawWin98Bevel(rightX + 4, py + UI::W98::TitleH + 48, rightW - 8, 2, false);

    // ── Row helpers ──
    // Clip rows to the right-panel area
    const int rowAreaTop = py + UI::W98::TitleH + 56;
    const int rowAreaBot = py + panelH - 70; // leave room for Start/Back buttons
    const int rowH = 28;
    const int rowGap = 2;

    auto sectionHeader = [&](const char* label, int y) {
        ui_.drawText(label, rightX + 6, y + 2, 10, UI::W98::Shadow);
        ui_.drawWin98Bevel(rightX + 80, y + 7, rightW - 88, 2, false);
    };

    auto drawOptionRow = [&](int idx, int y, const char* label, const std::string& value,
                             const char* /*hint*/, bool editable = true, bool dim = false) {
        bool sel = (hostSetupSelection_ == idx);
        SDL_Rect row = {rightX, y, rightW, rowH};
        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, row.x, row.y, row.w, row.h);
        if (hovered) ui_.hoveredItem = idx;
        if (hovered && !usingGamepad_) { hostSetupSelection_ = idx; menuSelection_ = idx; }
        if (hovered && ui_.mouseClicked) { hostSetupSelection_ = idx; menuSelection_ = idx; confirmInput_ = true; }

        // Background: silver selected, lighter silver hovered, white normal
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        Uint8 bg = dim ? 210 : (sel ? 180 : (hovered ? 220 : 232));
        SDL_SetRenderDrawColor(renderer_, bg, bg, bg, 255);
        SDL_RenderFillRect(renderer_, &row);
        // Thin bottom border
        SDL_SetRenderDrawColor(renderer_, 160, 160, 160, 255);
        SDL_Rect bot = {row.x, row.y + row.h - 1, row.w, 1};
        SDL_RenderFillRect(renderer_, &bot);
        if (sel) {
            // Dotted focus rect approximation: navy left strip
            SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
            SDL_Rect bar = {row.x, row.y, 3, row.h};
            SDL_RenderFillRect(renderer_, &bar);
        }

        SDL_Color labelCol = dim ? UI::W98::Shadow : UI::W98::Black;
        drawText(label, rightX + 8, y + 6, 13, labelCol);

        std::string shown = value;
        if (sel && editable) shown = "< " + shown + " >";
        ui_.drawTextRight(shown.c_str(), rightX + rightW - 6, y + 6, 13, dim ? UI::W98::Shadow : UI::W98::Black);
    };

    // ── Auto-scroll to keep selection visible ──
    {
        static const int ROW_REL_Y[] = { 20, 52, 84, 116, 162, 194, 226, 258, 290, 322, 354, 400, 444, 486 };
        const int VIS_H   = rowAreaBot - rowAreaTop;
        constexpr int MAX_SCR = 206;
        if (hostSetupSelection_ >= 0 && hostSetupSelection_ < 14) {
            int iy = ROW_REL_Y[hostSetupSelection_];
            if (iy < hostSetupScrollY_)                  hostSetupScrollY_ = iy;
            if (iy + rowH > hostSetupScrollY_ + VIS_H)  hostSetupScrollY_ = iy + rowH - VIS_H;
            hostSetupScrollY_ = std::max(0, std::min(MAX_SCR, hostSetupScrollY_));
        }
    }

    // ── Scissor-clip the rows ──
    SDL_Rect prevClipRect;
    SDL_RenderGetClipRect(renderer_, &prevClipRect);
    {
        SDL_Rect clip = {rightX, rowAreaTop, rightW, rowAreaBot - rowAreaTop};
        SDL_RenderSetClipRect(renderer_, &clip);
    }

    int rowY = rowAreaTop - hostSetupScrollY_;
    sectionHeader("NETWORK", rowY); rowY += 18;

    drawOptionRow(0, rowY, "Max players", std::to_string(hostMaxPlayers_), "", true); rowY += rowH + rowGap;

    std::string portDisplay = portTyping_ ? portStr_ : std::to_string(hostPort_);
    if (portTyping_) portDisplay += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
    drawOptionRow(1, rowY, "Port", portDisplay, "", false); rowY += rowH + rowGap;

    std::string userDisplay = config_.username;
    if (mpUsernameTyping_) userDisplay += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
    drawOptionRow(2, rowY, "Host name", userDisplay, "", false); rowY += rowH + rowGap;

    std::string passwordDisplay;
    if (hostPasswordTyping_) {
        passwordDisplay = lobbyPassword_;
        passwordDisplay += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
    } else if (lobbyPassword_.empty()) {
        passwordDisplay = "Open";
    } else {
        passwordDisplay = std::string(lobbyPassword_.size(), '*');
    }
    drawOptionRow(3, rowY, "Password", passwordDisplay, "", false); rowY += rowH + rowGap + 8;

    sectionHeader("MATCH", rowY); rowY += 18;

    drawOptionRow(4, rowY, "Mode",      lobbySettings_.isPvp ? "PvP" : "PvE",        "", true);  rowY += rowH + rowGap;
    drawOptionRow(5, rowY, "Map",       mapLabel,                                     "", true);  rowY += rowH + rowGap;
    drawOptionRow(6, rowY, "Teams",     teamSummary,                                  "", true);  rowY += rowH + rowGap;
    drawOptionRow(7, rowY, "Player HP", std::to_string(lobbySettings_.playerMaxHp),   "", true);  rowY += rowH + rowGap;
    drawOptionRow(8, rowY, "Lives",     livesSummary,                                 "", true);  rowY += rowH + rowGap;

    std::string objectiveLabel;
    if (lobbySettings_.isPvp) {
        if (lobbySettings_.pvpMatchDuration <= 0.0f) objectiveLabel = "Unlimited";
        else {
            char buf2[32]; snprintf(buf2, sizeof(buf2), "%d:%02d", (int)lobbySettings_.pvpMatchDuration / 60, (int)lobbySettings_.pvpMatchDuration % 60);
            objectiveLabel = buf2;
        }
    } else {
        objectiveLabel = (lobbySettings_.waveCount == 0) ? "Endless" : std::to_string(lobbySettings_.waveCount) + " waves";
    }
    drawOptionRow(9, rowY, lobbySettings_.isPvp ? "Round timer" : "Wave goal", objectiveLabel, "", true); rowY += rowH + rowGap;

    bool ffForced = lobbySettings_.isPvp && lobbySettings_.teamCount == 0;
    std::string ffLabel = ffForced ? "Forced on" : (lobbySettings_.friendlyFire ? "On" : "Off");
    drawOptionRow(10, rowY, lobbySettings_.isPvp ? "Friendly fire" : "PvP damage", ffLabel, "", !ffForced, ffForced); rowY += rowH + rowGap + 8;

    sectionHeader("PRESET", rowY); rowY += 18;
    std::string presetLabel = serverPresets_.empty() ? "No presets saved" : serverPresets_[presetSelection_ % (int)serverPresets_.size()].name;
    drawOptionRow(11, rowY, "Load preset", presetLabel, "", !serverPresets_.empty(), serverPresets_.empty()); rowY += rowH + rowGap;

    // ── Restore clip rect ──
    SDL_RenderSetClipRect(renderer_, prevClipRect.w > 0 ? &prevClipRect : nullptr);

    // ── Scrollbar (Win98 style: silver track + dark thumb) ──
    {
        constexpr int CONTENT_H = 520;
        const     int VIS_H     = rowAreaBot - rowAreaTop;
        constexpr int MAX_SCR   = 206;
        int sbX = rightX + rightW + 2;
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_, 212, 208, 200, 255);
        SDL_Rect track = {sbX, rowAreaTop, 10, VIS_H};
        SDL_RenderFillRect(renderer_, &track);
        ui_.drawWin98Bevel(sbX, rowAreaTop, 10, VIS_H, false);
        int thumbH = std::max(16, VIS_H * VIS_H / CONTENT_H);
        int thumbY = rowAreaTop + (int)((long long)hostSetupScrollY_ * (VIS_H - thumbH) / MAX_SCR);
        SDL_SetRenderDrawColor(renderer_, 212, 208, 200, 255);
        SDL_Rect thumb = {sbX, thumbY, 10, thumbH};
        SDL_RenderFillRect(renderer_, &thumb);
        ui_.drawWin98Bevel(sbX, thumbY, 10, thumbH, true);
    }

    // ── START / BACK buttons ──
    int btnAreaY = rowAreaBot + 6;
    int btnW = (rightW - 10) / 2;
    if (ui_.win98Button(12, "Start Hosting", rightX, btnAreaY, btnW, 26, hostSetupSelection_ == 12)) {
        hostSetupSelection_ = 12; menuSelection_ = 12; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 12 && !usingGamepad_) { hostSetupSelection_ = 12; menuSelection_ = 12; }

    if (ui_.win98Button(13, "Back", rightX + btnW + 10, btnAreaY, btnW, 26, hostSetupSelection_ == 13)) {
        hostSetupSelection_ = 13; menuSelection_ = 13; confirmInput_ = true;
    }
    if (ui_.hoveredItem == 13 && !usingGamepad_) { hostSetupSelection_ = 13; menuSelection_ = 13; }

    if (softKB_.active) {
        renderSoftKB(py + panelH - 44);
    }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Navigate with arrows/stick  |  Enter: Edit/Apply  |  Esc: Cancel");
}

void Game::renderJoinMenu() {
    ui_.drawDesktop();

    const int padX = 14;
    const int fieldH = 40;
    const int fieldGap = 8;
    const int btnH = 26;
    const int btnGap = 6;
    const int winW = 440;

    // Calculate window height: TitleH + pad + status(20) + 4 fields + sep + 3 btns + pad
    const int winH = UI::W98::TitleH + 10
                   + 20 + 6                          // status line
                   + 4 * (fieldH + fieldGap) + 6     // four input fields
                   + 2 + 8                            // separator
                   + 3 * (btnH + btnGap) + 10;        // three buttons
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Join Game");

    const int fieldW = winW - padX * 2;
    const int fieldX = winX + padX;

    // Connection status
    auto& net = NetworkManager::instance();
    int statusY = winY + UI::W98::TitleH + 10;
    if (net.state() == NetState::Connecting) {
        ui_.drawTextCentered("Connecting...", statusY, 14, UI::W98::Shadow);
    } else if (!connectStatus_.empty()) {
        bool isSaved = connectStatus_.find("saved") != std::string::npos ||
                       connectStatus_.find("Saved") != std::string::npos;
        SDL_Color sc = isSaved ? SDL_Color{0,128,0,255} : SDL_Color{180,0,0,255};
        ui_.drawTextCentered(connectStatus_.c_str(), statusY, 13, sc);
    }

    int fieldY = statusY + 26;

    auto drawField = [&](int idx, const char* label, const std::string& value,
                         bool editing, bool password = false) {
        bool focused = (joinMenuSelection_ == idx);
        // Build display text with cursor blink
        std::string display = value;
        if (editing) display += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
        ui_.drawText(label, fieldX, fieldY, 11, UI::W98::Shadow);
        ui_.drawWin98TextField(fieldX, fieldY + 13, fieldW, fieldH - 13,
                               display.c_str(), focused, password, gameTime_);

        bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, fieldX, fieldY, fieldW, fieldH);
        if (hovered && !usingGamepad_) { menuSelection_ = idx; joinMenuSelection_ = idx; }
        if (hovered) ui_.hoveredItem = idx;
        if (hovered && ui_.mouseClicked) { menuSelection_ = idx; joinMenuSelection_ = idx; confirmInput_ = true; }

        fieldY += fieldH + fieldGap;
    };

    drawField(0, "IP / Host (e.g. 192.168.1.10 or play.example.com)", joinAddress_, ipTyping_);

    {
        std::string pStr = joinPortTyping_ ? joinPortStr_ : std::to_string(joinPort_);
        drawField(1, "Port", pStr, joinPortTyping_);
    }

    drawField(2, "Username", config_.username, mpUsernameTyping_);

    {
        std::string pwDisp;
        if (joinPasswordTyping_) pwDisp = joinPassword_;
        else if (!joinPassword_.empty()) pwDisp = std::string(joinPassword_.size(), '*');
        drawField(3, "Password (leave blank if none)", pwDisp, joinPasswordTyping_, true);
    }

    if (softKB_.active) {
        renderSoftKB(fieldY + 6);
    } else {
        ui_.drawWin98Bevel(fieldX, fieldY, fieldW, 2, false);
        fieldY += 8;

        if (ui_.win98Button(4, "Connect", fieldX, fieldY, fieldW, btnH, joinMenuSelection_ == 4)) {
            menuSelection_ = 4; joinMenuSelection_ = 4; confirmInput_ = true;
        }
        if (ui_.hoveredItem == 4 && !usingGamepad_) { menuSelection_ = 4; joinMenuSelection_ = 4; }
        fieldY += btnH + btnGap;

        if (ui_.win98Button(5, "Save Server", fieldX, fieldY, fieldW, btnH, joinMenuSelection_ == 5)) {
            menuSelection_ = 5; joinMenuSelection_ = 5; confirmInput_ = true;
        }
        if (ui_.hoveredItem == 5 && !usingGamepad_) { menuSelection_ = 5; joinMenuSelection_ = 5; }
        fieldY += btnH + btnGap;

        if (ui_.win98Button(6, "Back", fieldX, fieldY, fieldW, btnH, joinMenuSelection_ == 6)) {
            menuSelection_ = 6; joinMenuSelection_ = 6; confirmInput_ = true;
        }
        if (ui_.hoveredItem == 6 && !usingGamepad_) { menuSelection_ = 6; joinMenuSelection_ = 6; }

        ui_.drawWin98StatusBar(SCREEN_H - 26, "Enter connection details then click Connect");
    }
}

void Game::renderLobby() {
    ui_.drawDesktop();

    auto& net = NetworkManager::instance();
    // Color aliases used in player-list and kick-mode rows below
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {120, 120, 130, 255};
    SDL_Color green  = {50, 255, 100, 255};
    SDL_Color dimGrn = {30, 160, 60, 255};
    SDL_Color yellow = {255, 220, 60, 255};
    SDL_Color red    = {255, 80, 80, 255};
    (void)white; (void)gray; (void)green; (void)dimGrn; (void)yellow; (void)red;

    // If still connecting (client), show a simple Win98 connecting window
    if (!net.isHost() && net.state() == NetState::Connecting) {
        const int cwinW = 400, cwinH = 160;
        const int cwinX = (SCREEN_W - cwinW) / 2;
        const int cwinY = (SCREEN_H - cwinH) / 2;
        ui_.drawWin98Window(cwinX, cwinY, cwinW, cwinH, "Connecting...");

        float remaining = connectTimer_;
        if (remaining < 0) remaining = 0;

        int dots = ((int)(gameTime_ * 3)) % 4;
        char dotBuf[8] = "";
        for (int i = 0; i < dots; i++) strcat(dotBuf, ".");
        char statusBuf[128];
        snprintf(statusBuf, sizeof(statusBuf), "Connecting to %s:%d%s", joinAddress_.c_str(), joinPort_, dotBuf);
        ui_.drawTextCentered(statusBuf, cwinY + UI::W98::TitleH + 14, 13, UI::W98::Black);

        char timBuf[64];
        snprintf(timBuf, sizeof(timBuf), "Timeout in %.1fs", remaining);
        ui_.drawTextCentered(timBuf, cwinY + UI::W98::TitleH + 34, 12, UI::W98::Shadow);

        // Progress bar as a Win98 bevel trough
        const int barX = cwinX + 20, barY = cwinY + UI::W98::TitleH + 60;
        const int barW = cwinW - 40, barH = 14;
        ui_.drawWin98Bevel(barX, barY, barW, barH, false);
        float prog = 1.0f - (remaining / 5.0f);
        if (prog < 0) prog = 0; if (prog > 1) prog = 1;
        SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
        SDL_Rect fill = {barX + 2, barY + 2, (int)((barW - 4) * prog), barH - 4};
        SDL_RenderFillRect(renderer_, &fill);

        ui_.drawWin98StatusBar(SCREEN_H - 26, "Press Esc / B to cancel");
        { UI::HintPair hints[] = { {UI::Action::Back, "Cancel"} };
          ui_.drawHintBar(hints, 1, SCREEN_H - 40); }
        return;
    }

    // ── Main lobby window ──
    const int winX = 30, winY = 30;
    const int winW = SCREEN_W - 60, winH = SCREEN_H - 60;
    ui_.drawWin98Window(winX, winY, winW, winH, "Lobby");

    int readyPlayers = 0;
    for (const auto& p : net.players()) if (p.ready || p.id == net.lobbyHostId()) readyPlayers++;

    std::string hostName = net.lobbyInfo().hostName.empty() ? config_.username : net.lobbyInfo().hostName;
    std::string modeSummary = lobbySettings_.isPvp ? "PvP" : "PvE";
    if (lobbySettings_.teamCount == 2) modeSummary += " - 2 teams";
    else if (lobbySettings_.teamCount == 4) modeSummary += " - 4 teams";
    else modeSummary += " - open";

    std::string mapSummary = (lobbyMapIdx_ == 0) ? "Generated arena" : net.lobbyInfo().mapName;
    if (mapSummary.empty()) mapSummary = "Generated arena";
    std::string accessSummary = lobbyPassword_.empty() ? "Open" : "Password";
    std::string readySummary = std::to_string(readyPlayers) + "/" + std::to_string((int)net.players().size()) + " ready  " + accessSummary;

    // Info badges as bevel boxes across the top of the window
    int badgeY = winY + UI::W98::TitleH + 8;
    int badgeH = 36;
    int badgeGap = 6;
    int badgeW = (winW - 28 - badgeGap * 3) / 4;
    int badgeX = winX + 14;

    auto drawBadge = [&](int x, const char* title, const std::string& value) {
        ui_.drawWin98Bevel(x, badgeY, badgeW, badgeH, false);
        ui_.drawText(title, x + 6, badgeY + 3, 10, UI::W98::Shadow);
        ui_.drawText(value.c_str(), x + 6, badgeY + 17, 13, UI::W98::Black);
    };
    drawBadge(badgeX,                              "HOST",  hostName);
    drawBadge(badgeX + (badgeW + badgeGap),        "MODE",  modeSummary);
    drawBadge(badgeX + (badgeW + badgeGap) * 2,   "MAP",   mapSummary);
    drawBadge(badgeX + (badgeW + badgeGap) * 3,   "READY", readySummary);

    // ══════════════════════════════════════════════════════════
    //  Settings panel (left side) — host can adjust, clients read-only
    // ══════════════════════════════════════════════════════════
    {
        bool isHostPlayer = net.isLobbyHost();
        int panelX = winX + 14;
        int panelY = winY + UI::W98::TitleH + badgeH + 16;
        int panelW = winW / 2 - 20;
        int panelH = winH - (panelY - winY) - 50;

        ui_.drawWin98Bevel(panelX, panelY, panelW, panelH, false);
        ui_.drawText("SETTINGS", panelX + 6, panelY + 4, 12, UI::W98::Black);
        const char* settHint = isHostPlayer
            ? "You can tweak these live before starting."
            : "Read-only - host controls these.";
        ui_.drawText(settHint, panelX + 74, panelY + 6, 10, UI::W98::Shadow);
        ui_.drawWin98Bevel(panelX + 4, panelY + 22, panelW - 8, 2, false);

        int rowStep = 26;
        int rowsVisH = panelH - 32;
        int clipTop  = panelY + 28;
        int clipBot  = panelY + panelH - 4;

        // ── Auto-scroll to keep selection visible ──
        {
            int rowTop = 28 + lobbySettingsSel_ * rowStep;
            if (rowTop < lobbySettingsScrollY_)
                lobbySettingsScrollY_ = rowTop;
            if (rowTop + rowStep > lobbySettingsScrollY_ + rowsVisH)
                lobbySettingsScrollY_ = rowTop + rowStep - rowsVisH;
            lobbySettingsScrollY_ = std::max(0, lobbySettingsScrollY_);
        }

        int rowY = panelY + 28 - lobbySettingsScrollY_;

        SDL_Rect prevClip;
        SDL_RenderGetClipRect(renderer_, &prevClip);
        {
            SDL_Rect rowClip = {panelX, clipTop, panelW, clipBot - clipTop};
            SDL_RenderSetClipRect(renderer_, &rowClip);
        }

        auto drawSettingRow = [&](int idx, const char* label, const char* value, SDL_Color /*valColor*/ = {255,255,255,255}) {
            bool sel = isHostPlayer && (lobbySettingsSel_ == idx);

            if (isHostPlayer) {
                bool rowVisible = (rowY >= clipTop - rowStep) && (rowY < clipBot);
                bool hovered = rowVisible && ui_.pointInRect(ui_.mouseX, ui_.mouseY, panelX + 2, rowY, panelW - 4, rowStep - 2);
                if (hovered) ui_.hoveredItem = 100 + idx;
                if (hovered && !usingGamepad_) { lobbySettingsSel_ = idx; menuSelection_ = idx; }
                if (hovered && ui_.mouseClicked) { lobbySettingsSel_ = idx; menuSelection_ = idx; confirmInput_ = true; }
            }

            // Row bg
            if (sel) {
                SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
                SDL_Rect bar = {panelX + 2, rowY, 3, rowStep - 2};
                SDL_RenderFillRect(renderer_, &bar);
                SDL_SetRenderDrawColor(renderer_, 200, 200, 220, 255);
                SDL_Rect bg = {panelX + 5, rowY, panelW - 7, rowStep - 2};
                SDL_RenderFillRect(renderer_, &bg);
            }

            char buf[128];
            if (sel) snprintf(buf, sizeof(buf), "%s  < %s >", label, value);
            else      snprintf(buf, sizeof(buf), "%s  %s",    label, value);
            SDL_Color lc = sel ? UI::W98::Black : UI::W98::Shadow;
            drawText(buf, panelX + 8, rowY + 4, sel ? 13 : 12, lc);
            rowY += rowStep;
        };

        // 0: Gamemode (PVP vs PVE)
        {
            const char* val = lobbySettings_.isPvp ? "PVP" : "PVE";
            drawSettingRow(0, "Gamemode:", val);
        }

        // 1: PvP toggle — shown in PvE mode as "PvP" (player damage), in PvP team mode as "Friendly Fire",
        //    greyed-out (implicit ON) in PvP no-teams mode.
        {
            bool pvpNoTeams = lobbySettings_.isPvp && lobbySettings_.teamCount == 0;
            if (pvpNoTeams) {
                // Greyed out — PvP is always-on in this mode
                char buf2[128]; snprintf(buf2, sizeof(buf2), "PvP:  ON");
                drawText(buf2, panelX + 4, rowY, 14, SDL_Color{55, 55, 65, 255});
                rowY += rowStep;
            } else if (!lobbySettings_.isPvp) {
                // PvE mode — show an explicit PvP (friendly fire) toggle
                const char* val = lobbySettings_.friendlyFire ? "ON" : "OFF";
                drawSettingRow(1, "PvP:", val,
                    lobbySettings_.friendlyFire ? red : (SDL_Color){80,200,120,255});
            } else {
                // PvP team mode — show as "Friendly Fire"
                const char* val = lobbySettings_.friendlyFire ? "ON" : "OFF";
                drawSettingRow(1, "Friendly Fire:", val,
                    lobbySettings_.friendlyFire ? red : (SDL_Color){80,200,120,255});
            }
        }

        // 2: Upgrades
        {
            const char* val = lobbySettings_.upgradesShared ? "Shared (all players)" : "Individual (picker only)";
            drawSettingRow(2, "Upgrades:", val);
        }

        // 3: Map selection (Generated or custom map file)
        {
            std::string mapLabel = "Generated";
            if (lobbyMapIdx_ > 0 && lobbyMapIdx_ <= (int)mapFiles_.size()) {
                std::string mf = mapFiles_[lobbyMapIdx_ - 1];
                size_t sl = mf.rfind('/'); if (sl == std::string::npos) sl = mf.rfind('\\');
                std::string base = (sl != std::string::npos) ? mf.substr(sl + 1) : mf;
                size_t dot = base.rfind('.'); if (dot != std::string::npos) base = base.substr(0, dot);
                mapLabel = base;
            }
            if (mapFiles_.empty()) mapLabel = "Generated (no custom maps)";
            drawSettingRow(3, "Map:", mapLabel.c_str());
        }

        // 4: Map width (greyed out when custom map selected)
        {
            bool custom = (lobbyMapIdx_ > 0);
            char v[16];
            if (custom) snprintf(v, sizeof(v), "(custom)");
            else        snprintf(v, sizeof(v), "%d", lobbySettings_.mapWidth);
            SDL_Color dc = custom ? SDL_Color{55,55,65,255} : SDL_Color{255,255,255,255};
            // dim the row text manually when custom map is active
            bool saved_sel = isHostPlayer && (lobbySettingsSel_ == 4);
            if (custom && !saved_sel) {
                // draw as plain dim text, no arrows
                char buf[128]; snprintf(buf, sizeof(buf), "Map Width:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, dc);
                rowY += rowStep;
            } else {
                drawSettingRow(4, "Map Width:", v);
            }
        }

        // 5: Map height (greyed out when custom map selected)
        {
            bool custom = (lobbyMapIdx_ > 0);
            char v[16];
            if (custom) snprintf(v, sizeof(v), "(custom)");
            else        snprintf(v, sizeof(v), "%d", lobbySettings_.mapHeight);
            SDL_Color dc = custom ? SDL_Color{55,55,65,255} : SDL_Color{255,255,255,255};
            bool saved_sel = isHostPlayer && (lobbySettingsSel_ == 5);
            if (custom && !saved_sel) {
                char buf[128]; snprintf(buf, sizeof(buf), "Map Height:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, dc);
                rowY += rowStep;
            } else {
                drawSettingRow(5, "Map Height:", v);
            }
        }

        // 6: Enemy HP (greyed out in PVP)
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.enemyHpScale);
            if (lobbySettings_.isPvp) {
                char buf[128]; snprintf(buf, sizeof(buf), "Enemy HP:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, SDL_Color{55,55,65,255});
                rowY += rowStep;
            } else {
                drawSettingRow(6, "Enemy HP:", v);
            }
        }

        // 7: Enemy speed (greyed out in PVP)
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.enemySpeedScale);
            if (lobbySettings_.isPvp) {
                char buf[128]; snprintf(buf, sizeof(buf), "Enemy Speed:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, SDL_Color{55,55,65,255});
                rowY += rowStep;
            } else {
                drawSettingRow(7, "Enemy Speed:", v);
            }
        }

        // 8: Spawn rate (greyed out in PVP)
        {
            char v[16]; snprintf(v, sizeof(v), "%.1fx", lobbySettings_.spawnRateScale);
            if (lobbySettings_.isPvp) {
                char buf[128]; snprintf(buf, sizeof(buf), "Spawn Rate:  %s", v);
                drawText(buf, panelX + 4, rowY, 14, SDL_Color{55,55,65,255});
                rowY += rowStep;
            } else {
                drawSettingRow(8, "Spawn Rate:", v);
            }
        }

        // 9: Player HP
        {
            char v[16]; snprintf(v, sizeof(v), "%d", lobbySettings_.playerMaxHp);
            drawSettingRow(9, "Player HP:", v);
        }

        // 10: Team count
        {
            const char* val = (lobbySettings_.teamCount == 4) ? "4 Teams" :
                              (lobbySettings_.teamCount == 2) ? "2 Teams" : "None";
            drawSettingRow(10, "Teams:", val);
        }

        // 11: Lives per player (up to 100)
        {
            char v[16];
            if (lobbySettings_.livesPerPlayer == 0)
                snprintf(v, sizeof(v), "Infinite");
            else
                snprintf(v, sizeof(v), "%d", lobbySettings_.livesPerPlayer);
            drawSettingRow(11, "Lives:", v);
        }

        // 12: Lives mode (only shown when lives are limited)
        if (lobbySettings_.livesPerPlayer > 0) {
            const char* val = lobbySettings_.livesShared ? "Shared Pool" : "Individual";
            drawSettingRow(12, "Lives Mode:", val);
        }

        // 13 / 12: Crate Interval (PVP) or Wave Count (PVE) — index depends on whether LivesMode is visible
        {
            int condIdx = (lobbySettings_.livesPerPlayer > 0) ? 13 : 12;
            if (lobbySettings_.isPvp) {
                char v[16]; snprintf(v, sizeof(v), "%.0fs", lobbySettings_.crateInterval);
                drawSettingRow(condIdx, "Crate Interval:", v);
            } else {
                char v[16];
                if (lobbySettings_.waveCount == 0)
                    snprintf(v, sizeof(v), "Endless");
                else
                    snprintf(v, sizeof(v), "%d", lobbySettings_.waveCount);
                drawSettingRow(condIdx, "Waves:", v);
            }
            // Match Time (PVP only)
            int matchTimeIdx  = -1;
            int presetSaveIdx = condIdx + 1;
            int presetLoadIdx = condIdx + 2;
            if (lobbySettings_.isPvp) {
                matchTimeIdx  = condIdx + 1;
                presetSaveIdx = condIdx + 2;
                presetLoadIdx = condIdx + 3;
                char v[16];
                if (lobbySettings_.pvpMatchDuration <= 0.0f)
                    snprintf(v, sizeof(v), "Unlimited");
                else
                    snprintf(v, sizeof(v), "%d:%02d",
                             (int)lobbySettings_.pvpMatchDuration / 60,
                             (int)lobbySettings_.pvpMatchDuration % 60);
                drawSettingRow(matchTimeIdx, "Match Time:", v);
            }
            drawSettingRow(presetSaveIdx, "Save Preset:", "[confirm]");
            {
                const char* presetLabel = serverPresets_.empty()
                    ? "(none saved)"
                    : serverPresets_[presetSelection_ % (int)serverPresets_.size()].name.c_str();
                drawSettingRow(presetLoadIdx, "Load Preset:", presetLabel);
            }
        }

        // Restore clip rect
        SDL_RenderSetClipRect(renderer_, prevClip.w > 0 ? &prevClip : nullptr);

        // Win98-style scrollbar
        {
            int contentH = 17 * rowStep;
            if (contentH > rowsVisH) {
                int maxScroll = contentH - rowsVisH;
                int sbX = panelX + panelW - 10;
                SDL_SetRenderDrawColor(renderer_, 212, 208, 200, 255);
                SDL_Rect track = {sbX, clipTop, 10, rowsVisH};
                SDL_RenderFillRect(renderer_, &track);
                ui_.drawWin98Bevel(sbX, clipTop, 10, rowsVisH, false);
                int thumbH = std::max(14, rowsVisH * rowsVisH / contentH);
                int thumbY = clipTop + (int)((long long)lobbySettingsScrollY_ * (rowsVisH - thumbH) / maxScroll);
                SDL_SetRenderDrawColor(renderer_, 212, 208, 200, 255);
                SDL_Rect thumb = {sbX, thumbY, 10, thumbH};
                SDL_RenderFillRect(renderer_, &thumb);
                ui_.drawWin98Bevel(sbX, thumbY, 10, thumbH, true);
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Player list (right side)
    // ══════════════════════════════════════════════════════════
    int listX = winX + winW / 2 + 6;
    int listY = winY + UI::W98::TitleH + badgeH + 16;
    int listPanelW = winW / 2 - 20;
    int listPanelH = winH - (listY - winY) - 50;
    ui_.drawWin98Bevel(listX, listY, listPanelW, listPanelH, false);
    ui_.drawText("PLAYERS", listX + 6, listY + 4, 12, UI::W98::Black);
    bool canManageLobby = net.isLobbyHost();
    bool isHostInKickMode = canManageLobby && (lobbyKickCursor_ >= 0);
    if (canManageLobby) {
        const char* kickLabel = isHostInKickMode
            ? "[A] Kick  [X] Xfer Host  [B] Cancel"
            : "[Y/TAB] Host Actions";
        SDL_Color kickHintC = isHostInKickMode ? SDL_Color{180,0,0,255} : UI::W98::Shadow;
        drawText(kickLabel, listX + 74, listY + 6, 10, kickHintC);
    }
    ui_.drawWin98Bevel(listX + 4, listY + 22, listPanelW - 8, 2, false);

    // Team colors for display
    static const SDL_Color teamColors[4] = {
        {180, 0, 0, 255}, {0, 0, 180, 255}, {0, 140, 0, 255}, {160, 120, 0, 255}
    };

    const auto& players = net.players();
    int plY = listY + 28;
    for (size_t i = 0; i < players.size(); i++) {
        bool isLocal = (players[i].id == net.localPlayerId());
        bool isHostP = (players[i].id == net.lobbyHostId());
        bool isKickTarget = isHostInKickMode && ((int)i == lobbyKickCursor_);

        // Row highlight
        if (isKickTarget) {
            SDL_SetRenderDrawColor(renderer_, 220, 180, 180, 255);
            SDL_Rect row = {listX + 4, plY - 2, listPanelW - 8, 24};
            SDL_RenderFillRect(renderer_, &row);
        } else if (isLocal) {
            SDL_SetRenderDrawColor(renderer_, 200, 210, 230, 255);
            SDL_Rect row = {listX + 4, plY - 2, listPanelW - 8, 24};
            SDL_RenderFillRect(renderer_, &row);
        }

        // Ready indicator dot
        if (players[i].ready) {
            SDL_SetRenderDrawColor(renderer_, 0, 128, 0, 255);
        } else {
            SDL_SetRenderDrawColor(renderer_, 160, 160, 160, 255);
        }
        SDL_Rect dot = {listX + 8, plY + 6, 8, 8};
        SDL_RenderFillRect(renderer_, &dot);

        // Name
        char entryBuf[128];
        snprintf(entryBuf, sizeof(entryBuf), "%s%s%s", players[i].username.c_str(),
                 isHostP ? " *" : "",
                 isKickTarget ? " [X]" : "");
        SDL_Color nameC = isKickTarget ? SDL_Color{180, 0, 0, 255}
                        : isLocal      ? UI::W98::Black
                        : UI::W98::Shadow;
        if (!isKickTarget && players[i].team >= 0 && players[i].team < 4)
            nameC = teamColors[players[i].team];
        drawText(entryBuf, listX + 20, plY, 14, nameC);

        // Ping
        if (!isLocal) {
            uint32_t peerPing = net.getPlayerPing(players[i].id);
            if (peerPing > 0) {
                char pingBuf[32];
                snprintf(pingBuf, sizeof(pingBuf), "%dms", peerPing);
                SDL_Color pc = (peerPing < 50) ? SDL_Color{0, 128, 0, 255} :
                               (peerPing < 100) ? SDL_Color{160, 100, 0, 255} :
                               SDL_Color{180, 0, 0, 255};
                drawText(pingBuf, listX + listPanelW - 50, plY, 11, pc);
            }
        }

        plY += 26;

        // Sub-players
        int subCount = (int)players[i].localSubPlayers;
        if (subCount < 0) subCount = 0;
        if (subCount > 3) subCount = 3;
        for (int s = 0; s < subCount; s++) {
            std::string subLabel;
            if (isLocal) {
                int found = 0;
                for (int si = 1; si < 4; si++) {
                    if (!coopSlots_[si].joined) continue;
                    if (found == s) {
                        subLabel = coopSlots_[si].username.empty() ? ("local-" + std::to_string(s + 1)) : coopSlots_[si].username;
                        break;
                    }
                    found++;
                }
                if (subLabel.empty()) subLabel = "local-" + std::to_string(s + 1);
            } else {
                subLabel = "local-" + std::to_string(s + 1);
            }
            std::string rowText = std::string("  -> ") + subLabel;
            drawText(rowText.c_str(), listX + 20, plY - 1, 11, UI::W98::Shadow);
            plY += 16;
        }
    }

    // ── Bottom action button ──
    int btnAreaY = winY + winH - 36;
    int btnW = 200;
    int btnX = (SCREEN_W - btnW) / 2;
    if (canManageLobby) {
        bool allReady = true;
        for (auto& p : players) { if (!p.ready && p.id != net.lobbyHostId()) allReady = false; }
        if (ui_.win98Button(50, "Start Game", btnX, btnAreaY, btnW, 26, true)) {
            confirmInput_ = true;
        }
        if (!allReady && players.size() > 1) {
            ui_.drawWin98StatusBar(SCREEN_H - 26, "Waiting for everyone to ready up...");
        } else {
            ui_.drawWin98StatusBar(SCREEN_H - 26, "All ready - press Start Game!");
        }
    } else {
        const char* rdyLabel = lobbyReady_ ? "Unready" : "Ready Up";
        if (ui_.win98Button(50, rdyLabel, btnX, btnAreaY, btnW, 26, lobbyReady_)) {
            confirmInput_ = true;
        }
        ui_.drawWin98StatusBar(SCREEN_H - 26,
            lobbyReady_ ? "You are ready. Press again to unready." : "Press Ready Up when happy with the setup.");
    }
    { UI::HintPair hints[] = { {UI::Action::Back, "Leave"}, {UI::Action::Navigate, "Navigate/Adjust"} };
      ui_.drawHintBar(hints, 2, SCREEN_H - 40); }
    if (canManageLobby) {
        const char* kickHintStr = isHostInKickMode
            ? "[Y/TAB] Exit action mode    [A] Kick    [X] Transfer Host    [B] Cancel"
            : "[Y/TAB] Kick / Transfer Host mode";
        drawTextCentered(kickHintStr, SCREEN_H - 22, 12,
                         isHostInKickMode ? SDL_Color{180, 0, 0, 220} : SDL_Color{80, 80, 90, 180});
    }

    // ── Floating chat window ────────────────────────────────────────────────
    {
        const int chatW = 300, chatH = 200;

        // First-frame init: position to default, clamped to screen
        if (!chatWinInit_) {
            chatWinInit_ = true;
            chatWinX_ = 880;
            chatWinY_ = 460;
        }
        chatWinX_ = std::max(0, std::min(SCREEN_W - chatW, chatWinX_));
        chatWinY_ = std::max(0, std::min(SCREEN_H - chatH, chatWinY_));

        // Title-bar drag
        bool overTitle = ui_.pointInRect(ui_.mouseX, ui_.mouseY,
            chatWinX_, chatWinY_, chatW - 22, UI::W98::TitleH);
        if (overTitle && ui_.mouseClicked) {
            chatWinDrag_ = true;
            chatWinDragOX_ = ui_.mouseX - chatWinX_;
            chatWinDragOY_ = ui_.mouseY - chatWinY_;
        }
        if (!ui_.mouseDown) chatWinDrag_ = false;
        if (chatWinDrag_) {
            chatWinX_ = ui_.mouseX - chatWinDragOX_;
            chatWinY_ = ui_.mouseY - chatWinDragOY_;
            chatWinX_ = std::max(0, std::min(SCREEN_W - chatW, chatWinX_));
            chatWinY_ = std::max(0, std::min(SCREEN_H - chatH, chatWinY_));
        }

        ui_.drawWin98Window(chatWinX_, chatWinY_, chatW, chatH, "Chat");

        // Inner layout constants
        const int innerX   = chatWinX_ + 4;
        const int innerY   = chatWinY_ + UI::W98::TitleH + 2;
        const int inputH   = 22;
        const int fieldW   = chatW - 60;
        const int sendW    = 46;
        const int bottomY  = chatWinY_ + chatH - inputH - 4;
        const int msgAreaH = bottomY - innerY - 2;

        // Message area background
        ui_.drawWin98Bevel(innerX, innerY, chatW - 8, msgAreaH, false);

        // Clip and draw messages
        SDL_Rect prevClipChat;
        SDL_RenderGetClipRect(renderer_, &prevClipChat);
        {
            SDL_Rect msgClip = {innerX + 2, innerY + 2, chatW - 12, msgAreaH - 4};
            SDL_RenderSetClipRect(renderer_, &msgClip);
        }

        const auto& history = net.chatHistory();
        const int msgFontSz = 11;
        const int lineH = 14;
        int maxLines = (msgAreaH - 4) / lineH;
        if (maxLines < 1) maxLines = 1;
        int startMsg = (int)history.size() - maxLines;
        if (startMsg < 0) startMsg = 0;

        int msgY = innerY + msgAreaH - 4 - (int)(history.size() - startMsg) * lineH;
        for (int mi = startMsg; mi < (int)history.size(); mi++) {
            char lineBuf[128];
            snprintf(lineBuf, sizeof(lineBuf), "%s: %s",
                     history[mi].sender.c_str(), history[mi].text.c_str());
            // Truncate to fit
            std::string line = lineBuf;
            while (line.size() > 36 && !line.empty()) line.resize(line.size() - 1);
            ui_.drawText(line.c_str(), innerX + 4, msgY, msgFontSz, UI::W98::Black);
            msgY += lineH;
        }

        SDL_RenderSetClipRect(renderer_, prevClipChat.w > 0 ? &prevClipChat : nullptr);

        // Input field
        {
            std::string display = chatInputBuf_;
            if (chatTyping_) {
                display += ((int)(gameTime_ * 3.0f) % 2 == 0) ? '_' : ' ';
            }
            ui_.drawWin98TextField(innerX, bottomY, fieldW, inputH,
                                   display.c_str(), chatTyping_, false, gameTime_);

            // Send button
            if (ui_.win98Button(210, "Send", innerX + fieldW + 2, bottomY, sendW, inputH, false)) {
                std::string msg(chatInputBuf_);
                if (!msg.empty()) {
                    net.sendChat(msg);
                    chatInputBuf_[0] = '\0';
                }
                chatTyping_ = false;
            }
        }

        // Hint
        if (!chatTyping_) {
            ui_.drawText("T = Chat", chatWinX_ + chatW - 68, chatWinY_ + chatH - 14, 10, UI::W98::Shadow);
        }
    }
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

        // Health bar — drawn first so name tag renders on top
        float barW = 40.0f;
        float barH = 4.0f;
        float hpRatio = (rp.maxHp > 0) ? (float)rp.hp / rp.maxHp : 0;
        SDL_FRect bgBar = {sp.x - barW / 2, sp.y - 28, barW, barH};
        SDL_FRect fgBar = {sp.x - barW / 2, sp.y - 28, barW * hpRatio, barH};

        // Name tag — above the HP bar
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
                    // Name sits 6px above the HP bar
                    SDL_Rect nd = {(int)sp.x - ns->w / 2, (int)sp.y - 46, ns->w, ns->h};
                    SDL_RenderCopy(renderer_, nt, nullptr, &nd);
                    SDL_DestroyTexture(nt);
                    SDL_FreeSurface(ns);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, 180);
        SDL_RenderFillRectF(renderer_, &bgBar);
        Uint8 hr = (Uint8)(255 * (1.0f - hpRatio));
        Uint8 hg = (Uint8)(255 * hpRatio);
        SDL_SetRenderDrawColor(renderer_, hr, hg, 0, 220);
        SDL_RenderFillRectF(renderer_, &fgBar);
    }

    // Kill/death/score + ping — compact panel in top-right corner
    NetPlayer* local = net.localPlayer();

    // Match timer (PVP: center-top)
    if (lobbySettings_.isPvp) {
        char timeBuf[32];
        SDL_Color timerCol = {220, 220, 220, 220};
        if (lobbySettings_.pvpMatchDuration > 0.0f && matchTimer_ > 0.0f) {
            int secs = (int)matchTimer_;
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", secs / 60, secs % 60);
            // Turn red in last 30 seconds
            if (matchTimer_ < 30.0f) timerCol = {255, 80, 80, 255};
            else if (matchTimer_ < 60.0f) timerCol = {255, 200, 60, 255};
        } else if (lobbySettings_.pvpMatchDuration <= 0.0f) {
            int secs = (int)matchElapsed_;
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", secs / 60, secs % 60);
            timerCol = {160, 160, 180, 180};
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "0:00");
            timerCol = {255, 80, 80, 255};
        }
        // Subtle background
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 70);
        SDL_Rect tBg = {SCREEN_W / 2 - 50, 4, 100, 26};
        SDL_RenderFillRect(renderer_, &tBg);
        drawTextCentered(timeBuf, 6, 20, timerCol);
    }
    {
        uint32_t ping = net.getPing();
        char line1[80] = "";
        if (local) {
            snprintf(line1, sizeof(line1), "K:%d  D:%d  Score:%d",
                     local->kills, local->deaths, local->score);
        }
        SDL_Color pingColor = (ping < 50)  ? SDL_Color{50, 255, 100, 220} :
                              (ping < 100) ? SDL_Color{255, 220, 60, 220} :
                                             SDL_Color{255, 80, 80, 220};
        char line2[48];
        snprintf(line2, sizeof(line2), "%dms  |  %d players", ping, (int)players.size());

        // Subtle background panel
        int pw = 220, ph = 38, pm = 10;
        int px = SCREEN_W - pw - pm;
        int py = pm;
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 90);
        SDL_Rect panel = {px - 6, py - 4, pw + 12, ph + 8};
        SDL_RenderFillRect(renderer_, &panel);

        if (line1[0]) drawText(line1, px, py,     13, {220, 220, 220, 200});
        drawText(line2,              px, py + 20, 11, pingColor);
    }
}

void Game::renderMultiplayerPause() {
    auto& net2 = NetworkManager::instance();
    bool hasTeams     = currentRules_.teamCount >= 2;
    bool isHostPlayer = net2.isLobbyHost();

    ui_.drawDarkOverlay(160);

    // Build the item list dynamically
    struct MenuItem { const char* label; int idx; bool isVolume; };
    char musBuf[64]; snprintf(musBuf, sizeof(musBuf), "Music: %d%%", config_.musicVolume * 100 / 128);
    char sfxBuf2[64]; snprintf(sfxBuf2, sizeof(sfxBuf2), "SFX: %d%%", config_.sfxVolume * 100 / 128);

    MenuItem items[10];
    int itemCount = 0;

    items[itemCount++] = { "Resume",      0, false };
    items[itemCount++] = { musBuf,        1, true };
    items[itemCount++] = { sfxBuf2,       2, true };
    if (hasTeams)     items[itemCount++] = { "Change Team",  3, false };
    if (isHostPlayer) items[itemCount++] = { "Admin",        hasTeams ? 4 : 3, false };
    if (isHostPlayer) {
        int egIdx = 3 + (hasTeams ? 1 : 0) + 1;
        items[itemCount++] = { "End Game", egIdx, false };
    }
    if (!isHostPlayer) {
        int dcIdx = 3 + (hasTeams ? 1 : 0);
        const char* dcLabel = spectatorMode_ ? "Back to Lobby" : "Disconnect";
        items[itemCount++] = { dcLabel, dcIdx, false };
    }

    const int padX = 14;
    const int btnH = 26;
    const int btnGap = 6;
    const int winW = 320;
    const int winH = UI::W98::TitleH + 14 + itemCount * (btnH + btnGap) + 10;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;

    const char* winTitle = spectatorMode_ ? "Spectating" : "Paused";
    ui_.drawWin98Window(winX, winY, winW, winH, winTitle);

    // ── Team-pick sub-state ────────────────────────────────────────────
    if (pauseMenuSub_ == 1) {
        int tc = currentRules_.teamCount; if (tc < 2) tc = 2;
        int boxW = (winW - padX * 2 - (tc - 1) * btnGap) / tc;
        int bx0 = winX + padX;
        int by  = winY + UI::W98::TitleH + 14;
        static const char* teamNames[] = {"Team 1","Team 2","Team 3","Team 4"};
        for (int t = 0; t < tc; t++) {
            bool sel = (pauseTeamCursor_ == t);
            int bx = bx0 + t * (boxW + btnGap);
            ui_.win98Button(t, t < 4 ? teamNames[t] : "Team", bx, by, boxW, btnH, sel);
        }
        ui_.drawWin98StatusBar(SCREEN_H - 26, "Choose a team  |  Esc/B: Back");
        return;
    }

    // ── Normal menu items ──────────────────────────────────────────────
    int bx = winX + padX;
    int by = winY + UI::W98::TitleH + 14;
    for (int i = 0; i < itemCount; i++) {
        bool sel = (menuSelection_ == items[i].idx);
        const char* displayLabel = items[i].label;
        char tmp[80];
        if (sel && items[i].isVolume) {
            snprintf(tmp, sizeof(tmp), "< %s >", items[i].label);
            displayLabel = tmp;
        }
        if (ui_.win98Button(items[i].idx, displayLabel, bx, by, winW - padX * 2, btnH, sel)) {
            menuSelection_ = items[i].idx;
            confirmInput_ = true;
        }
        if (ui_.hoveredItem == items[i].idx && !usingGamepad_) menuSelection_ = items[i].idx;
        by += btnH + btnGap;
    }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Game paused");

    // Admin overlay on top
    if (adminMenuOpen_) {
        renderAdminMenu();
    }
}

void Game::renderAdminMenu() {
    auto& net2 = NetworkManager::instance();
    const auto& players = net2.players();

    const int padX = 14;
    const int rowH = 30;
    const int winW = 520;
    const int winH = UI::W98::TitleH + 10 + (int)players.size() * (rowH + 4) + 40;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - std::min(winH, SCREEN_H - 40)) / 2;
    const int clampedH = std::min(winH, SCREEN_H - 40);

    ui_.drawDarkOverlay(120);
    ui_.drawWin98Window(winX, winY, winW, clampedH, "Admin Menu");

    static const char* actionLabels[] = {"Kick", "Respawn", "Team-", "Team+"};
    const int actBtnW = 54, actBtnH = 22, actGap = 4;

    int rowY = winY + UI::W98::TitleH + 10;
    for (int i = 0; i < (int)players.size(); i++) {
        const NetPlayer& np = players[i];
        bool rowSel = (adminMenuSel_ == i);

        bool rowHovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, winX + padX, rowY, winW - padX*2, rowH);
        if (rowHovered && !usingGamepad_) adminMenuSel_ = i;

        // Row highlight
        if (rowSel) {
            SDL_SetRenderDrawColor(renderer_, 200, 210, 230, 255);
            SDL_Rect bg = {winX + padX, rowY, winW - padX*2, rowH};
            SDL_RenderFillRect(renderer_, &bg);
        }

        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "#%d  %s", np.id, np.username.c_str());
        drawText(nameBuf, winX + padX + 4, rowY + 7, 13, rowSel ? UI::W98::Black : UI::W98::Shadow);

        // Action buttons on the right of each row
        int actStartX = winX + winW - padX - 4 * (actBtnW + actGap);
        for (int a = 0; a < 4; a++) {
            bool actSel = rowSel && (adminMenuAction_ == a);
            int bx = actStartX + a * (actBtnW + actGap);
            bool btnHov = ui_.pointInRect(ui_.mouseX, ui_.mouseY, bx, rowY + 2, actBtnW, actBtnH);
            if (btnHov && !usingGamepad_) { adminMenuSel_ = i; adminMenuAction_ = a; actSel = true; }
            if (btnHov) ui_.hoveredItem = 40 + i * 4 + a;
            if (btnHov && ui_.mouseClicked) { adminMenuSel_ = i; adminMenuAction_ = a; confirmInput_ = true; }
            ui_.win98Button(40 + i * 4 + a, actionLabels[a], bx, rowY + 3, actBtnW, actBtnH, actSel);
        }
        rowY += rowH + 4;
    }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Navigate: arrows/stick  |  Confirm  |  Esc/B: Close");
}

void Game::renderMultiplayerDeath() {
    ui_.drawDarkOverlay(160, 30, 4, 4);

    const int padX = 14;
    const int winW = 340;

    // Build content height dynamically
    int contentH = 0;
    contentH += 20 + 8;
    if (currentRules_.lives > 0) contentH += 18 + 4;  // lives line
    contentH += 18 + 8;  // respawn line / button
    contentH += 14 + 2;  // separator
    contentH += 16 + 4;  // stats line

    const int winH = UI::W98::TitleH + 14 + contentH + 10;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "PACKET LOSS: 100%");

    int cy = winY + UI::W98::TitleH + 14;

    // Lives remaining
    if (currentRules_.lives > 0) {
        char livesBuf[48];
        if (localLives_ > 0)
            snprintf(livesBuf, sizeof(livesBuf), "Lives remaining: %d", localLives_);
        else
            snprintf(livesBuf, sizeof(livesBuf), "No lives left!");
        SDL_Color livesCol = (localLives_ > 1) ? UI::W98::Black : SDL_Color{180,0,0,255};
        ui_.drawTextCentered(livesBuf, cy, 14, livesCol);
        cy += 18 + 4;
    }

    // Respawn countdown or button
    float totalTime = currentRules_.respawnTime;
    if (totalTime <= 0) totalTime = 3.0f;
    float remaining = respawnTimer_;
    if (remaining < 0) remaining = 0;

    if (remaining > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Respawning in %.1f...", remaining);
        ui_.drawTextCentered(buf, cy, 13, UI::W98::Black);
        cy += 18 + 4;

        // Progress bar as bevel trough
        const int barX = winX + padX, barW = winW - padX * 2, barH = 12;
        ui_.drawWin98Bevel(barX, cy, barW, barH, false);
        float progress = 1.0f - (remaining / totalTime);
        if (progress < 0) progress = 0; if (progress > 1) progress = 1;
        SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
        SDL_Rect fgBar = {barX + 2, cy + 2, (int)((barW - 4) * progress), barH - 4};
        SDL_RenderFillRect(renderer_, &fgBar);
        cy += barH + 8;
    } else {
        if (ui_.win98Button(0, "Respawn", winX + padX, cy, winW - padX * 2, 26, true)) {
            confirmInput_ = true;
        }
        cy += 26 + 8;
    }

    // Stats
    ui_.drawWin98Bevel(winX + padX, cy, winW - padX * 2, 2, false);
    cy += 8;
    auto& net = NetworkManager::instance();
    NetPlayer* local = net.localPlayer();
    if (local) {
        float kd = (local->deaths > 0) ? (float)local->kills / local->deaths : (float)local->kills;
        char statBuf[128];
        snprintf(statBuf, sizeof(statBuf), "K:%d  D:%d  K/D:%.1f  Score:%d",
                 local->kills, local->deaths, kd, local->score);
        ui_.drawTextCentered(statBuf, cy, 12, UI::W98::Shadow);
    }

    ui_.drawWin98StatusBar(SCREEN_H - 26, "TAB - Scoreboard");
}

void Game::renderWinLoss() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Background — dark with a tinted overlay
    SDL_SetRenderDrawColor(renderer_, 4, 6, 14, 255);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer_, &full);

    const bool isWin = matchResult_.isWin;

    // Banner overlay tint — green for win, red for loss, grey for neutral
    if (matchResult_.reason == MatchEndReason::HostEnded || matchResult_.reason == MatchEndReason::Unknown) {
        SDL_SetRenderDrawColor(renderer_, 60, 60, 80, 40);
    } else if (isWin) {
        SDL_SetRenderDrawColor(renderer_, 20, 180, 60, 40);
    } else {
        SDL_SetRenderDrawColor(renderer_, 180, 20, 20, 40);
    }
    SDL_RenderFillRect(renderer_, &full);

    // Headline colour
    SDL_Color headCol = isWin
        ? SDL_Color{80, 255, 120, 255}
        : (matchResult_.reason == MatchEndReason::HostEnded || matchResult_.reason == MatchEndReason::Unknown)
            ? SDL_Color{180, 180, 200, 255}
            : SDL_Color{255, 60, 60, 255};

    SDL_Color white = {220, 220, 220, 255};
    SDL_Color gray  = {110, 110, 120, 255};
    SDL_Color cyan  = {0, 255, 228, 255};
    SDL_Color gold  = {255, 200, 50, 255};

    // Headline (VICTORY / DEFEAT / GAME OVER)
    int headY = SCREEN_H / 6;
    drawTextCentered(matchResult_.headline.c_str(), headY, 54, headCol);

    // Decorative separator
    SDL_SetRenderDrawColor(renderer_, headCol.r, headCol.g, headCol.b, 80);
    SDL_Rect sep = {SCREEN_W / 2 - 120, headY + 64, 240, 2};
    SDL_RenderFillRect(renderer_, &sep);

    // Subtitle
    if (!matchResult_.subtitle.empty()) {
        drawTextCentered(matchResult_.subtitle.c_str(), headY + 82, 20, white);
    }

    // Match duration
    {
        int secs = (int)matchResult_.matchElapsed;
        char dur[32];
        snprintf(dur, sizeof(dur), "Match time:  %d:%02d", secs / 60, secs % 60);
        drawTextCentered(dur, headY + 116, 16, gray);
    }

    // Player / team highlight block
    int blockY = headY + 158;

    if (matchResult_.reason == MatchEndReason::TimeUp || matchResult_.reason == MatchEndReason::LastAlive) {
        auto& netR = NetworkManager::instance();
        auto  players = netR.players();

        // Sort by kills descending
        std::sort(players.begin(), players.end(),
                  [](const NetPlayer& a, const NetPlayer& b){ return a.kills > b.kills; });

        const int tableW = 480;
        const int tableX = (SCREEN_W - tableW) / 2;

        // Column headers
        drawText("#",       tableX,         blockY, 13, gray);
        drawText("PLAYER",  tableX + 32,    blockY, 13, gray);
        drawText("KILLS",   tableX + 300,   blockY, 13, gray);
        drawText("STATUS",  tableX + 388,   blockY, 13, gray);
        blockY += 24;

        for (size_t i = 0; i < players.size() && i < 8; i++) {
            bool isLocal = (players[i].id == netR.localPlayerId());
            bool survived = !players[i].spectating;
            SDL_Color rc = isLocal ? gold : white;
            if (!survived) { rc.r = (Uint8)(rc.r / 2); rc.g = (Uint8)(rc.g / 2); rc.b = (Uint8)(rc.b / 2); }

            // Row bg
            if (isLocal) {
                SDL_SetRenderDrawColor(renderer_, 0, 180, 160, 20);
                SDL_Rect row = {tableX - 8, blockY - 2, tableW + 16, 24};
                SDL_RenderFillRect(renderer_, &row);
            }

            char rankBuf[4], killBuf[8];
            snprintf(rankBuf, sizeof(rankBuf), "%d", (int)i + 1);
            snprintf(killBuf, sizeof(killBuf), "%d", players[i].kills);

            drawText(rankBuf,               tableX,         blockY, 16, i == 0 ? gold : gray);
            drawText(players[i].username.c_str(), tableX + 32, blockY, 16, rc);
            drawText(killBuf,               tableX + 300,   blockY, 16, rc);
            drawText(survived ? "Alive" : "Eliminated", tableX + 388, blockY, 14,
                     survived ? SDL_Color{50, 220, 80, 255} : SDL_Color{200, 60, 60, 255});
            blockY += 26;
        }
    } else if (matchResult_.reason == MatchEndReason::WavesCleared) {
        // PVE win — show wave count
        char waveBuf[64];
        if (lobbySettings_.waveCount > 0)
            snprintf(waveBuf, sizeof(waveBuf), "Cleared %d waves!", lobbySettings_.waveCount);
        else
            snprintf(waveBuf, sizeof(waveBuf), "All enemies defeated!");
        drawTextCentered(waveBuf, blockY, 22, gold);
        blockY += 36;

        // Show all players survived
        auto& netR = NetworkManager::instance();
        drawTextCentered("All players survived", blockY, 16, {80, 220, 80, 255});
        (void)netR;
    }

    // Hint — clickable button
    if (ui_.menuItem(0, "View Full Scoreboard", SCREEN_W / 2, SCREEN_H - 44, 300, 32,
                     gray, true, 14, 16)) {
        confirmInput_ = true;
    }
    SDL_Color hintLine = isWin ? SDL_Color{50, 200, 80, 100} : SDL_Color{200, 60, 60, 100};
    SDL_SetRenderDrawColor(renderer_, hintLine.r, hintLine.g, hintLine.b, hintLine.a);
    SDL_Rect hintBar = {0, SCREEN_H - 52, SCREEN_W, 2};
    SDL_RenderFillRect(renderer_, &hintBar);
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

    if (ui_.menuItem(0, "Continue", SCREEN_W / 2, SCREEN_H - 50, 200, 32,
                     {80, 80, 90, 255}, true, 14, 16)) {
        confirmInput_ = true;
    }
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

        const std::vector<SDL_Texture*>* bodyFrames = &defaultPlayerSprites_;
        const std::vector<SDL_Texture*>* legFrames = &defaultLegSprites_;
        bool usingCustomFrames = false;
        auto syncedIt = syncedCharacters_.find(rp.id);
        if (syncedIt != syncedCharacters_.end() && !syncedIt->second.isDefault && syncedIt->second.visualLoaded) {
            if (!syncedIt->second.visual.bodySprites.empty()) {
                bodyFrames = &syncedIt->second.visual.bodySprites;
                usingCustomFrames = true;
            }
            if (!syncedIt->second.visual.legSprites.empty()) legFrames = &syncedIt->second.visual.legSprites;
        } else if (rp.customCharacter) {
            for (auto& cd : availableChars_) {
                if (cd.name != rp.characterName) continue;
                if (!cd.bodySprites.empty()) {
                    bodyFrames = &cd.bodySprites;
                    usingCustomFrames = true;
                }
                if (!cd.legSprites.empty()) legFrames = &cd.legSprites;
                break;
            }
        }

        // Position/rotation are already interpolated in network.cpp
        Vec2 drawPos = rp.pos;
        float drawRot = rp.rotation;

        const Uint8 ghostAlpha = 80;
        const bool isGhost = rp.spectating;

        // Legs
        if (rp.moving && !legFrames->empty()) {
            int idx = rp.legFrame % (int)legFrames->size();
            SDL_Texture* legTex = (*legFrames)[idx];
            if (isGhost) SDL_SetTextureAlphaMod(legTex, ghostAlpha);
            renderSprite(legTex, drawPos, rp.legRotation + (float)M_PI / 2, 1.5f);
            if (isGhost) SDL_SetTextureAlphaMod(legTex, 255);
        }

        // Body — tint by team color or default blue
        if (!bodyFrames->empty()) {
            int idx = rp.animFrame % (int)bodyFrames->size();
            SDL_Texture* bodyTex = (*bodyFrames)[idx];
            Vec2 bodyPos = drawPos + Vec2::fromAngle(drawRot) * 6.0f;
            SDL_Color tint = usingCustomFrames ? SDL_Color{255, 255, 255, 255}
                                               : SDL_Color{180, 200, 255, 255};
            if (isGhost) tint = {140, 180, 255, ghostAlpha};
            else if (rp.team >= 0 && rp.team < 4) tint = teamTints[rp.team];
            if (isGhost) SDL_SetTextureAlphaMod(bodyTex, ghostAlpha);
            renderSpriteEx(bodyTex, bodyPos, drawRot + (float)M_PI / 2, 1.5f, tint);
            if (isGhost) SDL_SetTextureAlphaMod(bodyTex, 255);
        }

        // ── Render this client's sub-players (splitscreen partners) ──
        static const SDL_Color subTints[3] = {
            {255, 220, 140, 255}, {140, 255, 180, 255}, {255, 160, 200, 255}
        };
        for (int si = 0; si < (int)rp.subPlayers.size(); si++) {
            auto& sp = rp.subPlayers[si];
            if (!sp.alive) continue;
            Vec2 spPos = sp.pos;
            float spRot = sp.rotation;
            // Legs
            if (sp.moving && !legFrames->empty()) {
                int idx = sp.legFrame % (int)legFrames->size();
                renderSprite((*legFrames)[idx], spPos, sp.legRotation + (float)M_PI / 2, 1.5f);
            }
            // Body — slightly different tint from primary
            if (!bodyFrames->empty()) {
                int idx = sp.animFrame % (int)bodyFrames->size();
                Vec2 bodyPos = spPos + Vec2::fromAngle(spRot) * 6.0f;
                SDL_Color tint = usingCustomFrames ? SDL_Color{255, 255, 255, 255}
                                                   : subTints[si % 3];
                if (rp.team >= 0 && rp.team < 4) tint = teamTints[rp.team]; // use team color if teams
                renderSpriteEx((*bodyFrames)[idx], bodyPos, spRot + (float)M_PI / 2, 1.5f, tint);
            }
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

        // Click detection on team box
        if (!teamLocked_) {
            bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, bx, boxY, boxW, boxH);
            if (hovered) ui_.hoveredItem = t;
            if (hovered && !usingGamepad_) teamSelectCursor_ = t;
            if (hovered && ui_.mouseClicked) {
                teamSelectCursor_ = t;
                confirmInput_ = true;
            }
        }

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
        { UI::HintPair hints[] = { {UI::Action::Left, "Choose"}, {UI::Action::Confirm, "Lock In"}, {UI::Action::Back, "Back"} };
          ui_.drawHintBar(hints, 3, SCREEN_H - 80); }
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
    ui_.drawDesktop();

    static const char* tabNames[] = { "Mods", "Characters", "Maps", "Playlists" };

    const int winW = 900, winH = 520;
    const int winX = (SCREEN_W - winW) / 2;
    const int winY = (SCREEN_H - winH) / 2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Mods & Content");

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

    const int pad = 14;
    int cx = winX + pad;
    int cy = winY + UI::W98::TitleH + 10;

    // ── Tab row ──
    const int tabW = 170, tabH = 26, tabGap = 2;
    for (int t = 0; t < 4; t++) {
        bool active = (t == modMenuTab_);
        int tx = cx + t * (tabW + tabGap);
        if (ui_.win98Button(70 + t, tabNames[t], tx, cy, tabW, tabH, active)) {
            modMenuTab_ = t;
            modMenuSelection_ = 0;
        }
    }
    cy += tabH + 6;

    // ── Content area (sunken list box) ──
    const int contentH = winH - UI::W98::TitleH - 10 - tabH - 6 - 50;
    ui_.drawWin98Bevel(cx, cy, winW - 2*pad, contentH, false);

    const int listX = cx + 3;
    const int listY = cy + 3;
    const int listW = winW - 2*pad - 6;
    const int listH = contentH - 6;

    auto& mm = ModManager::instance();
    const int rowH = 34;
    int maxVisible = listH / rowH;
    if (maxVisible < 3) maxVisible = 3;

    SDL_Rect clip = {listX, listY, listW, listH};
    SDL_RenderSetClipRect(renderer_, &clip);

    if (modMenuTab_ == 0) {
        // ════ Mods tab ════
        const auto& mods = mm.mods();
        if (mods.empty()) {
            SDL_RenderSetClipRect(renderer_, nullptr);
            ui_.drawText("No mods installed.", listX + 8, listY + listH/2 - 10, 14, UI::W98::Shadow);
            ui_.drawText("Place mod folders in the mods/ directory.", listX + 8, listY + listH/2 + 8, 12, UI::W98::Shadow);
        } else {
            int scrollOff = std::max(0, modMenuSelection_ - maxVisible + 1);
            for (int i = scrollOff; i < (int)mods.size() && (i - scrollOff) < maxVisible; i++) {
                auto& mod = mods[i];
                int ry = listY + (i - scrollOff) * rowH;
                bool sel = (i == modMenuSelection_);

                bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, listX, ry, listW, rowH);
                if (hovered && !usingGamepad_) { modMenuSelection_ = i; sel = true; }
                if (hovered) ui_.hoveredItem = i % 60;
                if (hovered && ui_.mouseClicked) { modMenuSelection_ = i; confirmInput_ = true; }

                if (sel) {
                    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(renderer_, UI::W98::Navy.r, UI::W98::Navy.g, UI::W98::Navy.b, 255);
                    SDL_Rect row = {listX, ry, listW, rowH};
                    SDL_RenderFillRect(renderer_, &row);
                }

                SDL_Color textC   = sel ? UI::W98::White  : UI::W98::Black;
                SDL_Color dimC    = sel ? UI::W98::Silver : UI::W98::Shadow;
                SDL_Color onColor  = sel ? UI::W98::White : SDL_Color{0, 128, 0, 255};
                SDL_Color offColor = sel ? SDL_Color{255,180,180,255} : SDL_Color{180, 0, 0, 255};

                const char* statusTxt = mod.enabled ? "[ON] " : "[OFF]";
                ui_.drawText(statusTxt, listX + 6, ry + (rowH - 14) / 2, 13,
                             mod.enabled ? onColor : offColor);

                char nameLine[256];
                snprintf(nameLine, sizeof(nameLine), "%s  v%s  \xe2\x80\x94  by %s",
                         mod.name.c_str(), mod.version.c_str(), mod.author.c_str());
                ui_.drawText(nameLine, listX + 58, ry + (rowH - 14) / 2, 13, textC);

                // Content type tags (right-aligned)
                char tags[128] = "";
                if (mod.content.characters) strcat(tags, "chars ");
                if (mod.content.maps)       strcat(tags, "maps ");
                if (mod.content.gamemodes)  strcat(tags, "modes ");
                if (mod.content.sprites)    strcat(tags, "sprites ");
                if (mod.content.sounds)     strcat(tags, "sounds ");
                if (mod.content.items)      strcat(tags, "items");
                if (tags[0]) ui_.drawText(tags, listX + listW - 220, ry + (rowH - 12) / 2, 11, dimC);
            }

            // Scrollbar
            if ((int)mods.size() > maxVisible) {
                float ratio       = (float)maxVisible / (float)mods.size();
                float scrollRatio = (mods.size() > 1) ? (float)scrollOff / (float)(mods.size() - maxVisible) : 0.f;
                int sbH   = std::max(20, (int)(listH * ratio));
                int sbY   = listY + (int)((listH - sbH) * scrollRatio);
                SDL_SetRenderDrawColor(renderer_, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
                SDL_Rect sb = {listX + listW - 5, sbY, 4, sbH};
                SDL_RenderFillRect(renderer_, &sb);
            }
        }
    } else {
        // ════ Content tabs (Characters / Maps / Playlists) ════
        std::vector<std::string> paths;
        const char* emptyMsg  = "No content";
        const char* emptyHint = "Enable mods with content in the Mods tab";
        if (modMenuTab_ == 1) { paths = mm.allCharacterPaths(); emptyMsg = "No custom characters"; }
        else if (modMenuTab_ == 2) { paths = mm.allMapPaths();  emptyMsg = "No custom maps"; }
        else if (modMenuTab_ == 3) { paths = mm.allPackPaths(); emptyMsg = "No custom playlists"; }

        if (paths.empty()) {
            SDL_RenderSetClipRect(renderer_, nullptr);
            ui_.drawText(emptyMsg,  listX + 8, listY + listH/2 - 10, 14, UI::W98::Shadow);
            ui_.drawText(emptyHint, listX + 8, listY + listH/2 + 8,  12, UI::W98::Shadow);
        } else {
            int scrollOff = std::max(0, modMenuSelection_ - maxVisible + 1);
            for (int i = scrollOff; i < (int)paths.size() && (i - scrollOff) < maxVisible; i++) {
                int ry  = listY + (i - scrollOff) * rowH;
                bool sel = (i == modMenuSelection_);

                bool hovered = ui_.pointInRect(ui_.mouseX, ui_.mouseY, listX, ry, listW, rowH);
                if (hovered && !usingGamepad_) { modMenuSelection_ = i; sel = true; }
                if (hovered) ui_.hoveredItem = i % 60;
                if (hovered && ui_.mouseClicked) { modMenuSelection_ = i; }

                if (sel) {
                    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(renderer_, UI::W98::Navy.r, UI::W98::Navy.g, UI::W98::Navy.b, 255);
                    SDL_Rect row = {listX, ry, listW, rowH};
                    SDL_RenderFillRect(renderer_, &row);
                }

                SDL_Color textC = sel ? UI::W98::White  : UI::W98::Black;
                SDL_Color dimC  = sel ? UI::W98::Silver : UI::W98::Shadow;

                std::string name = paths[i];
                auto slash = name.rfind('/');
                if (slash == std::string::npos) slash = name.rfind('\\');
                if (slash != std::string::npos) name = name.substr(slash + 1);
                ui_.drawText(name.c_str(), listX + 8, ry + 6, 13, textC);

                std::string srcDir = paths[i];
                auto slashDir = srcDir.rfind('/');
                if (slashDir != std::string::npos) srcDir = srcDir.substr(0, slashDir);
                ui_.drawText(srcDir.c_str(), listX + 8, ry + rowH - 13, 11, dimC);
            }

            if ((int)paths.size() > maxVisible) {
                float ratio       = (float)maxVisible / (float)paths.size();
                float scrollRatio = (paths.size() > 1) ? (float)scrollOff / (float)(paths.size() - maxVisible) : 0.f;
                int sbH = std::max(20, (int)(listH * ratio));
                int sbY = listY + (int)((listH - sbH) * scrollRatio);
                SDL_SetRenderDrawColor(renderer_, UI::W98::Shadow.r, UI::W98::Shadow.g, UI::W98::Shadow.b, 255);
                SDL_Rect sb = {listX + listW - 5, sbY, 4, sbH};
                SDL_RenderFillRect(renderer_, &sb);
            }
        }
    }

    SDL_RenderSetClipRect(renderer_, nullptr);

    // ── Close button ──
    const int btnY = winY + winH - 42;
    auto& modsList = mm.mods();
    bool closeSel = (modMenuTab_ == 0) ? (modMenuSelection_ >= (int)modsList.size()) : false;
    if (ui_.win98Button(62, "Close", winX + winW - pad - 86, btnY, 86, 26, closeSel)) {
        if (modMenuTab_ == 0) modMenuSelection_ = (int)modsList.size();
        confirmInput_ = true;
    }
    if (ui_.hoveredItem == 62 && !usingGamepad_) {
        if (modMenuTab_ == 0) modMenuSelection_ = (int)modsList.size();
    }

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Toggle/Select"}, {UI::Action::Back, "Close"} };
      ui_.drawHintBar(hints, 2); }
}

