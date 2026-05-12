#include "shadow.hpp"
#include "log.hpp"

namespace shadow {

	namespace {
		GLuint fbos[CASCADE_COUNT];
		GLuint depth_textures[CASCADE_COUNT];

		shader::Program depth_program;
		GLint u_mvp_loc;

		mat4 light_vp[CASCADE_COUNT];
		// base splits at zoom=30 — scaled proportionally with camera zoom
		static constexpr f32 BASE_SPLITS[CASCADE_COUNT] = { 8.0f, 32.0f, 64.0f };
		static constexpr f32 BASE_ZOOM = 30.0f;
		f32 splits[CASCADE_COUNT] = { 8.0f, 32.0f, 64.0f };
		// ortho extent is larger than split so shadow casters near the
		// distance boundary are fully rasterized (not clipped by the frustum)
		static constexpr f32 ORTHO_PADDING = 2.0f;
		vec3 shadow_center;
	}

	bool init() {
		depth_program = shader::load("shaders/shadow_depth.vert", "shaders/shadow_depth.frag");
		if (!depth_program.id) {
			logger::error("Failed to load shadow depth shaders");
			return false;
		}
		u_mvp_loc = shader::get_uniform(depth_program, "u_mvp");

		gl::GenTextures(CASCADE_COUNT, depth_textures);
		gl::GenFramebuffers(CASCADE_COUNT, fbos);

		for (i32 i = 0; i < CASCADE_COUNT; i++) {
			gl::BindTexture(GL_TEXTURE_2D, depth_textures[i]);
			gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
			               MAP_SIZE, MAP_SIZE, 0,
			               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
			gl::BindTexture(GL_TEXTURE_2D, 0);

			gl::BindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
			gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			                         GL_TEXTURE_2D, depth_textures[i], 0);
			gl::DrawBuffer(GL_NONE);

			GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				logger::error("Shadow FBO %d incomplete: 0x%X", i, status);
				gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
				return false;
			}
		}

		gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
		return true;
	}

	void shutdown() {
		gl::DeleteFramebuffers(CASCADE_COUNT, fbos);
		gl::DeleteTextures(CASCADE_COUNT, depth_textures);
		shader::destroy(&depth_program);
	}

	void update(vec3 sun_dir, vec3 camera_pos, f32 camera_zoom) {
		shadow_center = camera_pos;

		// scale cascade splits with zoom so shadows always fill the screen
		f32 scale = camera_zoom / BASE_ZOOM;
		if (scale < 1.0f) scale = 1.0f;
		for (i32 i = 0; i < CASCADE_COUNT; i++) {
			splits[i] = BASE_SPLITS[i] * scale;
		}

		vec3 light_dir_n = normalize(sun_dir);
		vec3 light_pos = shadow_center - light_dir_n * (500.0f * scale);

		vec3 up = { 0.0f, 1.0f, 0.0f };
		if (fabsf(light_dir_n.y) > 0.99f) {
			up = { 0.0f, 0.0f, 1.0f };
		}
		mat4 light_view = mat4_look_at(light_pos, shadow_center, up);

		for (i32 i = 0; i < CASCADE_COUNT; i++) {
			f32 e = splits[i] * ORTHO_PADDING;
			mat4 light_proj = mat4_ortho(-e, e, -e, e, 0.1f, 1000.0f * scale);
			light_vp[i] = light_proj * light_view;
		}
	}

	void begin_cascade(i32 index) {
		gl::BindFramebuffer(GL_FRAMEBUFFER, fbos[index]);
		gl::Viewport(0, 0, MAP_SIZE, MAP_SIZE);
		gl::Clear(GL_DEPTH_BUFFER_BIT);

		shader::bind(depth_program);
		gl::Enable(GL_DEPTH_TEST);
		gl::DepthFunc(GL_LESS);

		gl::Enable(GL_POLYGON_OFFSET_FILL);
		gl::PolygonOffset(1.0f, 2.0f);

		gl::Enable(GL_CULL_FACE);
		gl::CullFace(GL_BACK);
	}

	void end_cascade() {
		gl::Disable(GL_POLYGON_OFFSET_FILL);
	}

	void end(i32 screen_w, i32 screen_h) {
		gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
		gl::Viewport(0, 0, screen_w, screen_h);
		shader::unbind();
	}

	void bind_maps() {
		for (i32 i = 0; i < CASCADE_COUNT; i++) {
			gl::ActiveTexture(GL_TEXTURE7 + i);
			gl::BindTexture(GL_TEXTURE_2D, depth_textures[i]);
		}
	}

	shader::Program get_depth_program() { return depth_program; }
	GLint get_u_mvp() { return u_mvp_loc; }
	mat4 get_light_vp(i32 cascade) { return light_vp[cascade]; }
	f32 get_split(i32 cascade) { return splits[cascade]; }
	vec3 get_center() { return shadow_center; }

}
