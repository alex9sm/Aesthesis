#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "vk_mesh.hpp"

namespace vk {

	// one batched draw: a run of consecutive instance-SSBO entries that all
	// reference the same mesh. emitted as one vkCmdDrawIndexed call.
	struct DrawBatch {
		MeshHandle mesh;
		u32        first_instance;
		u32        instance_count;
	};

	bool init_gbuffer();
	void shutdown_gbuffer();

	// `batches` describes the post-sort run-length encoding of the draw queue.
	// caller must have written the corresponding instance-SSBO entries in
	// the order implied by the batches (i.e. firstInstance + i indexes a
	// matching InstanceData slot).
	void execute_gbuffer_pass(VkCommandBuffer cmd,
		const DrawBatch* batches, u32 batch_count);

}
