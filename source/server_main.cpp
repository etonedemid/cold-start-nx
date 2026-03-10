// ─── server_main.cpp ─── Headless dedicated server entry point ───────────────
// Compiled when SERVER_ONLY=ON. Hosts a game without any SDL/graphics.
// Usage: cold_start [--port N] [--max-players N] [--password PW] [--name NAME]
// ─────────────────────────────────────────────────────────────────────────────
#include "network.h"
#include "gamemode.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <csignal>
#include <ctime>
#include <string>

#if !defined(__SWITCH__)

#ifdef _WIN32
#include <windows.h>
static void serverSleep(int ms) { Sleep(ms); }
#else
#include <time.h>
static void serverSleep(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}
#endif

static volatile bool g_running = true;

static void onSignal(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    uint16_t    port       = 7777;
    int         maxPlayers = 16;
    std::string password;
    std::string name       = "DedicatedServer";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            char* end = nullptr;
            long v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || v < 1 || v > 65535) {
                fprintf(stderr, "Invalid port: %s (must be 1-65535)\n", argv[i]);
                return 1;
            }
            port = (uint16_t)v;
        } else if (strcmp(argv[i], "--max-players") == 0 && i + 1 < argc) {
            maxPlayers = std::max(2, std::min(128, atoi(argv[++i])));
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            password = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        }
    }

    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    printf("COLD START Dedicated Server\n");
    printf("  port=%u  maxPlayers=%d  name=%s\n", port, maxPlayers, name.c_str());

    GameModeRegistry::instance().registerBuiltins();

    auto& net = NetworkManager::instance();
    if (!net.init()) {
        fprintf(stderr, "Failed to initialise network\n");
        return 1;
    }

    net.setUsername(name);
    if (!password.empty())
        net.setHostPassword(password);
    net.setDedicatedServer(true);

    if (!net.host(port, maxPlayers)) {
        fprintf(stderr, "Failed to start server on UDP port %u\n", port);
        net.shutdown();
        return 1;
    }

    // Log player join/leave events
    net.onPlayerJoined = [](uint8_t id, const std::string& uname) {
        printf("[+] Player %u joined: %s\n", id, uname.c_str());
    };
    net.onPlayerLeft = [](uint8_t id) {
        printf("[-] Player %u left\n", id);
    };
    net.onChatMessage = [](const std::string& sender, const std::string& text) {
        printf("[chat] %s: %s\n", sender.c_str(), text.c_str());
    };

    // Track lobby settings so we can use the correct map size when starting
    LobbySettings serverSettings;
    net.onConfigSyncReceived = [&serverSettings](const LobbySettings& s) {
        serverSettings = s;
        printf("[cfg] Map %dx%d  enemyHp=%.1fx  spawnRate=%.1fx\n",
               s.mapWidth, s.mapHeight, s.enemyHpScale, s.spawnRateScale);
    };

    // Start the game when the lobby host requests it
    net.onLobbyStartRequested = [&net, &serverSettings]() {
        uint32_t seed = (uint32_t)time(nullptr);
        printf("[start] Lobby host requested game start — seed=%u  map=%dx%d\n",
               seed, serverSettings.mapWidth, serverSettings.mapHeight);
        net.startGame(seed, serverSettings.mapWidth, serverSettings.mapHeight, {});
    };

    printf("Server running on UDP port %u. Press Ctrl+C to stop.\n", port);

    // ~60 Hz server loop
    while (g_running) {
        net.update(1.0f / 60.0f);
        serverSleep(16);
    }

    printf("Shutting down...\n");
    net.disconnect();
    net.shutdown();
    return 0;
}

#endif
