#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer {

	using MeshHandle = u32;
	static constexpr MeshHandle INVALID_MESH = 0;

	bool init();
	void shutdown();

	// resource management
	MeshHandle load_mesh(const char* path);
	void unload_mesh(MeshHandle handle);

	// frame
	void begin_frame(const mat4& view, const mat4& projection);
	void submit_mesh(MeshHandle mesh, const mat4& model, vec4 color);
	void end_frame();

}
