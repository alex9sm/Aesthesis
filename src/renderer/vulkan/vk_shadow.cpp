#include "vk_pch.hpp"
#include "vk_shadow.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_pipeline.hpp"
#include "vk_globals.hpp"
#include "vk_frame.hpp"
#include "vk_mesh.hpp"
#include "log.hpp"

namespace vk {

	static VkImage       shadow_image  = VK_NULL_HANDLE;
	static VmaAllocation shadow_alloc  = VK_NULL_HANDLE;
	static VkImageView   layer_views[CASCADE_COUNT] = {};
	static VkImageView   array_view    = VK_NULL_HANDLE;
	static VkImageLayout shadow_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	static VkSampler     shadow_sampler = VK_NULL_HANDLE;

	static VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline       pipeline        = VK_NULL_HANDLE;

	struct CascadePC {
		u32 cascade_index;
	};

	// --- creation ---

	static bool create_image_and_views() {
		Context& c = context();
		VmaAllocator a = allocator();

		VkImageCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ci.imageType = VK_IMAGE_TYPE_2D;
		ci.format = VK_FORMAT_D32_SFLOAT;
		ci.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1 };
		ci.mipLevels = 1;
		ci.arrayLayers = CASCADE_COUNT;
		ci.samples = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;

		if (vmaCreateImage(a, &ci, &aci, &shadow_image, &shadow_alloc, nullptr) != VK_SUCCESS) {
			logger::fatal("Failed to create CSM shadow image");
			return false;
		}

		// one single-layer view per cascade
		for (u32 i = 0; i < CASCADE_COUNT; i++) {
			VkImageViewCreateInfo vci = {};
			vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			vci.image = shadow_image;
			vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vci.format = VK_FORMAT_D32_SFLOAT;
			vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			vci.subresourceRange.levelCount = 1;
			vci.subresourceRange.baseArrayLayer = i;
			vci.subresourceRange.layerCount = 1;
			if (vkCreateImageView(c.device, &vci, nullptr, &layer_views[i]) != VK_SUCCESS) {
				logger::fatal("Failed to create CSM cascade layer view %u", i);
				return false;
			}
		}

		// full-array view, bound as sampler2DArrayShadow in the global set.
		VkImageViewCreateInfo avi = {};
		avi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		avi.image = shadow_image;
		avi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		avi.format = VK_FORMAT_D32_SFLOAT;
		avi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		avi.subresourceRange.levelCount = 1;
		avi.subresourceRange.baseArrayLayer = 0;
		avi.subresourceRange.layerCount = CASCADE_COUNT;
		if (vkCreateImageView(c.device, &avi, nullptr, &array_view) != VK_SUCCESS) {
			logger::fatal("Failed to create CSM array view");
			return false;
		}
		return true;
	}

	static bool create_sampler() {
		Context& c = context();
		VkSamplerCreateInfo s = {};
		s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		s.magFilter = VK_FILTER_LINEAR;
		s.minFilter = VK_FILTER_LINEAR;
		s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		s.compareEnable = VK_TRUE;
		s.compareOp = VK_COMPARE_OP_LESS;
		s.maxLod = 1.0f;
		if (vkCreateSampler(c.device, &s, nullptr, &shadow_sampler) != VK_SUCCESS) {
			logger::fatal("Failed to create CSM comparison sampler");
			return false;
		}
		return true;
	}

	static void write_descriptor() {
		Context& c = context();

		VkDescriptorImageInfo info = {};
		info.sampler = shadow_sampler;
		info.imageView = array_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		for (u32 fi = 0; fi < FRAMES_IN_FLIGHT; fi++) {
			VkWriteDescriptorSet w = {};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = global_set_for_frame(fi);
			w.dstBinding = 8;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			w.pImageInfo = &info;
			vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);
		}
	}

	static bool create_pipeline() {
		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = sizeof(vec3);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attr = {};
		attr.location = 0; attr.binding = 0; attr.format = VK_FORMAT_R32G32B32_SFLOAT; attr.offset = 0;

		VkDescriptorSetLayout set_layouts[] = { global_set_layout() };

		VkPushConstantRange pc = {};
		pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pc.offset = 0;
		pc.size = sizeof(CascadePC);

		// depth-only, no fragment stage.
		GraphicsPipelineSpec spec = {};
		spec.vs_path = "shaders/spv/shadow_depth.vert.spv";
		spec.vertex_bindings = &binding;
		spec.vertex_binding_count = 1;
		spec.vertex_attrs = &attr;
		spec.vertex_attr_count = 1;
		spec.cull = VK_CULL_MODE_BACK_BIT;
		spec.depth_test = VK_TRUE;
		spec.depth_write = VK_TRUE;
		spec.depth_compare = VK_COMPARE_OP_LESS;
		spec.depth_format = VK_FORMAT_D32_SFLOAT;
		spec.depthBiasEnable = VK_TRUE;
		spec.depth_bias_constant = 1.25f;
		spec.depth_bias_slope = 1.75f;
		spec.set_layouts = set_layouts;
		spec.set_layout_count = 1;
		spec.push_constant = &pc;

		return create_graphics_pipeline(spec, &pipeline, &pipeline_layout);
	}

	bool init_shadow() {
		if (!create_image_and_views()) return false;
		if (!create_sampler()) return false;
		if (!create_pipeline()) return false;
		write_descriptor();
		return true;
	}

	void shutdown_shadow() {
		Context& c = context();
		if (pipeline)        vkDestroyPipeline(c.device, pipeline, nullptr);
		if (pipeline_layout) vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
		if (shadow_sampler)  vkDestroySampler(c.device, shadow_sampler, nullptr);
		if (array_view)      vkDestroyImageView(c.device, array_view, nullptr);
		for (u32 i = 0; i < CASCADE_COUNT; i++) {
			if (layer_views[i]) vkDestroyImageView(c.device, layer_views[i], nullptr);
			layer_views[i] = VK_NULL_HANDLE;
		}
		if (shadow_image) vmaDestroyImage(allocator(), shadow_image, shadow_alloc);
		pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
		shadow_sampler = VK_NULL_HANDLE;
		array_view = VK_NULL_HANDLE;
		shadow_image = VK_NULL_HANDLE;
		shadow_alloc = VK_NULL_HANDLE;
		shadow_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	// --- barrier ---

	static void barrier(VkCommandBuffer cmd, VkImageLayout from, VkImageLayout to,
		VkAccessFlags src_access, VkAccessFlags dst_access,
		VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
	{
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = shadow_image;
		b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		b.subresourceRange.levelCount = 1;
		b.subresourceRange.baseArrayLayer = 0;
		b.subresourceRange.layerCount = CASCADE_COUNT;
		b.oldLayout = from;
		b.newLayout = to;
		b.srcAccessMask = src_access;
		b.dstAccessMask = dst_access;
		vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
	}

	// --- cascade fitting ---

	static mat4 reproject_z_range(const mat4& proj, f32 near_z, f32 far_z) {
		mat4 m = proj;
		m.col[2][2] = far_z / (near_z - far_z);
		m.col[3][2] = (near_z * far_z) / (near_z - far_z);
		return m;
	}

	static vec3 unproject(const mat4& m, f32 x, f32 y, f32 z) {
		f32 rx = m.col[0][0]*x + m.col[1][0]*y + m.col[2][0]*z + m.col[3][0];
		f32 ry = m.col[0][1]*x + m.col[1][1]*y + m.col[2][1]*z + m.col[3][1];
		f32 rz = m.col[0][2]*x + m.col[1][2]*y + m.col[2][2]*z + m.col[3][2];
		f32 rw = m.col[0][3]*x + m.col[1][3]*y + m.col[2][3]*z + m.col[3][3];
		f32 inv_w = (rw != 0.0f) ? 1.0f / rw : 1.0f;
		return { rx * inv_w, ry * inv_w, rz * inv_w };
	}

	static constexpr f32 SPLIT_LAMBDA    = 0.2f;  // blend of log/uniform splits
	static constexpr f32 SHADOW_MAX_DIST = 20.0f; // world units; cascades stop here instead of at the camera far plane
	static constexpr f32 CASCADE_PAD_XY = 5.0f;  // world units; guards edge casters
	static constexpr f32 CASCADE_PAD_Z  = 20.0f; // world units; guards casters just outside the fitted depth range

	void compute_cascades(const mat4& camera_view, const mat4& camera_proj,
		vec3 sun_dir, mat4 out_view_proj[CASCADE_COUNT], vec4& out_splits)
	{
		f32 near_z, far_z;
		mat4_extract_perspective_vk(camera_proj, &near_z, &far_z);
		
		if (far_z > SHADOW_MAX_DIST) far_z = SHADOW_MAX_DIST;

		f32 splits[CASCADE_COUNT];
		for (u32 i = 0; i < CASCADE_COUNT; i++) {
			f32 t = (f32)(i + 1) / (f32)CASCADE_COUNT;
			f32 log_split     = near_z * math::pow(far_z / near_z, t);
			f32 uniform_split = near_z + (far_z - near_z) * t;
			splits[i] = SPLIT_LAMBDA * log_split + (1.0f - SPLIT_LAMBDA) * uniform_split;
		}
		out_splits = { splits[0], splits[1], splits[2], 0.0f };

		vec3 light_dir = normalize(sun_dir);
		vec3 up = { 0.0f, 1.0f, 0.0f };
		if (math::abs(dot(light_dir, up)) > 0.99f) up = { 0.0f, 0.0f, 1.0f };

		f32 sx_vals[2] = { -1.0f, 1.0f };
		f32 sy_vals[2] = { -1.0f, 1.0f };
		f32 sz_vals[2] = { 0.0f, 1.0f };

		f32 split_near = near_z;
		for (u32 i = 0; i < CASCADE_COUNT; i++) {
			f32 split_far = splits[i];

			mat4 slice_proj = reproject_z_range(camera_proj, split_near, split_far);
			mat4 inv_slice_vp = mat4_inverse(slice_proj * camera_view);

			vec3 corners[8];
			u32 c = 0;
			for (u32 xi = 0; xi < 2; xi++)
				for (u32 yi = 0; yi < 2; yi++)
					for (u32 zi = 0; zi < 2; zi++)
						corners[c++] = unproject(inv_slice_vp, sx_vals[xi], sy_vals[yi], sz_vals[zi]);

			vec3 center = { 0.0f, 0.0f, 0.0f };
			for (u32 k = 0; k < 8; k++) center += corners[k];
			center = center * (1.0f / 8.0f);
			f32 radius = 0.0f;
			for (u32 k = 0; k < 8; k++) {
				f32 d = length(corners[k] - center);
				if (d > radius) radius = d;
			}

			vec3 light_pos = center + light_dir * (radius + CASCADE_PAD_Z);
			mat4 light_view = mat4_look_at(light_pos, center, up);

			vec3 lmin = mat4_transform_point(light_view, corners[0]);
			vec3 lmax = lmin;
			for (u32 k = 1; k < 8; k++) {
				vec3 p = mat4_transform_point(light_view, corners[k]);
				if (p.x < lmin.x) lmin.x = p.x; if (p.x > lmax.x) lmax.x = p.x;
				if (p.y < lmin.y) lmin.y = p.y; if (p.y > lmax.y) lmax.y = p.y;
				if (p.z < lmin.z) lmin.z = p.z; if (p.z > lmax.z) lmax.z = p.z;
			}

			// light-space z is negative in front of the eye (mat4_look_at's
			// convention); near/far distances are positive.
			f32 near_dist = -lmax.z - CASCADE_PAD_Z;
			f32 far_dist  = -lmin.z + CASCADE_PAD_Z;
			if (near_dist < 0.01f) near_dist = 0.01f;

			mat4 light_proj = mat4_ortho_vk(
				lmin.x - CASCADE_PAD_XY, lmax.x + CASCADE_PAD_XY,
				lmin.y - CASCADE_PAD_XY, lmax.y + CASCADE_PAD_XY,
				near_dist, far_dist);

			out_view_proj[i] = light_proj * light_view;
			split_near = split_far;
		}
	}

	// --- execution ---

	void execute_shadow_pass(VkCommandBuffer cmd, const DrawBatch* batches, u32 batch_count) {
		bool first_use = (shadow_layout == VK_IMAGE_LAYOUT_UNDEFINED);
		barrier(cmd, shadow_layout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			first_use ? 0 : VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			first_use ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
		shadow_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkDescriptorSet global_set = current_global_set();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
			0, 1, &global_set, 0, nullptr);

		// Y-flipped, matching depth_prepass — keeps triangle winding (and thus
		// back-face culling) consistent with the rest of the renderer.
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = (f32)SHADOW_MAP_SIZE;
		viewport.width = (f32)SHADOW_MAP_SIZE;
		viewport.height = -(f32)SHADOW_MAP_SIZE;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };

		for (u32 cascade = 0; cascade < CASCADE_COUNT; cascade++) {
			VkRenderingAttachmentInfo depth_attachment = {};
			depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depth_attachment.imageView = layer_views[cascade];
			depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depth_attachment.clearValue.depthStencil = { 1.0f, 0 };

			VkRenderingInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			info.renderArea.offset = { 0, 0 };
			info.renderArea.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
			info.layerCount = 1;
			info.pDepthAttachment = &depth_attachment;

			vkCmdBeginRendering(cmd, &info);
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			CascadePC pc = { cascade };
			vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CascadePC), &pc);

			for (u32 b = 0; b < batch_count; b++) {
				const DrawBatch& batch = batches[b];
				const MeshGPU* m = get_mesh(batch.mesh);
				if (!m) continue;

				VkDeviceSize offset = 0;
				vkCmdBindVertexBuffers(cmd, 0, 1, &m->position_buffer, &offset);
				vkCmdBindIndexBuffer(cmd, m->index_buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(cmd, m->index_count, batch.instance_count, 0, 0, batch.first_instance);
			}

			vkCmdEndRendering(cmd);
		}

		// hand off to the lighting pass, which samples this same frame.
		barrier(cmd, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		shadow_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

}
