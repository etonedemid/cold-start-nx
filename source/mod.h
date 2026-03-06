#pragma once
// ─── mod.h ─── Modding system (SRB2-style extensibility) ────────────────────
// Mod folder layout:
//   mods/
//     mymod/
//       mod.cfg              ← mod manifest (INI format)
//       scripts/             ← game scripts (future Lua/custom)
//       characters/          ← .cschar files and sprite folders
//       maps/                ← .csm map files
//       packs/               ← .cspack campaign files
//       sprites/             ← override or new sprites
//       sounds/              ← override or new sounds
//       gamemodes/           ← custom gamemode definitions
//       items/               ← custom item/upgrade definitions
//
// mod.cfg format:
//   [mod]
//   id=mymod                          ← unique identifier
//   name=My Cool Mod
//   author=SomePlayer
//   version=1.0
//   description=Adds cool stuff
//   game_version=1                    ← minimum game version
//
//   [content]
//   characters=true                   ← scan characters/ subfolder
//   maps=true                         ← scan maps/ subfolder
//   packs=true                        ← scan packs/ subfolder
//   sprites=true                      ← override sprites
//   sounds=true                       ← override sounds
//   gamemodes=true                    ← register custom gamemodes
//   items=true                        ← register custom items
//
//   [overrides]
//   ; override base game values
//   player_speed=600
//   enemy_hp=5
//   gravity=0
//
//   [dependencies]
//   ; required mods
//   dep1=othermod
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "charformat.h"
#include "mapformat.h"
#include "mappack.h"
#include "gamemode.h"

// ── Content flags ──
struct ModContent {
    bool characters  = false;
    bool maps        = false;
    bool packs       = false;
    bool sprites     = false;
    bool sounds      = false;
    bool gamemodes   = false;
    bool items       = false;
};

// ── Value overrides ──
struct ModOverrides {
    std::unordered_map<std::string, std::string> values;

    bool has(const std::string& key) const { return values.count(key) > 0; }
    std::string get(const std::string& key, const std::string& def = "") const {
        auto it = values.find(key);
        return it != values.end() ? it->second : def;
    }
    float getFloat(const std::string& key, float def = 0) const;
    int   getInt(const std::string& key, int def = 0) const;
};

// ── Item definition (mod-added upgrades) ──
struct ModItemDef {
    std::string id;            // unique item ID e.g. "mymod_laser"
    std::string name;
    std::string description;
    std::string spritePath;    // path to item sprite
    std::string modSource;     // which mod provides this

    // Effect parameters (INI key=value)
    std::unordered_map<std::string, std::string> params;
};

// ── Single mod instance ──
struct Mod {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    std::string folder;        // absolute path to mod folder
    int         gameVersion = 1;

    ModContent  content;
    ModOverrides overrides;
    std::vector<std::string> dependencies;

    // Loaded content references
    std::vector<std::string> characterPaths;
    std::vector<std::string> mapPaths;
    std::vector<std::string> packPaths;
    std::vector<ModItemDef>  items;
    std::vector<GameModeEntry> gamemodes;

    // Sprite/sound override mappings (original → mod path)
    std::unordered_map<std::string, std::string> spriteOverrides;
    std::unordered_map<std::string, std::string> soundOverrides;

    bool enabled = true;
    int  loadOrder = 0;

    bool loadFromFolder(const std::string& path);
};

// ── Mod Manager — scans, loads, manages all mods ──
class ModManager {
public:
    static ModManager& instance();

    // Scan all mod directories for mods
    void scanMods();

    // Load/unload
    void loadAllEnabled();
    void unloadAll();

    // Mod list access
    const std::vector<Mod>& mods() const { return mods_; }
    Mod* findMod(const std::string& id);
    void setEnabled(const std::string& id, bool enabled);

    // Merged content queries (across all enabled mods)
    std::vector<std::string> allCharacterPaths() const;
    std::vector<std::string> allMapPaths() const;
    std::vector<std::string> allPackPaths() const;
    std::vector<ModItemDef>  allItems() const;

    // Sprite/sound override resolution
    // Returns overridden path if any mod provides one, else original
    std::string resolveSpriteAsset(const std::string& original) const;
    std::string resolveSoundAsset(const std::string& original) const;

    // Merged overrides (later mods win)
    ModOverrides mergedOverrides() const;

    // Save/load mod enable states
    void saveModConfig();
    void loadModConfig();

    // Get all content for network sync
    struct SyncManifest {
        std::vector<std::string> maps;
        std::vector<std::string> characters;
        std::vector<std::string> packs;
    };
    SyncManifest buildSyncManifest() const;

    // Network mod sync: serialize all enabled mods into a blob
    // Includes mod.cfg + small content files (skip large media)
    std::vector<uint8_t> serializeEnabledMods() const;
    // Install mods from serialized blob (writes to mods/_mp_sync/)
    void deserializeAndInstallMods(const std::vector<uint8_t>& data);

private:
    std::vector<Mod> mods_;
    std::vector<std::string> scanDirs_;
    void scanDirectory(const std::string& dir);
};
