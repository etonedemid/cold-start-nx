// ─── charformat.cpp ─── .cschar loader ──────────────────────────────────────
#include "charformat.h"
#include "assets.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

// Simple INI parser — reads key=value pairs, respects [sections]
static bool parseLine(const char* line, std::string& key, std::string& value) {
    // Skip whitespace
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == ';' || *line == '[' || *line == '\0' || *line == '\n')
        return false;
    const char* eq = strchr(line, '=');
    if (!eq) return false;
    key = std::string(line, eq - line);
    value = std::string(eq + 1);
    // Trim trailing whitespace/newline
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
        value.pop_back();
    while (!key.empty() && (key.back() == ' '))
        key.pop_back();
    return true;
}

bool CharacterDef::loadFromFile(const std::string& path, SDL_Renderer* renderer) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { printf("Cannot open character file: %s\n", path.c_str()); return false; }

    // Derive folder from file path
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos)
        folder = path.substr(0, lastSlash + 1);
    else
        folder = "./";

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        std::string key, value;
        if (!parseLine(line, key, value)) continue;

        if (key == "name")        name = value;
        else if (key == "speed")  speed = (float)atof(value.c_str());
        else if (key == "hp")     hp = atoi(value.c_str());
        else if (key == "ammo")   ammo = atoi(value.c_str());
        else if (key == "fire_rate") fireRate = (float)atof(value.c_str());
        else if (key == "reload_time") reloadTime = (float)atof(value.c_str());
        else if (key == "body_frames") bodyFrames = atoi(value.c_str());
        else if (key == "leg_frames")  legFrames = atoi(value.c_str());
        else if (key == "death_frames") deathFrames = atoi(value.c_str());
        else if (key == "detail") hasDetail = (value != "none" && !value.empty());
    }
    fclose(f);

    // Load body sprites
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

    // Load leg sprites
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

    // Load death sprites
    deathSprites.clear();
    for (int i = 1; i <= deathFrames; i++) {
        snprintf(buf, sizeof(buf), "%sdeath-%d.png", folder.c_str(), i);
        SDL_Surface* s = IMG_Load(buf);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
            if (t) deathSprites.push_back(t);
        }
    }

    // Load detail sprite (optional)
    if (hasDetail) {
        snprintf(buf, sizeof(buf), "%sdetail.png", folder.c_str());
        SDL_Surface* s = IMG_Load(buf);
        if (s) {
            detailSprite = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
        }
    }

    printf("Loaded character: %s (body:%d legs:%d death:%d)\n",
        name.c_str(), (int)bodySprites.size(), (int)legSprites.size(), (int)deathSprites.size());
    return !bodySprites.empty();
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

std::vector<CharacterDef> scanCharacters(const std::string& baseDir, SDL_Renderer* renderer) {
    std::vector<CharacterDef> chars;

    DIR* dir = opendir(baseDir.c_str());
    if (!dir) {
        printf("Cannot scan character directory: %s\n", baseDir.c_str());
        return chars;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string subdir = baseDir + "/" + entry->d_name;
        struct stat st;
        if (stat(subdir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        // Look for .cschar file inside subdir
        DIR* sub = opendir(subdir.c_str());
        if (!sub) continue;
        struct dirent* se;
        while ((se = readdir(sub)) != nullptr) {
            std::string fname(se->d_name);
            if (fname.size() > 7 && fname.substr(fname.size() - 7) == ".cschar") {
                CharacterDef cd;
                if (cd.loadFromFile(subdir + "/" + fname, renderer)) {
                    chars.push_back(std::move(cd));
                }
                break; // one character per folder
            }
        }
        closedir(sub);
    }
    closedir(dir);

    return chars;
}
