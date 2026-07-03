#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "def_vulkan.h"
#include "vk_mem_alloc.h"

#include "utils/asset_table.h"
#include "utils/pool.h"
#include "utils/gpu_table.h"
#include "utils/thread_pool.h"

#include "backends/vulkan/resource_impl.h"
#include "backends/vulkan/pipeline_impl.h"
#include "backends/vulkan/upload_pool.h"

#include <glm/mat4x4.hpp>

#define EV2_MAX_FRAMES_IN_FLIGHT 3
#define EV2_FRAME_TIMEOUT 1e9

namespace ev2 {

//------------------------------------------------------------------------------
// Misc Vulkan support

struct VulkanOptions
{
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME,
    };
};

struct SwapChain
{
	VkSwapchainKHR              swapchain;
    VkFormat                    image_format;
    VkExtent2D                  extent;
    std::vector<VkImage>        images;
    std::vector<VkImageView>    image_views;
};

struct QueueSubmitter
{
	std::mutex 	sync;

	VkQueue 	queue;

	VkResult submit(uint32_t count, const VkSubmitInfo2 *info, VkFence fence) {
		std::unique_lock<std::mutex> lock(sync);
		return vkQueueSubmit2(queue, count, info, fence);
	}
};

struct QueueFamily
{
	uint32_t 							index;
	uint32_t 							queue_count;
	std::unique_ptr<QueueSubmitter[]> 	queues;
};

//------------------------------------------------------------------------------
// Frame context

struct RenderGraph;

struct SyncKey {
	TaggedResource resource;
	VkQueue queue;

	constexpr bool operator == (const SyncKey &other) const {
		return resource == other.resource && queue == other.queue;
	}

	struct Hash {
        size_t operator()(const SyncKey& k) const noexcept {
			size_t h1 = std::hash<uint64_t>{}(k.resource);
			size_t h2 = std::hash<VkQueue>{}(k.queue);

			return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };
};

struct FrameContext
{
	ev2::GfxContext *ctx;

	uint64_t index;

	double t; 
	double dt;
	uint32_t w, h;
	ev2::BufferID ubo;

	std::vector<ev2::Pass*> passes;
	std::vector<ev2::Pass> new_passes;

	// count = 1 + num workers
	std::vector<VkCommandPool> command_pools;

	VkSemaphore image_available_sempahore;
	VkSemaphore render_finished_semaphore;

	uint64_t swap_image_use_count;
	uint32_t swap_chain_image_index;

	VkDescriptorPool descriptor_pool;

	VkDescriptorSet descriptor_set;

	// render target for current swapchain image
	const ev2::RenderTarget *screen_target;

	std::unique_ptr<RenderGraph> render_graph;

	std::unordered_map<SyncKey, ResourceSync, SyncKey::Hash>
		sync_map;

	ResourceSync *get_resource_sync(TaggedResource resource, VkQueue queue);

	void add_pass(ev2::Pass *pass) {
		passes.emplace_back(pass);
	}
};

struct GPUFramedata
{
	uint32_t t_seconds;
	float t_fract;
	float dt;
};

//------------------------------------------------------------------------------
// Render graph

struct PassBarrier 
{
	ResourceStateFlags src_state;
	ResourceStateFlags dst_state;
	TaggedResource resource;

	// write flag for destination
	bool is_write : 1;
};

static inline uint64_t hash_combine(uint64_t a, uint64_t b) {
    return a ^ (b * 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
}

struct PassEdge
{
	uint32_t src_pass;
	uint32_t dst_pass;

	PassBarrier barrier;

	struct Key
	{
		uint32_t src_pass;
		uint32_t dst_pass;
		uint64_t resource;
	};

	constexpr bool operator == (const PassEdge &other) {
		return 
			barrier.resource == other.barrier.resource && 
			src_pass == other.src_pass &&
			dst_pass == other.dst_pass;
	}

	struct KeyHash {
		using is_transparent = void;
		size_t operator()(const Key& k) const noexcept {
			size_t h1 = std::hash<uint64_t>{}(k.src_pass);
			size_t h2 = std::hash<uint64_t>{}(k.dst_pass);
			size_t h3 = std::hash<uint64_t>{}(k.resource);

			return hash_combine(hash_combine(h1, h2), h3);
		}
    };

	struct KeyEquals {
		using is_transparent = void;

		bool operator()(const PassEdge& a, const Key &key) const {
			return 
				a.src_pass == key.src_pass &&
				a.dst_pass == key.dst_pass &&
				a.barrier.resource.u64 == key.resource;
		}

		bool operator()(const Key& a, const Key &b) const {
			return 
				a.src_pass == b.src_pass &&
				a.dst_pass == b.dst_pass &&
				a.resource == b.resource;
		}
	};

	Key get_key() const {
		return Key{
			.src_pass = src_pass,
			.dst_pass = dst_pass,
			.resource = barrier.resource
		};
	}

	bool is_on_same_queue() const {
		return 
			barrier.src_state.queue_family_index != barrier.dst_state.queue_family_index;
	}
};

struct PassCommand
{
	ev2::Command base;
	uint32_t barrier_count;
};

struct PassNode
{
	std::vector<PassCommand> commands;
	std::vector<PassBarrier> barriers;

	std::unordered_map<uint64_t, ResourceStateFlags> final_states;

	const ev2::Pass *pass;

	uint32_t submission_idx;
	uint32_t barrier_offset;
	uint32_t queue_family_index;

	bool is_gfx_node() const {
		return pass->gfx;
	}
};

struct RenderGraphSubmission
{
	std::vector<const PassNode *> nodes;

	std::vector<VkSemaphoreSubmitInfo> wait;
	std::vector<VkSemaphoreSubmitInfo> signal;

	VkCommandBuffer cmds;

	uint32_t queue_index;
};

struct RenderGraph
{
	ev2::GfxContext *ctx;

	std::vector<RenderGraphSubmission> submissions;

	// Satisfies a valid topological given by the order out the input passes
	std::vector<PassNode> nodes;

	// Satisfies a valid topological given by the order out the input passes
	std::vector<PassEdge> edges;

	std::unordered_map<
		PassEdge::Key,
		uint32_t,
		PassEdge::KeyHash,
		PassEdge::KeyEquals
	> edge_idx_map;

	std::unordered_map<uint64_t, uint32_t> buffer_passes;
	std::unordered_map<uint64_t, uint32_t> image_passes;

	std::vector<QueueSubmitter *> queues;
};

static constexpr uint32_t PASS_INDEX_OUT_OF_FRAME = UINT32_MAX;

//------------------------------------------------------------------------------
// Graphics context

struct GfxContext
{
	//-----------------------------------------------------------------------------
	// Vulkan data 

    VkInstance                  instance;
    VkDebugUtilsMessengerEXT    debugMessenger;

    VkPhysicalDevice            physicalDevice = VK_NULL_HANDLE;
    VkDevice                    device;

	std::vector<QueueFamily> 	queue_families;

	QueueFamily * 		graphics_family;
	QueueFamily * 		transfer_family;
	QueueFamily * 		compute_family;

	QueueSubmitter * 		graphics_queue;

    VkQueue                     present_queue;

    VkSurfaceKHR                surface;   

	VmaAllocator 				allocator;

	SwapChain 					swap_chain;
	std::vector<RenderTarget*>	swap_chain_targets;

	ImageID depth_buffer;
	VkImageView depth_buffer_view;

	VkDescriptorSetLayout 		base_descriptor_set_layouts[EV2_BASE_SET_COUNT];
	VkPipelineLayout 			base_pipeline_layouts[EV2_BASE_SET_COUNT];

	VkDescriptorPool static_descriptor_pool;

	uint64_t frame_counter;
	VkSemaphore frame_semaphore;

	//-----------------------------------------------------------------------------
	// Important things 
	
	struct {
		uint32_t max_workers;
		VkPhysicalDeviceLimits limits;
	} caps;
	
	// set on initialization
	std::thread::id main_thread_id;
	
	uint32_t max_frames_in_flight;

	uint64_t start_time_ns;

	uint32_t current_swap_chain_index;
	uint32_t desired_surface_width;
	uint32_t desired_surface_height;

	bool is_swap_chain_valid;

	// Workers
	std::unique_ptr<ThreadPool> worker_pool;

	// Per-frame data
	FrameContext frames[EV2_MAX_FRAMES_IN_FLIGHT];
	
	// Upload pools 
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

	//------------------------------------------------------------------------------

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

	VkDescriptorSetLayout get_base_descriptor_set_layout(uint32_t level);
	VkPipelineLayout get_base_pipeline_layout(uint32_t level);

	ev2::Result reset_swap_chain();

	ev2::Result wait_for_frame_completion(FrameContext *frame);

	inline FrameContext *get_current_frame() {
		return &frames[frame_counter % max_frames_in_flight];
	}
	inline FrameContext *get_previous_frame() {
		uint64_t idx = frame_counter ? frame_counter - 1 : 0;
		return &frames[idx % max_frames_in_flight];
	}

	// convenience (emplace)
	inline BufferID emplace_buffer(Buffer &&buf) {
		PoolID rid = buffer_pool->allocate(std::move(buf));
		return EV2_HANDLE_CAST(Buffer, rid.u64);
	}
	inline ImageID emplace_image(Image &&img) {
		PoolID rid = image_pool->allocate(std::move(img));
		return EV2_HANDLE_CAST(Image, rid.u64);
	}
	inline TextureID emplace_texture(Texture &&tex) {
		PoolID rid = texture_pool->allocate(std::move(tex));
		return EV2_HANDLE_CAST(Texture, rid.u64);
	}

	// convenience (get)

	inline Buffer *get_buffer(BufferID h) {
		PoolID rid = {.u64 = h.id};
		return buffer_pool->get(rid);
	}
	inline Image *get_image(ImageID h) {
		PoolID rid = {.u64 = h.id};
		return image_pool->get(rid);
	}
	inline Texture *get_texture(TextureID h) {
		PoolID rid = {.u64 = h.id};
		return texture_pool->get(rid);
	}

	inline ShaderBindings *get_shader_bindings(ShaderBindingsID h) {
		return EV2_TYPE_PTR_CAST(ShaderBindings, h);
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

extern VkPipelineRenderingCreateInfoKHR get_swapchain_rendering_info(GfxContext *ctx);
extern std::vector<VkDynamicState> get_dynamic_states(GfxContext *ctx);

};

#endif //DEVICE_
