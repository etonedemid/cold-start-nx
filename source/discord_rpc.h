// ─── discord_rpc.h ── Minimal Discord Rich Presence (IPC / pipe) ─────────────
// Set your Application ID from https://discord.com/developers/applications
// before building, e.g. -DDISCORD_APP_ID=\"123456789012345\"
#pragma once
#include <string>
#include <cstdint>

#ifndef DISCORD_APP_ID
#  define DISCORD_APP_ID "1482356403086561347"
#endif

struct DiscordActivity {
    std::string details;        // upper line  e.g. "Wave 5"
    std::string state;          // lower line  e.g. "Playing Solo"
    int64_t     startTime = 0;  // Unix seconds; 0 = no elapsed timer
    std::string largeImageKey;  // asset key from the dev portal
    std::string largeImageText; // tooltip for the large image
};

class DiscordRPC {
public:
    static DiscordRPC& instance();

    void init();
    void shutdown();
    void setActivity(const DiscordActivity& a);
    void tick(float dt);  // call every frame; handles lazy-connect & reconnect

private:
    DiscordRPC() = default;

    bool tryConnect();
    bool sendRaw(uint32_t op, const char* json, uint32_t len);
    void pumpRead();
    void handshake();
    void flushPending();

    std::string     appId_      = DISCORD_APP_ID;
    bool            connected_  = false;
    bool            shook_      = false;   // handshake sent
    float           retryTimer_ = 0.f;
    DiscordActivity pending_;
    bool            hasPending_ = false;
    int64_t         nonce_      = 1;

#ifdef _WIN32
    void*  pipe_ = (void*)(intptr_t)-1; // HANDLE; INVALID_HANDLE_VALUE sentinel
#else
    int    fd_   = -1;
#endif
};
