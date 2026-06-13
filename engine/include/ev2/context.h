#ifndef EV2_DEVICE_H
#define EV2_DEVICE_H

#define EV2_BACKEND_VULKAN

#include "ev2/defines.h"
#include "vulkan/vulkan.h"

namespace ev2 { 

MAKE_HANDLE(Sync);

enum Result
{
	TIMEOUT = 1,
	SUCCESS = 0,
	ELOAD_FAILED = -1,
	ERESIZE_FAILED = -2,
	EINVALID_BINDING = -3,
	EINIT_FAILED = -4,
	EMISMATCHED_SHADERS = -5,
	EUNKNOWN = -1024
};

struct GfxContext;

struct InitOptionsVulkan
{
	const char ** validationLayers;
	size_t validationLayerCount;
	const char ** instanceExtensions;
	size_t instanceExtensionCount;

	bool enableValidationLayers = true;
};

struct GfxContextInfoVulkan
{
	VkSurfaceKHR surface;
	uint32_t surface_width;
	uint32_t surface_height;
};

ev2::Result init_for_vulkan(const InitOptionsVulkan &opts);
VkInstance get_vulkan_instance();

GfxContext *create_context_for_vulkan(const char *path, 
								   const GfxContextInfoVulkan &params);
void destroy_context(GfxContext *ctx);

ev2::Result on_resize(GfxContext *ctx, uint32_t width, uint32_t height);
ev2::Result wait_complete(GfxContext *ctx, uint64_t sync);

};

#endif // EV2_DEVICE_H
