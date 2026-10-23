#include "level.hpp"
#include "api.hpp"
#include "gltf.hpp"
#include "physics.hpp"
#include "log.hpp"
#include "string.hpp"

#include <stdlib.h>

namespace level {

	static constexpr u32 MAX_LEVEL_BODIES = 1024;
	static constexpr u32 MAX_LEVEL_LIGHTS = 256;
	static constexpr vec3 NO_SUN_DIR = { 0.38f, 1.0f, 0.41f };

	static renderer::ModelHandle   model = renderer::INVALID_MODEL;
	static renderer::CubemapHandle env   = renderer::INVALID_CUBEMAP;

	static physics::ObjectBody      bodies[MAX_LEVEL_BODIES];
	static renderer::ObjectOverride overrides[MAX_LEVEL_BODIES];
	static u32                      body_count = 0;

	static renderer::GltfLight lights[MAX_LEVEL_LIGHTS];
	static u32                 light_count = 0;

	static bool has_spawn  = false;
	static vec3 spawn_feet = {};
	static f32  spawn_yaw  = 0.0f;

	static f32 scene_value(const char* extras, const char* key, f32 fallback) {
		char v[32];
		if (extras && renderer::gltf_extras_value(extras, key, v, sizeof(v))) return (f32)atof(v);
		return fallback;
	}

	bool load(const char* path) {
		renderer::GltfModel gm = {};
		if (!renderer::load_gltf_model(path, &gm)) {
			logger::error("level: failed to load '%s'", path);
			return false;
		}

		model = renderer::load_model(gm);
		body_count = physics::create_bodies(gm, mat4_identity(), bodies, MAX_LEVEL_BODIES);

		renderer::set_sun(NO_SUN_DIR, { 1.0f, 1.0f, 1.0f }, 0.0f);
		for (u32 i = 0; i < gm.light_count; i++) {
			const renderer::GltfLight& l = gm.lights[i];
			if (l.type == renderer::LightType::Sun) renderer::set_sun(-l.direction, l.color, l.intensity);
			else if (light_count < MAX_LEVEL_LIGHTS) lights[light_count++] = l;
		}

		char env_name[64];
		if (gm.scene_extras && renderer::gltf_extras_value(gm.scene_extras, "environment", env_name, sizeof(env_name))) {
			env = renderer::load_cubemap(env_name, scene_value(gm.scene_extras, "environment_intensity", 1.0f));
			if (env != renderer::INVALID_CUBEMAP) renderer::set_environment_cubemap(env);
		}
		renderer::set_exposure(scene_value(gm.scene_extras, "exposure", 1.0f));

		for (u32 i = 0; i < gm.entity_count; i++) {
			const renderer::GltfEntity& e = gm.entities[i];
			if (!str::equal(e.type, "player_spawn")) continue;
			const mat4& m = e.world_transform;
			spawn_feet = { m.col[3][0], m.col[3][1], m.col[3][2] };
			// the player looks down the node's -Z (Blender +Y)
			spawn_yaw  = math::atan2(m.col[2][0], m.col[2][2]);
			has_spawn  = true;
			break;
		}

		renderer::free_gltf_model(&gm);
		return true;
	}

	void unload() {
		for (u32 i = 0; i < body_count; i++) physics::destroy_body(bodies[i].body);
		body_count  = 0;
		light_count = 0;
		has_spawn   = false;

		if (env != renderer::INVALID_CUBEMAP) {
			renderer::clear_environment_cubemap();
			renderer::unload_cubemap(env);
			env = renderer::INVALID_CUBEMAP;
		}
		renderer::unload_model(model);
		model = renderer::INVALID_MODEL;

		renderer::set_sun(NO_SUN_DIR, { 1.0f, 1.0f, 1.0f }, 0.0f);
		renderer::set_exposure(1.0f);
	}

	void submit() {
		u32 override_count = 0;
		for (u32 i = 0; i < body_count; i++) {
			const physics::ObjectBody& ob = bodies[i];
			if (!ob.dynamic) continue;
			vec3 pos;
			quat rot;
			physics::get_transform(ob.body, &pos, &rot);
			overrides[override_count++] = { ob.object_index, mat4_from_pos_rot(pos, rot) * mat4_scale(ob.scale) };
		}
		renderer::submit_model(model, mat4_identity(), { 1.0f, 1.0f, 1.0f, 1.0f }, overrides, override_count);

		for (u32 i = 0; i < light_count; i++) {
			const renderer::GltfLight& l = lights[i];
			if (l.type == renderer::LightType::Point) {
				renderer::submit_point_light(l.position, l.color, l.range, l.intensity, l.radius);
			} else {
				renderer::submit_spot_light(l.position, l.color, l.range, l.intensity, l.radius,
					l.direction, l.inner_deg, l.outer_deg, l.casts_shadow);
			}
		}
	}

	bool player_spawn(vec3* feet, f32* yaw) {
		if (!has_spawn) return false;
		*feet = spawn_feet;
		*yaw  = spawn_yaw;
		return true;
	}

}
