// ─── mod.cpp ─── Mod system implementation ──────────────────────────────────
#include "mod.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#  define mkdir(p, m) _mkdir(p)
#endif

// ── INI parser helper (reusable) ──
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
parseINI(const std::string& path) {
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections;
    std::ifstream f(path);
    if (!f.is_open()) return sections;

    std::string currentSection;
    std::string line;
    while (std::getline(f, line)) {
        // Trim
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string::npos) {
                currentSection = line.substr(1, close - 1);
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // Trim key and value
            size_t ks = key.find_first_not_of(" \t");
            size_t ke = key.find_last_not_of(" \t");
            if (ks != std::string::npos && ke != std::string::npos) key = key.substr(ks, ke - ks + 1);
            size_t vs = val.find_first_not_of(" \t");
            size_t ve = val.find_last_not_of(" \t");
            if (vs != std::string::npos && ve != std::string::npos) val = val.substr(vs, ve - vs + 1);

            sections[currentSection][key] = val;
        }
    }
    return sections;
}

static bool toBool(const std::string& s) {
    return s == "true" || s == "1" || s == "yes";
}

static bool dirExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::vector<std::string> listFiles(const std::string& dir, const std::string& ext) {
    std::vector<std::string> result;
    DIR* d = opendir(dir.c_str());
    if (!d) return result;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() > ext.size() && name.substr(name.size() - ext.size()) == ext) {
            result.push_back(dir + "/" + name);
        }
    }
    closedir(d);
    return result;
}

// ── ModOverrides ──
float ModOverrides::getFloat(const std::string& key, float def) const {
    auto it = values.find(key);
    if (it == values.end()) return def;
    char* end = nullptr;
    const char* s = it->second.c_str();
    float v = std::strtof(s, &end);
    if (end == s) return def;
    return v;
}

int ModOverrides::getInt(const std::string& key, int def) const {
    auto it = values.find(key);
    if (it == values.end()) return def;
    char* end = nullptr;
    const char* s = it->second.c_str();
    long v = std::strtol(s, &end, 10);
    if (end == s) return def;
    return (int)v;
}

// ── Mod loading ──
bool Mod::loadFromFolder(const std::string& path) {
    folder = path;
    std::string cfgPath = path + "/mod.cfg";
    auto ini = parseINI(cfgPath);

    if (ini.find("mod") == ini.end()) {
        printf("Mod: no [mod] section in %s\n", cfgPath.c_str());
        return false;
    }

    auto& modSec = ini["mod"];
    id          = modSec.count("id")          ? modSec["id"]          : "";
    name        = modSec.count("name")        ? modSec["name"]        : id;
    author      = modSec.count("author")      ? modSec["author"]      : "Unknown";
    version     = modSec.count("version")     ? modSec["version"]     : "1.0";
    description = modSec.count("description") ? modSec["description"] : "";
    gameVersion = modSec.count("game_version") ? std::atoi(modSec["game_version"].c_str()) : 1;

    if (id.empty()) {
        printf("Mod: missing id in %s\n", cfgPath.c_str());
        return false;
    }

    // Content flags
    if (ini.count("content")) {
        auto& c = ini["content"];
        content.characters = c.count("characters") ? toBool(c["characters"]) : false;
        content.maps       = c.count("maps")       ? toBool(c["maps"])       : false;
        content.packs      = c.count("packs")      ? toBool(c["packs"])      : false;
        content.sprites    = c.count("sprites")     ? toBool(c["sprites"])    : false;
        content.sounds     = c.count("sounds")      ? toBool(c["sounds"])     : false;
        content.gamemodes  = c.count("gamemodes")   ? toBool(c["gamemodes"]) : false;
        content.items      = c.count("items")       ? toBool(c["items"])      : false;
    }

    // Overrides
    if (ini.count("overrides")) {
        overrides.values = ini["overrides"];
    }

    // Dependencies
    if (ini.count("dependencies")) {
        for (auto& [k, v] : ini["dependencies"]) {
            dependencies.push_back(v);
        }
    }

    // Scan content subfolders
    if (content.characters && dirExists(path + "/characters")) {
        characterPaths = listFiles(path + "/characters", ".cschar");
        // Also check subdirectories
        DIR* d = opendir((path + "/characters").c_str());
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                std::string sub = path + "/characters/" + ent->d_name;
                if (ent->d_name[0] != '.' && dirExists(sub)) {
                    auto subFiles = listFiles(sub, ".cschar");
                    characterPaths.insert(characterPaths.end(), subFiles.begin(), subFiles.end());
                }
            }
            closedir(d);
        }
    }

    if (content.maps && dirExists(path + "/maps")) {
        mapPaths = listFiles(path + "/maps", ".csm");
    }

    if (content.packs && dirExists(path + "/packs")) {
        packPaths = listFiles(path + "/packs", ".cspack");
    }

    // Sprite overrides
    if (content.sprites && dirExists(path + "/sprites")) {
        // Walk sprites/ and for each file, map original name → mod path
        std::function<void(const std::string&, const std::string&)> walkSprites;
        walkSprites = [&](const std::string& dir, const std::string& relBase) {
            DIR* d = opendir(dir.c_str());
            if (!d) return;
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                std::string full = dir + "/" + ent->d_name;
                std::string rel = relBase.empty() ? ent->d_name : relBase + "/" + ent->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    walkSprites(full, rel);
                } else {
                    spriteOverrides["sprites/" + rel] = full;
                }
            }
            closedir(d);
        };
        walkSprites(path + "/sprites", "");
    }

    // Sound overrides
    if (content.sounds && dirExists(path + "/sounds")) {
        DIR* d = opendir((path + "/sounds").c_str());
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                soundOverrides[std::string("sounds/") + ent->d_name] = path + "/sounds/" + ent->d_name;
            }
            closedir(d);
        }
    }

    // Custom items
    if (content.items && dirExists(path + "/items")) {
        auto itemFiles = listFiles(path + "/items", ".cfg");
        for (auto& itemFile : itemFiles) {
            auto itemIni = parseINI(itemFile);
            if (itemIni.count("item")) {
                ModItemDef item;
                auto& s = itemIni["item"];
                item.id          = s.count("id")   ? s["id"]   : "";
                item.name        = s.count("name")  ? s["name"]  : item.id;
                item.description = s.count("description") ? s["description"] : "";
                item.spritePath  = s.count("sprite") ? (path + "/items/" + s["sprite"]) : "";
                item.modSource   = id;
                if (itemIni.count("params")) item.params = itemIni["params"];
                if (!item.id.empty()) items.push_back(item);
            }
        }
    }

    // Custom gamemodes
    if (content.gamemodes && dirExists(path + "/gamemodes")) {
        auto gmFiles = listFiles(path + "/gamemodes", ".cfg");
        for (auto& gmFile : gmFiles) {
            auto gmIni = parseINI(gmFile);
            if (gmIni.count("gamemode")) {
                auto& s = gmIni["gamemode"];
                GameModeEntry gm;
                gm.id = s.count("id") ? s["id"] : "";
                gm.displayName = s.count("name") ? s["name"] : gm.id;
                gm.description = s.count("description") ? s["description"] : "";
                gm.modSource = id;

                // Parse rules
                auto& r = gm.defaultRules;
                r.name = gm.displayName;
                r.description = gm.description;
                r.type = GameModeType::CustomGamemode;
                if (s.count("max_players"))     r.maxPlayers = std::atoi(s["max_players"].c_str());
                if (s.count("friendly_fire"))   r.friendlyFire = toBool(s["friendly_fire"]);
                if (s.count("lives"))           r.lives = std::atoi(s["lives"].c_str());
                if (s.count("pvp"))             r.pvpEnabled = toBool(s["pvp"]);
                if (s.count("spawn_enemies"))   r.spawnEnemies = toBool(s["spawn_enemies"]);
                if (s.count("spawn_crates"))    r.spawnCrates = toBool(s["spawn_crates"]);
                if (s.count("time_limit"))      { r.hasTimer = true; r.timeLimit = std::stof(s["time_limit"]); }
                if (s.count("score_limit"))     r.deathmatchScoreLimit = std::atoi(s["score_limit"].c_str());
                if (s.count("respawn_time"))    r.respawnTime = std::stof(s["respawn_time"]);

                if (!gm.id.empty()) gamemodes.push_back(gm);
            }
        }
    }

    printf("Mod loaded: %s (%s) by %s — chars:%zu maps:%zu packs:%zu items:%zu gamemodes:%zu\n",
        name.c_str(), id.c_str(), author.c_str(),
        characterPaths.size(), mapPaths.size(), packPaths.size(),
        items.size(), gamemodes.size());

    return true;
}

// ── Mod Manager ──
ModManager& ModManager::instance() {
    static ModManager mgr;
    return mgr;
}

void ModManager::scanMods() {
    mods_.clear();

    // Scan directories
    std::vector<std::string> dirs = {
        "mods",
        "romfs/mods",
#ifdef __SWITCH__
        "romfs:/mods",
#endif
    };

    for (auto& dir : dirs) {
        scanDirectory(dir);
    }

    // Sort by load order
    std::sort(mods_.begin(), mods_.end(), [](const Mod& a, const Mod& b) {
        return a.loadOrder < b.loadOrder;
    });

    printf("ModManager: %zu mods found\n", mods_.size());
}

void ModManager::scanDirectory(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string modDir = dir + "/" + ent->d_name;
        if (!dirExists(modDir)) continue;

        // Check for mod.cfg
        std::string cfgPath = modDir + "/mod.cfg";
        struct stat st;
        if (stat(cfgPath.c_str(), &st) != 0) continue;

        Mod mod;
        mod.loadOrder = (int)mods_.size();
        if (mod.loadFromFolder(modDir)) {
            // Check not duplicate
            bool dup = false;
            for (auto& existing : mods_) {
                if (existing.id == mod.id) { dup = true; break; }
            }
            if (!dup) mods_.push_back(mod);
        }
    }
    closedir(d);
}

void ModManager::loadAllEnabled() {
    // Register gamemodes from enabled mods
    auto& gmReg = GameModeRegistry::instance();
    for (auto& m : mods_) {
        if (!m.enabled) continue;
        for (auto& gm : m.gamemodes) {
            gmReg.registerMode(gm);
        }
    }
}

void ModManager::unloadAll() {
    mods_.clear();
}

Mod* ModManager::findMod(const std::string& id) {
    for (auto& m : mods_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

void ModManager::setEnabled(const std::string& id, bool enabled) {
    if (auto* m = findMod(id)) m->enabled = enabled;
}

std::vector<std::string> ModManager::allCharacterPaths() const {
    std::vector<std::string> result;
    for (auto& m : mods_) {
        if (!m.enabled) continue;
        result.insert(result.end(), m.characterPaths.begin(), m.characterPaths.end());
    }
    return result;
}

std::vector<std::string> ModManager::allMapPaths() const {
    std::vector<std::string> result;
    for (auto& m : mods_) {
        if (!m.enabled) continue;
        result.insert(result.end(), m.mapPaths.begin(), m.mapPaths.end());
    }
    return result;
}

std::vector<std::string> ModManager::allPackPaths() const {
    std::vector<std::string> result;
    for (auto& m : mods_) {
        if (!m.enabled) continue;
        result.insert(result.end(), m.packPaths.begin(), m.packPaths.end());
    }
    return result;
}

std::vector<ModItemDef> ModManager::allItems() const {
    std::vector<ModItemDef> result;
    for (auto& m : mods_) {
        if (!m.enabled) continue;
        result.insert(result.end(), m.items.begin(), m.items.end());
    }
    return result;
}

std::string ModManager::resolveSpriteAsset(const std::string& original) const {
    // Last enabled mod with an override wins
    for (int i = (int)mods_.size() - 1; i >= 0; i--) {
        if (!mods_[i].enabled) continue;
        auto it = mods_[i].spriteOverrides.find(original);
        if (it != mods_[i].spriteOverrides.end()) return it->second;
    }
    return original;
}

std::string ModManager::resolveSoundAsset(const std::string& original) const {
    for (int i = (int)mods_.size() - 1; i >= 0; i--) {
        if (!mods_[i].enabled) continue;
        auto it = mods_[i].soundOverrides.find(original);
        if (it != mods_[i].soundOverrides.end()) return it->second;
    }
    return original;
}

ModOverrides ModManager::mergedOverrides() const {
    ModOverrides merged;
    for (auto& m : mods_) {
        if (!m.enabled) continue;
        for (auto& [k, v] : m.overrides.values) {
            merged.values[k] = v;
        }
    }
    return merged;
}

void ModManager::saveModConfig() {
    FILE* f = fopen("modconfig.cfg", "w");
    if (!f) return;
    fprintf(f, "[mods]\n");
    for (auto& m : mods_) {
        fprintf(f, "%s=%s\n", m.id.c_str(), m.enabled ? "true" : "false");
    }
    fclose(f);
}

void ModManager::loadModConfig() {
    FILE* f = fopen("modconfig.cfg", "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string key = line;
        std::string val = eq + 1;
        // Trim
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r')) val.pop_back();
        while (!key.empty() && (key[0] == ' ' || key[0] == '\t')) key.erase(key.begin());
        if (key == "[mods]") continue;

        if (auto* m = findMod(key)) {
            m->enabled = (val == "true" || val == "1");
        }
    }
    fclose(f);
}

ModManager::SyncManifest ModManager::buildSyncManifest() const {
    SyncManifest manifest;
    manifest.maps = allMapPaths();
    manifest.characters = allCharacterPaths();
    manifest.packs = allPackPaths();
    return manifest;
}

// ── Mod network sync: serialize all enabled mods into a transferable blob ──
// Format:
//   uint16_t numMods
//   for each mod:
//     uint16_t idLen, char[idLen] modId
//     uint16_t numFiles
//     for each file:
//       uint16_t pathLen, char[pathLen] relativePath
//       uint32_t dataLen, uint8_t[dataLen] fileData
//
// Skips individual files > 256KB (large sprites/sounds).
// Skips .png/.wav/.ogg/.mp3/.bmp files entirely (cosmetic).

static bool isMediaExtension(const std::string& name) {
    auto ext = [&]() -> std::string {
        size_t dot = name.rfind('.');
        if (dot == std::string::npos) return "";
        std::string e = name.substr(dot);
        for (auto& c : e) c = tolower(c);
        return e;
    }();
    return ext == ".png" || ext == ".wav" || ext == ".ogg" ||
           ext == ".mp3" || ext == ".bmp" || ext == ".jpg" ||
           ext == ".jpeg" || ext == ".ttf" || ext == ".otf";
}

std::vector<uint8_t> ModManager::serializeEnabledMods() const {
    std::vector<uint8_t> result;

    // Count enabled mods
    uint16_t numMods = 0;
    for (auto& m : mods_) if (m.enabled) numMods++;
    if (numMods == 0) return result;

    // Write header
    result.resize(2);
    memcpy(result.data(), &numMods, 2);

    for (auto& m : mods_) {
        if (!m.enabled) continue;

        // Write mod ID
        uint16_t idLen = (uint16_t)m.id.size();
        size_t pos = result.size();
        result.resize(pos + 2 + idLen);
        memcpy(result.data() + pos, &idLen, 2);
        memcpy(result.data() + pos + 2, m.id.c_str(), idLen);

        // Collect all files from the mod folder (skip media & large files)
        struct ModFile { std::string relPath; std::vector<uint8_t> data; };
        std::vector<ModFile> files;

        std::function<void(const std::string&, const std::string&)> walk;
        walk = [&](const std::string& dir, const std::string& rel) {
            DIR* d = opendir(dir.c_str());
            if (!d) return;
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                std::string full = dir + "/" + ent->d_name;
                std::string relFull = rel.empty() ? std::string(ent->d_name) : rel + "/" + ent->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        walk(full, relFull);
                    } else if (st.st_size <= 256 * 1024 && !isMediaExtension(full)) {
                        ModFile mf;
                        mf.relPath = relFull;
                        FILE* f = fopen(full.c_str(), "rb");
                        if (f) {
                            mf.data.resize(st.st_size);
                            size_t nread = fread(mf.data.data(), 1, st.st_size, f);
                            fclose(f);
                            if (nread == (size_t)st.st_size) {
                                files.push_back(std::move(mf));
                            } else {
                                printf("Mod sync: short read for %s (%zu/%ld)\n",
                                       full.c_str(), nread, (long)st.st_size);
                            }
                        }
                    }
                }
            }
            closedir(d);
        };
        walk(m.folder, "");

        // Write file count
        uint16_t numFiles = (uint16_t)files.size();
        pos = result.size();
        result.resize(pos + 2);
        memcpy(result.data() + pos, &numFiles, 2);

        // Write each file
        for (auto& f : files) {
            uint16_t pathLen = (uint16_t)f.relPath.size();
            uint32_t dataLen = (uint32_t)f.data.size();
            pos = result.size();
            result.resize(pos + 2 + pathLen + 4 + dataLen);
            memcpy(result.data() + pos, &pathLen, 2);
            memcpy(result.data() + pos + 2, f.relPath.c_str(), pathLen);
            memcpy(result.data() + pos + 2 + pathLen, &dataLen, 4);
            if (dataLen > 0)
                memcpy(result.data() + pos + 2 + pathLen + 4, f.data.data(), dataLen);
        }

        printf("ModSync: serialized mod '%s' — %u files\n", m.id.c_str(), numFiles);
    }

    printf("ModSync: total blob size = %zu bytes (%u mods)\n", result.size(), numMods);
    return result;
}

void ModManager::deserializeAndInstallMods(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;

    size_t offset = 0;
    uint16_t numMods;
    memcpy(&numMods, data.data(), 2);
    offset = 2;

    printf("ModSync: deserializing %u mods (%zu bytes)\n", numMods, data.size());

    // Create sync directory
    mkdir("mods", 0755);
    std::string syncBase = "mods/_mp_sync";
    mkdir(syncBase.c_str(), 0755);

    // Clear previous sync mods
    // (Simple approach: just overwrite; leftover files from previous sessions are fine)

    for (uint16_t mi = 0; mi < numMods && offset < data.size(); mi++) {
        // Read mod ID
        if (offset + 2 > data.size()) break;
        uint16_t idLen;
        memcpy(&idLen, data.data() + offset, 2);
        offset += 2;
        if (offset + idLen > data.size()) break;
        std::string modId((char*)data.data() + offset, idLen);
        offset += idLen;

        // Create mod directory
        std::string modDir = syncBase + "/" + modId;
        mkdir(modDir.c_str(), 0755);

        // Read file count
        if (offset + 2 > data.size()) break;
        uint16_t numFiles;
        memcpy(&numFiles, data.data() + offset, 2);
        offset += 2;

        printf("ModSync: installing mod '%s' (%u files)\n", modId.c_str(), numFiles);

        for (uint16_t fi = 0; fi < numFiles && offset < data.size(); fi++) {
            // Read relative path
            if (offset + 2 > data.size()) break;
            uint16_t pathLen;
            memcpy(&pathLen, data.data() + offset, 2);
            offset += 2;
            if (offset + pathLen > data.size()) break;
            std::string relPath((char*)data.data() + offset, pathLen);
            offset += pathLen;

            // Read data length
            if (offset + 4 > data.size()) break;
            uint32_t dataLen;
            memcpy(&dataLen, data.data() + offset, 4);
            offset += 4;
            if (offset + dataLen > data.size()) break;

            // Create subdirectories as needed
            std::string fullPath = modDir + "/" + relPath;
            // Find last slash and create directories
            for (size_t i = 0; i < fullPath.size(); i++) {
                if (fullPath[i] == '/') {
                    std::string sub = fullPath.substr(0, i);
                    mkdir(sub.c_str(), 0755);
                }
            }

            // Write file
            FILE* f = fopen(fullPath.c_str(), "wb");
            if (f) {
                if (dataLen > 0)
                    fwrite(data.data() + offset, 1, dataLen, f);
                fclose(f);
            }
            offset += dataLen;
        }
    }

    // Re-scan mods to pick up the newly installed sync mods
    // First remember current non-sync mods' enabled state
    std::unordered_map<std::string, bool> prevEnabled;
    for (auto& m : mods_) prevEnabled[m.id] = m.enabled;

    scanMods();
    loadModConfig();

    // Enable synced mods (they came from the host, so they should be active)
    for (auto& m : mods_) {
        if (m.folder.find("_mp_sync") != std::string::npos) {
            m.enabled = true;
        } else if (prevEnabled.count(m.id)) {
            m.enabled = prevEnabled[m.id];
        }
    }

    loadAllEnabled();
    printf("ModSync: mods installed and loaded\n");
}
