/*
 * Web host: the game in a page, as WebAssembly (wasm32-wasip1, built as a
 * "reactor": no main, the page calls the exports below).  The page reads
 * the map's files from a folder the player picks (nothing is uploaded),
 * copies them into this module's memory (web_alloc, web_add_file) and
 * starts the game; then, every animation frame, it passes the keys
 * (web_key) and the time (web_advance) and draws the picture it gets back.
 * The original timing, as in the SDL host: physics at 120 Hz, the picture
 * at 60 Hz.  No sound, no OpenGL renderer.  See web/terep2.js.
 */
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>
#include "../host/host.hpp"

#define EXPORT(name) extern "C" __attribute__((export_name(name)))

namespace {

struct File {
    std::string name;
    uint8_t *data;              // from web_alloc, owned here
    size_t size;
};
std::vector<File> files;        // the map's, by their names (any case)

bool running = false;
double tick_ms = 0, frame_ms = 0;  // time not yet spent on ticks / frames
std::vector<uint32_t> picture;  // the last frame, RGBA

} // namespace

// The map's files, read from memory with the case of the name ignored, as
// the SDL host does on the disk.
FILE *data_fopen(const char *name)
{
    for (const File &f : files)
        if (strcasecmp(f.name.c_str(), name) == 0)
            return fmemopen(f.data, f.size, "rb");
    return nullptr;
}

bool host_init_state()
{
    state_reset();
    return true;
}

void host_free_game()
{
    game_free();
}

EXPORT("web_alloc") void *web_alloc(size_t n)
{
    return malloc(n ? n : 1);
}

EXPORT("web_free") void web_free(void *p)
{
    free(p);
}

// A file of the map: data from web_alloc, which is kept (freed by web_clear_files).
EXPORT("web_add_file") void web_add_file(const char *name, uint8_t *data, size_t size)
{
    files.push_back({ name, data, size });
}

EXPORT("web_clear_files") void web_clear_files()
{
    for (File &f : files)
        free(f.data);
    files.clear();
}

// A game on the files added, at a render scale (1..MAX_RENDER_SCALE) and with
// the extra options (fix_ice_bug, hide_banner).  Returns false if the map
// cannot be played (no car1.dat...).
EXPORT("web_start") bool web_start(int scale, bool fix_ice, bool hide_banner)
{
    if (running)
        host_free_game();
    running = false;
    render_scale = scale < 1 ? 1 : scale > MAX_RENDER_SCALE ? MAX_RENDER_SCALE : scale;
    extra_options.fix_ice_bug = fix_ice;
    extra_options.hide_banner = hide_banner;
    host_init_state();
    if (game_init() != 0 || world.num_cars <= 0)
        return false;
    tick_ms = frame_ms = 0;
    running = true;
    return true;
}

EXPORT("web_stop") void web_stop()
{
    if (running)
        host_free_game();
    running = false;
}

// A key: a PC scancode (set 1), | 0x80 when released.
EXPORT("web_key") void web_key(int scancode)
{
    if (running)
        game_key((uint8_t)scancode);
}

// ms more of time: the physics ticks and the frames it holds.  Returns the
// new picture (web_width() x web_height() RGBA pixels), or null when no new
// frame is due.
EXPORT("web_advance") const uint32_t *web_advance(double ms)
{
    if (!running)
        return nullptr;
    if (ms > 250)
        ms = 250;                       // after a stall (a hidden tab): do not try to catch up
    for (tick_ms += ms; tick_ms >= 1000.0 / 120; tick_ms -= 1000.0 / 120)
        physics_tick();
    frame_ms += ms;
    if (frame_ms < 1000.0 / 60)
        return nullptr;
    frame_ms -= 1000.0 / 60;
    if (frame_ms > 1000.0 / 60)
        frame_ms = 0;
    game_frame();                       // its Esc never comes: the page keeps it
    const int n = screen_w() * screen_h();
    picture.resize((size_t)n);
    const uint8_t *scr = screen();
    for (int i = 0; i < n; i++) {
        const uint8_t *c = host_palette(scr[i]);
        picture[i] = 0xff000000u | (uint32_t)c[2] << 16 | (uint32_t)c[1] << 8 | c[0];
    }
    return picture.data();
}

EXPORT("web_width") int web_width() { return screen_w(); }
EXPORT("web_height") int web_height() { return screen_h(); }
