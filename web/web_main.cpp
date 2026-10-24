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
 * (web_car_*).  The OpenGL renderer (host/renderer_gl.cpp, with the
 * renderizeitor library) draws with WebGL 2 on the page's canvas: the
 * library's OpenGL calls are imports done by web/webgl.js
 * (rz_platform_web.cpp), and the text is drawn by the page (web_text_*).
 * See web/terep2.js.
 */
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>
#include "../host/game_renderer.hpp"
#include "../host/host.hpp"
#include "../host/renderer_api.hpp"
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
bool opengl = false;            // this game is drawn by the OpenGL renderer
int view_w = 1, view_h = 1;     // the canvas's size (OpenGL renderer), web_view
double tick_ms = 0, frame_ms = 0;  // time not yet spent on ticks / frames (a little below 0: early)

// The page's animation frames come about every 1/60 s, but not exactly: a
// tick or a frame is done that much early (and the next one that much later)
// rather than wait for the next animation frame, or the same picture would
// show twice and then the game would jump, and the ticks of a frame would go
// 1, 3 instead of 2, 2.
constexpr double TICK_MS = 1000.0 / 120, FRAME_MS = 1000.0 / 60;
constexpr double TICK_EARLY_MS = 1, FRAME_EARLY_MS = 2;
std::vector<uint32_t> picture;  // the last frame, RGBA

opl3_chip chip;
uint32_t audio_rate = 0;        // samples a second, 0: no sound
double audio_due = 0;           // samples owed for the time advanced
std::vector<int16_t> audio;     // the samples of the last web_advance, stereo

void chip_write(uint8_t reg, uint8_t value)
{
    OPL3_WriteReg(&chip, reg, value);
}

struct Text {
    float x, y, size;           // canvas pixels: where the top left corner is, the font's size
    uint32_t rgb;               // 0xRRGGBB
    std::string text;           // lines split by '\n'
};
std::vector<Text> texts;        // the OpenGL renderer's texts of the last frame

const File *find_file(const char *name)
{
    for (const File &f : files)
        if (strcasecmp(f.name.c_str(), name) == 0)
            return &f;
    return nullptr;
}

void stop_game()
{
    if (running) {
        if (opengl)
            opengl_stop();
        host_free_game();
    }
    running = false;
}

} // namespace

// The map's files, read from memory with the case of the name ignored, as
// the SDL host does on the disk.
FILE *data_fopen(const char *name)
{
    const File *f = find_file(name);
    return f ? fmemopen(f->data, f->size, "rb") : nullptr;
}

// The renderizeitor reads its PCX files with fopen: linked with
// --wrap=fopen, they come from memory too (there is no other file here).
extern "C" FILE *__wrap_fopen(const char *name, const char *)
{
    return data_fopen(name);
}

// The map's files for the OpenGL renderer: their names, which are all
// data_fopen needs ("" for a file that is not there).
MapFiles map_files(const std::string &)
{
    MapFiles m;
    auto find = [](const char *name) { const File *f = find_file(name); return f ? f->name : std::string(); };
    m.map = find(fname_map);
    m.col = find(fname_col);
    m.maptex = find(fname_maptex);
    m.textures = find(fname_textures);
    m.fog = find(fname_fog);
    m.smoke = find(fname_smoke);
    for (int i = 0; i < world.num_cars; i++) {
        MapFiles::Car c;
        c.dat = find(car_dat_name(i));
        if (!car_get(i).skin.empty())
            c.pcx = find(car_pcx_name(i));
        m.cars.push_back(c);
    }
    return m;
}

void web_text(float x, float y, float size, const uint8_t *rgb, const char *text)
{
    texts.push_back({ x, y, size, (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2], text });
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
// the extra options (fix_ice_bug, hide_banner), drawn by the game itself or,
// with gl, by the OpenGL renderer (web_view first).  Returns false if the
// map cannot be played (no car1.dat...) or the renderer cannot start.
EXPORT("web_start") bool web_start(int scale, bool fix_ice, bool hide_banner, bool gl)
{
    stop_game();
    render_scale = scale < 1 ? 1 : scale > MAX_RENDER_SCALE ? MAX_RENDER_SCALE : scale;
    extra_options.fix_ice_bug = fix_ice;
    extra_options.hide_banner = hide_banner;
    host_init_state();
    if (audio_rate)
        OPL3_Reset(&chip, audio_rate);  // game_init sets the AdLib up again
    if (game_init() != 0 || world.num_cars <= 0)
        return false;
    opengl = gl;
    texts.clear();
    if (opengl && !opengl_start()) {
        host_free_game();
        return false;
    }
    tick_ms = frame_ms = 0;
    running = true;
    return true;
}

EXPORT("web_stop") void web_stop()
{
    stop_game();
}

// The canvas's size in pixels, for the OpenGL renderer's text (the library
// asks webgl.js for it itself).
EXPORT("web_view") void web_view(int w, int h)
{
    view_w = w < 1 ? 1 : w;
    view_h = h < 1 ? 1 : h;
}

// The OpenGL renderer's draw distance (DRAW_DISTANCE_MIN..MAX tiles): any time.
EXPORT("web_draw_distance") void web_draw_distance(int tiles)
{
    opengl_set_draw_distance(tiles < DRAW_DISTANCE_MIN ? DRAW_DISTANCE_MIN
                             : tiles > DRAW_DISTANCE_MAX ? DRAW_DISTANCE_MAX : tiles);
}

// A key: a PC scancode (set 1), | 0x80 when released.
EXPORT("web_key") void web_key(int scancode)
{
    if (running)
        game_key((uint8_t)scancode);
}

// ms more of time: the physics ticks and the frames it holds.  Returns the
// new picture (web_width() x web_height() RGBA pixels), or null when no new
// frame is due.  With the OpenGL renderer the frame is already on the
// canvas: not null then, with its texts in web_text_*.
EXPORT("web_advance") const uint32_t *web_advance(double ms)
{
    if (!running)
        return nullptr;
    if (ms < 0)
        ms = 0;                         // the first animation frame can come stamped before the start
    if (ms > 250)
        ms = 250;                       // after a stall (a hidden tab): do not try to catch up
    for (tick_ms += ms; tick_ms >= TICK_MS - TICK_EARLY_MS; tick_ms -= TICK_MS) {
        physics_tick();
        if (opengl)
            opengl_tick();
    }
    audio.clear();
    if (audio_rate) {                   // the sound of that time (the writes of the last frame)
        audio_due += ms * audio_rate / 1000;
        int n = (int)audio_due;
        audio_due -= n;
        audio.resize((size_t)n * 2);
        OPL3_GenerateStream(&chip, audio.data(), (uint32_t)n);
    }
    frame_ms += ms;
    if (frame_ms < FRAME_MS - FRAME_EARLY_MS)
        return nullptr;
    frame_ms -= FRAME_MS;
    if (frame_ms > FRAME_MS)
        frame_ms = 0;                   // more than a frame behind: not to be made up
    if (opengl) {
        texts.clear();
        PictureRect where;
        opengl_frame(view_w, view_h, &where);
        static const uint32_t drawn = 0;
        return &drawn;
    }
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

// The OpenGL renderer's texts of the last frame (see Text).
EXPORT("web_text_count") int web_text_count() { return (int)texts.size(); }
EXPORT("web_text_x") float web_text_x(int i) { return texts[i].x; }
EXPORT("web_text_y") float web_text_y(int i) { return texts[i].y; }
EXPORT("web_text_size") float web_text_size(int i) { return texts[i].size; }
EXPORT("web_text_rgb") uint32_t web_text_rgb(int i) { return texts[i].rgb; }
EXPORT("web_text_str") const char *web_text_str(int i) { return texts[i].text.c_str(); }

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
