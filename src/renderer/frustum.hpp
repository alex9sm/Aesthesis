#pragma once

#include "math.hpp"
#include "api.hpp"   // MeshHandle

namespace renderer {

	// Build a world-space view frustum from the active camera matrices.
	// Combines proj * view internally; pass them as supplied to begin_frame.
	Frustum build_frustum(const mat4& view, const mat4& projection);

	// Returns true when the mesh's local AABB, transformed by `model`,
	// intersects the frustum (i.e. the draw should be kept). Returns false
	// to cull. An unknown mesh handle is treated as culled.
	bool cull_test(const Frustum& f, MeshHandle mesh, const mat4& model);

}
