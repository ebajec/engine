#include "ev2/context.h"
#include "ev2/pipeline.h"
#include "ev2/asset.h"

#include "GLFW/glfw3.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <ev2/utils/log.h>

struct App {
	GLFWwindow *win;
	ev2::GfxContext *ctx;

	int width = 500;
	int height = 500;
	bool enable_validation_layers = false;
	bool needs_resize = true;

} g_;

#if defined (__linux__) || defined(__APPLE__)

// posix
#include <unistd.h>

static std::atomic_int g_should_close = false;;
void handle_sigint(int sig)
{
	(void)sig;
	g_should_close = true;
	log_info("received SIGINT");
}
#endif

static inline int add_project_mounts(ev2::GfxContext *ctx)
{
	struct Mount {
		const char *name;
		const char *path;
	};

	int failures = 0;
#ifdef EV2_PROJECT_MOUNTS
	static constexpr Mount mounts[] = { EV2_PROJECT_MOUNTS };

	for (const Mount &mount : mounts) {
		if (ev2::add_mount(ctx, mount.name, mount.path) != ev2::SUCCESS) {
			log_warn("Failed to mount %s:// at %s", mount.name, mount.path);
			++failures;
		}
	}
#endif
	return failures;
}

static void print_glfw_platform()
{
	int p = glfwGetPlatform(); 

	const char *s;
	switch (p) {
		case GLFW_PLATFORM_X11: s = "X11"; break;
		case GLFW_PLATFORM_WAYLAND: s = "Wayland"; break;
		case GLFW_PLATFORM_WIN32: s = "Win32"; break;
	}
	printf("\x1b[32mGLFW is running on %s\x1b[0m\n", s); 
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (app->width != width || app->height != height)
		g_.needs_resize = true;
	

	app->width = width;
	app->height = height;
}
ev2::GfxContext *ev2_init_from_glfw(GLFWwindow *win)
{
	if (!glfwVulkanSupported()) {
		log_error("Vulkan not supported by GLFW");
		return nullptr;
	}

	std::vector<const char *> validationLayers = {
		"VK_LAYER_KHRONOS_validation",
	};

	uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;

    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(
		glfwExtensions, glfwExtensions + glfwExtensionCount);

	ev2::VulkanInitOptions init_opts = {
		.validationLayers = validationLayers.data(),
		.validationLayerCount = validationLayers.size(),
		.instanceExtensions = extensions.data(),
		.instanceExtensionCount = extensions.size(),
		.enableValidationLayers = g_.enable_validation_layers,
	};

	ev2::Result ev2_res = ev2::init_for_vulkan(init_opts);
	if (ev2_res != ev2::SUCCESS)
		return nullptr;

	VkInstance vk_instance = ev2::get_vulkan_instance(); 
	VkSurfaceKHR surface = VK_NULL_HANDLE;

	VkResult vk_res = glfwCreateWindowSurface(vk_instance, g_.win, nullptr, &surface); 

	if (vk_res < VK_SUCCESS) {
        log_error("failed to create window surface!");
		return nullptr;
    }

	ev2::GfxContextVulkanInfo vulkan_params = {
		.surface = surface		
	};

	ev2::GfxContext *ctx = ev2::create_context_for_vulkan(vulkan_params);
	add_project_mounts(ctx);
	std::signal(SIGINT, handle_sigint);

	return ctx;
}
	
int init(int argc, char *argv[])
{
	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i],"--wayland")) {
			glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
		} else if (!strcmp(argv[i],"--x11")) {
			glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
		} else if (!strcmp(argv[i],"--vk-validation"))
			g_.enable_validation_layers = true;
	}

	if (!glfwInit()) {
		log_error("Failed to initialize GLFW!");
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);

	int width = g_.width;
	int height = g_.height;

	print_glfw_platform();

	g_.win = glfwCreateWindow(width, height, "test", nullptr, nullptr);

	if (!g_.win) {
		log_error("Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	g_.ctx = ev2_init_from_glfw(g_.win);

	glfwSetWindowUserPointer(g_.win, &g_);
	glfwSetFramebufferSizeCallback(g_.win, &framebuffer_size_callback);
	
	return 0;
}

int main(int argc, char *argv[])
{
	int result = 0;
	if ((result = init(argc, argv)); result < 0)
		return result;

	ev2::GfxContext * ctx = g_.ctx;

	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, "minimal://pipeline/test.yaml");

	if (!pipeline.is_valid())
		return EXIT_FAILURE;

	ev2::ViewID view = ev2::create_view(ctx, nullptr, nullptr);

	float view_mat[4][4] = {
		{1,0,0,0},
		{0,1,0,0},
		{0,0,1,0},
		{0,0,0,1}
	};
	float proj_mat[4][4] = {
		{1,0,0,0},
		{0,1,0,0},
		{0,0,1,0},
		{0,0,0,1}
	};
	
	while (!glfwWindowShouldClose(g_.win) && !g_should_close) {
		glfwPollEvents();

		if (g_.needs_resize) {
			ev2::resize_swapchain(ctx, g_.width, g_.height);
			g_.needs_resize = false;
		}

		ev2::update_view(ctx, view, &view_mat[0][0], &proj_mat[0][0]);

		ev2::begin_frame(ctx);

		ev2::GfxPassInfo pass_info = {
			.target = {}, // passing a null target renders to the screen
			.view = view,
			.viewport = ev2::Rect{0, 0, (uint32_t)g_.width, (uint32_t)g_.height},
		};

		ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
		ev2::cmd_bind_gfx_pipeline(pass, pipeline);
		ev2::cmd_custom(pass, [](VkCommandBuffer cmds) {
			vkCmdDraw(cmds, 6, 1, 0, 0);
		});
		ev2::end_pass(ctx, pass);

		ev2::end_frame(ctx);
	}

	ev2::destroy_context(ctx);

	return result;
}
