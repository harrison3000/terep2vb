// O WebGL 2 que o renderer usa: as funções são imports do módulo "gl",
// feitas por web/webgl.js, uma para uma, com os nomes e os números do
// OpenGL ES 3.0. Ponteiros são posições na memória do módulo. Mais as duas
// do canvas: o contexto (criado uma vez).
#pragma once

#include <cstddef>
#include <cstdint>

namespace rz {

using GLenum     = unsigned int;
using GLuint     = unsigned int;
using GLint      = int;
using GLsizei    = int;
using GLfloat    = float;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLchar     = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr   = std::ptrdiff_t;

constexpr GLenum GL_NO_ERROR                = 0;
constexpr GLenum GL_FALSE                   = 0;
constexpr GLenum GL_TRUE                    = 1;
constexpr GLenum GL_TRIANGLES               = 0x0004;
constexpr GLenum GL_FRONT                   = 0x0404;
constexpr GLenum GL_BACK                    = 0x0405;
constexpr GLenum GL_CCW                     = 0x0901;
constexpr GLenum GL_CULL_FACE               = 0x0B44;
constexpr GLenum GL_DEPTH_TEST              = 0x0B71;
constexpr GLenum GL_SCISSOR_TEST            = 0x0C11;
constexpr GLenum GL_LEQUAL                  = 0x0203;
constexpr GLenum GL_LESS                    = 0x0201;
constexpr GLenum GL_UNPACK_ALIGNMENT        = 0x0CF5;
constexpr GLenum GL_TEXTURE_2D              = 0x0DE1;
constexpr GLenum GL_MAX_TEXTURE_SIZE        = 0x0D33;
constexpr GLenum GL_ONE                     = 1;
constexpr GLenum GL_BLEND                   = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA               = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA     = 0x0303;
constexpr GLenum GL_BYTE                    = 0x1400;
constexpr GLenum GL_UNSIGNED_BYTE           = 0x1401;
constexpr GLenum GL_INT                     = 0x1404;
constexpr GLenum GL_UNSIGNED_INT            = 0x1405;
constexpr GLenum GL_FLOAT                   = 0x1406;
constexpr GLenum GL_INT_2_10_10_10_REV      = 0x8D9F;
constexpr GLenum GL_DEPTH_COMPONENT         = 0x1902;
constexpr GLenum GL_RED                     = 0x1903;
constexpr GLenum GL_RGBA                    = 0x1908;
constexpr GLenum GL_POLYGON_OFFSET_FILL     = 0x8037;
constexpr GLenum GL_TEXTURE_COMPARE_MODE    = 0x884C;
constexpr GLenum GL_TEXTURE_COMPARE_FUNC    = 0x884D;
constexpr GLenum GL_COMPARE_REF_TO_TEXTURE  = 0x884E;
constexpr GLenum GL_TEXTURE_2D_ARRAY        = 0x8C1A;
constexpr GLenum GL_R8                      = 0x8229;
constexpr GLenum GL_RGBA8                   = 0x8058;
constexpr GLenum GL_DEPTH_COMPONENT24       = 0x81A6;
constexpr GLenum GL_NEAREST                 = 0x2600;
constexpr GLenum GL_LINEAR                  = 0x2601;
constexpr GLenum GL_NEAREST_MIPMAP_LINEAR   = 0x2702;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR    = 0x2703;
constexpr GLenum GL_TEXTURE_MAG_FILTER      = 0x2800;
constexpr GLenum GL_TEXTURE_MIN_FILTER      = 0x2801;
constexpr GLenum GL_TEXTURE_WRAP_S          = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T          = 0x2803;
constexpr GLenum GL_TEXTURE_MAX_LEVEL       = 0x813D;
constexpr GLenum GL_CLAMP_TO_EDGE           = 0x812F;
constexpr GLenum GL_TEXTURE0                = 0x84C0;
constexpr GLenum GL_COLOR_BUFFER_BIT        = 0x00004000;
constexpr GLenum GL_DEPTH_BUFFER_BIT        = 0x00000100;
constexpr GLenum GL_ARRAY_BUFFER            = 0x8892;
constexpr GLenum GL_UNIFORM_BUFFER          = 0x8A11;
constexpr GLenum GL_STATIC_DRAW             = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW            = 0x88E8;
constexpr GLenum GL_FRAGMENT_SHADER         = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER           = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS          = 0x8B81;
constexpr GLenum GL_LINK_STATUS             = 0x8B82;
constexpr GLenum GL_FRAMEBUFFER             = 0x8D40;
constexpr GLenum GL_DEPTH_ATTACHMENT        = 0x8D00;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE    = 0x8CD5;

#define RZ_GL(name) extern "C" __attribute__((import_module("gl"), import_name(#name)))

// O canvas (webgl.js): cria o contexto WebGL 2 na primeira vez (false: não há)
RZ_GL(canvasContext) bool canvasContext();

RZ_GL(glGetError)       GLenum glGetError();
RZ_GL(glGetIntegerv)    void glGetIntegerv(GLenum name, GLint* data);
RZ_GL(glBlendFunc)      void glBlendFunc(GLenum src, GLenum dst);
RZ_GL(glColorMask)      void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
RZ_GL(glDepthMask)      void glDepthMask(GLboolean flag);
RZ_GL(glPolygonOffset)  void glPolygonOffset(GLfloat factor, GLfloat units);
RZ_GL(glViewport)       void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
RZ_GL(glScissor)        void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
RZ_GL(glClearColor)     void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
RZ_GL(glClear)          void glClear(GLbitfield mask);
RZ_GL(glEnable)         void glEnable(GLenum cap);
RZ_GL(glDisable)        void glDisable(GLenum cap);
RZ_GL(glCullFace)       void glCullFace(GLenum mode);
RZ_GL(glFrontFace)      void glFrontFace(GLenum mode);
RZ_GL(glDepthFunc)      void glDepthFunc(GLenum func);
RZ_GL(glPixelStorei)    void glPixelStorei(GLenum name, GLint param);

RZ_GL(glGenTextures)    void glGenTextures(GLsizei n, GLuint* textures);
RZ_GL(glDeleteTextures) void glDeleteTextures(GLsizei n, const GLuint* textures);
RZ_GL(glBindTexture)    void glBindTexture(GLenum target, GLuint texture);
RZ_GL(glActiveTexture)  void glActiveTexture(GLenum texture);
RZ_GL(glTexParameteri)  void glTexParameteri(GLenum target, GLenum name, GLint param);
RZ_GL(glTexImage2D)     void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w,
                                          GLsizei h, GLint border, GLenum format, GLenum type,
                                          const void* data);
RZ_GL(glTexImage3D)     void glTexImage3D(GLenum target, GLint level, GLint internalFormat, GLsizei w,
                                          GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type,
                                          const void* data);

RZ_GL(glGenFramebuffers)        void glGenFramebuffers(GLsizei n, GLuint* ids);
RZ_GL(glDeleteFramebuffers)     void glDeleteFramebuffers(GLsizei n, const GLuint* ids);
RZ_GL(glBindFramebuffer)        void glBindFramebuffer(GLenum target, GLuint id);
RZ_GL(glCheckFramebufferStatus) GLenum glCheckFramebufferStatus(GLenum target);
RZ_GL(glFramebufferTexture2D)   void glFramebufferTexture2D(GLenum target, GLenum attachment,
                                                            GLenum texTarget, GLuint texture, GLint level);

RZ_GL(glCreateShader)      GLuint glCreateShader(GLenum type);
RZ_GL(glDeleteShader)      void glDeleteShader(GLuint shader);
RZ_GL(glShaderSource)      void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* src,
                                               const GLint* length);
RZ_GL(glCompileShader)     void glCompileShader(GLuint shader);
RZ_GL(glGetShaderiv)       void glGetShaderiv(GLuint shader, GLenum name, GLint* params);
RZ_GL(glGetShaderInfoLog)  void glGetShaderInfoLog(GLuint shader, GLsizei max, GLsizei* len, GLchar* log);
RZ_GL(glCreateProgram)     GLuint glCreateProgram();
RZ_GL(glDeleteProgram)     void glDeleteProgram(GLuint program);
RZ_GL(glAttachShader)      void glAttachShader(GLuint program, GLuint shader);
RZ_GL(glLinkProgram)       void glLinkProgram(GLuint program);
RZ_GL(glGetProgramiv)      void glGetProgramiv(GLuint program, GLenum name, GLint* params);
RZ_GL(glGetProgramInfoLog) void glGetProgramInfoLog(GLuint program, GLsizei max, GLsizei* len, GLchar* log);
RZ_GL(glUseProgram)        void glUseProgram(GLuint program);
RZ_GL(glGetUniformLocation) GLint glGetUniformLocation(GLuint program, const GLchar* name);
RZ_GL(glUniform1i)         void glUniform1i(GLint loc, GLint v);
RZ_GL(glUniform1f)         void glUniform1f(GLint loc, GLfloat v);
RZ_GL(glUniform3f)         void glUniform3f(GLint loc, GLfloat x, GLfloat y, GLfloat z);
RZ_GL(glGetUniformBlockIndex) GLuint glGetUniformBlockIndex(GLuint program, const GLchar* name);
RZ_GL(glUniformBlockBinding)  void glUniformBlockBinding(GLuint program, GLuint block, GLuint binding);
// transpose: sempre GL_FALSE no WebGL (setMatrices, rz_internal.h)
RZ_GL(glUniformMatrix4fv)  void glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose,
                                                   const GLfloat* v);

RZ_GL(glGenVertexArrays)    void glGenVertexArrays(GLsizei n, GLuint* ids);
RZ_GL(glDeleteVertexArrays) void glDeleteVertexArrays(GLsizei n, const GLuint* ids);
RZ_GL(glBindVertexArray)    void glBindVertexArray(GLuint id);
RZ_GL(glGenBuffers)         void glGenBuffers(GLsizei n, GLuint* ids);
RZ_GL(glDeleteBuffers)      void glDeleteBuffers(GLsizei n, const GLuint* ids);
RZ_GL(glBindBuffer)         void glBindBuffer(GLenum target, GLuint id);
RZ_GL(glBindBufferBase)     void glBindBufferBase(GLenum target, GLuint index, GLuint id);
RZ_GL(glBufferData)         void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
RZ_GL(glBufferSubData)      void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                                                 const void* data);
RZ_GL(glVertexAttribPointer)  void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                                         GLboolean normalized, GLsizei stride,
                                                         const void* offset);
RZ_GL(glVertexAttribIPointer) void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                                                          GLsizei stride, const void* offset);
RZ_GL(glVertexAttribI4i)      void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w);
RZ_GL(glVertexAttrib4f)       void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
RZ_GL(glEnableVertexAttribArray) void glEnableVertexAttribArray(GLuint index);
RZ_GL(glDrawArrays)         void glDrawArrays(GLenum mode, GLint first, GLsizei count);

#undef RZ_GL

} // namespace rz
