#pragma once

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;

using i8 = signed char;
using i16 = signed short;
using i32 = signed int;
using i64 = signed long long;

using f32 = float;
using f64 = double;

using byte = unsigned char;

using usize = decltype(sizeof(0));
using isize = decltype((char*)0 - (char*)0);

static_assert(sizeof(u8) == 1,  "u8 not 1 bytes.");
static_assert(sizeof(u16) == 2, "u16 not 2 bytes.");
static_assert(sizeof(u32) == 4, "u32 not 4 bytes.");
static_assert(sizeof(u64) == 8, "u64 not 8 bytes.");

static_assert(sizeof(i8) == 1,  "i8 not 1 bytes.");
static_assert(sizeof(i16) == 2, "i16 not 2 bytes.");
static_assert(sizeof(i32) == 4, "i32 not 4 bytes.");
static_assert(sizeof(i64) == 8, "i64 not 8 bytes.");

static_assert(sizeof(byte) == 1, "byte not 1 byte.");
static_assert(sizeof(usize) == sizeof(void*), "usize not 8 bytes.");
static_assert(sizeof(isize) == sizeof(void*), "isize not 8 bytes.");

#define KILOBYTES(n) ((usize)(n) * 1024)
#define MEGABYTES(n) ((usize)(n) * 1024 * 1024)