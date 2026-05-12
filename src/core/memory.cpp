#include "memory.hpp"

extern "C" void* malloc(size_t);
extern "C" void* realloc(void*, size_t);
extern "C" void free(void*);
extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memmove(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

extern "C" void* _aligned_malloc(size_t size, size_t alignment);
extern "C" void _aligned_free(void* ptr);

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

	void* mmalloc_aligned(usize size, usize alignment) {
		return ::_aligned_malloc(size, alignment);
	}

	void free_aligned(void* ptr) {
		::_aligned_free(ptr);
	}

	Arena arena_create(usize size) {

		Arena arena;
		arena.buffer = (byte*)memory::malloc(size);
		arena.size = size;
		arena.offset = 0;
		return arena;

	}

	void arena_destroy(Arena* arena) {

		memory::free(arena->buffer);
		arena->buffer = nullptr;
		arena->size = 0;
		arena->offset = 0;

	}

	void* arena_alloc(Arena* arena, usize bytes, usize alignment) {

		usize padding = 0;
		usize modulo = arena->offset % alignment;
		if (modulo != 0) {
			padding = alignment - modulo;
		}

		usize padded_offset = arena->offset + padding;

		if (padded_offset + bytes > arena->size) {
			return nullptr;
		}

		void* ptr = arena->buffer + padded_offset;
		arena->offset = padded_offset + bytes;

		return ptr;

	}

	void arena_reset(Arena* arena) {
		arena->offset = 0;
	}

}
