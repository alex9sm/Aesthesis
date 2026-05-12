#include "debug.hpp"
#include "shader.hpp"
#include "platform.hpp"
#include "log.hpp"

namespace debug {

	namespace {

		Mode current_mode = MODE_NONE;

		static const char* mode_names[] = {
			"OFF",
			"No Fog of War",
		};

		shader::Program flat_program = { 0 };
		GLint u_mvp   = -1;
		GLint u_color = -1;

		static const char* flat_vert_src =
			"#version 460 core\n"
			"layout(location = 0) in vec3 a_position;\n"
			"layout(location = 1) in vec3 a_normal;\n"
			"layout(location = 2) in vec2 a_texcoord;\n"
			"uniform mat4 u_mvp;\n"
			"void main() {\n"
			"    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
			"}\n";

		static const char* flat_frag_src =
			"#version 460 core\n"
			"uniform vec4 u_color;\n"
			"out vec4 frag_color;\n"
			"void main() {\n"
			"    frag_color = u_color;\n"
			"}\n";

	}

	void init() {
		flat_program = shader::compile(flat_vert_src, flat_frag_src);
		if (!flat_program.id) {
			logger::error("debug: failed to compile flat color shader");
			return;
		}
		u_mvp   = shader::get_uniform(flat_program, "u_mvp");
		u_color = shader::get_uniform(flat_program, "u_color");
	}

	void shutdown() {
		shader::destroy(&flat_program);
	}

	void update() {
		if (platform::key_pressed(platform::KEY_GRAVE)) {
			current_mode = (Mode)(((i32)current_mode + 1) % MODE_COUNT);
			logger::info("debug: view = %s", mode_names[current_mode]);
		}
	}

	Mode active_mode() {
		return current_mode;
	}


	bool is_no_fow_active() {
		return current_mode == MODE_NO_FOW;
	}


}
