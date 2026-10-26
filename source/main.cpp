#include "game.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
static LONG WINAPI handleFatalException(PEXCEPTION_POINTERS info) {
    FILE* f = fopen("cold_start.log", "a");
    if (f) {
        fprintf(f, "\n[FATAL] Unhandled exception 0x%08lX at 0x%p\n",
                (unsigned long)info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress);
        fclose(f);
    }
    MessageBoxA(nullptr,
        "cold_start crashed.\nSee cold_start.log for details.\n\n"
        "Please report this at github.com/etonedemid/cold-start-nx/issues",
        "Fatal Error", MB_OK | MB_ICONERROR);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void handleFatalSignal(int sig) {
    fprintf(stderr, "[FATAL] Signal %d received\n", sig);
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

#ifdef __SWITCH__
#include <switch.h>
#include <unistd.h>
#endif
#ifdef __WIIU__
#include <unistd.h>
#include <nn/ac/ac_c.h>
#include <nn/result.h>
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
#if !defined(__SWITCH__) && !defined(_WIN32) && !defined(__ANDROID__) && !defined(__WIIU__)
    // Prefer Wayland over X11 when running in a Wayland session (Linux only).
    // setenv with overwrite=0 so a user-set SDL_VIDEODRIVER is still respected.
    // Wii U excluded: its only SDL video driver is "wiiu"; forcing wayland/x11
    // makes SDL_Init(VIDEO) fail with "wayland,x11 not available".
    setenv("SDL_VIDEODRIVER", "wayland,x11", 0);
    // Use SDL's GLES2 renderer instead of the legacy "opengl" one. The legacy GL
    // backend has a batching bug on some Mesa drivers (SDL_RenderCopyExF vertex
    // collapse) which previously forced batching off and tanked performance.
    // GLES2 batches correctly, so we keep batching on (below). overwrite=0 keeps
    // any user-set SDL_RENDER_DRIVER, and the dedicated-server block below still
    // overrides this with "software".
    setenv("SDL_RENDER_DRIVER", "opengles2", 0);
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

    // Batching on everywhere: an unbatched draw call per sprite is a big cost
    // (Wii U ~15 fps without it). The Linux-GL CopyExF vertex-collapse bug that
    // used to force this off is sidestepped by selecting the GLES2 renderer on
    // Linux (above); console backends and Windows D3D batch fine too.
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
#ifdef _WIN32
    // Prevent Windows IME initialisation from freezing the game on first text input.
    // SDL internally calls ImmAssociateContext which blocks until the IME is ready;
    // suppressing the IME UI makes that call return immediately.
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
#endif
#ifdef __ANDROID__
    // Prevent SDL from synthesising mouse events from touch - without this,
    // every finger-down also fires SDL_MOUSEBUTTONDOWN which triggers shooting
    // and every finger-move fires SDL_MOUSEMOTION which moves the aim cursor.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#ifdef __SWITCH__
    socketInitializeDefault();
    nxlinkStdio();
    // Set CWD to the NRO directory so all relative opendir/fopen calls resolve
    // against sdmc:/switch/<folder>/ - hbmenu provides argv[0] with the sdmc: prefix.
    if (argc > 0 && argv[0] && argv[0][0]) {
        char nroDir[512];
        strncpy(nroDir, argv[0], sizeof(nroDir) - 1);
        nroDir[sizeof(nroDir) - 1] = '\0';
        char* lastSlash = strrchr(nroDir, '/');
        if (lastSlash) { *lastSlash = '\0'; chdir(nroDir); }
    }
#endif
#ifdef __WIIU__
    // Bring up the network so ENet's BSD sockets work. nn::ac assigns an IP via
    // the system's saved connection; non-fatal if there's no network (the game
    // just can't host/join). The socket library itself auto-inits on first use.
    if (NNResult_IsSuccess(ACInitialize())) {
        if (NNResult_IsFailure(ACConnect())) {
            printf("Wii U: ACConnect failed - multiplayer unavailable\n");
        } else {
            uint32_t ip = 0;
            ACGetAssignedAddress(&ip);
            printf("Wii U network up: %u.%u.%u.%u\n",
                   (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        }
    }
#endif

#ifdef _WIN32
    SetUnhandledExceptionFilter(handleFatalException);
#else
    signal(SIGSEGV, handleFatalSignal);
    signal(SIGABRT, handleFatalSignal);
    signal(SIGFPE,  handleFatalSignal);
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
