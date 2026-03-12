#pragma once
// ─── charformat.h ─── Character system (easy folder-based characters) ───────
//
// HOW TO MAKE A CHARACTER:
//   1. Create a folder: characters/MyChar/
//   2. Drop in your PNG sprites:
//        body-0001.png, body-0002.png, ...   (body/aim animation)
//        legs-0001.png, legs-0002.png, ...   (leg walk animation)
//        death-1.png, death-2.png, ...       (death animation)
//        detail.png                          (optional, for select screen)
//   3. That's it! Frame counts are auto-detected from files present.
//
//   OPTIONAL: Add a character.cfg for stat overrides:
//      name=My Guy
//      speed=520
//      hp=10
//      ammo=10
//      fire_rate=10
//      reload_time=1.0
//
//   The .cschar format is still supported for backwards compatibility.
// ─────────────────────────────────────────────────────────────────────────────
#include <SDL2/SDL.h>
#include <string>
#include <vector>

struct CharacterDef {
    std::string name        = "Default";
    std::string folder;     // path to character folder (with trailing /)

    // Stats (overridable via character.cfg or .cschar)
    float speed             = 520.0f;
    int   hp                = 10;
    int   ammo              = 10;
    float fireRate          = 10.0f;
    float reloadTime        = 1.0f;

    // Sprite counts (auto-detected from files, or overridden in config)
    int bodyFrames          = 0;
    int legFrames           = 0;
    int deathFrames         = 0;
    bool hasDetail          = false;

    // Loaded textures (populated at runtime)
    std::vector<SDL_Texture*> bodySprites;
    std::vector<SDL_Texture*> legSprites;
    std::vector<SDL_Texture*> deathSprites;
    SDL_Texture* detailSprite = nullptr;

    // Load from a folder (auto-detects sprites, reads .cschar or character.cfg)
    bool loadFromFolder(const std::string& folderPath, SDL_Renderer* renderer);

    // Legacy: load from a .cschar file (still works)
    bool loadFromFile(const std::string& path, SDL_Renderer* renderer);

    // Reload sprites without re-reading config (for hot-reload)
    bool reloadSprites(SDL_Renderer* renderer);

    void unload();

    // Validation: returns list of warnings/issues
    std::vector<std::string> validate() const;

    // Auto-detect sprite counts from files in folder
    static int countFiles(const std::string& folder, const char* pattern);

private:
    bool loadConfig(const std::string& path);   // parse .cschar or character.cfg
    bool loadSprites(SDL_Renderer* renderer);    // load PNGs from folder
};

// Scan a directory for character folders (with sprites or .cschar) and return all found
std::vector<CharacterDef> scanCharacters(const std::string& baseDir, SDL_Renderer* renderer);

// Quick-create a character template folder with default sprites copied
bool createCharacterTemplate(const std::string& baseDir, const std::string& charName);
