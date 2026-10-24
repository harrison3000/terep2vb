// Recursos de GPU do contexto (programas) e o frame.

#include <cstdio>

#include "rz_internal.h"
#include "rz_shaders.h"

namespace rz {

namespace {

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::printf("shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}


constexpr GLint kUnitAtlas  = 0;    // unidade de textura do atlas (e da textura do objeto)
                                    // (1..3: shadow maps, kUnitShadowFirst)

} // namespace

// Compila e liga um programa (também usado por rz_border.cpp)
GLuint linkProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::printf("link: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void initFogUniforms(GLuint program, FogUniforms& u) {
    u.on    = glGetUniformLocation(program, "uFogOn");
    u.start = glGetUniformLocation(program, "uFogStart");
    u.end   = glGetUniformLocation(program, "uFogEnd");
    u.color = glGetUniformLocation(program, "uFogColor");
    u.eye   = glGetUniformLocation(program, "uEye");
    u.origin = glGetUniformLocation(program, "uFogOrigin");
}

// Neblina do frame: liga só seguindo um alvo; cor = fundo
void bindFog(const RzContext* ctx, const FogUniforms& u) {
    glUniform1i(u.on, ctx->fogOn ? 1 : 0);
    glUniform1f(u.start, ctx->fogNear);
    glUniform1f(u.end, ctx->fogFar);
    glUniform3f(u.origin, ctx->fogOrigin.x, ctx->fogOrigin.y, ctx->fogOrigin.z);
    glUniform3f(u.color, ctx->backgroundR, ctx->backgroundG, ctx->backgroundB);
    glUniform3f(u.eye, ctx->eyePos.x, ctx->eyePos.y, ctx->eyePos.z);
}

// Luz flat (terreno e objetos): direção e ambiente, fixos; o programa precisa
// estar em uso
void initLightUniforms(GLuint program) {
    const Vec3 l = lightDirection();
    glUniform3f(glGetUniformLocation(program, "uLight"), l.x, l.y, l.z);
    glUniform1f(glGetUniformLocation(program, "uAmbient"), kAmbient);
}

bool createRenderer(RzContext* ctx) {
    TerrainProgram& t = ctx->terrainProgram;
    t.program = linkProgram(kTerrainVertexShader, kTerrainFragmentShader);
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
    o.program = linkProgram(kObjectVertexShader, kObjectFragmentShader);
    if (!o.program) return false;
    o.viewProj = glGetUniformLocation(o.program, "uViewProj");
    o.texture  = glGetUniformLocation(o.program, "uTexture");
    o.ambient   = glGetUniformLocation(o.program, "uShadowLight");
    o.model    = glGetUniformLocation(o.program, "uModel");
    o.fromBuffer = glGetUniformLocation(o.program, "uFromBuffer");
    glUseProgram(o.program);
    setMatrix(o.model, Mat4::identity());
    glUniform1i(o.fromBuffer, 0);
    glUniformBlockBinding(o.program, glGetUniformBlockIndex(o.program, "ObjectData"), kObjectDataBinding);
    glUniform1i(o.texture, kUnitAtlas);
    glUniform1f(o.ambient, kShadowLight);
    initShadowUniforms(o.program, o.shadow);
    initFogUniforms(o.program, o.fog);
    initLightUniforms(o.program);
    if (!createFallbackTexture(ctx)) return false;

    DepthProgram& d = ctx->depthProgram;
    d.program = linkProgram(kDepthVertexShader, kDepthFragmentShader);
    if (!d.program) return false;
    d.lightViewProj = glGetUniformLocation(d.program, "uLightViewProj");
    d.model         = glGetUniformLocation(d.program, "uModel");
    glUseProgram(d.program);
    setMatrix(d.model, Mat4::identity());

    DepthProgram& od = ctx->objectDepthProgram;
    od.program = linkProgram(kObjectDepthVertexShader, kDepthFragmentShader);
    if (!od.program) return false;
    od.lightViewProj = glGetUniformLocation(od.program, "uLightViewProj");
    glUseProgram(od.program);
    glUniformBlockBinding(od.program, glGetUniformBlockIndex(od.program, "ObjectData"), kObjectDataBinding);
    if (!createShadowMaps(ctx)) return false;
    if (!createBorder(ctx)) return false;
    if (!createGlassProgram(ctx)) return false;
    if (!createSprites(ctx)) return false;

    // Malha do terreno: buffer de tamanho fixo, preenchido em buildTerrainMesh
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
}

void renderFrame(RzContext* ctx) {
    ++ctx->frameCount;
    const Mat4 viewProj = updateCamera(ctx);

    prepareObjects(ctx);
    renderShadowMaps(ctx);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx->width, ctx->height);
    glClearColor(ctx->backgroundR, ctx->backgroundG, ctx->backgroundB, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glFrontFace(GL_CCW);                          // frente = anti-horário na tela (como no software)

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

        glEnable(GL_CULL_FACE);                   // terreno: só a face de cima
        glCullFace(GL_BACK);
        glBindVertexArray(ctx->terrainVao);
        glDrawArrays(GL_TRIANGLES, 0, kTriangleCount * 3);
        drawSkirt(ctx);                           // continuação além do mapa (com neblina)
    }

    drawObjects(ctx, viewProj);
    drawWheels(ctx, viewProj);                    // pneus das rodas: programa do terreno, sem textura
    drawWheelFaces(ctx, viewProj);                // faces das rodas: programa dos objetos, texturizadas
    drawGlass(ctx, viewProj);                     // vidro: filtro + brilho, depois do opaco
    drawSprites(ctx, viewProj);                   // fumaça etc.: alfa, de trás para frente
    drawWall(ctx, viewProj);                      // semitransparente: por último
}

} // namespace rz
