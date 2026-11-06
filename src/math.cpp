/*
 * Table-driven trigonometry, vector length and the 3x3 view rotation.
 */
#include <array>
#include "terep.hpp"

/* sin of 0..90 deg in 1024 steps, Q15 (1.0 clamped to 0x7fff).
 * Taylor series in fixed point (x in Q31, x^2 in Q30), rounding every step;
 * its error stays ~20x below what it would take to flip any entry. */
static constexpr std::array<int16_t, 1025> sin_table = [] {
    std::array<int16_t, 1025> t{};
    const uint64_t half_pi = 3373259426;        /* pi/2 in Q31 */
    for (uint64_t i = 0; i <= 1024; i++) {
        uint64_t x = (half_pi * i + 512) >> 10;
        uint64_t x2 = (x * x + (1ull << 31)) >> 32;
        uint64_t term = x, sum = x;
        for (uint64_t n = 1; n < 8; n++) {
            uint64_t d = 2 * n * (2 * n + 1);
            term = (((term * x2 + (1 << 29)) >> 30) + d / 2) / d;
            sum = (n & 1) ? sum - term : sum + term;
        }
        uint64_t v = (sum + (1 << 15)) >> 16;
        t[i] = (int16_t)(v > 0x7fff ? 0x7fff : v);
    }
    return t;
}();

/* atan(i / 256), i = 0..256, in binary angle units (0x10000 = 360 deg) */
static const uint16_t atan_table[257] = {
    0x0000, 0x0029, 0x0051, 0x007a, 0x00a3, 0x00cc, 0x00f4, 0x011d, 0x0146, 0x016f, 0x0197, 0x01c0,
    0x01e9, 0x0211, 0x023a, 0x0262, 0x028b, 0x02b4, 0x02dc, 0x0305, 0x032d, 0x0356, 0x037e, 0x03a7,
    0x03cf, 0x03f7, 0x0420, 0x0448, 0x0470, 0x0499, 0x04c1, 0x04e9, 0x0511, 0x0539, 0x0561, 0x0589,
    0x05b1, 0x05d9, 0x0601, 0x0629, 0x0651, 0x0678, 0x06a0, 0x06c8, 0x06ef, 0x0717, 0x073e, 0x0766,
    0x078d, 0x07b5, 0x07dc, 0x0803, 0x082a, 0x0851, 0x0878, 0x089f, 0x08c6, 0x08ed, 0x0914, 0x093b,
    0x0961, 0x0988, 0x09ae, 0x09d5, 0x09fb, 0x0a21, 0x0a48, 0x0a6e, 0x0a94, 0x0aba, 0x0ae0, 0x0b06,
    0x0b2c, 0x0b51, 0x0b77, 0x0b9d, 0x0bc2, 0x0be7, 0x0c0d, 0x0c32, 0x0c57, 0x0c7c, 0x0ca1, 0x0cc6,
    0x0ceb, 0x0d10, 0x0d34, 0x0d59, 0x0d7d, 0x0da2, 0x0dc6, 0x0dea, 0x0e0e, 0x0e32, 0x0e56, 0x0e7a,
    0x0e9e, 0x0ec2, 0x0ee5, 0x0f09, 0x0f2c, 0x0f4f, 0x0f73, 0x0f96, 0x0fb9, 0x0fdc, 0x0fff, 0x1021,
    0x1044, 0x1066, 0x1089, 0x10ab, 0x10cd, 0x10f0, 0x1112, 0x1134, 0x1155, 0x1177, 0x1199, 0x11ba,
    0x11dc, 0x11fd, 0x121e, 0x1240, 0x1261, 0x1282, 0x12a2, 0x12c3, 0x12e4, 0x1304, 0x1325, 0x1345,
    0x1365, 0x1386, 0x13a6, 0x13c6, 0x13e5, 0x1405, 0x1425, 0x1444, 0x1464, 0x1483, 0x14a2, 0x14c1,
    0x14e0, 0x14ff, 0x151e, 0x153d, 0x155b, 0x157a, 0x1598, 0x15b6, 0x15d5, 0x15f3, 0x1611, 0x162e,
    0x164c, 0x166a, 0x1687, 0x16a5, 0x16c2, 0x16df, 0x16fd, 0x171a, 0x1737, 0x1753, 0x1770, 0x178d,
    0x17a9, 0x17c6, 0x17e2, 0x17fe, 0x181a, 0x1836, 0x1852, 0x186e, 0x188a, 0x18a6, 0x18c1, 0x18dc,
    0x18f8, 0x1913, 0x192e, 0x1949, 0x1964, 0x197f, 0x199a, 0x19b4, 0x19cf, 0x19e9, 0x1a03, 0x1a1e,
    0x1a38, 0x1a52, 0x1a6c, 0x1a86, 0x1a9f, 0x1ab9, 0x1ad2, 0x1aec, 0x1b05, 0x1b1e, 0x1b38, 0x1b51,
    0x1b6a, 0x1b83, 0x1b9b, 0x1bb4, 0x1bcd, 0x1be5, 0x1bfd, 0x1c16, 0x1c2e, 0x1c46, 0x1c5e, 0x1c76,
    0x1c8e, 0x1ca6, 0x1cbd, 0x1cd5, 0x1cec, 0x1d04, 0x1d1b, 0x1d32, 0x1d49, 0x1d60, 0x1d77, 0x1d8e,
    0x1da5, 0x1dbc, 0x1dd2, 0x1de9, 0x1dff, 0x1e16, 0x1e2c, 0x1e42, 0x1e58, 0x1e6e, 0x1e84, 0x1e9a,
    0x1eaf, 0x1ec5, 0x1edb, 0x1ef0, 0x1f05, 0x1f1b, 0x1f30, 0x1f45, 0x1f5a, 0x1f6f, 0x1f84, 0x1f99,
    0x1fae, 0x1fc2, 0x1fd7, 0x1feb, 0x2000,
};

/* orig 1000:2aad */
Q15 fx_sin(Angle a)
{
    uint16_t t = a.bits() & 0x7fff;
    if (t & 0x4000)
        t = 0x8000 - t;                 /* mirror the second quadrant */
    Q15 v = { sin_table[t >> 4] };
    return (a.bits() & 0x8000) ? -v : v;
}

/* orig 1000:2ad8 */
Q15 fx_cos(Angle a)
{
    uint16_t t = a.bits() & 0x7fff;
    if (t & 0x4000)
        t -= 0x4000;
    else
        t = 0x4000 - t;
    Q15 v = { sin_table[t >> 4] };
    uint16_t quadrant = a.bits() >> 14;
    return (quadrant == 1 || quadrant == 2) ? -v : v;
}

/* atan(num/den) for 0 <= num < den, orig 1000:2b3b */
static Angle atan_ratio(uint16_t num, uint16_t den)
{
    uint16_t q = (uint16_t)(((uint32_t)num << 16) / den);
    return { (int16_t)atan_table[q >> 8] };
}

/* both arguments >= 0, orig 1000:2b2d */
static Angle atan_first_quadrant(int16_t y, int16_t x)
{
    if (y > x)
        return DEG90 - atan_ratio(x, y);
    if (y < x)
        return atan_ratio(y, x);
    return DEG45;
}

/* y >= 0, orig 1000:2b1f */
static Angle atan_upper_half(int16_t y, int16_t x)
{
    if (x < 0)
        return DEG180 - atan_first_quadrant(y, (int16_t)~x);
    if (x == 0)
        return DEG90;
    return atan_first_quadrant(y, x);
}

/* Angle of the vector (x, y), orig 1000:2b08.  Negative coordinates are
 * folded with NOT rather than NEG, as in the original. */
Angle fx_atan2(int16_t y, int16_t x)
{
    if (y < 0)
        return -atan_upper_half((int16_t)~y, x);
    if (y == 0)
        return x < 0 ? DEG180 : Angle{};
    return atan_upper_half(y, x);
}

/* Length of (b, a) computed as a/sin or b/cos of its angle, whichever is
 * better conditioned.  orig 1000:26dd */
int32_t vec2_length(int16_t a, int16_t b)
{
    Angle angle = fx_atan2(a, b);
    uint8_t octant_bits = (angle.bits() >> 8) & 0x60;
    if (octant_bits == 0x20 || octant_bits == 0x40)
        return ((int32_t)a << 15) / fx_sin(angle).raw;
    return ((int32_t)b << 15) / fx_cos(angle).raw;
}

/* orig 1000:271d */
int32_t vec3_length(int16_t a, int16_t b, int16_t c)
{
    return vec2_length((int16_t)vec2_length(a, b), c);
}

/* Divide a 16.16 vector by its length (+1): the result has 1.0 == 0x10000.
 * orig 1000:2726 */
Vec3 vec3_normalize(Vec3 v)
{
    int32_t len = vec3_length(fx_int(v.x), fx_int(v.y), fx_int(v.z)) + 1;
    return { v.x / len, v.y / len, v.z / len };
}

/* Perspective divide, orig 1000:2760.  depth is the camera-space y.  The
 * original divides with 16-bit IDIV and crashes (divide overflow) when the
 * result does not fit; here it wraps around instead (see the README). */
void project(int16_t x, int16_t depth, int16_t z, int16_t *sx, int16_t *sz)
{
    *sx = (int16_t)(((int32_t)x << 8) / depth);
    *sz = (int16_t)(((int32_t)z << 8) / depth);
}

/* Row vector times matrix, every product rounded to Q15 on its own.
 * orig 1000:277e */
void rotate(const Q15 m[9], int16_t x, int16_t y, int16_t z, int16_t out[3])
{
    out[0] = (int16_t)(x * m[0] + y * m[3] + z * m[6]);
    out[1] = (int16_t)(x * m[1] + y * m[4] + z * m[7]);
    out[2] = (int16_t)(x * m[2] + y * m[5] + z * m[8]);
}

/* Rotation matrix from roll, pitch and yaw.  orig 1000:2989 */
void build_matrix(Angles a, Q15 m[9])
{
    Q15 s0 = fx_sin(a.roll), c0 = fx_cos(a.roll);
    Q15 s1 = fx_sin(a.pitch), c1 = fx_cos(a.pitch);
    Q15 s2 = -fx_sin(a.yaw), c2 = fx_cos(a.yaw);

    Q15 s0s1 = s0 * s1;
    Q15 c0c2 = c0 * c2;
    Q15 s2c0 = s2 * c0;

    m[0] = c0c2 + (s0s1 * s2).neg_sat();
    m[1] = (s0s1 * c2 + s2c0).neg_sat();
    m[2] = (s0 * c1).neg_sat();
    m[3] = s2 * c1;
    m[4] = c2 * c1;
    m[5] = s1.neg_sat();
    m[6] = s2c0 * s1 + c2 * s0;
    m[7] = c0c2 * s1 + (s0 * s2).neg_sat();
    m[8] = c0 * c1;
}

/* orig 1000:27f1 */
void transpose(Q15 m[9])
{
    Q15 t;
    t = m[1]; m[1] = m[3]; m[3] = t;
    t = m[2]; m[2] = m[6]; m[6] = t;
    t = m[5]; m[5] = m[7]; m[7] = t;
}
