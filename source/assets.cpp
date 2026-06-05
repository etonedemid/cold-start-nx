#include "assets.h"
#include "mod.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <sys/stat.h>
#ifndef __SWITCH__
#  ifndef PLATFORM_ANDROID
#    include <climits>
#    include <unistd.h>
#    ifdef _WIN32
#      include <windows.h>
#    endif
#  endif
#endif

#ifdef PLATFORM_ANDROID
#include <errno.h>
#include <unistd.h>
#include <jni.h>
static std::string g_androidRomfsPath; // set by androidInitRomfs()

// Call an instance method on ColdStartActivity that returns a String.
static std::string androidCallStringMethod(const char* name, const char* sig) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) return "";
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!act) return "";
    jclass cls = env->GetObjectClass(act);
    if (!cls) { env->DeleteLocalRef(act); return ""; }
    jmethodID mid = env->GetMethodID(cls, name, sig);
    if (!mid) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls); env->DeleteLocalRef(act);
        return "";
    }
    jstring jres = (jstring)env->CallObjectMethod(act, mid);
    env->DeleteLocalRef(cls); env->DeleteLocalRef(act);
    if (!jres || env->ExceptionCheck()) { env->ExceptionClear(); if (jres) env->DeleteLocalRef(jres); return ""; }
    const char* raw = env->GetStringUTFChars(jres, nullptr);
    std::string out = raw ? raw : "";
    env->ReleaseStringUTFChars(jres, raw);
    env->DeleteLocalRef(jres);
    return out;
}

// Request storage permission then open the SAF folder picker.
static std::string androidBrowseFolder() {
    return androidCallStringMethod("requestStorageAndBrowse", "()Ljava/lang/String;");
}

static void mkdirs(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string sub = path.substr(0, i);
            mkdir(sub.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
}

static void extractFromApk(const std::string& destRoot) {
    // Read the filelist bundled alongside romfs assets in the APK
    SDL_RWops* fl = SDL_RWFromFile("romfs_filelist.txt", "r");
    if (!fl) { printf("androidInitRomfs: romfs_filelist.txt missing from APK\n"); return; }

    Sint64 sz = SDL_RWsize(fl);
    std::string manifest(sz, '\0');
    SDL_RWread(fl, &manifest[0], 1, sz);
    SDL_RWclose(fl);

    mkdirs(destRoot);

    std::istringstream ss(manifest);
    std::string rel;
    int copied = 0, skipped = 0;
    while (std::getline(ss, rel)) {
        while (!rel.empty() && (rel.back() == '\r' || rel.back() == '\n')) rel.pop_back();
        if (rel.empty()) continue;

        std::string destPath = destRoot + "/" + rel;
        // Skip if already exists
        struct stat st;
        if (stat(destPath.c_str(), &st) == 0) { skipped++; continue; }

        // Ensure parent dir
        std::string parent = destPath.substr(0, destPath.rfind('/'));
        mkdirs(parent);

        // Read from APK (SDL treats relative paths as APK assets on Android)
        SDL_RWops* src = SDL_RWFromFile(rel.c_str(), "rb");
        if (!src) { printf("androidInitRomfs: missing %s\n", rel.c_str()); continue; }
        Sint64 fsz = SDL_RWsize(src);
        std::vector<uint8_t> buf(fsz);
        SDL_RWread(src, buf.data(), 1, fsz);
        SDL_RWclose(src);

        FILE* dst = fopen(destPath.c_str(), "wb");
        if (dst) { fwrite(buf.data(), 1, fsz, dst); fclose(dst); copied++; }
    }
    printf("androidInitRomfs: extracted %d files, skipped %d existing\n", copied, skipped);
}

// Set g_androidRomfsPath and chdir to the base so all relative paths work.
// base    = the folder the user picked (mods/, config.txt, etc. go here)
// romfsRoot = base + "/romfs"  (read-only extracted assets)
static void androidApplyDataPath(const std::string& base, const std::string& romfsRoot) {
    g_androidRomfsPath = romfsRoot + "/";
    if (chdir(base.c_str()) != 0)
        printf("androidInitRomfs: chdir(%s) failed\n", base.c_str());
    else
        printf("androidInitRomfs: working dir set to %s\n", base.c_str());
}

void Assets::androidInitRomfs() {
    const char* intStorage = SDL_AndroidGetInternalStoragePath();

    // ── Check for saved preference ───────────────────────────────────────────
    std::string prefsFile = std::string(intStorage) + "/.romfs_location";
    FILE* pf = fopen(prefsFile.c_str(), "r");
    if (pf) {
        char buf[1024] = {};
        if (fgets(buf, sizeof(buf), pf)) {
            std::string romfsRoot(buf);
            while (!romfsRoot.empty() && (romfsRoot.back() == '\n' || romfsRoot.back() == '\r'))
                romfsRoot.pop_back();
            struct stat st;
            if (!romfsRoot.empty() && stat((romfsRoot + "/sprites").c_str(), &st) == 0) {
                fclose(pf);
                // base = parent of romfs dir
                std::string base = romfsRoot.substr(0, romfsRoot.rfind('/'));
                printf("androidInitRomfs: using saved path %s\n", romfsRoot.c_str());
                androidApplyDataPath(base, romfsRoot);
                return;
            }
        }
        fclose(pf);
    }

    // ── First run: ask user to pick a folder via native folder picker ────────
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
        "Cold Start - First Run",
        "Pick a folder where game data and mods will be stored.\n\n"
        "This folder will be accessible from your file manager.\n"
        "Drop mods into the 'mods/' subfolder inside it.",
        nullptr);

    std::string picked = androidBrowseFolder();
    std::string base;
    if (!picked.empty()) {
        base = picked + "/ColdStart";
    } else {
        // Picker cancelled or permission denied - fall back to app external storage
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,
            "Cold Start - Setup",
            "No folder selected. Using default app storage.\n\n"
            "You can change this by clearing app data and restarting.",
            nullptr);
        const char* ext = SDL_AndroidGetExternalStoragePath();
        base = ext ? ext : intStorage;
    }
    std::string romfsRoot = base + "/romfs";

    // Show where files will land
    {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "Game data folder:\n%s\n\n"
            "Extracting assets now...", base.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Cold Start - Setup", msg, nullptr);
    }

    printf("androidInitRomfs: extracting to %s\n", romfsRoot.c_str());
    mkdirs(base);
    extractFromApk(romfsRoot);

    androidApplyDataPath(base, romfsRoot);

    // Save so future launches skip this prompt
    pf = fopen(prefsFile.c_str(), "w");
    if (pf) { fprintf(pf, "%s\n", romfsRoot.c_str()); fclose(pf); }
}
std::string Assets::androidRomfsRoot() { return g_androidRomfsPath; }

#endif // PLATFORM_ANDROID

#ifndef PLATFORM_ANDROID
std::string Assets::androidRomfsRoot() { return ""; }
#endif

// Platform-specific asset root prefix
static std::string assetPrefix() {
#ifdef __SWITCH__
    return "romfs:/";
#elif defined(PLATFORM_ANDROID)
    return g_androidRomfsPath; // set by androidInitRomfs()
#else
    // Resolve romfs/ relative to the executable so the game works regardless
    // of the current working directory.
    char exePath[PATH_MAX] = {};
#  ifdef _WIN32
    GetModuleFileNameA(nullptr, exePath, PATH_MAX);
#  else
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) exePath[len] = '\0';
#  endif
    std::string dir(exePath);
    auto sep = dir.find_last_of("/\\");
    if (sep != std::string::npos) dir = dir.substr(0, sep + 1);
    else dir = "./";
    return dir + "romfs/";
#endif
}

bool Assets::init(SDL_Renderer* renderer) {
    renderer_ = renderer;
    return true;
}

std::string Assets::prefix() { return assetPrefix(); }

SDL_Texture* Assets::loadRelTex(const std::string& relPath) {
    auto it = textures_.find(relPath);
    if (it != textures_.end()) return it->second;
    SDL_Texture* t = loadTex(assetPrefix() + relPath);
    if (t) textures_[relPath] = t;
    return t;
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

    std::string resolved = ModManager::instance().resolveSpriteAsset(name);
    if (resolved != name) {
        SDL_Texture* t = loadTex(resolved);
        if (t) { textures_[name] = t; return t; }
    }

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

    std::string original = "sounds/" + name;
    std::string resolved = ModManager::instance().resolveSoundAsset(original);
    if (resolved != original) {
        Mix_Chunk* c = Mix_LoadWAV(resolved.c_str());
        if (c) { chunks_[name] = c; return c; }
    }

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

    std::string original = "sounds/" + name;
    std::string resolved = ModManager::instance().resolveSoundAsset(original);
    if (resolved != original) {
        Mix_Music* m = Mix_LoadMUS(resolved.c_str());
        if (m) { musics_[name] = m; return m; }
    }

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
