// Sprites (smoke, debris, splashing water): quads facing the camera.
//
// The legacy has a compact array (a sprite that dies makes the following
// ones move up a position), so there is no id: rzSetSprites gets the whole
// array and replaces the previous one. The renderer keeps no state per sprite.
//
// Each sprite: centre (tiles), diameter (tiles) and type.
//   Debris: colour index in the game's palette (that of the block atlas; with
//     no atlas, a grey ramp). The shape is a noise texture like Paint's
//     "spray" (kSpriteClicks clicks of dots near the centre), with
//     kSpriteFrames variations in an array texture; each sprite's picture
//     turns every kSpriteFrameTicks frames, offset by its position in the
//     array, so it does not look still.
//   Smoke: a white disc with low frequency 3D value noise, worked out in the
//     shader (x, y in the disc + centre in the world, z = time), so it changes
//     by itself and follows the sprite without jumping when the array is compacted.
//     Alpha = noise x opacity, with the disc's edge smoothed.
//
// Drawing: after the glass (smoke in front of a glass does not come out
// tinted) and before the boundary wall. Ordinary alpha blending, sorted back
// to front on the CPU (up to kMaxSprites), writing neither depth nor the
// destination's alpha, no culling. Colour = palette x the light of a face
// pointing up (as the flat ground), with fog. Neither casts nor receives a shadow.
//
// Memory: the VBO (kMaxSprites instances, one per sprite: one glDrawArraysInstanced
// of 6 vertices, which the shader opens into the quad) and the work vectors are
// allocated when the context is created; rzSetSprites and the drawing do not allocate.

#include <algorithm>
#include <cmath>

#include "rz_internal.h"

namespace rz {

namespace {


uint32_t hashU32(uint32_t h) {
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    return h ^ (h >> 16);
}

// "Spray" noise: kSpriteClicks clicks, each a cloud of uniform dots in a
// disc, with the click's centre drawn by lot near the middle. Alpha in
// four levels: outside the sprite's circle 0; inside, by the number of clicks
// that hit the pixel (kSpriteAlphaByHits: none, one, two or more).
// Mipmaps on the CPU (2x2 average of the alpha). One layer per picture.
void buildNoiseTexture(RzContext* ctx) {
    constexpr int32_t N = kSpriteNoiseSize;
    constexpr int32_t kLevels = 7;                         // 64 .. 1
    std::vector<uint8_t> level0(size_t(N) * N * kSpriteFrames, 0);
    std::vector<uint8_t> hits(size_t(N) * N), hit(size_t(N) * N);
    uint32_t seed = 12345u;
    auto rnd = [&]() { seed = hashU32(seed + 0x9E3779B9u); return float(seed & 0xFFFFFFu) / 16777216.0f; };
    for (int32_t f = 0; f < kSpriteFrames; ++f) {
        std::fill(hits.begin(), hits.end(), uint8_t(0));
        for (int32_t click = 0; click < kSpriteClicks; ++click) {
            std::fill(hit.begin(), hit.end(), uint8_t(0));
            const float ca = rnd() * 2.0f * kPi, cr = rnd() * kSpriteClickJitter * N;
            const float cx = 0.5f * N + cosf(ca) * cr, cy = 0.5f * N + sinf(ca) * cr;
            const float radius = kSpriteClickRadius * N;
            for (int32_t d = 0; d < kSpriteDotsPerClick; ++d) {
                const float a = rnd() * 2.0f * kPi, r = sqrtf(rnd()) * radius;   // uniform in the disc
                const int32_t px = int32_t(cx + cosf(a) * r), py = int32_t(cy + sinf(a) * r);
                if (px >= 0 && px < N && py >= 0 && py < N) hit[py * N + px] = 1;
            }
            for (size_t i = 0; i < hits.size(); ++i) hits[i] = uint8_t(hits[i] + hit[i]);   // a click counts once
        }
        uint8_t* img = level0.data() + size_t(f) * N * N;
        for (int32_t y = 0; y < N; ++y) {
            for (int32_t x = 0; x < N; ++x) {
                const float dx = float(x) + 0.5f - 0.5f * N, dy = float(y) + 0.5f - 0.5f * N;
                if (dx * dx + dy * dy >= 0.25f * N * N) continue;          // outside the circle: 0
                const int32_t h = hits[y * N + x] < 2 ? hits[y * N + x] : 2;
                img[y * N + x] = kSpriteAlphaByHits[h];
            }
        }
    }

    glGenTextures(1, &ctx->spriteNoiseTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ctx->spriteNoiseTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::vector<uint8_t> cur = level0, next;
    int32_t side = N;
    for (int32_t level = 0; level < kLevels; ++level) {
        glTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_R8, side, side, kSpriteFrames, 0,
                     GL_RED, GL_UNSIGNED_BYTE, cur.data());
        if (side == 1) break;
        const int32_t half = side / 2;
        next.assign(size_t(half) * half * kSpriteFrames, 0);
        for (int32_t f = 0; f < kSpriteFrames; ++f) {
            const uint8_t* s = cur.data() + size_t(f) * side * side;
            uint8_t* d = next.data() + size_t(f) * half * half;
            for (int32_t y = 0; y < half; ++y)
                for (int32_t x = 0; x < half; ++x)
                    d[y * half + x] = uint8_t((s[(2 * y) * side + 2 * x] + s[(2 * y) * side + 2 * x + 1] +
                                               s[(2 * y + 1) * side + 2 * x] + s[(2 * y + 1) * side + 2 * x + 1] + 2) / 4);
        }
        cur.swap(next);
        side = half;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, kLevels - 1);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);           // dots up close
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // soft from afar
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

// The sprites' palette: the atlas's (rzLoadTileAtlas); before it, a grey ramp.
// With the light of a face pointing up already, as the flat ground.
void setSpritePalette(RzContext* ctx, const uint8_t* paletteRGB) {
    const Vec3 l = lightDirection();
    const float light = kAmbient + (1.0f - kAmbient) * (l.y > 0.0f ? l.y : 0.0f);
    for (int32_t i = 0; i < 256; ++i) {
        const float r = paletteRGB ? paletteRGB[i * 3 + 0] : float(i);
        const float g = paletteRGB ? paletteRGB[i * 3 + 1] : float(i);
        const float b = paletteRGB ? paletteRGB[i * 3 + 2] : float(i);
        auto byte = [](float c) { return uint32_t(c < 0.0f ? 0.0f : (c > 255.0f ? 255.0f : c + 0.5f)); };
        ctx->spritePalette[i] = (byte(r * light) << 16) | (byte(g * light) << 8) | byte(b * light);
    }
}

bool createSprites(RzContext* ctx) {
    SpriteProgram& p = ctx->spriteProgram;
    p.program = linkProgram("sprite.vert", "sprite.frag");
    if (!p.program) return false;
    p.viewProj = glGetUniformLocation(p.program, "uViewProj");
    p.right    = glGetUniformLocation(p.program, "uRight");
    p.up       = glGetUniformLocation(p.program, "uUp");
    p.noise    = glGetUniformLocation(p.program, "uNoise");
    p.time     = glGetUniformLocation(p.program, "uTime");
    initFogUniforms(p.program, p.fog);

    ctx->sprites.reserve(kMaxSprites);
    ctx->spriteOrder.reserve(kMaxSprites);
    ctx->spriteStaging.resize(size_t(kMaxSprites));
    setSpritePalette(ctx, nullptr);
    buildNoiseTexture(ctx);

    glGenVertexArrays(1, &ctx->spriteVao);
    glGenBuffers(1, &ctx->spriteVbo);
    glBindVertexArray(ctx->spriteVao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->spriteVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(ctx->spriteStaging.size() * sizeof(SpriteInstance)),
                 nullptr, GL_DYNAMIC_DRAW);
    // Everything per instance (divisor 1): one record per sprite
    const GLsizei stride = sizeof(SpriteInstance);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));   // centre + radius
    glVertexAttribIPointer(1, 2, GL_BYTE, stride,                                          // picture, type
                           reinterpret_cast<void*>(offsetof(SpriteInstance, frame)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,                         // colour
                          reinterpret_cast<void*>(offsetof(SpriteInstance, color)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,                                // opacity
                          reinterpret_cast<void*>(offsetof(SpriteInstance, opacity)));
    for (GLuint a = 0; a < 4; ++a) {
        glEnableVertexAttribArray(a);
        glVertexAttribDivisor(a, 1);
    }
    glBindVertexArray(0);
    return glGetError() == GL_NO_ERROR;
}

void destroySprites(RzContext* ctx) {
    if (ctx->spriteProgram.program) glDeleteProgram(ctx->spriteProgram.program);
    if (ctx->spriteVbo) glDeleteBuffers(1, &ctx->spriteVbo);
    if (ctx->spriteVao) glDeleteVertexArrays(1, &ctx->spriteVao);
    if (ctx->spriteNoiseTex) glDeleteTextures(1, &ctx->spriteNoiseTex);
}

// After the glass, before the wall
void drawSprites(RzContext* ctx, const Mat4& viewProj) {
    if (ctx->sprites.empty()) return;

    // Back to front order (depth along the camera's front);
    // those entirely past the fog are left out
    const Vec3 eye = ctx->eyePos, fwd = ctx->camForward;
    const float reach = ctx->fogFar;
    ctx->spriteOrder.clear();
    for (int32_t i = 0; i < int32_t(ctx->sprites.size()); ++i) {
        const Sprite& s = ctx->sprites[size_t(i)];
        const Vec3 d = s.center - eye;
        if (ctx->fogOn) {
            const Vec3 dFog = s.center - ctx->fogOrigin;
            const float lim = reach + s.radius;
            if (dot(dFog, dFog) > lim * lim) continue;
        }
        const float depth = dot(d, fwd);
        if (depth < -s.radius) continue;                      // all behind the eye
        ctx->spriteOrder.push_back({ depth, i });
    }
    if (ctx->spriteOrder.empty()) return;
    std::sort(ctx->spriteOrder.begin(), ctx->spriteOrder.end(),
              [](const SpriteKey& a, const SpriteKey& b) { return a.depth > b.depth; });

    // One record per sprite, in drawing order
    const uint32_t tick = uint32_t(ctx->frameTime) / uint32_t(kSpriteFrameTicks);
    SpriteInstance* v = ctx->spriteStaging.data();
    for (const SpriteKey& k : ctx->spriteOrder) {
        const Sprite& s = ctx->sprites[size_t(k.index)];
        const int8_t frame = int8_t((tick + uint32_t(k.index) * 3u) % uint32_t(kSpriteFrames));
        *v++ = { s.center.x, s.center.y, s.center.z, s.radius, s.opacity, frame, int8_t(s.type), {}, s.color };
    }
    const GLsizei count = GLsizei(ctx->spriteOrder.size());
    glBindBuffer(GL_ARRAY_BUFFER, ctx->spriteVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(size_t(count) * sizeof(SpriteInstance)),
                    ctx->spriteStaging.data());

    const SpriteProgram& p = ctx->spriteProgram;
    glUseProgram(p.program);
    setMatrix(p.viewProj, viewProj);
    glUniform3f(p.right, ctx->camRight.x, ctx->camRight.y, ctx->camRight.z);
    glUniform3f(p.up, ctx->camUp.x, ctx->camUp.y, ctx->camUp.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ctx->spriteNoiseTex);
    glUniform1i(p.noise, 0);
    glUniform1f(p.time, fmodf(ctx->frameTime * (kSmokeNoiseSpeed / 60.0f), 1000.0f));
    bindFog(ctx, p.fog);

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);   // the destination's alpha = reserved byte
    glDepthMask(GL_FALSE);
    glBindVertexArray(ctx->spriteVao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);    // two triangles per sprite
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace rz

using namespace rz;

extern "C" {

int32_t rzSetSprites(RzContext* ctx, const RzSprite* sprites, int32_t count) {
    if (!ctx || count < 0 || (count > 0 && !sprites)) return recordError(ctx, "rzSetSprites", RZ_ERR_INVALID_ARG);
    if (count > kMaxSprites) return recordError(ctx, "rzSetSprites", RZ_ERR_SIZE);
    for (int32_t i = 0; i < count; ++i) {
        const RzSprite& s = sprites[i];
        if (!(s.size > 0.0f && s.size < 1.0e6f)) return recordError(ctx, "rzSetSprites", RZ_ERR_INVALID_ARG);   // catches NaN
        if (!validCoord(s.x) || !validCoord(s.y) || !validCoord(s.z)) {
            return recordError(ctx, "rzSetSprites", RZ_ERR_INVALID_ARG);
        }
        if (s.type != RZ_SPRITE_DEBRIS && s.type != RZ_SPRITE_SMOKE) return recordError(ctx, "rzSetSprites", RZ_ERR_INVALID_ARG);
        if (!(s.opacity >= 0.0f && s.opacity < 1.0e6f)) return recordError(ctx, "rzSetSprites", RZ_ERR_INVALID_ARG);   // catches NaN
    }
    const float scale = ctx->cellSize;
    ctx->sprites.clear();                                      // capacity reserved: does not allocate
    for (int32_t i = 0; i < count; ++i) {
        const RzSprite& s = sprites[i];
        Sprite d;
        d.center = { s.x * scale, s.z * scale, s.y * scale };   // (column, height, row)
        d.radius = 0.5f * s.size * ctx->cellSize;
        d.type   = s.type;
        d.opacity = s.opacity;
        d.color  = ctx->spritePalette[s.color];
        ctx->sprites.push_back(d);
    }
    return RZ_OK;
}

} // extern "C"
