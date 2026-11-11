// The objects' wheels: four tyres with rounded shoulders and, optionally,
// textured faces (rzSetObjectWheelFaces).
//
// rzSetObjectWheels: for each wheel, the object's vertex at the centre of the
// hub, whether it is a front or a rear one and the diameter (tiles). There
// must be two front ones and two rear ones.
// rzUpdateObjectWheels: steering angle of the two front ones (radians;
// positive turns left, anticlockwise seen from above). The rear ones straight.
//
// Each wheel's mesh is built once, in its own frame (x forward, y up, z axle,
// centre at the hub), and only redone when the wheels or the faces change.
// Each frame the CPU only works out a matrix per wheel (wheelModel: the hub's
// position, steering and turn), applied in the vertex shader (uModel).
//
// The car's frame, redone each frame from the four hubs:
//   forward f = middle of the front ones − middle of the rear ones;
//   up      u = perpendicular to f and to the line between the wheels, pointing to +y;
//   right   r = f x u.
// Each wheel: diameter d and width kWheelWidth x d, axle = r (in the front
// ones, turned around u by the steering), kWheelSegments sides.
//
// Drawing: the tyre with the terrain's program in the untextured mode (lit
// flat colour, shadows and fog for free), the faces with the objects'; no
// culling (closed cylinder: the depth sorts it out). They cast a shadow (maps
// 1 and 2, together with the object). One glDrawArrays per wheel (the matrix changes).

#include <cmath>

#include "rz_internal.h"

namespace rz {

namespace {

constexpr int32_t kWheelVertices = kWheelSideVerts;   // flat VBO per wheel (shell + black caps)

bool validId(const RzContext* ctx, int32_t id) {
    return ctx && id >= 0 && id < int32_t(ctx->objects.size()) && ctx->objects[id].alive;
}

Vec3 scale(Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }
Vec3 normalized(Vec3 v) {
    const float l2 = dot(v, v);
    return l2 > 0.0f ? scale(v, 1.0f / sqrtf(l2)) : Vec3{ 0.0f, 0.0f, 0.0f };
}

// A tyre's triangle with a normal per vertex (smooth shading, not flat).
void emitSmooth(TerrainVertex*& out, Vec3 a, Vec3 b, Vec3 c, Vec3 na, Vec3 nb, Vec3 nc) {
    *out++ = { a.x, a.y, a.z, kWheelColor, 0, 0, 0, 0, packNormal(na) };
    *out++ = { b.x, b.y, b.z, kWheelColor, 0, 0, 0, 0, packNormal(nb) };
    *out++ = { c.x, c.y, c.z, kWheelColor, 0, 0, 0, 0, packNormal(nc) };
}

// A corner of a face's disc: the position p and, in the atlas's square, the
// point (cx, cy) of the unit circle (centre = 0). face[]: u0, v0, u1, v1. v grows
// downwards in the texture, so the disc's "up" (cy > 0) is at the top (smaller v);
// the inner face is mirrored in u (it is seen from the other side).
GpuVertex capCorner(Vec3 p, uint32_t normal, const float face[4], float cx, float cy, bool mirror) {
    const float uc = 0.5f * (face[0] + face[2]), vc = 0.5f * (face[1] + face[3]);
    const float hu = 0.5f * (face[2] - face[0]), hv = 0.5f * (face[3] - face[1]);
    const float u = uc + (mirror ? -cx : cx) * hu;
    const float v = vc - cy * hv;
    return { p.x, p.y, p.z, normal, u, v };
}

void emitCap(GpuVertex*& out, Vec3 center, Vec3 a, Vec3 b, uint32_t normal,
             const float face[4], float ca, float sa, float cb, float sb, bool mirror) {
    *out++ = capCorner(center, normal, face, 0.0f, 0.0f, mirror);
    *out++ = capCorner(a, normal, face, ca, sa, mirror);
    *out++ = capCorner(b, normal, face, cb, sb, mirror);
}

// Any textured triangle (the sidewall is a ring, not a fan): each corner's UVs
// come from the point (cx, cy) on the square's circle (|cx|,|cy| <= 1).
void emitTexTri(GpuVertex*& out, Vec3 pa, Vec3 pb, Vec3 pc, uint32_t normal, const float face[4],
                float ax, float ay, float bx, float by, float cx, float cy, bool mirror) {
    *out++ = capCorner(pa, normal, face, ax, ay, mirror);
    *out++ = capCorner(pb, normal, face, bx, by, mirror);
    *out++ = capCorner(pc, normal, face, cx, cy, mirror);
}

// The car's frame from the four hubs (see the top of the file):
// fwd/axis [0] rear (straight), [1] front (steered). The first time, it fixes
// the direction of "up" and which cap of each wheel is the outer one.
void wheelFrame(Object& o, Vec3* up, Vec3 fwd[2], Vec3 axis[2]) {
    Vec3 hub[4];
    for (int32_t i = 0; i < 4; ++i) hub[i] = o.world[o.wheelVertex[i]];
    Vec3 frontMid = { 0, 0, 0 }, rearMid = { 0, 0, 0 };
    Vec3 fr[2], rr[2];
    int32_t nf = 0, nr = 0;
    for (int32_t i = 0; i < 4; ++i) {
        if (o.wheelFront[i]) { frontMid = frontMid + scale(hub[i], 0.5f); fr[nf++] = hub[i]; }
        else                 { rearMid  = rearMid  + scale(hub[i], 0.5f); rr[nr++] = hub[i]; }
    }
    const Vec3 f = normalized(frontMid - rearMid);
    const Vec3 carMid = scale(frontMid + rearMid, 0.5f);   // the car's centre (to know the outer face)
    // line between the wheels (the two pairs, the same way)
    Vec3 axle = fr[1] - fr[0];
    Vec3 axle2 = rr[1] - rr[0];
    if (dot(axle, axle2) < 0.0f) axle2 = scale(axle2, -1.0f);
    axle = axle + axle2;
    // The wheels' "up": fixes the direction the 1st time (car upright, u points
    // to +y) and then follows the body continuously, WITHOUT pulling it to +y
    // each frame. That way turning over does not flip the axis (nor, with it, the
    // caps' identity): u turns with the car instead of jumping.
    Vec3 u = normalized(cross(axle, f));
    if (!o.wheelOuterResolved) o.wheelUpSign = (u.y >= 0.0f) ? 1.0f : -1.0f;
    u = scale(u, o.wheelUpSign);
    if (dot(u, u) == 0.0f) u = { 0.0f, 1.0f, 0.0f };

    const float s = sinf(o.wheelSteer), c = cosf(o.wheelSteer);
    fwd[0] = f;
    fwd[1] = scale(f, c) + scale(cross(u, f), s);
    axis[0] = normalized(cross(fwd[0], u));
    axis[1] = normalized(cross(fwd[1], u));
    *up = u;

    // Decides once (and fixes) which cap of each wheel is the outer one, by the
    // STRAIGHT axle (axis[0], the car's right side), not by the steered one: that
    // way the steering, however large, no longer swaps inner and outer.
    if (!o.wheelOuterResolved) {
        for (int32_t i = 0; i < 4; ++i)
            o.wheelPlusOuter[i] = dot(hub[i] - carMid, axis[0]) > 0.0f ? 1 : 0;
        o.wheelOuterResolved = true;
        o.wheelsDirty = true;            // the mesh depends on which cap is the outer one
    }
}

// Builds the mesh of the four wheels, each in its own frame (x forward, y up,
// z axle, centre at the hub, no turn), and uploads the VBOs. Only at loading
// and when wheels/faces change: position, steering and turn come from wheelModel.
// The tyre (the side) goes to the flat VBO; the two caps go to the faces' VBO
// with the object's texture when there are faces (rzSetObjectWheelFaces), or to
// the flat VBO, black, when there are none.
void buildWheels(Object& o) {
    // unit ring (closed: ring[kWheelSegments] = ring[0])
    float ringC[kWheelSegments + 1], ringS[kWheelSegments + 1];
    for (int32_t k = 0; k <= kWheelSegments; ++k) {
        const float a = 2.0f * kPi * float(k % kWheelSegments) / float(kWheelSegments);
        ringC[k] = cosf(a);
        ringS[k] = sinf(a);
    }
    const Vec3 fw = { 1.0f, 0.0f, 0.0f }, u = { 0.0f, 1.0f, 0.0f }, ax = { 0.0f, 0.0f, 1.0f };
    const Vec3 p = { 0.0f, 0.0f, 0.0f };
    const uint32_t nOut = packNormal(ax), nIn = packNormal(scale(ax, -1.0f));

    const bool textured = o.wheelFacesSet;
    TerrainVertex* const flat0 = o.wheelStaging.data();
    GpuVertex*     const cap0  = o.wheelCapStaging.data();
    TerrainVertex* out = flat0;
    GpuVertex*     cap = cap0;
    for (int32_t i = 0; i < 4; ++i) {
        TerrainVertex* const outStart = out;
        GpuVertex*     const capStart = cap;
        const bool plusIsOuter = o.wheelPlusOuter[i] != 0;   // is the +axle cap the outer one?

        // The tyre's measures (world), from outside in along the radius: tread
        // (straight, radius R) -> rounded shoulder -> textured sidewall (a ring
        // `sideW` wide) -> the rim's recess (sunken middle `recess`). The shell
        // (tread + shoulder + the recess's wall) is of a fixed colour; the
        // sidewall and the recessed rim are textured.
        const float d      = o.wheelDiameter[i];
        const float R      = 0.5f * d;                       // radius of the tread
        const float w      = 0.5f * kWheelWidth * d;         // half width
        const float sh     = 0.12f * R;                      // small shoulder: only rounds the edge
        const float wt     = w - sh;                         // half width of the straight tread
        const float rimR   = R - sh;                         // radius at the edge (end of the shoulder)
        const float sideW  = 0.35f * R;                      // width of the textured sidewall
        const float sideIn = rimR - sideW;                   // radius where the wall turns into the rim's recess
        const float recess = 0.30f * w;                      // depth of the rim's recess
        const float frIn   = sideIn / rimR;                  // fraction of the face's radius (UV) at the inner edge

        // Profile of the fixed colour shell (a = axial, r = radius, n = normal in the
        // radial/axial plane): shoulder (-), straight tread, shoulder (+). Ends at ±w
        // (the edge); the textured face closes it from there inwards.
        constexpr int32_t kMaxSamples = 2 * kWheelShoulderSteps + 4;
        float pa[kMaxSamples], pr[kMaxSamples], pnr[kMaxSamples], pna[kMaxSamples];
        int32_t ns = 0;
        auto push = [&](float a, float r, float nr, float na) {
            pa[ns] = a; pr[ns] = r; pnr[ns] = nr; pna[ns] = na; ++ns;
        };
        for (int32_t s = 0; s <= kWheelShoulderSteps; ++s) { // shoulder (-): θ from π/2 (edge) to 0
            const float th = 0.5f * kPi * float(kWheelShoulderSteps - s) / float(kWheelShoulderSteps);
            push(-(wt + sh * sinf(th)), rimR + sh * cosf(th), cosf(th), -sinf(th));
        }
        push(wt, R, 1.0f, 0.0f);                             // straight tread (the other edge)
        for (int32_t s = 1; s <= kWheelShoulderSteps; ++s) { // shoulder (+): θ from 0 to π/2 (edge)
            const float th = 0.5f * kPi * float(s) / float(kWheelShoulderSteps);
            push(wt + sh * sinf(th), rimR + sh * cosf(th), cosf(th), sinf(th));
        }

        // The outer face uses the outer square (no mirror), the inner one the
        // inner (mirrored in u). The UV is fixed to the mesh, so the face's
        // picture turns with the wheel (the turn is in wheelModel).
        const float* fPlus  = plusIsOuter ? o.wheelFaceOuter : o.wheelFaceInner;   // +axle cap
        const float* fMinus = plusIsOuter ? o.wheelFaceInner : o.wheelFaceOuter;   // -axle cap
        const bool mPlus = !plusIsOuter, mMinus = plusIsOuter;

        for (int32_t k = 0; k < kWheelSegments; ++k) {
            const Vec3 rad0 = scale(fw, ringC[k]) + scale(u, ringS[k]);           // radial direction (unit)
            const Vec3 rad1 = scale(fw, ringC[k + 1]) + scale(u, ringS[k + 1]);
            // Fixed colour shell: a band per pair of profile points, with normals
            // per vertex (rounded shoulders with smooth light).
            for (int32_t j = 0; j + 1 < ns; ++j) {
                const Vec3 axA = scale(ax, pa[j]),     axB = scale(ax, pa[j + 1]);
                const Vec3 A0 = p + axA + scale(rad0, pr[j]),     A1 = p + axA + scale(rad1, pr[j]);
                const Vec3 B0 = p + axB + scale(rad0, pr[j + 1]), B1 = p + axB + scale(rad1, pr[j + 1]);
                const Vec3 nA0 = scale(rad0, pnr[j]) + scale(ax, pna[j]);
                const Vec3 nA1 = scale(rad1, pnr[j]) + scale(ax, pna[j]);
                const Vec3 nB0 = scale(rad0, pnr[j + 1]) + scale(ax, pna[j + 1]);
                const Vec3 nB1 = scale(rad1, pnr[j + 1]) + scale(ax, pna[j + 1]);
                emitSmooth(out, A0, B0, B1, nA0, nB0, nB1);
                emitSmooth(out, A0, B1, A1, nA0, nB1, nA1);
            }
            if (textured) {
                // The two faces: sidewall (textured ring, in the ±w plane) and
                // recessed rim (textured fan, sunk at ±(w-recess)); between
                // them, the recess's wall, of a fixed colour (radius sideIn).
                for (int32_t side = 0; side < 2; ++side) {
                    const float sgn = side == 0 ? 1.0f : -1.0f;            // + axle, then - axle
                    const uint32_t nf = side == 0 ? nOut : nIn;
                    const float* face = side == 0 ? fPlus : fMinus;
                    const bool mir = side == 0 ? mPlus : mMinus;
                    const Vec3 cFront = p + scale(ax, sgn * w);            // the sidewall's plane
                    const Vec3 cBack  = p + scale(ax, sgn * (w - recess)); // the recessed rim's plane
                    // sidewall: ring rimR -> sideIn, in the edge's plane
                    const Vec3 lo0 = cFront + scale(rad0, rimR),  lo1 = cFront + scale(rad1, rimR);
                    const Vec3 li0 = cFront + scale(rad0, sideIn), li1 = cFront + scale(rad1, sideIn);
                    emitTexTri(cap, lo0, lo1, li1, nf, face,
                               ringC[k], ringS[k], ringC[k + 1], ringS[k + 1],
                               frIn * ringC[k + 1], frIn * ringS[k + 1], mir);
                    emitTexTri(cap, lo0, li1, li0, nf, face,
                               ringC[k], ringS[k], frIn * ringC[k + 1], frIn * ringS[k + 1],
                               frIn * ringC[k], frIn * ringS[k], mir);
                    // the recess's wall (fixed colour): a cylinder at sideIn, from the edge to the bottom
                    const Vec3 bi0 = cBack + scale(rad0, sideIn), bi1 = cBack + scale(rad1, sideIn);
                    const Vec3 wn0 = scale(rad0, -1.0f), wn1 = scale(rad1, -1.0f);   // normal pointing in
                    emitSmooth(out, li0, bi0, bi1, wn0, wn0, wn1);
                    emitSmooth(out, li0, bi1, li1, wn0, wn1, wn1);
                    // recessed rim: textured fan from the centre to the inner edge
                    emitCap(cap, cBack, bi0, bi1, nf, face,
                            frIn * ringC[k], frIn * ringS[k], frIn * ringC[k + 1], frIn * ringS[k + 1], mir);
                }
            } else {
                // No texture: flat black caps, radius rimR, in the edge's plane.
                const Vec3 cP = p + scale(ax, w), cM = p - scale(ax, w);
                const Vec3 oP0 = cP + scale(rad0, rimR), oP1 = cP + scale(rad1, rimR);
                const Vec3 iM0 = cM + scale(rad0, rimR), iM1 = cM + scale(rad1, rimR);
                emitSmooth(out, cP, oP1, oP0, ax, ax, ax);
                const Vec3 axn = scale(ax, -1.0f);
                emitSmooth(out, cM, iM0, iM1, axn, axn, axn);
            }
        }
        o.wheelFlatFirst[i] = int32_t(outStart - flat0);
        o.wheelFlatCount[i] = int32_t(out - outStart);
        o.wheelCapFirst[i]  = int32_t(capStart - cap0);
        o.wheelCapCount[i]  = int32_t(cap - capStart);
    }
    o.wheelFlatVerts = int32_t(out - flat0);
    o.wheelCapVerts  = int32_t(cap - cap0);

    glBindBuffer(GL_ARRAY_BUFFER, o.wheelVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(size_t(o.wheelFlatVerts) * sizeof(TerrainVertex)),
                    o.wheelStaging.data());
    if (o.wheelCapVerts > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, o.wheelCapVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(size_t(o.wheelCapVerts) * sizeof(GpuVertex)),
                        o.wheelCapStaging.data());
    }
}

} // namespace

void freeWheels(Object& o) {
    if (o.wheelVbo) glDeleteBuffers(1, &o.wheelVbo);
    if (o.wheelVao) glDeleteVertexArrays(1, &o.wheelVao);
    if (o.wheelCapVbo) glDeleteBuffers(1, &o.wheelCapVbo);
    if (o.wheelCapVao) glDeleteVertexArrays(1, &o.wheelCapVao);
    o.wheelVbo = 0;
    o.wheelVao = 0;
    o.wheelCapVbo = 0;
    o.wheelCapVao = 0;
}

// Before the frame's passes (together with prepareObjects). Moves each wheel's
// turn on for this frame, by its speed: angle = distance gone / radius (each wheel
// by its diameter; positive = forwards). Redoes the local mesh only if
// wheels/faces changed; every frame, only the four matrices (wheelModel).
void prepareWheels(RzContext* ctx, Object& o) {
    if (o.wheelStaging.empty() || !o.positioned) return;
    for (int32_t i = 0; i < 4; ++i) {
        const float dist = o.wheelSpeed[i] * ctx->cellSize * ctx->frameStep;   // tiles/frame -> world, in this frame
        o.wheelRoll[i] -= dist / (0.5f * o.wheelDiameter[i]);   // -: the top goes forwards
        o.wheelRoll[i] = fmodf(o.wheelRoll[i], 2.0f * kPi);
    }
    Vec3 u, fwd[2], axis[2];
    wheelFrame(o, &u, fwd, axis);
    if (o.wheelsDirty) {
        buildWheels(o);
        o.wheelsDirty = false;
    }
    // The matrix's columns: x (forward) and y (up) turned by the wheel's turn
    // around the z axis; z = axle; translation = hub
    for (int32_t i = 0; i < 4; ++i) {
        const int32_t which = o.wheelFront[i];
        const Vec3 f = fwd[which], ax = axis[which], hub = o.world[o.wheelVertex[i]];
        const float cr = cosf(o.wheelRoll[i]), sr = sinf(o.wheelRoll[i]);
        const Vec3 x = scale(f, cr) + scale(u, sr);
        const Vec3 y = scale(f, -sr) + scale(u, cr);
        Mat4& m = o.wheelModel[i];
        m = Mat4::identity();
        m[0, 0] = x.x; m[0, 1] = y.x; m[0, 2] = ax.x; m[0, 3] = hub.x;
        m[1, 0] = x.y; m[1, 1] = y.y; m[1, 2] = ax.y; m[1, 3] = hub.y;
        m[2, 0] = x.z; m[2, 1] = y.z; m[2, 2] = ax.z; m[2, 3] = hub.z;
    }
}

// Shadow pass: the depth program is already bound. The position is in
// attribute 0 in both VBOs (tyre and faces), so both cast a shadow.
void drawWheelsDepth(const RzContext* ctx, const Object& o) {
    if (o.wheelStaging.empty() || !o.positioned) return;
    const GLint model = ctx->depthProgram.model;
    for (int32_t i = 0; i < 4; ++i) {
        setMatrix(model, o.wheelModel[i]);
        glBindVertexArray(o.wheelVao);
        glDrawArrays(GL_TRIANGLES, o.wheelFlatFirst[i], o.wheelFlatCount[i]);
        if (o.wheelCapCount[i] > 0) {
            glBindVertexArray(o.wheelCapVao);
            glDrawArrays(GL_TRIANGLES, o.wheelCapFirst[i], o.wheelCapCount[i]);
        }
    }
    setMatrix(model, Mat4::identity());
}

// Main pass, after the objects: the tyre (side), the terrain's program
// without texture. The faces go in drawWheelFaces. No culling (the depth sorts it out).
void drawWheels(RzContext* ctx, const Mat4& viewProj) {
    const Frustum f = frustumOf(viewProj);
    bool bound = false;
    const TerrainProgram& t = ctx->terrainProgram;
    for (const Object& o : ctx->objects) {
        if (!o.alive || !o.positioned || o.wheelStaging.empty() || !objectInView(ctx, o, f)) continue;
        if (!bound) {
            glUseProgram(t.program);
            setMatrix(t.viewProj, viewProj);
            bindShadowMaps(ctx, t.shadow);
            bindFog(ctx, t.fog);
            glUniform1i(t.textured, 0);
            glUniform1i(t.smooth, 1);            // tyre: per-pixel light (smooth normals)
            glDisable(GL_CULL_FACE);
            bound = true;
        }
        glBindVertexArray(o.wheelVao);
        for (int32_t i = 0; i < 4; ++i) {
            setMatrix(t.model, o.wheelModel[i]);
            glDrawArrays(GL_TRIANGLES, o.wheelFlatFirst[i], o.wheelFlatCount[i]);
        }
    }
    if (bound) {                                 // restores the terrain's: flat, no model
        glUniform1i(t.smooth, 0);
        setMatrix(t.model, Mat4::identity());
    }
}

// The wheels' faces (discs): the objects' program, with the object's texture,
// after drawWheels. No culling, as the tyre.
void drawWheelFaces(RzContext* ctx, const Mat4& viewProj) {
    const Frustum f = frustumOf(viewProj);
    bool bound = false;
    const ObjectProgram& prog = ctx->objectProgram;
    for (const Object& o : ctx->objects) {
        if (!o.alive || !o.positioned || o.wheelCapVerts <= 0 || !objectInView(ctx, o, f)) continue;
        if (!bound) {
            glUseProgram(prog.program);
            setMatrix(prog.viewProj, viewProj);
            bindShadowMaps(ctx, prog.shadow);           // leaves unit 0 active
            bindFog(ctx, prog.fog);
            // aPolygon (3) and aColor (4) have no array here: aPolygon's fixed
            // value has to be of the shader's type (ivec3), otherwise WebGL does not
            // draw (uFromBuffer is 0), and aColor's says "textured"
            glVertexAttribI4i(3, -1, 0, 0, 0);
            glVertexAttrib4f(4, 0.0f, 0.0f, 0.0f, float(kCornerTextured));
            glDisable(GL_CULL_FACE);
            bound = true;
        }
        glBindTexture(GL_TEXTURE_2D, o.texture ? o.texture : ctx->fallbackTex);
        glUniform1i(prog.texelMode, objectTextureGlass(ctx, o) ? kTexelsOpaqueGlass : kTexelsOpaque);
        glBindVertexArray(o.wheelCapVao);
        for (int32_t i = 0; i < 4; ++i) {
            setMatrix(prog.model, o.wheelModel[i]);
            glDrawArrays(GL_TRIANGLES, o.wheelCapFirst[i], o.wheelCapCount[i]);
        }
    }
    if (bound) setMatrix(prog.model, Mat4::identity());
}

} // namespace rz

using namespace rz;

extern "C" {

int32_t rzSetObjectWheels(RzContext* ctx, int32_t id, const RzWheel* wheels) {
    if (!validId(ctx, id) || !wheels) return recordError(ctx, "rzSetObjectWheels", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];
    int32_t fronts = 0;
    for (int32_t i = 0; i < 4; ++i) {
        if (wheels[i].hubVertex >= o.vertexCount()) return recordError(ctx, "rzSetObjectWheels", RZ_ERR_INVALID_ARG);
        if (!(wheels[i].diameter > 0.0f && wheels[i].diameter < 1.0e6f)) return recordError(ctx, "rzSetObjectWheels", RZ_ERR_INVALID_ARG);
        if (wheels[i].front) ++fronts;
    }
    if (fronts != 2) return recordError(ctx, "rzSetObjectWheels", RZ_ERR_INVALID_ARG);   // two front ones and two rear ones

    const float cs = ctx->cellSize;
    o.wheelSteer = 0.0f;
    o.wheelOuterResolved = false;      // decides inner/outer again for the new wheels
    for (int32_t i = 0; i < 4; ++i) {
        o.wheelVertex[i]   = wheels[i].hubVertex;
        o.wheelFront[i]    = wheels[i].front ? 1 : 0;
        o.wheelDiameter[i] = wheels[i].diameter * cs;
        o.wheelRoll[i]     = 0.0f;
        o.wheelSpeed[i]    = 0.0f;
    }
    if (!o.wheelVao) {                                          // loading: allocates once
        o.wheelStaging.resize(size_t(4) * kWheelVertices);     // tyre + (black faces, if no texture)
        glGenVertexArrays(1, &o.wheelVao);
        glGenBuffers(1, &o.wheelVbo);
        glBindVertexArray(o.wheelVao);
        glBindBuffer(GL_ARRAY_BUFFER, o.wheelVbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(o.wheelStaging.size() * sizeof(TerrainVertex)),
                     nullptr, GL_DYNAMIC_DRAW);
        setTerrainVertexLayout();

        o.wheelCapStaging.resize(size_t(4) * kWheelCapVerts);  // textured faces
        glGenVertexArrays(1, &o.wheelCapVao);
        glGenBuffers(1, &o.wheelCapVbo);
        glBindVertexArray(o.wheelCapVao);
        glBindBuffer(GL_ARRAY_BUFFER, o.wheelCapVbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(o.wheelCapStaging.size() * sizeof(GpuVertex)),
                     nullptr, GL_DYNAMIC_DRAW);
        setObjectVertexLayout();
        glBindVertexArray(0);
    }
    o.wheelsDirty = true;
    return recordError(ctx, "rzSetObjectWheels", glGetError() == GL_NO_ERROR ? RZ_OK : RZ_ERR_GL);
}

int32_t rzSetObjectWheelFaces(RzContext* ctx, int32_t id,
                              const RzWheelFace* outer, const RzWheelFace* inner) {
    if (!validId(ctx, id) || !outer || !inner) return recordError(ctx, "rzSetObjectWheelFaces", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];
    if (o.wheelStaging.empty()) return recordError(ctx, "rzSetObjectWheelFaces", RZ_ERR_INVALID_ARG);   // without rzSetObjectWheels
    const float in[8] = { outer->u0, outer->v0, outer->u1, outer->v1,
                          inner->u0, inner->v0, inner->u1, inner->v1 };
    for (float uv : in) if (!std::isfinite(uv)) return recordError(ctx, "rzSetObjectWheelFaces", RZ_ERR_INVALID_ARG);
    for (int32_t i = 0; i < 4; ++i) {
        o.wheelFaceOuter[i] = in[i];
        o.wheelFaceInner[i] = in[i + 4];
    }
    o.wheelFacesSet = true;
    o.wheelsDirty = true;
    return RZ_OK;
}

int32_t rzSetObjectWheelSpin(RzContext* ctx, int32_t id, const float* speeds) {
    if (!validId(ctx, id) || !speeds) return recordError(ctx, "rzSetObjectWheelSpin", RZ_ERR_INVALID_ARG);
    for (int32_t i = 0; i < 4; ++i) if (!std::isfinite(speeds[i])) return recordError(ctx, "rzSetObjectWheelSpin", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];
    if (o.wheelStaging.empty()) return recordError(ctx, "rzSetObjectWheelSpin", RZ_ERR_INVALID_ARG);   // without rzSetObjectWheels
    for (int32_t i = 0; i < 4; ++i) o.wheelSpeed[i] = speeds[i];
    return RZ_OK;
}

int32_t rzUpdateObjectWheels(RzContext* ctx, int32_t id, float steer) {
    if (!validId(ctx, id) || !std::isfinite(steer)) return recordError(ctx, "rzUpdateObjectWheels", RZ_ERR_INVALID_ARG);
    Object& o = ctx->objects[id];
    if (o.wheelStaging.empty()) return recordError(ctx, "rzUpdateObjectWheels", RZ_ERR_INVALID_ARG);   // without rzSetObjectWheels
    o.wheelSteer = steer;                // only the matrix changes (prepareWheels)
    return RZ_OK;
}

} // extern "C"
