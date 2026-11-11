// The objects' textures: a square 2D texture per object, read from an 8-bit
// PCX, and a fallback texture for the objects with no texture of their own or
// whose loading failed: a chequerboard made here, or a PCX (rzLoadFallbackTexture).
//
// Size: the image's width sets the side W, the largest power of 2 that fits
// in it (at least 256, at most kMaxObjectTexture and the driver's limit).
// Horizontally, what is left on the right is dropped; vertically, the image is
// cut at W rows or completed below repeating the last row. So
// v = 1 is W pixels, the same scale as u.
//
// The PCX's palette stays with the texture: the solid colours (rzAddObjectPolygon)
// come from it (rz_object.cpp). The fallback chequerboard made here has no palette.
//
// Glass texels (indices 240..254, kFirstGlassIndex): see rz_internal.h.
//
// Mipmaps made on the CPU: each texel of a level is of the class (opaque, glass
// or transparent) of the majority of the 4 below, with the average of their
// colours only (the alpha does not mix the classes); fixed filter, the same as
// the ground's (nearest in the level, linear between levels).

#include <cstring>

#include "rz_internal.h"

namespace rz {

namespace {

constexpr int32_t  kMinObjectTexture = 256;
constexpr int32_t  kMaxObjectTexture = 4096;
constexpr int32_t  kFallbackSize     = 256;
constexpr int32_t  kFallbackCell     = 32;
constexpr uint32_t kFallbackColorA   = 0x00C040C0u;   // magenta: "no texture"
constexpr uint32_t kFallbackColorB   = 0x00302830u;

int32_t floorPow2(int32_t v) {
    int32_t p = 1;
    while (p * 2 <= v) p *= 2;
    return p;
}

bool validId(const RzContext* ctx, int32_t id) {
    return ctx && id >= 0 && id < int32_t(ctx->objects.size()) && ctx->objects[id].alive;
}

void releaseTexture(Object& o) {
    if (o.texture) glDeleteTextures(1, &o.texture);
    o.texture = 0;
}

// The texel's class by the alpha: 0 transparent, 1 glass, 2 opaque
int32_t texelClass(uint32_t t) {
    const uint32_t a = t >> 24;
    return a == 0 ? 0 : (a == kGlassAlpha ? 1 : 2);
}

// Level L+1 from L (rgbaTexel): the class of the majority of the 4 texels (on
// a tie, the most opaque), with the rounded average of their colours
void downsampleClasses(const uint32_t* src, uint32_t* dst, int32_t dstSide) {
    const int32_t srcSide = dstSide * 2;
    constexpr uint32_t kAlpha[3] = { 0, kGlassAlpha, 255 };
    for (int32_t y = 0; y < dstSide; ++y) {
        for (int32_t x = 0; x < dstSide; ++x) {
            const uint32_t p[4] = { src[(2 * y) * srcSide + 2 * x],     src[(2 * y) * srcSide + 2 * x + 1],
                                    src[(2 * y + 1) * srcSide + 2 * x], src[(2 * y + 1) * srcSide + 2 * x + 1] };
            int32_t count[3] = { 0, 0, 0 };
            for (const uint32_t q : p) ++count[texelClass(q)];
            int32_t cls = 2;
            for (int32_t c = 1; c >= 0; --c)
                if (count[c] > count[cls]) cls = c;
            uint32_t sum[3] = { 0, 0, 0 };
            for (const uint32_t q : p) {
                if (texelClass(q) != cls) continue;
                for (int32_t c = 0; c < 3; ++c) sum[c] += (q >> (8 * c)) & 0xFF;
            }
            const uint32_t n = uint32_t(count[cls]);
            uint32_t out = kAlpha[cls] << 24;
            for (int32_t c = 0; c < 3; ++c) out |= ((sum[c] + n / 2) / n) << (8 * c);
            dst[y * dstSide + x] = out;
        }
    }
}

} // namespace

int32_t mipLevels(int32_t side) {
    int32_t levels = 1;
    while ((side >> (levels - 1)) > 1) ++levels;
    return levels;
}

// Fixed filter: magnification nearest; minification nearest within the level and
// linear blending between levels (GL_NEAREST_MIPMAP_LINEAR)
void applyFilter2D(GLuint texture, int32_t side) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipLevels(side) - 1);
}

// rgba: side x side texels (rgbaTexel; row 0 = v 0). Makes the mipmaps on the CPU
// and uploads it all; returns 0 if WebGL fails.
GLuint uploadSquareTexture(const uint32_t* rgba, int32_t side) {
    const int32_t levels = mipLevels(side);
    std::vector<uint32_t> chain;                         // temporary, for loading
    size_t total = 0;
    for (int32_t l = 0; l < levels; ++l) total += size_t(side >> l) * size_t(side >> l);
    chain.resize(total);
    std::memcpy(chain.data(), rgba, size_t(side) * size_t(side) * sizeof(uint32_t));

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    size_t offset = 0;
    for (int32_t l = 0; l < levels; ++l) {
        const int32_t s = side >> l;
        if (l > 0) {
            const int32_t prev = s * 2;
            downsampleClasses(chain.data() + offset - size_t(prev) * size_t(prev), chain.data() + offset, s);
        }
        fillTransparent(chain.data() + offset, s);       // colour of the holes = opaque neighbours
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     chain.data() + offset);
        offset += size_t(s) * size_t(s);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    applyFilter2D(texture, side);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

// PCX -> square texture (the rules at the top of the file), its palette (768
// bytes RGB) and whether it has glass texels. On an error, nothing is created and
// *outTexture stays 0.
static int32_t loadSquareTextureFromPcx(const uint8_t* pcx, int32_t size, GLuint* outTexture,
                                        uint8_t* outPalette, bool* outGlass) {
    *outTexture = 0;
    *outGlass = false;
    PcxImage img;
    const int32_t err = decodePcx(pcx, size, img);
    if (err != RZ_OK) return err;
    if (img.width < kMinObjectTexture) return RZ_ERR_SIZE;

    GLint driverMax = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &driverMax);
    int32_t limit = kMaxObjectTexture;
    if (driverMax > 0 && driverMax < limit) limit = driverMax;
    const int32_t side = floorPow2(img.width < limit ? img.width : limit);

    // Top left corner, side x side; below the image, the last row repeats
    uint32_t pal[256];
    for (int i = 0; i < 256; ++i) {
        const uint32_t rgb = (uint32_t(img.palette[i * 3 + 0]) << 16) | (uint32_t(img.palette[i * 3 + 1]) << 8)
                           | uint32_t(img.palette[i * 3 + 2]);
        pal[i] = rgbaTexel(rgb, i == kTransparentIndex ? 0 : 255);   // 255: transparent
    }
    for (int32_t i = kFirstGlassIndex; i < kTransparentIndex; ++i) {   // glass: the tone's grey
        const int32_t tone = i == kFirstGlassIndex ? kGlassTexelTone0 : i - kFirstGlassIndex;
        const uint32_t g = uint32_t(tone * 255 + 7) / 15;
        pal[i] = rgbaTexel((g << 16) | (g << 8) | g, kGlassAlpha);
    }
    std::vector<uint32_t> texels(size_t(side) * size_t(side));
    for (int32_t y = 0; y < side; ++y) {
        const int32_t srcY = y < img.height ? y : img.height - 1;
        const uint8_t* src = img.pixels.data() + size_t(srcY) * size_t(img.width);
        uint32_t* dst = texels.data() + size_t(y) * size_t(side);
        for (int32_t x = 0; x < side; ++x) {
            dst[x] = pal[src[x]];
            if (src[x] >= kFirstGlassIndex && src[x] < kTransparentIndex) *outGlass = true;
        }
    }

    *outTexture = uploadSquareTexture(texels.data(), side);
    if (!*outTexture) return RZ_ERR_GL;
    std::memcpy(outPalette, img.palette.data(), 768);
    return RZ_OK;
}

// Magenta/dark chequerboard: easy to see that the object was left with no texture.
// Replaces (and frees) the current fallback.
bool createFallbackTexture(RzContext* ctx) {
    std::vector<uint32_t> texels(size_t(kFallbackSize) * kFallbackSize);
    for (int32_t y = 0; y < kFallbackSize; ++y) {
        for (int32_t x = 0; x < kFallbackSize; ++x) {
            const bool a = ((x / kFallbackCell) + (y / kFallbackCell)) % 2 == 0;
            texels[size_t(y) * kFallbackSize + x] = rgbaTexel(a ? kFallbackColorA : kFallbackColorB, 255);
        }
    }
    const GLuint texture = uploadSquareTexture(texels.data(), kFallbackSize);
    if (!texture) return false;
    if (ctx->fallbackTex) glDeleteTextures(1, &ctx->fallbackTex);
    ctx->fallbackTex = texture;
    ctx->hasFallbackPalette = false;
    ctx->fallbackGlass = false;
    markObjectsDirty(ctx);
    return true;
}

} // namespace rz

using namespace rz;

extern "C" {

int32_t rzLoadObjectTexture(RzContext* ctx, int32_t id, const uint8_t* pcx, int32_t size) {
    if (!validId(ctx, id) || !pcx) return recordError(ctx, "rzLoadObjectTexture", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];
    releaseTexture(o);                       // on an error: fallback
    o.gpuDirty = true;                       // the solid colours: the new palette (or the fallback's)
    return recordError(ctx, "rzLoadObjectTexture",
                       loadSquareTextureFromPcx(pcx, size, &o.texture, o.palette, &o.textureGlass));
}

int32_t rzLoadFallbackTexture(RzContext* ctx, const uint8_t* pcx, int32_t size) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!pcx) return recordError(ctx, "rzLoadFallbackTexture", createFallbackTexture(ctx) ? RZ_OK : RZ_ERR_GL);   // back to the chequerboard

    GLuint texture;
    uint8_t palette[768];
    bool glass;
    const int32_t err = loadSquareTextureFromPcx(pcx, size, &texture, palette, &glass);
    if (err != RZ_OK) return recordError(ctx, "rzLoadFallbackTexture", err);   // the current fallback stays
    glDeleteTextures(1, &ctx->fallbackTex);
    ctx->fallbackTex = texture;
    std::memcpy(ctx->fallbackPalette, palette, sizeof(palette));
    ctx->hasFallbackPalette = true;
    ctx->fallbackGlass = glass;
    markObjectsDirty(ctx);                   // the solid colours of those using the fallback
    return RZ_OK;
}

} // extern "C"
