#include "game.h"
#include "game_internal.h"
#include <algorithm>

// Shared toolbar/icon helpers (used across all four screens)

static void drawServerIcon(SDL_Renderer* r, int ix, int iy, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_Rect t1={ix+1,iy+8,12,6}; SDL_RenderFillRect(r,&t1);
    SDL_Rect t2={ix+3,iy+4, 8,5}; SDL_RenderFillRect(r,&t2);
    SDL_Rect t3={ix+5,iy,   4,5}; SDL_RenderFillRect(r,&t3);
    SDL_SetRenderDrawColor(r, 255,255,255,200);
    SDL_Rect ts={ix+2,iy+9,4,2};  SDL_RenderFillRect(r,&ts);
}

static void drawPersonIcon(SDL_Renderer* r, int ix, int iy, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_Rect head={ix+4,iy,  6, 6}; SDL_RenderFillRect(r,&head);
    SDL_Rect body={ix+1,iy+7,12, 7}; SDL_RenderFillRect(r,&body);
}

// Toolbar drawing macro
// Each screen defines its own tbBtn/tbSep lambdas capturing renderer_/ui_/etc.

// Multiplayer Menu

void Game::renderMultiplayerMenu() {
    ui_.drawDesktop();

    const int WX=40, WY=28, WW=SCREEN_W-80, WH=SCREEN_H-56;
    const int TH = UI::W98::TitleH;
    ui_.drawWin98Window(WX, WY, WW, WH, "Multiplayer");

    // Toolbar band
    const int TB_Y=WY+TH, TB_H=44, TBY=TB_Y+4;
    const int TBW=76, TBH=36;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect tbBg={WX,TB_Y,WW,TB_H}; SDL_RenderFillRect(renderer_,&tbBg);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, WX, TB_Y+TB_H-2, WX+WW-1, TB_Y+TB_H-2);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_RenderDrawLine(renderer_, WX, TB_Y+TB_H-1, WX+WW-1, TB_Y+TB_H-1);

    // Load toolbar icons once (48×48 Win98-style PNGs, rendered at 20×20)
    SDL_Texture* icHost = Assets::instance().loadRelTex("sprites/ui/tb_host.png");
    SDL_Texture* icJoin = Assets::instance().loadRelTex("sprites/ui/tb_join.png");
    SDL_Texture* icBack = Assets::instance().loadRelTex("sprites/ui/tb_back.png");

    // Player list icons (msagent Win98 style)
    SDL_Texture* icPlayerUser = Assets::instance().loadRelTex("sprites/ui/chat_user.png");
    SDL_Texture* icPlayerHost = Assets::instance().loadRelTex("sprites/ui/chat_host.png");

    // tbBtn: toolbar button with optional icon texture. Falls back to colored square.
    auto tbBtn = [&](int id, const char* lbl, int x, int w,
                     SDL_Texture* icon, SDL_Color fallbackC) -> bool {
        bool sel = (multiMenuSelection_==id);
        bool hov = ui_.pointInRect(ui_.mouseX, ui_.mouseY, x, TBY, w, TBH);
        if (hov) ui_.hoveredItem = id;
        if (hov && !usingGamepad_) { multiMenuSelection_=id; menuSelection_=id; sel=true; }
        SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
        SDL_Rect bg={x,TBY,w,TBH}; SDL_RenderFillRect(renderer_,&bg);
        ui_.drawWin98Bevel(x,TBY,w,TBH, !(sel&&hov));
        // Icon: 20×20 centred horizontally, 5px from top
        const int isz=20;
        int ix=x+w/2-isz/2, iy=TBY+5;
        if (icon) {
            SDL_Rect dst={ix,iy,isz,isz}; SDL_RenderCopy(renderer_,icon,nullptr,&dst);
        } else {
            SDL_SetRenderDrawColor(renderer_, fallbackC.r,fallbackC.g,fallbackC.b,255);
            SDL_Rect ic={ix,iy,isz,isz}; SDL_RenderFillRect(renderer_,&ic);
        }
        int tw=(int)(strlen(lbl)*10*0.60f);
        drawText(lbl, x+w/2-tw/2, TBY+TBH-14, 10, UI::W98::Black);
        return hov && ui_.mouseClicked;
    };
    auto tbSep = [&](int x) {
        SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
        SDL_RenderDrawLine(renderer_, x+3, TBY+5, x+3, TBY+TBH-7);
        SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
        SDL_RenderDrawLine(renderer_, x+4, TBY+5, x+4, TBY+TBH-7);
    };

    int tbX=WX+6;
    if (tbBtn(0,"Host Game", tbX, TBW,    icHost, {0,0,128,255}))
        { multiMenuSelection_=0; menuSelection_=0; confirmInput_=true; }
    tbX+=TBW+4;
    if (tbBtn(1,"IP Connect",tbX, TBW,    icJoin, {0,100,0,255}))
        { multiMenuSelection_=1; menuSelection_=1; confirmInput_=true; }
    tbX+=TBW+10; tbSep(tbX); tbX+=12;
    if (tbBtn(2,"Back",      tbX, TBW-10, icBack, {110,70,0,255}))
        { multiMenuSelection_=2; menuSelection_=2; confirmInput_=true; }

    // Content: Saved Servers list
    const int STRIP_H=22, HDR_H=20;
    const int CONT_Y=TB_Y+TB_H;
    const int LIST_H=WH-TH-TB_H-HDR_H-STRIP_H-6;  // -6 keeps bevel inside window border

    // Section header
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect hdrBg={WX,CONT_Y,WW,HDR_H}; SDL_RenderFillRect(renderer_,&hdrBg);
    drawText("Saved Servers", WX+8, CONT_Y+3, 11, UI::W98::Shadow);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, WX, CONT_Y+HDR_H-1, WX+WW-1, CONT_Y+HDR_H-1);

    // Inset list
    const int LIST_Y=CONT_Y+HDR_H;
    ui_.drawWin98Bevel(WX+4, LIST_Y, WW-8, LIST_H, false);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_Rect listBg={WX+6,LIST_Y+2,WW-12,LIST_H-4}; SDL_RenderFillRect(renderer_,&listBg);

    if (savedServers_.empty()) {
        drawText("No saved servers.", WX+14, LIST_Y+14, 12, UI::W98::Shadow);
        drawText("Connect to a server and save it - it will appear here.",
                 WX+14, LIST_Y+32, 11, {130,130,130,255});
    } else {
        const int ROW_H=28;
        int lX=WX+6, lW=WW-12, rY=LIST_Y+2;
        int maxVis=(LIST_H-4)/ROW_H;
        int startIdx=0;
        if (serverListSelection_>=maxVis) startIdx=serverListSelection_-maxVis+1;
        for (int i=startIdx; i<(int)savedServers_.size()&&(i-startIdx)<maxVis; i++) {
            bool sel=(multiMenuSelection_==3+i);
            auto& s=savedServers_[i];
            bool hov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,lX,rY,lW,ROW_H);
            if (hov) ui_.hoveredItem=10+(i-startIdx);
            if (hov&&!usingGamepad_) { multiMenuSelection_=3+i; menuSelection_=3+i; sel=true; }
            if (hov&&ui_.mouseClicked) { multiMenuSelection_=3+i; menuSelection_=3+i; confirmInput_=true; }
            if (sel)      SDL_SetRenderDrawColor(renderer_, 0,0,128,255);
            else if (hov) SDL_SetRenderDrawColor(renderer_, 180,200,240,255);
            else          SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
            SDL_Rect row={lX,rY,lW,ROW_H}; SDL_RenderFillRect(renderer_,&row);
            SDL_Color ic=sel?SDL_Color{200,220,255,255}:SDL_Color{0,0,128,255};
            drawServerIcon(renderer_, lX+5, rY+(ROW_H-14)/2, ic);
            SDL_Color nc=sel?SDL_Color{255,255,255,255}:UI::W98::Black;
            SDL_Color ac=sel?SDL_Color{200,220,255,255}:UI::W98::Shadow;
            drawText(s.name.c_str(), lX+24, rY+7, 13, nc);
            char addr[128]; snprintf(addr,sizeof(addr),"%s:%d",s.address.c_str(),s.port);
            ui_.drawTextRight(addr, lX+lW-8, rY+7, 11, ac);
            rY+=ROW_H;
        }
    }

    // Security settings strip (two checkboxes + notices)
    const int SEC_H = 46;
    const int SEC_Y = WY + WH - STRIP_H - SEC_H - 2;
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect secBg={WX,SEC_Y,WW,SEC_H}; SDL_RenderFillRect(renderer_,&secBg);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, WX,SEC_Y,WX+WW-1,SEC_Y);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_RenderDrawLine(renderer_, WX,SEC_Y+1,WX+WW-1,SEC_Y+1);
    {
        // UPnP toggle
        int cbX = WX+8, cbY1 = SEC_Y+6, cbY2 = SEC_Y+26, cbW = 13, cbH = 13;
        // Row 1: UPnP
#if HAS_UPNP
        const bool upnpAvail = true;
#else
        const bool upnpAvail = false;
#endif
        SDL_Color upnpLblC = upnpAvail ? UI::W98::Black : UI::W98::Shadow;
        ui_.drawWin98Bevel(cbX,cbY1,cbW,cbH,false);
        {
            Uint8 fill = upnpAvail ? 255 : 220;
            SDL_SetRenderDrawColor(renderer_, fill, fill, fill, 255);
        }
        SDL_Rect inner1={cbX+2,cbY1+2,cbW-4,cbH-4}; SDL_RenderFillRect(renderer_,&inner1);
        if (upnpAvail && config_.enableUpnp) {
            SDL_SetRenderDrawColor(renderer_,0,0,0,255);
            SDL_RenderDrawLine(renderer_,cbX+2,cbY1+6,cbX+5,cbY1+10);
            SDL_RenderDrawLine(renderer_,cbX+5,cbY1+10,cbX+11,cbY1+2);
        }
        if (upnpAvail && ui_.mouseClicked && ui_.pointInRect(ui_.mouseX,ui_.mouseY,cbX,cbY1,cbW+160,cbH)) {
            config_.enableUpnp = !config_.enableUpnp; saveConfig();
        }
        drawText("Enable UPnP", cbX+cbW+6, cbY1, 12, upnpLblC);
        ui_.drawTextRight(upnpAvail ? "Opens a port on your router when hosting. Use on trusted networks only."
                                    : "Not supported by the current platform.",
                          WX+WW-10, cbY1+1, 10, upnpAvail ? SDL_Color{160,80,0,255} : UI::W98::Shadow);
        // Row 2: Accept Mods
        ui_.drawWin98Bevel(cbX,cbY2,cbW,cbH,false);
        SDL_SetRenderDrawColor(renderer_,255,255,255,255);
        SDL_Rect inner2={cbX+2,cbY2+2,cbW-4,cbH-4}; SDL_RenderFillRect(renderer_,&inner2);
        if (config_.acceptMods) {
            SDL_SetRenderDrawColor(renderer_,0,0,0,255);
            SDL_RenderDrawLine(renderer_,cbX+2,cbY2+6,cbX+5,cbY2+10);
            SDL_RenderDrawLine(renderer_,cbX+5,cbY2+10,cbX+11,cbY2+2);
        }
        if (ui_.mouseClicked && ui_.pointInRect(ui_.mouseX,ui_.mouseY,cbX,cbY2,cbW+160,cbH)) {
            config_.acceptMods = !config_.acceptMods; saveConfig();
        }
        drawText("Accept mods", cbX+cbW+6, cbY2, 12, UI::W98::Black);
        ui_.drawTextRight("Hosts and other players may send mods that replace game content.", WX+WW-10, cbY2+1, 10, {160,80,0,255});
    }

    // Bottom strip (Discord notice)
    const int STRIP_Y=WY+WH-STRIP_H-2;
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect strip={WX,STRIP_Y,WW,STRIP_H}; SDL_RenderFillRect(renderer_,&strip);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, WX,STRIP_Y,WX+WW-1,STRIP_Y);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_RenderDrawLine(renderer_, WX,STRIP_Y+1,WX+WW-1,STRIP_Y+1);
    {
        const char* discordTxt = "Join our community on Discord!";
        bool dhov = ui_.pointInRect(ui_.mouseX, ui_.mouseY, WX+4, STRIP_Y, 220, STRIP_H);
        drawText(discordTxt, WX+8, STRIP_Y+4, 11, dhov ? SDL_Color{0,0,255,255} : SDL_Color{0,0,160,255});
        if (dhov && ui_.mouseClicked)
            SDL_OpenURL("https://discord.gg/dv28MgtaNn");
    }

    // Status bar
    char uname[160]; snprintf(uname,sizeof(uname),"%s  (Online)",config_.username.c_str());
    // Green online dot
    SDL_SetRenderDrawColor(renderer_, 0,180,0,255);
    SDL_Rect dot={WX+6,SCREEN_H-22,8,8}; SDL_RenderFillRect(renderer_,&dot);
    ui_.drawWin98StatusBar(SCREEN_H-26, uname);
}

// Host Setup

void Game::renderHostSetup() {
    ui_.drawDesktop();

    const int WX=30, WY=26, WW=SCREEN_W-60, WH=SCREEN_H-52;
    const int TH=UI::W98::TitleH;
    ui_.drawWin98Window(WX, WY, WW, WH, "Host a Session");

    // Toolbar band
    const int TB_Y=WY+TH, TB_H=44, TBY=TB_Y+4;
    const int TBH=36;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect tbBg={WX,TB_Y,WW,TB_H}; SDL_RenderFillRect(renderer_,&tbBg);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, WX, TB_Y+TB_H-2, WX+WW-1, TB_Y+TB_H-2);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_RenderDrawLine(renderer_, WX, TB_Y+TB_H-1, WX+WW-1, TB_Y+TB_H-1);

    auto tbBtn = [&](int id, const char* lbl, int x, int w, SDL_Color iconC) -> bool {
        bool sel=(hostSetupSelection_==id);
        bool hov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,x,TBY,w,TBH);
        if (hov) ui_.hoveredItem=id;
        if (hov&&!usingGamepad_) { hostSetupSelection_=id; menuSelection_=id; sel=true; }
        SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
        SDL_Rect bg={x,TBY,w,TBH}; SDL_RenderFillRect(renderer_,&bg);
        ui_.drawWin98Bevel(x,TBY,w,TBH,!(sel&&hov));
        int ix=x+w/2-7, iy=TBY+5;
        SDL_SetRenderDrawColor(renderer_, iconC.r,iconC.g,iconC.b,255);
        SDL_Rect ic={ix,iy,14,14}; SDL_RenderFillRect(renderer_,&ic);
        SDL_SetRenderDrawColor(renderer_,
            (Uint8)std::min(255,(int)iconC.r+80),
            (Uint8)std::min(255,(int)iconC.g+80),
            (Uint8)std::min(255,(int)iconC.b+80),255);
        SDL_Rect sh={ix+1,iy+1,6,3}; SDL_RenderFillRect(renderer_,&sh);
        int tw=(int)(strlen(lbl)*10*0.60f);
        drawText(lbl, x+w/2-tw/2, TBY+TBH-14, 10, UI::W98::Black);
        return hov&&ui_.mouseClicked;
    };
    auto tbSep=[&](int x){
        SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
        SDL_RenderDrawLine(renderer_, x+3,TBY+5,x+3,TBY+TBH-7);
        SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
        SDL_RenderDrawLine(renderer_, x+4,TBY+5,x+4,TBY+TBH-7);
    };

    int tbX=WX+6;
    if (tbBtn(12,"Start Hosting",tbX,100,{0,100,0,255}))
        { hostSetupSelection_=12; menuSelection_=12; confirmInput_=true; }
    tbX+=106; tbSep(tbX); tbX+=12;
    if (tbBtn(13,"Back",tbX,76,{110,70,0,255}))
        { hostSetupSelection_=13; menuSelection_=13; confirmInput_=true; }

    // Content area
    const int CONT_Y=TB_Y+TB_H;
    const int CONT_H=WH-TH-TB_H-2;
    const int leftW=264, leftX=WX+10;
    const int rightX=WX+leftW+20, rightW=WW-leftW-34;

    // Left: Session card
    // Navy header bar
    SDL_SetRenderDrawColor(renderer_, 0,0,128,255);
    SDL_Rect cHdr={leftX,CONT_Y+8,leftW-4,18}; SDL_RenderFillRect(renderer_,&cHdr);
    drawText("SESSION CARD", leftX+5, CONT_Y+11, 10, {255,255,255,255});
    int lcY=CONT_Y+28;

    std::string ip=getLocalIP();
    std::string accessLabel=lobbyPassword_.empty()?"Open":"Password locked";
    std::string modeLabel=lobbySettings_.isPvp?"PvP skirmish":"Co-op survival";
    std::string mapLabel="Generated arena";
    if (hostMapSelectIdx_>0&&hostMapSelectIdx_<=(int)mapFiles_.size()) {
        mapLabel=mapFiles_[hostMapSelectIdx_-1];
        size_t s=mapLabel.rfind('/'); if (s==std::string::npos) s=mapLabel.rfind('\\');
        if (s!=std::string::npos) mapLabel=mapLabel.substr(s+1);
        size_t d=mapLabel.rfind('.'); if (d!=std::string::npos) mapLabel=mapLabel.substr(0,d);
    }
    const char* teamSummary=(lobbySettings_.teamCount==4)?"4 teams":
                            (lobbySettings_.teamCount==2)?"2 teams":"open teams";
    std::string livesSummary=(lobbySettings_.livesPerPlayer==0)
        ?"infinite lives":std::to_string(lobbySettings_.livesPerPlayer)+" lives";

    auto infoBox=[&](const char* title, const std::string& val){
        ui_.drawWin98Bevel(leftX,lcY,leftW-4,38,false);
        drawText(title, leftX+5,lcY+3,10,UI::W98::Shadow);
        drawText(val.c_str(), leftX+5,lcY+18,13,UI::W98::Black);
        lcY+=44;
    };
    infoBox("ADDRESS",   ip+":"+std::to_string(hostPort_));
    infoBox("ACCESS",    accessLabel);
    infoBox("PLAYSTYLE", modeLabel);

    // Snapshot bevel
    int snapH=CONT_H-(lcY-CONT_Y)-12;
    ui_.drawWin98Bevel(leftX,lcY,leftW-4,snapH,false);
    drawText("SNAPSHOT", leftX+5,lcY+5,10,UI::W98::Shadow);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, leftX+68,lcY+12,leftX+leftW-10,lcY+12);
    int sY=lcY+18;
    drawText(config_.username.c_str(),leftX+5,sY,15,UI::W98::Black); sY+=19;
    char s1[96]; snprintf(s1,sizeof(s1),"%d slots · %s",hostMaxPlayers_,teamSummary);
    drawText(s1,leftX+5,sY,12,UI::W98::Shadow); sY+=15;
    char s2[96]; snprintf(s2,sizeof(s2),"HP %d · %s",lobbySettings_.playerMaxHp,livesSummary.c_str());
    drawText(s2,leftX+5,sY,12,UI::W98::Shadow); sY+=19;
    drawText("Map:",leftX+5,sY,10,UI::W98::Shadow); sY+=13;
    drawText(mapLabel.c_str(),leftX+5,sY,13,UI::W98::Black); sY+=17;
    drawText("Objective:",leftX+5,sY,10,UI::W98::Shadow); sY+=13;
    if (lobbySettings_.isPvp) {
        char obj[64];
        if (lobbySettings_.pvpMatchDuration<=0) snprintf(obj,sizeof(obj),"Last team alive");
        else snprintf(obj,sizeof(obj),"%d:%02d round",(int)lobbySettings_.pvpMatchDuration/60,(int)lobbySettings_.pvpMatchDuration%60);
        drawText(obj,leftX+5,sY,13,UI::W98::Black);
    } else {
        char obj[64];
        if (lobbySettings_.waveCount==0) snprintf(obj,sizeof(obj),"Endless waves");
        else snprintf(obj,sizeof(obj),"%d-wave run",lobbySettings_.waveCount);
        drawText(obj,leftX+5,sY,13,UI::W98::Black);
    }

    // Right: Options panel
    // Navy header bar
    SDL_SetRenderDrawColor(renderer_, 0,0,128,255);
    SDL_Rect oHdr={rightX,CONT_Y+8,rightW,18}; SDL_RenderFillRect(renderer_,&oHdr);
    drawText("HOST OPTIONS", rightX+8,CONT_Y+11,11,{255,255,255,255});
    drawText("Navigate · Confirm to edit", rightX+108,CONT_Y+13,10,{180,200,255,255});

    ui_.drawWin98Bevel(rightX,CONT_Y+28,rightW,CONT_H-36,false);

    const int rowAreaTop=CONT_Y+36, rowAreaBot=CONT_Y+CONT_H-42;
    const int rowH=28, rowGap=2;

    auto sectionHeader=[&](const char* label,int y){
        drawText(label, rightX+6,y+2,10,UI::W98::Shadow);
        ui_.drawWin98Bevel(rightX+80,y+7,rightW-88,2,false);
    };
    auto drawOptionRow=[&](int idx,int y,const char* label,const std::string& value,
                           bool editable=true,bool dim=false){
        bool sel=(hostSetupSelection_==idx);
        SDL_Rect row={rightX,y,rightW,rowH};
        bool hov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,row.x,row.y,row.w,row.h);
        if (hov) ui_.hoveredItem=idx;
        if (hov&&!usingGamepad_) { hostSetupSelection_=idx; menuSelection_=idx; }
        if (hov&&ui_.mouseClicked) { hostSetupSelection_=idx; menuSelection_=idx; confirmInput_=true; }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        Uint8 bg=dim?210:(sel?180:(hov?220:232));
        SDL_SetRenderDrawColor(renderer_,bg,bg,bg,255);
        SDL_RenderFillRect(renderer_,&row);
        SDL_SetRenderDrawColor(renderer_,160,160,160,255);
        SDL_Rect bot={row.x,row.y+row.h-1,row.w,1}; SDL_RenderFillRect(renderer_,&bot);
        if (sel) {
            SDL_SetRenderDrawColor(renderer_,0,0,128,255);
            SDL_Rect bar={row.x,row.y,3,row.h}; SDL_RenderFillRect(renderer_,&bar);
        }
        SDL_Color lc=dim?UI::W98::Shadow:UI::W98::Black;
        drawText(label,rightX+8,y+6,13,lc);
        std::string shown=value;
        if (sel&&editable) shown="< "+shown+" >";
        ui_.drawTextRight(shown.c_str(),rightX+rightW-6,y+6,13,dim?UI::W98::Shadow:UI::W98::Black);
    };

    // Auto-scroll
    {
        static const int ROW_REL_Y[]={20,52,84,116,162,194,226,258,290,322,354,400,444,486};
        const int VIS_H=rowAreaBot-rowAreaTop;
        constexpr int MAX_SCR=206;
        if (hostSetupSelection_>=0&&hostSetupSelection_<14) {
            int iy=ROW_REL_Y[hostSetupSelection_];
            if (iy<hostSetupScrollY_) hostSetupScrollY_=iy;
            if (iy+rowH>hostSetupScrollY_+VIS_H) hostSetupScrollY_=iy+rowH-VIS_H;
            hostSetupScrollY_=std::max(0,std::min(206,hostSetupScrollY_));
        }
    }

    // Clip rows
    SDL_Rect prevClip; SDL_RenderGetClipRect(renderer_,&prevClip);
    {SDL_Rect clip={rightX,rowAreaTop,rightW,rowAreaBot-rowAreaTop}; SDL_RenderSetClipRect(renderer_,&clip);}

    int rowY=rowAreaTop-hostSetupScrollY_;
    sectionHeader("NETWORK",rowY); rowY+=18;
    drawOptionRow(0,rowY,"Max players",std::to_string(hostMaxPlayers_)); rowY+=rowH+rowGap;
    {
        std::string portDisplay=portTyping_?portStr_:std::to_string(hostPort_);
        if (portTyping_) portDisplay+=((int)(gameTime_*3.0f)%2==0)?'_':' ';
        drawOptionRow(1,rowY,"Port",portDisplay,false);
    } rowY+=rowH+rowGap;
    {
        std::string ud=config_.username;
        if (mpUsernameTyping_) ud+=((int)(gameTime_*3.0f)%2==0)?'_':' ';
        drawOptionRow(2,rowY,"Host name",ud,false);
    } rowY+=rowH+rowGap;
    {
        std::string pd;
        if (hostPasswordTyping_) { pd=lobbyPassword_; pd+=((int)(gameTime_*3.0f)%2==0)?'_':' '; }
        else if (lobbyPassword_.empty()) pd="Open";
        else pd=std::string(lobbyPassword_.size(),'*');
        drawOptionRow(3,rowY,"Password",pd,false);
    } rowY+=rowH+rowGap+8;

    sectionHeader("MATCH",rowY); rowY+=18;
    drawOptionRow(4,rowY,"Mode",       lobbySettings_.isPvp?"PvP":"PvE"); rowY+=rowH+rowGap;
    drawOptionRow(5,rowY,"Map",        mapLabel);                         rowY+=rowH+rowGap;
    drawOptionRow(6,rowY,"Teams",      teamSummary);                      rowY+=rowH+rowGap;
    drawOptionRow(7,rowY,"Player HP",  std::to_string(lobbySettings_.playerMaxHp)); rowY+=rowH+rowGap;
    drawOptionRow(8,rowY,"Lives",      livesSummary);                     rowY+=rowH+rowGap;
    {
        std::string obj;
        if (lobbySettings_.isPvp) {
            if (lobbySettings_.pvpMatchDuration<=0) obj="Unlimited";
            else {char b[32]; snprintf(b,sizeof(b),"%d:%02d",(int)lobbySettings_.pvpMatchDuration/60,(int)lobbySettings_.pvpMatchDuration%60); obj=b;}
        } else { obj=(lobbySettings_.waveCount==0)?"Endless":std::to_string(lobbySettings_.waveCount)+" waves"; }
        drawOptionRow(9,rowY,lobbySettings_.isPvp?"Round timer":"Wave goal",obj);
    } rowY+=rowH+rowGap;
    {
        bool ffForced=lobbySettings_.isPvp&&lobbySettings_.teamCount==0;
        std::string ffLabel=ffForced?"Forced on":(lobbySettings_.friendlyFire?"On":"Off");
        drawOptionRow(10,rowY,lobbySettings_.isPvp?"Friendly fire":"PvP damage",ffLabel,!ffForced,ffForced);
    } rowY+=rowH+rowGap+8;

    sectionHeader("PRESET",rowY); rowY+=18;
    {
        std::string pl=serverPresets_.empty()?"No presets saved":serverPresets_[presetSelection_%(int)serverPresets_.size()].name;
        drawOptionRow(11,rowY,"Load preset",pl,!serverPresets_.empty(),serverPresets_.empty());
    }

    SDL_RenderSetClipRect(renderer_, prevClip.w>0?&prevClip:nullptr);

    // Scrollbar
    {
        constexpr int CONT_H2=520; const int VIS_H=rowAreaBot-rowAreaTop; constexpr int MAX_SCR=206;
        int sbX=rightX+rightW+2;
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_,212,208,200,255);
        SDL_Rect track={sbX,rowAreaTop,10,VIS_H}; SDL_RenderFillRect(renderer_,&track);
        ui_.drawWin98Bevel(sbX,rowAreaTop,10,VIS_H,false);
        int thumbH=std::max(16,VIS_H*VIS_H/CONT_H2);
        int thumbY=rowAreaTop+(int)((long long)hostSetupScrollY_*(VIS_H-thumbH)/MAX_SCR);
        SDL_SetRenderDrawColor(renderer_,212,208,200,255);
        SDL_Rect thumb={sbX,thumbY,10,thumbH}; SDL_RenderFillRect(renderer_,&thumb);
        ui_.drawWin98Bevel(sbX,thumbY,10,thumbH,true);
    }

    // Start/Back buttons (below row area)
    int btnAreaY=rowAreaBot+6, btnW=(rightW-10)/2;
    if (ui_.win98Button(12,"Start Hosting",rightX,btnAreaY,btnW,26,hostSetupSelection_==12))
        { hostSetupSelection_=12; menuSelection_=12; confirmInput_=true; }
    if (ui_.hoveredItem==12&&!usingGamepad_) { hostSetupSelection_=12; menuSelection_=12; }
    if (ui_.win98Button(13,"Back",rightX+btnW+10,btnAreaY,btnW,26,hostSetupSelection_==13))
        { hostSetupSelection_=13; menuSelection_=13; confirmInput_=true; }
    if (ui_.hoveredItem==13&&!usingGamepad_) { hostSetupSelection_=13; menuSelection_=13; }

    if (softKB_.active) renderSoftKB();

    ui_.drawWin98StatusBar(SCREEN_H-26,"Navigate with arrows/stick  |  Enter: Edit/Apply  |  Esc: Cancel");
}

// Join Menu

void Game::renderJoinMenu() {
    ui_.drawDesktop();

    const int padX=14, fieldH=40, fieldGap=8, btnH=26, btnGap=6, winW=460;
    const int TH=UI::W98::TitleH;
    const int TB_H=44, TBH_BTN=36, TBW_BTN=80;

    // Window height: toolbar + status + 4 fields + sep + 3 btns + pad
    const int winH=TH+TB_H+10+20+6+4*(fieldH+fieldGap)+6+2+8+3*(btnH+btnGap)+10;
    const int winX=(SCREEN_W-winW)/2, winY=(SCREEN_H-winH)/2;
    ui_.drawWin98Window(winX, winY, winW, winH, "Join a Game");

    // Toolbar
    const int TB_Y=winY+TH, TBY=TB_Y+4;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect tbBg={winX,TB_Y,winW,TB_H}; SDL_RenderFillRect(renderer_,&tbBg);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, winX,TB_Y+TB_H-2,winX+winW-1,TB_Y+TB_H-2);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_RenderDrawLine(renderer_, winX,TB_Y+TB_H-1,winX+winW-1,TB_Y+TB_H-1);

    auto tbBtn=[&](int id, const char* lbl, int x, SDL_Color iconC) -> bool {
        bool sel=(joinMenuSelection_==id);
        bool hov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,x,TBY,TBW_BTN,TBH_BTN);
        if (hov) ui_.hoveredItem=id;
        if (hov&&!usingGamepad_) { joinMenuSelection_=id; menuSelection_=id; sel=true; }
        SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
        SDL_Rect bg={x,TBY,TBW_BTN,TBH_BTN}; SDL_RenderFillRect(renderer_,&bg);
        ui_.drawWin98Bevel(x,TBY,TBW_BTN,TBH_BTN,!(sel&&hov));
        int ix=x+TBW_BTN/2-7, iy=TBY+5;
        SDL_SetRenderDrawColor(renderer_, iconC.r,iconC.g,iconC.b,255);
        SDL_Rect ic={ix,iy,14,14}; SDL_RenderFillRect(renderer_,&ic);
        SDL_SetRenderDrawColor(renderer_,
            (Uint8)std::min(255,(int)iconC.r+80),(Uint8)std::min(255,(int)iconC.g+80),
            (Uint8)std::min(255,(int)iconC.b+80),255);
        SDL_Rect sh={ix+1,iy+1,6,3}; SDL_RenderFillRect(renderer_,&sh);
        int tw=(int)(strlen(lbl)*10*0.60f);
        drawText(lbl, x+TBW_BTN/2-tw/2, TBY+TBH_BTN-14, 10, UI::W98::Black);
        return hov&&ui_.mouseClicked;
    };
    auto tbSep=[&](int x){
        SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
        SDL_RenderDrawLine(renderer_, x+3,TBY+5,x+3,TBY+TBH_BTN-7);
        SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
        SDL_RenderDrawLine(renderer_, x+4,TBY+5,x+4,TBY+TBH_BTN-7);
    };

    int tbX=winX+6;
    if (tbBtn(4,"Connect",tbX,{0,100,0,255}))  { joinMenuSelection_=4; menuSelection_=4; confirmInput_=true; }
    tbX+=TBW_BTN+4;
    if (tbBtn(5,"Save",tbX,{0,0,128,255}))     { joinMenuSelection_=5; menuSelection_=5; confirmInput_=true; }
    tbX+=TBW_BTN+10; tbSep(tbX); tbX+=12;
    if (tbBtn(6,"Back",tbX,{110,70,0,255}))    { joinMenuSelection_=6; menuSelection_=6; confirmInput_=true; }

    // Fields
    const int fieldW=winW-padX*2, fieldX=winX+padX;
    auto& net=NetworkManager::instance();
    int statusY=winY+TH+TB_H+10;

    if (net.state()==NetState::Connecting) {
        ui_.drawTextCentered("Connecting...", statusY, 14, UI::W98::Shadow);
    } else if (!connectStatus_.empty()) {
        bool isSaved=connectStatus_.find("saved")!=std::string::npos||connectStatus_.find("Saved")!=std::string::npos;
        SDL_Color sc=isSaved?SDL_Color{0,128,0,255}:SDL_Color{180,0,0,255};
        ui_.drawTextCentered(connectStatus_.c_str(), statusY, 13, sc);
    }
    int fieldY=statusY+26;

    auto drawField=[&](int idx, const char* label, const std::string& value,
                       bool editing, bool password=false){
        bool focused=(joinMenuSelection_==idx);
        std::string display=value;
        if (editing) display+=((int)(gameTime_*3.0f)%2==0)?'_':' ';
        drawText(label, fieldX, fieldY, 11, UI::W98::Shadow);
        ui_.drawWin98TextField(fieldX, fieldY+13, fieldW, fieldH-13,
                               display.c_str(), focused, password, gameTime_);
        bool hov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,fieldX,fieldY,fieldW,fieldH);
        if (hov&&!usingGamepad_) { menuSelection_=idx; joinMenuSelection_=idx; }
        if (hov) ui_.hoveredItem=idx;
        if (hov&&ui_.mouseClicked) { menuSelection_=idx; joinMenuSelection_=idx; confirmInput_=true; }
        fieldY+=fieldH+fieldGap;
    };

    drawField(0,"IP / Host (e.g. 192.168.1.10 or play.example.com)",joinAddress_,ipTyping_);
    {std::string ps=joinPortTyping_?joinPortStr_:std::to_string(joinPort_);
     drawField(1,"Port",ps,joinPortTyping_);}
    drawField(2,"Username",config_.username,mpUsernameTyping_);
    {std::string pw;
     if (joinPasswordTyping_) pw=joinPassword_;
     else if (!joinPassword_.empty()) pw=std::string(joinPassword_.size(),'*');
     drawField(3,"Password (leave blank if none)",pw,joinPasswordTyping_,true);}

    if (softKB_.active) {
        renderSoftKB();
    } else {
        ui_.drawWin98Bevel(fieldX,fieldY,fieldW,2,false); fieldY+=8;
        if (ui_.win98Button(4,"Connect",fieldX,fieldY,fieldW,btnH,joinMenuSelection_==4))
            { menuSelection_=4; joinMenuSelection_=4; confirmInput_=true; }
        if (ui_.hoveredItem==4&&!usingGamepad_) { menuSelection_=4; joinMenuSelection_=4; }
        fieldY+=btnH+btnGap;
        if (ui_.win98Button(5,"Save Server",fieldX,fieldY,fieldW,btnH,joinMenuSelection_==5))
            { menuSelection_=5; joinMenuSelection_=5; confirmInput_=true; }
        if (ui_.hoveredItem==5&&!usingGamepad_) { menuSelection_=5; joinMenuSelection_=5; }
        fieldY+=btnH+btnGap;
        if (ui_.win98Button(6,"Back",fieldX,fieldY,fieldW,btnH,joinMenuSelection_==6))
            { menuSelection_=6; joinMenuSelection_=6; confirmInput_=true; }
        if (ui_.hoveredItem==6&&!usingGamepad_) { menuSelection_=6; joinMenuSelection_=6; }
        ui_.drawWin98StatusBar(SCREEN_H-26,"Enter connection details then click Connect");
    }
}

// Lobby

void Game::renderLobby() {
    ui_.drawDesktop();

    auto& net=NetworkManager::instance();
    static const SDL_Color teamColors[4]={
        {180,0,0,255},{0,0,180,255},{0,140,0,255},{160,120,0,255}
    };

    SDL_Texture* icPlayerUser = Assets::instance().loadRelTex("sprites/ui/chat_user.png");
    SDL_Texture* icPlayerHost = Assets::instance().loadRelTex("sprites/ui/chat_host.png");

    // Connecting overlay
    if (!net.isHost()&&net.state()==NetState::Connecting) {
        const int cW=400, cH=160;
        const int cX=(SCREEN_W-cW)/2, cY=(SCREEN_H-cH)/2;
        ui_.drawWin98Window(cX,cY,cW,cH,"Connecting...");
        float rem=std::max(0.0f,connectTimer_);
        int dots=((int)(gameTime_*3))%4;
        char dotBuf[8]=""; for (int i=0;i<dots;i++) strcat(dotBuf,".");
        char st[128]; snprintf(st,sizeof(st),"Connecting to %s:%d%s",joinAddress_.c_str(),joinPort_,dotBuf);
        ui_.drawTextCentered(st, cY+UI::W98::TitleH+14, 13, UI::W98::Black);
        char tb[64]; snprintf(tb,sizeof(tb),"Timeout in %.1fs",rem);
        ui_.drawTextCentered(tb, cY+UI::W98::TitleH+34, 12, UI::W98::Shadow);
        const int bX=cX+20, bY=cY+UI::W98::TitleH+60, bW=cW-40, bH=14;
        ui_.drawWin98Bevel(bX,bY,bW,bH,false);
        float prog=std::max(0.0f,std::min(1.0f,1.0f-(rem/5.0f)));
        SDL_SetRenderDrawColor(renderer_,0,0,128,255);
        SDL_Rect fill={bX+2,bY+2,(int)((bW-4)*prog),bH-4}; SDL_RenderFillRect(renderer_,&fill);
        ui_.drawWin98StatusBar(SCREEN_H-26,"Press Esc / B to cancel");
        {UI::HintPair h[]={{UI::Action::Back,"Cancel"}}; ui_.drawHintBar(h,1,SCREEN_H-40);}
        return;
    }

    const int WX=30, WY=26, WW=SCREEN_W-60, WH=SCREEN_H-52;
    const int TH=UI::W98::TitleH;

    // Build window title from host name
    std::string hostName=net.lobbyInfo().hostName.empty()?config_.username:net.lobbyInfo().hostName;
    char winTitle[128]; snprintf(winTitle,sizeof(winTitle),"Lobby \xe2\x80\x94 %s",hostName.c_str());
    ui_.drawWin98Window(WX,WY,WW,WH,winTitle);

    // Toolbar band
    const int TB_Y=WY+TH, TB_H=44, TBY=TB_Y+4;
    const int TBH_B=36, TBW_B=90;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect tbBg={WX,TB_Y,WW,TB_H}; SDL_RenderFillRect(renderer_,&tbBg);
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, WX,TB_Y+TB_H-2,WX+WW-1,TB_Y+TB_H-2);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_RenderDrawLine(renderer_, WX,TB_Y+TB_H-1,WX+WW-1,TB_Y+TB_H-1);

    bool canManage=net.isLobbyHost();
    const auto& players=net.players();
    int readyPlayers=0;
    for (const auto& p:players) if (p.ready||p.id==net.lobbyHostId()) readyPlayers++;
    bool allReady=true;
    for (auto& p:players) if (!p.ready&&p.id!=net.lobbyHostId()) allReady=false;

    auto tbBtn=[&](int id, const char* lbl, int x, int w, SDL_Color iconC) -> bool {
        bool hov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,x,TBY,w,TBH_B);
        if (hov) ui_.hoveredItem=id;
        SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
        SDL_Rect bg={x,TBY,w,TBH_B}; SDL_RenderFillRect(renderer_,&bg);
        ui_.drawWin98Bevel(x,TBY,w,TBH_B,true);
        int ix=x+w/2-7, iy=TBY+5;
        SDL_SetRenderDrawColor(renderer_, iconC.r,iconC.g,iconC.b,255);
        SDL_Rect ic={ix,iy,14,14}; SDL_RenderFillRect(renderer_,&ic);
        SDL_SetRenderDrawColor(renderer_,
            (Uint8)std::min(255,(int)iconC.r+80),(Uint8)std::min(255,(int)iconC.g+80),
            (Uint8)std::min(255,(int)iconC.b+80),255);
        SDL_Rect sh={ix+1,iy+1,6,3}; SDL_RenderFillRect(renderer_,&sh);
        int tw=(int)(strlen(lbl)*10*0.60f);
        drawText(lbl, x+w/2-tw/2, TBY+TBH_B-14, 10, UI::W98::Black);
        return hov&&ui_.mouseClicked;
    };
    auto tbSep=[&](int x){
        SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
        SDL_RenderDrawLine(renderer_, x+3,TBY+5,x+3,TBY+TBH_B-7);
        SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
        SDL_RenderDrawLine(renderer_, x+4,TBY+5,x+4,TBY+TBH_B-7);
    };

    int tbX=WX+6;
    if (canManage) {
        const char* startLbl=allReady||players.size()<=1?"Start Game":"Start Game";
        if (tbBtn(50,startLbl,tbX,TBW_B,{0,120,0,255})) confirmInput_=true;
    } else {
        const char* rdyLbl=lobbyReady_?"Unready":"Ready Up";
        SDL_Color rdyC=lobbyReady_?SDL_Color{0,100,0,255}:SDL_Color{0,0,128,255};
        if (tbBtn(50,rdyLbl,tbX,TBW_B,rdyC)) confirmInput_=true;
    }
    tbX+=TBW_B+10; tbSep(tbX); tbX+=12;
    if (tbBtn(51,"Leave",tbX,TBW_B-10,{140,0,0,255})) backInput_=true;

    // Address bar (like "To: channel/session info")
    const int ADDR_Y=TB_Y+TB_H, ADDR_H=24;
    SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
    SDL_Rect addrBg={WX,ADDR_Y,WW,ADDR_H}; SDL_RenderFillRect(renderer_,&addrBg);
    drawText("Session:", WX+8, ADDR_Y+5, 11, UI::W98::Shadow);
    // Build address bar content
    std::string mapSumm=(net.lobbyInfo().mapName.empty()||net.lobbyInfo().mapName=="Generated arena")
        ?"Generated arena":net.lobbyInfo().mapName;
    std::string modeSumm=lobbySettings_.isPvp?"PvP":"PvE";
    if (lobbySettings_.teamCount==2) modeSumm+=" · 2 Teams";
    else if (lobbySettings_.teamCount==4) modeSumm+=" · 4 Teams";
    char rdyStr[32]; snprintf(rdyStr,sizeof(rdyStr),"%d/%d ready",readyPlayers,(int)players.size());
    char addrContent[256]; snprintf(addrContent,sizeof(addrContent),
        "%s  ·  %s  ·  %s  ·  %s",hostName.c_str(),modeSumm.c_str(),mapSumm.c_str(),rdyStr);
    // Inset text field
    ui_.drawWin98Bevel(WX+70, ADDR_Y+2, WW-78, ADDR_H-4, false);
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_Rect addrFld={WX+72,ADDR_Y+4,WW-82,ADDR_H-8}; SDL_RenderFillRect(renderer_,&addrFld);
    drawText(addrContent, WX+76, ADDR_Y+6, 11, UI::W98::Black);
    // Bottom separator
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, WX,ADDR_Y+ADDR_H-1,WX+WW-1,ADDR_Y+ADDR_H-1);

    // Content area
    const int CONT_Y=ADDR_Y+ADDR_H, CONT_H=WH-TH-TB_H-ADDR_H-40;
    const int halfW=WW/2-16, settX=WX+12, playX=WX+WW/2+4;

    // Settings panel (left)
    // Navy header
    SDL_SetRenderDrawColor(renderer_, 0,0,128,255);
    SDL_Rect sHdr={settX,CONT_Y+4,halfW,18}; SDL_RenderFillRect(renderer_,&sHdr);
    drawText("SETTINGS", settX+6, CONT_Y+7, 11, {255,255,255,255});
    bool isHostPlayer=net.isLobbyHost();
    const char* settHint=isHostPlayer?"You can tweak these live.":"Read-only - host controls these.";
    drawText(settHint, settX+74, CONT_Y+9, 10, {180,200,255,255});

    const int panelH=CONT_H-8;
    ui_.drawWin98Bevel(settX,CONT_Y+24,halfW,panelH,false);

    const int rowStep=26, clipTop=CONT_Y+28, clipBot=CONT_Y+24+panelH-4;
    const int rowsVisH=clipBot-clipTop;

    // Auto-scroll
    {
        int rowTop=28+lobbySettingsSel_*rowStep;
        if (rowTop<lobbySettingsScrollY_) lobbySettingsScrollY_=rowTop;
        if (rowTop+rowStep>lobbySettingsScrollY_+rowsVisH) lobbySettingsScrollY_=rowTop+rowStep-rowsVisH;
        lobbySettingsScrollY_=std::max(0,lobbySettingsScrollY_);
    }

    int rowY=clipTop-lobbySettingsScrollY_;
    SDL_Rect prevClip; SDL_RenderGetClipRect(renderer_,&prevClip);
    {SDL_Rect rc={settX,clipTop,halfW,clipBot-clipTop}; SDL_RenderSetClipRect(renderer_,&rc);}

    // Config-menu style row with clickable < > arrow buttons for mouse control.
    // isConfirm=true replaces < > with a "Save" button that fires confirmInput_.
    const int ARR_W=22, ARR_H=rowStep-4;
    auto drawSettingRow=[&](int idx, const char* label, const char* value,
                            bool editable=true, bool dim=false, bool isConfirm=false){
        bool sel=isHostPlayer&&(lobbySettingsSel_==idx)&&!dim;
        bool rv=(rowY>=clipTop-rowStep)&&(rowY<clipBot);

        // Row background + border + left bar
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        Uint8 bg=dim?210:(sel?188:232);
        SDL_SetRenderDrawColor(renderer_,bg,bg,bg,255);
        SDL_Rect rowBg={settX,rowY,halfW,rowStep-1}; SDL_RenderFillRect(renderer_,&rowBg);
        SDL_SetRenderDrawColor(renderer_,160,160,160,255);
        SDL_Rect bot={settX,rowY+rowStep-2,halfW,1}; SDL_RenderFillRect(renderer_,&bot);
        if (sel) {
            SDL_SetRenderDrawColor(renderer_,0,0,128,255);
            SDL_Rect bar={settX,rowY,3,rowStep-1}; SDL_RenderFillRect(renderer_,&bar);
        }
        // Row hover/click (anywhere on row selects it)
        if (isHostPlayer&&rv&&!dim) {
            bool hov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,settX,rowY,halfW,rowStep-1);
            if (hov&&!usingGamepad_) { lobbySettingsSel_=idx; menuSelection_=idx; }
            if (hov&&ui_.mouseClicked) { lobbySettingsSel_=idx; menuSelection_=idx; }
        }

        SDL_Color lc=dim?UI::W98::Shadow:UI::W98::Black;
        drawText(label, settX+8, rowY+5, 13, lc);

        if (!dim && isHostPlayer && rv) {
            if (isConfirm) {
                // "Save" action button on the right
                const int bW=48, bH=rowStep-6, bX=settX+halfW-bW-4, bY=rowY+3;
                bool bHov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,bX,bY,bW,bH);
                if (bHov) ui_.hoveredItem=400+idx;
                SDL_SetRenderDrawColor(renderer_,212,208,200,255);
                SDL_Rect bb={bX,bY,bW,bH}; SDL_RenderFillRect(renderer_,&bb);
                ui_.drawWin98Bevel(bX,bY,bW,bH,true);
                int tw2=(int)(strlen("Save")*10*0.60f);
                drawText("Save", bX+bW/2-tw2/2, bY+bH/2-6, 10, UI::W98::Black);
                if (bHov&&ui_.mouseClicked) { lobbySettingsSel_=idx; menuSelection_=idx; confirmInput_=true; }
            } else if (editable) {
                // < > spinner buttons
                const int rBtnX=settX+halfW-ARR_W-4, lBtnX=rBtnX-ARR_W-2;
                const int bY=rowY+2;
                // < button
                bool lhov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,lBtnX,bY,ARR_W,ARR_H);
                if (lhov) ui_.hoveredItem=200+idx;
                SDL_SetRenderDrawColor(renderer_,212,208,200,255);
                SDL_Rect lb={lBtnX,bY,ARR_W,ARR_H}; SDL_RenderFillRect(renderer_,&lb);
                ui_.drawWin98Bevel(lBtnX,bY,ARR_W,ARR_H,true);
                int aw=(int)(strlen("<")*10*0.60f);
                drawText("<", lBtnX+ARR_W/2-aw/2, bY+ARR_H/2-7, 11, UI::W98::Black);
                if (lhov&&ui_.mouseClicked) { lobbySettingsSel_=idx; menuSelection_=idx; renderLeft_=true; ui_.mouseClicked=false; ui_.clickCooldownFrames=3; }
                // > button
                bool rhov=ui_.pointInRect(ui_.mouseX,ui_.mouseY,rBtnX,bY,ARR_W,ARR_H);
                if (rhov) ui_.hoveredItem=300+idx;
                SDL_SetRenderDrawColor(renderer_,212,208,200,255);
                SDL_Rect rb={rBtnX,bY,ARR_W,ARR_H}; SDL_RenderFillRect(renderer_,&rb);
                ui_.drawWin98Bevel(rBtnX,bY,ARR_W,ARR_H,true);
                int aw2=(int)(strlen(">")*10*0.60f);
                drawText(">", rBtnX+ARR_W/2-aw2/2, bY+ARR_H/2-7, 11, UI::W98::Black);
                if (rhov&&ui_.mouseClicked) { lobbySettingsSel_=idx; menuSelection_=idx; renderRight_=true; ui_.mouseClicked=false; ui_.clickCooldownFrames=3; }
                // Value text between buttons
                ui_.drawTextRight(value, lBtnX-6, rowY+5, 13, UI::W98::Black);
            } else {
                ui_.drawTextRight(value, settX+halfW-8, rowY+5, 13, UI::W98::Shadow);
            }
        } else {
            ui_.drawTextRight(value, settX+halfW-8, rowY+5, 13, dim?UI::W98::Shadow:UI::W98::Black);
        }
        rowY+=rowStep;
    };

    drawSettingRow(0,"Gamemode", lobbySettings_.isPvp?"PvP":"PvE");
    {
        bool pvpNoTeams=lobbySettings_.isPvp&&lobbySettings_.teamCount==0;
        if (pvpNoTeams) drawSettingRow(1,"PvP","ON",false,true);
        else if (!lobbySettings_.isPvp) drawSettingRow(1,"PvP damage", lobbySettings_.friendlyFire?"ON":"OFF");
        else drawSettingRow(1,"Friendly Fire", lobbySettings_.friendlyFire?"ON":"OFF");
    }
    drawSettingRow(2,"Upgrades", lobbySettings_.upgradesShared?"Shared":"Individual");
    {
        std::string ml="Generated";
        if (lobbyMapIdx_>0&&lobbyMapIdx_<=(int)mapFiles_.size()) {
            std::string mf=mapFiles_[lobbyMapIdx_-1];
            size_t sl=mf.rfind('/'); if (sl==std::string::npos) sl=mf.rfind('\\');
            std::string base=(sl!=std::string::npos)?mf.substr(sl+1):mf;
            size_t dot=base.rfind('.'); if (dot!=std::string::npos) base=base.substr(0,dot);
            ml=base;
        }
        if (mapFiles_.empty()) ml="Generated";
        drawSettingRow(3,"Map",ml.c_str());
    }
    {bool c=(lobbyMapIdx_>0); char v[16]; snprintf(v,sizeof(v),c?"(custom)":"%d",lobbySettings_.mapWidth);
     drawSettingRow(4,"Map Width",v,!c,c);}
    {bool c=(lobbyMapIdx_>0); char v[16]; snprintf(v,sizeof(v),c?"(custom)":"%d",lobbySettings_.mapHeight);
     drawSettingRow(5,"Map Height",v,!c,c);}
    {char v[16]; snprintf(v,sizeof(v),"%.1fx",lobbySettings_.enemyHpScale);
     drawSettingRow(6,"Enemy HP",v,!lobbySettings_.isPvp,lobbySettings_.isPvp);}
    {char v[16]; snprintf(v,sizeof(v),"%.1fx",lobbySettings_.enemySpeedScale);
     drawSettingRow(7,"Enemy Speed",v,!lobbySettings_.isPvp,lobbySettings_.isPvp);}
    {char v[16]; snprintf(v,sizeof(v),"%.1fx",lobbySettings_.spawnRateScale);
     drawSettingRow(8,"Spawn Rate",v,!lobbySettings_.isPvp,lobbySettings_.isPvp);}
    {char v[16]; snprintf(v,sizeof(v),"%d",lobbySettings_.playerMaxHp); drawSettingRow(9,"Player HP",v);}
    {const char* tv=(lobbySettings_.teamCount==4)?"4 Teams":(lobbySettings_.teamCount==2)?"2 Teams":"None";
     drawSettingRow(10,"Teams",tv);}
    {char v[16]; if(lobbySettings_.livesPerPlayer==0) snprintf(v,sizeof(v),"Infinite");
     else snprintf(v,sizeof(v),"%d",lobbySettings_.livesPerPlayer); drawSettingRow(11,"Lives",v);}
    if (lobbySettings_.livesPerPlayer>0) {
        drawSettingRow(12,"Lives Mode",lobbySettings_.livesShared?"Shared Pool":"Individual");
    }
    {
        int cIdx=(lobbySettings_.livesPerPlayer>0)?13:12;
        if (lobbySettings_.isPvp) {
            char v[16]; snprintf(v,sizeof(v),"%.0fs",lobbySettings_.crateInterval);
            drawSettingRow(cIdx,"Crate Interval",v);
        } else {
            char v[16]; if(lobbySettings_.waveCount==0) snprintf(v,sizeof(v),"Endless");
            else snprintf(v,sizeof(v),"%d",lobbySettings_.waveCount);
            drawSettingRow(cIdx,"Waves",v);
        }
        int ptIdx=cIdx+1, plIdx=cIdx+2;
        if (lobbySettings_.isPvp) {
            char v[16]; if(lobbySettings_.pvpMatchDuration<=0) snprintf(v,sizeof(v),"Unlimited");
            else snprintf(v,sizeof(v),"%d:%02d",(int)lobbySettings_.pvpMatchDuration/60,(int)lobbySettings_.pvpMatchDuration%60);
            drawSettingRow(ptIdx,"Match Time",v);
        }
        drawSettingRow(plIdx,"Save Preset","",true,false,true);
        {const char* pl=serverPresets_.empty()?"(none saved)":serverPresets_[presetSelection_%(int)serverPresets_.size()].name.c_str();
         drawSettingRow(plIdx+1,"Load Preset",pl,!serverPresets_.empty(),serverPresets_.empty());}
    }

    SDL_RenderSetClipRect(renderer_, prevClip.w>0?&prevClip:nullptr);

    // Settings scrollbar
    {
        int contentH=17*rowStep;
        if (contentH>rowsVisH) {
            int maxScroll=contentH-rowsVisH;
            int sbX=settX+halfW-10;
            SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
            SDL_Rect track={sbX,clipTop,10,rowsVisH}; SDL_RenderFillRect(renderer_,&track);
            ui_.drawWin98Bevel(sbX,clipTop,10,rowsVisH,false);
            int tH=std::max(14,rowsVisH*rowsVisH/contentH);
            int tY=clipTop+(int)((long long)lobbySettingsScrollY_*(rowsVisH-tH)/maxScroll);
            SDL_SetRenderDrawColor(renderer_, 212,208,200,255);
            SDL_Rect th={sbX,tY,10,tH}; SDL_RenderFillRect(renderer_,&th);
            ui_.drawWin98Bevel(sbX,tY,10,tH,true);
        }
    }

    // Players panel (right) - ICQ-style friends list
    bool isHostInKickMode=canManage&&(lobbyKickCursor_>=0);

    // Navy header
    SDL_SetRenderDrawColor(renderer_, 0,0,128,255);
    SDL_Rect pHdr={playX,CONT_Y+4,halfW,18}; SDL_RenderFillRect(renderer_,&pHdr);
    drawText("PLAYERS", playX+6, CONT_Y+7, 11, {255,255,255,255});
    if (canManage) {
        const char* kl=isHostInKickMode?"[A] Kick  [X] Xfer  [B] Cancel":"[Y] Host Actions";
        SDL_Color kc=isHostInKickMode?SDL_Color{255,180,180,255}:SDL_Color{180,200,255,255};
        drawText(kl, playX+78, CONT_Y+9, 10, kc);
    }

    ui_.drawWin98Bevel(playX,CONT_Y+24,halfW,panelH,false);
    // White background
    SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
    SDL_Rect plBg={playX+2,CONT_Y+26,halfW-4,panelH-4}; SDL_RenderFillRect(renderer_,&plBg);

    int plY=CONT_Y+32;
    for (size_t i=0;i<players.size();i++) {
        bool isLocal=(players[i].id==net.localPlayerId());
        bool isHostP=(players[i].id==net.lobbyHostId());
        bool isKick=isHostInKickMode&&((int)i==lobbyKickCursor_);
        const int ENTRY_H=28;

        // Row bg
        if (isKick) {
            SDL_SetRenderDrawColor(renderer_, 255,220,220,255);
        } else if (isLocal) {
            SDL_SetRenderDrawColor(renderer_, 200,215,240,255);
        } else {
            SDL_SetRenderDrawColor(renderer_, 255,255,255,255);
        }
        SDL_Rect rowBg={playX+2,plY,halfW-4,ENTRY_H}; SDL_RenderFillRect(renderer_,&rowBg);

        // Ready dot (8×8)
        SDL_SetRenderDrawColor(renderer_, players[i].ready?0:160, players[i].ready?140:160, 0, 255);
        SDL_Rect rdot={playX+6,plY+(ENTRY_H-8)/2,8,8}; SDL_RenderFillRect(renderer_,&rdot);

        // Player icon (msagent, 16×16)
        SDL_Texture* playerIc = isHostP ? icPlayerHost : icPlayerUser;
        if (playerIc) {
            SDL_Rect dst={playX+16, plY+(ENTRY_H-16)/2, 16, 16};
            if (isKick) SDL_SetTextureColorMod(playerIc, 255, 120, 120);
            else        SDL_SetTextureColorMod(playerIc, 255, 255, 255);
            SDL_RenderCopy(renderer_, playerIc, nullptr, &dst);
        } else {
            SDL_Color ic;
            if (isKick) ic={180,0,0,255};
            else if (isHostP) ic={0,0,160,255};
            else if (players[i].team>=0&&players[i].team<4) ic=teamColors[players[i].team];
            else ic={80,80,120,255};
            drawPersonIcon(renderer_, playX+18, plY+(ENTRY_H-14)/2, ic);
        }

        // Name
        char entryBuf[128];
        snprintf(entryBuf,sizeof(entryBuf),"%s%s%s",
                 players[i].username.c_str(), isHostP?" ★":"", isKick?" [×]":"");
        SDL_Color nc=isKick?SDL_Color{180,0,0,255}:isLocal?UI::W98::Black:UI::W98::Shadow;
        drawText(entryBuf, playX+36, plY+7, 13, nc);

        // Ping (right side)
        if (!isLocal) {
            uint32_t ping=net.getPlayerPing(players[i].id);
            if (ping>0) {
                char pingBuf[32]; snprintf(pingBuf,sizeof(pingBuf),"%dms",ping);
                SDL_Color pc=(ping<50)?SDL_Color{0,128,0,255}:(ping<100)?SDL_Color{160,100,0,255}:SDL_Color{180,0,0,255};
                ui_.drawTextRight(pingBuf, playX+halfW-8, plY+7, 11, pc);
            }
        } else {
            ui_.drawTextRight("(you)", playX+halfW-8, plY+7, 11, UI::W98::Shadow);
        }

        plY+=ENTRY_H;

        // Sub-players
        int subCount=(int)players[i].localSubPlayers;
        subCount=std::max(0,std::min(3,subCount));
        for (int s=0;s<subCount;s++) {
            std::string subLabel;
            if (isLocal) {
                int found=0;
                for (int si=1;si<4;si++) {
                    if (!coopSlots_[si].joined) continue;
                    if (found==s) { subLabel=coopSlots_[si].username.empty()?("local-"+std::to_string(s+1)):coopSlots_[si].username; break; }
                    found++;
                }
                if (subLabel.empty()) subLabel="local-"+std::to_string(s+1);
            } else { subLabel="local-"+std::to_string(s+1); }
            drawText(("  -> "+subLabel).c_str(), playX+36, plY, 11, UI::W98::Shadow);
            plY+=16;
        }
    }

    // Separator between players and bottom buttons (within players panel)
    SDL_SetRenderDrawColor(renderer_, 128,128,128,255);
    SDL_RenderDrawLine(renderer_, playX+4, CONT_Y+24+panelH-2, playX+halfW-4, CONT_Y+24+panelH-2);

    // Status bar only (toolbar buttons replace bottom action buttons)
    if (canManage) {
        if (!allReady&&players.size()>1)
            ui_.drawWin98StatusBar(SCREEN_H-26,"Waiting for all players to ready up...");
        else
            ui_.drawWin98StatusBar(SCREEN_H-26,"All ready - click Start Game in the toolbar!");
    } else {
        ui_.drawWin98StatusBar(SCREEN_H-26,
            lobbyReady_?"You are ready. Click Unready in the toolbar to cancel.":"Click Ready Up in the toolbar when happy with the setup.");
    }

    // Floating chat window
    {
        const int chatW=400, chatH=280;
        if (!chatWinInit_) { chatWinInit_=true; chatWinX_=860; chatWinY_=430; }
        chatWinX_=std::max(0,std::min(SCREEN_W-chatW,chatWinX_));
        chatWinY_=std::max(0,std::min(SCREEN_H-chatH,chatWinY_));
        bool overTitle=ui_.pointInRect(ui_.mouseX,ui_.mouseY,chatWinX_,chatWinY_,chatW-22,UI::W98::TitleH);
        if (overTitle&&ui_.mouseClicked) { chatWinDrag_=true; chatWinDragOX_=ui_.mouseX-chatWinX_; chatWinDragOY_=ui_.mouseY-chatWinY_; }
        if (!ui_.mouseDown) chatWinDrag_=false;
        if (chatWinDrag_) {
            chatWinX_=std::max(0,std::min(SCREEN_W-chatW,ui_.mouseX-chatWinDragOX_));
            chatWinY_=std::max(0,std::min(SCREEN_H-chatH,ui_.mouseY-chatWinDragOY_));
        }
        ui_.drawWin98Window(chatWinX_,chatWinY_,chatW,chatH,"Chat");
        const int iX=chatWinX_+4, iY=chatWinY_+UI::W98::TitleH+2;
        const int inputH=22, fieldW=chatW-60, sendW=46;
        const int botY=chatWinY_+chatH-inputH-4, msgH=botY-iY-2;
        ui_.drawWin98Bevel(iX,iY,chatW-8,msgH,false);
        SDL_Rect prevCC; SDL_RenderGetClipRect(renderer_,&prevCC);
        {SDL_Rect mc={iX+2,iY+2,chatW-12,msgH-4}; SDL_RenderSetClipRect(renderer_,&mc);}
        const auto& hist=net.chatHistory();
        const int lH=14; int maxL=(msgH-4)/lH; if(maxL<1) maxL=1;
        int sm=(int)hist.size()-maxL; if(sm<0) sm=0;
        int mY=iY+msgH-4-(int)(hist.size()-sm)*lH;
        for (int mi=sm;mi<(int)hist.size();mi++) {
            char lb[128]; snprintf(lb,sizeof(lb),"%s: %s",hist[mi].sender.c_str(),hist[mi].text.c_str());
            std::string line=lb; while(line.size()>52&&!line.empty()) line.resize(line.size()-1);
            ui_.drawText(line.c_str(),iX+4,mY,11,UI::W98::Black); mY+=lH;
        }
        SDL_RenderSetClipRect(renderer_,prevCC.w>0?&prevCC:nullptr);
        {
            std::string disp=chatInputBuf_;
            ui_.drawWin98TextField(iX,botY,fieldW,inputH,disp.c_str(),chatTyping_,false,gameTime_);
            // Click field to start typing
            if (!chatTyping_&&ui_.pointInRect(ui_.mouseX,ui_.mouseY,iX,botY,fieldW,inputH)&&ui_.mouseClicked) {
                chatTyping_=true;
#ifndef __SWITCH__
                SDL_StartTextInput();
#endif
            }
            if (ui_.win98Button(210,"Send",iX+fieldW+2,botY,sendW,inputH,false)) {
                std::string msg(chatInputBuf_);
                if (!msg.empty()) { net.sendChat(msg); chatInputBuf_[0]='\0'; }
                chatTyping_=false;
            }
        }
    }
}

void Game::renderMultiplayerGame() {
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

        // Health bar - drawn first so name tag renders on top
        float barW = 40.0f;
        float barH = 4.0f;
        float hpRatio = (rp.maxHp > 0) ? (float)rp.hp / rp.maxHp : 0;
        SDL_FRect bgBar = {sp.x - barW / 2, sp.y - 28, barW, barH};
        SDL_FRect fgBar = {sp.x - barW / 2, sp.y - 28, barW * hpRatio, barH};

        // Name tag - above the HP bar
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

    // Kill/death/score + ping - compact panel in top-right corner
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

    // Team-pick sub-state
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

    // Normal menu items
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
    ui_.drawDesktop();

    const bool isWin = matchResult_.isWin;
    bool isNeutral = (matchResult_.reason == MatchEndReason::HostEnded ||
                      matchResult_.reason == MatchEndReason::Unknown);

    // Window title bar color: navy for neutral, dark-green for win, dark-red for loss
    const char* winTitle = matchResult_.headline.c_str();

    const int WX=120, WY=60, WW=SCREEN_W-240;
    const int TH=UI::W98::TitleH;

    // Estimate content height
    auto& netR = NetworkManager::instance();
    auto players = netR.players();
    std::sort(players.begin(), players.end(),
              [](const NetPlayer& a, const NetPlayer& b){ return a.kills > b.kills; });
    bool hasTable=(matchResult_.reason==MatchEndReason::TimeUp||matchResult_.reason==MatchEndReason::LastAlive);
    int tableRows=hasTable?(int)std::min(players.size(),(size_t)8):0;
    int WH=TH+20+24+20+20+20+(hasTable?(26+28+tableRows*26):40)+20+36+16;
    WH=std::max(WH,240);
    int WYc=(SCREEN_H-WH)/2;

    ui_.drawWin98Window(WX, WYc, WW, WH, winTitle);

    int cy=WYc+TH+16;

    // Outcome badge (navy bar)
    SDL_Color badgeC=isNeutral?SDL_Color{80,80,120,255}:isWin?SDL_Color{0,100,0,255}:SDL_Color{140,0,0,255};
    SDL_SetRenderDrawColor(renderer_, badgeC.r,badgeC.g,badgeC.b,255);
    SDL_Rect badge={WX+10,cy,WW-20,22}; SDL_RenderFillRect(renderer_,&badge);
    SDL_Color badgeTxt={255,255,255,255};
    drawTextCentered(matchResult_.headline.c_str(), cy+4, 14, badgeTxt);
    cy+=28;

    // Subtitle
    if (!matchResult_.subtitle.empty()) {
        drawTextCentered(matchResult_.subtitle.c_str(), cy, 13, UI::W98::Shadow);
        cy+=18;
    }

    // Match time
    {int s=(int)matchResult_.matchElapsed;
     char dur[48]; snprintf(dur,sizeof(dur),"Match time:  %d:%02d",s/60,s%60);
     drawTextCentered(dur, cy, 12, UI::W98::Shadow); cy+=20;}

    // Separator
    ui_.drawWin98Bevel(WX+10, cy, WW-20, 2, false); cy+=10;

    if (hasTable) {
        // Column headers inside inset panel
        const int tX=WX+14, tW=WW-28;
        const int c0=tX, c1=tX+30, c2=tX+tW-120, c3=tX+tW-60;
        SDL_SetRenderDrawColor(renderer_, 0,0,128,255);
        SDL_Rect hdr={tX,cy,tW,20}; SDL_RenderFillRect(renderer_,&hdr);
        drawText("#",      c0,    cy+3, 11, {255,255,255,255});
        drawText("PLAYER", c1,    cy+3, 11, {255,255,255,255});
        drawText("KILLS",  c2,    cy+3, 11, {255,255,255,255});
        drawText("STATUS", c3,    cy+3, 11, {255,255,255,255});
        cy+=22;

        for (size_t i=0;i<players.size()&&i<8;i++) {
            bool isLocal=(players[i].id==netR.localPlayerId());
            bool survived=!players[i].spectating;
            if (isLocal) SDL_SetRenderDrawColor(renderer_,200,215,240,255);
            else SDL_SetRenderDrawColor(renderer_,i%2==0?245:255,i%2==0?245:255,i%2==0?245:255,255);
            SDL_Rect row={tX,cy,tW,24}; SDL_RenderFillRect(renderer_,&row);
            SDL_SetRenderDrawColor(renderer_,160,160,160,255);
            SDL_Rect bot2={tX,cy+23,tW,1}; SDL_RenderFillRect(renderer_,&bot2);

            char rank[4],kills[8]; snprintf(rank,sizeof(rank),"%d",(int)i+1); snprintf(kills,sizeof(kills),"%d",players[i].kills);
            SDL_Color nc=isLocal?UI::W98::Black:UI::W98::Shadow;
            drawText(rank,           c0,    cy+5, 12, nc);
            drawText(players[i].username.c_str(), c1, cy+5, 13, nc);
            drawText(kills,          c2,    cy+5, 12, nc);
            drawText(survived?"Alive":"Out", c3, cy+5, 12,
                survived?SDL_Color{0,120,0,255}:SDL_Color{160,0,0,255});
            cy+=26;
        }
        cy+=6;
    } else if (matchResult_.reason==MatchEndReason::WavesCleared) {
        char w[64];
        if (lobbySettings_.waveCount>0) snprintf(w,sizeof(w),"Cleared %d waves!",lobbySettings_.waveCount);
        else snprintf(w,sizeof(w),"All enemies defeated!");
        drawTextCentered(w, cy, 14, {0,100,0,255}); cy+=20;
        drawTextCentered("All players survived", cy, 13, UI::W98::Shadow); cy+=20;
    }

    // Button
    const int btnW=220, btnX=WX+(WW-btnW)/2;
    if (ui_.win98Button(0,"View Full Scoreboard",btnX,cy,btnW,26,true)) confirmInput_=true;

    ui_.drawWin98StatusBar(SCREEN_H-26,"Press Enter or click to continue to full scoreboard");
}

void Game::renderScoreboard() {
    ui_.drawDesktop();

    auto& net = NetworkManager::instance();
    auto players = net.players();
    std::sort(players.begin(), players.end(),
              [](const NetPlayer& a, const NetPlayer& b){ return a.score > b.score; });

    const int ROW_H = 26;
    const int WX = 80, WY = 40, WW = SCREEN_W - 160;
    const int TH = UI::W98::TitleH;
    const int WH = TH + 14 + 24 + (int)players.size() * ROW_H + 10 + 36 + 14;
    const int WYc = std::max(WY, (SCREEN_H - WH) / 2);
    ui_.drawWin98Window(WX, WYc, WW, WH, "Scoreboard");

    const int tX = WX + 10, tW = WW - 20;
    const int c0 = tX, c1 = tX + 30, c2 = tX + tW - 200, c3 = tX + tW - 130, c4 = tX + tW - 60;

    int cy = WYc + TH + 10;

    // Navy column header bar
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
    SDL_Rect hdr = {tX, cy, tW, 22}; SDL_RenderFillRect(renderer_, &hdr);
    drawText("#",      c0, cy+4, 11, {255,255,255,255});
    drawText("PLAYER", c1, cy+4, 11, {255,255,255,255});
    drawText("KILLS",  c2, cy+4, 11, {255,255,255,255});
    drawText("DEATHS", c3, cy+4, 11, {255,255,255,255});
    drawText("SCORE",  c4, cy+4, 11, {255,255,255,255});
    cy += 24;

    for (size_t i = 0; i < players.size(); i++) {
        bool isLocal = (players[i].id == net.localPlayerId());
        // Row bg (config-menu style alternating)
        Uint8 bg = isLocal ? 200 : (i % 2 == 0 ? 232 : 245);
        SDL_SetRenderDrawColor(renderer_, bg, isLocal ? 210 : bg, isLocal ? 240 : bg, 255);
        SDL_Rect row = {tX, cy, tW, ROW_H}; SDL_RenderFillRect(renderer_, &row);
        // Bottom border
        SDL_SetRenderDrawColor(renderer_, 160, 160, 160, 255);
        SDL_Rect bot = {tX, cy + ROW_H - 1, tW, 1}; SDL_RenderFillRect(renderer_, &bot);
        // Blue left bar for local player (like config menu selection)
        if (isLocal) {
            SDL_SetRenderDrawColor(renderer_, 0, 0, 128, 255);
            SDL_Rect bar = {tX, cy, 3, ROW_H}; SDL_RenderFillRect(renderer_, &bar);
        }

        char rank[8], kills[16], deaths[16], score[16];
        snprintf(rank,   sizeof(rank),   "%d", (int)i + 1);
        snprintf(kills,  sizeof(kills),  "%d", players[i].kills);
        snprintf(deaths, sizeof(deaths), "%d", players[i].deaths);
        snprintf(score,  sizeof(score),  "%d", players[i].score);

        SDL_Color nc = isLocal ? UI::W98::Black : UI::W98::Shadow;
        drawText(rank,   c0, cy + 5, 12, i == 0 ? SDL_Color{160,100,0,255} : nc);
        drawText(players[i].username.c_str(), c1, cy + 5, 13, nc);
        ui_.drawTextRight(kills,  c2 + 40, cy + 5, 12, nc);
        ui_.drawTextRight(deaths, c3 + 40, cy + 5, 12, nc);
        ui_.drawTextRight(score,  c4 + 40, cy + 5, 12, nc);
        cy += ROW_H;
    }
    cy += 10;

    const int btnW = 160, btnX = WX + (WW - btnW) / 2;
    if (ui_.win98Button(0, "Continue", btnX, cy, btnW, 26, true)) confirmInput_ = true;

    ui_.drawWin98StatusBar(SCREEN_H - 26, "Press Enter to return to lobby");
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

        // Body - tint by team color or default blue
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

        // Render this client's sub-players (splitscreen partners)
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
            // Body - slightly different tint from primary
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

// Team Selection Screen

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

        // Team name - centered in box
        drawBoxCentered(teamNames[t], boxY + 20, 26, selected ? tc2 : gray);

        // Color swatch
        SDL_SetRenderDrawColor(renderer_, tc2.r, tc2.g, tc2.b, 200);
        SDL_Rect swatch = {cx - 20, boxY + 60, 40, 40};
        SDL_RenderFillRect(renderer_, &swatch);

        // Player count on this team - centered in box
        auto& net = NetworkManager::instance();
        int count = 0;
        for (auto& p : net.players()) {
            if (p.team == t) count++;
        }
        char countBuf[32];
        snprintf(countBuf, sizeof(countBuf), "%d player%s", count, count == 1 ? "" : "s");
        drawBoxCentered(countBuf, boxY + 120, 14, gray);

        // List player names on this team - centered in box
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

// Mod Menu

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

    // Tab row
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

    // Content area (sunken list box)
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
        // Mods tab
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
                if (hovered && ui_.mouseClicked) { modMenuSelection_ = i; ui_.mouseClicked = false; }

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
        // Content tabs (Characters / Maps / Playlists)
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

    // Bottom buttons
    const int btnY = winY + winH - 42;
    auto& modsList = mm.mods();

    // Toggle button (Mods tab only, enabled when a mod row is selected)
    if (modMenuTab_ == 0 && !modsList.empty() && modMenuSelection_ < (int)modsList.size()) {
        const char* togLbl = modsList[modMenuSelection_].enabled ? "Disable" : "Enable";
        if (ui_.win98Button(63, togLbl, winX + pad, btnY, 86, 26, false)) {
            std::string id = modsList[modMenuSelection_].id;
            mm.setEnabled(id, !modsList[modMenuSelection_].enabled);
            applyModOverrides();
        }
    }

    // Close button - uses backInput_ so it never accidentally toggles a mod
    if (ui_.win98Button(62, "Close", winX + winW - pad - 86, btnY, 86, 26, false)) {
        backInput_ = true;
    }

    { UI::HintPair hints[] = { {UI::Action::Confirm, "Toggle"}, {UI::Action::Back, "Close"} };
      ui_.drawHintBar(hints, 2); }
}

