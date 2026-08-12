#pragma once

#include "types.hpp"
#include "math.hpp"
#include "opengl.hpp"

namespace particle {

struct ParticleDef {
	f32  lifetime_min, lifetime_max;
	f32  speed_min, speed_max;
	f32  size_start, size_end;
	f32  alpha_start, alpha_middle, alpha_end;  // opacity: start→middle over first half, middle→end over second half
	f32  drag;                // velocity damping per second
	f32  spread_angle;        // cone half-angle in degrees
	GLuint texture;
	f32  rotation_speed;      // radians/sec — each particle gets random start angle and random +/- direction
	i32  sheet_cols;          // spritesheet columns (default 1 = single texture)
	i32  sheet_rows;          // spritesheet rows (default 1)
	bool additive;            // false = alpha blend, true = additive (GL_ONE)
	bool loop;                // true = loop animation, false = play once over lifetime
	bool face_up;             // true = quad lies flat on XZ plane, false = camera billboard
};

struct Particle {
	vec3 position;
	vec3 velocity;
	f32  life;
	f32  max_life;
	f32  size_start;
	f32  size_end;
	f32  alpha_start;
	f32  alpha_middle;
	f32  alpha_end;
	f32  drag;
	f32  gravity_scale;
	GLuint texture;
	f32  rotation;            // current angle in radians
	f32  rotation_speed;      // radians/sec (sign = direction)
	f32  frame;               // current animation frame (float for smooth advance)
	f32  frame_rate;          // frames/sec, auto-calculated from sheet size / lifetime
	i32  sheet_cols;
	i32  sheet_rows;
	bool additive;
	bool face_up;
};

constexpr i32 MAX_PARTICLES = 4096;

bool init();
void shutdown();
void update(f32 dt);
void render(mat4 view, mat4 proj, vec3 cam_pos);
// position: world-space spawn point. direction: emission direction. count: how many.
void spawn(const ParticleDef* def, vec3 position, vec3 direction, i32 count);

// local_offset rotated by yaw and added to entity_pos. Use for wheel positions etc.
void spawn_local(const ParticleDef* def, vec3 entity_pos, f32 yaw, vec3 local_offset, vec3 direction, i32 count);

extern ParticleDef dust_trail;
extern ParticleDef muzzle_flash;
extern ParticleDef wind_dust;
extern ParticleDef explosion_small;
extern ParticleDef vehicle_smoke;

} // namespace particle
