#pragma once

#include "mesh.hpp"
#include "math.hpp"
#include "types.hpp"
#include "texture.hpp"

namespace gltf {

	static constexpr i32 MAX_NODES = 64;
	static constexpr i32 MAX_MATERIALS = 32;
	static constexpr i32 MAX_TEXTURES = 32;

	struct Material {
		GLuint diffuse_texture;   // 0 = none
		GLuint normal_texture;    // 0 = none
	};

	struct Node {
		char name[64];
		i32  mesh_index;      // -1 if no mesh
		i32  material_index;  // -1 if no material
		i32  parent;          // -1 if root
		mat4 local_transform;
	};

	struct Model {
		mesh::Mesh meshes[MAX_NODES];
		Node       nodes[MAX_NODES];
		Material   materials[MAX_MATERIALS];
		GLuint     textures[MAX_TEXTURES];
		i32        mesh_count;
		i32        node_count;
		i32        material_count;
		i32        texture_count;

		// local-space axis-aligned bounding box (computed from all mesh vertices)
		vec3       bounds_min;
		vec3       bounds_max;
	};

	bool  load(const char* path, Model* out);
	void  destroy(Model* model);

	i32   find_node(const Model& model, const char* name);

}
