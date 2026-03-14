// ─── discord_rpc.cpp ── Minimal Discord Rich Presence via IPC ────────────────
// Connects to the Discord desktop client's local IPC socket/pipe and sends
// SET_ACTIVITY commands.  No external dependency — pure Win32 / POSIX sockets.
// Stubs out to no-ops on non-PC platforms (Switch, Android, server).
#include "discord_rpc.h"

#if defined(PLATFORM_PC) && !defined(DEDICATED_SERVER)

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <fcntl.h>
#  include <errno.h>
#endif

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20)  ;   // strip control chars silently
        else                out += (char)c;
    }
    return out;
}

static std::string buildSetActivity(const DiscordActivity& a, int64_t pid, int64_t nonce) {
    char ts[64] = "";
    if (a.startTime > 0)
        snprintf(ts, sizeof(ts), ",\"timestamps\":{\"start\":%lld}", (long long)a.startTime);

    char img[256] = "";
    if (!a.largeImageKey.empty())
        snprintf(img, sizeof(img),
                 ",\"assets\":{\"large_image\":\"%s\",\"large_text\":\"%s\"}",
                 jsonEscape(a.largeImageKey).c_str(),
                 jsonEscape(a.largeImageText).c_str());

    char buf[4096];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"SET_ACTIVITY\","
             "\"args\":{\"pid\":%lld,"
             "\"activity\":{\"details\":\"%s\",\"state\":\"%s\"%s%s}},"
             "\"nonce\":\"%lld\"}",
             (long long)pid,
             jsonEscape(a.details).c_str(),
             jsonEscape(a.state).c_str(),
             ts, img,
             (long long)nonce);
    return buf;
}

// ── Singleton ─────────────────────────────────────────────────────────────────

DiscordRPC& DiscordRPC::instance() {
    static DiscordRPC inst;
    return inst;
}

// ── Platform I/O ─────────────────────────────────────────────────────────────

bool DiscordRPC::sendRaw(uint32_t op, const char* json, uint32_t len) {
#ifdef _WIN32
    if (pipe_ == (void*)(intptr_t)-1) return false;
    uint32_t hdr[2] = {op, len};
    DWORD written = 0;
    if (!WriteFile((HANDLE)pipe_, hdr, 8, &written, nullptr) || written != 8)
        return false;
    if (!WriteFile((HANDLE)pipe_, json, len, &written, nullptr) || written != len)
        return false;
    return true;
#else
    if (fd_ < 0) return false;
    uint32_t hdr[2] = {op, len};
    ssize_t r1 = ::send(fd_, hdr,  8,   MSG_NOSIGNAL);
    ssize_t r2 = ::send(fd_, json, len, MSG_NOSIGNAL);
    return r1 == 8 && r2 == (ssize_t)len;
#endif
}

bool DiscordRPC::tryConnect() {
#ifdef _WIN32
    for (int i = 0; i < 10; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileA(path,
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            pipe_ = (void*)h;
            return true;
        }
    }
    return false;
#else
    const char* dirs[] = {
        getenv("XDG_RUNTIME_DIR"),
        getenv("TMPDIR"),
        "/tmp",
        nullptr
    };
    for (int di = 0; dirs[di]; ++di) {
        for (int i = 0; i < 10; ++i) {
            char path[256];
            snprintf(path, sizeof(path), "%s/discord-ipc-%d", dirs[di], i);

            int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (s < 0) continue;

            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

            if (::connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                fcntl(s, F_SETFL, O_NONBLOCK);
                fd_ = s;
                return true;
            }
            ::close(s);
        }
    }
    return false;
#endif
}

void DiscordRPC::pumpRead() {
    // Drain any responses so the IPC buffer never fills up.
    // We don't need to parse them for basic presence.
#ifdef _WIN32
    if (pipe_ == (void*)(intptr_t)-1) return;
    DWORD avail = 0;
    if (!PeekNamedPipe((HANDLE)pipe_, nullptr, 0, nullptr, &avail, nullptr)) {
        // Pipe broken — mark disconnected
        CloseHandle((HANDLE)pipe_);
        pipe_ = (void*)(intptr_t)-1;
        connected_ = shook_ = false;
        return;
    }
    if (avail == 0) return;
    char tmp[1024];
    DWORD rd = 0;
    ReadFile((HANDLE)pipe_, tmp, static_cast<DWORD>(sizeof(tmp)), &rd, nullptr);
#else
    if (fd_ < 0) return;
    char tmp[1024];
    ssize_t n;
    while ((n = ::recv(fd_, tmp, sizeof(tmp), 0)) > 0) {}
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        // Peer closed or real error — reconnect next tick
        ::close(fd_);
        fd_ = -1;
        connected_ = shook_ = false;
    }
#endif
}

void DiscordRPC::handshake() {
    char hs[256];
    snprintf(hs, sizeof(hs), "{\"v\":1,\"client_id\":\"%s\"}", appId_.c_str());
    sendRaw(0, hs, static_cast<uint32_t>(strlen(hs)));
}

void DiscordRPC::flushPending() {
    if (!hasPending_ || !shook_) return;
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    int64_t pid = static_cast<int64_t>(getpid());
#endif
    std::string payload = buildSetActivity(pending_, static_cast<int64_t>(pid), nonce_++);
    if (sendRaw(1, payload.c_str(), static_cast<uint32_t>(payload.size()))) {
        hasPending_ = false;
    } else {
        // Write failure → assume disconnect; retry on next tick
#ifdef _WIN32
        if (pipe_ != (void*)(intptr_t)-1) {
            CloseHandle((HANDLE)pipe_);
            pipe_ = (void*)(intptr_t)-1;
        }
#else
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
        connected_ = shook_ = false;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void DiscordRPC::init() {
    // Lazy — first tick handles connection
    retryTimer_ = 1.0f; // small grace period at startup
}

void DiscordRPC::shutdown() {
#ifdef _WIN32
    if (pipe_ != (void*)(intptr_t)-1) {
        CloseHandle((HANDLE)pipe_);
        pipe_ = (void*)(intptr_t)-1;
    }
#else
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
    connected_ = shook_ = false;
}

void DiscordRPC::setActivity(const DiscordActivity& a) {
    pending_    = a;
    hasPending_ = true;
    if (connected_ && shook_) flushPending();
}

void DiscordRPC::tick(float dt) {
    if (connected_) {
        pumpRead();
        if (!connected_) return; // pumpRead detected disconnect
        if (!shook_) { handshake(); shook_ = true; }
        flushPending();
        return;
    }
    retryTimer_ -= dt;
    if (retryTimer_ > 0.f) return;
    retryTimer_ = 15.0f;

    if (tryConnect()) {
        connected_ = true;
        shook_     = false;
        handshake();
        shook_ = true;
        flushPending();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
#else  // ── Stubs for Switch / server / Android ──────────────────────────────

DiscordRPC& DiscordRPC::instance() { static DiscordRPC inst; return inst; }
void DiscordRPC::init()                           {}
void DiscordRPC::shutdown()                       {}
void DiscordRPC::setActivity(const DiscordActivity&) {}
void DiscordRPC::tick(float)                      {}
bool DiscordRPC::sendRaw(uint32_t, const char*, uint32_t) { return false; }
bool DiscordRPC::tryConnect()                     { return false; }
void DiscordRPC::pumpRead()                       {}
void DiscordRPC::handshake()                      {}
void DiscordRPC::flushPending()                   {}

#endif
