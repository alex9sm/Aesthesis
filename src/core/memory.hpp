#pragma once

#include "types.hpp"

namespace memory {

	struct Arena {
		byte* buffer;
		usize size;
		usize offset;
	};

	void* malloc(usize size);
	void* realloc(void* block, usize size);
	void free(void* block);
	void copy(void* dst, const void* src, usize size);
	void move(void* dst, const void* src, usize size);
	void set(void* dst, byte value, usize size);
	void* mmalloc_aligned(usize size, usize alignment);
	void free_aligned(void* ptr);

	Arena arena_create(usize size);
	void arena_destroy(Arena* arena);
	void* arena_alloc(Arena* arena, usize bytes, usize alignment);
	void arena_reset(Arena* arena);

}