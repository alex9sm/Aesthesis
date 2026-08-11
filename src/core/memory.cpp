#include "memory.hpp"
#include <stdlib.h>
#include <string.h>

namespace memory {

	void* malloc(usize size) {
		return ::malloc(size);
	}

	void* realloc(void* block, usize size) {
		return ::realloc(block, size);
	}

	void free(void* block) {
		::free(block);
	}

	void copy(void* dst, const void* src, usize size) {
		::memcpy(dst, src, size);
	}

	void move(void* dst, const void* src, usize size) {
		::memmove(dst, src, size);
	}

	void set(void* dst, byte value, usize size) {
		::memset(dst, value, size);
	}

}
