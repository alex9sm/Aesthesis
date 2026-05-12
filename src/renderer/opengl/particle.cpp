#include "particle.hpp"
#include "shader.hpp"
#include "opengl.hpp"
#include "log.hpp"
#include "texture.hpp"
#include <math.h>

namespace particle {

ParticleDef dust_trail = {
	0.5f, 1.5f,             // lifetime_min, lifetime_max
	1.0f, 3.0f,             // speed_min, speed_max
	1.0f, 6.0f,             // size_start, size_end
	0.8f, 0.4f, 0.0f,       // alpha_start, alpha_middle, alpha_end
	2.0f,                    // drag
	60.0f,                   // spread_angle
	0,                       // texture (loaded at init)
	1.0f,                    // rotation_speed (radians/sec)
	1,                       // sheet_cols
	1,                       // sheet_rows
	false,                   // additive
	false,                   // loop
	false,                   // face_up
};

ParticleDef muzzle_flash = {
	0.005f, 0.01f,            // lifetime_min, lifetime_max
	0.0f, 0.0f,              // speed_min, speed_max
	3.0f, 1.0f,              // size_start, size_end
	1.0f, 1.0f, 1.0f,        // alpha_start, alpha_middle, alpha_end
	0.0f,                    // drag
	15.0f,                   // spread_angle
	0,                       // texture (loaded at init)
	1.0f,                    // rotation_speed
	1,                       // sheet_cols
	1,                       // sheet_rows
	true,                    // additive
	false,                   // loop
	false,                   // face_up
};

ParticleDef wind_dust = {
	4.0f, 8.0f,            // lifetime_min, lifetime_max
	20.0f, 20.0f,            // speed_min, speed_max
	200.0f, 200.0f,            // size_start, size_end
	0.0f, 0.2f, 0.0f,       // alpha_start, alpha_middle, alpha_end (fade in then out)
	0.0f,                    // drag
	0.0f,                    // spread_angle (0 = all move in same wind direction)
	0,                       // texture (loaded at init)
	0.0f,                    // rotation_speed
	1,                       // sheet_cols
	1,                       // sheet_rows
	false,                   // additive
	false,                   // loop
	true,                    // face_up
};

ParticleDef explosion_small = {
	1.5f, 2.0f,            // lifetime_min, lifetime_max
	0.0f, 0.0f,            // speed_min, speed_max
	4.0f, 10.0f,            // size_start, size_end
	1.0f, 0.8f, 0.0f,       // alpha_start, alpha_middle, alpha_end 
	0.0f,                    // drag
	0.0f,                    // spread_angle 
	0,                       // texture (loaded at init)
	0.05f,                    // rotation_speed
	8,                       // sheet_cols
	8,                       // sheet_rows
	true,                   // additive
	false,                   // loop
	false,                    // face_up
};

ParticleDef vehicle_smoke = {
	1.5f, 2.0f,            // lifetime_min, lifetime_max
	1.0f, 3.0f,            // speed_min, speed_max
	2.0f, 6.0f,            // size_start, size_end
	1.0f, 0.5f, 0.0f,       // alpha_start, alpha_middle, alpha_end 
	0.0f,                    // drag
	30.0f,                    // spread_angle 
	0,                       // texture (loaded at init)
	0.6f,                    // rotation_speed
	1,                       // sheet_cols
	1,                       // sheet_rows
	false,                   // additive
	false,                   // loop
	false,                    // face_up
};

// particle pool
static Particle pool[MAX_PARTICLES];
static i32 alive_count = 0;

// GPU resources
static GLuint vao = 0;
static GLuint vbo = 0;
static shader::Program prog;

// per-particle GPU data: position(3) + size(1) + alpha(1) + rotation(1) + uv_offset(2) + uv_scale(2) = 10 floats
struct ParticleGPU {
	f32 px, py, pz;
	f32 size;
	f32 alpha;
	f32 rotation;
	f32 uv_x, uv_y;   // spritesheet sub-rect origin
	f32 uv_w, uv_h;   // spritesheet sub-rect size
};

static ParticleGPU gpu_data[MAX_PARTICLES];

// xorshift32 RNG
static u32 rng_state = 12345u;

static u32 xorshift32() {
	u32 x = rng_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_state = x;
	return x;
}

static f32 rand_f32() {
	return (f32)(xorshift32() & 0xFFFFFFu) / (f32)0xFFFFFFu;
}

static f32 rand_range(f32 lo, f32 hi) {
	return lo + rand_f32() * (hi - lo);
}

bool init() {
	prog = shader::load("shaders/particle.vert", "shaders/particle.frag");
	if (!prog.id) {
		logger::error("Failed to load particle shaders");
		return false;
	}

	gl::GenVertexArrays(1, &vao);
	gl::GenBuffers(1, &vbo);

	gl::BindVertexArray(vao);
	gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
	gl::BufferData(GL_ARRAY_BUFFER, sizeof(gpu_data), nullptr, GL_DYNAMIC_DRAW);

	// position (location 0) — per-instance
	gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)0);
	gl::EnableVertexAttribArray(0);
	gl::VertexAttribDivisor(0, 1);

	// size (location 1) — per-instance
	gl::VertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(3 * sizeof(f32)));
	gl::EnableVertexAttribArray(1);
	gl::VertexAttribDivisor(1, 1);

	// alpha (location 2) — per-instance
	gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(4 * sizeof(f32)));
	gl::EnableVertexAttribArray(2);
	gl::VertexAttribDivisor(2, 1);

	// rotation (location 3) — per-instance
	gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(5 * sizeof(f32)));
	gl::EnableVertexAttribArray(3);
	gl::VertexAttribDivisor(3, 1);

	// uv_offset (location 4) — per-instance
	gl::VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(6 * sizeof(f32)));
	gl::EnableVertexAttribArray(4);
	gl::VertexAttribDivisor(4, 1);

	// uv_scale (location 5) — per-instance
	gl::VertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(8 * sizeof(f32)));
	gl::EnableVertexAttribArray(5);
	gl::VertexAttribDivisor(5, 1);

	gl::BindVertexArray(0);
	gl::BindBuffer(GL_ARRAY_BUFFER, 0);

	alive_count = 0;

	// load built-in particle textures
	texture::Texture dust_tex = texture::load("assets/textures/dust_puff.png");
	if (dust_tex.id) {
		dust_trail.texture = dust_tex.id;
	} else {
		logger::warn("Texture not found");
	}

	texture::Texture flash_tex = texture::load("assets/textures/muzzle_flash.png");
	if (flash_tex.id) {
		muzzle_flash.texture = flash_tex.id;
	} else {
		logger::warn("Texture not found");
	}

	texture::Texture wind_tex = texture::load("assets/textures/wind_dust.png");
	if (wind_tex.id) {
		wind_dust.texture = wind_tex.id;
	}
	else {
		logger::warn("Texture not found");
	}

	texture::Texture explosion_small_tex = texture::load("assets/textures/explosion_sm_sheet.png");
	if (explosion_small_tex.id) {
		explosion_small.texture = explosion_small_tex.id;
	}
	else {
		logger::warn("Texture not found");
	}

	texture::Texture vehicle_smoke_tex = texture::load("assets/textures/vehicle_smoke.png");
	if (vehicle_smoke_tex.id) {
		vehicle_smoke.texture = vehicle_smoke_tex.id;
	}
	else {
		logger::warn("Texture not found");
	}

	return true;
}

void shutdown() {
	if (vbo) { gl::DeleteBuffers(1, &vbo); vbo = 0; }
	if (vao) { gl::DeleteVertexArrays(1, &vao); vao = 0; }
	shader::destroy(&prog);
	alive_count = 0;
}

void update(f32 dt) {
	constexpr f32 GRAVITY = -9.8f;

	for (i32 i = 0; i < alive_count; ) {
		Particle& p = pool[i];
		p.life -= dt;

		if (p.life <= 0.0f) {
			// swap-and-pop
			pool[i] = pool[alive_count - 1];
			alive_count--;
			continue;
		}

		// drag: velocity *= (1 - drag * dt), clamped
		f32 drag_factor = 1.0f - p.drag * dt;
		if (drag_factor < 0.0f) drag_factor = 0.0f;
		p.velocity.x *= drag_factor;
		p.velocity.y *= drag_factor;
		p.velocity.z *= drag_factor;

		// gravity
		p.velocity.y += GRAVITY * p.gravity_scale * dt;

		// integrate position
		p.position.x += p.velocity.x * dt;
		p.position.y += p.velocity.y * dt;
		p.position.z += p.velocity.z * dt;

		// rotation
		p.rotation += p.rotation_speed * dt;

		// spritesheet frame advance
		if (p.sheet_cols > 1 || p.sheet_rows > 1) {
			p.frame += p.frame_rate * dt;
			f32 total_frames = (f32)(p.sheet_cols * p.sheet_rows);
			if (p.frame >= total_frames) {
				p.frame = fmodf(p.frame, total_frames);
			}
		}

		i++;
	}
}

// collect unique texture+blend combos used by alive particles
struct Batch { GLuint texture; bool additive; bool face_up; i32 start; i32 count; };
constexpr i32 MAX_BATCHES = 16;

void render(mat4 view, mat4 proj, vec3 cam_pos) {
	if (alive_count <= 0) return;

	// sort particles by texture into batches for minimal state changes
	// first pass: collect unique textures
	Batch batches[MAX_BATCHES];
	i32 batch_count = 0;

	for (i32 i = 0; i < alive_count; i++) {
		GLuint tex = pool[i].texture;
		bool add = pool[i].additive;
		bool fup = pool[i].face_up;
		bool found = false;
		for (i32 b = 0; b < batch_count; b++) {
			if (batches[b].texture == tex && batches[b].additive == add && batches[b].face_up == fup) { found = true; break; }
		}
		if (!found && batch_count < MAX_BATCHES) {
			batches[batch_count].texture = tex;
			batches[batch_count].additive = add;
			batches[batch_count].face_up = fup;
			batches[batch_count].start = 0;
			batches[batch_count].count = 0;
			batch_count++;
		}
	}

	// second pass: build GPU data grouped by batch
	i32 write_idx = 0;
	for (i32 b = 0; b < batch_count; b++) {
		batches[b].start = write_idx;
		for (i32 i = 0; i < alive_count; i++) {
			Particle& p = pool[i];
			if (p.texture != batches[b].texture || p.additive != batches[b].additive || p.face_up != batches[b].face_up) continue;

			f32 t = 1.0f - (p.life / p.max_life);
			f32 size = p.size_start + (p.size_end - p.size_start) * t;
			f32 alpha;
			if (t < 0.5f) {
				alpha = p.alpha_start + (p.alpha_middle - p.alpha_start) * (t * 2.0f);
			} else {
				alpha = p.alpha_middle + (p.alpha_end - p.alpha_middle) * ((t - 0.5f) * 2.0f);
			}

			// compute spritesheet UV sub-rect
			f32 uv_x = 0.0f, uv_y = 0.0f;
			f32 uv_w = 1.0f, uv_h = 1.0f;
			if (p.sheet_cols > 1 || p.sheet_rows > 1) {
				i32 frame_idx = (i32)p.frame;
				i32 total = p.sheet_cols * p.sheet_rows;
				if (frame_idx >= total) frame_idx = total - 1;
				if (frame_idx < 0) frame_idx = 0;
				i32 col = frame_idx % p.sheet_cols;
				i32 row = frame_idx / p.sheet_cols;
				uv_w = 1.0f / (f32)p.sheet_cols;
				uv_h = 1.0f / (f32)p.sheet_rows;
				uv_x = (f32)col * uv_w;
				uv_y = (f32)row * uv_h;
			}

			gpu_data[write_idx].px = p.position.x;
			gpu_data[write_idx].py = p.position.y;
			gpu_data[write_idx].pz = p.position.z;
			gpu_data[write_idx].size = size;
			gpu_data[write_idx].alpha = alpha;
			gpu_data[write_idx].rotation = p.rotation;
			gpu_data[write_idx].uv_x = uv_x;
			gpu_data[write_idx].uv_y = uv_y;
			gpu_data[write_idx].uv_w = uv_w;
			gpu_data[write_idx].uv_h = uv_h;
			write_idx++;
			batches[b].count++;
		}
	}

	// upload all at once
	gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
	gl::BufferSubData(GL_ARRAY_BUFFER, 0, write_idx * (GLsizeiptr)sizeof(ParticleGPU), gpu_data);
	gl::BindBuffer(GL_ARRAY_BUFFER, 0);

	// render batches
	shader::bind(prog);
	shader::set_mat4(shader::get_uniform(prog, "u_view"), view);
	shader::set_mat4(shader::get_uniform(prog, "u_proj"), proj);
	shader::set_vec3(shader::get_uniform(prog, "u_camera_pos"), cam_pos);
	GLint u_texture = shader::get_uniform(prog, "u_texture");
	GLint u_face_up = shader::get_uniform(prog, "u_face_up");

	gl::Enable(GL_BLEND);
	gl::DepthMask(GL_FALSE);

	gl::BindVertexArray(vao);

	for (i32 b = 0; b < batch_count; b++) {
		if (batches[b].count <= 0) continue;

		// set blend mode per batch
		if (batches[b].additive) {
			gl::BlendFunc(GL_SRC_ALPHA, GL_ONE);
		} else {
			gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		shader::set_i32(u_face_up, batches[b].face_up ? 1 : 0);

		gl::ActiveTexture(GL_TEXTURE0);
		gl::BindTexture(GL_TEXTURE_2D, batches[b].texture);
		shader::set_i32(u_texture, 0);

		// rebind with offset for this batch
		gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
		GLsizeiptr offset = batches[b].start * (GLsizeiptr)sizeof(ParticleGPU);
		gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)offset);
		gl::VertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(offset + 3 * sizeof(f32)));
		gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(offset + 4 * sizeof(f32)));
		gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(offset + 5 * sizeof(f32)));
		gl::VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(offset + 6 * sizeof(f32)));
		gl::VertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(offset + 8 * sizeof(f32)));
		gl::BindBuffer(GL_ARRAY_BUFFER, 0);

		gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batches[b].count);
	}

	gl::BindVertexArray(0);
	gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl::DepthMask(GL_TRUE);
	shader::unbind();
}

void spawn(const ParticleDef* def, vec3 position, vec3 direction, i32 count) {
	f32 spread_rad = def->spread_angle * (3.14159265f / 180.0f);

	for (i32 i = 0; i < count; i++) {
		if (alive_count >= MAX_PARTICLES) return;

		Particle& p = pool[alive_count];

		f32 lifetime = rand_range(def->lifetime_min, def->lifetime_max);
		f32 speed = rand_range(def->speed_min, def->speed_max);

		// random direction within cone around 'direction'
		// generate random offset angles
		f32 theta = rand_f32() * 2.0f * 3.14159265f;       // azimuth around direction
		f32 phi = rand_f32() * spread_rad;                   // angle from direction

		// build a local coordinate frame from direction
		vec3 dir = normalize(direction);
		vec3 up = { 0.0f, 1.0f, 0.0f };
		if (fabsf(dot(dir, up)) > 0.99f) up = { 1.0f, 0.0f, 0.0f };
		vec3 right = normalize(cross(dir, up));
		vec3 fwd = cross(right, dir);

		f32 sin_phi = sinf(phi);
		f32 cos_phi = cosf(phi);
		vec3 vel_dir = {
			dir.x * cos_phi + right.x * sin_phi * cosf(theta) + fwd.x * sin_phi * sinf(theta),
			dir.y * cos_phi + right.y * sin_phi * cosf(theta) + fwd.y * sin_phi * sinf(theta),
			dir.z * cos_phi + right.z * sin_phi * cosf(theta) + fwd.z * sin_phi * sinf(theta),
		};

		p.position = position;
		p.velocity = { vel_dir.x * speed, vel_dir.y * speed, vel_dir.z * speed };
		p.life = lifetime;
		p.max_life = lifetime;
		p.size_start = def->size_start;
		p.size_end = def->size_end;
		p.alpha_start = def->alpha_start;
		p.alpha_middle = def->alpha_middle;
		p.alpha_end = def->alpha_end;
		p.drag = def->drag;
		p.texture = def->texture;
		p.face_up = def->face_up;
		if (def->rotation_speed == 0.0f) {
			p.rotation = 0.0f;
			p.rotation_speed = 0.0f;
		} else {
			p.rotation = rand_f32() * 2.0f * 3.14159265f;
			p.rotation_speed = def->rotation_speed * (rand_f32() > 0.5f ? 1.0f : -1.0f);
		}

		// spritesheet fields
		p.sheet_cols = def->sheet_cols > 0 ? def->sheet_cols : 1;
		p.sheet_rows = def->sheet_rows > 0 ? def->sheet_rows : 1;
		p.additive = def->additive;
		p.frame = 0.0f;
		i32 total_frames = p.sheet_cols * p.sheet_rows;
		if (total_frames > 1 && lifetime > 0.0f) {
			if (def->loop) {
				p.frame_rate = (f32)total_frames / lifetime;
			} else {
				// play once: last frame should be visible at end of life
				p.frame_rate = (f32)(total_frames - 1) / lifetime;
			}
		} else {
			p.frame_rate = 0.0f;
		}

		alive_count++;
	}
}

void spawn_local(const ParticleDef* def, vec3 entity_pos, f32 yaw, vec3 local_offset, vec3 direction, i32 count) {
	f32 sy = sinf(yaw), cy = cosf(yaw);
	vec3 world_pos = {
		entity_pos.x + cy * local_offset.x + sy * local_offset.z,
		entity_pos.y + local_offset.y,
		entity_pos.z - sy * local_offset.x + cy * local_offset.z,
	};
	spawn(def, world_pos, direction, count);
}

} // namespace particle
