#include "level.hpp"
#include "api.hpp"
#include "assets.hpp"
#include "gltf.hpp"
#include "physics.hpp"
#include "log.hpp"
#include "string.hpp"

#include <stdlib.h>

namespace level {

	static constexpr u32 MAX_LEVEL_BODIES = 1024;
	static constexpr u32 MAX_LEVEL_LIGHTS = 256;
	static constexpr u32 MAX_LEVEL_PROPS  = 512;
	static constexpr vec3 NO_SUN_DIR = { 0.38f, 1.0f, 0.41f };

	// one placement of a shared model
	struct Prop {
		assets::ModelRef ref;
		mat4             transform;
		u32              first_body;
		u32              body_count;
	};

	static renderer::ModelHandle   model = renderer::INVALID_MODEL;
	static renderer::CubemapHandle env   = renderer::INVALID_CUBEMAP;

	// the level's own bodies first, then each prop's range
	static physics::ObjectBody      bodies[MAX_LEVEL_BODIES];
	static renderer::ObjectOverride overrides[MAX_LEVEL_BODIES];
	static u32                      body_count     = 0;
	static u32                      own_body_count = 0;

	static Prop props[MAX_LEVEL_PROPS];
	static u32  prop_count = 0;

	static renderer::GltfLight lights[MAX_LEVEL_LIGHTS];
	static u32                 light_count = 0;
	static u32                 shadowed[MAX_LEVEL_LIGHTS];   // indices into lights, re-sorted nearest-first each frame
	static u32                 shadowed_count = 0;

	static renderer::GltfEntity* level_entities     = nullptr;
	static u32                   level_entity_count = 0;

	static bool has_spawn  = false;
	static vec3 spawn_feet = {};
	static f32  spawn_yaw  = 0.0f;

	static f32 scene_value(const char* extras, const char* key, f32 fallback) {
		char v[32];
		if (extras && renderer::gltf_extras_value(extras, key, v, sizeof(v))) return (f32)atof(v);
		return fallback;
	}

	static void place_props(const renderer::GltfModel& gm) {
		for (u32 i = 0; i < gm.entity_count; i++) {
			const renderer::GltfEntity& e = gm.entities[i];
			// with an `entity` type it's game code's to spawn (e.g. an enemy)
			if (e.model[0] == 0 || e.type[0] != 0) continue;
			if (prop_count >= MAX_LEVEL_PROPS) {
				logger::error("level: prop limit reached (%u)", MAX_LEVEL_PROPS);
				return;
			}
			assets::ModelRef ref = assets::acquire_model(e.model);
			if (ref == assets::INVALID_MODEL_REF) continue;

			Prop& p = props[prop_count++];
			p.ref        = ref;
			p.transform  = e.world_transform;
			p.first_body = body_count;
			p.body_count = assets::create_bodies(ref, e.world_transform, bodies + body_count, MAX_LEVEL_BODIES - body_count);
			body_count  += p.body_count;
		}
	}

	bool load(const char* path) {
		renderer::GltfModel gm = {};
		if (!renderer::load_gltf_model(path, &gm)) {
			logger::error("level: failed to load '%s'", path);
			return false;
		}

		model = renderer::load_model(gm);
		own_body_count = physics::create_bodies(gm, mat4_identity(), bodies, MAX_LEVEL_BODIES);
		body_count = own_body_count;
		place_props(gm);

		renderer::set_sun(NO_SUN_DIR, { 1.0f, 1.0f, 1.0f }, 0.0f);
		for (u32 i = 0; i < gm.light_count; i++) {
			const renderer::GltfLight& l = gm.lights[i];
			if (l.type == renderer::LightType::Sun) {
				renderer::set_sun(-l.direction, l.color, l.intensity);
			} else if (light_count < MAX_LEVEL_LIGHTS) {
				if (l.casts_shadow) shadowed[shadowed_count++] = light_count;
				lights[light_count++] = l;
			}
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

		// keep the entities for game code; free_gltf_model skips them once taken
		level_entities     = gm.entities;
		level_entity_count = gm.entity_count;
		gm.entities        = nullptr;
		gm.entity_count    = 0;

		renderer::free_gltf_model(&gm);
		return true;
	}

	void unload() {
		for (u32 i = 0; i < body_count; i++) physics::destroy_body(bodies[i].body);
		body_count     = 0;
		own_body_count = 0;
		for (u32 i = 0; i < prop_count; i++) assets::release_model(props[i].ref);
		prop_count     = 0;
		light_count    = 0;
		shadowed_count = 0;
		has_spawn      = false;

		renderer::free_gltf_entities(level_entities, level_entity_count);
		level_entities     = nullptr;
		level_entity_count = 0;

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

	// dynamic bodies in [first, first + count) draw at their simulated transform
	static void submit_with_bodies(renderer::ModelHandle handle, const mat4& transform, u32 first, u32 count) {
		u32 override_count = 0;
		for (u32 i = first; i < first + count; i++) {
			const physics::ObjectBody& ob = bodies[i];
			if (!ob.dynamic) continue;
			vec3 pos;
			quat rot;
			physics::get_transform(ob.body, &pos, &rot);
			overrides[override_count++] = { ob.object_index, mat4_from_pos_rot(pos, rot) * mat4_scale(ob.scale) };
		}
		renderer::submit_model(handle, transform, { 1.0f, 1.0f, 1.0f, 1.0f }, overrides, override_count);
	}

	static void submit_light(const renderer::GltfLight& l) {
		if (l.type == renderer::LightType::Point) {
			renderer::submit_point_light(l.position, l.color, l.range, l.intensity, l.radius);
		} else {
			renderer::submit_spot_light(l.position, l.color, l.range, l.intensity, l.radius,
				l.direction, l.inner_deg, l.outer_deg, l.casts_shadow);
		}
	}

	void submit(vec3 camera_pos) {
		submit_with_bodies(model, mat4_identity(), 0, own_body_count);
		for (u32 i = 0; i < prop_count; i++) {
			const Prop& p = props[i];
			submit_with_bodies(assets::render_model(p.ref), p.transform, p.first_body, p.body_count);
		}

		// the renderer gives its spot shadow slots to the first visible shadowed spots submitted,
		// so submit them nearest-first. insertion sort: last frame's order is nearly sorted
		for (u32 i = 1; i < shadowed_count; i++) {
			u32 idx = shadowed[i];
			f32 d = length_sq(lights[idx].position - camera_pos);
			u32 j = i;
			for (; j > 0 && length_sq(lights[shadowed[j - 1]].position - camera_pos) > d; j--) shadowed[j] = shadowed[j - 1];
			shadowed[j] = idx;
		}
		for (u32 i = 0; i < shadowed_count; i++) submit_light(lights[shadowed[i]]);
		for (u32 i = 0; i < light_count; i++) {
			if (!lights[i].casts_shadow) submit_light(lights[i]);
		}
	}

	bool player_spawn(vec3* feet, f32* yaw) {
		if (!has_spawn) return false;
		*feet = spawn_feet;
		*yaw  = spawn_yaw;
		return true;
	}

	const renderer::GltfEntity* entities(u32* count) {
		*count = level_entity_count;
		return level_entities;
	}

}
