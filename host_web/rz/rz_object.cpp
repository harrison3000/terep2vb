// Objects: meshes of convex polygons in absolute world coordinates.
//
// Loading: rzCreateObject creates the empty object (vertices, VAO and VBO);
// rzAddObjectPolygon / rzAddObjectTexturedPolygon check and copy a
// polygon (and its UVs or the palette index), triangulate it as a fan, grow
// the CPU buffers and reallocate the VBO. The object's texture is in rz_texture.cpp.
//
// The solid colour polygon (rzAddObjectPolygon) carries the colour in the
// vertex, that of the palette of the PCX of the texture in use (solidColor),
// and goes through the same light and shadow as a textured one's texel;
// changing the texture changes the palette, and the corners are uploaded
// again. The shaded one (rzAddObjectShadedPolygon) is the same, with a
// colour per corner, interpolated in RGB by the shader.
// The triangles' corners (VBO) are static: the object's vertex, polygon,
// UV and colour. The positions and the polygons' indices are in a uniform block of the
// object (ObjectData, kObjectDataSize); the vertex shader fetches the corner's position and
// works out the whole polygon's Newell normal (flat light per polygon, even
// if not planar). The lines (rzAddObjectLine) have their position and normal directly.
// Update (rzUpdateObjectVertices): takes the positions (tiles) to the world and
// redoes the bounding sphere; in the frame, only the block's positions (and the
// lines) are uploaded again.
// Frame (drawObjects): one glDrawArrays per visible object.

#include <algorithm>
#include <cmath>

#include "rz_internal.h"

using namespace rz;

namespace {

// The solid colour `index` for the vertex: that of the object's colour palette (that of
// its texture's PCX, the fallback's or, with the generated chequerboard, the atlas's, the
// game's; with none of them, grey); index 255 is a hole (kCornerHole).
uint32_t solidColor(const RzContext* ctx, const Object& o, int32_t index) {
    const uint8_t* palette = o.texture ? o.palette
                           : ctx->hasFallbackPalette ? ctx->fallbackPalette
                           : ctx->hasAtlasPalette ? ctx->atlasPalette : nullptr;
    const uint32_t rgb = palette ? (uint32_t(palette[index * 3]) << 16) | (uint32_t(palette[index * 3 + 1]) << 8)
                                   | uint32_t(palette[index * 3 + 2])
                                 : uint32_t(index) * 0x010101u;
    return rgbaTexel(rgb, index == kTransparentIndex ? kCornerHole : kCornerSolid);
}

bool validId(const RzContext* ctx, int32_t id) {
    return ctx && id >= 0 && id < int32_t(ctx->objects.size()) && ctx->objects[id].alive;
}

// Frees the GPU resources and empties the slot (free to be reused).
void freeObject(Object& o) {
    if (o.texture) glDeleteTextures(1, &o.texture);
    if (o.vbo) glDeleteBuffers(1, &o.vbo);
    if (o.vao) glDeleteVertexArrays(1, &o.vao);
    if (o.dataBuffer) glDeleteBuffers(1, &o.dataBuffer);
    freeGlass(o);
    freeWheels(o);
    o = Object{};
}

// Tiles -> world. The legacy has z up, (column, row, height); the
// renderer has y up: (x, y, z) = (column, height, row).
void loadPositions(const RzContext* ctx, Object& o, const RzVertex* vertices) {
    const float scale = ctx->cellSize;
    for (int32_t i = 0; i < o.vertexCount(); ++i) {
        const float col = vertices[i].x * scale;
        const float row = vertices[i].y * scale;
        const float up  = vertices[i].z * scale;
        o.world[i] = Vec3{ col, up, row };
    }

    // Bounding sphere (centre of the vertices' box, largest radius): culling by
    // distance (fog) and the box of the target's shadow map
    Vec3 lo = o.world[0], hi = o.world[0];
    for (const Vec3& v : o.world) {
        lo = { fminf(lo.x, v.x), fminf(lo.y, v.y), fminf(lo.z, v.z) };
        hi = { fmaxf(hi.x, v.x), fmaxf(hi.y, v.y), fmaxf(hi.z, v.z) };
    }
    o.center = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
    float r2 = 0.0f;
    for (const Vec3& v : o.world) {
        const Vec3 d = v - o.center;
        r2 = fmaxf(r2, dot(d, d));
    }
    o.radius = sqrtf(r2);
}

// Lines (rzAddObjectLine): a prism of square section (side = thickness)
// around the segment a-b, 4 sides + 2 caps, the palette's solid colour with
// each face's flat light. The section is oriented by the world's "up"
// (it does not turn with the object around the line; it is thin, it does not show).
// Faces in the legacy's direction (clockwise seen from outside), for the culling.
void emitLines(const RzContext* ctx, const Object& o, ObjectVertex*& v) {
    for (const ObjectLine& l : o.lines) {
        const Vec3 pa = o.world[l.a], pb = o.world[l.b];
        const uint32_t color = solidColor(ctx, o, l.palette);
        Vec3 d = pb - pa;
        const float len2 = dot(d, d);
        if (!(len2 > 0.0f)) {                                  // end to end: nothing visible
            for (int32_t k = 0; k < kLineVertices; ++k) *v++ = { pa.x, pa.y, pa.z, 0, 0.0f, 0.0f, -1, 0, 0, color };
            continue;
        }
        const float invLen = 1.0f / sqrtf(len2);
        d = { d.x * invLen, d.y * invLen, d.z * invLen };
        const Vec3 ref = fabsf(d.y) < 0.9f ? Vec3{ 0.0f, 1.0f, 0.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
        Vec3 u = cross(d, ref);
        const float invU = 1.0f / sqrtf(dot(u, u));
        u = { u.x * invU, u.y * invU, u.z * invU };
        const Vec3 w = cross(d, u);
        auto off = [&](float su_, float sw_) {
            return Vec3{ (u.x * su_ + w.x * sw_) * l.half, (u.y * su_ + w.y * sw_) * l.half, (u.z * su_ + w.z * sw_) * l.half };
        };
        const Vec3 o4[4] = { off(1, 1), off(-1, 1), off(-1, -1), off(1, -1) };
        Vec3 ca[4], cb[4];
        for (int32_t k = 0; k < 4; ++k) { ca[k] = pa + o4[k]; cb[k] = pb + o4[k]; }
        // quad p0..p3 with the outward normal n: clockwise seen from outside
        auto face = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 n) {
            if (dot(cross(p1 - p0, p2 - p0), n) > 0.0f) { const Vec3 t = p1; p1 = p3; p3 = t; }
            const uint32_t normal = packNormal(n);
            const Vec3 q[6] = { p0, p1, p2, p0, p2, p3 };
            for (const Vec3& p : q) *v++ = { p.x, p.y, p.z, normal, 0.0f, 0.0f, -1, 0, 0, color };
        };
        for (int32_t k = 0; k < 4; ++k) {
            const int32_t k1 = (k + 1) & 3;
            face(ca[k], ca[k1], cb[k1], cb[k], o4[k] + o4[k1]);      // side (normal through the edge's middle)
        }
        face(ca[0], ca[1], ca[2], ca[3], Vec3{ -d.x, -d.y, -d.z });  // caps
        face(cb[0], cb[1], cb[2], cb[3], d);
    }
}

// The staging's size: polygons + lines (loading step). The VBO and the block's
// indices go at the next frame (uploadBuffers), once per object.
void resizeObjectBuffers(Object& o) {
    o.staging.resize(o.triangles.size() * 3 + o.lines.size() * size_t(kLineVertices));
    o.buffersDirty = true;
    o.gpuDirty = true;
}

// Reallocates the VBO at the staging's size and uploads the polygons' indices to the block again
void uploadBuffers(Object& o) {
    glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(o.staging.size() * sizeof(ObjectVertex)),
                 nullptr, GL_STATIC_DRAW);
    const std::vector<int32_t> indices(o.indices.begin(), o.indices.end());   // int in the block
    glBindBuffer(GL_UNIFORM_BUFFER, o.dataBuffer);
    glBufferSubData(GL_UNIFORM_BUFFER, kObjectIndicesOffset, GLsizeiptr(indices.size() * sizeof(int32_t)),
                    indices.data());
}

// Builds the corners of the triangles (and of the lines) and uploads the whole
// vertex buffer again (which already has the right size: reallocated in resizeObjectBuffers).
void uploadObject(const RzContext* ctx, Object& o) {
    ObjectVertex* v = o.staging.data();
    for (const ObjectTriangle& tri : o.triangles) {
        const int32_t pal   = o.polygonPalette[tri.polygon];
        const int32_t first = o.polygonStart[tri.polygon], count = o.polygonLength[tri.polygon];
        const uint32_t color = pal >= 0 ? solidColor(ctx, o, pal) : rgbaTexel(0, kCornerTextured);
        const int32_t corners[3] = { tri.a, tri.b, tri.c };
        for (int32_t corner : corners) {
            const int32_t vertex = o.indices[corner];
            const uint32_t c = pal == kPolygonShaded ? solidColor(ctx, o, o.cornerShade[corner]) : color;
            *v++ = { 0.0f, 0.0f, 0.0f, 0, o.uvs[corner * 2], o.uvs[corner * 2 + 1], vertex, first, count, c };
        }
    }
    emitLines(ctx, o, v);
    glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(o.staging.size() * sizeof(ObjectVertex)),
                    o.staging.data());
}

// Lines: they depend on the positions, so they are redone and uploaded again (only
// their part, at the end of the VBO) at each update
void uploadLines(const RzContext* ctx, Object& o) {
    if (o.lines.empty()) return;
    const size_t first = o.triangles.size() * 3;
    ObjectVertex* v = o.staging.data() + first;
    emitLines(ctx, o, v);
    glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(first * sizeof(ObjectVertex)),
                    GLsizeiptr((o.staging.size() - first) * sizeof(ObjectVertex)), o.staging.data() + first);
}

// Positions in the world -> the object's block (a vec4 per vertex)
void uploadPositions(Object& o) {
    float* p = o.positionStaging.data();
    for (const Vec3& w : o.world) { *p++ = w.x; *p++ = w.y; *p++ = w.z; *p++ = 1.0f; }
    glBindBuffer(GL_UNIFORM_BUFFER, o.dataBuffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, GLsizeiptr(o.positionStaging.size() * sizeof(float)),
                    o.positionStaging.data());
}

// ObjectVertex's layout: position/normal/UV in attributes 0..2 (as
// GpuVertex) and (vertex, start, length of the polygon) integers in 3.
void setObjectBufferLayout() {
    const GLsizei stride = sizeof(ObjectVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(ObjectVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ObjectVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(3, 3, GL_INT, stride, reinterpret_cast<void*>(offsetof(ObjectVertex, vertex)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(ObjectVertex, color)));
    glEnableVertexAttribArray(4);
}

// Binds the object's block (positions and indices) for the shaders
void bindObjectBuffers(const Object& o) {
    glBindBufferBase(GL_UNIFORM_BUFFER, kObjectDataBinding, o.dataBuffer);
}

// Common body of rzAddObjectPolygon (indices, colour = paletteIndex),
// rzAddObjectShadedPolygon (indices and shades, each corner's colour) and
// rzAddObjectTexturedPolygon (corners with index + UV, paletteIndex = -1).
// Exactly one of indices/corners is not null.
int32_t addPolygon(RzContext* ctx, int32_t id, const uint16_t* indices, const RzTexVertex* corners,
                   const uint8_t* shades, int32_t count, int32_t paletteIndex) {
    if (!validId(ctx, id) || (!indices && !corners) || count < 0) return RZ_ERR_INVALID_ARG;
    if (!corners && !shades && (paletteIndex < 0 || paletteIndex > 255)) return RZ_ERR_INVALID_ARG;
    Object& o = ctx->objects[id];
    auto indexAt = [&](int32_t i) { return corners ? corners[i].index : indices[i]; };

    int32_t n = count;
    if (n > 1 && indexAt(n - 1) == indexAt(0)) --n;          // the legacy's closing
    for (int32_t i = 0; i < n; ++i) {
        if (indexAt(i) >= o.vertexCount()) return RZ_ERR_INVALID_ARG;
        if (corners && !(std::isfinite(corners[i].u) && std::isfinite(corners[i].v))) {
            return RZ_ERR_INVALID_ARG;
        }
    }
    if (n < 3) return RZ_OK;                                  // degenerate: ignored
    if (o.polygonStart.size() >= 65535) return RZ_ERR_SIZE;  // the triangle's polygon is a uint16
    if (o.indices.size() + size_t(n) > size_t(kMaxObjectIndices)) return RZ_ERR_SIZE;   // does not fit in the block

    // Copy of the indices and UVs, and the fan (loading step: the vectors grow here)
    const uint16_t polygon = uint16_t(o.polygonStart.size());
    const int32_t  first   = int32_t(o.indices.size());
    o.polygonStart.push_back(first);
    o.polygonLength.push_back(n);
    o.polygonPalette.push_back(corners ? int16_t(-1) : shades ? kPolygonShaded : int16_t(paletteIndex));
    for (int32_t i = 0; i < n; ++i) {
        o.indices.push_back(indexAt(i));
        o.cornerShade.push_back(shades ? shades[i] : 0);
        o.uvs.push_back(corners ? corners[i].u : 0.0f);
        o.uvs.push_back(corners ? corners[i].v : 0.0f);
    }
    for (int32_t i = 1; i + 1 < n; ++i) {
        o.triangles.push_back({ first, first + i, first + i + 1, polygon });
    }
    resizeObjectBuffers(o);
    return RZ_OK;
}

} // namespace

namespace rz {

// GpuVertex's layout (the wheels' faces): position (vec3), normal
// (4 bytes 2-10-10-10) and UV (vec2). The VAO and the VBO need to be bound already.
void setObjectVertexLayout() {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(GpuVertex),
                          reinterpret_cast<void*>(offsetof(GpuVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                          reinterpret_cast<void*>(offsetof(GpuVertex, u)));
    glEnableVertexAttribArray(2);
}

static bool drawable(const Object& o) {
    return o.alive && o.positioned && !o.staging.empty();
}

// Out of range: the whole sphere past the fog (+ a margin for long
// shadows). Without fog (overview), everything is in range.
bool objectInRange(const RzContext* ctx, const Object& o) {
    if (!ctx->fogOn) return true;
    const Vec3 d = o.center - ctx->fogOrigin;
    const float reach = ctx->fogFar + kFogCullMargin * ctx->cellSize + o.radius;
    return dot(d, d) <= reach * reach;
}

// In range and with the sphere (+ kViewCullMargin) inside the frustum of
// what draws: the camera, or the box of a shadow map
bool objectInView(const RzContext* ctx, const Object& o, const Frustum& f) {
    return objectInRange(ctx, o) && sphereInFrustum(f, o.center, o.radius + kViewCullMargin * ctx->cellSize);
}

// Uploads the changed VBOs again, before the frame's two passes (without
// allocating, but for the first upload after loading). Those out of range
// wait until they come in (gpuDirty stays).
void prepareObjects(RzContext* ctx) {
    for (Object& o : ctx->objects) {
        if (o.alive && objectInRange(ctx, o)) prepareWheels(ctx, o);
        if (!drawable(o) || !objectInRange(ctx, o)) continue;
        const bool rebuild = o.gpuDirty;
        if (o.buffersDirty) {
            uploadBuffers(o);
            o.buffersDirty = false;
        }
        if (rebuild) {
            uploadObject(ctx, o);
            o.gpuDirty = false;
        }
        if (o.positionsDirty) {
            uploadPositions(o);
            if (!rebuild) uploadLines(ctx, o);
            o.positionsDirty = false;
        }
    }
}

// Shadow pass: only the geometry (the depth program is already bound).
// onlyId >= 0: only that object; skipId >= 0: all but it.
// Those past the fog or outside the map's box are left out.
void drawObjectsDepth(RzContext* ctx, const Mat4& lightViewProj, int32_t onlyId, int32_t skipId) {
    const Frustum f = frustumOf(lightViewProj);
    auto selected = [&](int32_t id, const Object& o) {
        return drawable(o) && !(onlyId >= 0 && id != onlyId) && id != skipId && objectInView(ctx, o, f);
    };
    // Objects: a program of their own (position from the block)
    glUseProgram(ctx->objectDepthProgram.program);
    setMatrix(ctx->objectDepthProgram.lightViewProj, lightViewProj);
    for (int32_t id = 0; id < int32_t(ctx->objects.size()); ++id) {
        const Object& o = ctx->objects[id];
        if (!selected(id, o)) continue;
        bindObjectBuffers(o);
        glBindVertexArray(o.vao);
        glDrawArrays(GL_TRIANGLES, 0, o.drawVertexCount());
    }
    // Wheels: the common depth program (stays bound for the caller)
    glUseProgram(ctx->depthProgram.program);
    for (int32_t id = 0; id < int32_t(ctx->objects.size()); ++id) {
        const Object& o = ctx->objects[id];
        if (selected(id, o)) drawWheelsDepth(ctx, o);
    }
}

// Fixed culling, in the legacy's convention: drops the faces that are
// anticlockwise on the screen (glFrontFace sets front = visually anticlockwise,
// so it is GL_FRONT that goes).
void drawObjects(RzContext* ctx, const Mat4& viewProj) {
    const Frustum f = frustumOf(viewProj);
    bool programBound = false;
    for (Object& o : ctx->objects) {
        if (!drawable(o) || !objectInView(ctx, o, f)) continue;
        const ObjectProgram& prog = ctx->objectProgram;
        if (!programBound) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glUseProgram(prog.program);
            setMatrix(prog.viewProj, viewProj);
            bindShadowMaps(ctx, prog.shadow);           // leaves unit 0 active
            bindFog(ctx, prog.fog);
            glUniform1i(prog.fromBuffer, 1);
            programBound = true;
        }
        bindObjectBuffers(o);
        // Own texture or fallback
        const GLuint  texture = o.texture ? o.texture : ctx->fallbackTex;
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(prog.texelMode, objectTextureGlass(ctx, o) ? kTexelsOpaqueGlass : kTexelsOpaque);
        glBindVertexArray(o.vao);
        glDrawArrays(GL_TRIANGLES, 0, o.drawVertexCount());
    }
    if (programBound) glUniform1i(ctx->objectProgram.fromBuffer, 0);   // the wheels' faces: position directly
}

bool objectTextureGlass(const RzContext* ctx, const Object& o) {
    return o.texture ? o.textureGlass : ctx->fallbackGlass;
}

// The textures' glass texels (rz_internal.h, kFirstGlassIndex): again the
// objects whose texture has them, in the objects' program in kTexelsGlass, with
// the glass's state (rz_glass.cpp: highlight + destination x grey, writing
// neither depth nor alpha). They cast no shadow (the whole object's shadow covers it already).
void drawTexturedGlass(RzContext* ctx, const Mat4& viewProj) {
    const Frustum f = frustumOf(viewProj);
    const ObjectProgram& prog = ctx->objectProgram;
    bool programBound = false;
    for (Object& o : ctx->objects) {
        if (!drawable(o) || !objectTextureGlass(ctx, o) || !objectInView(ctx, o, f)) continue;
        if (!programBound) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glUseProgram(prog.program);
            setMatrix(prog.viewProj, viewProj);
            bindShadowMaps(ctx, prog.shadow);           // leaves unit 0 active
            bindFog(ctx, prog.fog);
            glUniform1i(prog.fromBuffer, 1);
            glUniform1i(prog.texelMode, kTexelsGlass);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_SRC_ALPHA);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
            glDepthMask(GL_FALSE);
            programBound = true;
        }
        bindObjectBuffers(o);
        glBindTexture(GL_TEXTURE_2D, o.texture ? o.texture : ctx->fallbackTex);
        glBindVertexArray(o.vao);
        glDrawArrays(GL_TRIANGLES, 0, o.drawVertexCount());
    }
    if (programBound) {
        glUniform1i(prog.fromBuffer, 0);
        glUniform1i(prog.texelMode, kTexelsOpaque);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_BLEND);
    }
}

void markObjectsDirty(RzContext* ctx) {
    for (Object& o : ctx->objects) o.gpuDirty = o.gpuDirty || o.alive;
}

void destroyAllObjects(RzContext* ctx) {
    for (Object& o : ctx->objects) {
        if (o.alive) freeObject(o);
    }
    ctx->objects.clear();
}

} // namespace rz

extern "C" {

int32_t rzCreateObject(RzContext* ctx, int32_t vertexCount, int32_t* outId) {
    if (!ctx || !outId) return recordError(ctx, "rzCreateObject", RZ_ERR_INVALID_ARG);
    *outId = -1;
    if (vertexCount < 1) return recordError(ctx, "rzCreateObject", RZ_ERR_INVALID_ARG);
    if (vertexCount > kMaxObjectVertices) return recordError(ctx, "rzCreateObject", RZ_ERR_SIZE);

    // A free slot, or a new one at the end (loading step)
    int32_t id = 0;
    while (id < int32_t(ctx->objects.size()) && ctx->objects[id].alive) ++id;
    if (id == int32_t(ctx->objects.size())) ctx->objects.emplace_back();

    Object& o = ctx->objects[id];
    o.world.assign(size_t(vertexCount), Vec3{ 0.0f, 0.0f, 0.0f });
    o.positionStaging.assign(size_t(vertexCount) * 4, 0.0f);

    // Vertex buffer (empty until the first polygon): corners (ObjectVertex)
    glGenVertexArrays(1, &o.vao);
    glGenBuffers(1, &o.vbo);
    glBindVertexArray(o.vao);
    glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
    setObjectBufferLayout();
    glBindVertexArray(0);

    // The block: positions (at each update) and the polygons' indices (resizeObjectBuffers)
    glGenBuffers(1, &o.dataBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, o.dataBuffer);
    glBufferData(GL_UNIFORM_BUFFER, kObjectDataSize, nullptr, GL_DYNAMIC_DRAW);

    o.alive      = true;
    o.positioned = false;
    o.gpuDirty   = false;

    if (glGetError() != GL_NO_ERROR) {
        freeObject(o);
        return recordError(ctx, "rzCreateObject", RZ_ERR_GL);
    }
    *outId = id;
    return RZ_OK;
}

int32_t rzAddObjectPolygon(RzContext* ctx, int32_t id,
                           const uint16_t* indices, int32_t count,
                           int32_t paletteIndex) {
    return recordError(ctx, "rzAddObjectPolygon", addPolygon(ctx, id, indices, nullptr, nullptr, count, paletteIndex));
}

int32_t rzAddObjectShadedPolygon(RzContext* ctx, int32_t id,
                                 const uint16_t* indices, const uint8_t* paletteIndices, int32_t count) {
    if (!indices || !paletteIndices) return recordError(ctx, "rzAddObjectShadedPolygon", RZ_ERR_INVALID_ARG);
    return recordError(ctx, "rzAddObjectShadedPolygon", addPolygon(ctx, id, indices, nullptr, paletteIndices, count, -1));
}

int32_t rzAddObjectTexturedPolygon(RzContext* ctx, int32_t id,
                                   const RzTexVertex* corners, int32_t count) {
    if (!corners) return recordError(ctx, "rzAddObjectTexturedPolygon", RZ_ERR_INVALID_ARG);
    return recordError(ctx, "rzAddObjectTexturedPolygon", addPolygon(ctx, id, nullptr, corners, nullptr, count, -1));
}

int32_t rzAddObjectLine(RzContext* ctx, int32_t id, uint16_t a, uint16_t b,
                        float thickness, int32_t paletteIndex) {
    if (!validId(ctx, id)) return recordError(ctx, "rzAddObjectLine", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];
    if (a >= o.vertexCount() || b >= o.vertexCount()) return recordError(ctx, "rzAddObjectLine", RZ_ERR_INVALID_ARG);
    if (!(thickness > 0.0f && thickness < 1.0e6f)) return recordError(ctx, "rzAddObjectLine", RZ_ERR_INVALID_ARG);   // catches NaN
    if (paletteIndex < 0 || paletteIndex > 255) return recordError(ctx, "rzAddObjectLine", RZ_ERR_INVALID_ARG);
    if (a == b) return RZ_OK;                                  // degenerate: ignored
    if (o.lines.size() >= 65535) return recordError(ctx, "rzAddObjectLine", RZ_ERR_SIZE);
    o.lines.push_back({ a, b, int16_t(paletteIndex), 0.5f * thickness * ctx->cellSize });
    resizeObjectBuffers(o);
    return RZ_OK;
}

int32_t rzUpdateObjectVertices(RzContext* ctx, int32_t id,
                               const RzVertex* vertices) {
    if (!validId(ctx, id) || !vertices) return recordError(ctx, "rzUpdateObjectVertices", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];
    for (int32_t i = 0; i < o.vertexCount(); ++i) {
        if (!validCoord(vertices[i].x) || !validCoord(vertices[i].y) || !validCoord(vertices[i].z)) {
            return recordError(ctx, "rzUpdateObjectVertices", RZ_ERR_INVALID_ARG);
        }
    }
    loadPositions(ctx, o, vertices);
    o.positioned = true;
    o.positionsDirty = true;
    o.glassDirty = !o.glassStaging.empty();
    return RZ_OK;
}

int32_t rzDestroyObject(RzContext* ctx, int32_t id) {
    if (!validId(ctx, id)) return recordError(ctx, "rzDestroyObject", RZ_ERR_INVALID_ARG);
    freeObject(ctx->objects[id]);
    return RZ_OK;
}

} // extern "C"
