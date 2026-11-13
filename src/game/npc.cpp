#include "npc.hpp"
#include "anim.hpp"
#include "api.hpp"
#include "assets.hpp"
#include "gltf.hpp"
#include "level.hpp"
#include "physics.hpp"
#include "log.hpp"
#include "string.hpp"

#include <stdlib.h>

namespace npc {

	static constexpr u32 MAX_NPCS      = 8;
	static constexpr u32 MAX_WAYPOINTS = 32;      // per path
	static constexpr f32 HEIGHT        = 1.8f;
	static constexpr f32 RADIUS        = 0.3f;
	static constexpr f32 WALK_SPEED    = 1.4f;    // m/s; match the walk clip's stride or the feet slide
	static constexpr f32 ACCEL         = 4.0f;    // m/s^2, rounds corners and eases starts/stops
	static constexpr f32 GRAVITY       = 12.0f;
	static constexpr f32 TURN_RATE     = 270.0f;  // degrees per second
	static constexpr f32 ARRIVE_DIST   = 0.3f;    // metres, horizontal
	static constexpr f32 CLIP_FADE     = 0.25f;

	enum class State : u8 { Idle, Walk };

	struct Npc {
		assets::ModelRef          ref;
		const renderer::GltfSkin* skin;
		const renderer::GltfClip* idle;
		const renderer::GltfClip* walk;
		renderer::SkinnedHandle   instance;
		physics::CharacterHandle  character;
		anim::Player              player;

		vec3  waypoints[MAX_WAYPOINTS];
		f32   waits[MAX_WAYPOINTS];      // seconds idled on arrival
		u32   waypoint_count;
		u32   target;
		State state;
		f32   wait_left;

		vec3  velocity;
		vec3  position, prev_position;   // feet
		f32   yaw, prev_yaw;             // radians; 0 faces +Z, the model's forward
	};

	static Npc npcs[MAX_NPCS];
	static u32 npc_count = 0;

	static f32 extras_f32(const renderer::GltfEntity& e, const char* key, f32 fallback) {
		char v[32];
		if (e.extras && renderer::gltf_extras_value(e.extras, key, v, sizeof(v))) return (f32)atof(v);
		return fallback;
	}

	static vec3 entity_position(const renderer::GltfEntity& e) {
		const mat4& m = e.world_transform;
		return { m.col[3][0], m.col[3][1], m.col[3][2] };
	}

	// the path's waypoints, ordered by `index`
	static void gather_path(Npc* n, const renderer::GltfEntity* ents, u32 count, const char* path) {
		i32 order[MAX_WAYPOINTS];
		for (u32 i = 0; i < count; i++) {
			const renderer::GltfEntity& e = ents[i];
			char name[64];
			if (!str::equal(e.type, "waypoint")) continue;
			if (!e.extras || !renderer::gltf_extras_value(e.extras, "path", name, sizeof(name)) || !str::equal(name, path)) continue;
			if (n->waypoint_count == MAX_WAYPOINTS) {
				logger::error("npc: path '%s' has more than %u waypoints", path, MAX_WAYPOINTS);
				return;
			}
			i32 index = (i32)extras_f32(e, "index", 0.0f);
			u32 j = n->waypoint_count++;
			for (; j > 0 && order[j - 1] > index; j--) {
				order[j]        = order[j - 1];
				n->waypoints[j] = n->waypoints[j - 1];
				n->waits[j]     = n->waits[j - 1];
			}
			order[j]        = index;
			n->waypoints[j] = entity_position(e);
			n->waits[j]     = extras_f32(e, "wait", 0.0f);
		}
	}

	static void set_state(Npc* n, State s, f32 fade) {
		n->state = s;
		const renderer::GltfClip* c = s == State::Walk && n->walk ? n->walk : n->idle;
		if (c && c != n->player.clip) anim::play(&n->player, *n->skin, c, true, fade);
	}

	static void spawn(const renderer::GltfEntity& e, const renderer::GltfEntity* ents, u32 count) {
		if (npc_count >= MAX_NPCS) {
			logger::error("npc: limit reached (%u)", MAX_NPCS);
			return;
		}
		if (e.model[0] == 0) {
			logger::error("npc '%s' has no `model`", e.name);
			return;
		}
		Npc& n = npcs[npc_count];
		n = {};
		n.ref = assets::acquire_model(e.model);
		if (n.ref == assets::INVALID_MODEL_REF) return;
		n.skin = assets::skin(n.ref);
		if (!n.skin) {
			logger::error("npc '%s': model '%s' has no skin", e.name, e.model);
			assets::release_model(n.ref);
			return;
		}
		n.instance = renderer::create_skinned(assets::render_model(n.ref));
		if (n.instance == renderer::INVALID_SKINNED) {
			assets::release_model(n.ref);
			return;
		}

		u32 clip_count;
		const renderer::GltfClip* clips = assets::clips(n.ref, &clip_count);
		n.idle = anim::find_clip(clips, clip_count, "idle");
		n.walk = anim::find_clip(clips, clip_count, "walk");
		if (!n.idle) logger::warn("npc '%s': '%s' has no 'idle' clip", e.name, e.model);
		if (!n.walk) logger::warn("npc '%s': '%s' has no 'walk' clip", e.name, e.model);

		char path[64] = "";
		if (e.extras) renderer::gltf_extras_value(e.extras, "path", path, sizeof(path));
		gather_path(&n, ents, count, path);
		if (n.waypoint_count == 0) logger::warn("npc '%s': no waypoints on path '%s', it stands still", e.name, path);

		n.position  = n.prev_position = entity_position(e);
		n.character = physics::create_character(n.position, RADIUS, HEIGHT);
		// faces the Empty's -Y in Blender (glTF +Z)
		const mat4& m = e.world_transform;
		n.yaw = n.prev_yaw = math::atan2(m.col[2][0], m.col[2][2]);
		set_state(&n, n.waypoint_count > 0 ? State::Walk : State::Idle, 0.0f);
		npc_count++;
	}

	void load() {
		u32 count;
		const renderer::GltfEntity* ents = level::entities(&count);
		for (u32 i = 0; i < count; i++) {
			if (str::equal(ents[i].type, "npc")) spawn(ents[i], ents, count);
		}
	}

	void unload() {
		for (u32 i = 0; i < npc_count; i++) {
			renderer::destroy_skinned(npcs[i].instance);
			physics::destroy_character(npcs[i].character);
			assets::release_model(npcs[i].ref);
		}
		npc_count = 0;
	}

	void tick(f32 dt) {
		for (u32 i = 0; i < npc_count; i++) {
			Npc& n = npcs[i];

			if (n.state == State::Idle) {
				n.wait_left -= dt;
				if (n.wait_left <= 0.0f && n.waypoint_count > 0) set_state(&n, State::Walk, CLIP_FADE);
			} else {
				vec3 to = n.waypoints[n.target] - n.position;
				if (to.x * to.x + to.z * to.z < ARRIVE_DIST * ARRIVE_DIST) {
					f32 wait = n.waits[n.target];
					n.target = (n.target + 1) % n.waypoint_count;
					if (wait > 0.0f) {
						n.wait_left = wait;
						set_state(&n, State::Idle, CLIP_FADE);
					}
				}
			}

			vec3 wish = {};
			if (n.state == State::Walk) {
				vec3 to = n.waypoints[n.target] - n.position;
				to.y = 0.0f;
				f32 d = length(to);
				if (d > 0.001f) wish = to * (WALK_SPEED / d);
			}

			bool grounded = physics::character_state(n.character).grounded && n.velocity.y < 0.1f;
			vec3 horiz = move_towards({ n.velocity.x, 0.0f, n.velocity.z }, wish, ACCEL * dt);
			n.velocity = { horiz.x, grounded ? 0.0f : n.velocity.y, horiz.z };
			n.velocity.y -= GRAVITY * dt;
			n.velocity = physics::move_character(n.character, n.velocity, dt);
			n.prev_position = n.position;
			n.position      = physics::character_state(n.character).position;

			// face where it's heading, at a capped turn rate
			n.prev_yaw = n.yaw;
			if (length_sq(wish) > 0.0f) {
				f32 diff = wrap_angle(math::atan2(wish.x, wish.z) - n.yaw);
				f32 step = to_radians(TURN_RATE) * dt;
				n.yaw = wrap_angle(n.yaw + (diff > step ? step : diff < -step ? -step : diff));
			}
		}
	}

	void update(f32 dt) {
		for (u32 i = 0; i < npc_count; i++) anim::update(&npcs[i].player, dt);
	}

	void submit(f32 alpha) {
		for (u32 i = 0; i < npc_count; i++) {
			const Npc& n = npcs[i];
			vec3 pos = n.prev_position + (n.position - n.prev_position) * alpha;
			f32  yaw = n.prev_yaw + wrap_angle(n.yaw - n.prev_yaw) * alpha;
			mat4 world = mat4_translate(pos) * mat4_rotate(yaw, { 0.0f, 1.0f, 0.0f });
			mat4 palette[renderer::MAX_SKIN_JOINTS];
			AABB bounds = anim::palette(n.player, *n.skin, palette);
			renderer::submit_skinned(n.instance, world, palette, n.skin->joint_count, bounds);
		}
	}

}
