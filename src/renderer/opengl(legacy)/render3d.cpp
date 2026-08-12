#include "render3d.hpp"
#include "log.hpp"

namespace render3d {

	namespace {
		shader::Program program;
		GLint u_model;
		GLint u_view;
		GLint u_projection;
		GLint u_color;
		GLint u_light_dir;
		GLint u_diffuse;
		GLint u_normal_map;
		GLint u_has_texture;
		GLint u_has_normal_map;
		GLint u_sun_color;
		GLint u_ambient_color;
		GLint u_camera_pos;
		GLint u_fog_color;
		GLint u_fog_start;
		GLint u_fog_end;
		GLint u_shadow_vp[3];
		GLint u_shadow_map[3];
		GLint u_shadow_splits;
		GLint u_shadow_center;
		GLint u_alpha;
	}

	bool init() {
		program = shader::load("shaders/mesh.vert", "shaders/mesh.frag");
		if (!program.id) {
			logger::error("Failed to load mesh shaders");
			return false;
		}

		u_model          = shader::get_uniform(program, "u_model");
		u_view           = shader::get_uniform(program, "u_view");
		u_projection     = shader::get_uniform(program, "u_projection");
		u_color          = shader::get_uniform(program, "u_color");
		u_light_dir      = shader::get_uniform(program, "u_light_dir");
		u_diffuse        = shader::get_uniform(program, "u_diffuse");
		u_normal_map     = shader::get_uniform(program, "u_normal_map");
		u_has_texture    = shader::get_uniform(program, "u_has_texture");
		u_has_normal_map = shader::get_uniform(program, "u_has_normal_map");
		u_sun_color      = shader::get_uniform(program, "u_sun_color");
		u_ambient_color  = shader::get_uniform(program, "u_ambient_color");
		u_camera_pos     = shader::get_uniform(program, "u_camera_pos");
		u_fog_color      = shader::get_uniform(program, "u_fog_color");
		u_fog_start      = shader::get_uniform(program, "u_fog_start");
		u_fog_end        = shader::get_uniform(program, "u_fog_end");
		u_shadow_vp[0]   = shader::get_uniform(program, "u_shadow_vp[0]");
		u_shadow_vp[1]   = shader::get_uniform(program, "u_shadow_vp[1]");
		u_shadow_vp[2]   = shader::get_uniform(program, "u_shadow_vp[2]");
		u_shadow_map[0]  = shader::get_uniform(program, "u_shadow_map[0]");
		u_shadow_map[1]  = shader::get_uniform(program, "u_shadow_map[1]");
		u_shadow_map[2]  = shader::get_uniform(program, "u_shadow_map[2]");
		u_shadow_splits  = shader::get_uniform(program, "u_shadow_splits");
		u_shadow_center  = shader::get_uniform(program, "u_shadow_center");
		u_alpha          = shader::get_uniform(program, "u_alpha");

		return true;
	}

	void shutdown() {
		shader::destroy(&program);
	}

	void begin(mat4 view, mat4 projection) {
		shader::bind(program);
		shader::set_mat4(u_view, view);
		shader::set_mat4(u_projection, projection);

		// defaults — caller should use set_sun_dir/set_camera_pos after begin()
		shader::set_vec3(u_sun_color, vec3{ 1.0f, 0.95f, 0.85f });
		shader::set_vec3(u_ambient_color, vec3{ 0.15f, 0.18f, 0.25f });

		// default: no texture
		shader::set_i32(u_diffuse, 0);
		shader::set_i32(u_normal_map, 1);
		shader::set_i32(u_has_texture, 0);
		shader::set_i32(u_has_normal_map, 0);
		shader::set_f32(u_alpha, 1.0f);
	}

	void set_sun_dir(vec3 dir) {
		shader::set_vec3(u_light_dir, dir);
	}

	void set_camera_pos(vec3 pos) {
		shader::set_vec3(u_camera_pos, pos);
	}

	void set_fog(vec3 color, f32 start, f32 end) {
		shader::set_vec3(u_fog_color, color);
		shader::set_f32(u_fog_start, start);
		shader::set_f32(u_fog_end, end);
	}

	void set_shadow(const mat4* shadow_vps, const f32* shadow_splits, vec3 shadow_center) {
		for (i32 i = 0; i < 3; i++) {
			shader::set_mat4(u_shadow_vp[i], shadow_vps[i]);
			shader::set_i32(u_shadow_map[i], 7 + i);
		}
		shader::set_f32_array(u_shadow_splits, shadow_splits, 3);
		shader::set_vec3(u_shadow_center, shadow_center);
	}

	void set_alpha(f32 a) {
		shader::set_f32(u_alpha, a);
	}

	void draw_mesh(const mesh::Mesh& m, mat4 model, vec4 color) {
		shader::set_mat4(u_model, model);
		shader::set_vec4(u_color, color);
		shader::set_i32(u_has_texture, 0);
		shader::set_i32(u_has_normal_map, 0);
		mesh::draw(m);
	}

	void draw_mesh_textured(const mesh::Mesh& m, mat4 model, GLuint diffuse_tex, GLuint normal_tex) {
		shader::set_mat4(u_model, model);
		shader::set_vec4(u_color, vec4{ 1.0f, 1.0f, 1.0f, 1.0f });

		if (diffuse_tex) {
			gl::ActiveTexture(GL_TEXTURE0);
			gl::BindTexture(GL_TEXTURE_2D, diffuse_tex);
			shader::set_i32(u_diffuse, 0);
			shader::set_i32(u_has_texture, 1);
		} else {
			shader::set_i32(u_has_texture, 0);
		}

		if (normal_tex) {
			gl::ActiveTexture(GL_TEXTURE1);
			gl::BindTexture(GL_TEXTURE_2D, normal_tex);
			shader::set_i32(u_normal_map, 1);
			shader::set_i32(u_has_normal_map, 1);
		} else {
			shader::set_i32(u_has_normal_map, 0);
		}

		mesh::draw(m);

	}

	void flush() {
		shader::unbind();
	}

}
