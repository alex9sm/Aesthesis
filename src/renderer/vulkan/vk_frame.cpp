#include "vk_frame.hpp"
#include "vk_init.hpp"
#include "vk_swapchain.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	struct PerFrame {
		VkCommandPool cmd_pool;
		VkCommandBuffer cmd_buffer;
		VkSemaphore image_available;  // signaled by acquire, waited by submit
		VkFence in_flight;            // signaled by submit, waited at start of next reuse
	};

	struct PerImage {
		VkSemaphore render_finished;  // signaled by submit, waited by present
	};

	static PerFrame frames[FRAMES_IN_FLIGHT] = {};
	static PerImage per_image[MAX_SWAPCHAIN_IMAGES] = {};
	static u32 current_frame = 0;
	static u32 current_image = 0;

	VkCommandBuffer current_cmd() { return frames[current_frame].cmd_buffer; }
	u32 current_swapchain_image() { return current_image; }

	// --- creation ---

	static bool create_frames() {
		Context& c = context();

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkCommandPoolCreateInfo pool_info = {};
			pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			pool_info.queueFamilyIndex = c.graphics_queue_index;
			if (vkCreateCommandPool(c.device, &pool_info, nullptr, &frames[i].cmd_pool) != VK_SUCCESS) {
				logger::fatal("Failed to create per-frame command pool");
				return false;
			}

			VkCommandBufferAllocateInfo alloc_info = {};
			alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			alloc_info.commandPool = frames[i].cmd_pool;
			alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			alloc_info.commandBufferCount = 1;
			if (vkAllocateCommandBuffers(c.device, &alloc_info, &frames[i].cmd_buffer) != VK_SUCCESS) {
				logger::fatal("Failed to allocate per-frame command buffer");
				return false;
			}

			VkSemaphoreCreateInfo sem_info = {};
			sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			if (vkCreateSemaphore(c.device, &sem_info, nullptr, &frames[i].image_available) != VK_SUCCESS) {
				logger::fatal("Failed to create image_available semaphore");
				return false;
			}

			VkFenceCreateInfo fence_info = {};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // first wait succeeds immediately
			if (vkCreateFence(c.device, &fence_info, nullptr, &frames[i].in_flight) != VK_SUCCESS) {
				logger::fatal("Failed to create in_flight fence");
				return false;
			}
		}
		return true;
	}

	static bool create_image_semaphores() {
		Context& c = context();

		// create MAX_SWAPCHAIN_IMAGES regardless of current count,
		// so swapchain recreation never needs to allocate more
		for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
			VkSemaphoreCreateInfo sem_info = {};
			sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			if (vkCreateSemaphore(c.device, &sem_info, nullptr, &per_image[i].render_finished) != VK_SUCCESS) {
				logger::fatal("Failed to create render_finished semaphore");
				return false;
			}
		}
		return true;
	}

	bool init_frames() {
		if (!create_frames()) return false;
		if (!create_image_semaphores()) return false;
		current_frame = 0;
		current_image = 0;
		return true;
	}

	void shutdown_frames() {
		Context& c = context();
		if (c.device) vkDeviceWaitIdle(c.device);

		for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
			if (per_image[i].render_finished) {
				vkDestroySemaphore(c.device, per_image[i].render_finished, nullptr);
			}
		}
		memory::set(per_image, 0, sizeof(per_image));

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			if (frames[i].in_flight)       vkDestroyFence(c.device, frames[i].in_flight, nullptr);
			if (frames[i].image_available) vkDestroySemaphore(c.device, frames[i].image_available, nullptr);
			if (frames[i].cmd_pool)        vkDestroyCommandPool(c.device, frames[i].cmd_pool, nullptr);
		}
		memory::set(frames, 0, sizeof(frames));
	}

	// --- per-frame ---

	bool begin_frame() {
		Context& c = context();
		Swapchain& sc = swapchain();
		PerFrame& f = frames[current_frame];

		// wait until this frame slot's previous submission has finished
		vkWaitForFences(c.device, 1, &f.in_flight, VK_TRUE, UINT64_MAX);

		VkResult result = vkAcquireNextImageKHR(c.device, sc.handle, UINT64_MAX,
			f.image_available, VK_NULL_HANDLE, &current_image);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreate_swapchain();
			return false;
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			logger::error("vkAcquireNextImageKHR failed: %d", (int)result);
			return false;
		}

		// only reset fence after a successful acquire so we don't deadlock on retry
		vkResetFences(c.device, 1, &f.in_flight);
		vkResetCommandPool(c.device, f.cmd_pool, 0);

		VkCommandBufferBeginInfo begin = {};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(f.cmd_buffer, &begin);

		return true;
	}

	bool end_frame() {
		Context& c = context();
		Swapchain& sc = swapchain();
		PerFrame& f = frames[current_frame];

		vkEndCommandBuffer(f.cmd_buffer);

		VkSemaphore wait_sem = f.image_available;
		VkSemaphore signal_sem = per_image[current_image].render_finished;
		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &wait_sem;
		submit.pWaitDstStageMask = &wait_stage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &f.cmd_buffer;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &signal_sem;

		VkResult result = vkQueueSubmit(c.graphics_queue, 1, &submit, f.in_flight);
		if (result != VK_SUCCESS) {
			logger::error("vkQueueSubmit failed: %d", (int)result);
			return false;
		}

		VkPresentInfoKHR present = {};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &signal_sem;
		present.swapchainCount = 1;
		present.pSwapchains = &sc.handle;
		present.pImageIndices = &current_image;

		result = vkQueuePresentKHR(c.present_queue, &present);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			recreate_swapchain();
		} else if (result != VK_SUCCESS) {
			logger::error("vkQueuePresentKHR failed: %d", (int)result);
			return false;
		}

		current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT;
		return true;
	}

}
