// What the web host's two halves share: web_main.cpp (the page's calls, the
// map's files, the time) and gl_renderer.cpp (the OpenGL renderer, WebGL 2).
#pragma once
#include <cstdint>

// web_main.cpp: a file of the map, by its name (any case), in memory; null
// if it is not there
const uint8_t *web_file(const char *name, int32_t *size);
// web_main.cpp: a text the page draws over the canvas this frame, its top left
// corner at (x, y) in canvas pixels, size the font's
void web_text(float x, float y, float size, const uint8_t *rgb, const char *text);

// gl_renderer.cpp: the OpenGL renderer, on the page's canvas
constexpr int DRAW_DISTANCE_MIN = 1, DRAW_DISTANCE_DEFAULT = 30, DRAW_DISTANCE_MAX = 110;  // tiles
bool gl_start();                        // after game_init; false: cannot (no WebGL 2, the map's files)
void gl_tick();                         // after each physics tick
void gl_frame(int height);              // a frame; height: the canvas's, in pixels (the text)
void gl_stop();                         // before the game's memory is freed
void gl_set_draw_distance(int tiles);   // where the fog starts: DRAW_DISTANCE_MIN..MAX; any time
