/*
 * Voxel-space terrain (the Comanche technique): for every screen column a
 * ray walks over the height map from near to far; each sample is projected
 * to a screen row and drawn from there down to the highest row drawn so far
 * in that column (a y-buffer), so nearer terrain hides farther terrain and
 * every pixel is written once.  Samples get farther apart with distance and
 * fade into the sky colour.
 *
 * Map units: the map is 256 x 256 cells (the game's 256 units each); a
 * height byte is 16 units, so 16 height steps make one cell.
 */
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include "../common/pcx.hpp"
#include "voxel_preview.hpp"

std::string find_file_nocase(const std::string &dir, const char *name)
{
    std::string found;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
            if (!strcasecmp(e->d_name, name)) {
                found = dir + "/" + e->d_name;
                break;
            }
        }
        closedir(d);
    }
    return found;
}

static uint32_t rgba(const uint8_t *rgb)
{
    return 0xff000000u | (uint32_t)rgb[2] << 16 | (uint32_t)rgb[1] << 8 | rgb[0];
}

static uint32_t blend(uint32_t a, uint32_t b, int t)     // t: 0 (a) .. 256 (b)
{
    uint32_t rb = ((a & 0xff00ff) * (256 - t) + (b & 0xff00ff) * t) >> 8;
    uint32_t g = ((a & 0x00ff00) * (256 - t) + (b & 0x00ff00) * t) >> 8;
    return 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
}

bool VoxelPreview::load(const std::string &dir)
{
    ok = false;
    PcxImage map, col;
    std::string map_path = find_file_nocase(dir, "map.pcx"), col_path = find_file_nocase(dir, "col.pcx");
    if (map_path.empty() || col_path.empty() || !map.load(map_path.c_str()) || !col.load(col_path.c_str()))
        return false;
    if (map.width < 256 || map.height < 256 || col.width < 256 || col.height < 256 || col.palette.size() != 768)
        return false;

    max_height = 0;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            height[y][x] = map.at(x, y);
            if (height[y][x] > max_height)
                max_height = height[y][x];
            colour[y][x] = rgba(&col.palette[col.at(x, y) * 3]);
        }
    }
    ok = true;
    return true;
}

void VoxelPreview::render(uint32_t *out, int w, int h, float angle) const
{
    const uint32_t sky_top = 0xffe0a060, sky_bottom = 0xfff0d0b0;   // ABGR: blue to pale
    for (int y = 0; y < h; y++) {
        uint32_t c = blend(sky_top, sky_bottom, y * 256 / h);
        for (int x = 0; x < w; x++)
            out[y * w + x] = c;
    }
    if (!ok)
        return;

    // camera: circling the middle of the map, looking at it, a bit above the highest point
    const float centre = 128.0f, radius = 185.0f;
    const float cam_x = centre + radius * std::cos(angle), cam_y = centre + radius * std::sin(angle);
    const float dir_x = -std::cos(angle), dir_y = -std::sin(angle);     // towards the middle
    const float cam_h = (float)max_height / 16.0f + 52.0f;             // in cells
    const float horizon = h * 0.22f, scale = h * 0.9f;
    const float half_fov = 0.55f, far = 460.0f;
    const uint32_t fog = sky_bottom;

    for (int sx = 0; sx < w; sx++) {
        float t = (2.0f * sx / (w - 1) - 1.0f) * half_fov;
        float rx = dir_x - dir_y * t, ry = dir_y + dir_x * t;           // not normalised: z is depth
        int ybuf = h;                                                   // lowest row still free
        for (float z = 4.0f, dz = 0.25f; z < far && ybuf > 0; z += dz, dz *= 1.012f) {
            float px = cam_x + rx * z, py = cam_y + ry * z;
            if (px < 0 || py < 0 || px >= 255 || py >= 255)
                continue;                                               // outside the map: nothing
            int ix = (int)px, iy = (int)py;
            float fx = px - ix, fy = py - iy;
            float hgt = ((height[iy][ix] * (1 - fx) + height[iy][ix + 1] * fx) * (1 - fy) +
                         (height[iy + 1][ix] * (1 - fx) + height[iy + 1][ix + 1] * fx) * fy) / 16.0f;
            int sy = (int)((cam_h - hgt) / z * scale + horizon);
            if (sy < 0)
                sy = 0;
            if (sy >= ybuf)
                continue;
            uint32_t c = blend(colour[iy][ix], fog, (int)(z / far * z / far * 256));
            for (int y = sy; y < ybuf; y++)
                out[y * w + sx] = c;
            ybuf = sy;
        }
    }
}
