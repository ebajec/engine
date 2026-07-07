#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "def_vulkan.h"
#include "vk_mem_alloc.h"

#include "utils/asset_table.h"
#include "utils/pool.h"
#include "utils/gpu_table.h"
#include "utils/thread_pool.h"
#include "utils/platform.h"

#include "backends/vulkan/resource_impl.h"
#include "backends/vulkan/pipeline_impl.h"
#include "backends/vulkan/upload_pool.h"

#include <glm/mat4x4.hpp>

#define EV2_MAX_FRAMES_IN_FLIGHT 3
#define EV2_FRAME_TIMEOUT 1e9

#define MAKE_VERSIONED_HANDLE_ACCESS(Type, TypeLower)\
inline Type##ID emplace_##TypeLower(Type &&val) {\
	PoolID id = TypeLower##_pool->allocate(std::move(val));\
	return Type##ID{\
		.id = id.slot,\
		.gen = id.gen\
	};\
}\
inline Type *get_##TypeLower(Type##ID h) {\
	PoolID id = {.slot = h.id, .gen = h.gen};\
	Type *ptr = TypeLower##_pool->get_checked(id);\
	if (!ptr) {\
		log_error("Invalid " #Type "ID passed : id=%d, gen=%d", h.id, h.gen);\
		throw std::runtime_error("Invalid handle");\
	}\
	return ptr;\
}\
__attribute__((noinline)) Type *get_##TypeLower##_unchecked(Type##ID h) {\
	PoolID id = {.slot = h.id, .gen = h.gen};\
	return TypeLower##_pool->get_unchecked(id);\
}

#define MAKE_ASSET_HANDLE_ACCESS(Type, TypeLower)\
inline Type *get_##TypeLower(Type##ID h) {\
	AssetID id = static_cast<uint32_t>(h.id);\
	AssetEntry *ent = assets->get_entry(id);\
	return (Type*)ent->usr;\
}\
inline const char *get_##TypeLower##_name(Type##ID h) {\
	AssetID id = static_cast<uint32_t>(h.id);\
	AssetEntry *ent = assets->get_entry(id);\
	return (Type*)ent ? ent->path : nullptr;\
}

namespace ev2 {

MAKE_POOL_ID_CONVERSION(Buffer)
MAKE_POOL_ID_CONVERSION(Image)
MAKE_POOL_ID_CONVERSION(Texture)
MAKE_POOL_ID_CONVERSION(Bindings)

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
    std::vector<ImageID>        image_ids;
	std::vector<RenderTarget*>	targets;
	std::vector<VkSemaphore> 	semaphores;
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

struct TransientCommands
{
	VkDevice device;
	VkCommandPool command_pool;
	std::vector<VkCommandBuffer> free_cmds;
	std::vector<VkCommandBuffer> in_use_cmds;

	VkResult init(VkDevice device, uint32_t queue_family_index);
	void destroy();
	VkResult reset();
	VkResult get_cmds(uint32_t count, VkCommandBuffer *out_cmds);
};

struct FrameContext
{
	ev2::GfxContext *ctx;

	uint64_t index = 0;
	uint64_t image_use_count = 0;

	uint32_t image_index;

	double t; 
	double dt;
	ev2::BufferID ubo;

	std::deque<ev2::Pass> passes;

	std::vector<TransientCommands> commands;

	VkSemaphore image_available_sempahore;

	// owned by the swapchain
	VkSemaphore render_finished_semaphore;

	VkDescriptorPool descriptor_pool;

	VkDescriptorSet descriptor_set;

	// render target for current swapchain image
	const ev2::RenderTarget *screen_target;

	std::unique_ptr<RenderGraph> render_graph;

	std::unordered_map<SyncKey, ResourceSync, SyncKey::Hash>
		sync_map;

	ResourceSync *get_resource_sync(TaggedResource resource, VkQueue queue);

	// Destroy any semaphores for resources not used by this frame
	void cull_unused_syncs();

	Pass * new_pass(Pass &&pass) {
		return &passes.emplace_back(std::move(pass));
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

	inline bool is_queue_transfer() const {
		return src_state.queue_family_index != dst_state.queue_family_index;
	}
};

struct PassEdge
{
	uint32_t src_node;
	uint32_t dst_node;

	ResourceStateFlags src_state;
	ResourceStateFlags dst_state;
	TaggedResource resource;

	bool src_write : 1;
	bool dst_write : 1;

	struct Key
	{
		uint32_t src_pass;
		uint32_t dst_pass;
		uint64_t resource;
	};

	constexpr bool operator == (const PassEdge &other) {
		return 
			resource == other.resource && 
			src_node == other.src_node &&
			dst_node == other.dst_node;
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
				a.src_node == key.src_pass &&
				a.dst_node == key.dst_pass &&
				a.resource.u64 == key.resource;
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
			.src_pass = src_node,
			.dst_pass = dst_node,
			.resource = resource
		};
	}

	bool is_cross_queue() const {
		return 
			src_state.queue_family_index != dst_state.queue_family_index;
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
	std::vector<PassBarrier> pre_barriers;
	std::vector<PassBarrier> post_barriers;

	std::unordered_map<uint64_t, ResourceStateFlags> final_states;

	const ev2::Pass *pass;

	uint32_t submission_idx;
	uint32_t barrier_offset;
	uint32_t queue_family_index;

	bool is_gfx_node() const {
		return pass->gfx.get();
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

static constexpr uint32_t PASS_NODE_INDEX_OUT_OF_FRAME = UINT32_MAX;

//------------------------------------------------------------------------------
// Graphics context

struct GfxContext
{
	//-----------------------------------------------------------------------------
	
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

	ImageID depth_buffer;
	VkImageView depth_buffer_view;

	VkDescriptorSetLayout 		base_descriptor_set_layouts[EV2_BASE_SET_COUNT];
	VkPipelineLayout 			base_pipeline_layouts[EV2_BASE_SET_COUNT];

	VkDescriptorPool static_descriptor_pool;

	uint64_t frame_counter;
	VkSemaphore frame_semaphore;

	//-----------------------------------------------------------------------------
	
	struct {
		uint32_t max_workers;
		VkPhysicalDeviceLimits limits;
	} caps;
	
	std::thread::id main_thread_id;

	uint32_t physical_device_index;
	
	uint32_t max_frames_in_flight;
	uint32_t framerate_hz = 240;

	uint64_t start_time_ns;
	struct timespec last_frame_ts;

	uint32_t current_swap_chain_index;
	uint32_t desired_surface_width;
	uint32_t desired_surface_height;

	bool is_swap_chain_valid;

	//-----------------------------------------------------------------------------
	
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
	std::unique_ptr<Pool<Buffer>> buffer_pool;
	std::unique_ptr<Pool<Image>> image_pool;
	std::unique_ptr<Pool<Texture>> texture_pool;
	std::unique_ptr<Pool<Bindings>> bindings_pool;

	// Special GPU Resources
	GPUTTable<ViewData> view_data;
	GPUTTable<glm::mat4> transforms;

	// defaults to identity matrix
	ViewID default_view;

	//------------------------------------------------------------------------------

	void assert_inside_frame();
	void assert_outside_frame();
	bool is_inside_frame() {return get_current_frame()->index >= frame_counter;}
	
	VkDescriptorSetLayout get_base_descriptor_set_layout(uint32_t level);
	VkPipelineLayout get_base_pipeline_layout(uint32_t level);

	ev2::Result reset_swap_chain();
	ev2::Result wait_for_frame(uint64_t frame_index);

	constexpr double seconds_since_start(struct timespec ts) {
		uint64_t time_ns = (uint64_t)ts.tv_sec + (uint64_t)ts.tv_nsec; 
		return (double)(time_ns - start_time_ns)/1e9;
	}

	constexpr FrameContext *get_current_frame() {
		return &frames[frame_counter % max_frames_in_flight];
	}

	MAKE_VERSIONED_HANDLE_ACCESS(Buffer, buffer);
	MAKE_VERSIONED_HANDLE_ACCESS(Image, image);
	MAKE_VERSIONED_HANDLE_ACCESS(Texture, texture);
	MAKE_VERSIONED_HANDLE_ACCESS(Bindings, bindings);

	MAKE_ASSET_HANDLE_ACCESS(GfxPipeline, gfx_pipeline);
	MAKE_ASSET_HANDLE_ACCESS(ComputePipeline, compute_pipeline);
	MAKE_ASSET_HANDLE_ACCESS(Shader, shader);
};

extern VkPipelineRenderingCreateInfoKHR get_swapchain_rendering_info(GfxContext *ctx);
extern std::vector<VkDynamicState> get_dynamic_states(GfxContext *ctx);

};

#endif //DEVICE_
