#pragma once
// ─── entity.h ─── Base types and entity components ──────────────────────────
#include "vec2.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <vector>

// ── Tags for collision filtering ──
enum Tag : uint8_t {
    TAG_NONE        = 0,
    TAG_PLAYER      = 1,
    TAG_ENEMY       = 2,
    TAG_BULLET      = 3,
    TAG_ENEMY_BULLET= 4,
    TAG_BOMB        = 5,
    TAG_EXPLOSION   = 6,
    TAG_WALL        = 7,
};

// ── Generic entity used by bullets, debris, explosions ──
struct Entity {
    Vec2  pos;
    Vec2  vel;
    float rotation  = 0;       // radians
    float size      = 16;      // half-extent for AABB
    float lifetime  = -1;      // <0 = infinite
    float age       = 0;
    Tag   tag       = TAG_NONE;
    bool  alive     = true;
    bool  piercing   = false;   // bullet passes through enemies
    int   damage    = 1;
    int   bounces   = 0;        // ricochet bounce count
    uint32_t netId  = 0;        // network ID for remote bullet tracking (0 = not networked)
    uint8_t ownerId = 255;      // player ID who fired this bullet (255 = unowned/local-only)

    // Sprite
    SDL_Texture* sprite = nullptr;
    int spriteW = 0, spriteH = 0;

    void tick(float dt) {
        pos += vel * dt;
        age += dt;
        if (lifetime > 0 && age >= lifetime) alive = false;
    }

    SDL_FRect bounds() const {
        return {pos.x - size, pos.y - size, size*2, size*2};
    }
};

// ── Simple AABB overlap ──
inline bool overlaps(const SDL_FRect& a, const SDL_FRect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

// ── Circle overlap ──
inline bool circleOverlap(Vec2 a, float ra, Vec2 b, float rb) {
    return Vec2::dist(a, b) < (ra + rb);
}
