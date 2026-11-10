// What the web host's two halves share: web_main.cpp (the page's calls, the
// map's files, the time) and gl_renderer.cpp (the OpenGL renderer, WebGL 2).
#pragma once
#include <cstdint>
#include "rz/renderizeitor.h"

// web_main.cpp: a file of the map, by its name (any case), in memory; null
// if it is not there
const uint8_t *web_file(const char *name, int32_t *size);
// web_main.cpp: a text the page draws over the canvas this frame, its top left
// corner at (x, y) in canvas pixels, size the font's
void web_text(float x, float y, float size, const uint8_t *rgb, const char *text);

// gl_renderer.cpp: the OpenGL renderer, on the page's canvas
constexpr int DRAW_DISTANCE_MIN = 1, DRAW_DISTANCE_DEFAULT = 30, DRAW_DISTANCE_MAX = 110;  // tiles
bool gl_start(int width, int height,    // after game_init, the canvas's size in pixels and the quality:
              int quality);             // 0 high, 1 medium, 2 low; false: cannot (no WebGL 2, the map's files)
void gl_resize(int width, int height);  // the canvas's size changed
void gl_tick();                         // after each physics tick
void gl_frame(float frames);            // a frame, frames of 60 Hz after the last one
void gl_stop();                         // before the game's memory is freed
void gl_set_draw_distance(int tiles);   // where the fog starts: DRAW_DISTANCE_MIN..MAX; any time
// The free camera (the page's checkpoint editor): on, at eye looking along
// forward (tiles, the game's axes: x column, y row, z up), or off (back to
// the camera of F1..F5)
void gl_free_camera(bool on, const float eye[3], const float forward[3]);
void gl_set_decals(const RzDecal *decals, int count);   // checkpoints projected on the cars (rzSetObjectDecals)
const float *gl_view();                 // the last frame's camera (rzGetView), RZ_VIEW_FLOATS floats
