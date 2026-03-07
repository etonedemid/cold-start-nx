// ─── main.cpp ─── Entry point for COLD START ────────────────────────────────
#include "game.h"
#include <cstdio>
#include <cstdlib>

#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // GUI builds have no console (-mwindows). Redirect stdout/stderr to a log
    // file so printf doesn't hit an invalid handle and pop an error dialog.
    freopen("cold_start.log", "w", stdout);
    freopen("cold_start.log", "a", stderr);
    // stdin is unused but silence the invalid-handle check in the CRT
    freopen("NUL", "r", stdin);
    setvbuf(stdout, nullptr, _IONBF, 0);
#endif
#if !defined(__SWITCH__) && !defined(_WIN32)
    // Prefer Wayland over X11 when running in a Wayland session.
    // setenv with overwrite=0 so a user-set SDL_VIDEODRIVER is still respected.
    setenv("SDL_VIDEODRIVER", "wayland,x11", 0);
#endif
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "0");
#ifdef __SWITCH__
    socketInitializeDefault();
    nxlinkStdio();
#endif

    printf("COLD START launching...\n");

    Game game;
    if (!game.init()) {
        printf("Failed to initialize game!\n");
#ifdef __SWITCH__
        socketExit();
#endif
        return 1;
    }

    game.run();
    game.shutdown();

#ifdef __SWITCH__
    socketExit();
#endif
    return 0;
}
