#pragma once
// Text-based INI format describing a "playlist" of maps + included character(s):
//
// [pack]
// name=My Campaign
// creator=PlayerName
// description=A short campaign with 3 levels
// version=1
//
// [character]
// path=characters/hero/hero.cschar
// ;; additional characters can be added:
// ;; path2=characters/alt/alt.cschar
//
// [maps]
// count=3
// map1=maps/level1.csm
// map2=maps/level2.csm
// map3=maps/level3.csm
//
// Maps are played in order. The player uses the pack's included character(s).
#include <string>
#include <vector>

struct MapPackEntry {
    std::string path;         // path to .csm file
    std::string name;         // fallback display name (from .csm header or filename)
    std::string displayName;  // per-map title override (empty = use name)
    std::string description;  // short description shown in map select
    std::string musicPath;    // override music file (resolved absolute path, empty = use map/default)
    bool        completed = false;

    const std::string& title() const { return displayName.empty() ? name : displayName; }
};

struct MapPack {
    std::string name        = "Untitled Pack";
    std::string creator     = "Unknown";
    std::string description;
    int         version     = 1;
    std::string folder;     // path to pack folder
    std::string iconPath;   // path to icon image (resolved absolute, empty = none)
    std::vector<std::string> tags;  // searchable tags (e.g. "action", "story", "short")

    // Included character path(s)
    std::vector<std::string> characterPaths;

    // Map playlist
    std::vector<MapPackEntry> maps;

    // Current progress
    int currentMapIndex = 0;

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    // Helpers
    bool hasNextMap() const { return currentMapIndex + 1 < (int)maps.size(); }
    std::string currentMapPath() const;
    bool advance() { if (hasNextMap()) { currentMapIndex++; return true; } return false; }
    void reset() { currentMapIndex = 0; for (auto& m : maps) m.completed = false; }
};

// Scan a directory for .cspack files
std::vector<MapPack> scanMapPacks(const std::string& baseDir);
