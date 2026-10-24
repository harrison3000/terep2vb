/*
 * The renderizeitor's platform on the web page (in place of its
 * rz_platform_egl.cpp): the "window" is the page's canvas, whose WebGL 2
 * context web/webgl.js makes, and the OpenGL functions of rz_gl.h are
 * imports of this module (module "gl"), done in WebGL by webgl.js.
 * platformGetProc gives the library pointers to those imports.
 */
#include <cstdlib>
#include <cstring>
#include "../host/renderizeitor/src/rz_gl.h"
#include "../host/renderizeitor/src/rz_platform.h"

#define IMPORT(name) __attribute__((import_module("gl"), import_name(name)))

// webgl.js: the canvas's context (made once, true if WebGL 2 works) and the
// size of its drawing buffer
extern "C" IMPORT("rzweb_context") int rzweb_context();
extern "C" IMPORT("rzweb_size") void rzweb_size(int32_t *width, int32_t *height);

using namespace rz;

#define RZ_GL_IMPORT(ret, name, args) extern "C" IMPORT(#name) ret web_##name args;
RZ_GL_FUNCTIONS(RZ_GL_IMPORT)
#undef RZ_GL_IMPORT

namespace {

struct Proc {
    const char *name;
    void *fn;
};

const Proc procs[] = {
#define RZ_GL_PROC(ret, name, args) { #name, reinterpret_cast<void *>(web_##name) },
RZ_GL_FUNCTIONS(RZ_GL_PROC)
#undef RZ_GL_PROC
};

} // namespace

namespace rz {

struct Platform {
    int unused;
};

// No offscreen mode on the page: rzCreate fails.
Platform *platformCreateOffscreen() { return nullptr; }

Platform *platformCreateSdl(void *, int32_t *outWidth, int32_t *outHeight)
{
    if (!rzweb_context())
        return nullptr;
    rzweb_size(outWidth, outHeight);
    return static_cast<Platform *>(std::calloc(1, sizeof(Platform)));
}

void platformDestroy(Platform *p) { std::free(p); }

bool platformMakeCurrent(Platform *p) { return p != nullptr; }

void *platformGetProc(const char *name)
{
    for (const Proc &p : procs)
        if (!std::strcmp(p.name, name))
            return p.fn;
    return nullptr;
}

bool platformDrawableSize(Platform *, int32_t *outWidth, int32_t *outHeight)
{
    rzweb_size(outWidth, outHeight);
    return true;
}

} // namespace rz
