// ─── assets.cpp ─── Asset management implementation ─────────────────────────
#include "assets.h"
#include <cstdio>
#include <sys/stat.h>

// Platform-specific asset root prefix
static std::string assetPrefix() {
#ifdef __SWITCH__
    return "romfs:/";
#elif defined(PLATFORM_ANDROID)
    return "";  // SDL_RWops handles Android assets
#else
    return "romfs/";  // PC: relative to binary
#endif
}

bool Assets::init(SDL_Renderer* renderer) {
    renderer_ = renderer;
    return true;
}

void Assets::shutdown() {
    for (auto& [k,v] : textures_) SDL_DestroyTexture(v);
    for (auto& [k,v] : chunks_)   Mix_FreeChunk(v);
    for (auto& [k,v] : musics_)   Mix_FreeMusic(v);
    for (auto& [k,v] : fonts_)    TTF_CloseFont(v);
    textures_.clear(); chunks_.clear(); musics_.clear(); fonts_.clear();
}

SDL_Texture* Assets::loadTex(const std::string& path) {
    SDL_Surface* s = IMG_Load(path.c_str());
    if (!s) { printf("IMG_Load(%s): %s\n", path.c_str(), IMG_GetError()); return nullptr; }
    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
    SDL_FreeSurface(s);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        // Force nearest-neighbor per-texture; SDL_HINT_RENDER_SCALE_QUALITY is
        // advisory only and ignored by OpenGL/Vulkan backends.
        SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    }
    return t;
}

SDL_Texture* Assets::tex(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) return it->second;

    // Try platform asset path
    std::string path = assetPrefix() + name;
    SDL_Texture* t = loadTex(path);
    if (t) { textures_[name] = t; return t; }

    // Fallback: try bare name (for custom/user paths)
    t = loadTex(name);
    if (t) { textures_[name] = t; return t; }

    printf("Failed to load texture: %s\n", name.c_str());
    return nullptr;
}

Mix_Chunk* Assets::sfx(const std::string& name) {
    auto it = chunks_.find(name);
    if (it != chunks_.end()) return it->second;

    std::string path = assetPrefix() + "sounds/" + name;
    Mix_Chunk* c = Mix_LoadWAV(path.c_str());
    if (!c) {
        path = "sounds/" + name;
        c = Mix_LoadWAV(path.c_str());
    }
    if (c) chunks_[name] = c;
    else printf("Failed to load sfx: %s (%s)\n", name.c_str(), Mix_GetError());
    return c;
}

Mix_Music* Assets::music(const std::string& name) {
    auto it = musics_.find(name);
    if (it != musics_.end()) return it->second;

    std::string path = assetPrefix() + "sounds/" + name;
    Mix_Music* m = Mix_LoadMUS(path.c_str());
    if (!m) {
        path = "sounds/" + name;
        m = Mix_LoadMUS(path.c_str());
    }
    if (m) musics_[name] = m;
    else printf("Failed to load music: %s (%s)\n", name.c_str(), Mix_GetError());
    return m;
}

TTF_Font* Assets::font(int size) {
    auto it = fonts_.find(size);
    if (it != fonts_.end()) return it->second;

    std::string pfx = assetPrefix();
    TTF_Font* f = TTF_OpenFont((pfx + "fonts/pixelmix.ttf").c_str(), size);
    if (!f) f = TTF_OpenFont((pfx + "fonts/Pixeled.ttf").c_str(), size);
    if (!f) f = TTF_OpenFont("fonts/pixelmix.ttf", size);
    if (f) fonts_[size] = f;
    else printf("Failed to load font size %d: %s\n", size, TTF_GetError());
    return f;
}

std::vector<SDL_Texture*> Assets::loadAnim(const std::string& basePath, int count, int startIdx) {
    std::vector<SDL_Texture*> frames;
    char buf[256];
    for (int i = startIdx; i < startIdx + count; i++) {
        snprintf(buf, sizeof(buf), "%s%04d.png", basePath.c_str(), i);
        SDL_Texture* t = tex(std::string(buf));
        if (t) frames.push_back(t);
    }
    return frames;
}
