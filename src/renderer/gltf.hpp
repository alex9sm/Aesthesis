#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer {

	// attribute stream (everything except position). split out from position so
	// position-only passes (depth prepass, shadow cascades) can bind a tight
	// 12 B/vertex stream instead of the full 48 B.
	struct VertexAttribs {
		vec3 normal;
		vec4 tangent;   // .xyz tangent, .w bitangent sign (glTF convention)
		vec2 uv;
	};

	struct MeshData {
		vec3*          positions;   // position stream (12 B/vertex)
		VertexAttribs* attribs;     // normal/tangent/uv stream (36 B/vertex)
		u32 vertex_count;
		u32* indices;
		u32 index_count;
		vec3 aabb_min;   // local-space bounds derived from POSITION accessor
		vec3 aabb_max;
	};

	// loads the first primitive of the first mesh from the .glb/.gltf at `path`.
	// caller owns the returned arrays and must call free_mesh_data when finished.
	bool load_gltf(const char* path, MeshData* out);
	void free_mesh_data(MeshData* mesh);

	// --- whole-model loader (used by renderer::load_model) ---

	// per-primitive geometry + the index of the material it uses (UINT32_MAX
	// for "no material" — caller substitutes the default material).
	struct GltfPrimitive {
		MeshData mesh;
		u32 material_index;
	};

	// material parameters parsed from glTF; texture references are indices
	// into the parent GltfModel::textures array (UINT32_MAX for "no texture",
	// caller substitutes the appropriate reserved default).
	struct GltfMaterial {
		vec4 base_color_factor;
		f32  metallic_factor;
		f32  roughness_factor;
		u32  albedo_index;
		u32  normal_index;
		u32  orm_index;
	};

	// resolved on-disk path of one referenced image, e.g.
	// "assets/models/foo/albedo.jpg".
	struct GltfTexturePath {
		char path[512];
	};

	// a single scene node baked to world-space (within the model), referencing
	// one primitive + the material override for that primitive.
	struct GltfNode {
		u32  primitive_index;
		u32  material_index;
		mat4 world_transform;
	};

	struct GltfModel {
		GltfPrimitive*    primitives;
		u32               primitive_count;
		GltfMaterial*     materials;
		u32               material_count;
		GltfTexturePath*  textures;
		u32               texture_count;
		GltfNode*         nodes;
		u32               node_count;
	};

	bool load_gltf_model(const char* path, GltfModel* out);
	void free_gltf_model(GltfModel* model);

}
