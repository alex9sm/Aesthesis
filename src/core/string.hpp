#pragma once

#include "types.hpp"
#include <stdarg.h>

namespace str {

	usize length(const char* str);
	int compare(const char* a, const char* b);
	bool equal(const char* a, const char* b);
	usize copy(char* dst, const char* src, usize max);
	usize concat(char* dst, const char* src, usize max);

	const char* find(const char* stack, const char* target);
	const char* find_char(const char* str, char c);
	bool starts_with(const char* str, const char* prefix);
	bool ends_with(const char* str, const char* suffix);
	int format(char* dst, usize max, const char* fmt, ...);
	int format_v(char* dst, usize max, const char* fmt, va_list args);

}
