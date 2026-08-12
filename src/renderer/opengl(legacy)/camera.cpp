#include "camera.hpp"
#include "platform.hpp"

Camera camera_create() {
	Camera c = {};
	c.position = { 0.0f, 0.0f, 0.0f };
	c.target_position = { 0.0f, 0.0f, 0.0f };
	c.zoom        = 30.0f;
	c.target_zoom = 30.0f;
	c.pitch = to_radians(60.0f);
	c.target_pitch = to_radians(60.0f);
	c.yaw = 0.0f;
	c.target_yaw = 0.0f;
	c.min_zoom = 30.0f;
	c.max_zoom = 100.0f;
	c.pan_speed = 20.0f;
	c.prev_mouse_x = 0;
	c.prev_mouse_y = 0;
	return c;
}

void camera_update(Camera& cam, f32 dt) {
	f32 speed = cam.pan_speed * (cam.zoom / 30.0f);

	// WASD panning relative to camera yaw
	// camera eye is at position + (sin(yaw), 0, -cos(yaw)) * horiz
	// so "forward" (away from camera) is (-sin(yaw), 0, cos(yaw))
	f32 fwd_x = -sinf(cam.yaw);
	f32 fwd_z = cosf(cam.yaw);
	f32 right_x = cosf(cam.yaw);
	f32 right_z = sinf(cam.yaw);

	if (platform::key_down(platform::KEY_W)) {
		cam.target_position.x += fwd_x * speed * dt;
		cam.target_position.z += fwd_z * speed * dt;
	}
	if (platform::key_down(platform::KEY_S)) {
		cam.target_position.x -= fwd_x * speed * dt;
		cam.target_position.z -= fwd_z * speed * dt;
	}
	if (platform::key_down(platform::KEY_A)) {
		cam.target_position.x += right_x * speed * dt;
		cam.target_position.z += right_z * speed * dt;
	}
	if (platform::key_down(platform::KEY_D)) {
		cam.target_position.x -= right_x * speed * dt;
		cam.target_position.z -= right_z * speed * dt;
	}

	// middle mouse drag to rotate
	i32 mx = platform::mouse_x();
	i32 my = platform::mouse_y();

	if (platform::mouse_down(platform::MOUSE_MIDDLE)) {
		i32 dx = mx - cam.prev_mouse_x;
		i32 dy = my - cam.prev_mouse_y;

		cam.target_yaw += (f32)dx * 0.005f;
		cam.target_pitch += (f32)dy * 0.005f;

		// clamp pitch: ~20 degrees (near horizon) to ~160 degrees (nearly top-down)
		f32 min_pitch = to_radians(10.0f);
		f32 max_pitch = to_radians(85.0f);
		if (cam.target_pitch < min_pitch) cam.target_pitch = min_pitch;
		if (cam.target_pitch > max_pitch) cam.target_pitch = max_pitch;
	}

	cam.prev_mouse_x = mx;
	cam.prev_mouse_y = my;

	f32 lerp = dt * 10.0f;
	if (lerp > 1.0f) lerp = 1.0f;

	cam.position.x += (cam.target_position.x - cam.position.x) * lerp;
	cam.position.z += (cam.target_position.z - cam.position.z) * lerp;
	cam.yaw += (cam.target_yaw - cam.yaw) * lerp;
	cam.pitch += (cam.target_pitch - cam.pitch) * lerp;

	// proportional zoom — scroll sets target, zoom smoothly follows
	f32 scroll = (f32)platform::mouse_scroll();
	if (scroll != 0.0f) {
		f32 zoom_factor = 0.1f; // 10% per scroll notch
		cam.target_zoom *= (1.0f - zoom_factor * scroll);
		if (cam.target_zoom < cam.min_zoom) cam.target_zoom = cam.min_zoom;
		if (cam.target_zoom > cam.max_zoom) cam.target_zoom = cam.max_zoom;
	}

	// smooth zoom interpolation — adjust 12.0f to change response speed
	cam.zoom += (cam.target_zoom - cam.zoom) * (dt * 12.0f < 1.0f ? dt * 12.0f : 1.0f);
}

vec3 camera_eye(const Camera& cam) {
	f32 dist = cam.zoom;
	f32 height = dist * sinf(cam.pitch);
	f32 horiz = dist * cosf(cam.pitch);

	return {
		cam.position.x + sinf(cam.yaw) * horiz,
		cam.position.y + height,
		cam.position.z - cosf(cam.yaw) * horiz
	};
}

mat4 camera_view(const Camera& cam) {
	vec3 eye = camera_eye(cam);
	vec3 center = cam.position;
	vec3 up = { 0.0f, 1.0f, 0.0f };
	return mat4_look_at(eye, center, up);
}

mat4 camera_projection() {
	f32 aspect = (f32)platform::window_width() / (f32)platform::window_height();
	// 0.1 close clip, 2000 far clip plane
	return mat4_perspective(to_radians(45.0f), aspect, 0.1f, 4000.0f);
}
