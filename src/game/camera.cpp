#include "camera.hpp"
#include "platform.hpp"

namespace camera {

	static constexpr f32 PITCH_LIMIT = 1.55334f; // ~89° in radians

	void init(Camera* c) {
		c->position   = { 0.0f, 1.0f, 3.0f };
		c->yaw        = 0.0f;
		c->pitch      = 0.0f;
		c->fov_y      = to_radians(60.0f);
		c->near_z     = 0.1f;
		c->far_z      = 100.0f;
		c->move_speed = 5.0f;
		c->boost_mult = 4.0f;
		c->mouse_sens = 0.0025f;
		c->captured   = false;
		c->kick       = { 0.0f, 0.0f };
		c->kick_vel   = { 0.0f, 0.0f };
		c->sway_phase = 0.0f;
		c->sway_weight = 0.0f;
	}

	// critically damped: a kick peaks after 1/KICK_OMEGA seconds and is ~gone after 5/KICK_OMEGA
	static constexpr f32 KICK_OMEGA = 14.0f;

	// walk sway: side drift once per stride, a lift on each step, roll with the drift
	static constexpr f32 SWAY_SIDE       = 0.012f;   // m
	static constexpr f32 SWAY_UP         = 0.008f;   // m
	static constexpr f32 SWAY_ROLL       = 0.3f;     // degrees
	static constexpr f32 SWAY_STEP_RATE  = 1.6f;     // steps per second at full speed, matches the walk clip
	static constexpr f32 SWAY_FULL_SPEED = 5.0f;     // m/s, the player's walk speed
	static constexpr f32 SWAY_EASE       = 0.15f;    // s to fade in / out

	// includes the recoil kick, so the view and anything aimed along it agree
	vec3 forward(const Camera& c) {
		f32 pitch = c.pitch + c.kick.x;
		if (pitch >  PITCH_LIMIT) pitch =  PITCH_LIMIT;
		if (pitch < -PITCH_LIMIT) pitch = -PITCH_LIMIT;
		f32 yaw = c.yaw + c.kick.y;
		f32 cp = math::cos(pitch);
		return {
			-math::sin(yaw) * cp,
			 math::sin(pitch),
			-math::cos(yaw) * cp
		};
	}

	void add_kick(Camera* c, f32 pitch, f32 yaw) {
		c->kick_vel += vec2{ pitch, yaw } * (KICK_OMEGA * EULER);
	}

	void update_kick(Camera* c, f32 dt) {
		spring_step(&c->kick.x, &c->kick_vel.x, KICK_OMEGA, dt);
		spring_step(&c->kick.y, &c->kick_vel.y, KICK_OMEGA, dt);
	}

	void update_sway(Camera* c, f32 speed, bool grounded, f32 dt) {
		f32 pace = speed / SWAY_FULL_SPEED;
		if (pace > 1.0f) pace = 1.0f;
		f32 target = grounded ? pace : 0.0f;
		f32 ease = dt / SWAY_EASE;
		c->sway_weight += (target - c->sway_weight) * (ease < 1.0f ? ease : 1.0f);
		// one phase cycle is two steps
		c->sway_phase += dt * PI * SWAY_STEP_RATE * pace;
		if (c->sway_phase >= TAU) c->sway_phase -= TAU;
	}

	vec3 right(const Camera& c) {
		return normalize(cross(forward(c), { 0.0f, 1.0f, 0.0f }));
	}

	// sway moves and rolls the view without turning it, so aim is unaffected
	mat4 view(const Camera& c) {
		vec3 f = forward(c);
		mat4 v = mat4_look_at(c.position, c.position + f, { 0.0f, 1.0f, 0.0f });
		if (c.sway_weight <= 0.0f) return v;

		// highest at mid-stance, when the side drift peaks over the planted foot
		f32 w = c.sway_weight;
		f32 s = math::sin(c.sway_phase);
		vec3 offset = { SWAY_SIDE * w * s, SWAY_UP * w * (0.5f - 0.5f * math::cos(2.0f * c.sway_phase)), 0.0f };
		return mat4_rotate(to_radians(SWAY_ROLL) * w * s, { 0.0f, 0.0f, 1.0f }) * mat4_translate(-offset) * v;
	}

	mat4 projection(const Camera& c, f32 aspect) {
		return mat4_perspective_vk(c.fov_y, aspect, c.near_z, c.far_z);
	}

	static void engage_capture(Camera* c) {
		c->captured = true;
		platform::set_cursor_visible(false);
		i32 cx = platform::window_width()  / 2;
		i32 cy = platform::window_height() / 2;
		platform::set_mouse_pos(cx, cy);
	}

	static void release_capture(Camera* c) {
		c->captured = false;
		platform::set_cursor_visible(true);
	}

	void set_captured(Camera* c, bool captured) {
		if (captured) engage_capture(c);
		else          release_capture(c);
	}

	void look(Camera* c) {
		if (c->captured) {
			i32 cx = platform::window_width()  / 2;
			i32 cy = platform::window_height() / 2;
			i32 dx = platform::mouse_x() - cx;
			i32 dy = platform::mouse_y() - cy;
			if (dx != 0 || dy != 0) {
				c->yaw   -= (f32)dx * c->mouse_sens;
				c->pitch -= (f32)dy * c->mouse_sens;
				if (c->pitch >  PITCH_LIMIT) c->pitch =  PITCH_LIMIT;
				if (c->pitch < -PITCH_LIMIT) c->pitch = -PITCH_LIMIT;
				platform::set_mouse_pos(cx, cy);
			}
		}
	}

	void update(Camera* c, f32 dt) {
		// look: hold RMB to engage capture; release to free the cursor.
		if (platform::mouse_pressed(platform::MOUSE_RIGHT)) {
			engage_capture(c);
		}
		if (platform::mouse_released(platform::MOUSE_RIGHT)) {
			release_capture(c);
		}
		look(c);

		// movement: always active.
		f32 speed = c->move_speed;
		if (platform::key_down(platform::KEY_LEFT_SHIFT) || platform::key_down(platform::KEY_RIGHT_SHIFT)) {
			speed *= c->boost_mult;
		}

		vec3 f = forward(*c);
		vec3 r = right(*c);
		vec3 up = { 0.0f, 1.0f, 0.0f };

		vec3 move = { 0.0f, 0.0f, 0.0f };
		if (platform::key_down(platform::KEY_W)) move += f;
		if (platform::key_down(platform::KEY_S)) move += -f;
		if (platform::key_down(platform::KEY_D)) move += r;
		if (platform::key_down(platform::KEY_A)) move += -r;
		if (platform::key_down(platform::KEY_SPACE)) move += up;
		if (platform::key_down(platform::KEY_LEFT_CTRL) || platform::key_down(platform::KEY_RIGHT_CTRL)) move += -up;

		if (length_sq(move) > 0.0f) {
			move = normalize(move);
			c->position += move * (speed * dt);
		}
	}

}
