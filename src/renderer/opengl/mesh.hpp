#pragma once

#include "opengl.hpp"
#include "math.hpp"

namespace mesh {

	struct Vertex {
		vec3 position;
		vec3 normal;
		vec2 texcoord;
	};

	struct Mesh {
		GLuint vao;
		GLuint vbo;
		GLuint ebo;
		i32 index_count;
		i32 vertex_count;
	};

	Mesh create(const Vertex* vertices, i32 vertex_count, const u32* indices, i32 index_count);
	Mesh create_no_indices(const Vertex* vertices, i32 vertex_count);
	void destroy(Mesh* m);
	void draw(const Mesh& m);

	Mesh generate_plane(f32 size, i32 subdivisions);
	Mesh generate_cube(f32 half_extent);

}
