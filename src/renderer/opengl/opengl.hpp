#pragma once

#include "types.hpp"

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef double         GLdouble;
typedef char           GLchar;
typedef isize          GLsizeiptr;
typedef isize          GLintptr;

// constants vvv

// strings
#define GL_VERSION                  0x1F02
#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01

// boolean
#define GL_TRUE                     1
#define GL_FALSE                    0

// types
#define GL_FLOAT                    0x1406
#define GL_UNSIGNED_INT             0x1405
#define GL_UNSIGNED_SHORT           0x1403
#define GL_UNSIGNED_BYTE            0x1401
#define GL_BYTE                     0x1400
#define GL_INT                      0x1404

// clear
#define GL_COLOR_BUFFER_BIT         0x00004000
#define GL_DEPTH_BUFFER_BIT         0x00000100
#define GL_STENCIL_BUFFER_BIT       0x00000400

// enable
#define GL_BLEND                    0x0BE2
#define GL_DEPTH_TEST               0x0B71
#define GL_LESS                     0x0201
#define GL_LEQUAL                   0x0203
#define GL_EQUAL                    0x202
#define GL_CULL_FACE                0x0B44
#define GL_SCISSOR_TEST             0x0C11
#define GL_MULTISAMPLE              0x809D
#define GL_STENCIL_TEST             0x0B90

// stencil
#define GL_KEEP                     0x1E00
#define GL_REPLACE                  0x1E01
#define GL_ALWAYS                   0x0207
#define GL_NOTEQUAL                 0x0205

// blend
#define GL_ZERO                     0x0000
#define GL_ONE                      0x0001
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303

// texture
#define GL_TEXTURE_2D               0x0DE1
#define GL_TEXTURE_CUBE_MAP         0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_WRAP_R           0x8072
#define GL_TEXTURE0                 0x84C0
#define GL_TEXTURE1                 0x84C1
#define GL_TEXTURE2                 0x84C2
#define GL_TEXTURE3                 0x84C3
#define GL_TEXTURE4                 0x84C4
#define GL_TEXTURE5					0x84C5
#define GL_TEXTURE6					0x84C6
#define GL_TEXTURE7					0x84C7
#define GL_TEXTURE8					0x84C8
#define GL_TEXTURE9					0x84C9
#define GL_TEXTURE10				0x84ca
#define GL_TEXTURE11				0x84cb
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_NEAREST                  0x2600
#define GL_LINEAR                   0x2601
#define GL_LINEAR_MIPMAP_LINEAR     0x2703
#define GL_NEAREST_MIPMAP_NEAREST   0x2700
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_REPEAT                   0x2901
#define GL_RED                      0x1903
#define GL_RGB                      0x1907
#define GL_RGBA                     0x1908
#define GL_R8                       0x8229
#define GL_R16                      0x822a
#define GL_RGB8                     0x8051
#define GL_RGB16F                   0x881b
#define GL_RGBA8                    0x8058
#define GL_SRGB8_ALPHA8             0x8C43
#define GL_UNPACK_ALIGNMENT         0x0CF5
#define GL_TEXTURE_SWIZZLE_R        0x8E42
#define GL_TEXTURE_SWIZZLE_G        0x8E43
#define GL_TEXTURE_SWIZZLE_B        0x8E44
#define GL_TEXTURE_SWIZZLE_A        0x8E45

// shader
#define GL_VERTEX_SHADER            0x8B31
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_INFO_LOG_LENGTH          0x8B84

// buffer
#define GL_ARRAY_BUFFER             0x8892
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#define GL_STATIC_DRAW              0x88E4
#define GL_DYNAMIC_DRAW             0x88E8
#define GL_MAP_WRITE_BIT            0x0002
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008

// draw mode
#define GL_POINTS                   0x0000
#define GL_LINES                    0x0001
#define GL_LINE_STRIP               0x0003
#define GL_TRIANGLES                0x0004
#define GL_TRIANGLE_STRIP           0x0005
#define GL_TRIANGLE_FAN             0x0006

// framebuffer
#define GL_FRAMEBUFFER              0x8D40
#define GL_READ_FRAMEBUFFER         0x8CA8
#define GL_DRAW_FRAMEBUFFER         0x8CA9
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_DEPTH_ATTACHMENT         0x8D00
#define GL_FRAMEBUFFER_COMPLETE     0x8CD5
#define GL_RENDERBUFFER             0x8D41
#define GL_DEPTH_COMPONENT          0x1902
#define GL_DEPTH_COMPONENT24        0x81A6
#define GL_DEPTH24_STENCIL8         0x88F0

// shadow / comparison
#define GL_TEXTURE_COMPARE_MODE     0x884C
#define GL_TEXTURE_COMPARE_FUNC     0x884D
#define GL_COMPARE_REF_TO_TEXTURE   0x884E
#define GL_NONE                     0
#define GL_FRONT                    0x0404
#define GL_BACK                     0x0405
#define GL_POLYGON_OFFSET_FILL      0x8037

// SSBO
#define GL_SHADER_STORAGE_BUFFER    0x90D2

// function pointers vvv

namespace gl {

	// state
	extern const GLubyte* (*GetString)(GLenum);
	extern void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
	extern void (*Clear)(GLbitfield);
	extern void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
	extern void (*Enable)(GLenum);
	extern void (*Disable)(GLenum);
	extern void (*DepthMask)(GLboolean);
	extern void (*DepthFunc)(GLenum);
	extern void (*BlendFunc)(GLenum, GLenum);
	extern void (*Scissor)(GLint, GLint, GLsizei, GLsizei);
	extern void (*PixelStorei)(GLenum, GLint);

	// shader
	extern GLuint (*CreateShader)(GLenum);
	extern void   (*DeleteShader)(GLuint);
	extern void   (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
	extern void   (*CompileShader)(GLuint);
	extern void   (*GetShaderiv)(GLuint, GLenum, GLint*);
	extern void   (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);

	// program
	extern GLuint (*CreateProgram)();
	extern void   (*DeleteProgram)(GLuint);
	extern void   (*AttachShader)(GLuint, GLuint);
	extern void   (*LinkProgram)(GLuint);
	extern void   (*UseProgram)(GLuint);
	extern void   (*GetProgramiv)(GLuint, GLenum, GLint*);
	extern void   (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);

	// uniforms
	extern GLint (*GetUniformLocation)(GLuint, const GLchar*);
	extern void  (*Uniform1i)(GLint, GLint);
	extern void  (*Uniform1f)(GLint, GLfloat);
	extern void  (*Uniform2f)(GLint, GLfloat, GLfloat);
	extern void  (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
	extern void  (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
	extern void  (*Uniform1fv)(GLint, GLsizei, const GLfloat*);
	extern void  (*Uniform3fv)(GLint, GLsizei, const GLfloat*);
	extern void  (*Uniform4fv)(GLint, GLsizei, const GLfloat*);
	extern void  (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);

	// VAO
	extern void (*GenVertexArrays)(GLsizei, GLuint*);
	extern void (*DeleteVertexArrays)(GLsizei, const GLuint*);
	extern void (*BindVertexArray)(GLuint);

	// VBO / EBO
	extern void (*GenBuffers)(GLsizei, GLuint*);
	extern void (*DeleteBuffers)(GLsizei, const GLuint*);
	extern void (*BindBuffer)(GLenum, GLuint);
	extern void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
	extern void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
	extern void* (*MapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
	extern GLboolean (*UnmapBuffer)(GLenum);

	// vertex attributes
	extern void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
	extern void (*EnableVertexAttribArray)(GLuint);

	// draw
	extern void (*DrawArrays)(GLenum, GLint, GLsizei);
	extern void (*DrawElements)(GLenum, GLsizei, GLenum, const void*);
	extern void (*VertexAttribDivisor)(GLuint, GLuint);
	extern void (*DrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
	extern void (*DrawElementsInstanced)(GLenum, GLsizei, GLenum, const void*, GLsizei);

	// texture
	extern void (*GenTextures)(GLsizei, GLuint*);
	extern void (*DeleteTextures)(GLsizei, const GLuint*);
	extern void (*BindTexture)(GLenum, GLuint);
	extern void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
	extern void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
	extern void (*TexParameteri)(GLenum, GLenum, GLint);
	extern void (*ActiveTexture)(GLenum);
	extern void (*GenerateMipmap)(GLenum);

	// framebuffer
	extern void   (*GenFramebuffers)(GLsizei, GLuint*);
	extern void   (*DeleteFramebuffers)(GLsizei, const GLuint*);
	extern void   (*BindFramebuffer)(GLenum, GLuint);
	extern void   (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
	extern GLenum (*CheckFramebufferStatus)(GLenum);
	extern void   (*GenRenderbuffers)(GLsizei, GLuint*);
	extern void   (*DeleteRenderbuffers)(GLsizei, const GLuint*);
	extern void   (*BindRenderbuffer)(GLenum, GLuint);
	extern void   (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
	extern void   (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
	extern void   (*DrawBuffer)(GLenum);
	extern void   (*CullFace)(GLenum);
	extern void   (*PolygonOffset)(GLfloat, GLfloat);

	// stencil
	extern void   (*StencilFunc)(GLenum, GLint, GLuint);
	extern void   (*StencilOp)(GLenum, GLenum, GLenum);
	extern void   (*StencilMask)(GLuint);
	extern void   (*ClearStencil)(GLint);
	extern void   (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);

	// SSBO
	extern void (*BindBufferBase)(GLenum, GLuint, GLuint);

}

// API
namespace renderer {

	bool init();
	void shutdown();

}
