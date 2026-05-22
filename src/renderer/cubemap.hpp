#pragma once

#include "types.hpp"

namespace renderer {

	// Six RGBA8 face buffers in Vulkan cube layer order:
	//   0 = +X, 1 = -X,
	//   2 = +Y, 3 = -Y,
	//   4 = +Z, 5 = -Z
	// All faces share the same width = height = size.
	struct CubemapFaces {
		u8* pixels[6];
		u32 size;
	};

	// Loads a single PNG at `assets/textures/global/<name>.png` containing the
	// six cubemap faces as a horizontal strip, arranged left-to-right in the
	// order +X, -X, +Y, -Y, +Z, -Z. The image must be 6N wide × N tall (each
	// face is N×N). On success, `out->pixels[i]` are engine-allocated RGBA8
	// face buffers and the caller must release them with `free_cubemap_faces`.
	// Returns false on missing file or wrong aspect ratio.
	bool load_cubemap_faces(const char* name, CubemapFaces* out);
	void free_cubemap_faces(CubemapFaces* faces);

}
