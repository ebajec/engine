#include "ev2/context.h"

#include "def_vulkan.h"
#include "context_impl.h"

#include "utils/asset_table.h"
#include "utils/pool.h"

#include "stb_image.h"

#include <algorithm>
#include <sstream>
#include <set>
#include <optional>

//------------------------------------------------------------------------------

VkInstance g_vk_instance = VK_NULL_HANDLE;

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
    	log_info("validation layer: %s", pCallbackData->pMessage);
		break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    	log_warn("validation layer: %s", pCallbackData->pMessage);
		break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    	log_error("validation layer: %s", pCallbackData->pMessage);
		break;
	default:
		break;
	}
    return VK_FALSE;
}

static void init_gl(ev2::GfxContext *ctx)
{
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);       // makes callback synchronous

	glDebugMessageCallback([]( GLenum source,
							  GLenum type,
							  GLuint id,
							  GLenum severity,
							  GLsizei length,
							  const GLchar* message,
							  const void* userParam )
	{
		const char *fmt = 
			"GL DEBUG: [%u]\n"
			"    Source:   0x%x\n"
			"    Type:     0x%x\n"
			"    Severity: 0x%x\n"
			"    Message:  %s";

		if (type == GL_DEBUG_TYPE_ERROR) {
			log_error(fmt, id, source, type, severity, message);
		} else {
			log_info(fmt, id, source, type, severity, message);
		}
	}, nullptr);

	glDebugMessageControl(
		GL_DONT_CARE,          // source
		GL_DONT_CARE,          // type
		GL_DONT_CARE,          // severity
		0, nullptr,            // count + list of IDs
		GL_TRUE);              // enable

    glEnable(GL_MULTISAMPLE);

	const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);

	log_info(
		"Successfully initiailzed OpenGL\n"
		"\tRenderer: %s\n"
		"\tOpenGL version: %s\n"
		"\tVendor: %s\n"
		"\tGLSL version: %s",
		renderer, version, vendor, glslVersion
	);
}

//------------------------------------------------------------------------------
// Vulkan initialization helpers

static bool checkValidationLayerSupport(const ev2::InitOptionsVulkan &opts)
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
	const ev2::InitOptionsVulkan &opts)
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
    createInfo.sType = 
		VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = 
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = 
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr; // Optional
    createInfo.pNext = nullptr;
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

static void create_queue_family(VkDevice dev, 
						   const VkQueueFamilyProperties &props, 
						   uint32_t index, QueueFamily *family)
{
	family->queue_count = props.queueCount;
	family->queues.reset(new QueueSubmitter[props.queueCount]);
	family->index = index;

	for (uint32_t i = 0; i < props.queueCount; ++i) {
    	vkGetDeviceQueue(dev, index, i, &family->queues[i].queue);
	}
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

    for (const VkPhysicalDevice& device : devices) {
        if (isDeviceSuitable(ctx->surface, device, opts)) {
            ctx->physicalDevice = device;
            break;
        }
    }

    if (ctx->physicalDevice == VK_NULL_HANDLE) {
        log_error("failed to find a suitable GPU!");
		return ev2::EINIT_FAILED;
    }

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(ctx->physicalDevice, &properties);

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

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueFamily,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority,
		};

        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    // device features
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = static_cast<uint32_t>(
			queueCreateInfos.size()),
		.pQueueCreateInfos = queueCreateInfos.data(),
		.enabledExtensionCount = static_cast<uint32_t>(
			opts.deviceExtensions.size()),
		.ppEnabledExtensionNames = opts.deviceExtensions.data(),
		.pEnabledFeatures = &deviceFeatures,
	};

	VkResult result = vkCreateDevice(ctx->physicalDevice, 
								  &createInfo, nullptr, &ctx->device); 
    if (result != VK_SUCCESS) {
        log_error("failed to create logical device!");
		return ev2::EINIT_FAILED;
    }

	std::vector<VkQueueFamilyProperties> queue_props;
	uint32_t queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queue_count, nullptr);
	queue_props.resize(queue_count);
	vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queue_count, queue_props.data());

	create_queue_family(ctx->device, 
					   queue_props[*indices.graphicsFamily], 
					   *indices.graphicsFamily, 
					   &ctx->graphics_family);

	create_queue_family(ctx->device, 
					   queue_props[*indices.transferFamily], 
					   *indices.transferFamily, 
					   &ctx->transfer_family);

	create_queue_family(ctx->device, 
					   queue_props[*indices.computeFamily], 
					   *indices.computeFamily, 
					   &ctx->compute_family);

    vkGetDeviceQueue(ctx->device, indices.presentFamily.value(), 0,
					 &ctx->presentQueue);

	return ev2::SUCCESS;
}

static VkResult create_swap_chain_views(ev2::GfxContext *ctx)
{
	ctx->swapChain.image_views.resize(ctx->swapChain.images.size());

    for (size_t i = 0; i < ctx->swapChain.images.size(); ++i) {
        VkImageViewCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = ctx->swapChain.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = ctx->swapChain.image_format,
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
								   &ctx->swapChain.image_views[i]);
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
		imageCount = ++imageCount > maxImageCount ? maxImageCount : imageCount;

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
    createInfo.oldSwapchain = VK_NULL_HANDLE;

	VkResult result = VK_SUCCESS;

	result = vkCreateSwapchainKHR(ctx->device, &createInfo, nullptr, 
							   &ctx->swapChain.swapchain); 

    if (result) {
        log_error("failed to create swap chain!");
		return ev2::ERESIZE_FAILED;
    }

    result = vkGetSwapchainImagesKHR(ctx->device, 
									 ctx->swapChain.swapchain, &imageCount, nullptr);
    if (result)
		return ev2::ERESIZE_FAILED;

    ctx->swapChain.images.resize(imageCount);
    
	result = vkGetSwapchainImagesKHR(ctx->device, ctx->swapChain.swapchain, &imageCount, 
							ctx->swapChain.images.data());
	if (result)
		return ev2::ERESIZE_FAILED;

    ctx->swapChain.image_format = surfaceFormat.format;
    ctx->swapChain.extent = extent;

	result = create_swap_chain_views(ctx);

	if (result)
		return ev2::ERESIZE_FAILED;

	return ev2::SUCCESS;
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

static ev2::Result init_frame_context(ev2::GfxContext *ctx,
									   FrameContext *frame)
{
	frame->ubo = ev2::create_buffer(ctx, sizeof(GPUFramedata), 
									ev2::BUFFER_USAGE_UNIFORM_BUFFER_BIT | 
									ev2::BUFFER_USAGE_TRANSFER_DST_BIT);

	uint32_t pool_count = 1 + ctx->max_workers;
	frame->command_pools.resize(pool_count);

	VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = 0,
		.queueFamilyIndex = ctx->graphics_family.index,
	};

	for (uint32_t i = 0; i < pool_count; ++i) {
		vkCreateCommandPool(ctx->device, &pool_info, nullptr, &frame->command_pools[i]);
	}

	return ev2::SUCCESS;
}

static ev2::Result init_device_resources(const char * path, ev2::GfxContext *ctx)
{
	//-----------------------------------------------------------------------------
	// important things
	
	ctx->max_frames_in_flight = 1;
	ctx->max_workers = std::max(std::thread::hardware_concurrency() - 1, 1U);

	ctx->worker_pool.reset(new ThreadPool(ctx->max_workers, "gfx_worker"));
	ctx->start_time_ns = 
		std::chrono::high_resolution_clock::now().time_since_epoch().count();


	// Resource pools
	ctx->buffer_pool.reset(ResourcePool<ev2::Buffer>::create());
	ctx->image_pool.reset(ResourcePool<ev2::Image>::create());
	ctx->texture_pool.reset(ResourcePool<ev2::Texture>::create());

	// Per-frame updated uniforms
	GLint64 ubo_offset_alignment;
	glGetInteger64v(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_offset_alignment);

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

	ev2::Result result = ev2::SUCCESS;

	for (uint32_t i = 0; i < ctx->max_frames_in_flight; ++i) {
		result = init_frame_context(ctx, &ctx->frames[i]);

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
		0,
		upload_capacity, 
		upload_alignment, 
		(1 << 14)
	));

	return ev2::SUCCESS;
}


namespace ev2 {

VkCommandPool GfxContext::get_command_pool(
	uint32_t frame, 
	uint32_t worker,
	uint32_t queue
)
{
	assert(frame < this->max_frames_in_flight);
	assert(worker < this->max_workers);
	assert(queue == 0);

	return frames[frame].command_pools[worker];
}

VkCommandPool GfxContext::get_current_frame_command_pool(
	uint32_t queue_family_index
)
{
	FrameContext *frame = this->get_current_frame();
	std::thread::id tid = std::this_thread::get_id();
	uint32_t worker = this->worker_pool->get_pool_index(tid);

	if (worker == UINT32_MAX) {
		log_error("Command pool only available on main thread or graphics worker");
		return VK_NULL_HANDLE;
	}

	return frame->command_pools[worker];
}

ev2::Result GfxContext::wait_for_frame_completion(FrameContext *frame)
{
	VkSemaphore wait_semaphore = get_graphics_timeline_semaphore();

	VkSemaphoreWaitInfo wait_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &wait_semaphore,
		.pValues = &frame->wait_value,
	};

	VkResult result = vkWaitSemaphores(device, &wait_info, EV2_FRAME_TIMEOUT);

	if (result == VK_TIMEOUT) {
		log_warn("Frame %d timed out", current_frame_index);
		return ev2::TIMEOUT;
	} else if (result != VK_SUCCESS) {
		return ev2::EUNKNOWN;
	}

	return ev2::SUCCESS;  
}


//------------------------------------------------------------------------------
// Interface

GfxContext *create_context_for_vulkan(const char *path, 
								 const GfxContextInfoVulkan &params)
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
	
	return ctx;

error:
	delete ctx;
	return nullptr;
}

ev2::Result init_for_vulkan(const ev2::InitOptionsVulkan &opts)
{
	if (opts.enableValidationLayers && !checkValidationLayerSupport(opts)) {
		log_error("Validation layers required, but not available");
		return ev2::EINIT_FAILED;
    }

    VkApplicationInfo appInfo = {
    	.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    	.pApplicationName = "Hello Triangle",
    	.applicationVersion = VK_API_VERSION,
    	.pEngineName = "No Engine",
    	.engineVersion = VK_API_VERSION,
    	.apiVersion = VK_API_VERSION,
	};
    
    auto extensions = getRequiredExtensions(opts);

    VkInstanceCreateInfo createInfo = {
    	.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    	.pApplicationInfo = &appInfo,
    	.enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
    	.ppEnabledExtensionNames = extensions.data(),
	};

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

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

void destroy_context(GfxContext *ctx)
{
	ctx->assets.reset();
	ctx->pool.reset();

	ctx->transforms.destroy(ctx);
	ctx->view_data.destroy(ctx);

	delete ctx;
}

};
