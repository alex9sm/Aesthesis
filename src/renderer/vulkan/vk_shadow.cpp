#include "vk_pch.hpp"
#include "shared.glsl"
#include "vk_shadow.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_pipeline.hpp"
#include "vk_globals.hpp"
#include "vk_frame.hpp"
#include "vk_mesh.hpp"
#include "vk_targets.hpp"
#include "log.hpp"

namespace vk {
	static RenderImage   shadow_img = {};
	static RenderImage   spot_atlas = {};
	static VkImageView   layer_views[CASCADE_COUNT] = {};
	static VkSampler     shadow_sampler = VK_NULL_HANDLE;

	static VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline       pipeline        = VK_NULL_HANDLE;

	static constexpr u32 SPOT_ATLAS_W = SPOT_ATLAS_COLS * SPOT_TILE_SIZE;
	static constexpr u32 SPOT_ATLAS_H = (SPOT_SHADOW_SLOTS / SPOT_ATLAS_COLS) * SPOT_TILE_SIZE;
	static_assert(SPOT_SHADOW_SLOTS % SPOT_ATLAS_COLS == 0, "spot atlas rows must be full");

	// --- creation ---

	static bool create_depth_image(RenderImage& img, u32 w, u32 h, u32 layers) {
		VkImageCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ci.imageType = VK_IMAGE_TYPE_2D;
		ci.format = VK_FORMAT_D32_SFLOAT;
		ci.extent = { w, h, 1 };
		ci.mipLevels = 1;
		ci.arrayLayers = layers;
		ci.samples = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;

		if (vmaCreateImage(allocator(), &ci, &aci, &img.image, &img.alloc, nullptr) != VK_SUCCESS) {
			logger::fatal("Failed to create %ux%u shadow depth image", w, h);
			return false;
		}
		img.format = VK_FORMAT_D32_SFLOAT;
		img.state  = ResState::Undefined;
		img.layers = layers;
		return true;
	}

	static bool create_image_and_views() {
		Context& c = context();
		if (!create_depth_image(shadow_img, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, CASCADE_COUNT)) return false;

		// one single-layer view per cascade
		for (u32 i = 0; i < CASCADE_COUNT; i++) {
			VkImageViewCreateInfo vci = {};
			vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			vci.image = shadow_img.image;
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
		avi.image = shadow_img.image;
		avi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		avi.format = VK_FORMAT_D32_SFLOAT;
		avi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		avi.subresourceRange.levelCount = 1;
		avi.subresourceRange.baseArrayLayer = 0;
		avi.subresourceRange.layerCount = CASCADE_COUNT;
		if (vkCreateImageView(c.device, &avi, nullptr, &shadow_img.view) != VK_SUCCESS) {
			logger::fatal("Failed to create CSM array view");
			return false;
		}

		if (!create_depth_image(spot_atlas, SPOT_ATLAS_W, SPOT_ATLAS_H, 1)) return false;
		VkImageViewCreateInfo svi = {};
		svi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		svi.image = spot_atlas.image;
		svi.viewType = VK_IMAGE_VIEW_TYPE_2D;
		svi.format = VK_FORMAT_D32_SFLOAT;
		svi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		svi.subresourceRange.levelCount = 1;
		svi.subresourceRange.layerCount = 1;
		if (vkCreateImageView(c.device, &svi, nullptr, &spot_atlas.view) != VK_SUCCESS) {
			logger::fatal("Failed to create spot shadow atlas view");
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

		VkDescriptorImageInfo infos[2] = {};
		infos[0] = { shadow_sampler, shadow_img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		infos[1] = { shadow_sampler, spot_atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		u32 bindings[2] = { BIND_SHADOW, BIND_SPOT_SHADOW };

		for (u32 fi = 0; fi < FRAMES_IN_FLIGHT; fi++) {
			VkWriteDescriptorSet w[2] = {};
			for (u32 i = 0; i < 2; i++) {
				w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w[i].dstSet = global_set_for_frame(fi);
				w[i].dstBinding = bindings[i];
				w[i].descriptorCount = 1;
				w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				w[i].pImageInfo = &infos[i];
			}
			vkUpdateDescriptorSets(c.device, 2, w, 0, nullptr);
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
		pc.size = sizeof(mat4);

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
		if (shadow_sampler)   vkDestroySampler(c.device, shadow_sampler, nullptr);
		if (shadow_img.view)  vkDestroyImageView(c.device, shadow_img.view, nullptr);
		for (u32 i = 0; i < CASCADE_COUNT; i++) {
			if (layer_views[i]) vkDestroyImageView(c.device, layer_views[i], nullptr);
			layer_views[i] = VK_NULL_HANDLE;
		}
		if (shadow_img.image) vmaDestroyImage(allocator(), shadow_img.image, shadow_img.alloc);
		if (spot_atlas.view)  vkDestroyImageView(c.device, spot_atlas.view, nullptr);
		if (spot_atlas.image) vmaDestroyImage(allocator(), spot_atlas.image, spot_atlas.alloc);
		pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
		shadow_sampler = VK_NULL_HANDLE;
		shadow_img = {};
		spot_atlas = {};
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

	static constexpr f32 SPLIT_LAMBDA    = 0.3f;  // blend of log/uniform splits
	static constexpr f32 SHADOW_MAX_DIST = 40.0f; // world units; cascades stop here instead of at the camera far plane
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

		mat4 light_basis     = mat4_look_at({ 0.0f, 0.0f, 0.0f }, -light_dir, up);
		mat4 light_basis_inv = mat4_inverse(light_basis);

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

			radius = math::ceil(radius * 16.0f) * (1.0f / 16.0f);
			f32  texel = (2.0f * radius) / (f32)SHADOW_MAP_SIZE;
			vec3 c_ls  = mat4_transform_point(light_basis, center);
			c_ls.x = math::floor(c_ls.x / texel) * texel;
			c_ls.y = math::floor(c_ls.y / texel) * texel;
			vec3 snapped_center = mat4_transform_point(light_basis_inv, c_ls);
			vec3 light_pos  = snapped_center + light_dir * (radius + CASCADE_PAD_Z);
			mat4 light_view = mat4_look_at(light_pos, snapped_center, up);
			mat4 light_proj = mat4_ortho_vk(
				-radius, radius,
				-radius, radius,
				0.01f, 2.0f * radius + CASCADE_PAD_Z);

			out_view_proj[i] = light_proj * light_view;
			split_near = split_far;
		}
	}

	// --- execution ---

	static void bind_pipeline(VkCommandBuffer cmd) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		VkDescriptorSet global_set = current_global_set();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
			0, 1, &global_set, 0, nullptr);
	}

	static void begin_depth_rendering(VkCommandBuffer cmd, VkImageView view, u32 w, u32 h) {
		VkRenderingAttachmentInfo depth_attachment = {};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil = { 1.0f, 0 };

		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.extent = { w, h };
		info.layerCount = 1;
		info.pDepthAttachment = &depth_attachment;
		vkCmdBeginRendering(cmd, &info);
	}

	// Y-flipped viewport over the square tile at (x, y), matching the camera passes' winding
	static void draw_view(VkCommandBuffer cmd, const ShadowView& v, u32 x, u32 y, u32 size) {
		VkViewport viewport = {};
		viewport.x = (f32)x;
		viewport.y = (f32)(y + size);
		viewport.width = (f32)size;
		viewport.height = -(f32)size;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor = { { (int32_t)x, (int32_t)y }, { size, size } };
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &v.view_proj);

		for (u32 b = 0; b < v.count; b++) {
			const DrawBatch& batch = v.batches[b];
			const MeshGPU* m = get_mesh(batch.mesh);
			if (!m) continue;

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &m->position_buffer, &offset);
			vkCmdBindIndexBuffer(cmd, m->index_buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, m->index_count, batch.instance_count, 0, 0, batch.first_instance);
		}
	}

	void execute_shadow_pass(VkCommandBuffer cmd, const ShadowView* cascades) {
		if (!cascades) {
			if (shadow_img.state != ResState::ShaderRead) transition(cmd, shadow_img, ResState::ShaderRead);
			return;
		}
		transition(cmd, shadow_img, ResState::DepthWrite);
		bind_pipeline(cmd);

		for (u32 cascade = 0; cascade < CASCADE_COUNT; cascade++) {
			begin_depth_rendering(cmd, layer_views[cascade], SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
			draw_view(cmd, cascades[cascade], 0, 0, SHADOW_MAP_SIZE);
			vkCmdEndRendering(cmd);
		}

		// hand off to the lighting pass, which samples this same frame.
		transition(cmd, shadow_img, ResState::ShaderRead);
	}

	void execute_spot_shadow_pass(VkCommandBuffer cmd, const ShadowView* spots, u32 count) {
		// lighting only samples occupied slots, but the binding's layout must always be valid
		if (count == 0) {
			if (spot_atlas.state != ResState::ShaderRead) transition(cmd, spot_atlas, ResState::ShaderRead);
			return;
		}
		transition(cmd, spot_atlas, ResState::DepthWrite);
		bind_pipeline(cmd);
		begin_depth_rendering(cmd, spot_atlas.view, SPOT_ATLAS_W, SPOT_ATLAS_H);
		for (u32 i = 0; i < count; i++) {
			draw_view(cmd, spots[i], (i % SPOT_ATLAS_COLS) * SPOT_TILE_SIZE,
				(i / SPOT_ATLAS_COLS) * SPOT_TILE_SIZE, SPOT_TILE_SIZE);
		}
		vkCmdEndRendering(cmd);
		transition(cmd, spot_atlas, ResState::ShaderRead);
	}

}
