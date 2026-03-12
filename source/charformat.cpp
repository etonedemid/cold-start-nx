// ─── charformat.cpp ─── Easy folder-based character system ──────────────────
#include "charformat.h"
#include "assets.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>

// ── INI parser (shared by .cschar and character.cfg) ─────────────────────────

static bool parseLine(const char* line, std::string& key, std::string& value) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == ';' || *line == '[' || *line == '\0' || *line == '\n')
        return false;
    const char* eq = strchr(line, '=');
    if (!eq) return false;
    key = std::string(line, eq - line);
    value = std::string(eq + 1);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
        value.pop_back();
    while (!key.empty() && key.back() == ' ')
        key.pop_back();
    return true;
}

// ── Auto-detect sprite count from files in a folder ─────────────────────────

static bool fileExists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

int CharacterDef::countFiles(const std::string& folder, const char* pattern) {
    // pattern is printf-style with %d or %04d
    int count = 0;
    char buf[512];
    for (int i = 1; i <= 100; i++) {
        snprintf(buf, sizeof(buf), pattern, i);
        std::string path = folder + buf;
        if (!fileExists(path))
            break;
        count++;
    }
    return count;
}

static int autoDetectBodyFrames(const std::string& folder) {
    return CharacterDef::countFiles(folder, "body-%04d.png");
}

static int autoDetectLegFrames(const std::string& folder) {
    return CharacterDef::countFiles(folder, "legs-%04d.png");
}

static int autoDetectDeathFrames(const std::string& folder) {
    // Death frames can be death-1.png or death-0001.png — try both
    int count = CharacterDef::countFiles(folder, "death-%d.png");
    if (count == 0)
        count = CharacterDef::countFiles(folder, "death-%04d.png");
    return count;
}

// ── Config file parser ──────────────────────────────────────────────────────

bool CharacterDef::loadConfig(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        std::string key, value;
        if (!parseLine(line, key, value)) continue;

        if (key == "name")              name = value;
        else if (key == "speed")        speed = (float)atof(value.c_str());
        else if (key == "hp")           hp = atoi(value.c_str());
        else if (key == "ammo")         ammo = atoi(value.c_str());
        else if (key == "fire_rate")    fireRate = (float)atof(value.c_str());
        else if (key == "reload_time")  reloadTime = (float)atof(value.c_str());
        else if (key == "body_frames")  bodyFrames = atoi(value.c_str());
        else if (key == "leg_frames")   legFrames = atoi(value.c_str());
        else if (key == "death_frames") deathFrames = atoi(value.c_str());
        else if (key == "detail")       hasDetail = (value != "none" && !value.empty());
    }
    fclose(f);
    return true;
}

// ── Sprite loader ───────────────────────────────────────────────────────────

bool CharacterDef::loadSprites(SDL_Renderer* renderer) {
    // Load body sprites (body-0001.png, body-0002.png, ...)
    bodySprites.clear();
    char buf[512];
    for (int i = 1; i <= bodyFrames; i++) {
        snprintf(buf, sizeof(buf), "%sbody-%04d.png", folder.c_str(), i);
        SDL_Surface* s = IMG_Load(buf);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
            if (t) bodySprites.push_back(t);
        }
    }

    // Load leg sprites (legs-0001.png, legs-0002.png, ...)
    legSprites.clear();
    for (int i = 1; i <= legFrames; i++) {
        snprintf(buf, sizeof(buf), "%slegs-%04d.png", folder.c_str(), i);
        SDL_Surface* s = IMG_Load(buf);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
            if (t) legSprites.push_back(t);
        }
    }

    // Load death sprites — try death-1.png first, fall back to death-0001.png
    deathSprites.clear();
    bool useZeroPadDeath = !fileExists(folder + "death-1.png") && fileExists(folder + "death-0001.png");
    for (int i = 1; i <= deathFrames; i++) {
        if (useZeroPadDeath)
            snprintf(buf, sizeof(buf), "%sdeath-%04d.png", folder.c_str(), i);
        else
            snprintf(buf, sizeof(buf), "%sdeath-%d.png", folder.c_str(), i);
        SDL_Surface* s = IMG_Load(buf);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
            if (t) deathSprites.push_back(t);
        }
    }

    // Load detail sprite (optional)
    if (hasDetail || fileExists(folder + "detail.png")) {
        snprintf(buf, sizeof(buf), "%sdetail.png", folder.c_str());
        SDL_Surface* s = IMG_Load(buf);
        if (s) {
            detailSprite = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
            hasDetail = (detailSprite != nullptr);
        }
    }

    return !bodySprites.empty();
}

// ── Main entry points ───────────────────────────────────────────────────────

bool CharacterDef::loadFromFolder(const std::string& folderPath, SDL_Renderer* renderer) {
    // Normalize folder path to end with /
    folder = folderPath;
    if (!folder.empty() && folder.back() != '/')
        folder += '/';

    // Derive name from folder name
    std::string tmp = folderPath;
    if (!tmp.empty() && tmp.back() == '/') tmp.pop_back();
    size_t lastSlash = tmp.find_last_of('/');
    if (lastSlash != std::string::npos)
        name = tmp.substr(lastSlash + 1);
    else
        name = tmp;

    // Auto-detect sprite counts from files present
    bodyFrames  = autoDetectBodyFrames(folder);
    legFrames   = autoDetectLegFrames(folder);
    deathFrames = autoDetectDeathFrames(folder);

    // Try to load config file for stat/name overrides
    // Priority: character.cfg > *.cschar (first found)
    if (fileExists(folder + "character.cfg")) {
        loadConfig(folder + "character.cfg");
    } else {
        // Look for any .cschar file
        DIR* dir = opendir(folderPath.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string fname(entry->d_name);
                if (fname.size() > 7 && fname.substr(fname.size() - 7) == ".cschar") {
                    loadConfig(folder + fname);
                    break;
                }
            }
            closedir(dir);
        }
    }

    // If config specified frame counts, those override auto-detection
    // (auto-detection already ran, config values overwrite if present)
    // Re-detect if config didn't set them (they'd be 0 from auto-detect if no files)
    if (bodyFrames == 0)  bodyFrames = autoDetectBodyFrames(folder);
    if (legFrames == 0)   legFrames = autoDetectLegFrames(folder);
    if (deathFrames == 0) deathFrames = autoDetectDeathFrames(folder);

    // Must have at least body sprites to be valid
    if (bodyFrames == 0) {
        printf("Character '%s': no body sprites found in %s\n", name.c_str(), folder.c_str());
        return false;
    }

    // Load sprites
    if (!loadSprites(renderer)) {
        printf("Character '%s': failed to load sprites from %s\n", name.c_str(), folder.c_str());
        return false;
    }

    printf("Loaded character: %s (body:%d legs:%d death:%d) from %s\n",
        name.c_str(), (int)bodySprites.size(), (int)legSprites.size(),
        (int)deathSprites.size(), folder.c_str());
    return true;
}

bool CharacterDef::loadFromFile(const std::string& path, SDL_Renderer* renderer) {
    // Legacy compatibility: derive folder from .cschar path and load as folder
    size_t lastSlash = path.find_last_of('/');
    std::string folderPath;
    if (lastSlash != std::string::npos)
        folderPath = path.substr(0, lastSlash);
    else
        folderPath = ".";
    return loadFromFolder(folderPath, renderer);
}

bool CharacterDef::reloadSprites(SDL_Renderer* renderer) {
    // Destroy existing textures
    for (auto* t : bodySprites) if (t) SDL_DestroyTexture(t);
    for (auto* t : legSprites) if (t) SDL_DestroyTexture(t);
    for (auto* t : deathSprites) if (t) SDL_DestroyTexture(t);
    if (detailSprite) SDL_DestroyTexture(detailSprite);
    bodySprites.clear();
    legSprites.clear();
    deathSprites.clear();
    detailSprite = nullptr;

    // Re-detect frame counts
    bodyFrames  = autoDetectBodyFrames(folder);
    legFrames   = autoDetectLegFrames(folder);
    deathFrames = autoDetectDeathFrames(folder);

    // Re-read config
    if (fileExists(folder + "character.cfg"))
        loadConfig(folder + "character.cfg");
    else {
        DIR* dir = opendir(folder.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string fname(entry->d_name);
                if (fname.size() > 7 && fname.substr(fname.size() - 7) == ".cschar") {
                    loadConfig(folder + fname);
                    break;
                }
            }
            closedir(dir);
        }
    }

    if (bodyFrames == 0) return false;

    printf("Hot-reloading character: %s\n", name.c_str());
    return loadSprites(renderer);
}

void CharacterDef::unload() {
    for (auto* t : bodySprites) if (t) SDL_DestroyTexture(t);
    for (auto* t : legSprites) if (t) SDL_DestroyTexture(t);
    for (auto* t : deathSprites) if (t) SDL_DestroyTexture(t);
    if (detailSprite) SDL_DestroyTexture(detailSprite);
    bodySprites.clear();
    legSprites.clear();
    deathSprites.clear();
    detailSprite = nullptr;
}

std::vector<std::string> CharacterDef::validate() const {
    std::vector<std::string> warnings;
    if (bodySprites.empty())
        warnings.push_back("No body sprites loaded");
    if (legSprites.empty())
        warnings.push_back("No leg sprites (player won't show legs when walking)");
    if (deathSprites.empty())
        warnings.push_back("No death sprites (player death won't animate)");
    if ((int)bodySprites.size() < bodyFrames)
        warnings.push_back("Body: loaded " + std::to_string(bodySprites.size()) + "/" + std::to_string(bodyFrames) + " frames");
    if ((int)legSprites.size() < legFrames)
        warnings.push_back("Legs: loaded " + std::to_string(legSprites.size()) + "/" + std::to_string(legFrames) + " frames");
    if ((int)deathSprites.size() < deathFrames)
        warnings.push_back("Death: loaded " + std::to_string(deathSprites.size()) + "/" + std::to_string(deathFrames) + " frames");
    if (speed < 100.0f || speed > 1200.0f)
        warnings.push_back("Speed out of typical range (100-1200)");
    if (hp < 1 || hp > 50)
        warnings.push_back("HP out of range (1-50)");
    return warnings;
}

// ── Scanner ─────────────────────────────────────────────────────────────────

std::vector<CharacterDef> scanCharacters(const std::string& baseDir, SDL_Renderer* renderer) {
    std::vector<CharacterDef> chars;

    DIR* dir = opendir(baseDir.c_str());
    if (!dir) return chars;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string subdir = baseDir + "/" + entry->d_name;
        struct stat st;
        if (stat(subdir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        // Try to load as a character folder (auto-detects sprites)
        CharacterDef cd;
        if (cd.loadFromFolder(subdir, renderer)) {
            chars.push_back(std::move(cd));
        }
    }
    closedir(dir);

    // Sort by name for consistent ordering
    std::sort(chars.begin(), chars.end(), [](const CharacterDef& a, const CharacterDef& b) {
        return a.name < b.name;
    });

    return chars;
}

// ── Template creator ────────────────────────────────────────────────────────

bool createCharacterTemplate(const std::string& baseDir, const std::string& charName) {
    std::string safeName = charName;
    for (char& c : safeName) {
        if (c == ' ') c = '_';
        if (c == '/' || c == '\\') c = '_';
    }
    if (safeName.empty()) safeName = "NewCharacter";

    std::string dir = baseDir + "/" + safeName;
    mkdir(baseDir.c_str(), 0755);
    mkdir(dir.c_str(), 0755);

    // Write a simple character.cfg
    std::string cfgPath = dir + "/character.cfg";
    if (!fileExists(cfgPath)) {
        FILE* f = fopen(cfgPath.c_str(), "w");
        if (!f) return false;
        fprintf(f, "# Character config for %s\n", charName.c_str());
        fprintf(f, "# Just edit the values below — sprites are auto-detected from PNGs!\n\n");
        fprintf(f, "name=%s\n\n", charName.c_str());
        fprintf(f, "# Stats (all optional — defaults shown)\n");
        fprintf(f, "speed=520\n");
        fprintf(f, "hp=10\n");
        fprintf(f, "ammo=10\n");
        fprintf(f, "fire_rate=10\n");
        fprintf(f, "reload_time=1.0\n");
        fclose(f);
    }

    // Copy default player sprites as templates
    auto copySpriteTemplate = [&](const char* srcPattern, const char* dstPattern, int count) {
        for (int i = 1; i <= count; i++) {
            char src[512], dst[512];
            snprintf(src, sizeof(src), srcPattern, i);
            snprintf(dst, sizeof(dst), dstPattern, i);
            std::string dstPath = dir + "/" + dst;
            if (fileExists(dstPath)) continue;

            std::string srcPath;
            // Try romfs first, then local
            const char* srcDirs[] = {"romfs/sprites/player/", "sprites/player/"};
            for (const char* sd : srcDirs) {
                srcPath = std::string(sd) + src;
                if (fileExists(srcPath)) break;
                srcPath.clear();
            }
            if (srcPath.empty()) continue;

            FILE* sf = fopen(srcPath.c_str(), "rb");
            if (!sf) continue;
            FILE* df = fopen(dstPath.c_str(), "wb");
            if (df) {
                char buffer[4096];
                size_t bytes;
                while ((bytes = fread(buffer, 1, sizeof(buffer), sf)) > 0)
                    fwrite(buffer, 1, bytes, df);
                fclose(df);
            }
            fclose(sf);
        }
    };

    copySpriteTemplate("body-%04d.png", "body-%04d.png", 11);
    copySpriteTemplate("legs-%04d.png", "legs-%04d.png", 8);
    copySpriteTemplate("death-%d.png",  "death-%d.png",  12);

    printf("Character template created: %s\n", dir.c_str());
    return true;
}
