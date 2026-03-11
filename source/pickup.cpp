// ─── pickup.cpp ─── Upgrade crate & pickup implementation ───────────────────
#include "pickup.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ── Upgrade info table ──
static const UpgradeInfo s_upgradeTable[] = {
    // name              description                        color               cursed
    { "Speed Up",        "+10% movement speed",             {100, 200, 255, 255}, false },  // SpeedUp
    { "Damage Up",       "+50% damage, stronger axe hits",  {255, 100, 100, 255}, false },  // DamageUp
    { "Fire Rate Up",    "+15% fire rate, faster axe reset",{255, 200, 50,  255}, false },  // FireRateUp
    { "Ammo Up",         "+5 max ammo",                     {200, 200, 200, 255}, false },  // AmmoUp
    { "Health Up",       "+1 max HP, full heal",            {255, 80,  80,  255}, false },  // HealthUp
    { "Quick Reload",    "30% faster reload",               {80,  200, 80,  255}, false },  // ReloadUp
    { "Blindness",       "5s invulnerability",               {100, 150, 255, 255}, false },  // Blindness
    { "Bomb",            "+3 bombs",                        {255, 180, 50,  255}, false },  // BombPickup
    { "Magnet",          "Bullets home slightly",           {200, 100, 255, 255}, false },  // Magnet
    { "Ricochet",        "Bullets bounce off walls",        {150, 255, 150, 255}, false },  // Ricochet
    { "Piercing",        "Bullets pierce, axe sweep widens",{255, 255, 100, 255}, false },  // Piercing
    { "Triple Shot",     "Fire 3-way spread",               {255, 150, 200, 255}, false },  // TripleShot
    { "Overclock",       "+20% fire rate, faster axe reset",{100, 255, 255, 255}, false },  // Overclock
    { "Heavy Rounds",    "+65% damage, heavier axe hits",   {210, 180, 140, 255}, false },  // HeavyRounds
    { "Bomb Core",       "+1 bomb, bombs charge faster",    {255, 140, 60, 255},  false },  // BombCore
    { "Juggernaut",      "+2 max HP, slower, wider axe",    {120, 220, 140, 255}, false },  // Juggernaut
    { "Stun Rounds",     "Hits briefly stun, axe pulse stuns", {140, 180, 255, 255}, false },  // StunRounds
    { "Scavenger",       "Kills refund 1 ammo",             {255, 240, 120, 255}, false },  // Scavenger
    { "Explosive Tips",  "Bullets pop in a small blast",    {255, 170, 80, 255},  false },  // ExplosiveTips
    { "Chain Lightning", "Bullets zap nearby enemies",      {110, 240, 255, 255}, false },  // ChainLightning
    { "Rail Slugs",      "Faster, bigger, harder shots",    {220, 220, 220, 255}, false },  // RailSlugs
    { "Sharpened Edge",  "+range, +arc, +axe damage",       {220, 220, 255, 255}, false },  // SharpenedEdge
    { "Bloodlust",       "Melee kills empower next swing",  {255, 70,  90,  255}, false },  // Bloodlust
    { "Shock Edge",      "Melee hit emits a stun pulse",    {100, 255, 255, 255}, false },  // ShockEdge
    { "Slow Down",       "-15% movement speed",             {150, 50,  50,  255}, true  },  // SlowDown
    { "Glass Cannon",    "+50% damage, -2 HP",              {255, 50,  255, 255}, true  },  // GlassCannon
};

const UpgradeInfo& getUpgradeInfo(UpgradeType type) {
    int idx = (int)type;
    if (idx < 0 || idx >= (int)UpgradeType::COUNT) idx = 0;
    return s_upgradeTable[idx];
}

// ── Crate ──
void PickupCrate::takeDamage(float dmg) {
    hp -= dmg;
    if (hp <= 0 && alive) {
        alive = false;
        opened = true;
    }
}

// ── Player upgrades ──
void PlayerUpgrades::apply(UpgradeType type) {
    switch (type) {
    case UpgradeType::SpeedUp:      speedBonus += 52.0f; break;
    case UpgradeType::DamageUp:     damageMulti += 0.5f; meleeDamageBonus += 1; break;
    case UpgradeType::FireRateUp:   fireRateBonus += 1.5f; meleeCooldownMulti *= 0.93f; break;
    case UpgradeType::AmmoUp:       ammoBonus += 3; break;
    case UpgradeType::HealthUp:     break; // handled in game logic (needs HP access)
    case UpgradeType::ReloadUp:     reloadMulti *= 0.7f; meleeCooldownMulti *= 0.95f; break;
    case UpgradeType::Blindness:    hasBlindness = true; blindnessTimer = 5.0f; break;
    case UpgradeType::BombPickup:   break; // handled in game logic
    case UpgradeType::Magnet:       hasMagnet = true; break;
    case UpgradeType::Ricochet:     hasRicochet = true; meleeArcBonus += 0.05f; break;
    case UpgradeType::Piercing:     hasPiercing = true; meleeArcBonus += 0.10f; meleeRangeBonus += 8.0f; break;
    case UpgradeType::TripleShot:   hasTripleShot = true; break;
    case UpgradeType::Overclock:    reloadMulti *= 0.8f; meleeCooldownMulti *= 0.88f; break;
    case UpgradeType::HeavyRounds:  damageMulti += 0.65f; meleeDamageBonus += 1; break;
    case UpgradeType::BombCore:
        killsPerBomb = std::max(3, killsPerBomb - 1);
        bombDashSpeedMulti *= 1.2f;
        break;
    case UpgradeType::Juggernaut:   meleeRangeBonus += 12.0f; meleeArcBonus += 0.12f; break; // other part handled in game logic
    case UpgradeType::StunRounds:   hasStunRounds = true; break;
    case UpgradeType::Scavenger:    hasScavenger = true; break;
    case UpgradeType::ExplosiveTips: hasExplosiveTips = true; break;
    case UpgradeType::ChainLightning: hasChainLightning = true; break;
    case UpgradeType::RailSlugs:    bulletSpeedMulti *= 1.30f; bulletSizeBonus += 2.5f; damageMulti += 0.35f; break;
    case UpgradeType::SharpenedEdge: meleeRangeBonus += 24.0f; meleeArcBonus += 0.16f; meleeDamageBonus += 1; break;
    case UpgradeType::Bloodlust:    hasBloodlust = true; meleeCooldownMulti *= 0.94f; break;
    case UpgradeType::ShockEdge:    hasShockEdge = true; break;
    case UpgradeType::SlowDown:     speedBonus -= 78.0f; break;
    case UpgradeType::GlassCannon:  damageMulti += 0.5f; meleeDamageBonus += 1; break;
    default: break;
    }
}

// ── Random upgrade roll ──
UpgradeType rollRandomUpgrade() {
    // Weighted roll — common stat ups more likely, special abilities rarer, cursed rare
    static const struct { UpgradeType type; int weight; } table[] = {
        { UpgradeType::SpeedUp,      20 },
        { UpgradeType::DamageUp,     20 },
        { UpgradeType::FireRateUp,   18 },
        { UpgradeType::AmmoUp,       15 },
        { UpgradeType::HealthUp,     18 },
        { UpgradeType::ReloadUp,     15 },
        { UpgradeType::Blindness,     8 },
        { UpgradeType::BombPickup,   10 },
        { UpgradeType::Magnet,        5 },
        { UpgradeType::Ricochet,      5 },
        { UpgradeType::Piercing,      5 },
        { UpgradeType::TripleShot,    4 },
        { UpgradeType::Overclock,     8 },
        { UpgradeType::HeavyRounds,   7 },
        { UpgradeType::BombCore,      6 },
        { UpgradeType::Juggernaut,    5 },
        { UpgradeType::StunRounds,    6 },
        { UpgradeType::Scavenger,     8 },
        { UpgradeType::ExplosiveTips, 5 },
        { UpgradeType::ChainLightning, 5 },
        { UpgradeType::RailSlugs,     5 },
        { UpgradeType::SharpenedEdge, 6 },
        { UpgradeType::Bloodlust,     5 },
        { UpgradeType::ShockEdge,     5 },
        { UpgradeType::SlowDown,      3 },
        { UpgradeType::GlassCannon,   3 },
    };
    int totalWeight = 0;
    for (auto& e : table) totalWeight += e.weight;

    int roll = rand() % totalWeight;
    int acc = 0;
    for (auto& e : table) {
        acc += e.weight;
        if (roll < acc) return e.type;
    }
    return UpgradeType::SpeedUp;
}

// ── Procedural pixel-art crate drawing ──
void drawCratePixelArt(SDL_Renderer* r, int cx, int cy, int size, float bob, bool glow) {
    int half = size / 2;
    int bobY = (int)(sinf(bob * 2.5f) * 3.0f);
    int y = cy + bobY;

    // Shadow
    SDL_SetRenderDrawColor(r, 0, 0, 0, 60);
    SDL_Rect shadow = { cx - half + 2, cy + half - 2, size - 4, 4 };
    SDL_RenderFillRect(r, &shadow);

    // Main body (wooden crate)
    SDL_SetRenderDrawColor(r, 160, 120, 60, 255);
    SDL_Rect body = { cx - half, y - half, size, size };
    SDL_RenderFillRect(r, &body);

    // Darker planks (horizontal stripes)
    SDL_SetRenderDrawColor(r, 130, 95, 45, 255);
    for (int i = 0; i < 3; i++) {
        SDL_Rect plank = { cx - half + 2, y - half + 4 + i * (size / 3), size - 4, 2 };
        SDL_RenderFillRect(r, &plank);
    }

    // Cross brace
    SDL_SetRenderDrawColor(r, 100, 75, 35, 255);
    // Vertical center bar
    SDL_Rect vbar = { cx - 2, y - half + 2, 4, size - 4 };
    SDL_RenderFillRect(r, &vbar);
    // Horizontal center bar
    SDL_Rect hbar = { cx - half + 2, y - 2, size - 4, 4 };
    SDL_RenderFillRect(r, &hbar);

    // Corner nails (small bright pixels)
    SDL_SetRenderDrawColor(r, 200, 200, 180, 255);
    int nail = 2;
    SDL_Rect nails[4] = {
        { cx - half + 3, y - half + 3, nail, nail },
        { cx + half - 5, y - half + 3, nail, nail },
        { cx - half + 3, y + half - 5, nail, nail },
        { cx + half - 5, y + half - 5, nail, nail },
    };
    for (auto& n : nails) SDL_RenderFillRect(r, &n);

    // Question mark or star in center
    SDL_SetRenderDrawColor(r, 255, 230, 100, 200);
    // Simple "?" made of pixels
    int qx = cx - 3, qy = y - 5;
    // top arc
    SDL_Rect q1 = { qx, qy, 8, 2 };           SDL_RenderFillRect(r, &q1);
    SDL_Rect q2 = { qx + 6, qy + 2, 2, 4 };   SDL_RenderFillRect(r, &q2);
    SDL_Rect q3 = { qx + 2, qy + 4, 4, 2 };   SDL_RenderFillRect(r, &q3);
    SDL_Rect q4 = { qx + 2, qy + 6, 2, 2 };   SDL_RenderFillRect(r, &q4);
    // dot
    SDL_Rect q5 = { qx + 2, qy + 10, 2, 2 };  SDL_RenderFillRect(r, &q5);

    // Glow ring when about to be picked up / special
    if (glow) {
        SDL_SetRenderDrawColor(r, 255, 220, 100, (Uint8)(80 + 40 * sinf(bob * 5.0f)));
        SDL_Rect gRect = { cx - half - 2, y - half - 2, size + 4, size + 4 };
        SDL_RenderDrawRect(r, &gRect);
    }
}

// ── Procedural pixel-art pickup drawing ──
void drawPickupPixelArt(SDL_Renderer* r, int cx, int cy, int size, UpgradeType type, float bob, float flash) {
    const UpgradeInfo& info = getUpgradeInfo(type);
    int half = size / 2;
    int bobY = (int)(sinf(bob * 3.0f) * 4.0f);
    int y = cy + bobY;

    // Flash effect (white overlay when about to despawn)
    bool flashOn = (flash > 0 && ((int)(flash * 8.0f) % 2 == 0));

    // Shadow
    SDL_SetRenderDrawColor(r, 0, 0, 0, 40);
    SDL_Rect shadow = { cx - half/2 + 1, cy + half - 1, half, 2 };
    SDL_RenderFillRect(r, &shadow);

    // Glow halo
    SDL_SetRenderDrawColor(r, info.color.r, info.color.g, info.color.b,
        (Uint8)(40 + 20 * sinf(bob * 4.0f)));
    SDL_Rect halo = { cx - half - 1, y - half - 1, size + 2, size + 2 };
    SDL_RenderFillRect(r, &halo);

    // Item body (rounded-ish colored square)
    if (flashOn) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 220);
    } else if (info.isCursed) {
        SDL_SetRenderDrawColor(r, 80, 30, 80, 255);
    } else {
        SDL_SetRenderDrawColor(r, info.color.r, info.color.g, info.color.b, 255);
    }
    SDL_Rect body = { cx - half + 1, y - half + 1, size - 2, size - 2 };
    SDL_RenderFillRect(r, &body);

    // Dark outline
    SDL_SetRenderDrawColor(r, 30, 30, 30, 200);
    SDL_Rect outline = { cx - half, y - half, size, size };
    SDL_RenderDrawRect(r, &outline);

    // Icon in center - simple pixel icons per type
    int ix = cx - 2, iy = y - 2;
    if (!flashOn) {
        switch (type) {
        case UpgradeType::SpeedUp:
            // Arrow right >>
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy, ix + 3, iy);
            SDL_RenderDrawLine(r, ix + 1, iy - 2, ix + 3, iy);
            SDL_RenderDrawLine(r, ix + 1, iy + 2, ix + 3, iy);
            break;
        case UpgradeType::DamageUp:
            // Sword / up arrow
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix + 1, iy - 3, ix + 1, iy + 3);
            SDL_RenderDrawLine(r, ix - 1, iy - 1, ix + 3, iy - 1);
            break;
        case UpgradeType::HealthUp: {
            // Cross / plus
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_Rect hpH = { ix - 1, iy, 6, 2 };  SDL_RenderFillRect(r, &hpH);
            SDL_Rect hpV = { ix + 1, iy - 2, 2, 6 }; SDL_RenderFillRect(r, &hpV);
            break;
        }
        case UpgradeType::Blindness: {
            // Eye / blindfold shape
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy, ix + 4, iy);
            SDL_RenderDrawLine(r, ix - 1, iy - 2, ix + 3, iy - 2);
            SDL_RenderDrawLine(r, ix - 1, iy + 2, ix + 3, iy + 2);
            SDL_RenderDrawLine(r, ix - 2, iy - 3, ix + 4, iy + 3);
            break;
        }
        case UpgradeType::BombPickup:
            // Circle (bomb)
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix, iy - 3, ix + 2, iy - 3);
            SDL_RenderDrawLine(r, ix - 2, iy - 1, ix - 2, iy + 1);
            SDL_RenderDrawLine(r, ix + 4, iy - 1, ix + 4, iy + 1);
            SDL_RenderDrawLine(r, ix, iy + 3, ix + 2, iy + 3);
            break;
        case UpgradeType::TripleShot:
            // Three dots/lines spread
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix + 1, iy - 3, ix + 1, iy + 3);
            SDL_RenderDrawLine(r, ix - 2, iy - 2, ix - 2, iy + 2);
            SDL_RenderDrawLine(r, ix + 4, iy - 2, ix + 4, iy + 2);
            break;
        case UpgradeType::Overclock:
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy + 2, ix, iy - 3);
            SDL_RenderDrawLine(r, ix, iy - 3, ix + 2, iy);
            SDL_RenderDrawLine(r, ix + 2, iy, ix + 4, iy - 1);
            SDL_RenderDrawLine(r, ix + 1, iy + 3, ix + 4, iy + 1);
            break;
        case UpgradeType::HeavyRounds: {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_Rect slug = { ix - 1, iy - 2, 6, 4 };
            SDL_RenderFillRect(r, &slug);
            SDL_RenderDrawLine(r, ix + 5, iy - 1, ix + 5, iy + 1);
            break;
        }
        case UpgradeType::BombCore: {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_Rect core = { ix - 1, iy - 1, 4, 4 };
            SDL_RenderFillRect(r, &core);
            SDL_RenderDrawLine(r, ix + 1, iy - 4, ix + 1, iy - 2);
            SDL_RenderDrawLine(r, ix + 1, iy + 2, ix + 1, iy + 4);
            SDL_RenderDrawLine(r, ix - 4, iy + 1, ix - 2, iy + 1);
            SDL_RenderDrawLine(r, ix + 4, iy + 1, ix + 6, iy + 1);
            break;
        }
        case UpgradeType::Juggernaut: {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_Rect helm = { ix - 2, iy - 2, 6, 5 };
            SDL_RenderDrawRect(r, &helm);
            SDL_RenderDrawLine(r, ix, iy + 3, ix + 2, iy + 3);
            break;
        }
        case UpgradeType::StunRounds:
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy, ix + 4, iy);
            SDL_RenderDrawLine(r, ix, iy - 3, ix + 2, iy - 1);
            SDL_RenderDrawLine(r, ix, iy + 3, ix + 2, iy + 1);
            break;
        case UpgradeType::Scavenger:
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy - 2, ix + 4, iy + 4);
            SDL_RenderDrawLine(r, ix - 2, iy + 4, ix + 1, iy + 1);
            SDL_RenderDrawLine(r, ix + 4, iy - 2, ix + 1, iy + 1);
            break;
        case UpgradeType::ExplosiveTips: {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_Rect core = { ix, iy, 3, 3 };
            SDL_RenderFillRect(r, &core);
            SDL_RenderDrawLine(r, ix + 1, iy - 3, ix + 1, iy - 1);
            SDL_RenderDrawLine(r, ix + 1, iy + 4, ix + 1, iy + 6);
            SDL_RenderDrawLine(r, ix - 3, iy + 1, ix - 1, iy + 1);
            SDL_RenderDrawLine(r, ix + 4, iy + 1, ix + 6, iy + 1);
            break;
        }
        case UpgradeType::ChainLightning:
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy - 1, ix, iy - 3);
            SDL_RenderDrawLine(r, ix, iy - 3, ix + 1, iy - 1);
            SDL_RenderDrawLine(r, ix + 1, iy - 1, ix - 1, iy + 1);
            SDL_RenderDrawLine(r, ix - 1, iy + 1, ix + 3, iy + 4);
            break;
        case UpgradeType::RailSlugs: {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_Rect slug = { ix - 2, iy - 1, 7, 3 };
            SDL_RenderFillRect(r, &slug);
            SDL_RenderDrawLine(r, ix + 5, iy - 1, ix + 5, iy + 1);
            SDL_RenderDrawLine(r, ix - 3, iy - 3, ix - 1, iy - 1);
            SDL_RenderDrawLine(r, ix - 3, iy + 3, ix - 1, iy + 1);
            break;
        }
        case UpgradeType::SharpenedEdge:
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy + 3, ix + 4, iy - 3);
            SDL_RenderDrawLine(r, ix - 1, iy + 3, ix + 5, iy - 3);
            SDL_RenderDrawLine(r, ix - 3, iy + 4, ix - 1, iy + 2);
            break;
        case UpgradeType::Bloodlust:
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix + 1, iy - 3, ix - 1, iy + 1);
            SDL_RenderDrawLine(r, ix - 1, iy + 1, ix + 1, iy + 4);
            SDL_RenderDrawLine(r, ix + 1, iy + 4, ix + 3, iy + 1);
            SDL_RenderDrawLine(r, ix + 3, iy + 1, ix + 1, iy - 3);
            break;
        case UpgradeType::ShockEdge:
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_RenderDrawLine(r, ix - 2, iy + 2, ix, iy - 2);
            SDL_RenderDrawLine(r, ix, iy - 2, ix + 2, iy + 1);
            SDL_RenderDrawLine(r, ix + 2, iy + 1, ix + 1, iy + 1);
            SDL_RenderDrawLine(r, ix + 1, iy + 1, ix + 3, iy + 4);
            break;
        default: {
            // Generic star
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_Rect st = { ix, iy, 3, 3 };
            SDL_RenderFillRect(r, &st);
            break;
        }
        }
    }
}
