// The map chooser's preferences, kept between runs in a small text file
// (prefs.txt in SDL's preference directory: ~/.local/share/terep2vibed on
// Linux, %APPDATA%\terep2vibed on Windows), one "key: value" per line:
//
//   renderer: opengl
//   scale: 3
//   draw-distance: 30
//   minimap: on
//   sound: on
//   fix-ice-bug: on
//   hide-banner: off
//   map: /path/to/the/last/map
//
// Unknown keys and bad values are ignored.
#pragma once
#include <string>
#include "launcher.hpp"

// Reads the file into host, render_scale, extra_options and *map (where the
// chooser opens).  Missing file: nothing changes.
void prefs_load(HostOptions *host, std::string *map);

// Writes them if they changed since they were read or last written.
void prefs_save(const HostOptions &host, const std::string &map);
