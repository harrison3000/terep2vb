// The context's GPU resources (programs) and the frame.

#include <cstdio>

#include "rz_internal.h"

namespace rz {

namespace {

// Programs: all are asked for at once at the start of createRenderer
// (startPrograms), and each one's status is only asked when it is used
// (linkProgram). Asking right after the link makes WebGL wait for the
// compilation one by one; this way the driver compiles them all together.
// The shaders' texts are in the page (web/shaders.js), asked for by the
// file's name, with the quality's #defines (CASCADES).
struct PendingProgram {
    const char* vertexName;
    const char* fragmentName;
    GLuint program, vs, fs;
};
constexpr int32_t kMaxPending = 9;
PendingProgram g_pending[kMaxPending];      // one context at a time
int32_t g_pendingCount = 0;
char g_defines[64];                         // the #defines of the context being created

GLuint startShader(GLenum type, const char* name) {
    GLuint shader = glCreateShader(type);
    shaderSourceByName(shader, name, g_defines);
    glCompileShader(shader);
    return shader;
}

PendingProgram startProgram(const char* vertexName, const char* fragmentName) {
    PendingProgram p = { vertexName, fragmentName, glCreateProgram(),
                         startShader(GL_VERTEX_SHADER, vertexName),
                         startShader(GL_FRAGMENT_SHADER, fragmentName) };
    glAttachShader(p.program, p.vs);
    glAttachShader(p.program, p.fs);
    glLinkProgram(p.program);
    return p;
}

bool shaderCompiled(GLuint shader, const char* name) {
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::printf("shader %s: %s\n", name, log);
    }
    return ok;
}

// The program ready, or 0 (with the error on the console)
GLuint finishProgram(const PendingProgram& p) {
    GLint ok = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        if (shaderCompiled(p.vs, p.vertexName) && shaderCompiled(p.fs, p.fragmentName)) {
            char log[2048];
            glGetProgramInfoLog(p.program, sizeof(log), nullptr, log);
            std::printf("link %s + %s: %s\n", p.vertexName, p.fragmentName, log);
        }
        glDeleteProgram(p.program);
    }
    glDeleteShader(p.vs);
    glDeleteShader(p.fs);
    return ok ? p.program : 0;
}

// All the context's programs, at once (see PendingProgram)
void startPrograms(const RzContext* ctx) {
    std::snprintf(g_defines, sizeof(g_defines), "#define CASCADES %d\n", int(ctx->cascades));
    static const char* const kPrograms[][2] = {
        { "terrain.vert", "terrain.frag" }, { "object.vert", "object.frag" },
        { "depth.vert", "depth.frag" },     { "object_depth.vert", "depth.frag" },
        { "wall.vert", "wall.frag" },       { "glass.vert", "glass.frag" },
        { "sprite.vert", "sprite.frag" },   { "sky.vert", "sky.frag" },
    };
    g_pendingCount = 0;
    for (const auto& names : kPrograms) g_pending[g_pendingCount++] = startProgram(names[0], names[1]);
}

constexpr GLint kUnitAtlas  = 0;    // texture unit of the atlas (and of the object's texture)
                                    // (1: the shadows, kUnitShadow)

} // namespace

// A program, by the names of the shaders (also used by rz_border.cpp,
// rz_glass.cpp, rz_sprites.cpp): the one already asked for by startPrograms, or compiled now
GLuint linkProgram(const char* vertexName, const char* fragmentName) {
    for (int32_t i = 0; i < g_pendingCount; ++i) {
        const PendingProgram p = g_pending[i];
        if (!std::strcmp(p.vertexName, vertexName) && !std::strcmp(p.fragmentName, fragmentName)) {
            g_pending[i] = g_pending[--g_pendingCount];
            return finishProgram(p);
        }
    }
    return finishProgram(startProgram(vertexName, fragmentName));
}

void initFogUniforms(GLuint program, FogUniforms& u) {
    u.on    = glGetUniformLocation(program, "uFogOn");
    u.start = glGetUniformLocation(program, "uFogStart");
    u.end   = glGetUniformLocation(program, "uFogEnd");
    u.color = glGetUniformLocation(program, "uFogColor");
    u.eye   = glGetUniformLocation(program, "uEye");
    u.origin = glGetUniformLocation(program, "uFogOrigin");
}

// The frame's fog: on only when following a target; colour = background
void bindFog(const RzContext* ctx, const FogUniforms& u) {
    glUniform1i(u.on, ctx->fogOn ? 1 : 0);
    glUniform1f(u.start, ctx->fogNear);
    glUniform1f(u.end, ctx->fogFar);
    glUniform3f(u.origin, ctx->fogOrigin.x, ctx->fogOrigin.y, ctx->fogOrigin.z);
    glUniform3f(u.color, ctx->backgroundR, ctx->backgroundG, ctx->backgroundB);
    glUniform3f(u.eye, ctx->eyePos.x, ctx->eyePos.y, ctx->eyePos.z);
}

// Flat light (terrain and objects): direction and ambient, fixed; the program has to
// be in use
void initLightUniforms(GLuint program) {
    const Vec3 l = lightDirection();
    glUniform3f(glGetUniformLocation(program, "uLight"), l.x, l.y, l.z);
    glUniform1f(glGetUniformLocation(program, "uAmbient"), kAmbient);
}

bool createRenderer(RzContext* ctx) {
    startPrograms(ctx);
    TerrainProgram& t = ctx->terrainProgram;
    t.program = linkProgram("terrain.vert", "terrain.frag");
    if (!t.program) return false;
    t.viewProj = glGetUniformLocation(t.program, "uViewProj");
    t.atlas    = glGetUniformLocation(t.program, "uAtlas");
    t.textured = glGetUniformLocation(t.program, "uTextured");
    t.shading  = glGetUniformLocation(t.program, "uShading");
    t.ambient   = glGetUniformLocation(t.program, "uShadowLight");
    t.smooth   = glGetUniformLocation(t.program, "uSmooth");
    t.model    = glGetUniformLocation(t.program, "uModel");
    const GLint shadowDim = glGetUniformLocation(t.program, "uShadowDim");
    glUseProgram(t.program);
    glUniform1i(t.atlas, kUnitAtlas);
    glUniform1i(t.smooth, 0);
    setMatrix(t.model, Mat4::identity());
    glUniform1f(t.shading, kTexturedShading);
    glUniform1f(t.ambient, kShadowLight);
    glUniform1f(shadowDim, kShadowTexturedDim);
    initShadowUniforms(t.program, t.shadow);
    initFogUniforms(t.program, t.fog);
    initLightUniforms(t.program);

    ObjectProgram& o = ctx->objectProgram;
    o.program = linkProgram("object.vert", "object.frag");
    if (!o.program) return false;
    o.viewProj = glGetUniformLocation(o.program, "uViewProj");
    o.texture  = glGetUniformLocation(o.program, "uTexture");
    o.ambient   = glGetUniformLocation(o.program, "uShadowLight");
    o.model    = glGetUniformLocation(o.program, "uModel");
    o.fromBuffer = glGetUniformLocation(o.program, "uFromBuffer");
    o.texelMode = glGetUniformLocation(o.program, "uTexelMode");
    o.decalCount = glGetUniformLocation(o.program, "uDecalCount");
    o.decal      = glGetUniformLocation(o.program, "uDecal");
    o.decalColor = glGetUniformLocation(o.program, "uDecalColor");
    glUseProgram(o.program);
    setMatrix(o.model, Mat4::identity());
    glUniform1i(o.fromBuffer, 0);
    glUniformBlockBinding(o.program, glGetUniformBlockIndex(o.program, "ObjectData"), kObjectDataBinding);
    glUniform1i(o.texture, kUnitAtlas);
    glUniform1f(o.ambient, kShadowLight);
    glUniform1f(glGetUniformLocation(o.program, "uSpecular"), kGlassSpecular);    // glass texels
    glUniform1f(glGetUniformLocation(o.program, "uShininess"), kGlassShininess);
    initShadowUniforms(o.program, o.shadow);
    initFogUniforms(o.program, o.fog);
    initLightUniforms(o.program);
    if (!createFallbackTexture(ctx)) return false;

    DepthProgram& d = ctx->depthProgram;
    d.program = linkProgram("depth.vert", "depth.frag");
    if (!d.program) return false;
    d.lightViewProj = glGetUniformLocation(d.program, "uLightViewProj");
    d.model         = glGetUniformLocation(d.program, "uModel");
    glUseProgram(d.program);
    setMatrix(d.model, Mat4::identity());

    DepthProgram& od = ctx->objectDepthProgram;
    od.program = linkProgram("object_depth.vert", "depth.frag");
    if (!od.program) return false;
    od.lightViewProj = glGetUniformLocation(od.program, "uLightViewProj");
    glUseProgram(od.program);
    glUniformBlockBinding(od.program, glGetUniformBlockIndex(od.program, "ObjectData"), kObjectDataBinding);
    if (!createShadowMaps(ctx)) return false;
    if (!createBorder(ctx)) return false;
    if (!createGlassProgram(ctx)) return false;
    if (!createSprites(ctx)) return false;
    if (!createSky(ctx)) return false;

    // The terrain's mesh: a buffer of fixed size, filled in buildTerrainMesh
    glGenVertexArrays(1, &ctx->terrainVao);
    glGenBuffers(1, &ctx->terrainVbo);
    glBindVertexArray(ctx->terrainVao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->terrainVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(kTriangleCount) * 3 * GLsizeiptr(sizeof(TerrainVertex)),
                 nullptr, GL_STATIC_DRAW);
    setTerrainVertexLayout();
    glBindVertexArray(0);
    return glGetError() == GL_NO_ERROR;
}

void destroyRenderer(RzContext* ctx) {
    for (int32_t i = 0; i < g_pendingCount; ++i) {   // asked for and not used (the creation failed before)
        glDeleteProgram(g_pending[i].program);
        glDeleteShader(g_pending[i].vs);
        glDeleteShader(g_pending[i].fs);
    }
    g_pendingCount = 0;
    if (ctx->terrainProgram.program) glDeleteProgram(ctx->terrainProgram.program);
    if (ctx->objectProgram.program) glDeleteProgram(ctx->objectProgram.program);
    if (ctx->terrainVao) glDeleteVertexArrays(1, &ctx->terrainVao);
    if (ctx->terrainVbo) glDeleteBuffers(1, &ctx->terrainVbo);
    if (ctx->atlasTex) glDeleteTextures(1, &ctx->atlasTex);
    if (ctx->fallbackTex) glDeleteTextures(1, &ctx->fallbackTex);
    if (ctx->depthProgram.program) glDeleteProgram(ctx->depthProgram.program);
    if (ctx->objectDepthProgram.program) glDeleteProgram(ctx->objectDepthProgram.program);
    destroyShadowMaps(ctx);
    destroyBorder(ctx);
    if (ctx->glassProgram.program) glDeleteProgram(ctx->glassProgram.program);
    destroySprites(ctx);
    destroySky(ctx);
}

void renderFrame(RzContext* ctx) {
    ctx->frameTime += ctx->frameStep;
    const Mat4 viewProj = updateCamera(ctx);
    ctx->lastViewProj = viewProj;

    prepareObjects(ctx);
    renderShadowMaps(ctx);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx->width, ctx->height);
    glClearColor(ctx->backgroundR, ctx->backgroundG, ctx->backgroundB, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky(ctx);                                 // behind everything: sun or moon, clouds
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glFrontFace(GL_CCW);                          // front = anticlockwise on the screen (as in the software one)

    if (ctx->hasTerrain()) {
        const TerrainProgram& t = ctx->terrainProgram;
        const bool textured = ctx->hasAtlas && ctx->hasTileMap;
        glUseProgram(t.program);
        setMatrix(t.viewProj, viewProj);
        bindShadowMaps(ctx, t.shadow);
        bindFog(ctx, t.fog);
        glUniform1i(t.textured, textured ? 1 : 0);
        glActiveTexture(GL_TEXTURE0 + GLenum(kUnitAtlas));
        glBindTexture(GL_TEXTURE_2D_ARRAY, textured ? ctx->atlasTex : 0);

        glEnable(GL_CULL_FACE);                   // terrain: only the top face
        glCullFace(GL_BACK);
        drawTerrain(ctx, viewProj, ctx->fogOn);   // only the chunks on the screen and before the end of the fog
        if (ctx->fogOn && ctx->skirtOn) drawSkirt(ctx, viewProj, true);   // continuation past the map: only with fog
    }

    {                                             // projected discs (rzSetObjectDecals)
        const ObjectProgram& o = ctx->objectProgram;
        glUseProgram(o.program);
        glUniform1i(o.decalCount, ctx->decalCount);
        if (ctx->decalCount) {
            glUniform4fv(o.decal, ctx->decalCount, ctx->decals);
            glUniform3fv(o.decalColor, ctx->decalCount, ctx->decalColors);
        }
    }
    drawObjects(ctx, viewProj);
    drawWheels(ctx, viewProj);                    // the wheels' tyres: the terrain's program, without texture
    drawWheelFaces(ctx, viewProj);                // the wheels' faces: the objects' program, textured
    drawGlass(ctx, viewProj);                     // glass: filter + highlight, after the opaque
    drawTexturedGlass(ctx, viewProj);             // glass texels: the same filter
    drawSprites(ctx, viewProj);                   // smoke etc.: alpha, back to front
    drawWall(ctx, viewProj);                      // semi-transparent: last
}

} // namespace rz
