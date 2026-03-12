#pragma once
// ─── update_checker.h ─── Version update checker ────────────────────────────
#include <string>

namespace UpdateChecker {
    // Fetches latest release version from GitHub API
    // Returns empty string on failure
    std::string fetchLatestVersion(const char* owner, const char* repo);

    // Compares two version strings (format: "X.Y.Z")
    // Returns true if 'latest' is newer than 'current'
    bool isNewerVersion(const char* current, const char* latest);
}
