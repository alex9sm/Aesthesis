#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer { struct GltfModel; }

namespace physics {

	using BodyHandle = u32;
	static constexpr BodyHandle INVALID_BODY = (BodyHandle)~0u;

	enum class ShapeType { Box, Sphere, Hull, Mesh };

	// box: half_extents. sphere: radius. hull: points. mesh: points + indices (static only)
	// center offsets box/sphere from the body origin
	struct ShapeDesc {
		ShapeType   type;
		vec3        half_extents;
		f32         radius;
		vec3        center;
		const vec3* points;
		u32         point_count;
		const u32*  indices;
		u32         index_count;
	};

	struct RayHit {
		vec3       position;
		vec3       normal;
		f32        distance;
		BodyHandle body;
	};

	// a body built from one tagged glTF object. render a dynamic one with
	// ObjectOverride{object_index, mat4_from_pos_rot(pos, rot) * mat4_scale(scale)}
	struct ObjectBody {
		u32        object_index;
		BodyHandle body;
		bool       dynamic;
		vec3       scale;
	};

	bool init();
	void shutdown();
	void step(f32 dt);   // one fixed step; caller owns the accumulator

	BodyHandle create_body(const ShapeDesc& shape, vec3 pos, quat rot, bool dynamic, f32 mass);
	void       destroy_body(BodyHandle body);
	void       get_transform(BodyHandle body, vec3* pos, quat* rot);
	void       set_transform(BodyHandle body, vec3 pos, quat rot);

	// one body per tagged object; returns the count written to out
	u32 create_bodies(const renderer::GltfModel& model, const mat4& transform, ObjectBody* out, u32 max);

	// dir must be normalized
	bool raycast(vec3 from, vec3 dir, f32 length, RayHit* out);

	// kinematic capsule (Jolt CharacterVirtual). position is the feet
	using CharacterHandle = u32;
	static constexpr CharacterHandle INVALID_CHARACTER = (CharacterHandle)~0u;

	struct CharacterState {
		vec3 position;
		bool grounded;
	};

	CharacterHandle create_character(vec3 feet, f32 radius, f32 height);
	void            destroy_character(CharacterHandle character);
	// moves by velocity * dt with collide-and-slide, stair step-up and stick-to-floor
	void            move_character(CharacterHandle character, vec3 velocity, f32 dt);
	CharacterState  character_state(CharacterHandle character);
	void            set_character_position(CharacterHandle character, vec3 feet);

}
