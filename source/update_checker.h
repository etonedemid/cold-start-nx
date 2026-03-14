#pragma once
// ─── update_checker.h ─── Version update checker ────────────────────────────
#include <string>
#include <functional>
#include <cstdint>

namespace UpdateChecker {
    // Fetches latest release version from GitHub API
    // Returns empty string on failure
    std::string fetchLatestVersion(const char* owner, const char* repo);

    // Compares two version strings (format: "X.Y.Z")
    // Returns true if 'latest' is newer than 'current'
    bool isNewerVersion(const char* current, const char* latest);

#ifdef __SWITCH__
    // Downloads cold_start.nro for the given version from GitHub releases
    // and overwrites destPath with it.  progressFn(done, total) is called
    // periodically from the download thread (total may be 0 if unknown).
    // Returns true on success.
    bool downloadNro(const char* owner, const char* repo, const char* version,
                     const char* destPath,
                     std::function<void(int64_t done, int64_t total)> progressFn);
#endif
}
