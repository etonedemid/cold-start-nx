// ─── mappack.cpp ─── Map Pack implementation ────────────────────────────────
#include "mappack.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

// Simple INI line parser
static bool parseLine(const std::string& line, std::string& key, std::string& value) {
    if (line.empty() || line[0] == ';' || line[0] == '#') return false;
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = line.substr(0, eq);
    value = line.substr(eq + 1);
    // Trim whitespace
    while (!key.empty() && key.back() == ' ') key.pop_back();
    while (!value.empty() && value[0] == ' ') value = value.substr(1);
    return true;
}

bool MapPack::loadFromFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;

    // Store folder path
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) folder = path.substr(0, slash + 1);
    else folder = "";

    char buf[512];
    std::string section;
    maps.clear();
    characterPaths.clear();
    int mapCount = 0;

    while (fgets(buf, sizeof(buf), f)) {
        std::string line(buf);
        // Strip newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.empty()) continue;

        // Section header
        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) section = line.substr(1, end - 1);
            continue;
        }

        std::string key, value;
        if (!parseLine(line, key, value)) continue;

        if (section == "pack") {
            if (key == "name") name = value;
            else if (key == "creator") creator = value;
            else if (key == "description") description = value;
            else if (key == "version") version = atoi(value.c_str());
        }
        else if (section == "character") {
            // Accept path, path1, path2, etc.
            if (key.substr(0, 4) == "path") {
                // Resolve relative to pack folder
                std::string resolved = value;
                if (!folder.empty() && value[0] != '/') resolved = folder + value;
                characterPaths.push_back(resolved);
            }
        }
        else if (section == "maps") {
            if (key == "count") {
                mapCount = atoi(value.c_str());
            }
            else if (key.substr(0, 3) == "map") {
                MapPackEntry entry;
                entry.path = value;
                if (!folder.empty() && value[0] != '/') entry.path = folder + value;
                // Extract display name from filename
                size_t s = value.find_last_of('/');
                entry.name = (s != std::string::npos) ? value.substr(s + 1) : value;
                // Strip .csm extension
                if (entry.name.size() > 4 && entry.name.substr(entry.name.size() - 4) == ".csm")
                    entry.name = entry.name.substr(0, entry.name.size() - 4);
                maps.push_back(entry);
            }
        }
    }
    fclose(f);

    currentMapIndex = 0;
    printf("MapPack loaded: %s (%d maps, %d characters)\n",
        name.c_str(), (int)maps.size(), (int)characterPaths.size());
    return !maps.empty();
}

bool MapPack::saveToFile(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;

    fprintf(f, "[pack]\n");
    fprintf(f, "name=%s\n", name.c_str());
    fprintf(f, "creator=%s\n", creator.c_str());
    fprintf(f, "description=%s\n", description.c_str());
    fprintf(f, "version=%d\n", version);

    fprintf(f, "\n[character]\n");
    for (int i = 0; i < (int)characterPaths.size(); i++) {
        if (i == 0) fprintf(f, "path=%s\n", characterPaths[i].c_str());
        else fprintf(f, "path%d=%s\n", i + 1, characterPaths[i].c_str());
    }

    fprintf(f, "\n[maps]\n");
    fprintf(f, "count=%d\n", (int)maps.size());
    for (int i = 0; i < (int)maps.size(); i++) {
        fprintf(f, "map%d=%s\n", i + 1, maps[i].path.c_str());
    }

    fclose(f);
    return true;
}

std::string MapPack::currentMapPath() const {
    if (currentMapIndex >= 0 && currentMapIndex < (int)maps.size())
        return maps[currentMapIndex].path;
    return "";
}

std::vector<MapPack> scanMapPacks(const std::string& baseDir) {
    std::vector<MapPack> packs;
    DIR* dir = opendir(baseDir.c_str());
    if (!dir) return packs;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname(entry->d_name);
        // Look for .cspack files
        if (fname.size() > 7 && fname.substr(fname.size() - 7) == ".cspack") {
            MapPack pack;
            std::string path = baseDir + "/" + fname;
            if (pack.loadFromFile(path))
                packs.push_back(std::move(pack));
        }
        // Also look in subdirectories for pack.cspack
        if (fname != "." && fname != "..") {
            std::string subdir = baseDir + "/" + fname;
            struct stat st;
            if (stat(subdir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                std::string packFile = subdir + "/pack.cspack";
                MapPack pack;
                if (pack.loadFromFile(packFile))
                    packs.push_back(std::move(pack));
            }
        }
    }
    closedir(dir);
    return packs;
}
