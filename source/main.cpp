// ─── main.cpp ─── Entry point for COLD START ────────────────────────────────
#include "game.h"
#include <cstdio>
#include <cstdlib>

#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char* argv[]) {
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
