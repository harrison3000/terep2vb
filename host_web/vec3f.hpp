// A 3-vector of floats for the web host's own maths (the OpenGL renderer's
// cameras), with the usual operators.  Same layout as the library's RzVertex
// (x, y, z), but converted explicitly where the library is called.
#pragma once
#include <cmath>

struct Vec3f {
    float x = 0, y = 0, z = 0;

    Vec3f operator+(Vec3f o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3f operator-(Vec3f o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3f operator-() const { return { -x, -y, -z }; }
    Vec3f operator*(float k) const { return { x * k, y * k, z * k }; }
    Vec3f operator/(float k) const { return { x / k, y / k, z / k }; }
    Vec3f &operator+=(Vec3f o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3f &operator-=(Vec3f o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    float dot(Vec3f o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3f cross(Vec3f o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3f normalized() const { float l = length(); return l > 0 ? *this / l : *this; }

    // k of the way to t (0: stays, 1: t): one step of an IIR filter.
    Vec3f toward(Vec3f t, float k) const { return *this + (t - *this) * k; }
};
