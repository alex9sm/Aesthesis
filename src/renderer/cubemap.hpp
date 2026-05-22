#pragma once

#include "types.hpp"

namespace renderer {

	// Six RGBA8 face buffers in Vulkan cube layer order:
	//   0 = +X (px), 1 = -X (nx),
	//   2 = +Y (py), 3 = -Y (ny),
	//   4 = +Z (pz), 5 = -Z (nz)
	// All faces share the same width = height = size.
	struct CubemapFaces {
		u8* pixels[6];
		u32 size;
	};

	// Loads 6 PNG faces from `assets/textures/global/<name>/{px,nx,py,ny,pz,nz}.png`.
	// On success, `out->pixels[i]` are stb_image-allocated RGBA8 buffers and the
	// caller must release them with `free_cubemap_faces`. Returns false if any
	// face is missing, sizes mismatch, or faces aren't square.
	bool load_cubemap_faces(const char* name, CubemapFaces* out);
	void free_cubemap_faces(CubemapFaces* faces);

}
