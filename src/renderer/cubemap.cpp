// stb_image's IMPLEMENTATION lives in vk_texture.cpp; we just need the decls.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cubemap.hpp"
#include "string.hpp"
#include "log.hpp"
#include "stb_image.h"

namespace renderer {

	static const char* FACE_NAMES[6] = {
		"px", "nx",
		"py", "ny",
		"pz", "nz",
	};

	bool load_cubemap_faces(const char* name, CubemapFaces* out) {
		if (!name || !out) return false;

		for (u32 i = 0; i < 6; i++) out->pixels[i] = nullptr;
		out->size = 0;

		char path[512];
		i32 ref_w = 0, ref_h = 0;

		for (u32 i = 0; i < 6; i++) {
			str::format(path, sizeof(path),
				"assets/textures/global/%s/%s.png", name, FACE_NAMES[i]);

			int w = 0, h = 0, ch = 0;
			stbi_uc* data = stbi_load(path, &w, &h, &ch, 4);
			if (!data) {
				logger::error("Cubemap face missing: %s", path);
				free_cubemap_faces(out);
				return false;
			}

			if (i == 0) {
				if (w != h) {
					logger::error("Cubemap face %s is not square (%dx%d)", path, w, h);
					stbi_image_free(data);
					free_cubemap_faces(out);
					return false;
				}
				ref_w = w;
				ref_h = h;
			}
			else if (w != ref_w || h != ref_h) {
				logger::error("Cubemap face size mismatch: %s is %dx%d, expected %dx%d",
					path, w, h, ref_w, ref_h);
				stbi_image_free(data);
				free_cubemap_faces(out);
				return false;
			}

			out->pixels[i] = (u8*)data;
		}

		out->size = (u32)ref_w;
		return true;
	}

	void free_cubemap_faces(CubemapFaces* faces) {
		if (!faces) return;
		for (u32 i = 0; i < 6; i++) {
			if (faces->pixels[i]) {
				stbi_image_free(faces->pixels[i]);
				faces->pixels[i] = nullptr;
			}
		}
		faces->size = 0;
	}

}
