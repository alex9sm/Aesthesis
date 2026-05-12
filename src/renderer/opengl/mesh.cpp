#include "mesh.hpp"
#include "memory.hpp"

#ifndef offsetof
#define offsetof(type, member) ((usize)&(((type*)0)->member))
#endif

namespace mesh {

	static void setup_vertex_attribs() {
		// position
		gl::EnableVertexAttribArray(0);
		gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
		// normal
		gl::EnableVertexAttribArray(1);
		gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
		// texcoord
		gl::EnableVertexAttribArray(2);
		gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texcoord));
	}

	Mesh create(const Vertex* vertices, i32 vertex_count, const u32* indices, i32 index_count) {
		Mesh m = {};
		m.vertex_count = vertex_count;
		m.index_count = index_count;

		gl::GenVertexArrays(1, &m.vao);
		gl::BindVertexArray(m.vao);

		gl::GenBuffers(1, &m.vbo);
		gl::BindBuffer(GL_ARRAY_BUFFER, m.vbo);
		gl::BufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(Vertex), vertices, GL_STATIC_DRAW);

		gl::GenBuffers(1, &m.ebo);
		gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
		gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(u32), indices, GL_STATIC_DRAW);

		setup_vertex_attribs();

		gl::BindVertexArray(0);
		return m;
	}

	Mesh create_no_indices(const Vertex* vertices, i32 vertex_count) {
		Mesh m = {};
		m.vertex_count = vertex_count;
		m.index_count = 0;

		gl::GenVertexArrays(1, &m.vao);
		gl::BindVertexArray(m.vao);

		gl::GenBuffers(1, &m.vbo);
		gl::BindBuffer(GL_ARRAY_BUFFER, m.vbo);
		gl::BufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(Vertex), vertices, GL_STATIC_DRAW);

		setup_vertex_attribs();

		gl::BindVertexArray(0);
		return m;
	}

	void destroy(Mesh* m) {
		if (m->ebo) gl::DeleteBuffers(1, &m->ebo);
		if (m->vbo) gl::DeleteBuffers(1, &m->vbo);
		if (m->vao) gl::DeleteVertexArrays(1, &m->vao);
		*m = {};
	}

	void draw(const Mesh& m) {
		gl::BindVertexArray(m.vao);
		if (m.index_count > 0) {
			gl::DrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, nullptr);
		} else {
			gl::DrawArrays(GL_TRIANGLES, 0, m.vertex_count);
		}
		gl::BindVertexArray(0);
	}

	Mesh generate_plane(f32 size, i32 subdivisions) {
		i32 verts_per_side = subdivisions + 1;
		i32 vert_count = verts_per_side * verts_per_side;
		i32 idx_count = subdivisions * subdivisions * 6;

		Vertex* verts = (Vertex*)memory::malloc(vert_count * sizeof(Vertex));
		u32* indices = (u32*)memory::malloc(idx_count * sizeof(u32));

		f32 half = size * 0.5f;
		f32 step = size / (f32)subdivisions;

		for (i32 z = 0; z < verts_per_side; z++) {
			for (i32 x = 0; x < verts_per_side; x++) {
				i32 i = z * verts_per_side + x;
				verts[i].position = { -half + x * step, 0.0f, -half + z * step };
				verts[i].normal = { 0.0f, 1.0f, 0.0f };
				verts[i].texcoord = { (f32)x / (f32)subdivisions, (f32)z / (f32)subdivisions };
			}
		}

		i32 idx = 0;
		for (i32 z = 0; z < subdivisions; z++) {
			for (i32 x = 0; x < subdivisions; x++) {
				u32 tl = z * verts_per_side + x;
				u32 tr = tl + 1;
				u32 bl = (z + 1) * verts_per_side + x;
				u32 br = bl + 1;

				indices[idx++] = tl;
				indices[idx++] = bl;
				indices[idx++] = tr;
				indices[idx++] = tr;
				indices[idx++] = bl;
				indices[idx++] = br;
			}
		}

		Mesh m = create(verts, vert_count, indices, idx_count);
		memory::free(verts);
		memory::free(indices);
		return m;
	}

	Mesh generate_cube(f32 half_extent) {
		f32 h = half_extent;
		Vertex verts[] = {
			// front
			{{ -h, -h,  h }, {  0,  0,  1 }, { 0, 0 }},
			{{  h, -h,  h }, {  0,  0,  1 }, { 1, 0 }},
			{{  h,  h,  h }, {  0,  0,  1 }, { 1, 1 }},
			{{ -h,  h,  h }, {  0,  0,  1 }, { 0, 1 }},
			// back
			{{  h, -h, -h }, {  0,  0, -1 }, { 0, 0 }},
			{{ -h, -h, -h }, {  0,  0, -1 }, { 1, 0 }},
			{{ -h,  h, -h }, {  0,  0, -1 }, { 1, 1 }},
			{{  h,  h, -h }, {  0,  0, -1 }, { 0, 1 }},
			// top
			{{ -h,  h,  h }, {  0,  1,  0 }, { 0, 0 }},
			{{  h,  h,  h }, {  0,  1,  0 }, { 1, 0 }},
			{{  h,  h, -h }, {  0,  1,  0 }, { 1, 1 }},
			{{ -h,  h, -h }, {  0,  1,  0 }, { 0, 1 }},
			// bottom
			{{ -h, -h, -h }, {  0, -1,  0 }, { 0, 0 }},
			{{  h, -h, -h }, {  0, -1,  0 }, { 1, 0 }},
			{{  h, -h,  h }, {  0, -1,  0 }, { 1, 1 }},
			{{ -h, -h,  h }, {  0, -1,  0 }, { 0, 1 }},
			// right
			{{  h, -h,  h }, {  1,  0,  0 }, { 0, 0 }},
			{{  h, -h, -h }, {  1,  0,  0 }, { 1, 0 }},
			{{  h,  h, -h }, {  1,  0,  0 }, { 1, 1 }},
			{{  h,  h,  h }, {  1,  0,  0 }, { 0, 1 }},
			// left
			{{ -h, -h, -h }, { -1,  0,  0 }, { 0, 0 }},
			{{ -h, -h,  h }, { -1,  0,  0 }, { 1, 0 }},
			{{ -h,  h,  h }, { -1,  0,  0 }, { 1, 1 }},
			{{ -h,  h, -h }, { -1,  0,  0 }, { 0, 1 }},
		};

		u32 indices[] = {
			 0, 1, 2,  2, 3, 0,    // front
			 4, 5, 6,  6, 7, 4,    // back
			 8, 9,10, 10,11, 8,    // top
			12,13,14, 14,15,12,    // bottom
			16,17,18, 18,19,16,    // right
			20,21,22, 22,23,20,    // left
		};

		return create(verts, 24, indices, 36);
	}

}
