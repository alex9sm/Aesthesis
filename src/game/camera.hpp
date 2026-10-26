#pragma once

#include "types.hpp"
#include "math.hpp"

struct Camera {
	vec3 position;
	f32  yaw;        // radians, rotation around world up (Y). 0 looks down -Z.
	f32  pitch;      // radians, looking up = positive. clamped to ~±89°.
	f32  fov_y;      // vertical FOV in radians
	f32  near_z;
	f32  far_z;
	f32  move_speed; // units / second
	f32  boost_mult; // shift multiplier
	f32  mouse_sens; // radians / pixel
	bool captured;   // true while RMB held; mouse delta drives yaw/pitch
	vec2 kick;       // recoil offset (pitch, yaw) in radians on top of the aim; springs back to 0
	vec2 kick_vel;
};

namespace camera {

	void init(Camera* c);
	void update(Camera* c, f32 dt);        // freecam: RMB look + fly
	void look(Camera* c);                  // mouse look only, while captured
	void set_captured(Camera* c, bool captured);

	// recoil: an impulse that peaks at (pitch, yaw) radians, then recovers to the aim
	void add_kick(Camera* c, f32 pitch, f32 yaw);
	void update_kick(Camera* c, f32 dt);

	mat4 view(const Camera& c);
	mat4 projection(const Camera& c, f32 aspect);

	vec3 forward(const Camera& c);
	vec3 right(const Camera& c);

}
