// The renderers the SDL host can draw a game with, chosen in the map
// chooser (or -r).  A renderer draws one frame at 60 Hz between gui_begin()
// and gui_end() (gui.hpp), into the host's OpenGL context, without swapping.
#pragma once

// Where a picture of screen_w() x screen_h() game pixels would be on the
// window (k window pixels per game pixel); the minimap is placed with it.
struct PictureRect {
    float x0, y0, k;
};

struct GameRenderer {
    const char *name;               // for -r, and in the chooser
    const char *what;
    bool available;                 // compiled in
    bool (*start)();                // after game_init, in the map's directory; false: cannot
    bool (*frame)(int win_w, int win_h, PictureRect *where);    // false: the game asked to end
    void (*tick)();                 // after each physics tick (120 Hz, not while paused), to
                                    // record what moves between frames; may be null
    void (*stop)();                 // before the game's memory is freed
};

extern const GameRenderer game_renderers[];
extern const int game_renderer_count;
int game_renderer_find(const char *name);   // index, -1 if unknown

// The OpenGL renderer's draw distance (opengl_set_draw_distance): the fog's
// start, in map tiles.
constexpr int DRAW_DISTANCE_MIN = 1, DRAW_DISTANCE_DEFAULT = 30, DRAW_DISTANCE_MAX = 110;

// renderer_sw.cpp, renderer_gl.cpp
bool software_start();
bool software_frame(int win_w, int win_h, PictureRect *where);
void software_stop();
bool opengl_start();
bool opengl_frame(int win_w, int win_h, PictureRect *where);
void opengl_tick();
void opengl_set_draw_distance(int tiles);  // DRAW_DISTANCE_MIN..MAX (launcher.hpp); any time
void opengl_stop();
