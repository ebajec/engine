#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "def_vulkan.h"
#include "vk_mem_alloc.h"

#include "utils/asset_table.h"
#include "utils/pool.h"
#include "utils/gpu_table.h"
#include "utils/thread_pool.h"

#include "backends/vulkan/resource_impl.h"
#include "backends/vulkan/render_impl.h"
#include "backends/vulkan/pipeline_impl.h"
#include "backends/vulkan/upload_pool.h"

#include <glm/mat4x4.hpp>

#define EV2_MAX_FRAMES_IN_FLIGHT 3
#define EV2_FRAME_TIMEOUT 1e9

struct VulkanOptions
{
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

struct SwapChain
{
	VkSwapchainKHR              swapchain;
    VkFormat                    image_format;
    VkExtent2D                  extent;
    std::vector<VkImage>        images;
    std::vector<VkImageView>    image_views;
    std::vector<VkFramebuffer>  framebuffers;
};

struct QueueSubmitter
{
	std::mutex 	sync;

	uint64_t 	submit_counter;
	VkSemaphore semaphore;

	VkQueue 	queue;

	VkResult submit(uint32_t count, const VkSubmitInfo2 *info, VkFence fence) {
		std::unique_lock<std::mutex> lock(sync);
		++submit_counter;
		return vkQueueSubmit2(queue, count, info, fence);
	}
};

struct QueueFamily
{
	uint32_t 							index;
	uint32_t 							queue_count;
	std::unique_ptr<QueueSubmitter[]> 	queues;
};

struct GPUFramedata
{
	uint32_t t_seconds;
	float t_fract;
	float dt;
};

struct FrameContext
{
	double t; 
	double dt;
	uint32_t w, h;
	ev2::BufferID ubo;

	// timeline semaphore value for completion of this
	// frame's work
	uint64_t wait_value;

	// count = num workers
	std::vector<VkCommandPool> command_pools;

	VkCommandPool get_worker_pool(uint32_t worker_idx) {
		assert(worker_idx + 1 < command_pools.size());
		return command_pools[1 + worker_idx];
	}

	VkCommandPool get_main_pool() {
		return command_pools[0];
	}
};
namespace ev2 {

struct GfxContext
{
	//-----------------------------------------------------------------------------
	// Vulkan data 

    VkInstance                  instance;
    VkDebugUtilsMessengerEXT    debugMessenger;

    VkPhysicalDevice            physicalDevice = VK_NULL_HANDLE;
    VkDevice                    device;

	std::vector<QueueFamily> 	queue_families;

	const QueueFamily * 		graphics_family;
	const QueueFamily * 		transfer_family;
	const QueueFamily * 		compute_family;

    VkQueue                     presentQueue;

    VkSurfaceKHR                surface;   

	VmaAllocator 				allocator;

	SwapChain 					swapChain;

	//-----------------------------------------------------------------------------
	// Important things 
	
	// set on initialization
	std::thread::id main_thread_id;
	
	uint32_t max_frames_in_flight;
	uint32_t max_workers;

	uint64_t start_time_ns;
	uint64_t current_frame_index;

	std::unique_ptr<ThreadPool> worker_pool;

	//------------------------------------------------------------------------------
	// Per-frame data
	
	FrameContext frames[EV2_MAX_FRAMES_IN_FLIGHT];
	
	//-----------------------------------------------------------------------------
	// Resource Pools 

	std::unique_ptr<UploadPool, void(*)(UploadPool*)> pool = {
		nullptr, UploadPool::destroy
	};

	// Assets
	std::unique_ptr<AssetTable, void(*)(AssetTable*)> assets = {
		nullptr, AssetTable::destroy
	};

	// Resource pools
	std::unique_ptr<ResourcePool<Buffer>> buffer_pool;
	std::unique_ptr<ResourcePool<Image>> image_pool;
	std::unique_ptr<ResourcePool<Texture>> texture_pool;

	// Special GPU Resources
	GPUTTable<ViewData> view_data;
	GPUTTable<glm::mat4> transforms;

	// defaults to identity matrix
	ViewID default_view;

	VkCommandPool get_command_pool(
		uint32_t frame, 
		uint32_t worker,
		uint32_t queue = 0
	);

	// @brief Get a command pool for the current thread + frame
	// for the selected queue family.
	VkCommandPool get_current_frame_command_pool(
		uint32_t queue_famlily_index = 0
	);

	ev2::Result wait_for_frame_completion(FrameContext *frame);

	inline FrameContext *get_current_frame() {
		return &frames[current_frame_index % max_frames_in_flight];
	}
	inline FrameContext *get_previous_frame() {
		uint64_t idx = current_frame_index ? current_frame_index - 1 : 0;
		return &frames[idx % max_frames_in_flight];
	}

	inline VkSemaphore get_graphics_timeline_semaphore() {
		return graphics_family->queues[0].semaphore;
	}

	// convenience (emplace)
	inline BufferID emplace_buffer(Buffer &&buf) {
		ResourceID rid = buffer_pool->allocate(std::move(buf));
		return EV2_HANDLE_CAST(Buffer, rid.u64);
	}
	inline ImageID emplace_image(Image &&img) {
		ResourceID rid = image_pool->allocate(std::move(img));
		return EV2_HANDLE_CAST(Image, rid.u64);
	}
	inline TextureID emplace_texture(Texture &&tex) {
		ResourceID rid = texture_pool->allocate(std::move(tex));
		return EV2_HANDLE_CAST(Texture, rid.u64);
	}

	// convenience (get)

	inline Buffer *get_buffer(BufferID h) {
		ResourceID rid = {.u64 = h.id};
		return buffer_pool->get(rid);
	}
	inline Image *get_image(ImageID h) {
		ResourceID rid = {.u64 = h.id};
		return image_pool->get(rid);
	}
	inline Texture *get_texture(TextureID h) {
		ResourceID rid = {.u64 = h.id};
		return texture_pool->get(rid);
	}

	inline GfxPipeline *get_gfx_pipeline(GfxPipelineID h) {
		AssetID id = static_cast<uint32_t>(h.id);
		AssetEntry *ent = assets->get_entry(id);
		return (GfxPipeline*)ent->usr;
	}
	inline ComputePipeline *get_compute_pipeline(ComputePipelineID h) {
		AssetID id = static_cast<uint32_t>(h.id);
		AssetEntry *ent = assets->get_entry(id);
		return (ComputePipeline*)ent->usr;
	}
	inline Shader *get_shader(ShaderID h) {
		AssetID id = static_cast<uint32_t>(h.id);
		AssetEntry *ent = assets->get_entry(id);
		return (Shader*)ent->usr;
	}
};

};

#endif //DEVICE_
