// cgltf uses strncpy/strcpy/fopen which MSVC flags as deprecated. these are safe
// here because cgltf bounds them against fixed-size buffers internally.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "gltf.hpp"
#include "memory.hpp"
#include "log.hpp"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace renderer {

	static const cgltf_attribute* find_attribute(const cgltf_primitive* prim, cgltf_attribute_type type) {
		for (cgltf_size i = 0; i < prim->attributes_count; i++) {
			if (prim->attributes[i].type == type) return &prim->attributes[i];
		}
		return nullptr;
	}

	bool load_gltf(const char* path, MeshData* out) {
		*out = {};

		cgltf_options options = {};
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse_file(&options, path, &data);
		if (result != cgltf_result_success) {
			logger::error("cgltf_parse_file failed (%d): %s", (int)result, path);
			return false;
		}

		result = cgltf_load_buffers(&options, data, path);
		if (result != cgltf_result_success) {
			logger::error("cgltf_load_buffers failed (%d): %s", (int)result, path);
			cgltf_free(data);
			return false;
		}

		if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) {
			logger::error("glTF has no meshes/primitives: %s", path);
			cgltf_free(data);
			return false;
		}

		const cgltf_primitive* prim = &data->meshes[0].primitives[0];

		const cgltf_attribute* pos_attr = find_attribute(prim, cgltf_attribute_type_position);
		const cgltf_attribute* nrm_attr = find_attribute(prim, cgltf_attribute_type_normal);

		if (!pos_attr) {
			logger::error("glTF primitive missing POSITION: %s", path);
			cgltf_free(data);
			return false;
		}

		u32 vertex_count = (u32)pos_attr->data->count;
		Vertex* vertices = (Vertex*)memory::malloc(sizeof(Vertex) * vertex_count);
		memory::set(vertices, 0, sizeof(Vertex) * vertex_count);

		// unpack positions
		for (u32 i = 0; i < vertex_count; i++) {
			f32 p[3] = {};
			cgltf_accessor_read_float(pos_attr->data, i, p, 3);
			vertices[i].position = { p[0], p[1], p[2] };
		}

		// unpack normals (if present)
		if (nrm_attr && nrm_attr->data->count == vertex_count) {
			for (u32 i = 0; i < vertex_count; i++) {
				f32 n[3] = {};
				cgltf_accessor_read_float(nrm_attr->data, i, n, 3);
				vertices[i].normal = { n[0], n[1], n[2] };
			}
		} else {
			// default upward normals when missing
			for (u32 i = 0; i < vertex_count; i++) {
				vertices[i].normal = { 0.0f, 1.0f, 0.0f };
			}
		}

		// indices
		u32 index_count = 0;
		u32* indices = nullptr;
		if (prim->indices) {
			index_count = (u32)prim->indices->count;
			indices = (u32*)memory::malloc(sizeof(u32) * index_count);
			for (u32 i = 0; i < index_count; i++) {
				indices[i] = (u32)cgltf_accessor_read_index(prim->indices, i);
			}
		} else {
			// non-indexed: synthesize sequential indices
			index_count = vertex_count;
			indices = (u32*)memory::malloc(sizeof(u32) * index_count);
			for (u32 i = 0; i < index_count; i++) indices[i] = i;
		}

		out->vertices = vertices;
		out->vertex_count = vertex_count;
		out->indices = indices;
		out->index_count = index_count;

		logger::info("Loaded glTF '%s' (%u verts, %u indices)", path, vertex_count, index_count);

		cgltf_free(data);
		return true;
	}

	void free_mesh_data(MeshData* mesh) {
		if (mesh->vertices) memory::free(mesh->vertices);
		if (mesh->indices)  memory::free(mesh->indices);
		*mesh = {};
	}

}
