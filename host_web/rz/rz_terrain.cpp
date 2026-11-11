// Data derived from the terrain, worked out on the CPU at loading: a mesh with
// lit flat colours, a palette by height, the atlas's mipmap levels and the bounding sphere.

#include <cmath>
#include <vector>

#include "rz_internal.h"

namespace rz {

namespace {

struct Rgb {
    float r, g, b;
};

struct PaletteBand {
    float lo, hi;       // band of average height, on the byte's scale (inclusive)
    Rgb   from, to;     // gradient within the band
};

// Provisional values (spec 7.2).
constexpr PaletteBand kBands[] = {
    {   0.0f,  40.0f, {  22.0f,  52.0f, 120.0f }, {  45.0f,  95.0f, 170.0f } },  // water
    {  41.0f,  60.0f, { 194.0f, 178.0f, 128.0f }, { 212.0f, 196.0f, 146.0f } },  // sand
    {  61.0f, 150.0f, {  78.0f, 148.0f,  56.0f }, {  38.0f,  98.0f,  34.0f } },  // grass
    { 151.0f, 210.0f, { 118.0f, 104.0f,  88.0f }, { 152.0f, 142.0f, 132.0f } },  // rock
    { 211.0f, 255.0f, { 228.0f, 228.0f, 234.0f }, { 255.0f, 255.0f, 255.0f } },  // snow
};

uint32_t packColor(float r, float g, float b) {
    auto channel = [](float v) -> uint32_t {
        if (v <= 0.0f) return 0;
        if (v >= 255.0f) return 255;
        return uint32_t(v + 0.5f);
    };
    return (channel(r) << 16) | (channel(g) << 8) | channel(b);   // reserved byte = 0
}

} // namespace

// Transparent texels (alpha 0) get the colour of their opaque neighbours (two
// dilation passes, 4-neighbours; whatever is left, the opaque ones' average), so
// that the filtering (blending between levels) does not pull in the colour of
// index 255 nor black at the holes' edges. The alpha does not change. A square image.
void fillTransparent(uint32_t* img, int32_t side) {
    uint64_t sum[3] = { 0, 0, 0 };
    uint32_t opaque = 0;
    for (int32_t i = 0; i < side * side; ++i) {
        if ((img[i] >> 24) == 0) continue;
        ++opaque;
        for (int32_t c = 0; c < 3; ++c) sum[c] += (img[i] >> (16 - 8 * c)) & 0xFF;
    }
    if (opaque == 0 || opaque == uint32_t(side * side)) return;   // all transparent or none
    uint32_t average = 0;
    for (int32_t c = 0; c < 3; ++c) average |= uint32_t((sum[c] + opaque / 2) / opaque) << (16 - 8 * c);

    // marks the texels with a colour already: bit 24 of the texel itself will not do
    // (alpha), so it uses a pass that only reads from those that had a colour before it
    std::vector<uint8_t> has(size_t(side) * size_t(side));
    for (int32_t i = 0; i < side * side; ++i) has[size_t(i)] = (img[i] >> 24) != 0;
    std::vector<uint8_t> next;
    for (int32_t pass = 0; pass < 2; ++pass) {
        next = has;
        for (int32_t y = 0; y < side; ++y) {
            for (int32_t x = 0; x < side; ++x) {
                const int32_t i = y * side + x;
                if (has[size_t(i)]) continue;
                uint32_t s[3] = { 0, 0, 0 }, n = 0;
                const int32_t nb[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
                for (const auto& q : nb) {
                    if (q[0] < 0 || q[1] < 0 || q[0] >= side || q[1] >= side) continue;
                    const int32_t j = q[1] * side + q[0];
                    if (!has[size_t(j)]) continue;
                    for (int32_t c = 0; c < 3; ++c) s[c] += (img[j] >> (16 - 8 * c)) & 0xFF;
                    ++n;
                }
                if (n == 0) continue;
                uint32_t color = 0;
                for (int32_t c = 0; c < 3; ++c) color |= ((s[c] + n / 2) / n) << (16 - 8 * c);
                img[i] = (img[i] & 0xFF000000u) | color;
                next[size_t(i)] = 1;
            }
        }
        has.swap(next);
    }
    for (int32_t i = 0; i < side * side; ++i) {
        if (!has[size_t(i)]) img[i] = (img[i] & 0xFF000000u) | average;
    }
}

// Level L+1 of a square image from level L (0xAARRGGBB): alpha =
// 2x2 average; colour = average of the opaque texels only, weighted by alpha
// (transparent texels do not pull the colour), rounded. Used in the atlas and in the
// objects' textures.
void downsample(const uint32_t* src, uint32_t* dst, int32_t dstSide) {
    const int32_t srcSide = dstSide * 2;
    for (int32_t y = 0; y < dstSide; ++y) {
        for (int32_t x = 0; x < dstSide; ++x) {
            const uint32_t p[4] = { src[(2 * y) * srcSide + 2 * x],     src[(2 * y) * srcSide + 2 * x + 1],
                                    src[(2 * y + 1) * srcSide + 2 * x], src[(2 * y + 1) * srcSide + 2 * x + 1] };
            uint32_t alphaSum = 0, sum[3] = { 0, 0, 0 };
            for (const uint32_t q : p) {
                const uint32_t a = q >> 24;
                alphaSum += a;
                for (int32_t c = 0; c < 3; ++c) sum[c] += ((q >> (16 - 8 * c)) & 0xFF) * a;
            }
            uint32_t out = ((alphaSum + 2) >> 2) << 24;
            if (alphaSum > 0) {
                for (int32_t c = 0; c < 3; ++c) out |= ((sum[c] + alphaSum / 2) / alphaSum) << (16 - 8 * c);
            }
            dst[y * dstSide + x] = out;
        }
    }
}

// Direction (normalised) to the light, fixed in the world. Provisional.
Vec3 lightDirection() {
    Vec3 l = { -0.45f, 0.45f, -0.40f };   // elevation :)
    const float inv = 1.0f / sqrtf(dot(l, l));
    return { l.x * inv, l.y * inv, l.z * inv };
}

uint32_t packNormal(Vec3 n) {
    const float len2 = dot(n, n);
    if (!(len2 > 1.0e-20f)) n = { 0.0f, -1.0f, 0.0f };
    else {
        const float inv = 1.0f / sqrtf(len2);
        n = { n.x * inv, n.y * inv, n.z * inv };
    }
    auto q = [](float v) -> uint32_t {
        int32_t i = int32_t(lrintf(v * 511.0f));
        if (i > 511) i = 511;
        if (i < -511) i = -511;
        return uint32_t(i) & 0x3FFu;
    };
    return q(n.x) | (q(n.y) << 10) | (q(n.z) << 20);
}

void setTerrainVertexLayout() {
    const GLsizei stride = sizeof(TerrainVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(TerrainVertex, color)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(TerrainVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_INT_2_10_10_10_REV, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
    glEnableVertexAttribArray(3);
}

// Palette indexed by the sum of a triangle's 3 heights (0..765).
void buildPalette(uint32_t* palette) {
    constexpr float third = 1.0f / 3.0f;
    for (int sum = 0; sum < kPaletteSize; ++sum) {
        const float avg = float(sum) * third;
        const PaletteBand* band = &kBands[0];
        for (const PaletteBand& b : kBands) {
            if (avg >= b.lo - 0.5f) band = &b;    // bands in increasing order
        }
        float t = (avg - band->lo) / (band->hi - band->lo);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        palette[sum] = packColor(band->from.r + (band->to.r - band->from.r) * t,
                                 band->from.g + (band->to.g - band->from.g) * t,
                                 band->from.b + (band->to.b - band->from.b) * t);
    }
}

// The terrain's mesh (spec 7.1), a vertex per triangle corner:
// the legacy's diagonal, (c+1, r)–(c, r+1). Corners  1 2  ->  A = 1 3 2, B = 2 3 4:
//                                                    3 4
//   A = (c, r), (c, r+1), (c+1, r)      B = (c+1, r), (c, r+1), (c+1, r+1)
// Flat colour: palette by the sum of the 3 heights, lit by the face's normal.
// u along the column, v along the row; the quad's block from the block map.
// Order in the VBO: in chunks of kChunkQuads x kChunkQuads quads (row-major),
// each chunk contiguous and with its quads row-major, 6 vertices per quad; the
// culling (drawTerrain) draws only the chunks that show. The whole mesh is
// also kept on the CPU (ctx->terrainStaging, in the same order), for
// rzUpdateTerrain to redo only a rectangle of quads and upload only its stretches.
namespace {

int32_t chunkWidth(int32_t chunk) {          // quads of the chunk on an axis (the last one is smaller)
    const int32_t rest = kQuadsPerSide - chunk * kChunkQuads;
    return rest < kChunkQuads ? rest : kChunkQuads;
}

// First vertex of quad (c, r) in the VBO
size_t quadVertex(const RzContext* ctx, int32_t c, int32_t r) {
    const int32_t cx = c / kChunkQuads, cz = r / kChunkQuads;
    const RzContext::TerrainChunk& k = ctx->terrainChunks[cz * kChunksPerSide + cx];
    return size_t(k.first) + size_t((r % kChunkQuads) * chunkWidth(cx) + c % kChunkQuads) * 6;
}

// Where each chunk starts in the VBO (fixed: only depends on the map's size)
void layoutChunks(RzContext* ctx) {
    int32_t first = 0;
    for (int32_t cz = 0; cz < kChunksPerSide; ++cz) {
        for (int32_t cx = 0; cx < kChunksPerSide; ++cx) {
            RzContext::TerrainChunk& k = ctx->terrainChunks[cz * kChunksPerSide + cx];
            k.first = first;
            k.count = chunkWidth(cx) * chunkWidth(cz) * 6;
            first += k.count;
        }
    }
}

// Range of heights (world) of the chunk's points, edges included
void chunkBounds(RzContext* ctx, int32_t cx, int32_t cz) {
    const uint8_t* h = ctx->heights.data();
    uint8_t lo = 255, hi = 0;
    for (int32_t r = cz * kChunkQuads; r <= cz * kChunkQuads + chunkWidth(cz); ++r) {
        for (int32_t c = cx * kChunkQuads; c <= cx * kChunkQuads + chunkWidth(cx); ++c) {
            const uint8_t v = h[r * kGridSize + c];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    RzContext::TerrainChunk& k = ctx->terrainChunks[cz * kChunksPerSide + cx];
    k.yMin = float(lo) * ctx->heightScale;
    k.yMax = float(hi) * ctx->heightScale;
}

// Quads c0..c1 x r0..r1 (inclusive) -> staging
void buildQuads(RzContext* ctx, int32_t c0, int32_t r0, int32_t c1, int32_t r1) {
    uint32_t palette[kPaletteSize];
    buildPalette(palette);
    const uint8_t* h = ctx->heights.data();
    const float cs = ctx->cellSize;
    const float hs = ctx->heightScale;

    struct Corner { int32_t dc, dr; };
    constexpr Corner kCorners[2][3] = { { { 0, 0 }, { 0, 1 }, { 1, 0 } },     // A = 1 3 2
                                        { { 1, 0 }, { 0, 1 }, { 1, 1 } } };   // B = 2 3 4
    for (int32_t r = r0; r <= r1; ++r) {
        for (int32_t c = c0; c <= c1; ++c) {
            TerrainVertex* v = ctx->terrainStaging.data() + quadVertex(ctx, c, r);
            const uint8_t layer = ctx->tileMap.empty() ? 0 : ctx->tileMap[r * kGridSize + c];
            for (const auto& tri : kCorners) {
                Vec3 p[3];
                int32_t sum = 0;
                for (int32_t k = 0; k < 3; ++k) {
                    const int32_t gc = c + tri[k].dc, gr = r + tri[k].dr;
                    const int32_t height = h[gr * kGridSize + gc];
                    sum += height;
                    p[k] = { float(gc) * cs, float(height) * hs, float(gr) * cs };
                }
                const uint32_t normal = packNormal(cross(p[1] - p[0], p[2] - p[0]));
                const uint32_t color = palette[sum];
                for (int32_t k = 0; k < 3; ++k) {
                    *v++ = { p[k].x, p[k].y, p[k].z, color,
                             uint8_t(tri[k].dc), uint8_t(tri[k].dr), layer, 0, normal };
                }
            }
        }
    }
}

// Uploads quads c0..c1 x r0..r1 (inclusive) again: a stretch per row and chunk
void uploadQuads(RzContext* ctx, int32_t c0, int32_t r0, int32_t c1, int32_t r1) {
    glBindBuffer(GL_ARRAY_BUFFER, ctx->terrainVbo);
    for (int32_t r = r0; r <= r1; ++r) {
        for (int32_t c = c0; c <= c1;) {
            const int32_t end = (c / kChunkQuads + 1) * kChunkQuads - 1;   // the chunk's last quad
            const int32_t last = end < c1 ? end : c1;
            const size_t first = quadVertex(ctx, c, r);
            glBufferSubData(GL_ARRAY_BUFFER, GLintptr(first * sizeof(TerrainVertex)),
                            GLsizeiptr(size_t(last - c + 1) * 6 * sizeof(TerrainVertex)),
                            ctx->terrainStaging.data() + first);
            c = last + 1;
        }
    }
}

// The whole mesh
void uploadAll(RzContext* ctx) {
    glBindBuffer(GL_ARRAY_BUFFER, ctx->terrainVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(ctx->terrainStaging.size() * sizeof(TerrainVertex)),
                    ctx->terrainStaging.data());
}

constexpr int32_t kLastQuad = kQuadsPerSide - 1;   // 254

} // namespace

// The whole mesh + border; the whole terrain's shadow map redone.
// rzSetHeightmap and rzSetTerrainScale (loading).
bool buildTerrainMesh(RzContext* ctx) {
    ctx->terrainStaging.resize(size_t(kTriangleCount) * 3);   // loading: allocates the first time
    layoutChunks(ctx);
    for (int32_t cz = 0; cz < kChunksPerSide; ++cz)
        for (int32_t cx = 0; cx < kChunksPerSide; ++cx) chunkBounds(ctx, cx, cz);
    buildQuads(ctx, 0, 0, kLastQuad, kLastQuad);
    uploadAll(ctx);
    buildBorder(ctx);                        // continuation and wall depend on the heights/blocks
    markTerrainShadowAll(ctx);
    return glGetError() == GL_NO_ERROR;
}

// Only the blocks changed (rzSetTileMap): the quads' and the continuation's
// blocks; the heights, the relief's border and the shadow stay.
bool rebuildTerrainTiles(RzContext* ctx) {
    ctx->terrainStaging.resize(size_t(kTriangleCount) * 3);
    layoutChunks(ctx);
    buildQuads(ctx, 0, 0, kLastQuad, kLastQuad);
    uploadAll(ctx);
    rebuildSkirtMesh(ctx);
    return glGetError() == GL_NO_ERROR;
}

// rzUpdateTerrain: reads the host's buffers again (the pointers kept), compares
// with the copies and, for each point/quad that changed (few in the game):
//   - height: redoes and uploads the 4 quads around the point, updates the
//     extended copy (camera) and the height range of the point's chunks (the
//     far shadow is not redone: see rz_shadow.cpp); if the point is on the
//     map's border, it only moves along the y of the outside terrain's
//     vertices on top of it (moveSkirtEdgeVertex;
//     the rest outside is not recalculated: accepted, rzSetHeightmap redoes everything);
//   - block: redoes and uploads the quad; if it is less than kSkirtTileBand quads
//     from the border, redoes the continuation's mesh (it copies/draws from that band).
// Without changes, it does nothing. Does not allocate.
bool updateTerrain(RzContext* ctx) {
    auto refreshQuads = [&](int32_t c0, int32_t r0, int32_t c1, int32_t r1) {   // inclusive, clipped
        if (c0 < 0) c0 = 0;
        if (r0 < 0) r0 = 0;
        if (c1 > kLastQuad) c1 = kLastQuad;
        if (r1 > kLastQuad) r1 = kLastQuad;
        buildQuads(ctx, c0, r0, c1, r1);
        uploadQuads(ctx, c0, r0, c1, r1);
    };

    if (ctx->heightSource) {
        const uint8_t* src = ctx->heightSource;
        uint8_t* h = ctx->heights.data();
        for (int32_t i = 0; i < kVertexCount; ++i) {
            if (src[i] == h[i]) continue;
            const int32_t gc = i % kGridSize, gr = i / kGridSize;
            h[i] = src[i];
            setExtendedHeight(ctx, gc, gr, float(src[i]));
            refreshQuads(gc - 1, gr - 1, gc, gr);
            // height range of the point's chunks (up to 4, on the split)
            for (int32_t cz = (gr - 1) / kChunkQuads; cz <= gr / kChunkQuads; ++cz)
                for (int32_t cx = (gc - 1) / kChunkQuads; cx <= gc / kChunkQuads; ++cx)
                    if (cx >= 0 && cz >= 0 && cx < kChunksPerSide && cz < kChunksPerSide) chunkBounds(ctx, cx, cz);
            if (gc == 0 || gr == 0 || gc == kQuadsPerSide || gr == kQuadsPerSide) moveSkirtEdgeVertex(ctx, gc, gr);
        }
    }
    if (ctx->tileSource && ctx->hasTileMap) {
        const uint8_t* src = ctx->tileSource;
        uint8_t* t = ctx->tileMap.data();
        bool edgeTile = false;
        for (int32_t r = 0; r < kQuadsPerSide; ++r) {
            for (int32_t c = 0; c < kQuadsPerSide; ++c) {
                const int32_t i = r * kGridSize + c;
                if (src[i] == t[i]) continue;
                t[i] = src[i];
                refreshQuads(c, r, c, r);
                if (c < kSkirtTileBand || r < kSkirtTileBand ||
                    c > kLastQuad - kSkirtTileBand || r > kLastQuad - kSkirtTileBand) edgeTile = true;
            }
        }
        // ~3 ms (llvmpipe). A possible future optimisation: only the outside triangles
        // that copy from this quad (see spec 12 and the super_complicated_border_update branch)
        if (edgeTile) rebuildSkirtMesh(ctx);
    }
    return true;                // no glGetError: called in the game, and in WebGL it waits for the GPU
}

void drawTerrain(const RzContext* ctx, const Mat4& matrix, bool withFog) {
    const Frustum f = frustumOf(matrix);
    const float cs = ctx->cellSize;
    const float reach = ctx->fogFar;
    const Vec3 o = ctx->fogOrigin;
    glBindVertexArray(ctx->terrainVao);
    int32_t runFirst = 0, runCount = 0;          // chunks one after the other in the VBO: a single upload
    for (int32_t cz = 0; cz < kChunksPerSide; ++cz) {
        for (int32_t cx = 0; cx < kChunksPerSide; ++cx) {
            const RzContext::TerrainChunk& k = ctx->terrainChunks[cz * kChunksPerSide + cx];
            const Vec3 lo = { float(cx * kChunkQuads) * cs, k.yMin, float(cz * kChunkQuads) * cs };
            const Vec3 hi = { float(cx * kChunkQuads + chunkWidth(cx)) * cs, k.yMax,
                              float(cz * kChunkQuads + chunkWidth(cz)) * cs };
            bool visible = boxInFrustum(f, lo, hi);
            if (visible && withFog) {            // the box's point nearest to the fog's origin
                const float dx = fmaxf(fmaxf(lo.x - o.x, 0.0f), o.x - hi.x);
                const float dy = fmaxf(fmaxf(lo.y - o.y, 0.0f), o.y - hi.y);
                const float dz = fmaxf(fmaxf(lo.z - o.z, 0.0f), o.z - hi.z);
                visible = dx * dx + dy * dy + dz * dz <= reach * reach;
            }
            if (!visible) continue;
            if (runCount > 0 && runFirst + runCount == k.first) {
                runCount += k.count;
                continue;
            }
            if (runCount > 0) glDrawArrays(GL_TRIANGLES, runFirst, runCount);
            runFirst = k.first;
            runCount = k.count;
        }
    }
    if (runCount > 0) glDrawArrays(GL_TRIANGLES, runFirst, runCount);
}

// Bounding sphere: radius = half the diagonal of the terrain's box. Used by the
// overview and by the near/far planes.
void updateTerrainBounds(RzContext* ctx) {
    const float hx = 127.5f * ctx->cellSize;
    const float hy = 127.5f * ctx->heightScale;
    ctx->terrainRadius = sqrtf(hx * hx + hy * hy + hx * hx);
}

// Paletted 256x256 atlas (the PCX's corner) -> 256 blocks of 16x16 in RGBA (rgbaTexel)
// with the 5 mipmap levels, arranged by level to go up with
// glTexImage3D: level L starts at tiles + 256 · kLevelOffset[L].
void buildAtlasLevels(uint32_t* tiles, const uint8_t* indices, const uint8_t* paletteRGB) {
    constexpr int32_t kLevelOffset[kMipLevels] = { 0, 256, 320, 336, 340 };   // texels per block
    constexpr int32_t kTilesPerRow = kAtlasSize / kTileSize;

    uint32_t pal[256];
    for (int i = 0; i < 256; ++i) {
        const uint32_t rgb = (uint32_t(paletteRGB[i * 3 + 0]) << 16) | (uint32_t(paletteRGB[i * 3 + 1]) << 8)
                           | uint32_t(paletteRGB[i * 3 + 2]);
        pal[i] = rgbaTexel(rgb, i == kTransparentIndex ? 0 : 255);   // 255: transparent
    }

    for (int32_t t = 0; t < kMaxTiles; ++t) {
        uint32_t* level0 = tiles + t * 256;
        const int32_t tx = (t % kTilesPerRow) * kTileSize;
        const int32_t ty = (t / kTilesPerRow) * kTileSize;
        for (int32_t y = 0; y < kTileSize; ++y) {
            const uint8_t* src = indices + (ty + y) * kAtlasSize + tx;
            for (int32_t x = 0; x < kTileSize; ++x) level0[y * kTileSize + x] = pal[src[x]];
        }
        fillTransparent(level0, kTileSize);
        for (int32_t level = 1; level < kMipLevels; ++level) {
            const int32_t side = kTileSize >> level;
            const int32_t prevSide = side * 2;
            const uint32_t* prev = tiles + kMaxTiles * kLevelOffset[level - 1] + t * prevSide * prevSide;
            uint32_t* cur = tiles + kMaxTiles * kLevelOffset[level] + t * side * side;
            downsample(prev, cur, side);
            fillTransparent(cur, side);
        }
    }
}

} // namespace rz
