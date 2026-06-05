#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <vector>

class Assets {
public:
    static Assets& instance() { static Assets a; return a; }

    bool init(SDL_Renderer* renderer);
    void shutdown();

    SDL_Texture* tex(const std::string& name);
    Mix_Chunk*   sfx(const std::string& name);
    Mix_Music*   music(const std::string& name);
    TTF_Font*    font(int size = 16);

    // Load a series of numbered PNGs as animation frames
    std::vector<SDL_Texture*> loadAnim(const std::string& basePath, int count, int startIdx = 1);

    // Returns the romfs base path (e.g. "/path/to/exe/romfs/" or "romfs:/")
    static std::string prefix();

    // Android only: prompt user for storage location, extract assets from APK.
    // Must be called after SDL_Init, before any assets are loaded.
    static void androidInitRomfs();

    // Load a texture from a path relative to the romfs root (caches by relPath)
    SDL_Texture* loadRelTex(const std::string& relPath);

private:
    Assets() = default;
    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<std::string, SDL_Texture*> textures_;
    std::unordered_map<std::string, Mix_Chunk*>   chunks_;
    std::unordered_map<std::string, Mix_Music*>   musics_;
    std::unordered_map<int, TTF_Font*>            fonts_;

    SDL_Texture* loadTex(const std::string& path);
};
