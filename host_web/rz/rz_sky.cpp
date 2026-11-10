// Céu: um triângulo que cobre a tela, desenhado logo depois de limpar (atrás
// de tudo, sem profundidade). A base é a cor de fundo (a da neblina: o
// horizonte emenda com o terreno que some nela); por cima, o sol (fundo
// claro) ou a lua (fundo escuro) na direção da luz, e nuvens discretas num
// plano alto, em células (levemente pixelizadas), andando devagar, sumindo
// perto do horizonte. O raio de cada pixel sai da base da câmera e das
// focais (sem inverter matriz).

#include "rz_internal.h"

namespace rz {

bool createSky(RzContext* ctx) {
    SkyProgram& s = ctx->skyProgram;
    s.program = linkProgram("sky.vert", "sky.frag");
    if (!s.program) return false;
    s.right   = glGetUniformLocation(s.program, "uRight");
    s.up      = glGetUniformLocation(s.program, "uUp");
    s.forward = glGetUniformLocation(s.program, "uForward");
    s.focal   = glGetUniformLocation(s.program, "uFocal");
    s.light   = glGetUniformLocation(s.program, "uLight");
    s.color   = glGetUniformLocation(s.program, "uColor");
    s.time    = glGetUniformLocation(s.program, "uTime");
    glGenVertexArrays(1, &ctx->skyVao);           // sem atributos: os cantos saem de gl_VertexID
    return glGetError() == GL_NO_ERROR;
}

void destroySky(RzContext* ctx) {
    if (ctx->skyProgram.program) glDeleteProgram(ctx->skyProgram.program);
    if (ctx->skyVao) glDeleteVertexArrays(1, &ctx->skyVao);
}

// Depois do glClear, antes de tudo (a câmera do frame já calculada)
void drawSky(const RzContext* ctx) {
    const SkyProgram& s = ctx->skyProgram;
    const Vec3 l = lightDirection();
    glUseProgram(s.program);
    glUniform3f(s.right, ctx->camRight.x, ctx->camRight.y, ctx->camRight.z);
    glUniform3f(s.up, ctx->camUp.x, ctx->camUp.y, ctx->camUp.z);
    glUniform3f(s.forward, ctx->camForward.x, ctx->camForward.y, ctx->camForward.z);
    glUniform3f(s.focal, ctx->focalX, ctx->focalY, 0.0f);
    glUniform3f(s.light, l.x, l.y, l.z);
    glUniform3f(s.color, ctx->backgroundR, ctx->backgroundG, ctx->backgroundB);
    glUniform1f(s.time, ctx->frameTime / 60.0f);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(ctx->skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace rz
