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

        // Room interior: glass ceiling, and for half of rooms a tiled floor
        // (randomly rotated per tile at render time).
        bool roomTileFloor = (rand() % 2 == 0);
        for (int ry = r.y; ry < r.y + r.h; ry++) {
            for (int rx = r.x; rx < r.x + r.w; rx++) {
                if (roomTileFloor) set(rx, ry, TILE_TILEFLOOR);
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
                        set(tx, ty, roomTileFloor ? TILE_TILEFLOOR : TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            } else if (side == 1) { // bottom
                int dx = r.x + 1 + rand() % std::max(1, r.w - 2);
                for (int i = 0; i < 2 && dx + i < r.x + r.w; i++) {
                    int tx = dx + i, ty = r.y + r.h;
                    if (isInBounds(tx, ty) && tx > 0 && tx < width-1 && ty < height-1) {
                        set(tx, ty, roomTileFloor ? TILE_TILEFLOOR : TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            } else if (side == 2) { // left
                int dy = r.y + 1 + rand() % std::max(1, r.h - 2);
                for (int i = 0; i < 2 && dy + i < r.y + r.h; i++) {
                    int tx = r.x - 1, ty = dy + i;
                    if (isInBounds(tx, ty) && ty > 0 && ty < height-1 && tx > 0) {
                        set(tx, ty, roomTileFloor ? TILE_TILEFLOOR : TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            } else { // right
                int dy = r.y + 1 + rand() % std::max(1, r.h - 2);
                for (int i = 0; i < 2 && dy + i < r.y + r.h; i++) {
                    int tx = r.x + r.w, ty = dy + i;
                    if (isInBounds(tx, ty) && ty > 0 && ty < height-1 && tx < width-1) {
                        set(tx, ty, roomTileFloor ? TILE_TILEFLOOR : TILE_GRASS);
                        ceiling[ty * width + tx] = CEIL_GLASS;
                    }
                }
            }
        }
    }

    // Buildings (multi-room conjoined structures) on large maps
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
            // Tiled floor for the seed room too (all buildings get tile floors)
            for (int ry = seed.y; ry < seed.y + seed.h; ry++)
                for (int rx = seed.x; rx < seed.x + seed.w; rx++)
                    set(rx, ry, TILE_TILEFLOOR);

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

                    // Fill interior with tiled floor + glass ceiling (all buildings)
                    for (int ry = nr.y; ry < nr.y + nr.h; ry++)
                        for (int rx = nr.x; rx < nr.x + nr.w; rx++) {
                            set(rx, ry, TILE_TILEFLOOR);
                            ceiling[ry * width + rx] = CEIL_GLASS;
                        }

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
                                    set(dx + i, wallY, TILE_TILEFLOOR);
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
                                    set(wallX, dy + i, TILE_TILEFLOOR);
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
                                    set(dx2+i, nr.y-1, TILE_TILEFLOOR);
                                    ceiling[(nr.y-1)*width+(dx2+i)] = CEIL_GLASS;
                                }
                        } else if (es == 1) {
                            int dx2 = nr.x + 1 + rand() % std::max(1, nr.w - 2);
                            for (int i = 0; i < 2 && dx2 + i < nr.x + nr.w; i++)
                                if (isInBounds(dx2+i, nr.y+nr.h) && (dx2+i) > 0 && nr.y+nr.h < height-1) {
                                    set(dx2+i, nr.y+nr.h, TILE_TILEFLOOR);
                                    ceiling[(nr.y+nr.h)*width+(dx2+i)] = CEIL_GLASS;
                                }
                        } else if (es == 2) {
                            int dy2 = nr.y + 1 + rand() % std::max(1, nr.h - 2);
                            for (int i = 0; i < 2 && dy2 + i < nr.y + nr.h; i++)
                                if (isInBounds(nr.x-1, dy2+i) && dy2+i > 0 && nr.x-1 > 0) {
                                    set(nr.x-1, dy2+i, TILE_TILEFLOOR);
                                    ceiling[(dy2+i)*width+(nr.x-1)] = CEIL_GLASS;
                                }
                        } else {
                            int dy2 = nr.y + 1 + rand() % std::max(1, nr.h - 2);
                            for (int i = 0; i < 2 && dy2 + i < nr.y + nr.h; i++)
                                if (isInBounds(nr.x+nr.w, dy2+i) && dy2+i > 0 && nr.x+nr.w < width-1) {
                                    set(nr.x+nr.w, dy2+i, TILE_TILEFLOOR);
                                    ceiling[(dy2+i)*width+(nr.x+nr.w)] = CEIL_GLASS;
                                }
                        }
                    }
                    placed = true;
                }
            }
        }
    }

    // Chained rooms: a linear "building" that snakes across the map, each room
    // joined to the previous by a doorway. Distinct from the bushy building pass
    // above (which attaches rooms to random members). Gated on medium+ maps.
    if (mapArea >= 1500) {
        int numChains = 1 + mapArea / 3000;

        // Overlap test against all placed rooms except one (the room we're
        // deliberately attaching to, which we're allowed to touch).
        auto fitsIdx = [&](const Room& r, int skipIdx) {
            if (r.x < 2 || r.y < 2 || r.x + r.w >= width - 2 || r.y + r.h >= height - 2) return false;
            Room center = {width / 2 - 4, height / 2 - 4, 8, 8};
            if (r.overlaps(center, 2)) return false;
            for (int k = 0; k < (int)rooms.size(); k++) {
                if (k == skipIdx) continue;
                if (r.overlaps(rooms[k], 1)) return false;
            }
            return true;
        };
        // Stamp a room: glass-ceiling interior + perimeter walls (kept off any
        // cell already carved as glass, so shared edges stay openable).
        auto carve = [&](const Room& r) {
            for (int ry = r.y; ry < r.y + r.h; ry++)
                for (int rx = r.x; rx < r.x + r.w; rx++) {
                    set(rx, ry, TILE_TILEFLOOR);  // all buildings get tiled floors
                    ceiling[ry * width + rx] = CEIL_GLASS;
                }
            for (int rx = r.x - 1; rx <= r.x + r.w; rx++) {
                if (isInBounds(rx, r.y - 1)   && ceiling[(r.y-1)*width+rx]   != CEIL_GLASS) set(rx, r.y - 1,   TILE_WALL);
                if (isInBounds(rx, r.y + r.h) && ceiling[(r.y+r.h)*width+rx] != CEIL_GLASS) set(rx, r.y + r.h, TILE_WALL);
            }
            for (int ry = r.y - 1; ry <= r.y + r.h; ry++) {
                if (isInBounds(r.x - 1, ry)   && ceiling[ry*width+(r.x-1)]   != CEIL_GLASS) set(r.x - 1, ry,   TILE_WALL);
                if (isInBounds(r.x + r.w, ry) && ceiling[ry*width+(r.x+r.w)] != CEIL_GLASS) set(r.x + r.w, ry, TILE_WALL);
            }
        };
        // Breach one perimeter wall to open ground so the building is enterable.
        auto carveEntrance = [&](const Room& r) -> bool {
            for (int attempt = 0; attempt < 24; attempt++) {
                int s = rand() % 4, wx, wy, ox, oy;
                if      (s == 0) { wx = r.x + 1 + rand()%std::max(1,r.w-2); wy = r.y - 1;     ox = wx;          oy = r.y - 2; }
                else if (s == 1) { wx = r.x + 1 + rand()%std::max(1,r.w-2); wy = r.y + r.h;   ox = wx;          oy = r.y + r.h + 1; }
                else if (s == 2) { wy = r.y + 1 + rand()%std::max(1,r.h-2); wx = r.x - 1;     ox = r.x - 2;     oy = wy; }
                else             { wy = r.y + 1 + rand()%std::max(1,r.h-2); wx = r.x + r.w;   ox = r.x + r.w+1; oy = wy; }
                if (wx <= 0 || wy <= 0 || wx >= width-1 || wy >= height-1) continue;
                if (!isInBounds(ox, oy)) continue;
                // Outside cell must be genuinely open ground (not a wall and not
                // another room), so the entrance reaches the arena.
                if (isSolid(ox, oy) || ceiling[oy*width+ox] == CEIL_GLASS) continue;
                set(wx, wy, TILE_TILEFLOOR); ceiling[wy*width+wx] = CEIL_GLASS;
                int wx2 = (s <= 1) ? wx + 1 : wx, wy2 = (s <= 1) ? wy : wy + 1;
                if (isInBounds(wx2, wy2) && wx2 > 0 && wy2 > 0 && wx2 < width-1 && wy2 < height-1) {
                    set(wx2, wy2, TILE_TILEFLOOR); ceiling[wy2*width+wx2] = CEIL_GLASS;
                }
                return true;
            }
            return false;
        };

        for (int ci = 0; ci < numChains; ci++) {
            int chainLen = 3 + rand() % 4;  // 3-6 rooms

            // Seed the chain somewhere that fits.
            Room cur; bool seeded = false;
            for (int t = 0; t < 30 && !seeded; t++) {
                cur.w = 4 + rand() % 3; cur.h = 3 + rand() % 3;
                cur.x = 3 + rand() % std::max(1, width - cur.w - 6);
                cur.y = 3 + rand() % std::max(1, height - cur.h - 6);
                if (fitsIdx(cur, -1)) seeded = true;
            }
            if (!seeded) continue;
            carve(cur);
            rooms.push_back(cur);
            int curIdx = (int)rooms.size() - 1;
            int chainStart = curIdx;

            for (int ri = 1; ri < chainLen; ri++) {
                bool placed = false;
                for (int t = 0; t < 16 && !placed; t++) {
                    Room nr; nr.w = 3 + rand() % 4; nr.h = 3 + rand() % 3;
                    int side = rand() % 4;
                    if      (side == 0) { nr.x = cur.x + rand() % std::max(1, cur.w - 2); nr.y = cur.y - nr.h - 1; }
                    else if (side == 1) { nr.x = cur.x + rand() % std::max(1, cur.w - 2); nr.y = cur.y + cur.h + 1; }
                    else if (side == 2) { nr.x = cur.x - nr.w - 1; nr.y = cur.y + rand() % std::max(1, cur.h - 2); }
                    else                { nr.x = cur.x + cur.w + 1; nr.y = cur.y + rand() % std::max(1, cur.h - 2); }
                    if (!fitsIdx(nr, curIdx)) continue;

                    carve(nr);
                    // Open a 2-wide doorway in the wall shared with cur.
                    if (side == 0 || side == 1) {
                        int wallY = (side == 0) ? cur.y - 1 : cur.y + cur.h;
                        int oL = std::max(nr.x, cur.x), oR = std::min(nr.x + nr.w, cur.x + cur.w);
                        if (oR - oL >= 2) {
                            int dx = oL + rand() % std::max(1, oR - oL - 1);
                            for (int i = 0; i < 2 && dx + i < oR; i++)
                                if (isInBounds(dx + i, wallY)) { set(dx + i, wallY, TILE_TILEFLOOR); ceiling[wallY*width+(dx+i)] = CEIL_GLASS; }
                        }
                    } else {
                        int wallX = (side == 2) ? cur.x - 1 : cur.x + cur.w;
                        int oT = std::max(nr.y, cur.y), oB = std::min(nr.y + nr.h, cur.y + cur.h);
                        if (oB - oT >= 2) {
                            int dy = oT + rand() % std::max(1, oB - oT - 1);
                            for (int i = 0; i < 2 && dy + i < oB; i++)
                                if (isInBounds(wallX, dy + i)) { set(wallX, dy + i, TILE_TILEFLOOR); ceiling[(dy+i)*width+wallX] = CEIL_GLASS; }
                        }
                    }
                    rooms.push_back(nr);
                    cur = nr; curIdx = (int)rooms.size() - 1;
                    placed = true;
                }
                if (!placed) break;  // nowhere to extend - end this chain
            }

            // Doorways to the arena: bigger buildings get more entrances so they
            // aren't single-choke deathtraps (~1 per 3 rooms, min 1). Each lands
            // on a different room. A connectivity backstop below still guarantees
            // reachability if a chain is fully hemmed in.
            int roomsInChain = (int)rooms.size() - chainStart;
            int entrancesWanted = (roomsInChain >= 4) ? 2 : 1;  // "big" = 4+ rooms
            int carvedEntrances = 0;
            for (int k = chainStart; k < (int)rooms.size() && carvedEntrances < entrancesWanted; k++)
                if (carveEntrance(rooms[k])) carvedEntrances++;
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

    // Connectivity guarantee: every walkable tile must be reachable from the
    // center spawn. Any sealed pocket (e.g. a hemmed-in building) gets a 1-wide
    // corridor punched toward the center, so enemies/pickups can't end up
    // stranded - which would otherwise softlock a wave clear.
    {
        const int cx = width / 2, cy = height / 2;
        for (int pass = 0; pass < 8; pass++) {
            std::vector<uint8_t> vis((size_t)width * height, 0);
            std::vector<int> stack;
            stack.push_back(cy * width + cx);
            vis[cy * width + cx] = 1;
            while (!stack.empty()) {
                int idx = stack.back(); stack.pop_back();
                int x = idx % width, y = idx / width;
                const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; d++) {
                    int nx = x + dx[d], ny = y + dy[d];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    int ni = ny * width + nx;
                    if (vis[ni] || isSolid(nx, ny)) continue;
                    vis[ni] = 1; stack.push_back(ni);
                }
            }
            int sx = -1, sy = -1;
            for (int y = 1; y < height - 1 && sx < 0; y++)
                for (int x = 1; x < width - 1; x++)
                    if (!isSolid(x, y) && !vis[y * width + x]) { sx = x; sy = y; break; }
            if (sx < 0) break;  // fully connected
            // March from the sealed tile toward center, knocking out any walls.
            int x = sx, y = sy;
            for (int step = 0; step < width + height; step++) {
                if (vis[y * width + x]) break;  // joined the reachable region
                if (abs(cx - x) >= abs(cy - y)) x += (cx > x) ? 1 : -1;
                else                            y += (cy > y) ? 1 : -1;
                if (x <= 0 || y <= 0 || x >= width - 1 || y >= height - 1) break;
                if (isSolid(x, y)) { set(x, y, TILE_GRASS); ceiling[y * width + x] = CEIL_NONE; }
            }
        }
    }

    findSpawnPoints();
}

bool TileMap::isSolid(int tx, int ty) const {
    if (!isInBounds(tx, ty)) return true;
    int idx = ty * width + tx;
    if (!noCollide.empty() && (size_t)idx < noCollide.size() && noCollide[idx])
        return false;
    uint8_t t = get(tx, ty);
    return t == TILE_WALL || t == TILE_GLASS || t == TILE_DESK || t == TILE_BOX;
}

bool TileMap::isInBounds(int tx, int ty) const {
    if (endless) return true;  // infinite world: every tile exists (computed on demand)
    return tx >= 0 && tx < width && ty >= 0 && ty < height;
}

uint8_t TileMap::get(int tx, int ty) const {
    if (endless) {
        if (!endlessCarved.empty() && endlessCarved.count(tileKey(tx, ty))) return TILE_GRASS;
        return proceduralTile(tx, ty);
    }
    if (!isInBounds(tx, ty)) return TILE_WALL;
    return tiles[ty * width + tx];
}

void TileMap::set(int tx, int ty, uint8_t t) {
    if (endless) return;  // storage-free: terrain is purely a function of the seed
    if (!isInBounds(tx, ty)) return;
    tiles[ty * width + tx] = t;
}

// --- Endless world ---------------------------------------------------------
// A small integer hash; deterministic per (seed, x, y) so the same seed always
// produces the same world and revisited tiles look identical (no storage needed).
static inline uint32_t hashTile(uint32_t seed, int x, int y) {
    uint32_t h = seed * 374761393u + (uint32_t)x * 668265263u + (uint32_t)y * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

void TileMap::beginEndless(uint32_t worldSeed) {
    endless = true;
    seed    = worldSeed ? worldSeed : 1u;
    tiles.clear();
    ceiling.clear();   // empty -> roof overlay safely skips (it checks size)
    noCollide.clear();
    spawnPoints.clear();
    endlessCarved.clear();
    scorched.clear();
    // Large nominal extent so minimap centering / camera bounds give a vast roam
    // area; tiles outside it are still generated on demand by proceduralTile().
    width  = 16384;
    height = 16384;
}

uint8_t TileMap::proceduralCeiling(int tx, int ty) const {
    // Carved (bombed) tiles lose their roof.
    if (!endlessCarved.empty() && endlessCarved.count(tileKey(tx, ty))) return CEIL_NONE;
    // Glass roofs cover exactly the tiled-floor areas (rooms + maze interiors);
    // open ground (grass/gravel) is uncovered.
    return proceduralTile(tx, ty) == TILE_TILEFLOOR ? CEIL_GLASS : CEIL_NONE;
}

void TileMap::scorchArea(Vec2 center, float radius) {
    int x0 = toTile(center.x - radius), x1 = toTile(center.x + radius);
    int y0 = toTile(center.y - radius), y1 = toTile(center.y + radius);
    float r2 = radius * radius;
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            float dx = toWorld(tx) - center.x, dy = toWorld(ty) - center.y;
            if (dx * dx + dy * dy <= r2) scorched.insert(tileKey(tx, ty));
        }
}

void TileMap::carveExplosion(Vec2 center, float radius) {
    int x0 = toTile(center.x - radius), x1 = toTile(center.x + radius);
    int y0 = toTile(center.y - radius), y1 = toTile(center.y + radius);
    float r2 = radius * radius;
    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            float dx = toWorld(tx) - center.x, dy = toWorld(ty) - center.y;
            if (dx * dx + dy * dy > r2) continue;
            scorched.insert(tileKey(tx, ty));   // mark for the burnt/darkened look
            if (endless) {
                uint8_t t = proceduralTile(tx, ty);
                if (t == TILE_WALL || t == TILE_GLASS || t == TILE_DESK)
                    endlessCarved.insert(tileKey(tx, ty));
            } else {
                // Keep the outer border intact so the player can't escape the arena.
                if (tx <= 0 || ty <= 0 || tx >= width - 1 || ty >= height - 1) continue;
                uint8_t t = get(tx, ty);
                if (t == TILE_WALL || t == TILE_GLASS || t == TILE_DESK || t == TILE_BOX)
                    set(tx, ty, TILE_GRASS);
                size_t idx = (size_t)ty * width + tx;
                if (idx < ceiling.size()) ceiling[idx] = CEIL_NONE;
            }
        }
    }
}

// Binary-tree maze: each cell carves a passage either north or east, chosen by a
// hash. This makes a *perfect* (fully-connected) maze that's computable per-tile
// with no global state - ideal for the storage-free endless world.
static inline bool mazeCarveEast(int a, int b, int Cx, uint32_t hb) {
    bool canN = (b > 0), canE = (a < Cx - 1);
    if (!canN && !canE) return false;   // top-right root: carves nothing
    if (!canN) return true;             // top row: must carve east
    if (!canE) return false;            // right column: must carve north
    return (hashTile(hb, a, b) & 1u) != 0;
}
static inline bool mazeCarveNorth(int a, int b, int Cx, uint32_t hb) {
    bool canN = (b > 0), canE = (a < Cx - 1);
    if (!canN && !canE) return false;
    if (!canN) return false;
    if (!canE) return true;
    return (hashTile(hb, a, b) & 1u) == 0;
}
// Is interior cell-grid coord (ix,iy) open? Cells sit at odd coords; walls/pillars
// at even. iw=2*Cx+1, ih=2*Cy+1.
static bool mazeOpen(int ix, int iy, int Cx, int Cy, uint32_t hb) {
    if (ix < 0 || iy < 0 || ix >= 2 * Cx + 1 || iy >= 2 * Cy + 1) return false;
    bool ox = (ix & 1), oy = (iy & 1);
    if (ox && oy)   return true;    // a cell
    if (!ox && !oy) return false;   // a pillar
    if (!ox && oy) {                // vertical wall = east wall of the cell on its left
        int a = ix / 2 - 1, b = (iy - 1) / 2;
        if (a < 0) return false;
        return mazeCarveEast(a, b, Cx, hb);
    }
    // horizontal wall = north wall of the cell below it
    int a = (ix - 1) / 2, b = iy / 2;
    if (b >= Cy) return false;
    return mazeCarveNorth(a, b, Cx, hb);
}
// Tile within a building's local rect [0,bw)x[0,bhgt): outer wall ring + interior
// maze + a single doorway on the bottom edge.
static uint8_t mazeBuildingTile(int lx, int ly, int Cx, int Cy, uint32_t bh) {
    int bw = 2 * Cx + 3, bhgt = 2 * Cy + 3;
    int aEnt   = (int)((bh >> 26) % (uint32_t)std::max(1, Cx));
    int entLx  = 2 * aEnt + 2;       // column of the bottom doorway
    if (lx == entLx && (ly == bhgt - 1 || ly == bhgt - 2)) return TILE_TILEFLOOR; // doorway
    if (lx == 0 || ly == 0 || lx == bw - 1 || ly == bhgt - 1) return TILE_WALL;   // outer wall
    return mazeOpen(lx - 1, ly - 1, Cx, Cy, bh) ? TILE_TILEFLOOR : TILE_WALL;
}

uint8_t TileMap::proceduralTile(int tx, int ty) const {
    // Giant maze buildings on a coarse 96-tile grid: ~16% of cells hold a big-to-
    // GIANT walled maze (perfect binary-tree maze, fully connected, one doorway).
    // The 96 grid is a multiple of the 16 room grid, so a building cell suppresses
    // the small rooms inside it and just leaves open ground around the building.
    const int BC = 96;
    int gx = tx / BC, gy = ty / BC;
    uint32_t bh = hashTile(seed ^ 0xB1D6F00Du, gx, gy);
    bool buildingCell = (bh % 100u) < 16u;
    if (buildingCell) {
        int Cx = 6 + (int)((bh >> 4) % 22u);   // 6..27 cells -> ~15..57 tiles wide
        int Cy = 6 + (int)((bh >> 9) % 22u);
        int bw = 2 * Cx + 3, bhgt = 2 * Cy + 3;
        int gox = gx * BC, goy = gy * BC;
        int bx0 = gox + 2 + (int)((bh >> 14) % (uint32_t)std::max(1, BC - bw - 4));
        int by0 = goy + 2 + (int)((bh >> 20) % (uint32_t)std::max(1, BC - bhgt - 4));
        if (tx >= bx0 && tx < bx0 + bw && ty >= by0 && ty < by0 + bhgt)
            return mazeBuildingTile(tx - bx0, ty - by0, Cx, Cy, bh);
        // Open ground around the building (no small rooms in a building cell).
        uint32_t h = hashTile(seed, tx, ty), gc = hashTile(seed ^ 0xA53C9D1u, tx >> 2, ty >> 2);
        if ((gc % 100u) < 30u && (h % 100u) < 78u) return TILE_GRAVEL;
        return TILE_GRASS;
    }

    // The world is organised into 16x16 cells. ~30% of cells hold a walled room
    // with a tiled floor and a 2-wide doorway; the rest hold open ground with the
    // occasional wall "ruin" for cover. Rooms are kept fully inside their cell
    // with a 1-tile open margin so every doorway opens onto walkable ground -
    // the world is therefore always traversable (no sealed pockets).
    const int CELL = 16;
    int cx = tx >> 4, cy = ty >> 4;       // assumes tx,ty >= 0 (player spawns at a large +offset)
    int ox = cx * CELL, oy = cy * CELL;
    uint32_t rcell = hashTile(seed ^ 0x52004Du, cx, cy);

    if ((rcell % 100u) < 30u) {
        int rw  = 6 + (int)((rcell >> 4) % 4u);    // interior width  6-9
        int rht = 4 + (int)((rcell >> 8) % 3u);    // interior height 4-6
        // Wall ring stays within [ox+2 .. ox+13] so door-adjacent tiles are open.
        int rx0 = ox + 3 + (int)((rcell >> 12) % (uint32_t)std::max(1, 11 - rw));
        int ry0 = oy + 3 + (int)((rcell >> 16) % (uint32_t)std::max(1, 11 - rht));
        if (tx >= rx0 - 1 && tx <= rx0 + rw && ty >= ry0 - 1 && ty <= ry0 + rht) {
            bool perim = (tx == rx0 - 1 || tx == rx0 + rw || ty == ry0 - 1 || ty == ry0 + rht);
            if (!perim) return TILE_TILEFLOOR;     // room interior
            int side = (int)((rcell >> 20) % 4u);  // 2-wide doorway on one side
            int dpos = (int)(rcell >> 22);
            if (side == 0) { int dx = rx0 + 1 + dpos % std::max(1, rw - 2); if (ty == ry0 - 1   && tx >= dx && tx < dx + 2) return TILE_TILEFLOOR; }
            else if (side == 1) { int dx = rx0 + 1 + dpos % std::max(1, rw - 2); if (ty == ry0 + rht && tx >= dx && tx < dx + 2) return TILE_TILEFLOOR; }
            else if (side == 2) { int dy = ry0 + 1 + dpos % std::max(1, rht - 2); if (tx == rx0 - 1   && ty >= dy && ty < dy + 2) return TILE_TILEFLOOR; }
            else                { int dy = ry0 + 1 + dpos % std::max(1, rht - 2); if (tx == rx0 + rw  && ty >= dy && ty < dy + 2) return TILE_TILEFLOOR; }
            return TILE_WALL;                       // room wall (doorframe handled above)
        }
        // tiles outside the room fall through to open terrain
    } else {
        // Sparse wall ruins for cover (cells without a room only).
        uint32_t wc = hashTile(seed ^ 0x51EDu, cx, cy);
        if ((wc % 100u) < 30u) {
            int ax = 4 + (int)((wc >> 8) % 6u);
            int ay = 4 + (int)((wc >> 16) % 6u);
            int dx = (tx & 15) - ax, dy = (ty & 15) - ay;
            int len = 2 + (int)((wc >> 24) % 3u);
            bool horiz = (wc & 0x40u) != 0;
            if (horiz) { if (dy == 0 && dx >= 0 && dx < len) return TILE_WALL; }
            else       { if (dx == 0 && dy >= 0 && dy < len) return TILE_WALL; }
        }
    }

    // Base terrain: gravel patches over grass (both walkable).
    uint32_t h     = hashTile(seed, tx, ty);
    uint32_t gcell = hashTile(seed ^ 0xA53C9D1u, tx >> 2, ty >> 2);
    if ((gcell % 100u) < 30u && (h % 100u) < 78u) return TILE_GRAVEL;
    return TILE_GRASS;
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

bool TileMap::isSpawnSafe(int tx, int ty) const {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (isSolid(tx + dx, ty + dy)) return false;
    return true;
}

void TileMap::findSpawnPoints() {
    spawnPoints.clear();
    // Edges (inside walls) as spawn points - verify tile + neighbors are clear
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
