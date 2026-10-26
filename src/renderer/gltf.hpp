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

	// up to 4 joints (indices into GltfSkin) with weights summing to 1
	struct SkinVertex {
		u16 joints[4];
		f32 weights[4];
	};

	struct MeshData {
		vec3*          positions;   // position stream (12 B/vertex)
		VertexAttribs* attribs;     // normal/tangent/uv stream (36 B/vertex)
		SkinVertex*    skin;        // skinned meshes only, else null (24 B/vertex)
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
		f32  normal_scale;
		u32  albedo_index;
		u32  normal_index;
		u32  orm_index;
	};

	// resolved on-disk path of one referenced image, e.g.
	// "assets/models/foo/albedo.jpg".
	struct GltfTexturePath {
		char path[512];
		bool srgb;   // colour data (base colour); everything else is linear
	};

	// physics flags from the Blender object's custom properties (glTF node extras).
	// the renderer ignores these; physics::create_bodies reads them.
	enum class PhysicsType  : u8 { None, Static, Dynamic };
	enum class PhysicsShape : u8 { Auto, Box, Sphere, Hull, Mesh };   // Auto: mesh if static, hull if dynamic

	struct ObjectPhysics {
		PhysicsType  type;
		PhysicsShape shape;
		f32          mass;
	};

	// one glTF node with a mesh = one Blender object, baked to world-space (within the model)
	struct GltfObject {
		mat4          world_transform;
		ObjectPhysics physics;
	};

	// one primitive of an object (a Blender object has one per material slot)
	struct GltfNode {
		u32 primitive_index;
		u32 material_index;
		u32 object_index;
	};

	// KHR_lights_punctual + the light object's custom properties (range, radius, shadow).
	// intensity is in Blender's render units (exporter's Standard mode ×683 undone):
	// point/spot = W / 4π, sun = W/m². range is always filled (derived when not authored).
	enum class LightType : u8 { Point, Spot, Sun };

	struct GltfLight {
		LightType type;
		vec3      position;
		vec3      direction;   // world-space shine direction (node −Z)
		vec3      color;
		f32       intensity;
		f32       range;
		f32       radius;
		f32       inner_deg;
		f32       outer_deg;
		bool      casts_shadow;
	};

	// a node tagged with an `entity` and/or `model` custom property (an Empty)
	struct GltfEntity {
		char  name[64];
		char  type[32];     // `entity` property, empty if none
		char  model[128];   // `model` property: shared model under assets/shared/, empty if none
		mat4  world_transform;
		char* extras;     // copy of the node's extras JSON
	};

	static constexpr u32 MAX_SKIN_JOINTS = 128;

	// joints are ordered so every parent comes before its children
	struct GltfSkin {
		u32   joint_count;
		i32*  parents;         // -1 for roots
		mat4* inverse_bind;
		vec3* rest_t;          // rest-pose local transform
		quat* rest_r;
		vec3* rest_s;
		char  (*names)[64];
		mat4  root;            // world transform above the root joints (the Blender Armature object)
		f32   bounds_margin;   // furthest any vertex sits from its main joint, in model space
	};

	enum class ChannelPath : u8 { Translation, Rotation, Scale };

	struct GltfChannel {
		u32         joint;
		ChannelPath path;
		bool        step;       // STEP interpolation, else LINEAR
		u32         key_count;
		f32*        times;      // seconds from the clip's start
		f32*        values;     // 3 floats per key (4 for rotation, xyzw)
	};

	// one Blender action
	struct GltfClip {
		char         name[64];
		f32          duration;
		f32          frame_time;   // key spacing. a loop wraps last key -> first over one frame, as Blender plays a range
		GltfChannel* channels;
		u32          channel_count;
	};

	struct GltfModel {
		GltfPrimitive*    primitives;
		u32               primitive_count;
		GltfMaterial*     materials;
		u32               material_count;
		GltfTexturePath*  textures;
		u32               texture_count;
		GltfObject*       objects;
		u32               object_count;
		GltfNode*         nodes;
		u32               node_count;
		GltfLight*        lights;
		u32               light_count;
		GltfEntity*       entities;
		u32               entity_count;
		char*             scene_extras;   // copy of the scene's extras JSON (may be null)
		GltfSkin*         skin;           // first skin, null if none
		GltfClip*         clips;
		u32               clip_count;
	};

	bool load_gltf_model(const char* path, GltfModel* out);
	void free_gltf_model(GltfModel* model);
	// for an owner that took GltfModel::skin / clips (nulled in the model)
	void free_gltf_skin(GltfSkin* skin);
	void free_gltf_clips(GltfClip* clips, u32 count);
	// for an owner that took GltfModel::entities (nulled in the model) to keep past free_gltf_model
	void free_gltf_entities(GltfEntity* entities, u32 count);

	// value of a top-level key in a flat extras JSON object, quotes stripped
	bool gltf_extras_value(const char* json, const char* key, char* out, usize max);

}
