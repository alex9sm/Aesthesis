#include "opengl.hpp"
#include "platform.hpp"
#include "log.hpp"

namespace gl {

	// state
	const GLubyte* (*GetString)(GLenum) = nullptr;
	void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
	void (*Clear)(GLbitfield) = nullptr;
	void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
	void (*Enable)(GLenum) = nullptr;
	void (*Disable)(GLenum) = nullptr;
	void (*DepthMask)(GLboolean) = nullptr;
	void (*DepthFunc)(GLenum) = nullptr;
	void (*BlendFunc)(GLenum, GLenum) = nullptr;
	void (*Scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
	void (*PixelStorei)(GLenum, GLint) = nullptr;

	// shader
	GLuint (*CreateShader)(GLenum) = nullptr;
	void   (*DeleteShader)(GLuint) = nullptr;
	void   (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
	void   (*CompileShader)(GLuint) = nullptr;
	void   (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
	void   (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;

	// program
	GLuint (*CreateProgram)() = nullptr;
	void   (*DeleteProgram)(GLuint) = nullptr;
	void   (*AttachShader)(GLuint, GLuint) = nullptr;
	void   (*LinkProgram)(GLuint) = nullptr;
	void   (*UseProgram)(GLuint) = nullptr;
	void   (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
	void   (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;

	// uniforms
	GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
	void  (*Uniform1i)(GLint, GLint) = nullptr;
	void  (*Uniform1f)(GLint, GLfloat) = nullptr;
	void  (*Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
	void  (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat) = nullptr;
	void  (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
	void  (*Uniform1fv)(GLint, GLsizei, const GLfloat*) = nullptr;
	void  (*Uniform3fv)(GLint, GLsizei, const GLfloat*) = nullptr;
	void  (*Uniform4fv)(GLint, GLsizei, const GLfloat*) = nullptr;
	void  (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;

	// VAO
	void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
	void (*DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
	void (*BindVertexArray)(GLuint) = nullptr;

	// VBO / EBO
	void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
	void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
	void (*BindBuffer)(GLenum, GLuint) = nullptr;
	void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
	void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
	void* (*MapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield) = nullptr;
	GLboolean (*UnmapBuffer)(GLenum) = nullptr;

	// vertex attributes
	void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
	void (*EnableVertexAttribArray)(GLuint) = nullptr;

	// draw
	void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
	void (*DrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
	void (*VertexAttribDivisor)(GLuint, GLuint) = nullptr;
	void (*DrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei) = nullptr;
	void (*DrawElementsInstanced)(GLenum, GLsizei, GLenum, const void*, GLsizei) = nullptr;

	// texture
	void (*GenTextures)(GLsizei, GLuint*) = nullptr;
	void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
	void (*BindTexture)(GLenum, GLuint) = nullptr;
	void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
	void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
	void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
	void (*ActiveTexture)(GLenum) = nullptr;
	void (*GenerateMipmap)(GLenum) = nullptr;

	// framebuffer
	void   (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
	void   (*DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
	void   (*BindFramebuffer)(GLenum, GLuint) = nullptr;
	void   (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
	GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
	void   (*GenRenderbuffers)(GLsizei, GLuint*) = nullptr;
	void   (*DeleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
	void   (*BindRenderbuffer)(GLenum, GLuint) = nullptr;
	void   (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
	void   (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
	void   (*DrawBuffer)(GLenum) = nullptr;
	void   (*CullFace)(GLenum) = nullptr;
	void   (*PolygonOffset)(GLfloat, GLfloat) = nullptr;

	// stencil
	void   (*StencilFunc)(GLenum, GLint, GLuint) = nullptr;
	void   (*StencilOp)(GLenum, GLenum, GLenum) = nullptr;
	void   (*StencilMask)(GLuint) = nullptr;
	void   (*ClearStencil)(GLint) = nullptr;
	void   (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean) = nullptr;

	// SSBO
	void (*BindBufferBase)(GLenum, GLuint, GLuint) = nullptr;

}

namespace renderer {

	namespace {

		#define GL_LOAD(name) gl::name = (decltype(gl::name))platform::gl_get_proc_address("gl" #name)

		bool load_functions() {
			GL_LOAD(GetString);
			if (!gl::GetString) return false;

			// state
			GL_LOAD(Viewport);
			GL_LOAD(Clear);
			GL_LOAD(ClearColor);
			GL_LOAD(Enable);
			GL_LOAD(Disable);
			GL_LOAD(DepthMask);
			GL_LOAD(DepthFunc);
			GL_LOAD(BlendFunc);
			GL_LOAD(Scissor);
			GL_LOAD(PixelStorei);

			// shader
			GL_LOAD(CreateShader);
			GL_LOAD(DeleteShader);
			GL_LOAD(ShaderSource);
			GL_LOAD(CompileShader);
			GL_LOAD(GetShaderiv);
			GL_LOAD(GetShaderInfoLog);

			// program
			GL_LOAD(CreateProgram);
			GL_LOAD(DeleteProgram);
			GL_LOAD(AttachShader);
			GL_LOAD(LinkProgram);
			GL_LOAD(UseProgram);
			GL_LOAD(GetProgramiv);
			GL_LOAD(GetProgramInfoLog);

			// uniforms
			GL_LOAD(GetUniformLocation);
			GL_LOAD(Uniform1i);
			GL_LOAD(Uniform1f);
			GL_LOAD(Uniform2f);
			GL_LOAD(Uniform3f);
			GL_LOAD(Uniform4f);
			GL_LOAD(Uniform1fv);
			GL_LOAD(Uniform3fv);
			GL_LOAD(Uniform4fv);
			GL_LOAD(UniformMatrix4fv);

			// VAO
			GL_LOAD(GenVertexArrays);
			GL_LOAD(DeleteVertexArrays);
			GL_LOAD(BindVertexArray);

			// VBO / EBO
			GL_LOAD(GenBuffers);
			GL_LOAD(DeleteBuffers);
			GL_LOAD(BindBuffer);
			GL_LOAD(BufferData);
			GL_LOAD(BufferSubData);
			GL_LOAD(MapBufferRange);
			GL_LOAD(UnmapBuffer);

			// vertex attributes
			GL_LOAD(VertexAttribPointer);
			GL_LOAD(EnableVertexAttribArray);

			// draw
			GL_LOAD(DrawArrays);
			GL_LOAD(DrawElements);
			GL_LOAD(VertexAttribDivisor);
			GL_LOAD(DrawArraysInstanced);
			GL_LOAD(DrawElementsInstanced);

			// texture
			GL_LOAD(GenTextures);
			GL_LOAD(DeleteTextures);
			GL_LOAD(BindTexture);
			GL_LOAD(TexImage2D);
			GL_LOAD(TexSubImage2D);
			GL_LOAD(TexParameteri);
			GL_LOAD(ActiveTexture);
			GL_LOAD(GenerateMipmap);

			// framebuffer
			GL_LOAD(GenFramebuffers);
			GL_LOAD(DeleteFramebuffers);
			GL_LOAD(BindFramebuffer);
			GL_LOAD(FramebufferTexture2D);
			GL_LOAD(CheckFramebufferStatus);
			GL_LOAD(GenRenderbuffers);
			GL_LOAD(DeleteRenderbuffers);
			GL_LOAD(BindRenderbuffer);
			GL_LOAD(RenderbufferStorage);
			GL_LOAD(FramebufferRenderbuffer);
			GL_LOAD(DrawBuffer);
			GL_LOAD(CullFace);
			GL_LOAD(PolygonOffset);

			// stencil
			GL_LOAD(StencilFunc);
			GL_LOAD(StencilOp);
			GL_LOAD(StencilMask);
			GL_LOAD(ClearStencil);
			GL_LOAD(ColorMask);

			// SSBO
			GL_LOAD(BindBufferBase);

			return true;
		}

		#undef GL_LOAD

	}

	bool init() {
		if (!load_functions()) return false;

		const char* version = (const char*)gl::GetString(GL_VERSION);
		const char* gpu     = (const char*)gl::GetString(GL_RENDERER);

		logger::info("OpenGL initialized");
		logger::info("  Version:  %s", version ? version : "unknown");
		logger::info("  Renderer: %s", gpu     ? gpu     : "unknown");

		return true;
	}

	void shutdown() {
	}

}
