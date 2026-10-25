#include "rz_internal.h"

namespace rz {

Mat4 Mat4::identity() {
    Mat4 m{};
    m[0, 0] = 1.0f; m[1, 1] = 1.0f; m[2, 2] = 1.0f; m[3, 3] = 1.0f;
    return m;
}

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i, j] = a[i, 0] * b[0, j] + a[i, 1] * b[1, j]
                    + a[i, 2] * b[2, j] + a[i, 3] * b[3, j];
        }
    }
    return r;
}

// Gribb-Hartmann: cada plano é a linha 3 ± uma das linhas 0, 1, 2
Frustum frustumOf(const Mat4& m) {
    Frustum f;
    for (int i = 0; i < 6; ++i) {
        const int row = i / 2;
        const float sign = (i & 1) ? -1.0f : 1.0f;
        for (int j = 0; j < 4; ++j) f.plane[i][j] = m[3, j] + sign * m[row, j];
    }
    return f;
}

// Fora se o canto mais para dentro de algum plano ainda está fora dele
bool boxInFrustum(const Frustum& f, Vec3 lo, Vec3 hi) {
    for (const auto& p : f.plane) {
        const float x = p[0] >= 0.0f ? hi.x : lo.x;
        const float y = p[1] >= 0.0f ? hi.y : lo.y;
        const float z = p[2] >= 0.0f ? hi.z : lo.z;
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f) return false;
    }
    return true;
}

} // namespace rz
