#include "ev2/context.h"

#include "def_vulkan.h"
#include "context_impl.h"
#include "pipeline_impl.h"

#include "utils/asset_table.h"
#include "utils/pool.h"
#include "utils/platform.h"
#include "ev2/utils/ansi_colors.h"

#include "stb_image.h"

#include <cstdarg>
#include <algorithm>
#include <sstream>
#include <set>
#include <optional>

//------------------------------------------------------------------------------

namespace ev2 {

VkInstance g_vk_instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT g_vk_messenger = VK_NULL_HANDLE;

//------------------------------------------------------------------------------

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> transferFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return 
			graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#pragma clang diagnostic ignored "-Wunused-function"

static void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

//------------------------------------------------------------------------------

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
	switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
		break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    	log_info(ANSI_PINK(validation layer:) " %s", pCallbackData->pMessage);
		break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    	log_warn(ANSI_PINK(validation layer:) " %s", pCallbackData->pMessage);
		break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    	log_error_nofileinfo(ANSI_PINK(validation layer:) " %s", pCallbackData->pMessage);
		break;
	default:
		break;
	}
    return VK_FALSE;
}

#pragma clang diagnostic push

//------------------------------------------------------------------------------
// Vulkan initialization helpers

static bool checkValidationLayerSupport(const ev2::VulkanInitOptions &opts)
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount,nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount,availableLayers.data());

    for (size_t i = 0; i < opts.validationLayerCount; ++i) {
		const char *layerName = opts.validationLayers[i];
        bool layerFound = false;

        for (const auto& layerProperites : availableLayers) {
            if (!strcmp(layerName,layerProperites.layerName)) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) 
            return false;
    }

    return true;
}   

static std::vector<const char*> getRequiredExtensions(
	const ev2::VulkanInitOptions &opts)
{
    std::vector<const char*> extensions; 
	extensions.reserve(1 + opts.instanceExtensionCount);

	for (size_t i = 0; i < opts.instanceExtensionCount; ++i) {
		extensions.push_back(opts.instanceExtensions[i]);
	}

    if (opts.enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

static void populateDebugMessengerCreateInfo(
	VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
} 

static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, 
											VkSurfaceKHR surface) 
{
    QueueFamilyIndices indices;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device,
											 &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }   
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.computeFamily = i;
        }   
        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
            indices.transferFamily = i;
        }   
        if (presentSupport) {
            indices.presentFamily = i;
        }

        i++;
    }
    
    return indices;
}

static SwapChainSupportDetails querySwapChainSupport(
	VkPhysicalDevice device, VkSurfaceKHR surface) 
{
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    // surface formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, 
											 &formatCount, details.formats.data());
    }

    // present modes
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, 
											  &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, 
												  &presentModeCount, 
												  details.presentModes.data());
    }

    return details;
}


static bool checkDeviceExtensionSupport(VkPhysicalDevice device, 
								 const VulkanOptions &opts) 
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device,nullptr,&extensionCount,nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    vkEnumerateDeviceExtensionProperties(device,nullptr, 
										 &extensionCount, availableExtensions.data());
    std::set<std::string> requiredExtensions(
		opts.deviceExtensions.begin(), opts.deviceExtensions.end());
    
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}


static bool isDeviceSuitable(VkSurfaceKHR surface, VkPhysicalDevice device, 
							 const VulkanOptions &opts)
{
    QueueFamilyIndices indices = findQueueFamilies(device, surface);
    bool extensionsSupported = checkDeviceExtensionSupport(device, opts);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = 
			querySwapChainSupport(device, surface);
        swapChainAdequate = 
			!swapChainSupport.formats.empty() && 
			!swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(
	const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
			availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

static VkPresentModeKHR chooseSwapPresentMode(
	const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}


static VkExtent2D chooseSwapExtent(uint32_t w, uint32_t h, 
							const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = {w, h};

        actualExtent.width = std::clamp(actualExtent.width, 
										capabilities.minImageExtent.width, 
										capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, 
										 capabilities.minImageExtent.height, 
										 capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

static void create_queue_family(
	ev2::GfxContext * ctx,
	uint32_t index,
	const VkQueueFamilyProperties &props)
{
	QueueFamily *family = &ctx->queue_families[index];

	family->queue_count = props.queueCount;
	family->queues.reset(new QueueSubmitter[props.queueCount]);
	family->index = index;

	for (uint32_t i = 0; i < props.queueCount; ++i) {
    	vkGetDeviceQueue(ctx->device, index, i, &family->queues[i].queue);
	}
}


static void destroy_swap_chain_targets(ev2::GfxContext *ctx)
{
	if (ctx->depth_buffer.is_valid())
		destroy_image(ctx, ctx->depth_buffer);

	if (ctx->depth_buffer_view)
		vkDestroyImageView(ctx->device, ctx->depth_buffer_view, nullptr);

	for (ev2::RenderTarget *target : ctx->swap_chain.targets)
		if (target)
			ev2::destroy_render_target_internal(ctx, target);

	ctx->swap_chain.targets.clear();
}

static ev2::Result create_swap_chain_targets(ev2::GfxContext *ctx)
{

	const SwapChain &swap_chain = ctx->swap_chain;

	uint32_t image_count = (uint32_t)swap_chain.image_views.size();
	const uint32_t w = swap_chain.extent.width;
	const uint32_t h = swap_chain.extent.height;

	ev2::Result result = ev2::SUCCESS;

	VkResult vk_result = create_depth_stencil_image(
		ctx, w,h, &ctx->depth_buffer, &ctx->depth_buffer_view);
	if (vk_result != VK_SUCCESS)
		return set_error(ev2::EBAD_SWAPCHAIN, "Failed to create depth buffer");

	ctx->swap_chain.targets.resize(image_count, nullptr);

	for (uint32_t i = 0; i < image_count; ++i) {
		ev2::RenderTarget *target = ev2::create_render_target_internal(ctx,
			w, h, 
			swap_chain.image_views[i],
			ctx->depth_buffer_view 
	  	);

		// This should never fail, just assigning stuff
		if (!target) {
			result = set_error(ev2::EBAD_SWAPCHAIN, "Failed to create swapchain render targets");
			break;
		}

		ctx->swap_chain.targets[i] = target;
	}

	if (result != ev2::SUCCESS)
		destroy_swap_chain_targets(ctx);

	return result;
}

//------------------------------------------------------------------------------
// Vulkan initialization

static ev2::Result pick_physical_device(ev2::GfxContext *ctx, 
										const VulkanOptions &opts)
{
	uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(g_vk_instance, &deviceCount, nullptr);

    if (!deviceCount) {
        log_error("failed to find GPUs with vulkan support");
		return ev2::EINIT_FAILED;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(g_vk_instance, &deviceCount, devices.data());

    for (uint32_t i = 0; i < devices.size(); ++i) {
		VkPhysicalDevice device = devices[i];
        if (isDeviceSuitable(ctx->surface, device, opts)) {
            ctx->physicalDevice = device;
			ctx->physical_device_index = i;
            break;
        }
    }

    if (ctx->physicalDevice == VK_NULL_HANDLE) {
        log_error("failed to find a suitable GPU!");
		return ev2::EINIT_FAILED;
    }

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(ctx->physicalDevice, &properties);

	ctx->caps.limits = properties.limits;

	std::stringstream msg;

	msg << "Physical device: \n"
		<< "\t'" << properties.deviceName << "'" << "\n"
		<< "\tAPI Version: " << properties.apiVersion << "\n"
		<< "\tDriver Version: " << properties.driverVersion << "\n"
	;

	log_info("%s", msg.str().c_str());

	return ev2::SUCCESS;
}

static ev2::Result pick_logical_device(ev2::GfxContext *ctx, 
									   const VulkanOptions &opts)
{
	QueueFamilyIndices indices = findQueueFamilies(ctx->physicalDevice, 
												ctx->surface);

    std::set<uint32_t> uniqueQueueFamilies = {
		indices.graphicsFamily.value(), 
		indices.presentFamily.value()
	};

	if (indices.computeFamily.has_value())
		uniqueQueueFamilies.insert(*indices.computeFamily);
	if (indices.transferFamily.has_value())
		uniqueQueueFamilies.insert(*indices.transferFamily);

	uint32_t family_count;
	std::vector<VkQueueFamilyProperties> queue_props;

	vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, 
										  &family_count, nullptr);
	queue_props.resize(family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, 
										  &family_count, queue_props.data());

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueFamily,
			.queueCount = queue_props[queueFamily].queueCount,
			.pQueuePriorities = &queuePriority,
		};

        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    // device features
    VkPhysicalDeviceFeatures deviceFeatures{};

	VkPhysicalDeviceVulkan12Features features12{
		.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.timelineSemaphore = VK_TRUE,
	};

	VkPhysicalDeviceVulkan13Features features13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &features12,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE,
	};

    VkDeviceCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features13,
		.queueCreateInfoCount = (uint32_t)queueCreateInfos.size(),
		.pQueueCreateInfos = queueCreateInfos.data(),
		.enabledExtensionCount = (uint32_t)opts.deviceExtensions.size(),
		.ppEnabledExtensionNames = opts.deviceExtensions.data(),
		.pEnabledFeatures = &deviceFeatures,
	};

	VkResult result = vkCreateDevice(ctx->physicalDevice, 
								  &createInfo, nullptr, &ctx->device); 
    if (result != VK_SUCCESS) {
        log_error("failed to create logical device!");
		return ev2::EINIT_FAILED;
    }

    vkGetDeviceQueue(ctx->device, indices.presentFamily.value(), 0,
					 &ctx->present_queue);

	ctx->queue_families.resize(family_count);

	//-----------------------------------------------------------------------------
	// Always initialize the graphics queue family
	
	uint32_t graphics_index = *indices.graphicsFamily;
	create_queue_family(ctx, *indices.graphicsFamily, 
					   queue_props[*indices.graphicsFamily]);
	ctx->graphics_family = &ctx->queue_families[graphics_index];
	ctx->graphics_queue = &ctx->graphics_family->queues[0];

	//-----------------------------------------------------------------------------
	// Initialize dedicated compute and/or transfer if available
	
	if (indices.transferFamily.has_value()) {
		uint32_t transfer_index = *indices.transferFamily;
		create_queue_family(ctx, *indices.transferFamily, 
						   queue_props[*indices.transferFamily]);
		ctx->transfer_family = &ctx->queue_families[transfer_index];
	} else {
		ctx->transfer_family = ctx->graphics_family;
	}

	if (indices.computeFamily.has_value()) {
		uint32_t compute_index = *indices.transferFamily;
		create_queue_family(ctx, *indices.computeFamily, 
						   queue_props[*indices.computeFamily]);
		ctx->compute_family = &ctx->queue_families[compute_index];
	} else {
		ctx->compute_family = ctx->graphics_family;
	}

	return ev2::SUCCESS;
}

static VkResult create_swap_chain_views(ev2::GfxContext *ctx)
{
	ctx->swap_chain.image_views.resize(ctx->swap_chain.images.size());

    for (size_t i = 0; i < ctx->swap_chain.images.size(); ++i) {
        VkImageViewCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = ctx->swap_chain.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = ctx->swap_chain.image_format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		};

		VkResult res = vkCreateImageView(ctx->device, 
								   &createInfo, nullptr, 
								   &ctx->swap_chain.image_views[i]);
        if (res != VK_SUCCESS) {
            log_error("failed to create swap chain image views!");
			return res;
        }
    }

	return VK_SUCCESS;
}

static ev2::Result create_swap_chain(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
{
	SwapChainSupportDetails swapChainSupport = querySwapChainSupport(
		ctx->physicalDevice, ctx->surface);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(w, h, swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount;
    uint32_t maxImageCount = swapChainSupport.capabilities.maxImageCount;

    if (maxImageCount) 
		imageCount = (imageCount + 1) > maxImageCount ? maxImageCount : (imageCount + 1);

    VkSwapchainCreateInfoKHR createInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = ctx->surface,
		.minImageCount = imageCount,
		.imageFormat = surfaceFormat.format,
		.imageColorSpace = surfaceFormat.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	};

    QueueFamilyIndices indices = findQueueFamilies(ctx->physicalDevice, 
												   ctx->surface);
    uint32_t queueFamilyIndices[] = {
		indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = ctx->swap_chain.swapchain;

	VkResult result = VK_SUCCESS;

	result = vkCreateSwapchainKHR(ctx->device, &createInfo, nullptr, 
							   &ctx->swap_chain.swapchain); 

    if (result) {
        return set_error(ev2::ERESIZE_FAILED, "vkCreateSwapchainKHR failed!");
    }

    result = vkGetSwapchainImagesKHR(ctx->device, 
									 ctx->swap_chain.swapchain, &imageCount, nullptr);
    if (result)
		return set_error(ev2::ERESIZE_FAILED, "vkGetSwapchainImagesKHR failed!");

    ctx->swap_chain.images.resize(imageCount);
    
	result = vkGetSwapchainImagesKHR(ctx->device, ctx->swap_chain.swapchain, &imageCount, 
							ctx->swap_chain.images.data());
	if (result)
		return set_error(ev2::ERESIZE_FAILED, "vkGetSwapchainImagesKHR failed!");

	ctx->swap_chain.semaphores.resize(imageCount);

	for (uint32_t i = 0; i < imageCount; ++i) {
		VkSemaphoreCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};	
		result = vkCreateSemaphore(
			ctx->device, &create_info, nullptr, &ctx->swap_chain.semaphores[i]);

		if (result)
			return set_error(ev2::ERESIZE_FAILED, "Failed to create swapchain image semaphores");
	}

    ctx->swap_chain.image_format = surfaceFormat.format;
    ctx->swap_chain.extent = extent;

	result = create_swap_chain_views(ctx);

	if (result)
		return set_error(ev2::ERESIZE_FAILED, "Failed to create swap chain image views");
	
	ctx->swap_chain.image_ids.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i) {
		ctx->swap_chain.image_ids[i] = ctx->emplace_image(Image{
			.image = ctx->swap_chain.images[i],
			.allocation = VK_NULL_HANDLE,
			.base_view = VK_NULL_HANDLE,
			.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
			.w = extent.width, 
			.h = extent.height,
			.d = 1,
			.levels = 1,
		});
	}

	return ev2::SUCCESS;
}

static void destroy_swap_chain(GfxContext *ctx, SwapChain &swap_chain)
{
	destroy_swap_chain_targets(ctx);

    for (size_t i = 0; i < swap_chain.image_views.size(); i++) {
		ctx->image_pool->deallocate(to_pool_id(swap_chain.image_ids[i]));
	}

    for (size_t i = 0; i < swap_chain.image_views.size(); i++) {
        vkDestroyImageView(ctx->device, swap_chain.image_views[i], nullptr);
    }

	if (swap_chain.swapchain) {
    	vkDestroySwapchainKHR(ctx->device, swap_chain.swapchain, nullptr);
		swap_chain.swapchain = VK_NULL_HANDLE;
	}

	for (size_t i = 0; i < swap_chain.semaphores.size(); ++i) {
		vkDestroySemaphore(ctx->device, swap_chain.semaphores[i], nullptr);
	}

	swap_chain.images.clear();
	swap_chain.image_views.clear();
	swap_chain.image_ids.clear();
}

static ev2::Result create_allocator(ev2::GfxContext *ctx)
{
	VmaAllocatorCreateInfo create_info = {
		.flags = 0,
		.physicalDevice = ctx->physicalDevice,
		.device = ctx->device,
		.preferredLargeHeapBlockSize = 0,
		.pAllocationCallbacks = nullptr,
		.pDeviceMemoryCallbacks = nullptr,
		.pHeapSizeLimit = 0,
		.pVulkanFunctions = 0,
		.instance = g_vk_instance,
		.vulkanApiVersion = VK_API_VERSION,
		.pTypeExternalMemoryHandleTypes = nullptr, 
	};

	VkResult res = vmaCreateAllocator(&create_info, &ctx->allocator);

	if (res != VK_SUCCESS) {
		log_error("Failed to create Vulkan memory allocator");
		return ev2::EINIT_FAILED;
	}
	return ev2::SUCCESS;
}

static ev2::Result create_static_descriptor_pool(ev2::GfxContext *ctx, VkDescriptorPool *pool)
{
	VkDescriptorPoolSize pool_sizes[] = {
		VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 64,
		},
		VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 128,
		},
		VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 64,
		},
		VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 64,
		},
		VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = 32,
		},
	};

	VkDescriptorPoolCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = 
			VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 64,
		.poolSizeCount = sizeof(pool_sizes)/sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};
	VkResult result = vkCreateDescriptorPool(ctx->device, &create_info, 
						nullptr, pool);

	if (result != VK_SUCCESS)
		return ev2::EINIT_FAILED;
	return ev2::SUCCESS;
}

static ev2::Result init_frame_descriptor_set(ev2::GfxContext *ctx, FrameContext *frame)
{

	VkDescriptorSetLayout layout = 
		ctx->get_base_descriptor_set_layout(EV2_BASE_SET_PER_FRAME); 

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->static_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout 
	};

	VkResult vk_result = 
		vkAllocateDescriptorSets(ctx->device, &alloc_info, &frame->descriptor_set);

	if (vk_result != VK_SUCCESS)
		return ev2::EINIT_FAILED;

	ev2::Buffer *ubo = ctx->get_buffer(frame->ubo);

	VkDescriptorBufferInfo ubo_info = {
		.buffer = ubo->buffer,
		.offset = 0,
		.range = ubo->size
	};

	VkWriteDescriptorSet writes[] = {
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = frame->descriptor_set,
			.dstBinding = EV2_FRAME_UBO_BINDING,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &ubo_info,
		}
	};

	vkUpdateDescriptorSets(
		ctx->device, 
		sizeof(writes)/sizeof(writes[0]), writes, 
		0, nullptr
	);

	return ev2::SUCCESS;
}

static ev2::Result create_frame_context(ev2::GfxContext *ctx,
									   FrameContext *frame)
{
	VkResult vk_result = VK_SUCCESS;

	frame->ctx = ctx;
	frame->ubo = ev2::create_buffer(ctx, sizeof(GPUFramedata), 
									ev2::BUFFER_USAGE_UNIFORM_BUFFER_BIT | 
									ev2::BUFFER_USAGE_TRANSFER_DST_BIT);

	uint32_t pool_count = 1;
frame->commands.resize(pool_count);

	for (uint32_t i = 0; i < pool_count; ++i) {
		vk_result = frame->commands[i].init(
			ctx->device, 
			ctx->graphics_family->index);

		if (vk_result != VK_SUCCESS) {
			return set_error(ev2::EINIT_FAILED, "Failed to create frame command pools");
		}
	}

	ev2::Result result = create_static_descriptor_pool(ctx, &frame->descriptor_pool);

	if (result != ev2::SUCCESS)
		return result;

	{
		VkSemaphoreCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};
		vk_result = vkCreateSemaphore(
			ctx->device, 
			&create_info, 
			nullptr, 
			&frame->image_available_sempahore
		);

		if (vk_result != VK_SUCCESS)
			return ev2::EINIT_FAILED;
	}

	result = init_frame_descriptor_set(ctx, frame);

	if (result != ev2::SUCCESS)
		return ev2::EINIT_FAILED;

	return ev2::SUCCESS;
}

static ev2::Result init_device_resources(const char * path, ev2::GfxContext *ctx)
{
	ev2::Result result = ev2::SUCCESS;

	//-----------------------------------------------------------------------------
	// important things
	
	ctx->max_frames_in_flight = 2;
	ctx->caps.max_workers = 2; 
	//std::max(
	//	std::min(std::thread::hardware_concurrency() - 1, 2U), 
	//	1U
	//);

	ctx->worker_pool.reset(new ThreadPool(ctx->caps.max_workers, "gfx_worker"));
	ctx->start_time_ns = 
		std::chrono::high_resolution_clock::now().time_since_epoch().count();

	ctx->last_frame_ts = platform::monotonic_clock_time();

	result = create_static_descriptor_pool(ctx, &ctx->static_descriptor_pool);

	if (result)
		return result;

	VkResult vk_result = VK_SUCCESS; 
	{
		VkSemaphoreTypeCreateInfo timeline_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.pNext = NULL,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue = 0,
		};

		VkSemaphoreCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = &timeline_info
		};

		vk_result = vkCreateSemaphore(
			ctx->device, &create_info, nullptr, &ctx->frame_semaphore);

		if (result)
			return result;
	}

	ctx->base_descriptor_set_layouts[EV2_BASE_SET_PER_FRAME] = 
		ev2::generate_per_frame_descriptor_set_layout(ctx);
	ctx->base_descriptor_set_layouts[EV2_BASE_SET_PER_PASS] = 
		ev2::generate_per_pass_descriptor_set_layout(ctx);
	ctx->base_descriptor_set_layouts[EV2_BASE_SET_BINDLESS] = 
		ev2::generate_bindless_descriptor_set_layout(ctx);

	for (uint32_t i = 0; i < EV2_BASE_SET_COUNT; ++i) {
		if (ctx->base_descriptor_set_layouts[i] == VK_NULL_HANDLE)
			return ev2::EINIT_FAILED;
	}

	for (uint32_t i = 0; i < EV2_BASE_SET_COUNT; ++i) {
		VkPipelineLayoutCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.flags = 0,
			.setLayoutCount = 1 + i, 
			.pSetLayouts = ctx->base_descriptor_set_layouts,
			.pushConstantRangeCount = 0,
			.pPushConstantRanges = nullptr,
		};

		vk_result = vkCreatePipelineLayout(
			ctx->device, 
			&create_info, 
			nullptr, 
			&ctx->base_pipeline_layouts[i]);
		
		if (vk_result != VK_SUCCESS) {
			result = ev2::EINIT_FAILED;
			break;
		}
	}

	if (result)
		return result;

	// Resource pools
	ctx->buffer_pool.reset(Pool<ev2::Buffer>::create());
	ctx->image_pool.reset(Pool<ev2::Image>::create());
	ctx->texture_pool.reset(Pool<ev2::Texture>::create());

	// Per-frame updated uniforms
	uint64_t ubo_offset_alignment = 
		ctx->caps.limits.minUniformBufferOffsetAlignment;

	ctx->transforms = GPUTTable<glm::mat4>(
		(size_t)ubo_offset_alignment,
		ev2::BUFFER_USAGE_TRANSFER_DST_BIT | 
		ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT
	);
	ctx->view_data = GPUTTable<ev2::ViewData>(
		(size_t)ubo_offset_alignment,
		ev2::BUFFER_USAGE_TRANSFER_DST_BIT | 
		ev2::BUFFER_USAGE_UNIFORM_BUFFER_BIT
	);


	for (uint32_t i = 0; i < ctx->max_frames_in_flight; ++i) {
		result = create_frame_context(ctx, &ctx->frames[i]);

		if (result)
			return result;
	}

	glm::mat4 proj_def = glm::mat4(1.f);
	glm::mat4 view_def = glm::mat4(1.f);

	ev2::ViewData viewdata = ev2::view_data_from_matrices(
		glm::value_ptr(proj_def), glm::value_ptr(view_def));

	ctx->default_view = EV2_HANDLE_CAST(View,ctx->view_data.add(viewdata));

	// Assets
	ctx->assets.reset(AssetTable::create(ctx, path));

	// Upload pools
	size_t upload_capacity = (1 << 9) * (1 << 20);
	size_t upload_alignment = 512;

	ctx->pool.reset(ev2::UploadPool::create(ctx, 
		ctx->graphics_family,
		upload_capacity, 
		upload_alignment, 
		(1 << 14)
	));

	return ev2::SUCCESS;
}


VkDescriptorSetLayout GfxContext::get_base_descriptor_set_layout(uint32_t set)
{
	return set < EV2_BASE_SET_COUNT ? base_descriptor_set_layouts[set] : VK_NULL_HANDLE;
}

VkPipelineLayout GfxContext::get_base_pipeline_layout(uint32_t set)
{
	return set < EV2_BASE_SET_COUNT ? base_pipeline_layouts[set] : VK_NULL_HANDLE;
}

bool GfxContext::assert_inside_frame()
{
	bool inside = get_current_frame()->index >= frame_counter;

	if (!inside) {
		log_error("Outside of frame!"); 
		abort();
	}

	return inside;
}

ev2::Result GfxContext::reset_swap_chain()
{
	vkDeviceWaitIdle(this->device);

	destroy_swap_chain(this, this->swap_chain);

	ev2::Result result = 
		create_swap_chain(this, desired_surface_width, desired_surface_height);

	if (result != ev2::SUCCESS)
		goto failure;

	result = create_swap_chain_targets(this);

	if (result != ev2::SUCCESS)
		goto failure;

	this->is_swap_chain_valid = true;

	return result;

failure:
	destroy_swap_chain(this, this->swap_chain);
	return result;
}

ev2::Result GfxContext::wait_for_frame(uint64_t frame_index)
{
	if (!frame_counter)
		return ev2::SUCCESS;

	uint64_t wait_value = 1 + frame_index;
	VkSemaphoreWaitInfo wait_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &frame_semaphore,
		.pValues = &wait_value,
	};

	VkResult result = vkWaitSemaphores(device, &wait_info, EV2_FRAME_TIMEOUT);

	if (result == VK_TIMEOUT) {
		log_warn("Frame %d timed out", frame_index);
		return ev2::TIMEOUT;
	} else if (result != VK_SUCCESS) {
		return ev2::EUNKNOWN;
	}

	return ev2::SUCCESS;  
}

//------------------------------------------------------------------------------
// FrameContext

ResourceSync *FrameContext::get_resource_sync(TaggedResource resource, VkQueue queue)
{
	auto [it, inserted] = sync_map.emplace(SyncKey{
		.resource = resource,
		.queue = queue 
	}, ResourceSync{});

	ResourceSync &sync = it->second;

	if (inserted) {
		VkSemaphoreTypeCreateInfo timeline_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.pNext = NULL,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue = 0,
		};

		VkSemaphoreCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = &timeline_info,
		};

		VkResult result = vkCreateSemaphore(ctx->device, &create_info, nullptr, &sync.semaphore);

		if (result != VK_SUCCESS)
			return nullptr;
	}

	return &sync;
}

void FrameContext::cull_unused_syncs()
{
	std::vector<SyncKey> old;
	for (auto &[key, sync] : sync_map) {
		const ResourceState *state = nullptr;
		switch (key.resource.type){
			case RESOURCE_TYPE_BUFFER:
				state = &ctx->get_buffer_unchecked(key.resource.to_buffer())->state;
				break;
			case RESOURCE_TYPE_IMAGE:
				state = &ctx->get_image_unchecked(key.resource.to_image())->state;
				break;
		}
		assert(state);

		if (state->last_used_by_frame < this->index) {
			vkDestroySemaphore(ctx->device, sync.semaphore, nullptr);
			old.push_back(key);
		}
	}

	for (const SyncKey &key : old)
		sync_map.erase(key);
}

//------------------------------------------------------------------------------
// Interface

GfxContext *create_context_for_vulkan(const char *path, 
								 const GfxContextVulkanInfo &params)
{
	if (!g_vk_instance) {
		log_error(
			"Vulkan is not initialized.  Try calling 'init_for_vulkan' first"
		);
		return nullptr;
	}
	if (!params.surface) {
		log_error(
			"VkSurfaceKHR is VK_NULL_HANDLE"
		);
		return nullptr;
	}

	stbi_set_flip_vertically_on_load(true);

	GfxContext *ctx = new GfxContext{};
	ctx->surface = params.surface;

	ctx->main_thread_id = std::this_thread::get_id();

	ev2::Result result = ev2::SUCCESS;

	VulkanOptions opts = {
		.deviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		}
	};

	result = pick_physical_device(ctx, opts);
	if (result)
		goto error;

	result = pick_logical_device(ctx, opts);
	if (result)
		goto error;

	result = create_allocator(ctx);
	if (result)
		goto error;

	log_info("initialized vulkan");

	result = init_device_resources(path, ctx);
	if (result)
		goto error;

	ctx->reset_swap_chain();
	
	return ctx;

error:
	delete ctx;
	return nullptr;
}

ev2::Result on_resize(GfxContext *ctx, uint32_t width, uint32_t height)
{
	uint32_t old_width = ctx->desired_surface_width;
	uint32_t old_height = ctx->desired_surface_width;

	if (old_width != width || old_height != height) {
		ctx->desired_surface_width = width;
		ctx->desired_surface_height = height;
		ctx->is_swap_chain_valid = false;
	}

	return ev2::SUCCESS;
}

ev2::Result init_for_vulkan(const ev2::VulkanInitOptions &opts)
{
	if (opts.enableValidationLayers && !checkValidationLayerSupport(opts)) {
		log_error("Validation layers required, but not available");
		return ev2::EINIT_FAILED;
    }

    VkApplicationInfo appInfo = {
    	.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    	.pApplicationName = "Hello Triangle",
    	.applicationVersion = VK_VERSION,
    	.pEngineName = "No Engine",
    	.engineVersion = VK_VERSION,
    	.apiVersion = VK_API_VERSION,
	};
    
    auto extensions = getRequiredExtensions(opts);

    VkInstanceCreateInfo createInfo = {
    	.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    	.pApplicationInfo = &appInfo,
    	.enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
    	.ppEnabledExtensionNames = extensions.data(),
	};

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
		.sType = 
			VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = 
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = 
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback,
		.pUserData = nullptr,
	};

    if (opts.enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(opts.validationLayerCount);
        createInfo.ppEnabledLayerNames = opts.validationLayers;

        populateDebugMessengerCreateInfo(debugCreateInfo);
        debugCreateInfo.pNext = nullptr;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    }
    else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext  = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &g_vk_instance) != VK_SUCCESS) {
        log_error("failed to create Vulkan instance!");
		return ev2::EINIT_FAILED;
    }

	if (opts.enableValidationLayers) {
		if (CreateDebugUtilsMessengerEXT(g_vk_instance, &debugCreateInfo, nullptr, &g_vk_messenger)) {
			log_error("Failed to create debug messenger!");
			return ev2::EINIT_FAILED;
		}
	}

    if ((false)) {
        uint32_t extensionCount = 0;
    
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> extension_props(extensionCount);

        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extension_props.data());

		std::stringstream msg; 
		msg << "available extensions:\n"; 
        for (const auto& extension : extension_props)
            msg << "\t" << extension.extensionName << "\n";
		
		log_info("%s", msg.str().c_str());
    }

	return ev2::SUCCESS;
}

VkInstance get_vulkan_instance()
{
	return g_vk_instance;
}

static void destroy_frame_context(FrameContext *frame)
{
	GfxContext *ctx = frame->ctx;

	for (TransientCommands &commands : frame->commands) {
		commands.destroy();
	}

	if (frame->descriptor_pool)
		vkDestroyDescriptorPool(ctx->device, frame->descriptor_pool, nullptr);

	if (frame->image_available_sempahore)
		vkDestroySemaphore(ctx->device, frame->image_available_sempahore, nullptr);

	for (auto &[key, sync] : frame->sync_map) {
		vkDestroySemaphore(ctx->device, sync.semaphore, nullptr);
	}
}

void destroy_context(GfxContext *ctx)
{
	if (ctx->is_inside_frame()) {
		log_error("destroy_context called inside of frame, aborting");
		abort();
	}

	vkDeviceWaitIdle(ctx->device);

	for (FrameContext &frame : ctx->frames) {
		destroy_frame_context(&frame);
	}

	ctx->assets.reset();
	ctx->pool.reset();

	ctx->transforms.destroy(ctx);
	ctx->view_data.destroy(ctx);

	destroy_swap_chain(ctx, ctx->swap_chain);

	delete ctx;
}

Result _set_error_internal(Result result, const char *file, int line, const char *msg, ...)
{
	assert(result != SUCCESS);

	va_list args;
    va_start(args, msg);

	_log_function(LOG_LEVEL_ERROR, file, line, msg, args);

    va_end(args);

	return result;
}

};

#ifdef EV2_IMGUI_COMPATIBILITY

#include <imgui.h>
#include "backends/imgui_impl_vulkan.h"

namespace ev2 {
void populate_imgui_vulkan_init_info(GfxContext *ctx, ImGui_ImplVulkan_InitInfo *info)
{
	std::vector<VkDynamicState> internal_dynamic_states = get_dynamic_states(ctx);

	ImVector<VkDynamicState> dynamic_states;
	dynamic_states.reserve(dynamic_states.size());
	for (VkDynamicState state : internal_dynamic_states)
		dynamic_states.push_back(state);

	*info = ImGui_ImplVulkan_InitInfo{
		.ApiVersion = VK_API_VERSION,
		.Instance = g_vk_instance,
		.PhysicalDevice = ctx->physicalDevice,
		.Device = ctx->device,
		.QueueFamily = ctx->graphics_family->index,
		.Queue = ctx->graphics_queue->queue,
		.DescriptorPool = VK_NULL_HANDLE,
		.DescriptorPoolSize = 16,
		.MinImageCount = ctx->max_frames_in_flight,
		.ImageCount = ctx->max_frames_in_flight,
		.PipelineCache = VK_NULL_HANDLE,
		.PipelineInfoMain = ImGui_ImplVulkan_PipelineInfo{
			.PipelineRenderingCreateInfo = get_swapchain_rendering_info(ctx),
		},
		.UseDynamicRendering = true,
		.Allocator = nullptr,
		.CheckVkResultFn = nullptr,
		.MinAllocationSize = 1024 * 1024,
	};
}
}

#endif

