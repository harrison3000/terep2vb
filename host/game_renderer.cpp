// The list of renderers.  The OpenGL one is there only when the host is
// built with the external library (make sdl GLR=1, see the Makefile).
#include <cstring>
#include "game_renderer.hpp"

#ifdef TEREP2_GLR
constexpr bool HAVE_GLR = true;
#else
constexpr bool HAVE_GLR = false;
#endif

const GameRenderer game_renderers[] = {
    { "software", "the original's renderer, drawn by the game itself", true,
      software_start, software_frame, nullptr, software_stop },
    { "opengl", "the external OpenGL 3.3 renderer (see TODO-lib.md)", HAVE_GLR,
      opengl_start, opengl_frame, opengl_tick, opengl_stop },
};
const int game_renderer_count = sizeof game_renderers / sizeof game_renderers[0];

int game_renderer_find(const char *name)
{
    for (int i = 0; i < game_renderer_count; i++)
        if (!strcmp(game_renderers[i].name, name))
            return i;
    return -1;
}
