/*
 * The minimap: each cell of the map in its colour (col.pcx through the
 * game's palette), darker where it is low, x to the right and y down, as
 * the map is laid out (a camera with yaw 0 looks up the picture).  A car
 * is a rectangle in its own colour at its reference point (where the camera
 * of F1 sits), turned the way the car points, a dark dot at its front, with
 * its number next to it in the game's font; the camera's car on top.  All of it
 * goes through an ImGui draw list over the game's picture.
 */
#include <cmath>
#include <cstdio>
#include "host.hpp"
#include "minimap.hpp"

static const ImU32 CAR_COLOURS[10] = {
    IM_COL32(255, 60, 60, 255), IM_COL32(255, 230, 40, 255), IM_COL32(60, 220, 255, 255), IM_COL32(255, 80, 255, 255),
    IM_COL32(80, 255, 90, 255), IM_COL32(255, 150, 30, 255), IM_COL32(255, 255, 255, 255), IM_COL32(120, 120, 255, 255),
    IM_COL32(180, 255, 180, 255), IM_COL32(255, 180, 200, 255),
};
constexpr int MAP_ALPHA = 150;          // about 60 %

Minimap::Minimap()
{
    static uint32_t pixels[256 * 256];
    for (int i = 0; i < 256 * 256; i++) {
        const uint8_t *rgb = host_palette(colormap()[i]);
        int light = 150 + heightmap()[i] * 106 / 255;          // 150..256: low ground darker
        pixels[i] = 0xff000000u | (uint32_t)(rgb[2] * light >> 8) << 16 |
                    (uint32_t)(rgb[1] * light >> 8) << 8 | (uint32_t)(rgb[0] * light >> 8);
    }
    map.upload(pixels, 256, 256, false);
}

// In the game's own font (font_glyph: columns, bit 0 at the top).
void Minimap::text(ImDrawList *dl, const char *s, float x, float y, float px, ImU32 c) const
{
    for (; *s; s++, x += px) {
        const uint8_t *col = font_glyph((uint8_t)*s);
        for (; *col; col++, x += px)
            for (int row = 0; row < 5; row++)
                if (*col >> row & 1)
                    dl->AddRectFilled(ImVec2(x, y + row * px), ImVec2(x + px, y + (row + 1) * px), c);
    }
}

// Where the car points on the map, in 32 steps (0: up the map, then
// clockwise): the game's integer atan2 of Car::axis_y reversed, from the
// body vertices 4..7 in whole map units.
constexpr int HEADINGS = 32;
static int car_heading(Car &car)
{
    const CarVertex *v = car.vertices();
    Vec3 d = (v[6].pos - v[4].pos) + (v[7].pos - v[5].pos);
    Angle a = fx_atan2((int16_t)(d.x >> 16), (int16_t)-(d.y >> 16));
    return ((a.bits() + 0x8000 / HEADINGS) / (0x10000 / HEADINGS)) % HEADINGS;    // rounded to the nearest
}

// The unit vector of a heading on the minimap (x right, y down).
static ImVec2 heading_vector(int h)
{
    float a = h * (2 * 3.14159265f / HEADINGS);
    return ImVec2(std::sin(a), -std::cos(a));
}

void Minimap::draw(ImDrawList *dl, float x0, float y0, float k, int w, int h) const
{
    // laid out in the game's pixels, then put on the window
    const int size = h * 3 / 10, margin = h / 50;
    const float ax = x0 + (w - size - margin) * k, ay = y0 + (h - size - margin) * k, as = size * k;
    dl->AddImage(map.id(), ImVec2(ax, ay), ImVec2(ax + as, ay + as), ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, MAP_ALPHA));

    const int mark = size / 80 > 1 ? size / 80 : 1, px = h / 200 > 1 ? h / 200 : 1;   // mark: half the width
    // the car of view 1's camera last, on top of the others
    const int n_cars = world.num_cars, last = world.player_car[0];
    for (int j = 1; j <= n_cars; j++) {
        const int i = (last + j) % n_cars;
        Car &car = car_get(i);
        const Pos &p = car.ref_vertex().pos;
        float x = ax + (float)((p.x.raw >> 8) * (uint64_t)size >> 24) * k;  // 8.24 cells -> pixels
        float y = ay + (float)((p.y.raw >> 8) * (uint64_t)size >> 24) * k;
        ImU32 c = CAR_COLOURS[i % 10];

        // a rectangle twice as long as wide, along the car; outlined in black
        ImVec2 f = heading_vector(car_heading(car));
        ImVec2 r(-f.y, f.x);
        auto rect = [&](float len, float wid, ImU32 col) {
            ImVec2 fl(f.x * len, f.y * len), rw(r.x * wid, r.y * wid);
            dl->AddQuadFilled(ImVec2(x + fl.x + rw.x, y + fl.y + rw.y), ImVec2(x + fl.x - rw.x, y + fl.y - rw.y),
                              ImVec2(x - fl.x - rw.x, y - fl.y - rw.y), ImVec2(x - fl.x + rw.x, y - fl.y + rw.y), col);
        };
        float wid = 0.8f * (mark + 0.5f) * k, len = 2 * wid;      // half sizes
        rect(len + k, wid + k, IM_COL32(0, 0, 0, 255));
        rect(len, wid, c);
        dl->AddCircleFilled(ImVec2(x + f.x * len * 0.6f, y + f.y * len * 0.6f), wid * 0.5f, IM_COL32(0, 0, 0, 255));
        char n[8];
        snprintf(n, sizeof n, "%d", i + 1);
        text(dl, n, x + (mark + 2 * px) * k, y - 2 * px * k, px * k, c);
    }
}
