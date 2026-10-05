#include "player.hpp"
#include "platform.hpp"

namespace player {

	static constexpr f32 HEIGHT      = 1.8f;
	static constexpr f32 RADIUS      = 0.4f;
	static constexpr f32 EYE_HEIGHT  = 1.6f;
	static constexpr f32 WALK_SPEED  = 5.0f;
	static constexpr f32 JUMP_SPEED  = 5.4f;   // ~1.5 m apex
	static constexpr f32 GRAVITY     = 12.0f;
	static constexpr f32 GROUND_ACCEL = 40.0f;  // m/s^2, ~0.2 s to full speed
	static constexpr f32 GROUND_DECEL = 40.0f;  // m/s^2, ~0.125 s to stop
	static constexpr f32 AIR_ACCEL    = 5.0f;

	static vec3 move_towards(vec3 from, vec3 to, f32 max_delta) {
		vec3 d = to - from;
		f32 len = length(d);
		if (len <= max_delta) return to;
		return from + d * (max_delta / len);
	}

	void init(Player* p) {
		*p = {};
		p->character = physics::create_character({ 0.0f, 0.0f, 0.0f }, RADIUS, HEIGHT);
	}

	void shutdown(Player* p) {
		physics::destroy_character(p->character);
		*p = {};
	}

	void spawn(Player* p, vec3 eye) {
		vec3 feet = { eye.x, eye.y - EYE_HEIGHT, eye.z };
		physics::set_character_position(p->character, feet);
		p->position      = feet;
		p->prev_position = feet;
		p->velocity      = { 0.0f, 0.0f, 0.0f };
		p->jump_queued   = false;
	}

	void input(Player* p) {
		if (platform::key_pressed(platform::KEY_SPACE)) p->jump_queued = true;
	}

	void tick(Player* p, f32 yaw, f32 dt) {
		// rising from a jump still reads as grounded for a tick; don't snap vertical velocity
		bool grounded = physics::character_state(p->character).grounded && p->velocity.y < 0.1f;

		vec3 forward = { -math::sin(yaw), 0.0f, -math::cos(yaw) };
		vec3 right   = {  math::cos(yaw), 0.0f, -math::sin(yaw) };
		vec3 wish = { 0.0f, 0.0f, 0.0f };
		if (platform::key_down(platform::KEY_W)) wish += forward;
		if (platform::key_down(platform::KEY_S)) wish += -forward;
		if (platform::key_down(platform::KEY_D)) wish += right;
		if (platform::key_down(platform::KEY_A)) wish += -right;
		bool has_input = length_sq(wish) > 0.0f;
		if (has_input) wish = normalize(wish) * WALK_SPEED;

		vec3 horiz = { p->velocity.x, 0.0f, p->velocity.z };
		if (grounded) {
			horiz = move_towards(horiz, wish, (has_input ? GROUND_ACCEL : GROUND_DECEL) * dt);
			p->velocity = { horiz.x, p->jump_queued ? JUMP_SPEED : 0.0f, horiz.z };
		} else if (has_input) {
			// steers toward wish at most WALK_SPEED, so no speed gain from strafing
			horiz = move_towards(horiz, wish, AIR_ACCEL * dt);
			p->velocity.x = horiz.x;
			p->velocity.z = horiz.z;
		}
		p->velocity.y -= GRAVITY * dt;
		p->jump_queued = false;

		p->velocity = physics::move_character(p->character, p->velocity, dt);
		p->prev_position = p->position;
		p->position      = physics::character_state(p->character).position;
	}

	vec3 eye(const Player& p, f32 alpha) {
		vec3 feet = p.prev_position + (p.position - p.prev_position) * alpha;
		return { feet.x, feet.y + EYE_HEIGHT, feet.z };
	}

}
