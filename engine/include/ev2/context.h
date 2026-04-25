#ifndef EV2_DEVICE_H
#define EV2_DEVICE_H

#define EV2_BACKEND_VULKAN

#include "ev2/defines.h"

#ifdef EV2_BACKEND_VULKAN
#include "vulkan/vulkan.h"
#endif

namespace ev2 { 

enum Result
{
	TIMEOUT = 1,
	SUCCESS = 0,
	ELOAD_FAILED = -1,
	ERESIZE_FAILED = -2,
	EINVALID_BINDING = -3,
	EINIT_FAILED = -4,
	EUNKNOWN = -1024
};

struct Context;

#ifdef EV2_BACKEND_VULKAN
struct InitOptionsVulkan
{
	const char ** validationLayers;
	size_t validationLayerCount;
	const char ** instanceExtensions;
	size_t instanceExtensionCount;

	bool enableValidationLayers = true;
};

struct ContextInfoVulkan
{
	VkSurfaceKHR surface;
};

ev2::Result init_for_vulkan(const InitOptionsVulkan &opts);
VkInstance get_vulkan_instance();

Context *create_context_for_vulkan(const char *path, 
								   const ContextInfoVulkan &params);
#else
Device *create_device(const char *path);
#endif

void destroy_context(Context *dev);
};

#endif // EV2_DEVICE_H
