#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer {

	struct Vertex {
		vec3 position;
		vec3 normal;
	};

	struct MeshData {
		Vertex* vertices;
		u32 vertex_count;
		u32* indices;
		u32 index_count;
	};

	// loads the first primitive of the first mesh from the .glb/.gltf at `path`.
	// caller owns the returned arrays and must call free_mesh_data when finished.
	bool load_gltf(const char* path, MeshData* out);
	void free_mesh_data(MeshData* mesh);

}
