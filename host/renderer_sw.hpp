// The SDL host's renderer: the game draws its own frame (game_frame), shown
// at 60 Hz between gui_begin() and gui_end() (gui.hpp), without swapping.
#pragma once

// Where a picture of screen_w() x screen_h() game pixels is on the window
// (k window pixels per game pixel); the minimap is placed with it.
struct PictureRect {
    float x0, y0, k;
};

bool software_start();                                          // after game_init
bool software_frame(int win_w, int win_h, PictureRect *where);  // false: the game asked to end
void software_stop();                                           // before the game's memory is freed
