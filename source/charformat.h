#pragma once
// ─── charformat.h ─── .cschar (Cold Start Character) file format ────────────
// Text-based format (INI-like) for easy editing:
//
//   [character]
//   name=My Guy
//   speed=520
//   hp=10
//   ammo=10
//   fire_rate=10
//   reload_time=1.0
//
//   [sprites]
//   body_frames=10          # body-0001.png .. body-0010.png in char folder
//   leg_frames=8            # legs-0001.png .. legs-0008.png in char folder
//   death_frames=12         # death-1.png .. death-12.png in char folder
//   detail=detail.png       # optional large sprite for selection screen
//
// Folder layout for a character "mychar":
//   characters/mychar/
//     mychar.cschar          # this descriptor
//     body-0001.png .. body-NNNN.png
//     legs-0001.png .. legs-NNNN.png
//     death-1.png .. death-NN.png
//     detail.png              # optional
// ─────────────────────────────────────────────────────────────────────────────
#include <SDL2/SDL.h>
#include <string>
#include <vector>

struct CharacterDef {
    std::string name        = "Default";
    std::string folder;     // path to character folder

    // Stats
    float speed             = 520.0f;
    int   hp                = 10;
    int   ammo              = 10;
    float fireRate          = 10.0f;
    float reloadTime        = 1.0f;

    // Sprite counts
    int bodyFrames          = 10;
    int legFrames           = 8;
    int deathFrames         = 12;
    bool hasDetail          = false; // has detail.png for selection screen

    // Loaded textures (populated at runtime)
    std::vector<SDL_Texture*> bodySprites;
    std::vector<SDL_Texture*> legSprites;
    std::vector<SDL_Texture*> deathSprites;
    SDL_Texture* detailSprite = nullptr;

    bool loadFromFile(const std::string& path, SDL_Renderer* renderer);
    void unload();
};

// Scan a directory for .cschar files and return all found CharacterDefs
std::vector<CharacterDef> scanCharacters(const std::string& baseDir, SDL_Renderer* renderer);
