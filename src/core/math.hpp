#pragma once

#include "types.hpp"

// ---------------------------------------------------------------------------
// Scalar math. These live in namespace math:: rather than shadowing the libc
// names at global scope, because stb_image.h / stb_truetype.h pull in <math.h>
// in the same translation units and would collide.
//
// sqrt maps to a single SQRTSS/SQRTSD when SSE2 is available (always on x64).
// Define AE_MATH_SSE2 as 0 to force the portable Newton-Raphson path instead.
// ---------------------------------------------------------------------------

#if !defined(AE_MATH_SSE2)
	#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__) || defined(__SSE2__)
		#define AE_MATH_SSE2 1
	#else
		#define AE_MATH_SSE2 0
	#endif
#endif

#if AE_MATH_SSE2
	#include <emmintrin.h>
#endif

namespace math {

	namespace detail {

		union F32Bits { f32 f; u32 u; };
		union F64Bits { f64 f; u64 u; };
		constexpr f32 DP1 = 0.78515625f;
		constexpr f32 DP2 = 2.4187564849853515625e-4f;
		constexpr f32 DP3 = 3.77489497744594108e-8f;

		constexpr f32 FOUR_OVER_PI = 1.27323954473516f;

		inline f32 sin_poly(f32 x, f32 z) {
			return ((-1.9515295891e-4f * z + 8.3321608736e-3f) * z
				- 1.6666654611e-1f) * z * x + x;
		}

		inline f32 cos_poly(f32 z) {
			return ((2.443315711809948e-5f * z - 1.388731625493765e-3f) * z
				+ 4.166664568298827e-2f) * z * z - 0.5f * z + 1.0f;
		}

	}

	inline f32 abs(f32 x) {
		detail::F32Bits b;
		b.f = x;
		b.u &= 0x7FFFFFFFu;
		return b.f;
	}

	inline f32 sqrt(f32 x) {
#if AE_MATH_SSE2
		return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
#else
		if (!(x > 0.0f)) return 0.0f;
		detail::F32Bits b;
		b.f = x;
		b.u = (b.u >> 1) + 0x1FC00000u;
		f32 r = b.f;
		r = 0.5f * (r + x / r);
		r = 0.5f * (r + x / r);
		r = 0.5f * (r + x / r);
		return r;
#endif
	}

	inline f64 sqrt(f64 x) {
#if AE_MATH_SSE2
		return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(x)));
#else
		if (!(x > 0.0)) return 0.0;
		detail::F64Bits b;
		b.f = x;
		b.u = (b.u >> 1) + 0x1FF8000000000000ull;
		f64 r = b.f;
		r = 0.5 * (r + x / r);
		r = 0.5 * (r + x / r);
		r = 0.5 * (r + x / r);
		r = 0.5 * (r + x / r);
		return r;
#endif
	}

	inline f32 sin(f32 x) {

		f32 sign = 1.0f;
		if (x < 0.0f) { x = -x; sign = -1.0f; }

		i32 j = (i32)(x * detail::FOUR_OVER_PI);   // octant index
		f32 y = (f32)j;
		if (j & 1) { j += 1; y += 1.0f; }
		j &= 7;
		if (j > 3) { sign = -sign; j -= 4; }

		x = ((x - y * detail::DP1) - y * detail::DP2) - y * detail::DP3;

		f32 z = x * x;
		f32 r = (j == 1 || j == 2) ? detail::cos_poly(z) : detail::sin_poly(x, z);

		return sign * r;

	}

	inline f32 cos(f32 x) {

		x = abs(x);
		f32 sign = 1.0f;

		i32 j = (i32)(x * detail::FOUR_OVER_PI);
		f32 y = (f32)j;
		if (j & 1) { j += 1; y += 1.0f; }
		j &= 7;
		if (j > 3) { sign = -sign; j -= 4; }
		if (j > 1) { sign = -sign; }

		x = ((x - y * detail::DP1) - y * detail::DP2) - y * detail::DP3;

		f32 z = x * x;
		f32 r = (j == 1 || j == 2) ? detail::sin_poly(x, z) : detail::cos_poly(z);

		return sign * r;

	}

	inline f32 tan(f32 x) {
		return sin(x) / cos(x);
	}

}

struct vec2 { f32 x, y; };
struct vec3 { f32 x, y, z; };
struct vec4 { f32 x, y, z, w; };

struct mat4 { f32 col[4][4]; };

inline vec2 operator+(vec2 a, vec2 b) { return { a.x + b.x, a.y + b.y }; }
inline vec2 operator-(vec2 a, vec2 b) { return { a.x - b.x, a.y - b.y }; }
inline vec2 operator*(vec2 v, f32 s) { return { v.x * s,   v.y * s }; }
inline vec2 operator*(f32 s, vec2 v) { return v * s; }
inline vec2& operator+=(vec2& a, vec2 b) { a.x += b.x; a.y += b.y; return a; }

inline f32  dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline f32  length_sq(vec2 v) { return dot(v, v); }
inline f32  length(vec2 v) { return math::sqrt(length_sq(v)); }
inline vec2 normalize(vec2 v) { f32 l = length(v); return { v.x / l, v.y / l }; }

inline vec3 operator+(vec3 a, vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline vec3 operator-(vec3 a, vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline vec3 operator-(vec3 v) { return { -v.x, -v.y, -v.z }; }
inline vec3 operator*(vec3 v, f32 s) { return { v.x * s, v.y * s, v.z * s }; }
inline vec3 operator*(f32 s, vec3 v) { return v * s; }
inline vec3& operator+=(vec3& a, vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }

inline f32  dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline f32  length_sq(vec3 v) { return dot(v, v); }
inline f32  length(vec3 v) { return math::sqrt(length_sq(v)); }
inline vec3 normalize(vec3 v) { f32 l = length(v); return { v.x / l, v.y / l, v.z / l }; }

inline vec3 cross(vec3 a, vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline vec4 operator+(vec4 a, vec4 b) { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
inline vec4 operator*(vec4 v, f32 s) { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
inline f32  dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline mat4 mat4_identity() {
    mat4 m = {};
    m.col[0][0] = 1.0f;
    m.col[1][1] = 1.0f;
    m.col[2][2] = 1.0f;
    m.col[3][3] = 1.0f;
    return m;
}

inline mat4 mat4_mul(mat4 a, mat4 b) {
    mat4 r = {};
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            r.col[c][row] =
                a.col[0][row] * b.col[c][0] +
                a.col[1][row] * b.col[c][1] +
                a.col[2][row] * b.col[c][2] +
                a.col[3][row] * b.col[c][3];
        }
    }
    return r;
}

inline mat4 operator*(mat4 a, mat4 b) { return mat4_mul(a, b); }

inline mat4 mat4_translate(vec3 t) {
    mat4 m = mat4_identity();
    m.col[3][0] = t.x;
    m.col[3][1] = t.y;
    m.col[3][2] = t.z;
    return m;
}

inline mat4 mat4_scale(vec3 s) {
    mat4 m = mat4_identity();
    m.col[0][0] = s.x;
    m.col[1][1] = s.y;
    m.col[2][2] = s.z;
    return m;
}

inline mat4 mat4_scale(f32 s) {
    return mat4_scale({ s, s, s });
}

inline mat4 mat4_rotate(f32 angle, vec3 axis) {
    f32 c = math::cos(angle);
    f32 s = math::sin(angle);
    f32 t = 1.0f - c;

    mat4 m = {};
    m.col[0][0] = t * axis.x * axis.x + c;
    m.col[0][1] = t * axis.x * axis.y + s * axis.z;
    m.col[0][2] = t * axis.x * axis.z - s * axis.y;

    m.col[1][0] = t * axis.x * axis.y - s * axis.z;
    m.col[1][1] = t * axis.y * axis.y + c;
    m.col[1][2] = t * axis.y * axis.z + s * axis.x;

    m.col[2][0] = t * axis.x * axis.z + s * axis.y;
    m.col[2][1] = t * axis.y * axis.z - s * axis.x;
    m.col[2][2] = t * axis.z * axis.z + c;

    m.col[3][3] = 1.0f;
    return m;
}

inline mat4 mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 r = normalize(cross(f, up));
    vec3 u = cross(r, f);

    mat4 m = {};
    m.col[0][0] = r.x;  m.col[1][0] = r.y;  m.col[2][0] = r.z;  m.col[3][0] = -dot(r, eye);
    m.col[0][1] = u.x;  m.col[1][1] = u.y;  m.col[2][1] = u.z;  m.col[3][1] = -dot(u, eye);
    m.col[0][2] = -f.x;  m.col[1][2] = -f.y;  m.col[2][2] = -f.z;  m.col[3][2] = dot(f, eye);
    m.col[3][3] = 1.0f;
    return m;
}

inline mat4 mat4_perspective(f32 fov_y, f32 aspect, f32 z_near, f32 z_far) {
    f32 f = 1.0f / math::tan(fov_y * 0.5f);
    mat4 m = {};
    m.col[0][0] = f / aspect;
    m.col[1][1] = f;
    m.col[2][2] = (z_far + z_near) / (z_near - z_far);
    m.col[2][3] = -1.0f;
    m.col[3][2] = (2.0f * z_far * z_near) / (z_near - z_far);
    return m;
}

// Vulkan-corrected perspective: clip-space Z is [0, 1] (vs GL's [-1, 1]).
// Pair with a viewport that flips Y so GL-style winding/UV conventions hold.
inline mat4 mat4_perspective_vk(f32 fov_y, f32 aspect, f32 z_near, f32 z_far) {
    f32 f = 1.0f / math::tan(fov_y * 0.5f);
    mat4 m = {};
    m.col[0][0] = f / aspect;
    m.col[1][1] = f;
    m.col[2][2] = z_far / (z_near - z_far);
    m.col[2][3] = -1.0f;
    m.col[3][2] = (z_near * z_far) / (z_near - z_far);
    return m;
}

// Inverse of mat4_perspective_vk: recover near/far from a Vulkan-corrected
// projection matrix. col[2][2] = -f/(f-n), col[3][2] = -n*f/(f-n).
inline void mat4_extract_perspective_vk(const mat4& p, f32* out_near, f32* out_far) {
    f32 A = p.col[2][2];
    f32 B = p.col[3][2];
    f32 n = (A != 0.0f) ? B / A : 0.1f;
    f32 f = ((1.0f + A) != 0.0f) ? (A * n) / (1.0f + A) : 1000.0f;
    if (out_near) *out_near = n;
    if (out_far)  *out_far  = f;
}

inline mat4 mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 z_near, f32 z_far) {
    mat4 m = {};
    m.col[0][0] = 2.0f / (right - left);
    m.col[1][1] = 2.0f / (top - bottom);
    m.col[2][2] = -2.0f / (z_far - z_near);
    m.col[3][0] = -(right + left) / (right - left);
    m.col[3][1] = -(top + bottom) / (top - bottom);
    m.col[3][2] = -(z_far + z_near) / (z_far - z_near);
    m.col[3][3] = 1.0f;
    return m;
}

inline vec3 mat4_transform_point(mat4 m, vec3 p) {
    return {
        m.col[0][0]*p.x + m.col[1][0]*p.y + m.col[2][0]*p.z + m.col[3][0],
        m.col[0][1]*p.x + m.col[1][1]*p.y + m.col[2][1]*p.z + m.col[3][1],
        m.col[0][2]*p.x + m.col[1][2]*p.y + m.col[2][2]*p.z + m.col[3][2]
    };
}

inline vec3 mat4_transform_dir(mat4 m, vec3 d) {
    return {
        m.col[0][0]*d.x + m.col[1][0]*d.y + m.col[2][0]*d.z,
        m.col[0][1]*d.x + m.col[1][1]*d.y + m.col[2][1]*d.z,
        m.col[0][2]*d.x + m.col[1][2]*d.y + m.col[2][2]*d.z
    };
}

inline mat4 mat4_inverse(mat4 m) {
    // cofactor expansion for general 4x4 inverse
    f32 a00 = m.col[0][0], a01 = m.col[0][1], a02 = m.col[0][2], a03 = m.col[0][3];
    f32 a10 = m.col[1][0], a11 = m.col[1][1], a12 = m.col[1][2], a13 = m.col[1][3];
    f32 a20 = m.col[2][0], a21 = m.col[2][1], a22 = m.col[2][2], a23 = m.col[2][3];
    f32 a30 = m.col[3][0], a31 = m.col[3][1], a32 = m.col[3][2], a33 = m.col[3][3];

    f32 b00 = a00*a11 - a01*a10, b01 = a00*a12 - a02*a10;
    f32 b02 = a00*a13 - a03*a10, b03 = a01*a12 - a02*a11;
    f32 b04 = a01*a13 - a03*a11, b05 = a02*a13 - a03*a12;
    f32 b06 = a20*a31 - a21*a30, b07 = a20*a32 - a22*a30;
    f32 b08 = a20*a33 - a23*a30, b09 = a21*a32 - a22*a31;
    f32 b10 = a21*a33 - a23*a31, b11 = a22*a33 - a23*a32;

    f32 det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06;
    if (math::abs(det) < 1e-12f) return mat4_identity();
    f32 inv_det = 1.0f / det;

    mat4 r;
    r.col[0][0] = ( a11*b11 - a12*b10 + a13*b09) * inv_det;
    r.col[0][1] = (-a01*b11 + a02*b10 - a03*b09) * inv_det;
    r.col[0][2] = ( a31*b05 - a32*b04 + a33*b03) * inv_det;
    r.col[0][3] = (-a21*b05 + a22*b04 - a23*b03) * inv_det;
    r.col[1][0] = (-a10*b11 + a12*b08 - a13*b07) * inv_det;
    r.col[1][1] = ( a00*b11 - a02*b08 + a03*b07) * inv_det;
    r.col[1][2] = (-a30*b05 + a32*b02 - a33*b01) * inv_det;
    r.col[1][3] = ( a20*b05 - a22*b02 + a23*b01) * inv_det;
    r.col[2][0] = ( a10*b10 - a11*b08 + a13*b06) * inv_det;
    r.col[2][1] = (-a00*b10 + a01*b08 - a03*b06) * inv_det;
    r.col[2][2] = ( a30*b04 - a31*b02 + a33*b00) * inv_det;
    r.col[2][3] = (-a20*b04 + a21*b02 - a23*b00) * inv_det;
    r.col[3][0] = (-a10*b09 + a11*b07 - a12*b06) * inv_det;
    r.col[3][1] = ( a00*b09 - a01*b07 + a02*b06) * inv_det;
    r.col[3][2] = (-a30*b03 + a31*b01 - a32*b00) * inv_det;
    r.col[3][3] = ( a20*b03 - a21*b01 + a22*b00) * inv_det;
    return r;
}

constexpr f32 PI = 3.14159265358979323846f;
constexpr f32 TAU = 6.28318530717958647692f;

inline f32 to_radians(f32 degrees) { return degrees * (PI / 180.0f); }
inline f32 to_degrees(f32 radians) { return radians * (180.0f / PI); }

struct dvec2 { f64 x, y; };

inline dvec2 operator+(dvec2 a, dvec2 b)  { return { a.x + b.x, a.y + b.y }; }
inline dvec2 operator-(dvec2 a, dvec2 b)  { return { a.x - b.x, a.y - b.y }; }
inline dvec2 operator-(dvec2 v)           { return { -v.x, -v.y }; }
inline dvec2 operator*(dvec2 v, f64 s)    { return { v.x * s, v.y * s }; }
inline dvec2 operator*(f64 s, dvec2 v)    { return v * s; }
inline dvec2 operator/(dvec2 v, f64 s)    { return { v.x / s, v.y / s }; }
inline dvec2& operator+=(dvec2& a, dvec2 b) { a.x += b.x; a.y += b.y; return a; }
inline dvec2& operator-=(dvec2& a, dvec2 b) { a.x -= b.x; a.y -= b.y; return a; }
inline f64   dot_d(dvec2 a, dvec2 b)      { return a.x * b.x + a.y * b.y; }
inline f64   length_sq(dvec2 v)           { return v.x * v.x + v.y * v.y; }
inline f64   length(dvec2 v)              { return math::sqrt(length_sq(v)); }
inline dvec2 normalize_d(dvec2 v)         { f64 l = length(v); return { v.x / l, v.y / l }; }
inline vec2  to_vec2(dvec2 v)             { return { (f32)v.x, (f32)v.y }; }

constexpr f64 PI_D  = 3.14159265358979323846;
constexpr f64 TAU_D = 6.28318530717958647692;

// seeded LCG random, returns float in [0,1]
inline f32 rand_next(u32* seed) {
	*seed = *seed * 1103515245u + 12345u;
	*seed = (*seed >> 16) ^ *seed;
	*seed = *seed * 2654435769u;
	return (f32)(*seed & 0x7FFFFFu) / (f32)0x7FFFFFu;
}

struct AABB {
    vec3 min;
    vec3 max;
};

inline AABB aabb_transform(const AABB& local, mat4 m) {
    vec3 center = (local.min + local.max) * 0.5f;
    vec3 extent = (local.max - local.min) * 0.5f;
    vec3 new_center = mat4_transform_point(m, center);
    vec3 new_extent = {
        math::abs(m.col[0][0]) * extent.x + math::abs(m.col[1][0]) * extent.y + math::abs(m.col[2][0]) * extent.z,
        math::abs(m.col[0][1]) * extent.x + math::abs(m.col[1][1]) * extent.y + math::abs(m.col[2][1]) * extent.z,
        math::abs(m.col[0][2]) * extent.x + math::abs(m.col[1][2]) * extent.y + math::abs(m.col[2][2]) * extent.z
    };

    return { new_center - new_extent, new_center + new_extent };
}

struct Frustum {
    vec4 planes[6];
};

inline Frustum frustum_from_vp(mat4 vp) {
    Frustum f;
    f.planes[0] = { vp.col[0][3]+vp.col[0][0], vp.col[1][3]+vp.col[1][0], vp.col[2][3]+vp.col[2][0], vp.col[3][3]+vp.col[3][0] };
    f.planes[1] = { vp.col[0][3]-vp.col[0][0], vp.col[1][3]-vp.col[1][0], vp.col[2][3]-vp.col[2][0], vp.col[3][3]-vp.col[3][0] };
    f.planes[2] = { vp.col[0][3]+vp.col[0][1], vp.col[1][3]+vp.col[1][1], vp.col[2][3]+vp.col[2][1], vp.col[3][3]+vp.col[3][1] };
    f.planes[3] = { vp.col[0][3]-vp.col[0][1], vp.col[1][3]-vp.col[1][1], vp.col[2][3]-vp.col[2][1], vp.col[3][3]-vp.col[3][1] };
    f.planes[4] = { vp.col[0][3]+vp.col[0][2], vp.col[1][3]+vp.col[1][2], vp.col[2][3]+vp.col[2][2], vp.col[3][3]+vp.col[3][2] };
    f.planes[5] = { vp.col[0][3]-vp.col[0][2], vp.col[1][3]-vp.col[1][2], vp.col[2][3]-vp.col[2][2], vp.col[3][3]-vp.col[3][2] };

    for (int i = 0; i < 6; i++) {
        f32 len = math::sqrt(f.planes[i].x*f.planes[i].x + f.planes[i].y*f.planes[i].y + f.planes[i].z*f.planes[i].z);
        if (len > 0.0001f) {
            f32 inv = 1.0f / len;
            f.planes[i].x *= inv;
            f.planes[i].y *= inv;
            f.planes[i].z *= inv;
            f.planes[i].w *= inv;
        }
    }
    return f;
}

inline bool frustum_test_aabb(const Frustum& f, const AABB& box) {
    for (int i = 0; i < 6; i++) {
        vec3 n = { f.planes[i].x, f.planes[i].y, f.planes[i].z };
        vec3 p = {
            n.x >= 0 ? box.max.x : box.min.x,
            n.y >= 0 ? box.max.y : box.min.y,
            n.z >= 0 ? box.max.z : box.min.z
        };
        if (dot(n, p) + f.planes[i].w < 0.0f) return false;
    }
    return true;
}
