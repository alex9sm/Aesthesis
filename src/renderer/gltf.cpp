// cgltf uses strncpy/strcpy/fopen which MSVC flags as deprecated. these are safe
// here because cgltf bounds them against fixed-size buffers internally.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "gltf.hpp"
#include "memory.hpp"
#include "string.hpp"
#include "log.hpp"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace renderer {

	// --- shared helpers ---

	static const cgltf_attribute* find_attribute(const cgltf_primitive* prim, cgltf_attribute_type type) {
		for (cgltf_size i = 0; i < prim->attributes_count; i++) {
			if (prim->attributes[i].type == type) return &prim->attributes[i];
		}
		return nullptr;
	}

	// find an attribute with a specific set index (e.g. TEXCOORD_0 has index 0).
	static const cgltf_attribute* find_attribute_indexed(const cgltf_primitive* prim,
		cgltf_attribute_type type, int set_index)
	{
		for (cgltf_size i = 0; i < prim->attributes_count; i++) {
			if (prim->attributes[i].type == type && prim->attributes[i].index == set_index) {
				return &prim->attributes[i];
			}
		}
		return nullptr;
	}

	// unpack one cgltf_primitive into MeshData. returns false if no positions.
	static bool primitive_to_mesh(const cgltf_primitive* prim, MeshData* out) {
		const cgltf_attribute* pos_attr = find_attribute(prim, cgltf_attribute_type_position);
		const cgltf_attribute* nrm_attr = find_attribute(prim, cgltf_attribute_type_normal);
		const cgltf_attribute* tan_attr = find_attribute(prim, cgltf_attribute_type_tangent);
		const cgltf_attribute* uv_attr  = find_attribute_indexed(prim, cgltf_attribute_type_texcoord, 0);

		if (!pos_attr) return false;

		u32 vertex_count = (u32)pos_attr->data->count;
		Vertex* vertices = (Vertex*)memory::malloc(sizeof(Vertex) * vertex_count);
		memory::set(vertices, 0, sizeof(Vertex) * vertex_count);

		for (u32 i = 0; i < vertex_count; i++) {
			f32 p[3] = {};
			cgltf_accessor_read_float(pos_attr->data, i, p, 3);
			vertices[i].position = { p[0], p[1], p[2] };
		}

		// AABB: prefer the POSITION accessor's min/max (exact, free). fall back
		// to scanning vertex positions when the exporter omitted them.
		if (pos_attr->data->has_min && pos_attr->data->has_max) {
			out->aabb_min = { pos_attr->data->min[0], pos_attr->data->min[1], pos_attr->data->min[2] };
			out->aabb_max = { pos_attr->data->max[0], pos_attr->data->max[1], pos_attr->data->max[2] };
		} else if (vertex_count > 0) {
			vec3 lo = vertices[0].position;
			vec3 hi = lo;
			for (u32 i = 1; i < vertex_count; i++) {
				vec3 p = vertices[i].position;
				if (p.x < lo.x) lo.x = p.x; if (p.x > hi.x) hi.x = p.x;
				if (p.y < lo.y) lo.y = p.y; if (p.y > hi.y) hi.y = p.y;
				if (p.z < lo.z) lo.z = p.z; if (p.z > hi.z) hi.z = p.z;
			}
			out->aabb_min = lo;
			out->aabb_max = hi;
		} else {
			out->aabb_min = { 0, 0, 0 };
			out->aabb_max = { 0, 0, 0 };
		}

		if (nrm_attr && nrm_attr->data->count == vertex_count) {
			for (u32 i = 0; i < vertex_count; i++) {
				f32 n[3] = {};
				cgltf_accessor_read_float(nrm_attr->data, i, n, 3);
				vertices[i].normal = { n[0], n[1], n[2] };
			}
		} else {
			for (u32 i = 0; i < vertex_count; i++) {
				vertices[i].normal = { 0.0f, 1.0f, 0.0f };
			}
		}

		// TANGENT (vec4: xyz tangent, w bitangent sign). Missing tangents fall
		// back to {1,0,0,1} — normal mapping will be wrong, but the engine
		// still renders without crashing.
		if (tan_attr && tan_attr->data->count == vertex_count) {
			for (u32 i = 0; i < vertex_count; i++) {
				f32 t[4] = {};
				cgltf_accessor_read_float(tan_attr->data, i, t, 4);
				vertices[i].tangent = { t[0], t[1], t[2], t[3] };
			}
		} else {
			if (!tan_attr) {
				logger::error("glTF primitive missing TANGENT attribute — normal mapping will be incorrect");
			}
			for (u32 i = 0; i < vertex_count; i++) {
				vertices[i].tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
			}
		}

		// TEXCOORD_0
		if (uv_attr && uv_attr->data->count == vertex_count) {
			for (u32 i = 0; i < vertex_count; i++) {
				f32 uv[2] = {};
				cgltf_accessor_read_float(uv_attr->data, i, uv, 2);
				vertices[i].uv = { uv[0], uv[1] };
			}
		}
		// else: uv remains {0,0} from the initial memory::set above.

		u32 index_count = 0;
		u32* indices = nullptr;
		if (prim->indices) {
			index_count = (u32)prim->indices->count;
			indices = (u32*)memory::malloc(sizeof(u32) * index_count);
			for (u32 i = 0; i < index_count; i++) {
				indices[i] = (u32)cgltf_accessor_read_index(prim->indices, i);
			}
		} else {
			index_count = vertex_count;
			indices = (u32*)memory::malloc(sizeof(u32) * index_count);
			for (u32 i = 0; i < index_count; i++) indices[i] = i;
		}

		out->vertices = vertices;
		out->vertex_count = vertex_count;
		out->indices = indices;
		out->index_count = index_count;
		return true;
	}

	// --- single-primitive loader (legacy: used by renderer::load_mesh) ---

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

		if (!primitive_to_mesh(&data->meshes[0].primitives[0], out)) {
			logger::error("glTF primitive missing POSITION: %s", path);
			cgltf_free(data);
			return false;
		}

		logger::info("Loaded glTF '%s' (%u verts, %u indices)",
			path, out->vertex_count, out->index_count);

		cgltf_free(data);
		return true;
	}

	void free_mesh_data(MeshData* mesh) {
		if (mesh->vertices) memory::free(mesh->vertices);
		if (mesh->indices)  memory::free(mesh->indices);
		*mesh = {};
	}

	// --- whole-model loader ---

	// extract directory portion of `path` (with trailing slash) into `out`.
	static void split_directory(const char* path, char* out, usize out_max) {
		usize last_slash = (usize)-1;
		for (usize i = 0; path[i]; i++) {
			if (path[i] == '/' || path[i] == '\\') last_slash = i;
		}
		if (last_slash == (usize)-1) {
			out[0] = '\0';
			return;
		}
		usize n = last_slash + 1;
		if (n >= out_max) n = out_max - 1;
		for (usize i = 0; i < n; i++) out[i] = path[i];
		out[n] = '\0';
	}

	// resolve `uri` against `base_dir`, write into out[out_max].
	static void resolve_uri(const char* base_dir, const char* uri, char* out, usize out_max) {
		str::copy(out, base_dir, out_max);
		str::concat(out, uri, out_max);
	}

	// recursive walk of a cgltf node tree, appending one GltfNode per primitive,
	// per node-with-mesh. parent_matrix is multiplied into the node's local
	// transform to produce the world transform.
	//
	// `prim_offset_per_mesh[i]` gives the starting index in model->primitives
	// for cgltf mesh i's primitives.
	static void walk_node(GltfModel* model, u32* node_capacity,
		const cgltf_data* data, const cgltf_node* node,
		const mat4& parent_world,
		const u32* prim_offset_per_mesh, const u32* mat_index_for_cgltf_mat,
		const cgltf_material** mat_keys, u32 mat_count)
	{
		// compute local transform via cgltf
		f32 local[16] = {};
		cgltf_node_transform_local(node, local);
		// cgltf produces column-major matrix in `local` (cgltf docs).
		mat4 local_mat = {};
		for (u32 c = 0; c < 4; c++)
			for (u32 r = 0; r < 4; r++)
				local_mat.col[c][r] = local[c * 4 + r];

		mat4 world = parent_world * local_mat;

		if (node->mesh) {
			cgltf_size mesh_index = node->mesh - data->meshes;
			u32 prim_base = prim_offset_per_mesh[mesh_index];
			for (cgltf_size p = 0; p < node->mesh->primitives_count; p++) {
				u32 prim_idx = prim_base + (u32)p;
				if (prim_idx >= model->primitive_count) continue;

				// look up material index in our flat materials array
				u32 mat_idx = (u32)~0u;
				const cgltf_material* prim_mat = node->mesh->primitives[p].material;
				if (prim_mat) {
					for (u32 m = 0; m < mat_count; m++) {
						if (mat_keys[m] == prim_mat) { mat_idx = mat_index_for_cgltf_mat[m]; break; }
					}
				}

				// grow nodes array if needed
				if (model->node_count >= *node_capacity) {
					u32 new_cap = (*node_capacity == 0) ? 8 : (*node_capacity * 2);
					GltfNode* new_nodes = (GltfNode*)memory::malloc(sizeof(GltfNode) * new_cap);
					if (model->nodes) {
						memory::copy(new_nodes, model->nodes, sizeof(GltfNode) * model->node_count);
						memory::free(model->nodes);
					}
					model->nodes = new_nodes;
					*node_capacity = new_cap;
				}

				GltfNode& gn = model->nodes[model->node_count++];
				gn.primitive_index = prim_idx;
				gn.material_index  = mat_idx;
				gn.world_transform = world;
			}
		}

		for (cgltf_size i = 0; i < node->children_count; i++) {
			walk_node(model, node_capacity, data, node->children[i], world,
				prim_offset_per_mesh, mat_index_for_cgltf_mat, mat_keys, mat_count);
		}
	}

	bool load_gltf_model(const char* path, GltfModel* out) {
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

		char base_dir[512];
		split_directory(path, base_dir, sizeof(base_dir));

		// --- count + allocate primitives ---
		u32 total_prims = 0;
		for (cgltf_size m = 0; m < data->meshes_count; m++) {
			total_prims += (u32)data->meshes[m].primitives_count;
		}
		if (total_prims == 0) {
			logger::error("glTF has no primitives: %s", path);
			cgltf_free(data);
			return false;
		}

		out->primitives = (GltfPrimitive*)memory::malloc(sizeof(GltfPrimitive) * total_prims);
		memory::set(out->primitives, 0, sizeof(GltfPrimitive) * total_prims);
		out->primitive_count = 0;

		// offset table: prim_offset_per_mesh[meshIndex] = first global primitive index
		u32* prim_offset_per_mesh = (u32*)memory::malloc(sizeof(u32) * data->meshes_count);

		for (cgltf_size m = 0; m < data->meshes_count; m++) {
			prim_offset_per_mesh[m] = out->primitive_count;
			for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++) {
				GltfPrimitive& gp = out->primitives[out->primitive_count];
				if (!primitive_to_mesh(&data->meshes[m].primitives[p], &gp.mesh)) {
					logger::error("glTF primitive missing POSITION: %s (mesh %u prim %u)",
						path, (u32)m, (u32)p);
					gp.mesh = {};
				}
				gp.material_index = (u32)~0u;  // resolved below
				out->primitive_count++;
			}
		}

		// --- materials + textures ---

		// build texture key array (cgltf_image* -> GltfTexturePath index).
		// dedupes by image pointer.
		u32 tex_capacity = 16;
		out->textures = (GltfTexturePath*)memory::malloc(sizeof(GltfTexturePath) * tex_capacity);
		out->texture_count = 0;
		const cgltf_image** image_keys = (const cgltf_image**)memory::malloc(sizeof(const cgltf_image*) * tex_capacity);

		auto add_texture = [&](const cgltf_image* image) -> u32 {
			if (!image || !image->uri) return (u32)~0u;
			for (u32 i = 0; i < out->texture_count; i++) {
				if (image_keys[i] == image) return i;
			}
			if (out->texture_count >= tex_capacity) {
				u32 new_cap = tex_capacity * 2;
				GltfTexturePath* new_paths = (GltfTexturePath*)memory::malloc(sizeof(GltfTexturePath) * new_cap);
				const cgltf_image** new_keys = (const cgltf_image**)memory::malloc(sizeof(const cgltf_image*) * new_cap);
				memory::copy(new_paths, out->textures, sizeof(GltfTexturePath) * out->texture_count);
				memory::copy(new_keys, image_keys, sizeof(const cgltf_image*) * out->texture_count);
				memory::free(out->textures);
				memory::free(image_keys);
				out->textures = new_paths;
				image_keys = new_keys;
				tex_capacity = new_cap;
			}
			u32 idx = out->texture_count++;
			image_keys[idx] = image;
			resolve_uri(base_dir, image->uri, out->textures[idx].path,
				sizeof(out->textures[idx].path));
			return idx;
		};

		// materials are deduped by cgltf_material pointer. cgltf stores them in
		// data->materials[]; the index into that array is naturally unique.
		// We allocate exactly materials_count entries.
		out->materials = nullptr;
		out->material_count = 0;
		const cgltf_material** mat_keys = nullptr;
		u32* mat_index_for_cgltf_mat = nullptr;

		if (data->materials_count > 0) {
			out->materials = (GltfMaterial*)memory::malloc(sizeof(GltfMaterial) * data->materials_count);
			mat_keys = (const cgltf_material**)memory::malloc(sizeof(const cgltf_material*) * data->materials_count);
			mat_index_for_cgltf_mat = (u32*)memory::malloc(sizeof(u32) * data->materials_count);

			for (cgltf_size m = 0; m < data->materials_count; m++) {
				const cgltf_material* cm = &data->materials[m];
				GltfMaterial& gm = out->materials[out->material_count];
				gm.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
				gm.metallic_factor   = 1.0f;
				gm.roughness_factor  = 1.0f;
				gm.albedo_index = (u32)~0u;
				gm.normal_index = (u32)~0u;
				gm.orm_index    = (u32)~0u;

				if (cm->has_pbr_metallic_roughness) {
					const cgltf_pbr_metallic_roughness& pbr = cm->pbr_metallic_roughness;
					gm.base_color_factor = {
						pbr.base_color_factor[0],
						pbr.base_color_factor[1],
						pbr.base_color_factor[2],
						pbr.base_color_factor[3]
					};
					gm.metallic_factor  = pbr.metallic_factor;
					gm.roughness_factor = pbr.roughness_factor;

					if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
						gm.albedo_index = add_texture(pbr.base_color_texture.texture->image);
					}
					// metallic_roughness is treated as the ORM texture by convention.
					if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image) {
						gm.orm_index = add_texture(pbr.metallic_roughness_texture.texture->image);
					}
				}

				if (cm->normal_texture.texture && cm->normal_texture.texture->image) {
					gm.normal_index = add_texture(cm->normal_texture.texture->image);
				}

				// occlusion: ignored unless it points at the same image as MR
				// (engine requires ORM-packed export).
				if (cm->occlusion_texture.texture && cm->occlusion_texture.texture->image) {
					const cgltf_image* occl_img = cm->occlusion_texture.texture->image;
					const cgltf_image* mr_img = cm->has_pbr_metallic_roughness
						? cm->pbr_metallic_roughness.metallic_roughness_texture.texture
							? cm->pbr_metallic_roughness.metallic_roughness_texture.texture->image
							: nullptr
						: nullptr;
					if (occl_img != mr_img) {
						// separate occlusion image — engine requires ORM-packed; ignore.
					}
				}

				mat_keys[out->material_count] = cm;
				mat_index_for_cgltf_mat[out->material_count] = out->material_count;
				out->material_count++;
			}
		}

		// resolve primitive -> material_index by walking meshes again
		u32 prim_cursor = 0;
		for (cgltf_size m = 0; m < data->meshes_count; m++) {
			for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++) {
				const cgltf_material* pm = data->meshes[m].primitives[p].material;
				if (pm) {
					for (u32 mi = 0; mi < out->material_count; mi++) {
						if (mat_keys[mi] == pm) {
							out->primitives[prim_cursor].material_index = mi;
							break;
						}
					}
				}
				prim_cursor++;
			}
		}

		// --- node tree (scene 0, or all top-level nodes if no scene) ---

		u32 node_capacity = 16;
		out->nodes = (GltfNode*)memory::malloc(sizeof(GltfNode) * node_capacity);
		out->node_count = 0;

		const cgltf_scene* scene = data->scene ? data->scene
			: (data->scenes_count > 0 ? &data->scenes[0] : nullptr);

		mat4 ident = {};
		ident.col[0][0] = 1.0f; ident.col[1][1] = 1.0f;
		ident.col[2][2] = 1.0f; ident.col[3][3] = 1.0f;

		if (scene) {
			for (cgltf_size i = 0; i < scene->nodes_count; i++) {
				walk_node(out, &node_capacity, data, scene->nodes[i], ident,
					prim_offset_per_mesh, mat_index_for_cgltf_mat, mat_keys, out->material_count);
			}
		} else {
			// no scene declared: walk all nodes that have no parent.
			for (cgltf_size i = 0; i < data->nodes_count; i++) {
				if (data->nodes[i].parent) continue;
				walk_node(out, &node_capacity, data, &data->nodes[i], ident,
					prim_offset_per_mesh, mat_index_for_cgltf_mat, mat_keys, out->material_count);
			}
		}

		// --- cleanup temporaries ---
		memory::free(prim_offset_per_mesh);
		memory::free(image_keys);
		if (mat_keys) memory::free((void*)mat_keys);
		if (mat_index_for_cgltf_mat) memory::free(mat_index_for_cgltf_mat);

		logger::info("Loaded glTF model '%s' (%u prims, %u materials, %u textures, %u nodes)",
			path, out->primitive_count, out->material_count, out->texture_count, out->node_count);

		cgltf_free(data);
		return true;
	}

	void free_gltf_model(GltfModel* model) {
		if (model->primitives) {
			for (u32 i = 0; i < model->primitive_count; i++) {
				free_mesh_data(&model->primitives[i].mesh);
			}
			memory::free(model->primitives);
		}
		if (model->materials) memory::free(model->materials);
		if (model->textures)  memory::free(model->textures);
		if (model->nodes)     memory::free(model->nodes);
		*model = {};
	}

}
