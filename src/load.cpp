/*
 * Loading a track directory: the map layers (PCX images), the fog and
 * translucency tables, and up to five cars.
 *
 * File names are the original's (fname_*, terep.hpp); the host opens them
 * (data_fopen, case-insensitive on systems that care).
 */
#include <cstdlib>
#include <cstring>
#include "terep.hpp"
#include "../common/pcx.hpp"

/* ------------------------------------------------------------------------
 * PCX (8-bit, single plane, RLE): read by common/pcx, then put in a segment
 * ---------------------------------------------------------------------- */

/* At most 256 x 256 pixels, as the original: wider images are cropped (rows
 * are packed, min(width, 256) apart, not padded to 256), taller ones cut at
 * 256 rows.  The original (orig 1000:5acf) did this while decoding, reading
 * the file in 768-byte chunks, and checked only the bits per pixel and the
 * planes of the header (1000:5a95). */
static void pcx_to_segment(const PcxImage &img, uint8_t *dst)
{
    int w = img.width < 0x100 ? img.width : 0x100;
    int h = img.height < 0x100 ? img.height : 0x100;
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * w, &img.pixels[(size_t)y * img.width], (size_t)w);
}

static bool pcx_read(const char *name, PcxImage &img)
{
    FILE *f = data_fopen(name);
    if (!f)
        return false;
    bool ok = img.read(f);
    fclose(f);
    return ok;
}

/* orig 1000:5a60 */
static bool pcx_load(const char *name, uint8_t *dst)
{
    PcxImage img;
    if (!pcx_read(name, img))
        return false;
    pcx_to_segment(img, dst);
    return true;
}

static void read_table(const char *name, void *dst, size_t size)
{
    FILE *f = data_fopen(name);
    if (!f)
        return;
    fread(dst, 1, size, f);
    fclose(f);
}

/* Height map, terrain textures, colour map with the palette, fog and
 * translucency tables.  Stops at the first missing image (the original
 * goes on with whatever is in memory).  orig 1000:24c0 */
static void load_map()
{
    if (!pcx_load(fname_map, heightmap()))
        return;
    if (!pcx_load(fname_maptex, segments.maptex.data()))
        return;

    PcxImage col;                       /* the tile of each cell, and the palette */
    if (!pcx_read(fname_col, col))
        return;
    if (col.palette.size() == sizeof map_tables.palette)
        memcpy(map_tables.palette, col.palette.data(), sizeof map_tables.palette);
    pcx_to_segment(col, colormap());

    read_table(fname_fog, map_tables.fog_table, sizeof map_tables.fog_table);
    read_table(fname_smoke, map_tables.shade_table, sizeof map_tables.shade_table);
}

/* ------------------------------------------------------------------------
 * Cars
 * ---------------------------------------------------------------------- */

/* Load a car file and put it on the map at (x, y), 100 units above the
 * ground.  Returns the file size, 0 if it is missing.  orig 1000:2454 */
static uint16_t load_car(Car &car, const char *name, uint16_t x, uint16_t y)
{
    FILE *f = data_fopen(name);
    if (!f)
        return 0;
    uint16_t size = car_read(car, f);
    fclose(f);

    int16_t z = (int16_t)(terrain_height(x, y) + 100);
    Vec3 offset = { (int32_t)Coord::from_units(x).raw, (int32_t)Coord::from_units(y).raw,
                    (int32_t)Coord::from_units((uint16_t)z).raw };
    for (CarVertex &v : car.points)
        v.pos += offset;
    return size;
}

/* The reference vertex (camera anchor) is the one marked 0xffff, else the
 * first.  The original takes its loop count from the low word of the
 * first vertex's x instead of the vertex count (0 counting as 65536), and
 * would go on reading past the vertices; here the search stops at the
 * last vertex.  orig 1000:2431 */
static void find_ref_vertex(Car &car)
{
    uint32_t count = (uint16_t)car.points[0].pos.x.raw;
    if (count == 0)
        count = 0x10000;
    car.ref_index = 0;
    for (uint32_t i = 0; i < count && i < car.points.size(); i++) {
        if (car.points[i].wheel == 0xffff) {
            car.ref_index = (uint16_t)i;
            return;
        }
    }
}

/* The car's skin: a PCX of up to 256 pixels width in its own segment.
 * Without it the car uses textures.pcx. */
static void load_car_skin(Car &car, const char *name)
{
    FILE *f = data_fopen(name);
    if (!f)
        return;
    PcxImage img;
    bool ok = img.read(f);
    fclose(f);
    car.skin.assign(SEGMENT_SIZE, 0);   /* the original took any file there was */
    if (ok)
        pcx_to_segment(img, car.skin.data());
}

static const char *const car_dat_names[MAX_CARS] = { "car1.dat", "car2.dat", "car3.dat", "car4.dat", "car5.dat" };
static const char *const car_pcx_names[MAX_CARS] = { "car1.pcx", "car2.pcx", "car3.pcx", "car4.pcx", "car5.pcx" };
const char *car_dat_name(int i) { return car_dat_names[i]; }
const char *car_pcx_name(int i) { return car_pcx_names[i]; }

/* Cars are loaded until a carN.dat is missing, lined up along y = 0x7a00
 * every 256 units of x, at most 5 (the original's name tables hold 5; a
 * sixth car took its name from the bytes after them). */
static void load_cars()
{
    world.num_cars = 0;
    for (int i = 0; i < MAX_CARS; i++) {
        const char *dat = car_dat_name(i), *pcx = car_pcx_name(i);
        Car &car = cars[i];
        uint16_t size = load_car(car, dat, (uint16_t)(0x8000 - i * 256), 0x7a00);
        if (!size || car.points.empty())
            break;
        find_ref_vertex(car);
        load_car_skin(car, pcx);
        world.num_cars++;
    }
}

/* ------------------------------------------------------------------------ */

/* Changes render_scale during a game: only the hires screen depends on it. */
void game_set_scale(int scale)
{
    render_scale = scale;
    segments.hires_screen.assign(scale > 1 ? (size_t)screen_w() * screen_h() : 0, 0);
}

/* The segments and the cars, freed. */
void game_free()
{
    segments = Segments();
    for (Car &c : cars)
        c = Car();
}

/* Returns 0 on success.  orig f_init */
int game_init()
{
    /* what state_reset does not cover, as at the start of the program */
    memset(keys, 0xff, sizeof keys);
    game_free();
    game_frame_reset();

    world.num_cars = -2;
    world.gravity = 0x1800;
    world.spring_break_stretch = 0x800;
    world.spring_break_squash = (int16_t)0xf000;
    render.clip_left = 0;
    render.screen_cx = 160;
    render.clip_right = 319;
    render.clip_top = 0;
    render.screen_cy = 80;
    render.clip_bottom = 199;

    FILE *cfg = data_fopen(fname_cfg);   /* optional spring limits */
    if (cfg) {
        fread(&world.spring_break_stretch, 1, 2, cfg);
        fread(&world.spring_break_squash, 1, 2, cfg);
        fclose(cfg);
    }

    for (auto *s : { &segments.heightmap, &segments.colormap, &segments.textures, &segments.maptex, &segments.screen })
        s->assign(SEGMENT_SIZE, 0);
    load_map();
    pcx_load(fname_textures, segments.textures.data());      /* orig 1000:255c */
    load_cars();
    if (render_scale > 1)
        segments.hires_screen.assign((size_t)screen_w() * screen_h(), 0);
    sound_init();
    world.sim_running = 1;
    return 0;
}
