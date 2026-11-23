#pragma once

#include <cmath>
#include <cstring>
#include <cstdio>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <xmmintrin.h>
#define MD_USE_SSE 1
#else
#define MD_USE_SSE 0
#endif

namespace gfx {

    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

        static inline float dot(Vec3 a, Vec3 b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        static inline Vec3 cross(Vec3 a, Vec3 b) {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        }

        static inline Vec3 normalize(Vec3 v) {
            float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return len > 0.0f ? Vec3{ v.x / len, v.y / len, v.z / len } : Vec3{};
        }
    };

    struct Vec4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct Mat4 {
        float m[4][4];

        static inline Mat4 identity() {
            Mat4 res;
            std::memset(&res, 0, sizeof(res));
            res.m[0][0] = 1.0f; res.m[1][1] = 1.0f;
            res.m[2][2] = 1.0f; res.m[3][3] = 1.0f;
            return res;
        }

        static inline Mat4 mul(const Mat4& a, const Mat4& b) {
            Mat4 res;
        #if MD_USE_SSE
            __m128 b0 = _mm_loadu_ps(b.m[0]);
            __m128 b1 = _mm_loadu_ps(b.m[1]);
            __m128 b2 = _mm_loadu_ps(b.m[2]);
            __m128 b3 = _mm_loadu_ps(b.m[3]);
            for (int j = 0; j < 4; j++) {
                __m128 x = _mm_mul_ps(_mm_set1_ps(a.m[j][0]), b0);
                x = _mm_add_ps(x, _mm_mul_ps(_mm_set1_ps(a.m[j][1]), b1));
                x = _mm_add_ps(x, _mm_mul_ps(_mm_set1_ps(a.m[j][2]), b2));
                x = _mm_add_ps(x, _mm_mul_ps(_mm_set1_ps(a.m[j][3]), b3));
                _mm_storeu_ps(res.m[j], x);
            }
        #else
            for (int j = 0; j < 4; j++) {
                for (int i = 0; i < 4; i++) {
                    res.m[j][i] = a.m[j][0] * b.m[0][i] +
                                  a.m[j][1] * b.m[1][i] +
                                  a.m[j][2] * b.m[2][i] +
                                  a.m[j][3] * b.m[3][i];
                }
            }
        #endif
            return res;
        }

        static inline Mat4 transpose(const Mat4& m) {
            Mat4 r;
#if MD_USE_SSE
            __m128 c0 = _mm_loadu_ps(m.m[0]);
            __m128 c1 = _mm_loadu_ps(m.m[1]);
            __m128 c2 = _mm_loadu_ps(m.m[2]);
            __m128 c3 = _mm_loadu_ps(m.m[3]);
            _MM_TRANSPOSE4_PS(c0, c1, c2, c3);
            _mm_storeu_ps(r.m[0], c0);
            _mm_storeu_ps(r.m[1], c1);
            _mm_storeu_ps(r.m[2], c2);
            _mm_storeu_ps(r.m[3], c3);
#else
            for (int c = 0; c < 4; c++)
                for (int row = 0; row < 4; row++)
                    r.m[c][row] = m.m[row][c];
#endif
            return r;
        }

        static inline Vec4 mul_vec4(const Mat4& m, const Vec4& v) {
            Vec4 r;
#if MD_USE_SSE
            __m128 x = _mm_mul_ps(_mm_loadu_ps(m.m[0]), _mm_set1_ps(v.x));
            x = _mm_add_ps(x, _mm_mul_ps(_mm_loadu_ps(m.m[1]), _mm_set1_ps(v.y)));
            x = _mm_add_ps(x, _mm_mul_ps(_mm_loadu_ps(m.m[2]), _mm_set1_ps(v.z)));
            x = _mm_add_ps(x, _mm_mul_ps(_mm_loadu_ps(m.m[3]), _mm_set1_ps(v.w)));
            _mm_storeu_ps(&r.x, x);
#else
            r.x = m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0] * v.w;
            r.y = m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1] * v.w;
            r.z = m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2] * v.w;
            r.w = m.m[0][3] * v.x + m.m[1][3] * v.y + m.m[2][3] * v.z + m.m[3][3] * v.w;
#endif
            return r;
        }

        static inline Mat4 translate(Vec3 t) {
            Mat4 res = identity();
            res.m[3][0] = t.x;
            res.m[3][1] = t.y;
            res.m[3][2] = t.z;
            return res;
        }

        static inline Mat4 scale(Vec3 s) {
            Mat4 r = identity();
            r.m[0][0] = s.x;
            r.m[1][1] = s.y;
            r.m[2][2] = s.z;
            return r;
        }

        static inline Mat4 rotate_x(float angle) {
            Mat4 res = identity();
            float c = cosf(angle);
            float s = sinf(angle);
            res.m[1][1] =  c; res.m[1][2] = s;
            res.m[2][1] = -s; res.m[2][2] = c;
            return res;
        }

        static inline Mat4 rotate_y(float angle) {
            Mat4 res = identity();
            float c = cosf(angle);
            float s = sinf(angle);
            res.m[0][0] = c; res.m[0][2] = -s;
            res.m[2][0] = s; res.m[2][2] =  c;
            return res;
        }

        static inline Mat4 rotate_z(float angle) {
            Mat4 res = identity();
            float c = cosf(angle);
            float s = sinf(angle);
            res.m[0][0] =  c; res.m[0][1] = s;
            res.m[1][0] = -s; res.m[1][1] = c;
            return res;
        }

        static inline Mat4 perspective(float fov, float aspect, float near_plane, float far_plane) {
            float t = tanf(fov / 2.0f);
            Mat4 res;
            std::memset(&res, 0, sizeof(res));
            res.m[0][0] = 1.0f / (aspect * t);
            res.m[1][1] = 1.0f / t;

            // DX12 Z-range [0, 1]
            res.m[2][2] = -far_plane / (far_plane - near_plane);
            res.m[2][3] = -1.0f;
            res.m[3][2] = -(far_plane * near_plane) / (far_plane - near_plane);
            res.m[3][3] = 0.0f;
            return res;
        }

        static inline Mat4 ortho(float l, float r, float b, float t, float znear, float zfar) {
            Mat4 r_mat = identity();
            r_mat.m[0][0] = 2.0f / (r - l);
            r_mat.m[1][1] = 2.0f / (t - b);

            // DX12 Z-range [0, 1]
            r_mat.m[2][2] = -1.0f / (zfar - znear);

            r_mat.m[3][0] = -(r + l) / (r - l);
            r_mat.m[3][1] = -(t + b) / (t - b);
            r_mat.m[3][2] = -znear / (zfar - znear);
            return r_mat;
        }

        static inline Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up) {
            Vec3 f = Vec3::normalize(center - eye);
            Vec3 s = Vec3::normalize(Vec3::cross(f, up));
            Vec3 u = Vec3::cross(s, f);

            Mat4 r = identity();
            r.m[0][0] = s.x;  r.m[1][0] = s.y;  r.m[2][0] = s.z;
            r.m[0][1] = u.x;  r.m[1][1] = u.y;  r.m[2][1] = u.z;
            r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
            r.m[3][0] = -Vec3::dot(s, eye);
            r.m[3][1] = -Vec3::dot(u, eye);
            r.m[3][2] =  Vec3::dot(f, eye);
            return r;
        }
        static inline void print(Mat4 const& m, const char* name = "Mat4") {
            std::printf("%s:\n", name);
            for (int r = 0; r < 4; ++r) {
                std::printf(" [% 10.6f % 10.6f % 10.6f % 10.6f]\n",
                    m.m[r][0],m.m[r][1],m.m[r][2],m.m[r][3]);
            }
        }
    };
}
