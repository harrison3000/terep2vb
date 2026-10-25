// Dados derivados do terreno calculados na CPU, na carga: malha com cores flat
// iluminadas, paleta pela altura, níveis de mipmap do atlas e esfera envolvente.

#include <cmath>
#include <vector>

#include "rz_internal.h"

namespace rz {

namespace {

struct Rgb {
    float r, g, b;
};

struct PaletteBand {
    float lo, hi;       // faixa de altura média, na escala do byte (inclusiva)
    Rgb   from, to;     // gradiente dentro da faixa
};

// Valores provisórios (spec 7.2).
constexpr PaletteBand kBands[] = {
    {   0.0f,  40.0f, {  22.0f,  52.0f, 120.0f }, {  45.0f,  95.0f, 170.0f } },  // água
    {  41.0f,  60.0f, { 194.0f, 178.0f, 128.0f }, { 212.0f, 196.0f, 146.0f } },  // areia
    {  61.0f, 150.0f, {  78.0f, 148.0f,  56.0f }, {  38.0f,  98.0f,  34.0f } },  // grama
    { 151.0f, 210.0f, { 118.0f, 104.0f,  88.0f }, { 152.0f, 142.0f, 132.0f } },  // rocha
    { 211.0f, 255.0f, { 228.0f, 228.0f, 234.0f }, { 255.0f, 255.0f, 255.0f } },  // neve
};

uint32_t packColor(float r, float g, float b) {
    auto channel = [](float v) -> uint32_t {
        if (v <= 0.0f) return 0;
        if (v >= 255.0f) return 255;
        return uint32_t(v + 0.5f);
    };
    return (channel(r) << 16) | (channel(g) << 8) | channel(b);   // byte reservado = 0
}

} // namespace

// Texels transparentes (alfa 0) recebem a cor dos opacos vizinhos (duas
// passadas de dilatação, 4-vizinhos; o que sobrar, a média dos opacos), para
// a filtragem (mistura entre níveis) não puxar a cor do índice
// 255 nem preto nas bordas dos buracos. O alfa não muda. Imagem quadrada.
void fillTransparent(uint32_t* img, int32_t side) {
    uint64_t sum[3] = { 0, 0, 0 };
    uint32_t opaque = 0;
    for (int32_t i = 0; i < side * side; ++i) {
        if ((img[i] >> 24) == 0) continue;
        ++opaque;
        for (int32_t c = 0; c < 3; ++c) sum[c] += (img[i] >> (16 - 8 * c)) & 0xFF;
    }
    if (opaque == 0 || opaque == uint32_t(side * side)) return;   // tudo transparente ou nada
    uint32_t average = 0;
    for (int32_t c = 0; c < 3; ++c) average |= uint32_t((sum[c] + opaque / 2) / opaque) << (16 - 8 * c);

    // marca dos texels já com cor: bit 24 do próprio texel não serve (alfa), usa
    // uma passada que só lê de quem tinha cor antes dela
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

// Nível L+1 de uma imagem quadrada a partir do nível L (0xAARRGGBB): alfa =
// média 2x2; cor = média só dos texels opacos, ponderada pelo alfa (texels
// transparentes não puxam a cor), arredondada. Usado no atlas e nas texturas
// dos objetos.
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

// Direção (normalizada) para a luz, fixa no mundo. Provisória.
Vec3 lightDirection() {
    Vec3 l = { -0.45f, 0.45f, -0.40f };   // elevação :)
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

// Paleta indexada pela soma das 3 alturas de um triângulo (0..765).
void buildPalette(uint32_t* palette) {
    constexpr float third = 1.0f / 3.0f;
    for (int sum = 0; sum < kPaletteSize; ++sum) {
        const float avg = float(sum) * third;
        const PaletteBand* band = &kBands[0];
        for (const PaletteBand& b : kBands) {
            if (avg >= b.lo - 0.5f) band = &b;    // bandas em ordem crescente
        }
        float t = (avg - band->lo) / (band->hi - band->lo);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        palette[sum] = packColor(band->from.r + (band->to.r - band->from.r) * t,
                                 band->from.g + (band->to.g - band->from.g) * t,
                                 band->from.b + (band->to.b - band->from.b) * t);
    }
}

// Malha do terreno (spec 7.1), um vértice por canto de triângulo:
// diagonal do legado, (c+1, r)–(c, r+1). Cantos  1 2  ->  A = 1 3 2, B = 2 3 4:
//                                                  3 4
//   A = (c, r), (c, r+1), (c+1, r)      B = (c+1, r), (c, r+1), (c+1, r+1)
// Cor flat: paleta pela soma das 3 alturas, iluminada pela normal da face.
// u ao longo da coluna, v ao longo da linha; bloco do quad pelo mapa de blocos.
// Ordem no VBO: em blocos de kChunkQuads x kChunkQuads quads (row-major),
// cada bloco contíguo e com os quads dele row-major, 6 vértices por quad; o
// recorte (drawTerrain) desenha só os blocos que aparecem. A malha inteira
// fica também na CPU (ctx->terrainStaging, na mesma ordem), para
// rzUpdateTerrain refazer só um retângulo de quads e reenviar só os trechos dele.
namespace {

int32_t chunkWidth(int32_t chunk) {          // quads do bloco num eixo (o último é menor)
    const int32_t rest = kQuadsPerSide - chunk * kChunkQuads;
    return rest < kChunkQuads ? rest : kChunkQuads;
}

// Primeiro vértice do quad (c, r) no VBO
size_t quadVertex(const RzContext* ctx, int32_t c, int32_t r) {
    const int32_t cx = c / kChunkQuads, cz = r / kChunkQuads;
    const RzContext::TerrainChunk& k = ctx->terrainChunks[cz * kChunksPerSide + cx];
    return size_t(k.first) + size_t((r % kChunkQuads) * chunkWidth(cx) + c % kChunkQuads) * 6;
}

// Onde cada bloco começa no VBO (fixo: só depende do tamanho do mapa)
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

// Faixa de alturas (mundo) dos pontos do bloco, bordas incluídas
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

// Quads c0..c1 x r0..r1 (inclusivos) -> staging
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

// Reenvia os quads c0..c1 x r0..r1 (inclusivos): um trecho por linha e bloco
void uploadQuads(RzContext* ctx, int32_t c0, int32_t r0, int32_t c1, int32_t r1) {
    glBindBuffer(GL_ARRAY_BUFFER, ctx->terrainVbo);
    for (int32_t r = r0; r <= r1; ++r) {
        for (int32_t c = c0; c <= c1;) {
            const int32_t end = (c / kChunkQuads + 1) * kChunkQuads - 1;   // último quad do bloco
            const int32_t last = end < c1 ? end : c1;
            const size_t first = quadVertex(ctx, c, r);
            glBufferSubData(GL_ARRAY_BUFFER, GLintptr(first * sizeof(TerrainVertex)),
                            GLsizeiptr(size_t(last - c + 1) * 6 * sizeof(TerrainVertex)),
                            ctx->terrainStaging.data() + first);
            c = last + 1;
        }
    }
}

// A malha inteira
void uploadAll(RzContext* ctx) {
    glBindBuffer(GL_ARRAY_BUFFER, ctx->terrainVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(ctx->terrainStaging.size() * sizeof(TerrainVertex)),
                    ctx->terrainStaging.data());
}

constexpr int32_t kLastQuad = kQuadsPerSide - 1;   // 254

} // namespace

// Malha inteira + borda; mapa de sombra do terreno inteiro refeito.
// rzSetHeightmap e rzSetTerrainScale (carga).
bool buildTerrainMesh(RzContext* ctx) {
    ctx->terrainStaging.resize(size_t(kTriangleCount) * 3);   // carga: aloca na primeira vez
    layoutChunks(ctx);
    for (int32_t cz = 0; cz < kChunksPerSide; ++cz)
        for (int32_t cx = 0; cx < kChunksPerSide; ++cx) chunkBounds(ctx, cx, cz);
    buildQuads(ctx, 0, 0, kLastQuad, kLastQuad);
    uploadAll(ctx);
    buildBorder(ctx);                        // continuação e parede dependem das alturas/blocos
    markTerrainShadowAll(ctx);
    return glGetError() == GL_NO_ERROR;
}

// Só os blocos mudaram (rzSetTileMap): bloco dos quads e da continuação; as
// alturas, a borda de relevo e a sombra ficam.
bool rebuildTerrainTiles(RzContext* ctx) {
    ctx->terrainStaging.resize(size_t(kTriangleCount) * 3);
    layoutChunks(ctx);
    buildQuads(ctx, 0, 0, kLastQuad, kLastQuad);
    uploadAll(ctx);
    rebuildSkirtMesh(ctx);
    return glGetError() == GL_NO_ERROR;
}

// rzUpdateTerrain: relê os buffers do host (ponteiros guardados), compara com
// as cópias e, para cada ponto/quad que mudou (no jogo são poucos):
//   - altura: refaz e reenvia os 4 quads em volta do ponto, atualiza a cópia
//     estendida (câmera) e a faixa de alturas dos blocos do ponto (a sombra
//     de longe não é refeita: ver rz_shadow.cpp); se o ponto é da borda do
//     mapa, só move junto o y das vértices do terreno de fora que estão em
//     cima dele (moveSkirtEdgeVertex;
//     o resto de fora não é recalculado: aceito, rzSetHeightmap refaz tudo);
//   - bloco: refaz e reenvia o quad; se está a menos de kSkirtTileBand quads
//     da borda, refaz a malha da continuação (ela copia/sorteia dessa faixa).
// Sem mudança, não faz nada. Não aloca.
bool updateTerrain(RzContext* ctx) {
    auto refreshQuads = [&](int32_t c0, int32_t r0, int32_t c1, int32_t r1) {   // inclusivos, recortados
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
            // faixa de alturas dos blocos do ponto (até 4, na divisa)
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
        // ~3 ms (llvmpipe). Otimização futura possível: só os triângulos de fora
        // que copiam deste quad (ver spec 12 e o branch super_complicated_border_update)
        if (edgeTile) rebuildSkirtMesh(ctx);
    }
    return true;                // sem glGetError: chamado no jogo, e no WebGL ele espera a GPU
}

void drawTerrain(const RzContext* ctx, const Mat4& matrix, bool withFog) {
    const Frustum f = frustumOf(matrix);
    const float cs = ctx->cellSize;
    const float reach = ctx->fogFar;
    const Vec3 o = ctx->fogOrigin;
    glBindVertexArray(ctx->terrainVao);
    int32_t runFirst = 0, runCount = 0;          // blocos seguidos no VBO: um envio só
    for (int32_t cz = 0; cz < kChunksPerSide; ++cz) {
        for (int32_t cx = 0; cx < kChunksPerSide; ++cx) {
            const RzContext::TerrainChunk& k = ctx->terrainChunks[cz * kChunksPerSide + cx];
            const Vec3 lo = { float(cx * kChunkQuads) * cs, k.yMin, float(cz * kChunkQuads) * cs };
            const Vec3 hi = { float(cx * kChunkQuads + chunkWidth(cx)) * cs, k.yMax,
                              float(cz * kChunkQuads + chunkWidth(cz)) * cs };
            bool visible = boxInFrustum(f, lo, hi);
            if (visible && withFog) {            // o ponto da caixa mais perto da origem da neblina
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

// Esfera envolvente: raio = meia diagonal da caixa do terreno. Usada pela
// visão geral e pelos planos near/far.
void updateTerrainBounds(RzContext* ctx) {
    const float hx = 127.5f * ctx->cellSize;
    const float hy = 127.5f * ctx->heightScale;
    ctx->terrainRadius = sqrtf(hx * hx + hy * hy + hx * hx);
}

// Atlas paletizado 256x256 (canto do PCX) -> 256 blocos 16x16 em RGBA (rgbaTexel)
// com os 5 níveis de mipmap, organizados por nível para subir com
// glTexImage3D: nível L começa em tiles + 256 · kLevelOffset[L].
void buildAtlasLevels(uint32_t* tiles, const uint8_t* indices, const uint8_t* paletteRGB) {
    constexpr int32_t kLevelOffset[kMipLevels] = { 0, 256, 320, 336, 340 };   // texels por bloco
    constexpr int32_t kTilesPerRow = kAtlasSize / kTileSize;

    uint32_t pal[256];
    for (int i = 0; i < 256; ++i) {
        const uint32_t rgb = (uint32_t(paletteRGB[i * 3 + 0]) << 16) | (uint32_t(paletteRGB[i * 3 + 1]) << 8)
                           | uint32_t(paletteRGB[i * 3 + 2]);
        pal[i] = rgbaTexel(rgb, i == kTransparentIndex ? 0 : 255);   // 255: transparente
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
