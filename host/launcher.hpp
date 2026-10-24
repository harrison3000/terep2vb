// Map chooser shown by the SDL host when no map directory is given: a
// directory browser, a preview of the selected map, the render scale and
// the extra options, such as bug fixes (Dear ImGui).
#pragma once
#include <string>
#include <SDL.h>
#include "game_renderer.hpp"           // DRAW_DISTANCE_*

// What the SDL host does around the game (not options of the game itself).
struct HostOptions {
    bool no_sound = false;          // "Disable sound": no audio device, no OPL emulator
    bool minimap = false;           // the map and the cars in the bottom right corner
    int renderer = 0;               // index in game_renderers (game_renderer.hpp)
    int draw_distance = DRAW_DISTANCE_DEFAULT;  // OpenGL renderer: where the fog starts, in tiles
};

// Runs until the player picks a map (true, *map set) or closes the
// window (false).  Starts browsing where *map (if set: the command line,
// the preferences or the last call) is, with it selected, or else in the
// current directory.  render_scale, extra_options (see terep.hpp) and the
// host's options are edited in place, starting from their current values.
bool launcher_run(std::string *map, HostOptions *host);     // draws in the gui (gui.hpp)
