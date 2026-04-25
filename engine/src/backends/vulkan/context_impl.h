#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "def_vulkan.h"
#include "vk_mem_alloc/vma.h"

#include "utils/asset_table.h"
#include "utils/pool.h"
#include "utils/gpu_table.h"

#include "backends/vulkan/resource_impl.h"
#include "backends/vulkan/render_impl.h"
#include "backends/vulkan/pipeline_impl.h"
#include "backends/vulkan/upload_pool.h"

#include <glm/mat4x4.hpp>

struct VulkanOptions
{
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

namespace ev2 {

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
	BufferID ubo;
};

struct Context
{
    VkInstance                  instance;
    VkDebugUtilsMessengerEXT    debugMessenger;

    VkPhysicalDevice            physicalDevice = VK_NULL_HANDLE;
    VkDevice                    device;
    VkQueue                     graphicsQueue;
    VkQueue                     presentQueue;
    VkSurfaceKHR                surface;   

	VmaAllocator 				allocator;

	VkSwapchainKHR              swapChain;
    VkFormat                    swapChainImageFormat;
    VkExtent2D                  swapChainExtent;
    std::vector<VkImage>        swapChainImages;
    std::vector<VkImageView>    swapChainImageViews;
    std::vector<VkFramebuffer>  swapChainFramebuffers;

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

	// Frame data
	FrameContext frame;

	// Misc stats
	uint64_t start_time_ns;

	// convenience
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

	inline GraphicsPipeline *get_gfx_pipeline(GraphicsPipelineID h) {
		AssetID id = static_cast<uint32_t>(h.id);
		AssetEntry *ent = assets->get_entry(id);
		return (GraphicsPipeline*)ent->usr;
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

#endif //DEVICE_INTERNAL_H
