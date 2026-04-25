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
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
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

static void init_gl(ev2::Context *dev)
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
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) break;

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

//------------------------------------------------------------------------------
// Vulkan initialization

static ev2::Result pick_physical_device(ev2::Context *dev, 
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
        if (isDeviceSuitable(dev->surface, device, opts)) {
            dev->physicalDevice = device;
            break;
        }
    }

    if (dev->physicalDevice == VK_NULL_HANDLE) {
        log_error("failed to find a suitable GPU!");
		return ev2::EINIT_FAILED;
    }

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(dev->physicalDevice, &properties);

	std::stringstream msg;

	msg << "Physical device: \n"
		<< "\t'" << properties.deviceName << "'" << "\n"
		<< "\tAPI Version: " << properties.apiVersion << "\n"
		<< "\tDriver Version: " << properties.driverVersion << "\n"
	;

	log_info("%s", msg.str().c_str());

	return ev2::SUCCESS;
}

static ev2::Result pick_logical_device(ev2::Context *dev, 
									   const VulkanOptions &opts)
{
	QueueFamilyIndices indices = findQueueFamilies(dev->physicalDevice, 
												dev->surface);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
		indices.graphicsFamily.value(), indices.presentFamily.value()};

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

	VkResult result = vkCreateDevice(dev->physicalDevice, 
								  &createInfo, nullptr, &dev->device); 
    if (result != VK_SUCCESS) {
        log_error("failed to create logical device!");
		return ev2::EINIT_FAILED;
    }

    vkGetDeviceQueue(dev->device, indices.graphicsFamily.value(), 0, 
					 &dev->graphicsQueue);
    vkGetDeviceQueue(dev->device, indices.presentFamily.value(), 0,
					 &dev->presentQueue);

	return ev2::SUCCESS;
}

static VkResult create_swap_chain_views(ev2::Context *dev)
{
	dev->swapChainImageViews.resize(dev->swapChainImages.size());

    for (size_t i = 0; i < dev->swapChainImages.size(); ++i) {
        VkImageViewCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = dev->swapChainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = dev->swapChainImageFormat,
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

		VkResult res = vkCreateImageView(dev->device, 
								   &createInfo, nullptr, 
								   &dev->swapChainImageViews[i]);
        if (res != VK_SUCCESS) {
            log_error("failed to create swap chain image views!");
			return res;
        }
    }
	return VK_SUCCESS;
}

static ev2::Result create_swap_chain(ev2::Context *dev, uint32_t w, uint32_t h)
{
	SwapChainSupportDetails swapChainSupport = querySwapChainSupport(
		dev->physicalDevice, dev->surface);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(w, h, swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount;
    uint32_t maxImageCount = swapChainSupport.capabilities.maxImageCount;

    if (maxImageCount) 
		imageCount = ++imageCount > maxImageCount ? maxImageCount : imageCount;

    VkSwapchainCreateInfoKHR createInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = dev->surface,
		.minImageCount = imageCount,
		.imageFormat = surfaceFormat.format,
		.imageColorSpace = surfaceFormat.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	};

    QueueFamilyIndices indices = findQueueFamilies(dev->physicalDevice, 
												   dev->surface);
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

	result = vkCreateSwapchainKHR(dev->device, &createInfo, nullptr, &dev->swapChain); 

    if (result) {
        log_error("failed to create swap chain!");
		return ev2::ERESIZE_FAILED;
    }

    result = vkGetSwapchainImagesKHR(dev->device, dev->swapChain, &imageCount, nullptr);
    if (result)
		return ev2::ERESIZE_FAILED;

    dev->swapChainImages.resize(imageCount);
    
	result = vkGetSwapchainImagesKHR(dev->device, dev->swapChain, &imageCount, 
							dev->swapChainImages.data());
	if (result)
		return ev2::ERESIZE_FAILED;

    dev->swapChainImageFormat = surfaceFormat.format;
    dev->swapChainExtent = extent;

	result = create_swap_chain_views(dev);

	if (result)
		return ev2::ERESIZE_FAILED;

	return ev2::SUCCESS;
}

static ev2::Result create_allocator(ev2::Context *dev)
{
	VmaAllocatorCreateInfo create_info = {
		.flags = 0,
		.physicalDevice = dev->physicalDevice,
		.device = dev->device,
		.instance = g_vk_instance,
		.vulkanApiVersion = VK_API_VERSION
	};

	VkResult res = vmaCreateAllocator(&create_info, &dev->allocator);

	if (res != VK_SUCCESS) {
		log_error("Failed to create Vulkan memory allocator");
		return ev2::EINIT_FAILED;
	}
	return ev2::SUCCESS;
}

static ev2::Result init_device_resources(const char * path, ev2::Context *dev)
{
	dev->buffer_pool.reset(ResourcePool<ev2::Buffer>::create());
	dev->image_pool.reset(ResourcePool<ev2::Image>::create());
	dev->texture_pool.reset(ResourcePool<ev2::Texture>::create());

	size_t upload_capacity = (1 << 9) * (1 << 20);
	size_t upload_alignment = 512;

	dev->pool.reset(ev2::UploadPool::create(dev, 
		upload_capacity, 
		upload_alignment, 
		(1 << 14)
	));

	GLint64 ubo_offset_alignment;
	glGetInteger64v(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_offset_alignment);

	dev->transforms = GPUTTable<glm::mat4>((size_t)ubo_offset_alignment);
	dev->view_data = GPUTTable<ev2::ViewData>((size_t)ubo_offset_alignment);

	dev->assets.reset(AssetTable::create(dev, path));

	dev->frame.ubo = ev2::create_buffer(dev, sizeof(ev2::GPUFramedata), ev2::MAP_WRITE);

	dev->start_time_ns = 
		std::chrono::high_resolution_clock::now().time_since_epoch().count();

	glm::mat4 proj_def = glm::mat4(1.f);
	glm::mat4 view_def = glm::mat4(1.f);

	ev2::ViewData viewdata = ev2::view_data_from_matrices(
		glm::value_ptr(proj_def), glm::value_ptr(view_def));

	dev->default_view = EV2_HANDLE_CAST(View,dev->view_data.add(viewdata));

	return ev2::SUCCESS;
}

//------------------------------------------------------------------------------
// Interface

namespace ev2 {

Context *create_context_for_vulkan(const char *path, 
								 const ContextInfoVulkan &params)
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

	Context *dev = new Context{};
	dev->surface = params.surface;

	ev2::Result result = ev2::SUCCESS;

	//init_gl(dev);

	VulkanOptions opts = {
		.deviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		}
	};

	result = pick_physical_device(dev, opts);
	if (result)
		goto error;

	result = pick_logical_device(dev, opts);
	if (result)
		goto error;

	result = create_allocator(dev);
	if (result)
		goto error;

	log_info("initialized vulkan");

	result = init_device_resources(path, dev);
	if (result)
		goto error;
	
	return dev;

error:
	delete dev;
	return nullptr;
}

ev2::Result init_for_vulkan(const ev2::InitOptionsVulkan &opts)
{
	if (opts.enableValidationLayers && !checkValidationLayerSupport(opts)) {
		log_error("Validation layers required, but not available");
		return ev2::EINIT_FAILED;
    }

    VkApplicationInfo appInfo{};
    
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_API_VERSION;
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_API_VERSION;
    appInfo.apiVersion = VK_API_VERSION;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions(opts);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

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
        log_error(" failed to create Vulkan instance!");
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

void destroy_context(Context *dev)
{
	dev->assets.reset();
	dev->pool.reset();

	dev->transforms.destroy(dev);
	dev->view_data.destroy(dev);

	delete dev;
}

};
