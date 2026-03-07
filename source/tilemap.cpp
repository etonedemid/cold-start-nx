// ─── tilemap.cpp ─── Procedural arena generation ────────────────────────────
#include "tilemap.h"
#include "vec2.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

// Use portable PRNG for all map generation (mapRand / mapSrand from tilemap.h)
// so that the same seed produces the same map on every platform.
#define rand()  mapRand()
#define srand(s) mapSrand(s)

struct Room {
    int x, y, w, h;
    bool overlaps(const Room& o, int pad = 1) const {
        return !(x - pad >= o.x + o.w || o.x - pad >= x + w ||
                 y - pad >= o.y + o.h || o.y - pad >= y + h);
    }
};

void TileMap::generate(int mapWidth, int mapHeight) {
    width = mapWidth;
    height = mapHeight;
    tiles.assign(width * height, TILE_GRASS);
    ceiling.assign(width * height, CEIL_NONE);

    // Border walls
    for (int x = 0; x < width; x++) {
        set(x, 0, TILE_WALL);
        set(x, height - 1, TILE_WALL);
    }
    for (int y = 0; y < height; y++) {
        set(0, y, TILE_WALL);
        set(width - 1, y, TILE_WALL);
    }

    // Generate gravel patches (organic blob clusters with varied shapes)
    int numPatches = (width * height) / 60 + 2;
    for (int i = 0; i < numPatches; i++) {
        int cx = 3 + rand() % (width - 6);
        int cy = 3 + rand() % (height - 6);
        int baseRadius = 2 + rand() % 4;  // 2-5 tile radius
        // Use per-direction radius variation for organic shapes
        float rScale[8]; // 8 directions worth of random bulge factors
        for (int d = 0; d < 8; d++) {
            rScale[d] = 0.6f + (float)(rand() % 80) / 100.0f; // 0.6-1.4
        }
        for (int dy = -baseRadius - 1; dy <= baseRadius + 1; dy++) {
            for (int dx = -baseRadius - 1; dx <= baseRadius + 1; dx++) {
                int tx = cx + dx;
                int ty = cy + dy;
                if (tx > 0 && tx < width - 1 && ty > 0 && ty < height - 1) {
                    float dist = sqrtf((float)(dx * dx + dy * dy));
                    // Interpolate radius based on angle
                    float angle = atan2f((float)dy, (float)dx);
                    if (angle < 0) angle += 2.0f * M_PI;
                    float sector = angle / (2.0f * M_PI) * 8.0f;
                    int s0 = (int)sector % 8;
                    int s1 = (s0 + 1) % 8;
                    float t = sector - (int)sector;
                    float localScale = rScale[s0] * (1.0f - t) + rScale[s1] * t;
                    float localRadius = baseRadius * localScale;
                    if (dist <= localRadius + 0.3f) {
                        // Feathered edge: higher chance near center
                        float edgeFactor = 1.0f - (dist / (localRadius + 0.5f));
                        if (edgeFactor > 0.3f || rand() % 100 < (int)(edgeFactor * 150)) {
                            set(tx, ty, TILE_GRAVEL);
                        }
                    }
                }
            }
        }
    }

    // Generate rooms with glass ceilings (rarer now)
    std::vector<Room> rooms;
    int mapArea = width * height;
    int maxRooms = 1 + mapArea / 400;
    int attempts = 0;
    while ((int)rooms.size() < maxRooms && attempts < maxRooms * 20) {
        attempts++;
        Room r;
        r.w = 4 + rand() % 5;  // 4-8 tiles wide
        r.h = 3 + rand() % 4;  // 3-6 tiles tall
        r.x = 2 + rand() % (width - r.w - 4);
        r.y = 2 + rand() % (height - r.h - 4);

        // Don't overlap existing rooms
        bool ok = true;
        for (auto& existing : rooms) {
            if (r.overlaps(existing, 2)) { ok = false; break; }
        }
        // Don't overlap center spawn area
        int cx = width / 2, cy = height / 2;
        Room center = {cx - 3, cy - 3, 6, 6};
        if (r.overlaps(center, 1)) ok = false;

        if (!ok) continue;
        rooms.push_back(r);

        // Room interior: keep existing ground tiles (grass/gravel), just add glass ceiling
        for (int ry = r.y; ry < r.y + r.h; ry++) {
            for (int rx = r.x; rx < r.x + r.w; rx++) {
                // Don't overwrite the ground tile — room floor = same as map
                ceiling[ry * width + rx] = CEIL_GLASS;
            }
        }

        // Walls around the room perimeter
        for (int rx = r.x - 1; rx <= r.x + r.w; rx++) {
            if (isInBounds(rx, r.y - 1) && !isSolid(rx, r.y - 1) && ceiling[((r.y-1) * width + rx)] != CEIL_GLASS)
                set(rx, r.y - 1, TILE_WALL);
            if (isInBounds(rx, r.y + r.h) && !isSolid(rx, r.y + r.h) && ceiling[((r.y+r.h) * width + rx)] != CEIL_GLASS)
                set(rx, r.y + r.h, TILE_WALL);
        }
        for (int ry = r.y - 1; ry <= r.y + r.h; ry++) {
            if (isInBounds(r.x - 1, ry) && !isSolid(r.x - 1, ry) && ceiling[(ry * width + (r.x-1))] != CEIL_GLASS)
                set(r.x - 1, ry, TILE_WALL);
            if (isInBounds(r.x + r.w, ry) && !isSolid(r.x + r.w, ry) && ceiling[(ry * width + (r.x+r.w))] != CEIL_GLASS)
                set(r.x + r.w, ry, TILE_WALL);
        }

        // Create 2-3 wide doorway entrances on different sides
        int numDoors = 2 + rand() % 2;
        bool usedSide[4] = {false, false, false, false};
        for (int d = 0; d < numDoors; d++) {
            // Pick an unused side if possible
            int side;
            int tries = 0;
            do {
                side = rand() % 4;
                tries++;
            } while (usedSide[side] && tries < 8);
            usedSide[side] = true;

            // Make 2-wide doorways
            if (side == 0) { // top
                int dx = r.x + 1 + rand() % std::max(1, r.w - 2);
                for (int i = 0; i < 2 && dx + i < r.x + r.w; i++) {
                    int tx = dx + i, ty = r.y - 1;
                    if (isInBounds(tx, ty) && tx > 0 && tx < width-1 && ty > 0) {
                        set(tx, ty, TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            } else if (side == 1) { // bottom
                int dx = r.x + 1 + rand() % std::max(1, r.w - 2);
                for (int i = 0; i < 2 && dx + i < r.x + r.w; i++) {
                    int tx = dx + i, ty = r.y + r.h;
                    if (isInBounds(tx, ty) && tx > 0 && tx < width-1 && ty < height-1) {
                        set(tx, ty, TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            } else if (side == 2) { // left
                int dy = r.y + 1 + rand() % std::max(1, r.h - 2);
                for (int i = 0; i < 2 && dy + i < r.y + r.h; i++) {
                    int tx = r.x - 1, ty = dy + i;
                    if (isInBounds(tx, ty) && ty > 0 && ty < height-1 && tx > 0) {
                        set(tx, ty, TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            } else { // right
                int dy = r.y + 1 + rand() % std::max(1, r.h - 2);
                for (int i = 0; i < 2 && dy + i < r.y + r.h; i++) {
                    int tx = r.x + r.w, ty = dy + i;
                    if (isInBounds(tx, ty) && ty > 0 && ty < height-1 && tx < width-1) {
                        set(tx, ty, TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            }
        }
    }

    // ── Buildings (multi-room conjoined structures) on large maps ──
    if (mapArea > 2000) {
        int maxBuildings = 1 + mapArea / 2000;
        for (int bi = 0; bi < maxBuildings; bi++) {
            int numBldRooms = 2 + rand() % 3; // 2-4 rooms per building
            std::vector<Room> bldRooms;
            // Seed room for building
            Room seed;
            seed.w = 4 + rand() % 4;
            seed.h = 3 + rand() % 3;
            seed.x = 3 + rand() % std::max(1, width - seed.w - 6);
            seed.y = 3 + rand() % std::max(1, height - seed.h - 6);
            // Check overlap with existing rooms and center
            bool seedOk = true;
            for (auto& existing : rooms) {
                if (seed.overlaps(existing, 3)) { seedOk = false; break; }
            }
            int cx = width / 2, cy = height / 2;
            Room center = {cx - 4, cy - 4, 8, 8};
            if (seed.overlaps(center, 2)) seedOk = false;
            if (!seedOk) continue;

            bldRooms.push_back(seed);
            rooms.push_back(seed);

            // Grow building by attaching rooms to existing ones
            for (int ri = 1; ri < numBldRooms; ri++) {
                bool placed = false;
                for (int att = 0; att < 20 && !placed; att++) {
                    Room& parent = bldRooms[rand() % bldRooms.size()];
                    Room nr;
                    nr.w = 3 + rand() % 4;
                    nr.h = 3 + rand() % 3;
                    int side = rand() % 4;
                    if (side == 0) { // attach above
                        nr.x = parent.x + rand() % std::max(1, parent.w - 2);
                        nr.y = parent.y - nr.h;
                    } else if (side == 1) { // below
                        nr.x = parent.x + rand() % std::max(1, parent.w - 2);
                        nr.y = parent.y + parent.h;
                    } else if (side == 2) { // left
                        nr.x = parent.x - nr.w;
                        nr.y = parent.y + rand() % std::max(1, parent.h - 2);
                    } else { // right
                        nr.x = parent.x + parent.w;
                        nr.y = parent.y + rand() % std::max(1, parent.h - 2);
                    }
                    // Bounds check
                    if (nr.x < 2 || nr.y < 2 || nr.x + nr.w >= width - 2 || nr.y + nr.h >= height - 2)
                        continue;
                    // Overlap with other rooms (allow touching parent)
                    bool nrOk = true;
                    for (auto& er : rooms) {
                        if (&er == &parent) continue; // skip parent
                        if (nr.overlaps(er, 1)) { nrOk = false; break; }
                    }
                    if (nr.overlaps(center, 2)) nrOk = false;
                    if (!nrOk) continue;

                    bldRooms.push_back(nr);
                    rooms.push_back(nr);

                    // Fill interior with glass ceiling
                    for (int ry = nr.y; ry < nr.y + nr.h; ry++)
                        for (int rx = nr.x; rx < nr.x + nr.w; rx++)
                            ceiling[ry * width + rx] = CEIL_GLASS;

                    // Walls around new room (skip shared wall with parent)
                    for (int rx = nr.x - 1; rx <= nr.x + nr.w; rx++) {
                        if (isInBounds(rx, nr.y - 1) && !isSolid(rx, nr.y - 1) && ceiling[((nr.y-1)*width+rx)] != CEIL_GLASS)
                            set(rx, nr.y - 1, TILE_WALL);
                        if (isInBounds(rx, nr.y + nr.h) && !isSolid(rx, nr.y + nr.h) && ceiling[((nr.y+nr.h)*width+rx)] != CEIL_GLASS)
                            set(rx, nr.y + nr.h, TILE_WALL);
                    }
                    for (int ry = nr.y - 1; ry <= nr.y + nr.h; ry++) {
                        if (isInBounds(nr.x - 1, ry) && !isSolid(nr.x - 1, ry) && ceiling[(ry*width+(nr.x-1))] != CEIL_GLASS)
                            set(nr.x - 1, ry, TILE_WALL);
                        if (isInBounds(nr.x + nr.w, ry) && !isSolid(nr.x + nr.w, ry) && ceiling[(ry*width+(nr.x+nr.w))] != CEIL_GLASS)
                            set(nr.x + nr.w, ry, TILE_WALL);
                    }

                    // Open shared wall to create internal doorway
                    if (side == 0 || side == 1) {
                        int wallY = (side == 0) ? parent.y - 1 : parent.y + parent.h;
                        int overlapL = std::max(nr.x, parent.x);
                        int overlapR = std::min(nr.x + nr.w, parent.x + parent.w);
                        if (overlapR - overlapL >= 2) {
                            int dx = overlapL + rand() % std::max(1, overlapR - overlapL - 1);
                            for (int i = 0; i < 2 && dx + i < overlapR; i++) {
                                if (isInBounds(dx + i, wallY)) {
                                    set(dx + i, wallY, TILE_GRASS);
                                    ceiling[wallY * width + (dx+i)] = CEIL_GLASS;
                                }
                            }
                        }
                    } else {
                        int wallX = (side == 2) ? parent.x - 1 : parent.x + parent.w;
                        int overlapT = std::max(nr.y, parent.y);
                        int overlapB = std::min(nr.y + nr.h, parent.y + parent.h);
                        if (overlapB - overlapT >= 2) {
                            int dy = overlapT + rand() % std::max(1, overlapB - overlapT - 1);
                            for (int i = 0; i < 2 && dy + i < overlapB; i++) {
                                if (isInBounds(wallX, dy + i)) {
                                    set(wallX, dy + i, TILE_GRASS);
                                    ceiling[(dy+i) * width + wallX] = CEIL_GLASS;
                                }
                            }
                        }
                    }

                    // External doorways for new room
                    int extDoor = 1 + rand() % 2;
                    for (int ed = 0; ed < extDoor; ed++) {
                        int es = rand() % 4;
                        if (es == side) es = (es + 1) % 4; // skip internal side
                        if (es == 0) {
                            int dx2 = nr.x + 1 + rand() % std::max(1, nr.w - 2);
                            for (int i = 0; i < 2 && dx2 + i < nr.x + nr.w; i++)
                                if (isInBounds(dx2+i, nr.y-1) && (dx2+i) > 0 && nr.y-1 > 0) {
                                    set(dx2+i, nr.y-1, TILE_GRASS);
                                    ceiling[(nr.y-1)*width+(dx2+i)] = CEIL_GLASS;
                                }
                        } else if (es == 1) {
                            int dx2 = nr.x + 1 + rand() % std::max(1, nr.w - 2);
                            for (int i = 0; i < 2 && dx2 + i < nr.x + nr.w; i++)
                                if (isInBounds(dx2+i, nr.y+nr.h) && (dx2+i) > 0 && nr.y+nr.h < height-1) {
                                    set(dx2+i, nr.y+nr.h, TILE_GRASS);
                                    ceiling[(nr.y+nr.h)*width+(dx2+i)] = CEIL_GLASS;
                                }
                        } else if (es == 2) {
                            int dy2 = nr.y + 1 + rand() % std::max(1, nr.h - 2);
                            for (int i = 0; i < 2 && dy2 + i < nr.y + nr.h; i++)
                                if (isInBounds(nr.x-1, dy2+i) && dy2+i > 0 && nr.x-1 > 0) {
                                    set(nr.x-1, dy2+i, TILE_GRASS);
                                    ceiling[(dy2+i)*width+(nr.x-1)] = CEIL_GLASS;
                                }
                        } else {
                            int dy2 = nr.y + 1 + rand() % std::max(1, nr.h - 2);
                            for (int i = 0; i < 2 && dy2 + i < nr.y + nr.h; i++)
                                if (isInBounds(nr.x+nr.w, dy2+i) && dy2+i > 0 && nr.x+nr.w < width-1) {
                                    set(nr.x+nr.w, dy2+i, TILE_GRASS);
                                    ceiling[(dy2+i)*width+(nr.x+nr.w)] = CEIL_GLASS;
                                }
                        }
                    }
                    placed = true;
                }
            }
        }
    }

    // Make sure center is clear for player spawn
    for (int y = height / 2 - 2; y <= height / 2 + 2; y++)
        for (int x = width / 2 - 2; x <= width / 2 + 2; x++)
            if (isInBounds(x, y)) {
                set(x, y, TILE_GRASS);
                ceiling[y * width + x] = CEIL_NONE;
            }

    // Scatter some breakable box clusters in open areas
    int numBoxClusters = 2 + (width * height) / 300;
    for (int i = 0; i < numBoxClusters; i++) {
        int bx = 3 + rand() % (width - 6);
        int by = 3 + rand() % (height - 6);
        // Don't place near center spawn
        if (abs(bx - width/2) < 4 && abs(by - height/2) < 4) continue;
        // Place 1-3 boxes in an L or line shape
        int count = 1 + rand() % 3;
        for (int j = 0; j < count; j++) {
            int tx = bx + (j % 2);
            int ty = by + (j / 2);
            if (isInBounds(tx, ty) && !isSolid(tx, ty) && ceiling[ty * width + tx] == CEIL_NONE) {
                set(tx, ty, TILE_BOX);
            }
        }
    }

    findSpawnPoints();
}

bool TileMap::isSolid(int tx, int ty) const {
    if (!isInBounds(tx, ty)) return true;
    uint8_t t = get(tx, ty);
    return t == TILE_WALL || t == TILE_GLASS || t == TILE_DESK || t == TILE_BOX;
}

bool TileMap::isInBounds(int tx, int ty) const {
    return tx >= 0 && tx < width && ty >= 0 && ty < height;
}

uint8_t TileMap::get(int tx, int ty) const {
    if (!isInBounds(tx, ty)) return TILE_WALL;
    return tiles[ty * width + tx];
}

void TileMap::set(int tx, int ty, uint8_t t) {
    tiles[ty * width + tx] = t;
}

bool TileMap::worldCollides(float wx, float wy, float halfSize) const {
    // Check all tiles covered by the AABB
    int x0 = toTile(wx - halfSize);
    int y0 = toTile(wy - halfSize);
    int x1 = toTile(wx + halfSize);
    int y1 = toTile(wy + halfSize);

    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (isSolid(tx, ty)) return true;
    return false;
}

// Helper: check if a tile and its immediate neighbors are all non-solid
bool TileMap::isSpawnSafe(int tx, int ty) const {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (isSolid(tx + dx, ty + dy)) return false;
    return true;
}

void TileMap::findSpawnPoints() {
    spawnPoints.clear();
    // Edges (inside walls) as spawn points — verify tile + neighbors are clear
    for (int x = 2; x < width - 2; x += 3) {
        if (isSpawnSafe(x, 2))          spawnPoints.push_back({toWorld(x), toWorld(2)});
        if (isSpawnSafe(x, height - 3)) spawnPoints.push_back({toWorld(x), toWorld(height - 3)});
    }
    for (int y = 2; y < height - 2; y += 3) {
        if (isSpawnSafe(2, y))          spawnPoints.push_back({toWorld(2), toWorld(y)});
        if (isSpawnSafe(width - 3, y))  spawnPoints.push_back({toWorld(width - 3), toWorld(y)});
    }
    // Also add interior open spots for larger maps
    for (int y = 4; y < height - 4; y += 6) {
        for (int x = 4; x < width - 4; x += 6) {
            if (isSpawnSafe(x, y)) {
                spawnPoints.push_back({toWorld(x), toWorld(y)});
            }
        }
    }
    // Ensure at least 6 spawn points with safety check
    if (spawnPoints.size() < 6) {
        // Scan entire map for valid spots
        for (int y = 2; y < height - 2 && spawnPoints.size() < 12; y += 2) {
            for (int x = 2; x < width - 2 && spawnPoints.size() < 12; x += 2) {
                if (isSpawnSafe(x, y)) {
                    spawnPoints.push_back({toWorld(x), toWorld(y)});
                }
            }
        }
    }
}
