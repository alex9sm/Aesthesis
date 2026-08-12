#define _CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include "../../dependencies/cgltf.h"

#include "gltf.hpp"
#include "file.hpp"
#include "memory.hpp"
#include "log.hpp"
#include "string.hpp"
#include "texture.hpp"

namespace gltf {

	static vec3 read_vec3(const float* f) { return { f[0], f[1], f[2] }; }
	static vec2 read_vec2(const float* f) { return { f[0], f[1] }; }

	static mat4 cgltf_node_to_mat4(const cgltf_node* node) {
		mat4 m = mat4_identity();

		if (node->has_matrix) {
			for (int c = 0; c < 4; c++)
				for (int r = 0; r < 4; r++)
					m.col[c][r] = node->matrix[c * 4 + r];
			return m;
		}

		mat4 t = mat4_identity();
		mat4 r = mat4_identity();
		mat4 s = mat4_identity();

		if (node->has_translation) {
			t = mat4_translate({ node->translation[0], node->translation[1], node->translation[2] });
		}

		if (node->has_rotation) {
			f32 qx = node->rotation[0];
			f32 qy = node->rotation[1];
			f32 qz = node->rotation[2];
			f32 qw = node->rotation[3];

			r.col[0][0] = 1.0f - 2.0f * (qy*qy + qz*qz);
			r.col[0][1] = 2.0f * (qx*qy + qw*qz);
			r.col[0][2] = 2.0f * (qx*qz - qw*qy);

			r.col[1][0] = 2.0f * (qx*qy - qw*qz);
			r.col[1][1] = 1.0f - 2.0f * (qx*qx + qz*qz);
			r.col[1][2] = 2.0f * (qy*qz + qw*qx);

			r.col[2][0] = 2.0f * (qx*qz + qw*qy);
			r.col[2][1] = 2.0f * (qy*qz - qw*qx);
			r.col[2][2] = 1.0f - 2.0f * (qx*qx + qy*qy);

			r.col[3][3] = 1.0f;
		}

		if (node->has_scale) {
			s = mat4_scale({ node->scale[0], node->scale[1], node->scale[2] });
		}

		return t * r * s;
	}

	static bool load_mesh_primitive(const cgltf_primitive& prim, mesh::Mesh* out,
	                                vec3* bounds_min, vec3* bounds_max) {
		if (prim.type != cgltf_primitive_type_triangles) return false;

		const cgltf_accessor* pos_accessor = nullptr;
		const cgltf_accessor* norm_accessor = nullptr;
		const cgltf_accessor* uv_accessor = nullptr;

		for (cgltf_size i = 0; i < prim.attributes_count; i++) {
			if (prim.attributes[i].type == cgltf_attribute_type_position)
				pos_accessor = prim.attributes[i].data;
			else if (prim.attributes[i].type == cgltf_attribute_type_normal)
				norm_accessor = prim.attributes[i].data;
			else if (prim.attributes[i].type == cgltf_attribute_type_texcoord)
				uv_accessor = prim.attributes[i].data;
		}

		if (!pos_accessor) return false;

		i32 vertex_count = (i32)pos_accessor->count;
		mesh::Vertex* vertices = (mesh::Vertex*)memory::malloc(vertex_count * sizeof(mesh::Vertex));
		memory::set(vertices, 0, vertex_count * sizeof(mesh::Vertex));

		for (i32 i = 0; i < vertex_count; i++) {
			float tmp[3];
			cgltf_accessor_read_float(pos_accessor, i, tmp, 3);
			vertices[i].position = read_vec3(tmp);

			// accumulate bounds
			if (vertices[i].position.x < bounds_min->x) bounds_min->x = vertices[i].position.x;
			if (vertices[i].position.y < bounds_min->y) bounds_min->y = vertices[i].position.y;
			if (vertices[i].position.z < bounds_min->z) bounds_min->z = vertices[i].position.z;
			if (vertices[i].position.x > bounds_max->x) bounds_max->x = vertices[i].position.x;
			if (vertices[i].position.y > bounds_max->y) bounds_max->y = vertices[i].position.y;
			if (vertices[i].position.z > bounds_max->z) bounds_max->z = vertices[i].position.z;

			if (norm_accessor) {
				cgltf_accessor_read_float(norm_accessor, i, tmp, 3);
				vertices[i].normal = read_vec3(tmp);
			} else {
				vertices[i].normal = { 0.0f, 1.0f, 0.0f };
			}

			if (uv_accessor) {
				float uv[2];
				cgltf_accessor_read_float(uv_accessor, i, uv, 2);
				vertices[i].texcoord = read_vec2(uv);
			}
		}

		if (prim.indices) {
			i32 index_count = (i32)prim.indices->count;
			u32* indices = (u32*)memory::malloc(index_count * sizeof(u32));
			for (i32 i = 0; i < index_count; i++) {
				indices[i] = (u32)cgltf_accessor_read_index(prim.indices, i);
			}
			*out = mesh::create(vertices, vertex_count, indices, index_count);
			memory::free(indices);
		} else {
			*out = mesh::create_no_indices(vertices, vertex_count);
		}

		memory::free(vertices);
		return true;
	}

	static void get_directory(char* out, usize max, const char* path) {
		str::copy(out, path, max);
		i32 last_slash = -1;
		for (i32 i = 0; out[i]; i++) {
			if (out[i] == '/' || out[i] == '\\') last_slash = i;
		}
		if (last_slash >= 0) {
			out[last_slash + 1] = '\0';
		} else {
			out[0] = '\0';
		}
	}

	bool load(const char* path, Model* out) {
		memory::set(out, 0, sizeof(Model));

		// read file into memory
		u64 file_size = 0;
		if (!file::get_size(path, &file_size)) {
			logger::error("glTF file not found: %s", path);
			return false;
		}

		void* file_data = memory::malloc((usize)file_size);
		file::read_file(path, file_data, (usize)file_size);

		cgltf_options options = {};
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse(&options, file_data, (cgltf_size)file_size, &data);
		if (result != cgltf_result_success) {
			logger::error("Failed to parse glTF: %s", path);
			memory::free(file_data);
			return false;
		}

	result = cgltf_load_buffers(&options, data, path);
	if (result != cgltf_result_success) {
		logger::error("Failed to load glTF buffers (result=%d): %s", (i32)result, path);
			cgltf_free(data);
			memory::free(file_data);
			return false;
		}

		// get base directory for resolving relative texture paths
		char base_dir[256];
		get_directory(base_dir, sizeof(base_dir), path);

		// load textures (images)
		for (cgltf_size i = 0; i < data->images_count && out->texture_count < MAX_TEXTURES; i++) {
			cgltf_image& img = data->images[i];
			if (img.uri) {
				char tex_path[256];
				str::copy(tex_path, base_dir, sizeof(tex_path));
				str::concat(tex_path, img.uri, sizeof(tex_path));

				texture::Texture tex = texture::load(tex_path, false);
				if (tex.id) {
					// generate mipmaps for better filtering
					gl::BindTexture(GL_TEXTURE_2D, tex.id);
					gl::GenerateMipmap(GL_TEXTURE_2D);
					gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
					gl::BindTexture(GL_TEXTURE_2D, 0);

					out->textures[out->texture_count] = tex.id;
					} else {
					out->textures[out->texture_count] = 0;
					logger::warn("  Failed to load texture: %s", tex_path);
				}
				out->texture_count++;
			}
		}

		// load materials
		for (cgltf_size i = 0; i < data->materials_count && out->material_count < MAX_MATERIALS; i++) {
			cgltf_material& mat = data->materials[i];
			Material& m = out->materials[out->material_count];
			m.diffuse_texture = 0;
			m.normal_texture = 0;

			// base color texture
			if (mat.has_pbr_metallic_roughness && mat.pbr_metallic_roughness.base_color_texture.texture) {
				cgltf_texture* tex = mat.pbr_metallic_roughness.base_color_texture.texture;
				if (tex->image) {
					i32 img_index = (i32)(tex->image - data->images);
					if (img_index >= 0 && img_index < out->texture_count) {
						m.diffuse_texture = out->textures[img_index];
					}
				}
			}

			// normal texture
			if (mat.normal_texture.texture) {
				cgltf_texture* tex = mat.normal_texture.texture;
				if (tex->image) {
					i32 img_index = (i32)(tex->image - data->images);
					if (img_index >= 0 && img_index < out->texture_count) {
						m.normal_texture = out->textures[img_index];
					}
				}
			}

			out->material_count++;
		}

		// load meshes — also track which material each mesh uses
		// We store material indices in a temp array indexed by mesh index
		i32 mesh_material_indices[MAX_NODES];
		for (i32 i = 0; i < MAX_NODES; i++) mesh_material_indices[i] = -1;

		out->bounds_min = {  1e18f,  1e18f,  1e18f };
		out->bounds_max = { -1e18f, -1e18f, -1e18f };

		for (cgltf_size i = 0; i < data->meshes_count && out->mesh_count < MAX_NODES; i++) {
			cgltf_mesh& gltf_mesh = data->meshes[i];
			if (gltf_mesh.primitives_count > 0) {
				if (load_mesh_primitive(gltf_mesh.primitives[0], &out->meshes[out->mesh_count],
				                        &out->bounds_min, &out->bounds_max)) {
					// get material index from first primitive
					cgltf_material* prim_mat = gltf_mesh.primitives[0].material;
					if (prim_mat) {
						mesh_material_indices[out->mesh_count] = (i32)(prim_mat - data->materials);
					}
					out->mesh_count++;
				}
			}
		}

		// load nodes
		for (cgltf_size i = 0; i < data->nodes_count && out->node_count < MAX_NODES; i++) {
			cgltf_node& gltf_node = data->nodes[i];
			Node& node = out->nodes[out->node_count];

			if (gltf_node.name) {
				str::copy(node.name, gltf_node.name, sizeof(node.name));
			} else {
				str::format(node.name, sizeof(node.name), "node_%d", (i32)i);
			}

			node.local_transform = cgltf_node_to_mat4(&gltf_node);

			if (gltf_node.mesh) {
				node.mesh_index = (i32)(gltf_node.mesh - data->meshes);
				node.material_index = mesh_material_indices[node.mesh_index];
			} else {
				node.mesh_index = -1;
				node.material_index = -1;
			}

			if (gltf_node.parent) {
				node.parent = (i32)(gltf_node.parent - data->nodes);
			} else {
				node.parent = -1;
			}

			out->node_count++;
		}

		cgltf_free(data);
		memory::free(file_data);

		return true;
	}

	void destroy(Model* model) {
		for (i32 i = 0; i < model->mesh_count; i++) {
			mesh::destroy(&model->meshes[i]);
		}
		for (i32 i = 0; i < model->texture_count; i++) {
			if (model->textures[i]) {
				gl::DeleteTextures(1, &model->textures[i]);
			}
		}
		memory::set(model, 0, sizeof(Model));
	}

	i32 find_node(const Model& model, const char* name) {
		for (i32 i = 0; i < model.node_count; i++) {
			if (str::equal(model.nodes[i].name, name)) return i;
		}
		return -1;
	}

}
