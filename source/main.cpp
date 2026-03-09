// ─── main.cpp ─── Entry point for COLD START ────────────────────────────────
#include "game.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char* argv[]) {
    bool dedicated = false;
    uint16_t dedicatedPort = 7777;
    int dedicatedMaxPlayers = 16;
    std::string dedicatedPassword;
    std::string dedicatedName = "DedicatedServer";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dedicated") == 0) {
            dedicated = true;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            dedicatedPort = (uint16_t)std::max(1, std::min(65535, atoi(argv[++i])));
        } else if (strcmp(argv[i], "--max-players") == 0 && i + 1 < argc) {
            dedicatedMaxPlayers = std::max(2, std::min(128, atoi(argv[++i])));
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            dedicatedPassword = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            dedicatedName = argv[++i];
        }
    }

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

    if (dedicated) {
#if !defined(__SWITCH__)
    #if defined(_WIN32)
        _putenv_s("SDL_VIDEODRIVER", "dummy");
        _putenv_s("SDL_AUDIODRIVER", "dummy");
        _putenv_s("SDL_RENDER_DRIVER", "software");
    #else
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        setenv("SDL_RENDER_DRIVER", "software", 1);
    #endif
#endif
    }

    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "0");
#ifdef __SWITCH__
    socketInitializeDefault();
    nxlinkStdio();
#endif

    printf("COLD START launching...\n");

    Game game;
    if (dedicated) {
        game.configureDedicatedServer(dedicatedPort, dedicatedMaxPlayers,
                                      dedicatedPassword, dedicatedName);
        printf("Dedicated mode enabled (port=%u, maxPlayers=%d)\n",
               dedicatedPort, dedicatedMaxPlayers);
    }
    if (!game.init()) {
        printf("Failed to initialize game!\n");
        game.shutdown();
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
