/*
 * Web host: the game in a page, as WebAssembly (wasm32-wasip1, built as a
 * "reactor": no main, the page calls the exports below).  The page reads
 * the map's files from a folder the player picks (nothing is uploaded),
 * copies them into this module's memory (web_alloc, web_add_file) and
 * starts the game; then, every animation frame, it passes the keys
 * (web_key) and the time (web_advance) and draws the picture it gets back.
 * The original timing, as in the SDL host: physics at 120 Hz, the picture
 * at 60 Hz.  Sound: the game's AdLib writes go to the Nuked-OPL3 emulator,
 * whose samples, made as the time advances, the page plays (web_audio_*).
 * The minimap: the map's picture (web_minimap) and where the cars are
 * (web_car_*).  No OpenGL renderer.  See web/terep2.js.
 */
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>
#include "../host/host.hpp"
extern "C" {
#include "opl3.h"
}

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

opl3_chip chip;
uint32_t audio_rate = 0;        // samples a second, 0: no sound
double audio_due = 0;           // samples owed for the time advanced
std::vector<int16_t> audio;     // the samples of the last web_advance, stereo

void chip_write(uint8_t reg, uint8_t value)
{
    OPL3_WriteReg(&chip, reg, value);
}

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

// Sound at that many samples a second (0: none), for the games started from
// now on.
EXPORT("web_audio_start") void web_audio_start(uint32_t rate)
{
    audio_rate = rate;
    if (rate)
        OPL3_Reset(&chip, rate);
    opl_write_hook = rate ? chip_write : nullptr;
    audio_due = 0;
    audio.clear();
}

// The samples made by the last web_advance: web_audio_frames() stereo pairs
// of int16 at web_audio_samples().
EXPORT("web_audio_samples") const int16_t *web_audio_samples() { return audio.data(); }
EXPORT("web_audio_frames") int web_audio_frames() { return (int)(audio.size() / 2); }

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
    if (audio_rate)
        OPL3_Reset(&chip, audio_rate);  // game_init sets the AdLib up again
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
    if (ms < 0)
        ms = 0;                         // the first animation frame can come stamped before the start
    if (ms > 250)
        ms = 250;                       // after a stall (a hidden tab): do not try to catch up
    for (tick_ms += ms; tick_ms >= 1000.0 / 120; tick_ms -= 1000.0 / 120)
        physics_tick();
    audio.clear();
    if (audio_rate) {                   // the sound of that time (the writes of the last frame)
        audio_due += ms * audio_rate / 1000;
        int n = (int)audio_due;
        audio_due -= n;
        audio.resize((size_t)n * 2);
        OPL3_GenerateStream(&chip, audio.data(), (uint32_t)n);
    }
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

// The minimap's picture: 256 x 256 RGBA, each cell of the map in its colour,
// darker where it is low (as the SDL host's minimap).
EXPORT("web_minimap") const uint32_t *web_minimap()
{
    static uint32_t pixels[256 * 256];
    for (int i = 0; i < 256 * 256; i++) {
        const uint8_t *rgb = host_palette(colormap()[i]);
        int light = 150 + heightmap()[i] * 106 / 255;
        pixels[i] = 0xff000000u | (uint32_t)(rgb[2] * light >> 8) << 16 |
                    (uint32_t)(rgb[1] * light >> 8) << 8 | (uint32_t)(rgb[0] * light >> 8);
    }
    return pixels;
}

// The cars for the minimap: where each one's reference point is, in map
// cells (0..256, x to the right, y down), and where it points, in 32 steps
// (0 up the map, clockwise), as the SDL host's minimap.
EXPORT("web_car_count") int web_car_count() { return running ? world.num_cars : 0; }
EXPORT("web_car_followed") int web_car_followed() { return world.player_car[0]; }
EXPORT("web_car_x") double web_car_x(int i) { return car_get(i).ref_vertex().pos.x.raw / 16777216.0; }
EXPORT("web_car_y") double web_car_y(int i) { return car_get(i).ref_vertex().pos.y.raw / 16777216.0; }
EXPORT("web_car_heading") int web_car_heading(int i)
{
    const CarVertex *v = car_get(i).vertices();
    Vec3 d = (v[6].pos - v[4].pos) + (v[7].pos - v[5].pos);
    Angle a = fx_atan2((int16_t)(d.x >> 16), (int16_t)-(d.y >> 16));
    return ((a.bits() + 0x8000 / 32) / (0x10000 / 32)) % 32;
}
