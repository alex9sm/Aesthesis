#pragma once

#include "types.hpp"

namespace memory {

	void* malloc(usize size);
	void* realloc(void* block, usize size);
	void free(void* block);
	void copy(void* dst, const void* src, usize size);
	void move(void* dst, const void* src, usize size);
	void set(void* dst, byte value, usize size);

}
