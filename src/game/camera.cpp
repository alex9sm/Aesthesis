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
	}

	vec3 forward(const Camera& c) {
		f32 cp = cosf(c.pitch);
		return {
			-sinf(c.yaw) * cp,
			 sinf(c.pitch),
			-cosf(c.yaw) * cp
		};
	}

	vec3 right(const Camera& c) {
		return normalize(cross(forward(c), { 0.0f, 1.0f, 0.0f }));
	}

	mat4 view(const Camera& c) {
		vec3 f = forward(c);
		return mat4_look_at(c.position, c.position + f, { 0.0f, 1.0f, 0.0f });
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

	void update(Camera* c, f32 dt) {
		// look: hold RMB to engage capture; release to free the cursor.
		if (platform::mouse_pressed(platform::MOUSE_RIGHT)) {
			engage_capture(c);
		}
		if (platform::mouse_released(platform::MOUSE_RIGHT)) {
			release_capture(c);
		}

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
