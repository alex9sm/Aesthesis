// stb_image's IMPLEMENTATION lives in vk_texture.cpp; we just need the decls.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cubemap.hpp"
#include "string.hpp"
#include "memory.hpp"
#include "log.hpp"
#include "stb_image.h"

namespace renderer {

	bool load_cubemap_faces(const char* name, CubemapFaces* out) {
		if (!name || !out) return false;

		for (u32 i = 0; i < 6; i++) out->pixels[i] = nullptr;
		out->size = 0;

		char path[512];
		str::format(path, sizeof(path), "assets/textures/global/%s.png", name);

		int w = 0, h = 0, ch = 0;
		stbi_uc* data = stbi_load(path, &w, &h, &ch, 4);
		if (!data) {
			logger::error("Cubemap missing: %s", path);
			return false;
		}

		// Expected layout: 6 faces horizontally → width must be 6 * height.
		if (h <= 0 || w != h * 6) {
			logger::error("Cubemap %s has wrong dimensions %dx%d (expected 6N x N)",
				path, w, h);
			stbi_image_free(data);
			return false;
		}

		u32 size = (u32)h;
		u32 face_bytes = size * size * 4;
		u32 src_stride = (u32)w * 4;        // bytes per row in the source strip
		u32 dst_stride = size * 4;          // bytes per row in a face buffer

		// Copy each square out of the strip into its own contiguous RGBA8 buffer.
		// vk_cubemap's staging path expects each face's pixels packed tightly.
		for (u32 f = 0; f < 6; f++) {
			u8* face_buf = (u8*)memory::malloc(face_bytes);
			if (!face_buf) {
				logger::error("Cubemap %s: failed to allocate face buffer", path);
				stbi_image_free(data);
				free_cubemap_faces(out);
				return false;
			}

			u32 col_offset = f * size * 4;  // byte offset into each row for this face
			for (u32 row = 0; row < size; row++) {
				memory::copy(face_buf + row * dst_stride,
					data + row * src_stride + col_offset,
					dst_stride);
			}
			out->pixels[f] = face_buf;
		}

		stbi_image_free(data);
		out->size = size;
		return true;
	}

	void free_cubemap_faces(CubemapFaces* faces) {
		if (!faces) return;
		for (u32 i = 0; i < 6; i++) {
			if (faces->pixels[i]) {
				memory::free(faces->pixels[i]);
				faces->pixels[i] = nullptr;
			}
		}
		faces->size = 0;
	}

}
