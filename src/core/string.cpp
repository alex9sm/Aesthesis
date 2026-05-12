#include "string.hpp"
#include <math.h>

extern "C" __declspec(dllimport) int __cdecl wvsprintfA(char* buffer, const char* format, va_list args);

namespace str {

	usize length(const char* str) {

		const char* s = str;
		while (*s) {
			s++;
		}

		return (usize)(s - str);

	}

	int compare(const char* a, const char* b) {

		if (a == nullptr || b == nullptr) return (a == b) ? 0 : (a ? 1 : -1);
		while (*a && (*a == *b)) {
			a++;
			b++;
		}

		return *(u8*)a - *(u8*)b;

	}

	bool equal(const char* a, const char* b) {

		return compare(a, b) == 0;

	}

	void copy(char* dst, const char* src, usize max) {

		if (!dst || max == 0) return;
		if (!src) {
			dst[0] = '\0';
			return;
		}

		usize i = 0;
		while (i < max - 1 && src[i] != '\0') {
			dst[i] = src[i];
			i++;
		}
		dst[i] = '\0';

	}

	void concat(char* dst, const char* src, usize max) {

		usize dst_len = length(dst);
		if (dst_len >= max) return;
		copy(dst + dst_len, src, max - dst_len);

	}

	const char* find(const char* stack, const char* target) {

		if (!stack || !target) return nullptr;
		if (!*target) return stack;

		for (; *stack; stack++) {
			if (*stack == *target) {
				const char* h = stack;
				const char* n = target;
				while (*h && *n && *h == *n) {
					h++;
					n++;
				}
				if (!*n) return stack;
			}
		}
		return nullptr;

	}

	const char* find_char(const char* str, char c) {

		if (!str) return nullptr;
		while (*str) {
			if (*str == c) return str;
			str++;
		}

		return nullptr;

	}

	bool starts_with(const char* str, const char* prefix) {

		if (!str || !prefix) return false;
		while (*prefix) {
			if (*prefix++ != *str++) return false;
		}

		return true;

	}

	bool ends_with(const char* str, const char* suffix) {

		if (!str || !suffix) return false;
		usize str_len = length(str);
		usize suf_len = length(suffix);
		if (suf_len > str_len) return false;

		return equal(str + (str_len - suf_len), suffix);

	}

	int format(char* dst, usize max, const char* fmt, ...) {

		if (!dst || max == 0) return 0;

		va_list args;
		va_start(args, fmt);
		int result = wvsprintfA(dst, fmt, args);
		va_end(args);

		if (result >= 0 && (usize)result >= max) {
			dst[max - 1] = '\0';
		}

		return result;

	}

	int format_v(char* dst, usize max, const char* fmt, va_list args) {

		if (!dst || max == 0) return 0;
		int result = wvsprintfA(dst, fmt, args);

		if (result >= 0 && (usize)result >= max) {
			dst[max - 1] = '\0';
		}

		return result;

	}

	int float_to_str(char* buf, usize max, f32 value) {

		if (!buf || max == 0) return 0;

		bool neg = value < 0.0f;
		if (neg) value = -value;

		i32 integer = (i32)value;
		i32 frac = (i32)(((f64)value - (f64)integer) * 100.0 + 0.5);
		if (frac >= 100) { integer++; frac = 0; }

		if (neg)
			return format(buf, max, "-%d.%02d", integer, frac);
		else
			return format(buf, max, "%d.%02d", integer, frac);

	}

	f32 str_to_float(const char* s) {

		if (!s) return 0.0f;
		while (*s == ' ' || *s == '\t') s++;
		if (*s == '\0') return 0.0f;

		f32 sign = 1.0f;
		if (*s == '-') { sign = -1.0f; s++; }
		else if (*s == '+') { s++; }

		f32 result = 0.0f;
		while (*s >= '0' && *s <= '9') {
			result = result * 10.0f + (f32)(*s - '0');
			s++;
		}

		if (*s == '.') {
			s++;
			f32 frac = 0.1f;
			while (*s >= '0' && *s <= '9') {
				result += (f32)(*s - '0') * frac;
				frac *= 0.1f;
				s++;
			}
		}

		return sign * result;

	}

}
