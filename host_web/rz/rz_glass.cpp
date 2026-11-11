// The objects' translucent polygons (glass).
//
// Each object can have, besides the opaque polygons, glass polygons in shades
// of grey (16 tones: tone/15, 0 = black, 15 = transparent). The glass is a
// multiplicative filter (what is behind is multiplied by the grey) plus a
// "blurred" specular highlight (Blinn-Phong of low exponent) of the directional
// light, gone in the shadow. In a single pass, with glBlendFunc(GL_ONE, GL_SRC_ALPHA):
//     destination = specular + destination x grey
// The filter does not depend on the drawing order, so there is no sorting (with
// two glasses on top of each other, only the back one's highlight is a little
// darkened by the front one, or not: a small difference).
//
// Drawn after the opaque objects, writing neither depth nor the destination's
// alpha, with the same culling as the objects (only the outside face). Casts no
// shadow; receives it (in the highlight). With fog: the filter goes to 1 and the highlight to 0.
// Normal: the outward one (−Newell, the legacy's direction), per polygon.

#include <cmath>

#include "rz_internal.h"

namespace rz {

namespace {

bool validId(const RzContext* ctx, int32_t id) {
    return ctx && id >= 0 && id < int32_t(ctx->objects.size()) && ctx->objects[id].alive;
}

// Builds the vertices (outward normal and grey per polygon) and uploads the VBO again
void uploadGlass(Object& o) {
    GlassVertex* v = o.glassStaging.data();
    for (size_t p = 0; p < o.glassStart.size(); ++p) {
        const uint16_t* idx = o.glassIndices.data() + o.glassStart[p];
        const int32_t n = o.glassLength[p];
        Vec3 nn = { 0.0f, 0.0f, 0.0f };
        for (int32_t i = 0; i < n; ++i) {
            const Vec3 cur  = o.world[idx[i]];
            const Vec3 next = o.world[idx[i + 1 == n ? 0 : i + 1]];
            nn.x += (cur.y - next.y) * (cur.z + next.z);
            nn.y += (cur.z - next.z) * (cur.x + next.x);
            nn.z += (cur.x - next.x) * (cur.y + next.y);
        }
        const float len = sqrtf(dot(nn, nn));
        const float inv = len > 0.0f ? -1.0f / len : 0.0f;          // outward = −Newell
        const Vec3 normal = { nn.x * inv, nn.y * inv, nn.z * inv };
        const float tint = float(o.glassTone[p]) * (1.0f / 15.0f);
        for (int32_t i = 1; i + 1 < n; ++i) {
            const uint16_t corners[3] = { idx[0], idx[i], idx[i + 1] };
            for (uint16_t k : corners) {
                const Vec3 q = o.world[k];
                *v++ = { q.x, q.y, q.z, normal.x, normal.y, normal.z, tint };
            }
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, o.glassVbo);
    if (o.glassResized) {                     // the first upload after new polygons
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(o.glassStaging.size() * sizeof(GlassVertex)),
                     o.glassStaging.data(), GL_DYNAMIC_DRAW);
        o.glassResized = false;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(o.glassStaging.size() * sizeof(GlassVertex)),
                        o.glassStaging.data());
    }
}

} // namespace

bool createGlassProgram(RzContext* ctx) {
    GlassProgram& g = ctx->glassProgram;
    g.program = linkProgram("glass.vert", "glass.frag");
    if (!g.program) return false;
    g.viewProj  = glGetUniformLocation(g.program, "uViewProj");
    g.light     = glGetUniformLocation(g.program, "uLight");
    g.specular  = glGetUniformLocation(g.program, "uSpecular");
    g.shininess = glGetUniformLocation(g.program, "uShininess");
    initShadowUniforms(g.program, g.shadow);
    initFogUniforms(g.program, g.fog);
    return true;
}

void freeGlass(Object& o) {
    if (o.glassVbo) glDeleteBuffers(1, &o.glassVbo);
    if (o.glassVao) glDeleteVertexArrays(1, &o.glassVao);
    o.glassVbo = 0;
    o.glassVao = 0;
}

// The glass pass: after the opaque objects
void drawGlass(RzContext* ctx, const Mat4& viewProj) {
    const Frustum f = frustumOf(viewProj);
    bool programBound = false;
    for (Object& o : ctx->objects) {
        if (!o.alive || !o.positioned || o.glassStaging.empty() || !objectInView(ctx, o, f)) continue;
        if (!programBound) {
            const GlassProgram& g = ctx->glassProgram;
            glUseProgram(g.program);
            setMatrix(g.viewProj, viewProj);
            const Vec3 l = lightDirection();
            glUniform3f(g.light, l.x, l.y, l.z);
            glUniform1f(g.specular, kGlassSpecular);
            glUniform1f(g.shininess, kGlassShininess);
            bindShadowMaps(ctx, g.shadow);
            bindFog(ctx, g.fog);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);                     // as the objects: only the outside face
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_SRC_ALPHA);        // specular + destination x grey
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
            glDepthMask(GL_FALSE);
            programBound = true;
        }
        if (o.glassDirty) {
            uploadGlass(o);
            o.glassDirty = false;
        }
        glBindVertexArray(o.glassVao);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(o.glassStaging.size()));
    }
    if (programBound) {
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_BLEND);
    }
}

} // namespace rz

using namespace rz;

extern "C" {

int32_t rzAddObjectTranslucentPolygon(RzContext* ctx, int32_t id,
                                      const uint16_t* indices, int32_t count,
                                      int32_t tone) {
    if (!validId(ctx, id) || !indices || count < 0) return recordError(ctx, "rzAddObjectTranslucentPolygon", RZ_ERR_INVALID_ARG);
    if (tone < 0 || tone > 15) return recordError(ctx, "rzAddObjectTranslucentPolygon", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];

    int32_t n = count;
    if (n > 1 && indices[n - 1] == indices[0]) --n;          // the legacy's closing
    for (int32_t i = 0; i < n; ++i) {
        if (indices[i] >= o.vertexCount()) return recordError(ctx, "rzAddObjectTranslucentPolygon", RZ_ERR_INVALID_ARG);
    }
    if (n < 3) return RZ_OK;                                  // degenerate: ignored

    if (!o.glassVao) {                                        // the object's first glass
        glGenVertexArrays(1, &o.glassVao);
        glGenBuffers(1, &o.glassVbo);
        glBindVertexArray(o.glassVao);
        glBindBuffer(GL_ARRAY_BUFFER, o.glassVbo);
        const GLsizei stride = sizeof(GlassVertex);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(GlassVertex, nx)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(GlassVertex, tint)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    // Copy (loading step: the vectors grow here)
    o.glassStart.push_back(int32_t(o.glassIndices.size()));
    o.glassLength.push_back(n);
    o.glassTone.push_back(uint8_t(tone));
    o.glassIndices.insert(o.glassIndices.end(), indices, indices + n);
    o.glassStaging.resize(o.glassStaging.size() + size_t(n - 2) * 3);

    o.glassResized = true;                    // the VBO at the next upload (uploadGlass), once
    o.glassDirty = true;
    return RZ_OK;
}

} // extern "C"
