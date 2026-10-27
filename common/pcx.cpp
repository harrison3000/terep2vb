#include "pcx.hpp"

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }

// Fails (an empty image) on anything but 8 bits in one plane, or data cut short.
bool PcxImage::decode(const uint8_t *file, size_t size)
{
    constexpr size_t HEADER = 128, PALETTE = 769;      // the palette: 0x0c, then 256 x RGB
    width = height = 0;
    pixels.clear();
    palette.clear();

    // the header: 0x0a, version, encoding (1 RLE, 0 none), bits per pixel,
    // window xmin, ymin, xmax, ymax, ..., planes, bytes per line
    if (!file || size < HEADER)
        return false;
    const uint8_t *h = file;
    unsigned encoding = h[2];
    if (h[0] != 0x0a || encoding > 1 || h[3] != 8 || h[65] != 1)
        return false;
    unsigned xmin = u16(h + 4), ymin = u16(h + 6), xmax = u16(h + 8), ymax = u16(h + 10);
    int line = (int)u16(h + 66);                       // bytes per line, may be padded past the width
    if (xmax < xmin || ymax < ymin || line < (int)(xmax - xmin + 1))
        return false;

    const uint8_t *end = file + size;
    bool has_palette = size >= HEADER + PALETTE && end[-(ptrdiff_t)PALETTE] == 0x0c;
    if (has_palette)
        end -= PALETTE;

    // the pixels, without each line's padding; a run may go on past the end of a line
    int w = (int)(xmax - xmin + 1), ht = (int)(ymax - ymin + 1);
    std::vector<uint8_t> out((size_t)w * ht);
    const uint8_t *src = h + HEADER;
    uint8_t value = 0;
    int left = 0;
    for (int y = 0; y < ht; y++) {
        uint8_t *row = out.data() + (size_t)y * w;
        for (int x = 0; x < line; x++) {
            while (left == 0) {                        // a run of 0: nothing
                if (src >= end)
                    return false;                      // cut short
                uint8_t b = *src++;
                if (encoding == 1 && b >= 0xc0) {
                    if (src >= end)
                        return false;
                    left = b & 0x3f;
                    value = *src++;
                } else {
                    left = 1;
                    value = b;
                }
            }
            if (x < w)
                row[x] = value;
            left--;
        }
    }
    width = w;
    height = ht;
    pixels.swap(out);
    if (has_palette)
        palette.assign(file + size - 768, file + size);
    return true;
}

bool PcxImage::read(FILE *f)
{
    std::vector<uint8_t> file;
    uint8_t buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
        file.insert(file.end(), buf, buf + n);
    return decode(file.data(), file.size());
}

bool PcxImage::load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = read(f);
    fclose(f);
    return ok;
}
