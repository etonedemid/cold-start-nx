// ─── update_checker.cpp ─── Version update checker ──────────────────────────
#include "update_checker.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
#elif defined(__SWITCH__)
    #include <switch.h>
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
#endif

namespace UpdateChecker {

static bool initialized = false;

static void initSockets() {
    if (initialized) return;
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    initialized = true;
}

// Simple HTTP GET request implementation
static std::string httpGet(const char* host, const char* path) {
    initSockets();

    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, "443", &hints, &result) != 0) {
        // Try port 80 if SSL not available
        if (getaddrinfo(host, "80", &hints, &result) != 0) {
            return "";
        }
    }

    int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(result);
        return "";
    }

    // Set timeout to 5 seconds
    #ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    #else
    struct timeval tv = {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    #endif

    if (connect(sock, result->ai_addr, result->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(result);
        return "";
    }
    freeaddrinfo(result);

    // Build HTTP request
    char request[1024];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: ColdStart/1.0\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n", path, host);

    send(sock, request, strlen(request), 0);

    // Read response
    std::string response;
    char buffer[4096];
    int bytesRead;
    while ((bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    close(sock);

    // Extract body (after \r\n\r\n)
    size_t bodyStart = response.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        return response.substr(bodyStart + 4);
    }
    return "";
}

// Parse version from GitHub API JSON response
static std::string parseVersionFromJson(const std::string& json) {
    // Look for "tag_name":"vX.Y.Z"
    size_t pos = json.find("\"tag_name\"");
    if (pos == std::string::npos) return "";
    
    pos = json.find("\"", pos + 11);
    if (pos == std::string::npos) return "";
    
    pos++; // Skip opening quote
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    
    std::string tag = json.substr(pos, end - pos);
    
    // Strip leading 'v' if present
    if (!tag.empty() && tag[0] == 'v') {
        tag = tag.substr(1);
    }
    
    return tag;
}

std::string fetchLatestVersion(const char* owner, const char* repo) {
    char path[512];
    snprintf(path, sizeof(path), "/repos/%s/%s/releases/latest", owner, repo);
    
    std::string response = httpGet("api.github.com", path);
    if (response.empty()) return "";
    
    return parseVersionFromJson(response);
}

bool isNewerVersion(const char* current, const char* latest) {
    if (!current || !latest) return false;
    
    int currMajor = 0, currMinor = 0, currPatch = 0;
    int latestMajor = 0, latestMinor = 0, latestPatch = 0;
    
    // Parse current version
    sscanf(current, "%d.%d.%d", &currMajor, &currMinor, &currPatch);
    
    // Parse latest version
    sscanf(latest, "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
    
    // Compare
    if (latestMajor > currMajor) return true;
    if (latestMajor < currMajor) return false;
    
    if (latestMinor > currMinor) return true;
    if (latestMinor < currMinor) return false;
    
    return latestPatch > currPatch;
}

} // namespace UpdateChecker

// ─── Switch-only: download NRO from GitHub releases ──────────────────────────
#ifdef __SWITCH__
#include <curl/curl.h>
#include <cstdio>
#include <functional>
#include <cstdint>

namespace UpdateChecker {

namespace {
    struct WriteCtx { FILE* fp; };
    static size_t onWrite(void* ptr, size_t size, size_t nmemb, void* ud) {
        auto* c = static_cast<WriteCtx*>(ud);
        return fwrite(ptr, size, nmemb, c->fp);
    }

    struct ProgressCtx {
        std::function<void(int64_t, int64_t)> fn;
    };
    static int onProgress(void* ud, curl_off_t dlTotal, curl_off_t dlNow,
                          curl_off_t, curl_off_t) {
        auto* c = static_cast<ProgressCtx*>(ud);
        if (c && c->fn) c->fn((int64_t)dlNow, (int64_t)dlTotal);
        return 0;
    }
} // anonymous namespace

bool downloadNro(const char* owner, const char* repo, const char* version,
                 const char* destPath,
                 std::function<void(int64_t done, int64_t total)> progressFn) {
    // Build GitHub release asset URL
    char url[512];
    snprintf(url, sizeof(url),
        "https://github.com/%s/%s/releases/download/v%s/cold-start-nx.nro",
        owner, repo, version);

    // Write to a temp file next to the destination
    char tmpPath[256];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", destPath);

    FILE* fp = fopen(tmpPath, "wb");
    if (!fp) return false;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        curl_global_cleanup();
        return false;
    }

    WriteCtx    wctx{fp};
    ProgressCtx pctx{progressFn};

    curl_easy_setopt(curl, CURLOPT_URL,             url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   onWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &wctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,    &pctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,      0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  0L);  // no CA bundle on Switch
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,       "ColdStart-Switch/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         180L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    fclose(fp);

    if (res != CURLE_OK) {
        remove(tmpPath);
        return false;
    }

    // Atomically replace the running NRO
    remove(destPath);
    if (rename(tmpPath, destPath) != 0) {
        remove(tmpPath);
        return false;
    }
    return true;
}

} // namespace UpdateChecker
#endif // __SWITCH__
