// The world's edge: the terrain's "continuation" past the ends of the map and
// the boundary wall of the playable area.
//
// Continuation: up to kSkirtExtent tiles past each border (the fog's reach +
// slack), generated at loading from the map itself:
//   - height: starts from the border's height (the nearest point of the map)
//     and, over kSkirtBlend tiles, goes to (average of the borders + value
//     noise in two octaves). Where it meets the map, the height is the border's: no step.
//   - texture block: mostly continues that of the border right "in front",
//     with the copy point meandering along the border (rivers go on as
//     bands that curve); a share of the cells draws a block near the
//     border by lot (skirtTile).
//   - mesh in bands (kSkirtBands): cells of 1 tile up to 8 tiles from the
//     border, of 2 up to 16, of 4 up to 32 and of 16 up to kSkirtExtent (128); far
//     away, the fog covers it. On the lines where two bands meet, the heights
//     of the points in between are interpolated, so as not to open cracks
//     (T-junctions).
//   - only drawn following a target (with fog); casts a shadow in map 0.
//   - groundHeight (camera) uses the same heights, at the same resolution.
//
// Wall: on the map's four borders, from the ground up to kBorderWallHeight
// tiles above, translucent red, with red circles with a white border and a
// hollow X in the middle (one per cell of kBorderXSize tiles), in a pattern
// fixed to the world (along the wall and at the world's height: the wall goes
// up and down with the terrain, the pattern does not). It only
// shows near the camera's target: alpha = 1 - smoothstep(near, far, horizontal
// distance from the target to the wall's point). Drawn after the opaque, with
// blending and without writing depth; it neither casts nor receives a shadow.

#include <cmath>

#include "rz_internal.h"

namespace rz {

namespace {

constexpr int32_t kExtLo  = -kSkirtExtent;                           // -128 (the grids' origin)
constexpr int32_t kExtHi  = kQuadsPerSide + kSkirtExtent + 1;        // 384
constexpr int32_t kExtSide = kExtHi - kExtLo + 1;                    // points per side (513)
// Outline of band i: from bandLo(i) to bandHi(i) on both axes
constexpr int32_t bandLo(int32_t i) { return -kSkirtBands[i].limit; }
constexpr int32_t bandHi(int32_t i) { return kQuadsPerSide + kSkirtBands[i].limit + 1; }
// grids aligned to kExtLo; each band's outline closes on whole cells of
// it and of the outer one; growing steps, each a multiple of the previous
constexpr bool bandsAligned() {
    for (int32_t i = 0; i < kSkirtBandCount; ++i) {
        const int32_t s = kSkirtBands[i].step;
        const int32_t outer = i + 1 < kSkirtBandCount ? kSkirtBands[i + 1].step : s;
        if ((bandLo(i) - kExtLo) % outer != 0 || (bandHi(i) - kExtLo) % outer != 0) return false;
        if (outer % s != 0) return false;
        if (i > 0 && kSkirtBands[i].limit <= kSkirtBands[i - 1].limit) return false;
    }
    return kSkirtBands[0].step == 1;
}
static_assert(bandsAligned(), "the continuation's bands are misaligned");

struct WallVertex {
    float x, y, z;
    float u, v;            // tiles along the wall / above the base
};

uint32_t hash2(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Value noise (smoothed bilinear), 0..1
float valueNoise(float x, float y, int32_t period, uint32_t seed) {
    const float fx = x / float(period), fy = y / float(period);
    const float x0f = floorf(fx), y0f = floorf(fy);
    const int32_t x0 = int32_t(x0f), y0 = int32_t(y0f);
    float tx = fx - x0f, ty = fy - y0f;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    auto at = [&](int32_t ix, int32_t iy) { return float(hash2(ix, iy, seed) & 0xFFFFu) / 65535.0f; };
    const float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

float& ext(RzContext* ctx, int32_t gx, int32_t gz) {
    return ctx->extHeights[size_t(gz + kSkirtExtent) * kExtSide + size_t(gx + kSkirtExtent)];
}

// Heights (unit of the byte, float) from -kSkirtExtent to 255 + kSkirtExtent + 1
void buildExtendedHeights(RzContext* ctx) {
    ctx->extHeights.assign(size_t(kExtSide) * kExtSide, 0.0f);
    const uint8_t* h = ctx->heights.data();

    // Base height: average of the borders
    float sum = 0.0f;
    for (int32_t i = 0; i < kGridSize; ++i) {
        sum += float(h[i]) + float(h[kQuadsPerSide * kGridSize + i])
             + float(h[i * kGridSize]) + float(h[i * kGridSize + kQuadsPerSide]);
    }
    const float base = sum / float(4 * kGridSize);

    for (int32_t gz = kExtLo; gz <= kExtHi; ++gz) {            // kExtHi: the outer corner of the last cell
        for (int32_t gx = kExtLo; gx <= kExtHi; ++gx) {
            const int32_t cx = gx < 0 ? 0 : (gx > kQuadsPerSide ? kQuadsPerSide : gx);
            const int32_t cz = gz < 0 ? 0 : (gz > kQuadsPerSide ? kQuadsPerSide : gz);
            const float edge = float(h[cz * kGridSize + cx]);
            if (cx == gx && cz == gz) {               // inside the map
                ext(ctx, gx, gz) = edge;
                continue;
            }
            const float dx = float(gx - cx), dz = float(gz - cz);
            const float d = sqrtf(dx * dx + dz * dz);
            float w = d / kSkirtBlend;
            if (w > 1.0f) w = 1.0f;
            w = w * w * (3.0f - 2.0f * w);
            const float noise = (valueNoise(float(gx), float(gz), 32, 11u) - 0.5f) * kSkirtNoiseLow
                              + (valueNoise(float(gx), float(gz), 12, 23u) - 0.5f) * kSkirtNoiseHigh;
            float v = edge + (base + noise - edge) * w;
            if (v < 0.0f) v = 0.0f;
            if (v > 255.0f) v = 255.0f;
            ext(ctx, gx, gz) = v;
        }
    }

    // Seams between bands: on the lines of a band's outline (lo..hi), the
    // points between the corners of the outer band's cells (step) lie
    // on the straight line between them (no cracks)
    auto fixLine = [&](bool alongX, int32_t fixed, int32_t lo, int32_t hi, int32_t step) {
        for (int32_t s = lo; s < hi; s += step) {
            const float a = alongX ? ext(ctx, s, fixed) : ext(ctx, fixed, s);
            const float b = alongX ? ext(ctx, s + step, fixed) : ext(ctx, fixed, s + step);
            for (int32_t k = 1; k < step; ++k) {
                const float v = a + (b - a) * float(k) / float(step);
                if (alongX) ext(ctx, s + k, fixed) = v; else ext(ctx, fixed, s + k) = v;
            }
        }
    };
    auto fixRing = [&](int32_t lo, int32_t hi, int32_t step) {
        fixLine(true, lo, lo, hi, step);  fixLine(true, hi, lo, hi, step);
        fixLine(false, lo, lo, hi, step); fixLine(false, hi, lo, hi, step);
    };
    for (int32_t i = 0; i + 1 < kSkirtBandCount; ++i)     // band i / band i+1
        fixRing(bandLo(i), bandHi(i), kSkirtBands[i + 1].step);
}


// A cell (c, r)-(c+s, r+s): two triangles with the map's diagonal
void emitCell(const RzContext* ctx, std::vector<TerrainVertex>& out, const uint32_t* palette,
              int32_t c, int32_t r, int32_t s, uint8_t layer) {
    const float cs = ctx->cellSize, hs = ctx->heightScale;
    struct Corner { int32_t dc, dr; };
    constexpr Corner kCorners[2][3] = { { { 0, 0 }, { 0, 1 }, { 1, 0 } },     // the same as the map's (1 3 2, 2 3 4)
                                        { { 1, 0 }, { 0, 1 }, { 1, 1 } } };
    for (const auto& tri : kCorners) {
        Vec3 p[3];
        float sum = 0.0f;
        for (int32_t k = 0; k < 3; ++k) {
            const int32_t gc = c + tri[k].dc * s, gr = r + tri[k].dr * s;
            const float height = ctx->extHeights[size_t(gr + kSkirtExtent) * kExtSide + size_t(gc + kSkirtExtent)];
            sum += height;
            p[k] = { float(gc) * cs, height * hs, float(gr) * cs };
        }
        const uint32_t normal = packNormal(cross(p[1] - p[0], p[2] - p[0]));
        int32_t idx = int32_t(sum + 0.5f);
        if (idx > kPaletteSize - 1) idx = kPaletteSize - 1;
        const uint32_t color = palette[idx];
        for (int32_t k = 0; k < 3; ++k) {
            out.push_back({ p[k].x, p[k].y, p[k].z, color,
                            uint8_t(tri[k].dc), uint8_t(tri[k].dr), layer, 0, normal });
        }
    }
}

void buildSkirtMesh(RzContext* ctx) {
    uint32_t palette[kPaletteSize];
    buildPalette(palette);

    std::vector<TerrainVertex>& mesh = ctx->skirtStaging;   // reused (allocates only the 1st time)
    mesh.clear();
    mesh.reserve(size_t(60000) * 3);
    // The cell's region: 0 north (r < 0, with the corners), 2 south (r >= 255),
    // 3 west (c < 0), 1 east (c >= 255). Each one is contiguous in the buffer,
    // for drawSkirt to skip those past the fog.
    auto regionOf = [](int32_t c, int32_t r) {
        if (r < 0) return 0;
        if (r >= kQuadsPerSide) return 2;
        return c < 0 ? 3 : 1;
    };
    for (int32_t region = 0; region < 4; ++region) {
        ctx->skirtFirst[region] = int32_t(mesh.size());
        // Each band, less what is inside the previous one (the first: less the map)
        for (int32_t i = 0; i < kSkirtBandCount; ++i) {
            const int32_t step = kSkirtBands[i].step;
            const int32_t lo = i + 1 < kSkirtBandCount ? bandLo(i) : kExtLo;
            const int32_t hi = i + 1 < kSkirtBandCount ? bandHi(i) : kExtHi;
            const int32_t inLo = i > 0 ? bandLo(i - 1) : 0;
            const int32_t inHi = i > 0 ? bandHi(i - 1) : kQuadsPerSide;
            for (int32_t r = lo; r < hi; r += step) {
                for (int32_t c = lo; c < hi; c += step) {
                    if (r >= inLo && r < inHi && c >= inLo && c < inHi) continue;
                    if (regionOf(c, r) != region) continue;
                    emitCell(ctx, mesh, palette, c, r, step, skirtTile(ctx, c, r));
                }
            }
        }
        ctx->skirtCount[region] = int32_t(mesh.size()) - ctx->skirtFirst[region];
    }
    glBindBuffer(GL_ARRAY_BUFFER, ctx->skirtVbo);
    if (ctx->skirtVertexCount == int32_t(mesh.size())) {     // same size: no reallocation
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(mesh.size() * sizeof(TerrainVertex)), mesh.data());
    } else {
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.size() * sizeof(TerrainVertex)), mesh.data(),
                     GL_STATIC_DRAW);
    }
    ctx->skirtVertexCount = int32_t(mesh.size());
}

// Wall: a quad per tile on each of the four sides, from the border's height
// (a little below, not to leave a crack) up to kBorderWallHeight above
void buildWallMesh(RzContext* ctx) {
    const float cs = ctx->cellSize, hs = ctx->heightScale;
    const uint8_t* h = ctx->heights.data();
    std::vector<WallVertex> mesh;
    mesh.reserve(size_t(4) * kQuadsPerSide * 6);
    const float sink = 0.5f * cs, top = kBorderWallHeight * cs;
    // side: point i of the border -> (x, z, height)
    auto point = [&](int32_t side, int32_t i, float* x, float* z, float* y) {
        int32_t gx = 0, gz = 0;
        switch (side) {
            case 0: gx = i;             gz = 0;             break;
            case 1: gx = kQuadsPerSide; gz = i;             break;
            case 2: gx = i;             gz = kQuadsPerSide; break;
            default: gx = 0;            gz = i;             break;
        }
        *x = float(gx) * cs;
        *z = float(gz) * cs;
        *y = float(h[gz * kGridSize + gx]) * hs - sink;
    };
    for (int32_t side = 0; side < 4; ++side) {
        for (int32_t i = 0; i < kQuadsPerSide; ++i) {
            float x0, z0, y0, x1, z1, y1;
            point(side, i, &x0, &z0, &y0);
            point(side, i + 1, &x1, &z1, &y1);
            const float u0 = float(i), u1 = float(i + 1);
            const float vt = (top + sink) / cs;
            const WallVertex a = { x0, y0, z0, u0, 0.0f }, b = { x1, y1, z1, u1, 0.0f };
            const WallVertex c = { x1, y1 + top + sink, z1, u1, vt }, d = { x0, y0 + top + sink, z0, u0, vt };
            mesh.push_back(a); mesh.push_back(b); mesh.push_back(c);
            mesh.push_back(a); mesh.push_back(c); mesh.push_back(d);
        }
    }
    ctx->wallVertexCount = int32_t(mesh.size());
    glBindBuffer(GL_ARRAY_BUFFER, ctx->wallVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.size() * sizeof(WallVertex)), mesh.data(),
                 GL_STATIC_DRAW);
}

} // namespace

// Block of a cell of the continuation. Mostly it continues the block of the
// border's quad that is "in front" of the cell (a river goes on as a river),
// but the point it copies from meanders: along the border, it shifts by a
// smooth value noise in (position along, distance out), with an amplitude that
// grows with the distance (up to kSkirtMeanderMax tiles). Neighbouring cells
// have similar shifts, so the bands stay together and curve instead of going
// in a straight line. With a chance of kSkirtJitterPercent %, the cell
// takes a block drawn by lot from the window near the border (kSkirtTileBand),
// to break the repetition. In the corners (out on both axes), it copies the corner's quad.
uint8_t skirtTile(const RzContext* ctx, int32_t c, int32_t r) {
    if (ctx->tileMap.empty()) return 0;
    const int32_t last = kQuadsPerSide - 1;                  // 254: the last quad
    const int32_t outC = c < 0 ? -c : (c > last ? c - last : 0);
    const int32_t outR = r < 0 ? -r : (r > last ? r - last : 0);
    const uint32_t h = hash2(c, r, 77u);

    int32_t qc, qr;
    if (h % 100u < uint32_t(kSkirtJitterPercent)) {
        // drawing lots in the window near the nearest border point
        const int32_t along  = int32_t((h >> 8) % uint32_t(2 * kSkirtTileBand + 1)) - kSkirtTileBand;
        const int32_t inward = int32_t((h >> 20) % uint32_t(kSkirtTileBand));
        qc = c < 0 ? inward : (c > last ? last - inward : c + along);
        qr = r < 0 ? inward : (r > last ? last - inward : r + along);
    } else {
        // continuation, meandering along the border
        auto meander = [](int32_t along, int32_t out, uint32_t seed) {
            float amp = float(out) * kSkirtMeanderGrowth;
            if (amp > kSkirtMeanderMax) amp = kSkirtMeanderMax;
            const float n = valueNoise(float(along), float(out), kSkirtMeanderPeriod, seed) - 0.5f;
            return int32_t(floorf(n * 2.0f * amp + 0.5f));
        };
        qc = c < 0 ? 0 : (c > last ? last : c);
        qr = r < 0 ? 0 : (r > last ? last : r);
        if (outR > 0 && outC == 0) qc += meander(c, outR, r < 0 ? 31u : 37u);   // north/south
        if (outC > 0 && outR == 0) qr += meander(r, outC, c < 0 ? 41u : 43u);   // west/east
    }
    qc = qc < 0 ? 0 : (qc > last ? last : qc);
    qr = qr < 0 ? 0 : (qr > last ? last : qr);
    return ctx->tileMap[qr * kGridSize + qc];
}

bool createBorder(RzContext* ctx) {
    // Continuation: the same vertex format as the terrain's
    glGenVertexArrays(1, &ctx->skirtVao);
    glGenBuffers(1, &ctx->skirtVbo);
    glBindVertexArray(ctx->skirtVao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->skirtVbo);
    setTerrainVertexLayout();

    // Wall
    glGenVertexArrays(1, &ctx->wallVao);
    glGenBuffers(1, &ctx->wallVbo);
    glBindVertexArray(ctx->wallVao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->wallVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WallVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WallVertex),
                          reinterpret_cast<void*>(offsetof(WallVertex, u)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    WallProgram& w = ctx->wallProgram;
    w.program = linkProgram("wall.vert", "wall.frag");
    if (!w.program) return false;
    w.viewProj   = glGetUniformLocation(w.program, "uViewProj");
    w.target     = glGetUniformLocation(w.program, "uTarget");
    w.fadeNear   = glGetUniformLocation(w.program, "uFadeNear");
    w.fadeFar    = glGetUniformLocation(w.program, "uFadeFar");
    w.xSize      = glGetUniformLocation(w.program, "uXSize");
    w.cellSize   = glGetUniformLocation(w.program, "uCellSize");
    w.top        = glGetUniformLocation(w.program, "uTop");
    w.topFade    = glGetUniformLocation(w.program, "uTopFade");
    initFogUniforms(w.program, w.fog);
    return glGetError() == GL_NO_ERROR;
}

void destroyBorder(RzContext* ctx) {
    if (ctx->skirtVao) glDeleteVertexArrays(1, &ctx->skirtVao);
    if (ctx->skirtVbo) glDeleteBuffers(1, &ctx->skirtVbo);
    if (ctx->wallVao) glDeleteVertexArrays(1, &ctx->wallVao);
    if (ctx->wallVbo) glDeleteBuffers(1, &ctx->wallVbo);
    if (ctx->wallProgram.program) glDeleteProgram(ctx->wallProgram.program);
}

// A point of the map's border changed height (rzUpdateTerrain): only the
// vertices of the continuation on top of it follow (no crack with the map).
// The rest of the outside terrain, the colours and the wall stay as they were
// (accepted: the changes in the game are rare and small; rzSetHeightmap redoes everything).
void moveSkirtEdgeVertex(RzContext* ctx, int32_t gc, int32_t gr) {
    const float x = float(gc) * ctx->cellSize, z = float(gr) * ctx->cellSize;
    const float y = float(ctx->heights[size_t(gr) * kGridSize + size_t(gc)]) * ctx->heightScale;
    std::vector<TerrainVertex>& mesh = ctx->skirtStaging;
    glBindBuffer(GL_ARRAY_BUFFER, ctx->skirtVbo);
    for (size_t i = 0; i < mesh.size(); ++i) {
        if (mesh[i].x != x || mesh[i].z != z) continue;     // the same sum as when building: exact equality
        mesh[i].y = y;
        glBufferSubData(GL_ARRAY_BUFFER, GLintptr(i * sizeof(TerrainVertex)), GLsizeiptr(sizeof(float) * 3),
                        &mesh[i]);
    }
}

// Only the continuation's mesh (blocks near the border changed; rzUpdateTerrain)
void rebuildSkirtMesh(RzContext* ctx) {
    buildSkirtMesh(ctx);
}

// A point of the map (inside) in the extended copy, which equals its height
// (rzUpdateTerrain, a change inside: the outer border does not depend on it)
void setExtendedHeight(RzContext* ctx, int32_t gc, int32_t gr, float height) {
    if (!ctx->extHeights.empty()) ext(ctx, gc, gr) = height;
}

// Loading (called at the end of buildTerrainMesh): extended heights, the
// continuation's and the wall's meshes
void buildBorder(RzContext* ctx) {
    buildExtendedHeights(ctx);
    buildSkirtMesh(ctx);
    buildWallMesh(ctx);
}

// Extended height (world) at (x, z), bilinear at the mesh's resolution
// (the step of the band the point is in; 1 tile in the map)
float extendedGroundHeight(const RzContext* ctx, float x, float z) {
    const float invCell = 1.0f / ctx->cellSize;
    float gx = x * invCell, gz = z * invCell;
    const float lo = float(-kSkirtExtent), hi = float(kQuadsPerSide + kSkirtExtent) - 0.001f;
    gx = fminf(fmaxf(gx, lo), hi);
    gz = fminf(fmaxf(gz, lo), hi);
    auto inside = [&](int32_t a, int32_t b) { return gx >= float(a) && gx < float(b) && gz >= float(a) && gz < float(b); };
    float step = float(kSkirtBands[kSkirtBandCount - 1].step);
    for (int32_t i = kSkirtBandCount - 2; i >= 0; --i)
        if (inside(bandLo(i), bandHi(i))) step = float(kSkirtBands[i].step);
    const float ox = float(kExtLo);                            // grids aligned to kExtLo
    const float cx = floorf((gx - ox) / step) * step + ox;
    const float cz = floorf((gz - ox) / step) * step + ox;
    const float fx = (gx - cx) / step, fz = (gz - cz) / step;
    auto at = [&](float px, float pz) {
        return ctx->extHeights[size_t(int32_t(pz) + kSkirtExtent) * kExtSide + size_t(int32_t(px) + kSkirtExtent)];
    };
    const float a = at(cx, cz), b = at(cx + step, cz), c = at(cx, cz + step), d = at(cx + step, cz + step);
    const float top = a + (b - a) * fx, bot = c + (d - c) * fx;
    return (top + (bot - top) * fz) * ctx->heightScale;
}

// Continuation, in the regions (0 north, 1 east, 2 south, 3 west) that fall
// within matrix (projection · view); withFog: also only those that start before
// the end of the fog (in the middle of the map, none). Program and uniforms already set.
void drawSkirt(const RzContext* ctx, const Mat4& matrix, bool withFog) {
    if (ctx->skirtVertexCount == 0) return;
    const float cs = ctx->cellSize;
    const float lo = float(-kSkirtExtent) * cs, hi = float(kQuadsPerSide + kSkirtExtent) * cs;
    const float m0 = 0.0f, m1 = float(kQuadsPerSide) * cs;
    const float top = 255.0f * ctx->heightScale;      // outside heights: 0..255 (buildExtendedHeights)
    const Vec3 box[4][2] = { { { lo, 0.0f, lo }, { hi, top, m0 } }, { { m1, 0.0f, m0 }, { hi, top, m1 } },
                             { { lo, 0.0f, m1 }, { hi, top, hi } }, { { lo, 0.0f, m0 }, { m0, top, m1 } } };
    const Frustum f = frustumOf(matrix);
    const float reach = ctx->fogFar;
    const Vec3 o = ctx->fogOrigin;
    glBindVertexArray(ctx->skirtVao);
    for (int32_t i = 0; i < 4; ++i) {
        if (ctx->skirtCount[i] == 0 || !boxInFrustum(f, box[i][0], box[i][1])) continue;
        if (withFog) {                               // horizontally
            const float dx = fmaxf(fmaxf(box[i][0].x - o.x, 0.0f), o.x - box[i][1].x);
            const float dz = fmaxf(fmaxf(box[i][0].z - o.z, 0.0f), o.z - box[i][1].z);
            if (dx * dx + dz * dz > reach * reach) continue;
        }
        glDrawArrays(GL_TRIANGLES, ctx->skirtFirst[i], ctx->skirtCount[i]);
    }
}

// Wall: after everything opaque
void drawWall(const RzContext* ctx, const Mat4& viewProj) {
    if (!ctx->wallOn || !ctx->hasTerrain() || ctx->wallVertexCount == 0) return;
    const WallProgram& w = ctx->wallProgram;
    glUseProgram(w.program);
    setMatrix(w.viewProj, viewProj);
    glUniform3f(w.target, ctx->targetPos.x, ctx->targetPos.y, ctx->targetPos.z);
    glUniform1f(w.fadeNear, kBorderFadeNear * ctx->cellSize);
    glUniform1f(w.fadeFar, kBorderFadeFar * ctx->cellSize);
    glUniform1f(w.xSize, kBorderXSize);
    glUniform1f(w.cellSize, ctx->cellSize);
    glUniform1f(w.top, kBorderWallHeight + 0.5f);      // v of the top: height + what sinks (buildWallMesh)
    glUniform1f(w.topFade, kBorderTopFade);
    bindFog(ctx, w.fog);

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);   // the destination's alpha = reserved byte
    glDepthMask(GL_FALSE);
    glBindVertexArray(ctx->wallVao);
    glDrawArrays(GL_TRIANGLES, 0, ctx->wallVertexCount);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace rz
