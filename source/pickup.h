#pragma once
// ─── pickup.h ─── Upgrade crate & pickup system (TBOI-style) ────────────────
#include "vec2.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

// ── Upgrade categories ──
enum class UpgradeType : uint8_t {
    // Stat upgrades
    SpeedUp,        // +movement speed
    DamageUp,       // +bullet damage
    FireRateUp,     // +fire rate
    AmmoUp,         // +max ammo
    HealthUp,       // +1 max HP and heal 1
    ReloadUp,       // faster reload
    // Special abilities
    Shield,         // temporary invuln frames
    BombPickup,     // +1 bomb
    Magnet,         // bullets home slightly
    Ricochet,       // bullets bounce off walls
    Piercing,       // bullets go through enemies
    TripleShot,     // fire 3 bullets in spread
    // Negative (cursed) — rare
    SlowDown,       // -movement speed
    GlassCannon,    // +damage but -HP
    COUNT
};

// Human-readable info for UI/mod display
struct UpgradeInfo {
    const char* name;
    const char* description;
    SDL_Color   color;          // tint color for the crate glow
    bool        isCursed;       // negative/risky upgrade
};

// Global upgrade registry
const UpgradeInfo& getUpgradeInfo(UpgradeType type);

// ── Crate entity ──
struct PickupCrate {
    Vec2  pos;
    bool  alive        = true;
    float hp           = 3.0f;      // hits to break
    float bobTimer     = 0;         // visual bob animation
    float glowTimer    = 0;         // glow pulse
    bool  opened       = false;     // broken open, showing pickup
    float openTimer    = 0;         // time since opened (for despawn)

    UpgradeType contents = UpgradeType::SpeedUp;

    void  takeDamage(float dmg);
};

// ── Floating pickup (after crate breaks) ──
struct Pickup {
    Vec2  pos;
    bool  alive        = true;
    float lifetime     = 8.0f;     // despawn after this
    float age          = 0;
    float bobTimer     = 0;
    float flashTimer   = 0;        // flash when about to despawn

    UpgradeType type   = UpgradeType::SpeedUp;
};

// ── Active player upgrades tracking ──
struct PlayerUpgrades {
    float speedBonus      = 0;     // added to base speed
    float damageMulti     = 1.0f;  // bullet damage multiplier
    float fireRateBonus   = 0;     // added to base fire rate
    int   ammoBonus       = 0;     // added to max ammo
    float reloadMulti     = 1.0f;  // reload time multiplier (<1 = faster)
    bool  hasShield       = false;
    float shieldTimer     = 0;
    bool  hasMagnet       = false;
    bool  hasRicochet     = false;
    bool  hasPiercing     = false;
    bool  hasTripleShot   = false;

    void reset() { *this = PlayerUpgrades{}; }
    void apply(UpgradeType type);
};

// ── Crate sprite drawing (procedural pixel art) ──
// These draw crate/pickup sprites directly via SDL primitives
// so no external PNG is needed — but mods can override with textures.
void drawCratePixelArt(SDL_Renderer* r, int cx, int cy, int size, float bob, bool glow);
void drawPickupPixelArt(SDL_Renderer* r, int cx, int cy, int size, UpgradeType type, float bob, float flash);

// ── Crate spawn logic ──
UpgradeType rollRandomUpgrade();
