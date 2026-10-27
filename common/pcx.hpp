// Reading 8-bit PCX images (ZSoft: one plane, RLE or not, the VGA palette at
// the end), shared by the game (src/load.cpp, which crops them into its
// segments), the SDL host (the map preview) and the web's renderer
// (host_web/rz, the textures).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct PcxImage {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;    // width * height palette indices, row by row, the top first
    std::vector<uint8_t> palette;   // 768 bytes (RGB, 8 bits), empty if the file has none

    bool decode(const uint8_t *file, size_t size);   // the whole file, in memory
    bool read(FILE *f);                              // the rest of an open file (not closed)
    bool load(const char *path);
    uint8_t at(int x, int y) const { return pixels[(size_t)y * width + x]; }
};
