#include "app.h"

#include "imgui_internal.h"

#include "texture_viewer.h"

#include "ev2/utils/log.h"

#include "ev2/context.h"
#include "ev2/pipeline.h"

#include <ev2/imgui/inspector.h>

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_vulkan.h"

// STL / libc
#include <cstdio>
#include <csignal>
#include <atomic>

#ifdef __linux__

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

#ifdef EV2_BACKEND_OPENGL
int init_gl_context(GLFWwindow *window)
{
	glfwMakeContextCurrent(window);

	#ifndef NDEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
	#endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr,"Failed to initialize GLAD\n");
		return EXIT_FAILURE;
    }

	return EXIT_SUCCESS;
}
#endif

void print_glfw_platform()
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

App::App(int width, int height, const char *title)
{
	win.width = width;
	win.height = height;
	win.title = title;
}

void App::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
#ifdef ENABLE_IMGUI
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
#endif
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) 
	{
		if (app->input.mouse_mode == GLFW_CURSOR_DISABLED) {
			app->input.mouse_mode = GLFW_CURSOR_NORMAL;
		} else  {
			app->input.mouse_mode = GLFW_CURSOR_DISABLED;
		}

		glfwSetInputMode(window, GLFW_CURSOR, app->input.mouse_mode);
	}

	for (const key_callback_t callback : app->key_callbacks) {
		callback(key, scancode, action, mods);
	}
}
void App::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
#ifdef ENABLE_IMGUI
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
#endif
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
		app->input.left_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_RELEASE) {
		app->input.left_mouse_pressed = false;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS) {
		app->input.right_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_RELEASE) {
		app->input.right_mouse_pressed = false;
	}
}
void App::scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
#ifdef ENABLE_IMGUI
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
#endif
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	app->input.scroll.x += xoffset;
	app->input.scroll.y += yoffset;

	app->input.scroll_delta.x = xoffset;
	app->input.scroll_delta.y = yoffset;
}
void App::cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
#ifdef ENABLE_IMGUI
	ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
#endif
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	glm::dvec2 pos = glm::dvec2(xpos, ypos);
}
void App::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (width != app->win.width || height != app->win.height) {
		app->win.width = width;
		app->win.height = height;

		app->input.needs_resize = true;
	}
}

void App::update_input()
{
	std::swap(input.mouse_pos[0], input.mouse_pos[1]);
	glfwGetCursorPos(win.ptr, &input.mouse_pos[0].x, &input.mouse_pos[0].y);

#ifdef ENABLE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	input.mouse_in_gui = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || io.WantCaptureMouse;
#endif
}

int App::resize(int width, int height)
{
	glfwSetWindowSize(win.ptr, width, height);

	win.width = width;
	win.height = height;

	return ev2::on_resize(ctx, width, height) == ev2::SUCCESS ? 
		App::OK : App::ERROR;
}

int App::begin_frame()
{
	if (glfwWindowShouldClose(win.ptr))
		return SHOULD_CLOSE;
	if (g_should_close) {
		glfwSetWindowShouldClose(win.ptr, true);
		return SHOULD_CLOSE;
	}

	int result = OK;

	// I find it nicer to have ImGui captured wherever I need it to between frames
	if (frame_counter++) {
	} else {
		input.t0 = glfwGetTime();
	}

#ifdef ENABLE_IMGUI
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
#endif

	input.t1 = input.t0;
	input.t0 = glfwGetTime();
	input.dt = input.t0 - input.t1;
	input.scroll_delta = glm::vec2(0);

	glfwPollEvents();

	update_input();

	if (input.needs_resize) {
		if ((result = resize(win.width, win.height)) != App::OK)
			return result;

		input.needs_resize = false;
	}

#ifdef ENABLE_IMGUI
	imgui();
#endif

	ev2::Result ev2_result = ev2::begin_frame(ctx);
	if (ev2_result != ev2::SUCCESS)
		return App::ERROR;

	std::vector<ev2::ImageID> delete_list;

	for (const auto&[image, viewer] : image_viewers) {
		if (viewer->update(ctx) == App::SHOULD_CLOSE) {
			delete_list.push_back(image);
		}
	}
	
	for (ev2::ImageID image : delete_list) {
		close_image_viewer(image);
	}

	return result;
}

int App::end_frame()
{
	for (const auto &[image, viewer] : image_viewers) {
		viewer->render(ctx);
	}

#ifdef ENABLE_IMGUI
	ImGui::Render();
	ImDrawData *draw_data = ImGui::GetDrawData();
#endif 
	ev2::PassID gui_pass = ev2::begin_gfx_pass(
		ctx, 
		{}, {}, 
		ev2::Rect{0,0,(uint32_t)win.width, (uint32_t)win.height}
	);

	for (const auto[image, _] : imgui_images) {
		ev2::cmd_use_image(gui_pass, image, ev2::USAGE_SAMPLED_GRAPHICS);
	}

	ev2::cmd_custom(gui_pass, [draw_data](VkCommandBuffer cmds) {
		ImGui_ImplVulkan_RenderDrawData(draw_data, cmds);
	});
	ev2::end_pass(ctx, gui_pass);

	imgui_images.clear();

	ev2::end_frame(ctx);
	return App::OK;
}

void App::acquire_image_for_gui(ev2::ImageID image)
{
	auto [it, inserted] = imgui_images.emplace(image, VK_NULL_HANDLE);

	if (!inserted)
		return;
}

void App::release_image_for_gui(ev2::ImageID image)
{
	imgui_images.erase(image);
}

int App::initialize(int argc, char *argv[])
{
	bool enable_validation_layers = false;

	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i],"--wayland")) {
			glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
		} else if (!strcmp(argv[i],"--x11")) {
			glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
		} else if (!strcmp(argv[i],"--vk-validation"))
			enable_validation_layers = true;
	}

	if (!glfwInit()) {
		log_error("Failed to initialize GLFW!");
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);

	print_glfw_platform();

	win.ptr = glfwCreateWindow(win.width, win.height, win.title, nullptr, nullptr);

	if (!win.ptr) {
		log_error("Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	if (!glfwVulkanSupported()) {
		log_error("Vulkan not supported by GLFW");
		return EXIT_FAILURE;
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
		.enableValidationLayers = enable_validation_layers,
	};

	ev2::Result ev2_res = ev2::init_for_vulkan(init_opts);
	if (ev2_res != ev2::SUCCESS)
		return EXIT_FAILURE;

	VkInstance vk_instance = ev2::get_vulkan_instance(); 
	VkSurfaceKHR surface = VK_NULL_HANDLE;

	VkResult vk_res = glfwCreateWindowSurface(vk_instance, win.ptr, nullptr, &surface); 

	if (vk_res < VK_SUCCESS) {
        log_error("failed to create window surface!");
		return EXIT_FAILURE;
    }

	ev2::GfxContextVulkanInfo vulkan_params = {
		.surface = surface		
	};

	ctx = ev2::create_context_for_vulkan(RESOURCE_PATH, vulkan_params);

#ifdef ENABLE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= 
		ImGuiConfigFlags_NavEnableKeyboard |
		ImGuiConfigFlags_DockingEnable;
	io.FontGlobalScale = 1.0f; 

	// Set ImGui style
	ImGui::StyleColorsDark();
	if (!ImGui_ImplGlfw_InitForVulkan(win.ptr, true)) {
		log_error("Failed to initialize imgui for glfw");
		return EXIT_FAILURE;
	}

	ImGui_ImplVulkan_InitInfo init_info;
	ev2::populate_imgui_vulkan_init_info(ctx, &init_info);

	if (!ImGui_ImplVulkan_Init(&init_info)) {
		log_error("Failed to initialize imgui for vulkan");
		return EXIT_FAILURE;
	}

	ImPlot::CreateContext();
#endif

	ev2::imgui::set_image_viewer_close_callback(this, image_viewer_close_callback);
	ev2::imgui::set_image_viewer_open_callback(this, image_viewer_open_callback);

	glfwSetKeyCallback(win.ptr,&App::key_callback);
	glfwSetMouseButtonCallback(win.ptr,&App::mouse_button_callback);
	glfwSetScrollCallback(win.ptr,&App::scroll_callback);
	glfwSetCursorPosCallback(win.ptr,&App::cursor_pos_callback);
	glfwSetFramebufferSizeCallback(win.ptr, &App::framebuffer_size_callback);
	glfwSetWindowUserPointer(win.ptr, this);

	std::signal(SIGINT, handle_sigint);

	return EXIT_SUCCESS;
}

void App::terminate()
{
#ifdef ENABLE_IMGUI
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	ImPlot::DestroyContext();
#endif

	ev2::destroy_context(ctx);

	glfwDestroyWindow(win.ptr);
	glfwTerminate();
}

static void build_default_layout(ImGuiID dockspaceID)
{
	// Check FIRST, before creating anything
	if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr)
	{
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetMainViewport()->WorkSize);

		ImGuiID dockMainID = dockspaceID;
		ImGuiID dockLeftID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Left, 0.25f, nullptr, &dockMainID);
		ImGuiID dockDownID = ImGui::DockBuilderSplitNode(dockLeftID, ImGuiDir_Down, 0.5f, nullptr, &dockLeftID);

		ImGui::DockBuilderDockWindow("Editor", dockLeftID);
		ImGui::DockBuilderDockWindow(ev2::imgui::INSPECTOR_PANEL_NAME, dockDownID);
		ImGui::DockBuilderFinish(dockspaceID);
	}
    ImGui::DockSpace(dockspaceID, ImVec2(0, 0), ImGuiDockNodeFlags_None);
}

void App::setup_root_dockspace()
{
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImVec2 work_pos  = vp->WorkPos;
	ImVec2 work_size = vp->WorkSize;

	ImGui::SetNextWindowPos(work_pos);
	ImGui::SetNextWindowSize(work_size);
	ImGui::SetNextWindowViewport(vp->ID);

	ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("EditorRoot", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceID = ImGui::GetID("EditorRootDockspace");

	root_dockspace = dockspaceID;

	// Setup initial layout
	build_default_layout(dockspaceID);
    ImGui::End();
}
void App::image_viewer_open_callback(void *usr, ev2::ImageID image)
{
	App *app = static_cast<App*>(usr);
	app->open_image_viewer(image);
}
void App::image_viewer_close_callback(void *usr, ev2::ImageID image)
{
	App *app = static_cast<App*>(usr);
	app->close_image_viewer(image);
}

std::shared_ptr<ImageViewerPanel> App::open_image_viewer(ev2::ImageID image)
{
	auto it = image_viewers.find(image);
	if (it != image_viewers.end())
		return it->second;

	static int width = 500;
	static int height = 500;

	glm::ivec2 pos = glm::ivec2(0.5f*(
		glm::vec2(win.width, win.height) -
		glm::vec2(width, height)
	));

	const char *name = ev2::get_image_name(ctx, image);

	std::string panel_name = "Viewer: "; 
	if (name)
		panel_name += name;
	else 
		panel_name += "Image" + std::to_string(image.id);

	std::shared_ptr<ImageViewerPanel> panel( 
		new ImageViewerPanel(this, pos.x, pos.y, width, height, 
			"pipelines/screen_quad.yaml", panel_name.c_str())
	);

	if (panel->init(ctx, image) != App::OK) {
		return nullptr;
	}

	image_viewers[image] = panel;
	return panel;
}

void App::close_image_viewer(ev2::ImageID image)
{
	auto it = image_viewers.find(image);

	if (it == image_viewers.end())
		return;

	it->second->destroy(ctx);
	it->second.reset();
	
	image_viewers.erase(it);
}

void App::imgui()
{
	setup_root_dockspace();

	if (frame_counter) {
		plot_frame_times(input.dt);
	}

	ev2::imgui::inspector_panel_imgui(ctx);
}

