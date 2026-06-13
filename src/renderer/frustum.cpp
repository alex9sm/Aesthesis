#include "vk_pch.hpp"

#include "frustum.hpp"
#include "vk_mesh.hpp"

namespace renderer {

	Frustum build_frustum(const mat4& view, const mat4& projection) {
		return frustum_from_vp(projection * view);
	}

	bool cull_test(const Frustum& f, MeshHandle mesh, const mat4& model) {
		const vk::MeshGPU* m = vk::get_mesh(mesh);
		if (!m) return false;
		AABB world = aabb_transform(m->local_aabb, model);
		return frustum_test_aabb(f, world);
	}

}
