// Shadows: cascades (cascaded shadow maps) of the directional light.
// Orthographic projections with the same light view, fixed, looking at the
// terrain's centre; each cascade is a box in that view.
//
// Following a target (with fog), by 3D distance to the eye:
//   0  0 .. kCascadeSplit0              terrain + objects, every frame
//   1  .. kCascadeSplit1                terrain + objects, every frame
//   2  .. end of the fog                terrain only
// The three are the layers of a depth array texture
// (sampler2DArrayShadow), kShadowMapSize per side, each layer in its own FBO.
// Boxes 0 and 1 enclose the sphere of the slice of the camera's frustum (the
// radius only depends on the FOV and the limits: it does not change when the
// camera turns) and move in steps of a texel, so the shadow does not shake. In
// all of them, only the terrain's chunks that fall in the box are drawn (drawTerrain).
// 2 is a square around the eye (half width = end of the fog + recentring +
// margin), drawn only when the eye moves kCascadeFarRecenter tiles since the
// last drawing, the fog changes size or a new terrain is loaded
// (markTerrainShadowAll). Ice that breaks (rzUpdateTerrain) does not
// redo it: it breaks near a car, which uses cascades 0 and 1, redone every
// frame; from afar the difference does not show, and the next recentring
// includes it already. Objects past kCascadeSplit1 cast no shadow (accepted:
// the fog starts right after).
//
// The shader picks the cascade by the distance to the eye, with a band of
// kCascadeBlend tiles before each split blending the two neighbours. Outside
// a cascade's box, it does not shade.
// In the overview (no target, no fog): only cascade 2, covering the whole
// terrain, with terrain and objects, redone every frame.
// The size is limited to GL_MAX_TEXTURE_SIZE (WebGL 2 guarantees 2048).
// In depth, all of them cover the whole terrain's sphere with room to spare.

#include <cmath>

#include "rz_internal.h"

namespace rz {

namespace {

// A map's box in the light's plane: centre (cx, cy) in the light's view and half width
struct ShadowBox {
    float cx, cy, half;
};

float shadowRadius(const RzContext* ctx) {
    return ctx->terrainRadius * 1.1f + 4.0f * ctx->cellSize;   // room for objects above the terrain
}

Vec3 terrainCenter(const RzContext* ctx) {
    const float cs = ctx->cellSize;
    return { 127.5f * cs, 127.5f * ctx->heightScale, 127.5f * cs };
}

Mat4 lightView(const RzContext* ctx) {
    const float radius = shadowRadius(ctx);
    const Vec3 c = terrainCenter(ctx);
    const Vec3 l = lightDirection();
    const Vec3 eye = c + Vec3{ l.x * 2.0f * radius, l.y * 2.0f * radius, l.z * 2.0f * radius };
    return lookAt(eye, c);
}

// The box's orthographic projection; depth from R to 3R (the terrain's sphere, seen from 2R)
Mat4 boxProjection(const RzContext* ctx, ShadowBox b) {
    const float radius = shadowRadius(ctx);
    const float nearPlane = radius, farPlane = 3.0f * radius;
    const float invHalf = 1.0f / b.half;
    Mat4 p = Mat4::identity();
    p[0, 0] = invHalf;  p[0, 3] = -b.cx * invHalf;
    p[1, 1] = invHalf;  p[1, 3] = -b.cy * invHalf;
    p[2, 2] = -2.0f / (farPlane - nearPlane);
    p[2, 3] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    return p;
}

// Box centred on the world's point p, with the centre snapped to the texel grid
ShadowBox snappedBox(const Mat4& view, Vec3 p, float half, int32_t size) {
    float cx = view[0, 0] * p.x + view[0, 1] * p.y + view[0, 2] * p.z + view[0, 3];
    float cy = view[1, 0] * p.x + view[1, 1] * p.y + view[1, 2] * p.z + view[1, 3];
    const float texel = 2.0f * half / float(size);
    cx = floorf(cx / texel + 0.5f) * texel;
    cy = floorf(cy / texel + 0.5f) * texel;
    return { cx, cy, half };
}

void beginPass(const RzContext* ctx, int32_t map, const Mat4& matrix) {
    glBindFramebuffer(GL_FRAMEBUFFER, ctx->shadowFbo[map]);
    glViewport(0, 0, ctx->shadowSize, ctx->shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    setMatrix(ctx->depthProgram.lightViewProj, matrix);
}


// Sphere enclosing the part of the camera's frustum at a (3D) distance between
// d0 and d1 from the eye: the depth slice [d0 · cos(half diagonal), d1]. The
// radius only depends on the FOV and the limits (stable as the camera turns).
float sliceSphere(const RzContext* ctx, float d0, float d1, Vec3* center) {
    const float tx = 1.0f / ctx->focalX, ty = 1.0f / ctx->focalY;
    const float k2 = tx * tx + ty * ty;                // (half diagonal / depth)^2
    const float n = d0 / sqrtf(1.0f + k2), f = d1;
    float z = 0.5f * (1.0f + k2) * (f + n);             // centre that evens out the corners
    if (z > f) z = f;
    if (z < n) z = n;
    const float rFar  = sqrtf((f - z) * (f - z) + f * f * k2);
    const float rNear = sqrtf((z - n) * (z - n) + n * n * k2);
    const Vec3 fw = ctx->camForward;
    *center = { ctx->eyePos.x + fw.x * z, ctx->eyePos.y + fw.y * z, ctx->eyePos.z + fw.z * z };
    return rFar > rNear ? rFar : rNear;
}

// Terrain and continuation: only the chunks and the regions that fall in the map
void drawTerrainDepth(const RzContext* ctx, const Mat4& matrix) {
    if (!ctx->hasTerrain()) return;
    drawTerrain(ctx, matrix, false);
    if (ctx->skirtOn) drawSkirt(ctx, matrix, false);
}

} // namespace

// The depth array texture with comparison (sampler2DArrayShadow, PCF
// 2x2 with GL_LINEAR), each layer in an FBO with no colour.
bool createShadowMaps(RzContext* ctx) {
    GLint driverMax = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &driverMax);
    const int32_t size = driverMax > 0 && ctx->shadowSize > driverMax ? driverMax : ctx->shadowSize;
    ctx->shadowSize = size;
    glGenTextures(1, &ctx->shadowTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadowTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, size, size, ctx->cascades, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    for (int32_t i = 0; i < ctx->cascades; ++i) {
        glGenFramebuffers(1, &ctx->shadowFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, ctx->shadowFbo[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, ctx->shadowTex, 0, i);
        const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glClear(GL_DEPTH_BUFFER_BIT);                   // empty = all lit
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok) return false;
    }
    markTerrainShadowAll(ctx);
    return true;
}

void markTerrainShadowAll(RzContext* ctx) {
    ctx->terrainShadowDirty = true;
}

void destroyShadowMaps(RzContext* ctx) {
    for (int32_t i = 0; i < kShadowMaps; ++i) {
        if (ctx->shadowFbo[i]) glDeleteFramebuffers(1, &ctx->shadowFbo[i]);
        ctx->shadowFbo[i] = 0;
    }
    if (ctx->shadowTex) glDeleteTextures(1, &ctx->shadowTex);
    ctx->shadowTex = 0;
}

// Works out the cascades' matrices and redraws those that need it. Uses the
// frame's eye and camera (updateCamera before) and the VBOs already uploaded
// (prepareObjects before). No culling (the back faces cast too),
// with a polygon offset against "acne".
void renderShadowMaps(RzContext* ctx) {
    const Mat4 view = lightView(ctx);
    const float radius = shadowRadius(ctx);
    const float cs = ctx->cellSize;
    const bool overview = ctx->shadowWholeTerrain;

    glUseProgram(ctx->depthProgram.program);
    // Depth test turned on right here: without it GL does not write
    // depth (at the 1st frame it has not been turned on by the main pass yet)
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(kShadowOffsetFactor, kShadowOffsetUnits);

    if (overview) {
        // Overview: only cascade 2 (in the medium quality, the only one), the
        // whole terrain + objects, every frame
        const ShadowBox whole = { 0.0f, 0.0f, radius };
        ctx->shadowMatrix[2] = boxProjection(ctx, whole) * view;
        beginPass(ctx, ctx->cascades - 1, ctx->shadowMatrix[2]);
        drawTerrainDepth(ctx, ctx->shadowMatrix[2]);
        drawObjectsDepth(ctx, ctx->shadowMatrix[2], -1, -1);
        ctx->cascadeFarValid = true;
        ctx->cascadeFarOverview = true;
        ctx->terrainShadowDirty = false;
        ctx->shadowMatrix[0] = ctx->shadowMatrix[1] = ctx->shadowMatrix[2];
        ctx->shadowTexel[0] = ctx->shadowTexel[1] = ctx->shadowTexel[2] = 2.0f * radius / float(ctx->shadowSize);
        glDisable(GL_POLYGON_OFFSET_FILL);
        return;
    }

    // Cascades 0 and 1: the slice's sphere, terrain (the box's rows) + objects.
    // With a single cascade: 0, from the eye up to ctx->shadowReach
    const float splits[3] = { 0.0f, kCascadeSplit0 * cs, ctx->shadowReach * cs };
    const float blend = kCascadeBlend * cs;
    const bool single = ctx->cascades == 1;
    for (int32_t i = 0; i < (single ? 1 : 2); ++i) {
        Vec3 center;
        const float d0 = i == 0 ? 0.0f : splits[i] - blend;
        float half = sliceSphere(ctx, d0, splits[single ? 2 : i + 1], &center);
        half += 2.0f * half / float(ctx->shadowSize);        // + texels of the PCF
        // Size in steps of kCascadeSizeStep (rounding up): when the FOV changes,
        // the box does not change size every frame, and between the steps the
        // snapping to texels (snappedBox) keeps the shadow still
        const float stepLog = logf(kCascadeSizeStep);
        half = expf(ceilf(logf(half) / stepLog) * stepLog);
        ctx->shadowTexel[i] = 2.0f * half / float(ctx->shadowSize);
        const ShadowBox box = snappedBox(view, center, half, ctx->shadowSize);
        ctx->shadowMatrix[i] = boxProjection(ctx, box) * view;
        beginPass(ctx, i, ctx->shadowMatrix[i]);
        drawTerrainDepth(ctx, ctx->shadowMatrix[i]);
        drawObjectsDepth(ctx, ctx->shadowMatrix[i], -1, -1);
    }
    if (single) {                       // without cascade 2 (the shaders of the CASCADES 1 variant do not read them)
        ctx->shadowMatrix[1] = ctx->shadowMatrix[2] = ctx->shadowMatrix[0];
        ctx->shadowTexel[1] = ctx->shadowTexel[2] = ctx->shadowTexel[0];
        ctx->cascadeFarValid = false;
        ctx->terrainShadowDirty = false;
        glDisable(GL_POLYGON_OFFSET_FILL);
        return;
    }

    // Cascade 2: terrain only, a square around the eye, redone only when needed
    // the fog's reach from the eye (in the transition from the overview its
    // origin leaves the eye)
    const Vec3 toFog = ctx->fogOrigin - ctx->eyePos;
    const float farHalf = ctx->fogFar + sqrtf(dot(toFog, toFog)) + (kCascadeFarRecenter + kCascadeFarMargin) * cs;
    const float dx = ctx->eyePos.x - ctx->cascadeFarCenter.x, dz = ctx->eyePos.z - ctx->cascadeFarCenter.z;
    const float recenter = kCascadeFarRecenter * cs;
    // recentres when the eye moved too much, the fog changed or it comes from
    // the overview; a new terrain (loading) only redraws (at the same centre: the
    // texel grid does not jump)
    const bool moveBox = !ctx->cascadeFarValid || ctx->cascadeFarOverview || farHalf != ctx->cascadeFarHalf ||
                         dx * dx + dz * dz > recenter * recenter;
    if (moveBox || ctx->terrainShadowDirty) {
        if (moveBox) {
            ctx->cascadeFarCenter = ctx->eyePos;
            ctx->cascadeFarHalf = farHalf;
        }
        const ShadowBox box = snappedBox(view, ctx->cascadeFarCenter, ctx->cascadeFarHalf, ctx->shadowSize);
        ctx->shadowTexel[2] = 2.0f * ctx->cascadeFarHalf / float(ctx->shadowSize);
        ctx->shadowMatrix[2] = boxProjection(ctx, box) * view;
        beginPass(ctx, 2, ctx->shadowMatrix[2]);
        drawTerrainDepth(ctx, ctx->shadowMatrix[2]);
        ctx->cascadeFarValid = true;
        ctx->cascadeFarOverview = false;
    }
    ctx->terrainShadowDirty = false;

    glDisable(GL_POLYGON_OFFSET_FILL);
}

void initShadowUniforms(GLuint program, ShadowUniforms& u) {
    u.matrices = glGetUniformLocation(program, "uShadowMatrix");
    u.cascade  = glGetUniformLocation(program, "uCascade");
    u.onlyFar  = glGetUniformLocation(program, "uCascadeOnlyFar");
    u.offset   = glGetUniformLocation(program, "uShadowOffset");
    u.map      = glGetUniformLocation(program, "uShadow");
    glUseProgram(program);
    glUniform1i(u.map, kUnitShadow);
    glUniform1f(glGetUniformLocation(program, "uShadowTerminator"), kShadowTerminator);
}

// Program already in use. Leaves texture unit 0 active.
void bindShadowMaps(const RzContext* ctx, const ShadowUniforms& u) {
    glActiveTexture(GL_TEXTURE0 + GLenum(kUnitShadow));
    glBindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadowTex);
    glActiveTexture(GL_TEXTURE0);
    setMatrices(u.matrices, ctx->shadowMatrix, kShadowMaps);
    const float cs = ctx->cellSize;
    glUniform3f(u.cascade, kCascadeSplit0 * cs, ctx->shadowReach * cs, kCascadeBlend * cs);
    glUniform1i(u.onlyFar, ctx->shadowWholeTerrain ? 1 : 0);
    glUniform3f(u.offset, kShadowNormalOffset * ctx->shadowTexel[0], kShadowNormalOffset * ctx->shadowTexel[1],
                kShadowNormalOffset * ctx->shadowTexel[2]);
}

} // namespace rz
