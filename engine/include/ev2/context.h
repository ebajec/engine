#ifndef EV2_DEVICE_H
#define EV2_DEVICE_H

#define EV2_BACKEND_VULKAN

#define EV2_IMGUI_COMPATIBILITY

#include "ev2/defines.h"
#include "vulkan/vulkan.h"

enum {
	EV2_BASE_SET_BINDLESS, 
	EV2_BASE_SET_PER_FRAME,
	EV2_BASE_SET_PER_PASS,
	EV2_BASE_SET_COUNT
};

enum {
	EV2_GFX_SET_PER_DRAW = 3
};

#ifdef EV2_IMGUI_COMPATIBILITY
struct ImGui_ImplVulkan_InitInfo;
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
	EBAD_SHADER = -5,
	ERENDER_GRAPH = -6,
	EBAD_SWAPCHAIN = -7,
	EUNKNOWN = -1024
};

Result _set_error_internal(Result result, const char *file, int line, const char *msg, ...);

#define set_error(result, format, ...)\
	_set_error_internal(result, __FILE__, __LINE__, format, ##__VA_ARGS__)


struct GfxContext;

struct VulkanInitOptions
{
	const char ** validationLayers;
	size_t validationLayerCount;
	const char ** instanceExtensions;
	size_t instanceExtensionCount;

	bool enableValidationLayers = true;
};

struct GfxContextVulkanInfo
{
	VkSurfaceKHR surface;
	uint32_t surface_width;
	uint32_t surface_height;
};

ev2::Result init_for_vulkan(const VulkanInitOptions &opts);
VkInstance get_vulkan_instance();
GfxContext *create_context_for_vulkan(const char *path, const GfxContextVulkanInfo &params);

void destroy_context(GfxContext *ctx);

ev2::Result on_resize(GfxContext *ctx, uint32_t width, uint32_t height);
ev2::Result wait_complete(GfxContext *ctx, uint64_t sync);

#ifdef EV2_IMGUI_COMPATIBILITY
void populate_imgui_vulkan_init_info(GfxContext *ctx, ImGui_ImplVulkan_InitInfo *info);
#endif

};

#endif // EV2_DEVICE_H
