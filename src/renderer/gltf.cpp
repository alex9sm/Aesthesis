// cgltf uses strncpy/strcpy/fopen which MSVC flags as deprecated. these are safe
// here because cgltf bounds them against fixed-size buffers internally.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "gltf.hpp"
#include "memory.hpp"
#include "string.hpp"
#include "log.hpp"

#include <stdlib.h>

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

	// --- geometry dedup ---
	//
	// Exporters routinely emit one copy of a mesh per placement (16 identical
	// pawns => 16 accessor sets holding identical bytes), which the batcher
	// cannot instance because it buckets by mesh handle. Hashing the raw
	// accessor bytes collapses those back to one primitive, so repeated
	// geometry costs one draw with N instances instead of N draws.

	static u64 fnv1a(const void* bytes, usize len, u64 h) {
		const u8* p = (const u8*)bytes;
		for (usize i = 0; i < len; i++) {
			h ^= p[i];
			h *= 0x100000001b3ull;
		}
		return h;
	}

	static u64 hash_accessor(const cgltf_accessor* acc, u64 h) {
		if (!acc) return fnv1a("none", 4, h);

		u64 desc[3] = { (u64)acc->count, (u64)acc->type, (u64)acc->component_type };
		h = fnv1a(desc, sizeof(desc), h);

		const cgltf_buffer_view* view = acc->buffer_view;
		// cgltf_buffer_view_data already applies view->offset and honours the
		// extension-decoded override, so only acc->offset remains to be added.
		const u8* base = view ? (const u8*)cgltf_buffer_view_data(view) : nullptr;
		usize span = acc->count
			? (usize)(acc->count - 1) * acc->stride + cgltf_calc_size(acc->type, acc->component_type)
			: 0;

		// sparse accessors patch their base bytes after the fact, so the raw
		// range does not identify them; unreadable ranges are likewise
		// unidentifiable. fold the address in so these dedup against nothing.
		if (acc->is_sparse || !base || acc->offset + span > view->size) {
			u64 key = (u64)(usize)acc;
			return fnv1a(&key, sizeof(key), h);
		}
		return fnv1a(base + acc->offset, span, h);
	}

	// identity of a primitive's geometry — everything primitive_to_mesh reads.
	static u64 primitive_geometry_hash(const cgltf_primitive* prim) {
		const cgltf_attribute* attrs[6] = {
			find_attribute(prim, cgltf_attribute_type_position),
			find_attribute(prim, cgltf_attribute_type_normal),
			find_attribute(prim, cgltf_attribute_type_tangent),
			find_attribute_indexed(prim, cgltf_attribute_type_texcoord, 0),
			find_attribute_indexed(prim, cgltf_attribute_type_joints, 0),
			find_attribute_indexed(prim, cgltf_attribute_type_weights, 0),
		};
		u64 h = 0xcbf29ce484222325ull;
		for (u32 i = 0; i < 6; i++) h = hash_accessor(attrs[i] ? attrs[i]->data : nullptr, h);
		return hash_accessor(prim->indices, h);
	}

	static mat4 to_mat4(const f32* m) {
		mat4 r = {};
		for (u32 c = 0; c < 4; c++)
			for (u32 row = 0; row < 4; row++)
				r.col[c][row] = m[c * 4 + row];
		return r;
	}

	// JOINTS_0 / WEIGHTS_0. joint_remap maps the file's joint order to the skin's parent-first order
	static SkinVertex* read_skin_stream(const cgltf_primitive* prim, u32 vertex_count,
		const i32* joint_remap, u32 joint_count)
	{
		const cgltf_attribute* j_attr = find_attribute_indexed(prim, cgltf_attribute_type_joints, 0);
		const cgltf_attribute* w_attr = find_attribute_indexed(prim, cgltf_attribute_type_weights, 0);
		if (!joint_remap || !j_attr || !w_attr) return nullptr;
		if (j_attr->data->count != vertex_count || w_attr->data->count != vertex_count) return nullptr;

		SkinVertex* skin = (SkinVertex*)memory::malloc(sizeof(SkinVertex) * vertex_count);
		for (u32 i = 0; i < vertex_count; i++) {
			cgltf_uint j[4] = {};
			f32 w[4] = {};
			cgltf_accessor_read_uint(j_attr->data, i, j, 4);
			cgltf_accessor_read_float(w_attr->data, i, w, 4);
			f32 sum = w[0] + w[1] + w[2] + w[3];
			for (u32 k = 0; k < 4; k++) {
				skin[i].joints[k]  = (u16)(j[k] < joint_count ? joint_remap[j[k]] : 0);
				skin[i].weights[k] = sum > 0.0f ? w[k] / sum : (k == 0 ? 1.0f : 0.0f);
			}
		}
		return skin;
	}

	// unpack one cgltf_primitive into MeshData. returns false if no positions.
	static bool primitive_to_mesh(const cgltf_primitive* prim, MeshData* out,
		const i32* joint_remap = nullptr, u32 joint_count = 0)
	{
		const cgltf_attribute* pos_attr = find_attribute(prim, cgltf_attribute_type_position);
		const cgltf_attribute* nrm_attr = find_attribute(prim, cgltf_attribute_type_normal);
		const cgltf_attribute* tan_attr = find_attribute(prim, cgltf_attribute_type_tangent);
		const cgltf_attribute* uv_attr  = find_attribute_indexed(prim, cgltf_attribute_type_texcoord, 0);

		if (!pos_attr) return false;

		u32 vertex_count = (u32)pos_attr->data->count;
		vec3* positions = (vec3*)memory::malloc(sizeof(vec3) * vertex_count);
		VertexAttribs* attribs = (VertexAttribs*)memory::malloc(sizeof(VertexAttribs) * vertex_count);
		memory::set(attribs, 0, sizeof(VertexAttribs) * vertex_count);

		for (u32 i = 0; i < vertex_count; i++) {
			f32 p[3] = {};
			cgltf_accessor_read_float(pos_attr->data, i, p, 3);
			positions[i] = { p[0], p[1], p[2] };
		}

		// AABB: prefer the POSITION accessor's min/max (exact, free). fall back
		// to scanning vertex positions when the exporter omitted them.
		if (pos_attr->data->has_min && pos_attr->data->has_max) {
			out->aabb_min = { pos_attr->data->min[0], pos_attr->data->min[1], pos_attr->data->min[2] };
			out->aabb_max = { pos_attr->data->max[0], pos_attr->data->max[1], pos_attr->data->max[2] };
		} else if (vertex_count > 0) {
			vec3 lo = positions[0];
			vec3 hi = lo;
			for (u32 i = 1; i < vertex_count; i++) {
				vec3 p = positions[i];
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
				attribs[i].normal = { n[0], n[1], n[2] };
			}
		} else {
			for (u32 i = 0; i < vertex_count; i++) {
				attribs[i].normal = { 0.0f, 1.0f, 0.0f };
			}
		}

		// TANGENT (vec4: xyz tangent, w bitangent sign). Missing tangents fall
		// back to {1,0,0,1} — normal mapping will be wrong, but the engine
		// still renders without crashing.
		if (tan_attr && tan_attr->data->count == vertex_count) {
			for (u32 i = 0; i < vertex_count; i++) {
				f32 t[4] = {};
				cgltf_accessor_read_float(tan_attr->data, i, t, 4);
				attribs[i].tangent = { t[0], t[1], t[2], t[3] };
			}
		} else {
			if (!tan_attr) {
				logger::error("glTF primitive missing TANGENT attribute — normal mapping will be incorrect");
			}
			for (u32 i = 0; i < vertex_count; i++) {
				attribs[i].tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
			}
		}

		// TEXCOORD_0
		if (uv_attr && uv_attr->data->count == vertex_count) {
			for (u32 i = 0; i < vertex_count; i++) {
				f32 uv[2] = {};
				cgltf_accessor_read_float(uv_attr->data, i, uv, 2);
				attribs[i].uv = { uv[0], uv[1] };
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

		out->positions = positions;
		out->attribs = attribs;
		out->skin = read_skin_stream(prim, vertex_count, joint_remap, joint_count);
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
		if (mesh->positions) memory::free(mesh->positions);
		if (mesh->attribs)   memory::free(mesh->attribs);
		if (mesh->skin)      memory::free(mesh->skin);
		if (mesh->indices)   memory::free(mesh->indices);
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

	static char* copy_string(const char* s) {
		usize n = str::length(s) + 1;
		char* out = (char*)memory::malloc(n);
		memory::copy(out, s, n);
		return out;
	}

	bool gltf_extras_value(const char* json, const char* key, char* out, usize max) {
		char pattern[64];
		str::format(pattern, sizeof(pattern), "\"%s\"", key);
		const char* p = str::find(json, pattern);
		if (!p) return false;
		p = str::find_char(p + str::length(pattern), ':');
		if (!p) return false;
		p++;
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

		bool quoted = *p == '"';
		if (quoted) p++;
		usize n = 0;
		while (*p && n + 1 < max) {
			if (quoted ? *p == '"' : (*p == ',' || *p == '}' || *p == ' ' || *p == '\r' || *p == '\n')) break;
			out[n++] = *p++;
		}
		out[n] = 0;
		return n > 0;
	}

	static ObjectPhysics parse_physics(const cgltf_node* node) {
		ObjectPhysics ph = { PhysicsType::None, PhysicsShape::Auto, 10.0f };
		const char* json = node->extras.data;
		if (!json) return ph;

		const char* name = node->name ? node->name : "<unnamed>";
		char v[32];
		if (gltf_extras_value(json, "physics", v, sizeof(v))) {
			if      (str::equal(v, "static"))  ph.type = PhysicsType::Static;
			else if (str::equal(v, "dynamic")) ph.type = PhysicsType::Dynamic;
			else logger::warn("glTF object '%s': unknown physics '%s'", name, v);
		}
		if (gltf_extras_value(json, "shape", v, sizeof(v))) {
			if      (str::equal(v, "box"))    ph.shape = PhysicsShape::Box;
			else if (str::equal(v, "sphere")) ph.shape = PhysicsShape::Sphere;
			else if (str::equal(v, "hull"))   ph.shape = PhysicsShape::Hull;
			else if (str::equal(v, "mesh"))   ph.shape = PhysicsShape::Mesh;
			else logger::warn("glTF object '%s': unknown shape '%s'", name, v);
		}
		if (gltf_extras_value(json, "mass", v, sizeof(v))) ph.mass = (f32)atof(v);

		if (ph.type == PhysicsType::Dynamic && node->children_count > 0) {
			logger::warn("glTF object '%s': children of a dynamic object won't follow it", name);
		}
		return ph;
	}

	// the exporter's Standard lighting mode multiplies Blender's W and W/m² by this
	static constexpr f32 WATTS_TO_LUMENS = 683.0f;
	// irradiance (Blender units) at which an unauthored range cuts off
	static constexpr f32 LIGHT_CUTOFF = 0.25f;

	static GltfLight parse_light(const cgltf_node* node, const mat4& world) {
		const cgltf_light* cl = node->light;
		GltfLight l = {};
		l.type = cl->type == cgltf_light_type_directional ? LightType::Sun
			: cl->type == cgltf_light_type_spot ? LightType::Spot : LightType::Point;
		l.position  = { world.col[3][0], world.col[3][1], world.col[3][2] };
		l.direction = normalize(vec3{ -world.col[2][0], -world.col[2][1], -world.col[2][2] });
		l.color     = { cl->color[0], cl->color[1], cl->color[2] };
		l.intensity = cl->intensity / WATTS_TO_LUMENS;
		l.range     = cl->range;
		l.radius    = 0.05f;
		l.inner_deg = to_degrees(cl->spot_inner_cone_angle);
		l.outer_deg = to_degrees(cl->spot_outer_cone_angle);

		const char* json = node->extras.data;
		char v[32];
		if (json) {
			if (gltf_extras_value(json, "range", v, sizeof(v)))  l.range  = (f32)atof(v);
			if (gltf_extras_value(json, "radius", v, sizeof(v))) l.radius = (f32)atof(v);
			if (gltf_extras_value(json, "shadow", v, sizeof(v))) l.casts_shadow = str::equal(v, "true") || str::equal(v, "1");
		}
		if (l.casts_shadow && l.type == LightType::Point) {
			logger::warn("glTF light '%s': point lights don't cast shadows", node->name ? node->name : "<unnamed>");
			l.casts_shadow = false;
		}
		if (l.range <= 0.0f) l.range = math::sqrt(l.intensity / LIGHT_CUTOFF);
		return l;
	}

	// --- skin + clips ---

	static mat4 trs_matrix(vec3 t, quat r, vec3 s) {
		return mat4_translate(t) * quat_to_mat4(r) * mat4_scale(s);
	}

	// joints are reordered parent-first; new_index[file joint] gives the skin's index
	static GltfSkin* load_skin(const cgltf_skin* cs, i32* new_index) {
		u32 n = (u32)cs->joints_count;
		i32* file_parent = (i32*)memory::malloc(sizeof(i32) * n);
		u32* file_of_new = (u32*)memory::malloc(sizeof(u32) * n);
		for (u32 i = 0; i < n; i++) {
			file_parent[i] = -1;
			new_index[i] = -1;
			for (u32 j = 0; j < n; j++) {
				if (cs->joints[i]->parent == cs->joints[j]) file_parent[i] = (i32)j;
			}
		}
		u32 placed = 0;
		while (placed < n) {
			for (u32 i = 0; i < n; i++) {
				if (new_index[i] >= 0) continue;
				if (file_parent[i] >= 0 && new_index[file_parent[i]] < 0) continue;
				new_index[i] = (i32)placed;
				file_of_new[placed++] = i;
			}
		}

		GltfSkin* s = (GltfSkin*)memory::malloc(sizeof(GltfSkin));
		*s = {};
		s->joint_count  = n;
		s->parents      = (i32*)memory::malloc(sizeof(i32) * n);
		s->inverse_bind = (mat4*)memory::malloc(sizeof(mat4) * n);
		s->rest_t       = (vec3*)memory::malloc(sizeof(vec3) * n);
		s->rest_r       = (quat*)memory::malloc(sizeof(quat) * n);
		s->rest_s       = (vec3*)memory::malloc(sizeof(vec3) * n);
		s->names        = (char(*)[64])memory::malloc(64 * n);

		for (u32 k = 0; k < n; k++) {
			u32 i = file_of_new[k];
			const cgltf_node* jn = cs->joints[i];
			s->parents[k] = file_parent[i] < 0 ? -1 : new_index[file_parent[i]];

			s->inverse_bind[k] = mat4_identity();
			if (cs->inverse_bind_matrices) {
				f32 m[16] = {};
				cgltf_accessor_read_float(cs->inverse_bind_matrices, i, m, 16);
				s->inverse_bind[k] = to_mat4(m);
			}

			if (jn->has_matrix) {
				mat4_decompose(to_mat4(jn->matrix), &s->rest_t[k], &s->rest_r[k], &s->rest_s[k]);
			} else {
				s->rest_t[k] = jn->has_translation ? vec3{ jn->translation[0], jn->translation[1], jn->translation[2] } : vec3{ 0, 0, 0 };
				s->rest_r[k] = jn->has_rotation ? quat{ jn->rotation[0], jn->rotation[1], jn->rotation[2], jn->rotation[3] } : quat_identity();
				s->rest_s[k] = jn->has_scale ? vec3{ jn->scale[0], jn->scale[1], jn->scale[2] } : vec3{ 1, 1, 1 };
			}
			str::copy(s->names[k], jn->name ? jn->name : "", 64);
		}

		s->root = mat4_identity();
		const cgltf_node* above = cs->joints[file_of_new[0]]->parent;
		if (above) {
			f32 m[16] = {};
			cgltf_node_transform_world(above, m);
			s->root = to_mat4(m);
		}

		memory::free(file_parent);
		memory::free(file_of_new);
		return s;
	}

	// posed bounds are the joints' box grown by this margin
	static f32 skin_bounds_margin(const GltfSkin& s, const GltfModel& model) {
		mat4* bind = (mat4*)memory::malloc(sizeof(mat4) * s.joint_count);
		vec3* joint_pos = (vec3*)memory::malloc(sizeof(vec3) * s.joint_count);
		for (u32 k = 0; k < s.joint_count; k++) {
			mat4 local = trs_matrix(s.rest_t[k], s.rest_r[k], s.rest_s[k]);
			mat4 global = (s.parents[k] < 0 ? s.root : bind[s.parents[k]]) * local;
			bind[k] = global;
			joint_pos[k] = { global.col[3][0], global.col[3][1], global.col[3][2] };
		}
		for (u32 k = 0; k < s.joint_count; k++) bind[k] = bind[k] * s.inverse_bind[k];

		f32 margin = 0.0f;
		for (u32 p = 0; p < model.primitive_count; p++) {
			const MeshData& m = model.primitives[p].mesh;
			if (!m.skin) continue;
			for (u32 v = 0; v < m.vertex_count; v++) {
				const SkinVertex& sv = m.skin[v];
				u32 main = 0;
				for (u32 i = 1; i < 4; i++) if (sv.weights[i] > sv.weights[main]) main = i;
				u32 j = sv.joints[main];
				f32 d = length(mat4_transform_point(bind[j], m.positions[v]) - joint_pos[j]);
				if (d > margin) margin = d;
			}
		}
		memory::free(bind);
		memory::free(joint_pos);
		return margin;
	}

	// one clip per glTF animation (one per Blender action); channels on non-joint nodes are ignored
	static GltfClip* load_clips(const cgltf_data* data, const cgltf_skin* cs, const i32* new_index) {
		GltfClip* clips = (GltfClip*)memory::malloc(sizeof(GltfClip) * data->animations_count);
		static bool warned_cubic = false;

		for (cgltf_size a = 0; a < data->animations_count; a++) {
			const cgltf_animation* ca = &data->animations[a];
			GltfClip& clip = clips[a];
			clip = {};
			str::copy(clip.name, ca->name ? ca->name : "", sizeof(clip.name));
			clip.channels = (GltfChannel*)memory::malloc(sizeof(GltfChannel) * (ca->channels_count + 1));

			f32 t0 = 1e30f, t1 = -1e30f;
			for (cgltf_size c = 0; c < ca->channels_count; c++) {
				const cgltf_animation_channel* ch = &ca->channels[c];
				if (ch->target_path != cgltf_animation_path_type_translation
					&& ch->target_path != cgltf_animation_path_type_rotation
					&& ch->target_path != cgltf_animation_path_type_scale) continue;
				i32 joint = -1;
				for (cgltf_size j = 0; j < cs->joints_count; j++) {
					if (cs->joints[j] == ch->target_node) { joint = new_index[j]; break; }
				}
				if (joint < 0) continue;

				const cgltf_animation_sampler* smp = ch->sampler;
				bool cubic = smp->interpolation == cgltf_interpolation_type_cubic_spline;
				if (cubic && !warned_cubic) {
					logger::warn("glTF clip '%s': CUBICSPLINE played as linear", clip.name);
					warned_cubic = true;
				}

				GltfChannel& gc = clip.channels[clip.channel_count++];
				gc.joint = (u32)joint;
				gc.path = ch->target_path == cgltf_animation_path_type_translation ? ChannelPath::Translation
					: ch->target_path == cgltf_animation_path_type_rotation ? ChannelPath::Rotation : ChannelPath::Scale;
				gc.step = smp->interpolation == cgltf_interpolation_type_step;
				gc.key_count = (u32)smp->input->count;
				u32 comps = gc.path == ChannelPath::Rotation ? 4 : 3;
				gc.times  = (f32*)memory::malloc(sizeof(f32) * gc.key_count);
				gc.values = (f32*)memory::malloc(sizeof(f32) * comps * gc.key_count);
				for (u32 k = 0; k < gc.key_count; k++) {
					cgltf_accessor_read_float(smp->input, k, &gc.times[k], 1);
					// a cubic key stores in-tangent, value, out-tangent
					cgltf_accessor_read_float(smp->output, cubic ? k * 3 + 1 : k, &gc.values[k * comps], comps);
				}
				if (gc.key_count > 0) {
					if (gc.times[0] < t0) t0 = gc.times[0];
					if (gc.times[gc.key_count - 1] > t1) t1 = gc.times[gc.key_count - 1];
				}
				for (u32 k = 1; k < gc.key_count; k++) {
					f32 step = gc.times[k] - gc.times[k - 1];
					if (step > 0.0f && (clip.frame_time == 0.0f || step < clip.frame_time)) clip.frame_time = step;
				}
			}

			// the exporter writes absolute times (frame / fps); clips start at their first key
			if (t1 < t0) t0 = t1 = 0.0f;
			for (u32 c = 0; c < clip.channel_count; c++) {
				for (u32 k = 0; k < clip.channels[c].key_count; k++) clip.channels[c].times[k] -= t0;
			}
			clip.duration = t1 - t0;
		}
		return clips;
	}

	void free_gltf_skin(GltfSkin* skin) {
		if (!skin) return;
		memory::free(skin->parents);
		memory::free(skin->inverse_bind);
		memory::free(skin->rest_t);
		memory::free(skin->rest_r);
		memory::free(skin->rest_s);
		memory::free(skin->names);
		memory::free(skin);
	}

	void free_gltf_clips(GltfClip* clips, u32 count) {
		if (!clips) return;
		for (u32 i = 0; i < count; i++) {
			for (u32 c = 0; c < clips[i].channel_count; c++) {
				memory::free(clips[i].channels[c].times);
				memory::free(clips[i].channels[c].values);
			}
			memory::free(clips[i].channels);
		}
		memory::free(clips);
	}

	// recursive walk of a cgltf node tree, appending one GltfNode per primitive,
	// per node-with-mesh, plus the node's light and entity.
	// `prim_offset_per_mesh[i]` gives cgltf mesh i's first slot in the flat
	// (mesh, primitive) slot space; `prim_remap` maps a slot to the deduped
	// index in model->primitives.
	static void walk_node(GltfModel* model, u32* node_capacity,
		const cgltf_data* data, const cgltf_node* node,
		const mat4& parent_world,
		const u32* prim_offset_per_mesh, const u32* prim_remap, u32 slot_count,
		const u32* mat_index_for_cgltf_mat,
		const cgltf_material** mat_keys, u32 mat_count)
	{
		// cgltf produces a column-major matrix
		f32 local[16] = {};
		cgltf_node_transform_local(node, local);
		mat4 world = parent_world * to_mat4(local);

		char type[32] = "";
		char model_name[128] = "";
		const char* json = node->extras.data;
		bool has_type  = json && gltf_extras_value(json, "entity", type, sizeof(type));
		bool has_model = json && gltf_extras_value(json, "model", model_name, sizeof(model_name));
		if (has_type || has_model) {
			GltfEntity& e = model->entities[model->entity_count++];
			str::copy(e.name, node->name ? node->name : "", sizeof(e.name));
			str::copy(e.type, type, sizeof(e.type));
			str::copy(e.model, model_name, sizeof(e.model));
			e.world_transform = world;
			e.extras = copy_string(json);
		}
		// the subtree under a shared model reference is only its Blender preview
		if (has_model) return;

		if (node->mesh) {
			u32 object_index = model->object_count++;
			// glTF ignores a skinned mesh's own transform: its joints place it
			model->objects[object_index] = { node->skin ? mat4_identity() : world, parse_physics(node) };

			cgltf_size mesh_index = node->mesh - data->meshes;
			u32 prim_base = prim_offset_per_mesh[mesh_index];
			for (cgltf_size p = 0; p < node->mesh->primitives_count; p++) {
				u32 slot = prim_base + (u32)p;
				if (slot >= slot_count) continue;
				u32 prim_idx = prim_remap[slot];
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
				gn.object_index    = object_index;
			}
		}

		if (node->light) model->lights[model->light_count++] = parse_light(node, world);

		for (cgltf_size i = 0; i < node->children_count; i++) {
			walk_node(model, node_capacity, data, node->children[i], world,
				prim_offset_per_mesh, prim_remap, slot_count,
				mat_index_for_cgltf_mat, mat_keys, mat_count);
		}
	}

	static bool has_model_ref(const cgltf_node* node) {
		char v[128];
		return node->extras.data && gltf_extras_value(node->extras.data, "model", v, sizeof(v));
	}

	// meshes reachable without entering a shared model's preview subtree
	static void mark_used_meshes(const cgltf_data* data, const cgltf_node* node, bool* mesh_used) {
		if (has_model_ref(node)) return;
		if (node->mesh) mesh_used[node->mesh - data->meshes] = true;
		for (cgltf_size i = 0; i < node->children_count; i++) mark_used_meshes(data, node->children[i], mesh_used);
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

		const cgltf_scene* scene = data->scene ? data->scene
			: (data->scenes_count > 0 ? &data->scenes[0] : nullptr);

		// only meshes and materials the walked nodes use get converted, so a shared
		// model's preview copy costs nothing
		bool* mesh_used     = (bool*)memory::malloc(data->meshes_count + 1);
		bool* material_used = (bool*)memory::malloc(data->materials_count + 1);
		memory::set(mesh_used, 0, data->meshes_count + 1);
		memory::set(material_used, 0, data->materials_count + 1);
		if (scene) {
			for (cgltf_size i = 0; i < scene->nodes_count; i++) mark_used_meshes(data, scene->nodes[i], mesh_used);
		} else {
			for (cgltf_size i = 0; i < data->nodes_count; i++) {
				if (!data->nodes[i].parent) mark_used_meshes(data, &data->nodes[i], mesh_used);
			}
		}

		// one skin per model: every skinned primitive is posed by the first skin
		i32* joint_remap = nullptr;
		u32  joint_count = 0;
		if (data->skins_count > 0) {
			const cgltf_skin* cs = &data->skins[0];
			if (data->skins_count > 1) logger::warn("glTF '%s': %u skins, only the first is used", path, (u32)data->skins_count);
			if (cs->joints_count > MAX_SKIN_JOINTS) {
				logger::error("glTF '%s': %u joints (max %u), skin ignored", path, (u32)cs->joints_count, MAX_SKIN_JOINTS);
			} else if (cs->joints_count > 0) {
				joint_count = (u32)cs->joints_count;
				joint_remap = (i32*)memory::malloc(sizeof(i32) * joint_count);
				out->skin = load_skin(cs, joint_remap);
				if (data->animations_count > 0) {
					out->clips = load_clips(data, cs, joint_remap);
					out->clip_count = (u32)data->animations_count;
				}
			}
		}

		// offset table: prim_offset_per_mesh[meshIndex] = first flat slot index.
		// prim_remap maps each flat slot to its deduped primitive; slots sharing
		// byte-identical geometry converge on one entry in out->primitives.
		u32* prim_offset_per_mesh = (u32*)memory::malloc(sizeof(u32) * data->meshes_count);
		u32* prim_remap  = (u32*)memory::malloc(sizeof(u32) * total_prims);
		u64* prim_hashes = (u64*)memory::malloc(sizeof(u64) * total_prims);

		u32 slot_count = 0;
		for (cgltf_size m = 0; m < data->meshes_count; m++) {
			prim_offset_per_mesh[m] = slot_count;
			for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++) {
				const cgltf_primitive* cp = &data->meshes[m].primitives[p];
				if (!mesh_used[m]) {
					prim_remap[slot_count++] = (u32)~0u;
					continue;
				}
				if (cp->material) material_used[cp->material - data->materials] = true;

				// 64-bit content hash; distinct geometry colliding is ~1e-17 here,
				// and vertex/index counts are folded in, so no byte-compare pass.
				u64 h = primitive_geometry_hash(cp);
				u32 unique = (u32)~0u;
				for (u32 u = 0; u < out->primitive_count; u++) {
					if (prim_hashes[u] == h) { unique = u; break; }
				}

				if (unique == (u32)~0u) {
					GltfPrimitive& gp = out->primitives[out->primitive_count];
					if (!primitive_to_mesh(cp, &gp.mesh, joint_remap, joint_count)) {
						logger::error("glTF primitive missing POSITION: %s (mesh %u prim %u)",
							path, (u32)m, (u32)p);
						gp.mesh = {};
					}
					gp.material_index = (u32)~0u;  // resolved below
					prim_hashes[out->primitive_count] = h;
					unique = out->primitive_count++;
				}
				prim_remap[slot_count++] = unique;
			}
		}

		// --- materials + textures ---

		// build texture key array (cgltf_image* -> GltfTexturePath index).
		// dedupes by image pointer.
		u32 tex_capacity = 16;
		out->textures = (GltfTexturePath*)memory::malloc(sizeof(GltfTexturePath) * tex_capacity);
		out->texture_count = 0;
		const cgltf_image** image_keys = (const cgltf_image**)memory::malloc(sizeof(const cgltf_image*) * tex_capacity);

		auto add_texture = [&](const cgltf_image* image, bool srgb) -> u32 {
			if (!image || !image->uri) return (u32)~0u;
			for (u32 i = 0; i < out->texture_count; i++) {
				if (image_keys[i] == image) {
					out->textures[i].srgb |= srgb;
					return i;
				}
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
			out->textures[idx].srgb = srgb;
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
				if (!material_used[m]) continue;
				const cgltf_material* cm = &data->materials[m];
				GltfMaterial& gm = out->materials[out->material_count];
				gm.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
				gm.metallic_factor   = 0.0f;   // overwritten by the PBR block; 0 avoids chrome for non-PBR materials
				gm.roughness_factor  = 1.0f;
				gm.normal_scale      = 1.0f;
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
						gm.albedo_index = add_texture(pbr.base_color_texture.texture->image, true);
					}
					// metallic_roughness is treated as the ORM texture by convention.
					if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image) {
						gm.orm_index = add_texture(pbr.metallic_roughness_texture.texture->image, false);
					}
				}

				if (cm->normal_texture.texture && cm->normal_texture.texture->image) {
					gm.normal_index = add_texture(cm->normal_texture.texture->image, false);
					gm.normal_scale = cm->normal_texture.scale;
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

		// resolve primitive -> material_index by walking meshes again. deduped
		u32 prim_cursor = 0;
		for (cgltf_size m = 0; m < data->meshes_count; m++) {
			for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++) {
				const cgltf_material* pm = data->meshes[m].primitives[p].material;
				u32 prim_idx = prim_remap[prim_cursor++];
				if (prim_idx == (u32)~0u) continue;
				GltfPrimitive& gp = out->primitives[prim_idx];
				if (pm && gp.material_index == (u32)~0u) {
					for (u32 mi = 0; mi < out->material_count; mi++) {
						if (mat_keys[mi] == pm) {
							gp.material_index = mi;
							break;
						}
					}
				}
			}
		}

		// --- node tree (scene 0, or all top-level nodes if no scene) ---

		u32 node_capacity = 16;
		out->nodes = (GltfNode*)memory::malloc(sizeof(GltfNode) * node_capacity);
		out->node_count = 0;
		// every object is a node, so nodes_count bounds it
		out->objects = (GltfObject*)memory::malloc(sizeof(GltfObject) * (data->nodes_count > 0 ? data->nodes_count : 1));
		out->object_count = 0;
		out->lights = (GltfLight*)memory::malloc(sizeof(GltfLight) * (data->nodes_count > 0 ? data->nodes_count : 1));
		out->light_count = 0;
		out->entities = (GltfEntity*)memory::malloc(sizeof(GltfEntity) * (data->nodes_count > 0 ? data->nodes_count : 1));
		out->entity_count = 0;

		if (scene && scene->extras.data) out->scene_extras = copy_string(scene->extras.data);

		mat4 ident = {};
		ident.col[0][0] = 1.0f; ident.col[1][1] = 1.0f;
		ident.col[2][2] = 1.0f; ident.col[3][3] = 1.0f;

		if (scene) {
			for (cgltf_size i = 0; i < scene->nodes_count; i++) {
				walk_node(out, &node_capacity, data, scene->nodes[i], ident,
					prim_offset_per_mesh, prim_remap, slot_count,
					mat_index_for_cgltf_mat, mat_keys, out->material_count);
			}
		} else {
			// no scene declared: walk all nodes that have no parent.
			for (cgltf_size i = 0; i < data->nodes_count; i++) {
				if (data->nodes[i].parent) continue;
				walk_node(out, &node_capacity, data, &data->nodes[i], ident,
					prim_offset_per_mesh, prim_remap, slot_count,
					mat_index_for_cgltf_mat, mat_keys, out->material_count);
			}
		}

		if (out->skin) out->skin->bounds_margin = skin_bounds_margin(*out->skin, *out);

		// --- cleanup temporaries ---
		if (joint_remap) memory::free(joint_remap);
		memory::free(prim_offset_per_mesh);
		memory::free(prim_remap);
		memory::free(prim_hashes);
		memory::free(mesh_used);
		memory::free(material_used);
		memory::free(image_keys);
		if (mat_keys) memory::free((void*)mat_keys);
		if (mat_index_for_cgltf_mat) memory::free(mat_index_for_cgltf_mat);

		logger::info("Loaded glTF model '%s' (%u/%u prims after dedup, %u materials, %u textures, %u nodes, %u lights, %u entities)",
			path, out->primitive_count, total_prims,
			out->material_count, out->texture_count, out->node_count, out->light_count, out->entity_count);
		if (out->skin) logger::info("  skin: %u joints, %u clips", out->skin->joint_count, out->clip_count);

		cgltf_free(data);
		return true;
	}

	void free_gltf_entities(GltfEntity* entities, u32 count) {
		if (!entities) return;
		for (u32 i = 0; i < count; i++) memory::free(entities[i].extras);
		memory::free(entities);
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
		if (model->objects)   memory::free(model->objects);
		if (model->nodes)     memory::free(model->nodes);
		if (model->lights)    memory::free(model->lights);
		free_gltf_entities(model->entities, model->entity_count);
		if (model->scene_extras) memory::free(model->scene_extras);
		free_gltf_skin(model->skin);
		free_gltf_clips(model->clips, model->clip_count);
		*model = {};
	}

}
