#include "shader.hpp"
#include "file.hpp"
#include "memory.hpp"
#include "log.hpp"

namespace shader {

	namespace {

		GLuint compile_stage(GLenum type, const char* source) {
			GLuint id = gl::CreateShader(type);
			gl::ShaderSource(id, 1, &source, nullptr);
			gl::CompileShader(id);

			GLint success = 0;
			gl::GetShaderiv(id, GL_COMPILE_STATUS, &success);
			if (!success) {
				GLint len = 0;
				gl::GetShaderiv(id, GL_INFO_LOG_LENGTH, &len);
				char buf[1024];
				if (len > (GLint)sizeof(buf)) len = (GLint)sizeof(buf);
				gl::GetShaderInfoLog(id, len, nullptr, buf);
				buf[len < (GLint)sizeof(buf) ? len : (GLint)sizeof(buf) - 1] = '\0';

				const char* stage = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
				logger::error("Shader compile failed (%s): %s", stage, buf);

				gl::DeleteShader(id);
				return 0;
			}

			return id;
		}

	}

	Program compile(const char* vert_source, const char* frag_source) {
		Program result = { 0 };

		GLuint vert = compile_stage(GL_VERTEX_SHADER, vert_source);
		if (!vert) return result;

		GLuint frag = compile_stage(GL_FRAGMENT_SHADER, frag_source);
		if (!frag) {
			gl::DeleteShader(vert);
			return result;
		}

		GLuint program = gl::CreateProgram();
		gl::AttachShader(program, vert);
		gl::AttachShader(program, frag);
		gl::LinkProgram(program);

		gl::DeleteShader(vert);
		gl::DeleteShader(frag);

		GLint success = 0;
		gl::GetProgramiv(program, GL_LINK_STATUS, &success);
		if (!success) {
			GLint len = 0;
			gl::GetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
			char buf[1024];
			if (len > (GLint)sizeof(buf)) len = (GLint)sizeof(buf);
			gl::GetProgramInfoLog(program, len, nullptr, buf);
			buf[len < (GLint)sizeof(buf) ? len : (GLint)sizeof(buf) - 1] = '\0';

			logger::error("Shader link failed: %s", buf);

			gl::DeleteProgram(program);
			return result;
		}

		result.id = program;
		return result;
	}

	Program load(const char* vert_path, const char* frag_path) {
		Program result = { 0 };

		u64 vert_size = 0;
		u64 frag_size = 0;
		if (!file::get_size(vert_path, &vert_size) || !file::get_size(frag_path, &frag_size)) {
			logger::error("Shader file not found");
			return result;
		}

		char* vert_source = (char*)memory::malloc(vert_size + 1);
		char* frag_source = (char*)memory::malloc(frag_size + 1);

		file::read_file(vert_path, vert_source, vert_size);
		file::read_file(frag_path, frag_source, frag_size);
		vert_source[vert_size] = '\0';
		frag_source[frag_size] = '\0';

		result = compile(vert_source, frag_source);

		memory::free(frag_source);
		memory::free(vert_source);

		return result;
	}

	void destroy(Program* program) {
		if (program->id) {
			gl::DeleteProgram(program->id);
			program->id = 0;
		}
	}

	void bind(Program program)   { gl::UseProgram(program.id); }
	void unbind()                 { gl::UseProgram(0); }

	GLint get_uniform(Program program, const char* name) {
		return gl::GetUniformLocation(program.id, name);
	}

	void set_i32(GLint loc, i32 val)            { gl::Uniform1i(loc, val); }
	void set_f32(GLint loc, f32 val)             { gl::Uniform1f(loc, val); }
	void set_vec2(GLint loc, vec2 v)             { gl::Uniform2f(loc, v.x, v.y); }
	void set_vec3(GLint loc, vec3 v)             { gl::Uniform3f(loc, v.x, v.y, v.z); }
	void set_vec4(GLint loc, vec4 v)             { gl::Uniform4f(loc, v.x, v.y, v.z, v.w); }
	void set_mat4(GLint loc, const mat4& m)      { gl::UniformMatrix4fv(loc, 1, GL_FALSE, &m.col[0][0]); }
	void set_f32_array(GLint loc, const f32* data, i32 count)  { gl::Uniform1fv(loc, count, data); }
	void set_vec3_array(GLint loc, const f32* data, i32 count) { gl::Uniform3fv(loc, count, data); }
	void set_vec4_array(GLint loc, const f32* data, i32 count) { gl::Uniform4fv(loc, count, data); }

}
