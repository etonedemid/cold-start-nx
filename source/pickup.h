#pragma once
#include "constants.h"
#include "vec2.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

// Upgrade quality / rarity tier
enum class UpgradeQuality : uint8_t {
    Common,
    Uncommon,
    Rare,
    Epic,
    Cursed,
};

// Upgrade categories
enum class UpgradeType : uint8_t {
    // Stat upgrades
    SpeedUp,        // +movement speed
    DamageUp,       // +bullet damage
    FireRateUp,     // +fire rate
    AmmoUp,         // +max ammo
    HealthUp,       // +1 max HP and heal 1
    ReloadUp,       // faster reload
    // Special abilities
    Blindness,      // temporary invuln frames
    BombPickup,     // +1 bomb
    Magnet,         // bullets home slightly
    Ricochet,       // bullets bounce off walls
    TripleShot,     // fire 3 bullets in spread
    Overclock,      // faster fire/reload
    HeavyRounds,    // more damage, slower handling
    BombCore,       // bomb-focused upgrade
    Juggernaut,     // more max HP, less speed
    StunRounds,     // bullets briefly stun enemies
    Scavenger,      // kills refund ammo
    ExplosiveTips,  // bullets make a small blast on hit
    ChainLightning, // bullets zap nearby enemies
    RailSlugs,      // faster, heavier gun shots
    SharpenedEdge,  // longer, wider axe swings
    Bloodlust,      // melee kills empower next swing
    ShockEdge,      // melee impacts emit a stun pulse
    AutoReloader,   // automatically reload when ammo hits 0
    Vampire,        // each kill restores 1 HP
    LastStand,      // at 1 HP: +100% damage and +30% speed
    // Parry upgrades
    QuickParry,     // -1s parry cooldown (stackable)
    ParrySurge,     // parry deals 2x damage, stronger knockback
    ReactiveParry,  // parry window is 2x wider
    // Negative (cursed) - rare
    SlowDown,       // -movement speed
    GlassCannon,    // +damage but -HP
    COUNT
};

// Human-readable info for UI/mod display
struct UpgradeInfo {
    const char*    name;
    const char*    description;
    SDL_Color      color;          // tint color for the crate glow
    bool           isCursed;       // negative/risky upgrade
    UpgradeQuality quality;        // rarity tier used for drop weighting
};

// Global upgrade registry
const UpgradeInfo& getUpgradeInfo(UpgradeType type);

// Crate entity
struct PickupCrate {
    Vec2  pos;
    bool  alive        = true;
    float hp           = 3.0f;      // hits to break
    float bobTimer     = 0;         // visual bob animation
    float glowTimer    = 0;         // glow pulse
    bool  opened       = false;     // broken open, showing pickup
    float openTimer    = 0;         // time since opened (for despawn)
    bool  hasContents  = true;      // false = destructible prop only, drops nothing

    UpgradeType contents = UpgradeType::SpeedUp;

    void  takeDamage(float dmg);
};

// Floating pickup (after crate breaks)
struct Pickup {
    Vec2  pos;
    bool  alive        = true;
    float lifetime     = 8.0f;     // despawn after this
    float age          = 0;
    float bobTimer     = 0;
    float flashTimer   = 0;        // flash when about to despawn

    UpgradeType type   = UpgradeType::SpeedUp;
};

// Active player upgrades tracking
struct PlayerUpgrades {
    float speedBonus      = 0;     // added to base speed
    float damageMulti     = 1.0f;  // bullet damage multiplier
    float fireRateBonus   = 0;     // added to base fire rate
    int   ammoBonus       = 0;     // added to max ammo
    float reloadMulti     = 1.0f;  // reload time multiplier (<1 = faster)
    bool  hasBlindness    = false;
    float blindnessTimer  = 0;
    int   hasMagnet          = 0;
    int   hasRicochet        = 0;
    int   hasTripleShot      = 0;
    int   hasStunRounds      = 0;
    int   hasScavenger       = 0;
    int   hasExplosiveTips   = 0;
    int   hasChainLightning  = 0;
    int   hasBloodlust       = 0;
    int   hasShockEdge       = 0;
    int   hasAutoReload      = 0;
    int   hasVampire         = 0;
    int   hasLastStand       = 0;
    float parryCdReduction = 0.0f;  // seconds shaved off parry cooldown (QuickParry, stacks)
    int   hasParrySurge      = 0;   // parry contact/reflect: (1+stacks)x damage
    int   hasReactiveParry   = 0;   // parry window: (1+stacks)x wider
    int   killsPerBomb    = KILLS_PER_BOMB;
    float bombDashSpeedMulti = 1.0f;
    float bulletSpeedMulti = 1.0f;
    float bulletSizeBonus  = 0.0f;
    float meleeRangeBonus = 0.0f;
    float meleeArcBonus   = 0.0f;
    float meleeCooldownMulti = 1.0f;
    int   meleeDamageBonus = 0;

    void reset() { *this = PlayerUpgrades{}; }
    void apply(UpgradeType type);
};

// Crate sprite drawing (procedural pixel art)
// These draw crate/pickup sprites directly via SDL primitives
// so no external PNG is needed - but mods can override with textures.
void drawCratePixelArt(SDL_Renderer* r, int cx, int cy, int size, float bob, bool glow);
void drawPickupPixelArt(SDL_Renderer* r, int cx, int cy, int size, UpgradeType type, float bob, float flash);

// Crate spawn logic
UpgradeType rollRandomUpgrade();
// Wave-aware version: for waves ≤ 25, filters out boolean upgrades already owned so
// early-game offers feel varied. After wave 25, all duplicates are allowed.
UpgradeType rollRandomUpgrade(const PlayerUpgrades& upg, int wave);
