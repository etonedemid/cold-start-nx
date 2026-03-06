// ─── main.cpp ─── Entry point for COLD START ────────────────────────────────
#include "game.h"
#include <cstdio>

#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char* argv[]) {
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
