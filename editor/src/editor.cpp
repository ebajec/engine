#include "ev2/editor.h"
#include "ev2/image_viewer.h"

// ev2
#include <ev2/imgui/inspector.h>
#include <ev2/utils/log.h>

// imgui
#include <imgui.h>
#include <implot.h>
#include "imgui_internal.h"
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_vulkan.h"

#include "GLFW/glfw3.h"

#include <cstdio>
#include <csignal>
#include <atomic>
#include <memory>
#include <unordered_set>

#if defined (__linux__) || defined(__APPLE__)
// posix
#include <unistd.h>
#endif

namespace Editor 
{

namespace {

//------------------------------------------------------------------------------
// Editor

struct EditorState {
	InputData input;
	WindowData win;

	ev2::GfxContext *ctx;

	uint64_t frame_counter = 0;

	// id of viewport
	uint32_t capture_owner = 0;
	uint32_t hot_viewport = 0;
	HotViewportFlags hot_viewport_flags = 0;

	// set on release of mouse capture
	bool discard_mouse_delta = false;

	std::unordered_map<
		ev2::ImageID, 
		std::weak_ptr<ImageViewer2>
	> inspector_image_viewers;
	std::unordered_set<std::shared_ptr<ImageViewer2>> image_viewers;

	ev2::PassID gui_pass;

	ImGuiID root_dockspace;

	std::atomic_bool should_close = false;
	std::atomic_bool has_shutdown = false;
	std::atomic_bool has_initialized = false;

	//------------------------------------------------------------------------------
	
	void update_input();
	int resize(int width, int height);
	void setup_root_dockspace();
	void imgui();
} g;

void EditorState::update_input()
{
	std::swap(input.mouse_pos[0], input.mouse_pos[1]);

	if (discard_mouse_delta) {
		discard_mouse_delta = false;
		input.mouse_pos[1] = input.mouse_pos[0];
	}

	glfwGetCursorPos(win.ptr, &input.mouse_pos[0].x, &input.mouse_pos[0].y);

#ifdef EV2_ENABLE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	input.mouse_in_gui = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || io.WantCaptureMouse;

	glm::vec3 dir(0);
	if (!io.WantTextInput) {
		if (glfwGetKey(g.win.ptr, GLFW_KEY_W)) dir += glm::vec3(1,0,0);
		if (glfwGetKey(g.win.ptr, GLFW_KEY_A)) dir += glm::vec3(0,-1,0);
		if (glfwGetKey(g.win.ptr, GLFW_KEY_S)) dir += glm::vec3(-1,0,0);
		if (glfwGetKey(g.win.ptr, GLFW_KEY_D)) dir += glm::vec3(0,1,0);
		if (glfwGetKey(g.win.ptr, GLFW_KEY_LEFT_SHIFT)) dir += glm::vec3(0,0,-1);
		if (glfwGetKey(g.win.ptr, GLFW_KEY_SPACE)) dir += glm::vec3(0,0,1);
	}
	input.move_dir = dir;
#endif
}

int EditorState::resize(int width, int height)
{
	glfwSetWindowSize(win.ptr, width, height);

	win.width = width;
	win.height = height;

	return ev2::resize_swapchain(ctx, width, height) == ev2::SUCCESS ? 
		OK : ERROR;
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

void EditorState::setup_root_dockspace()
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

static inline void plot_frame_times(float delta)
{
	delta *= 1000.f;
	static std::vector<float> times;
	static std::vector<float> deltas;
	static size_t scroll = 0;
	static constexpr size_t samples = 500;
	static float avg = 0;

	if (deltas.size() < samples) 
		deltas.resize(samples,0);
	if (times.size() < samples) 
		times.resize(samples,0);

	deltas[scroll] = delta;
	times[scroll] = (float)glfwGetTime();

	scroll = (scroll + 1)%samples;

	avg = 0.99f*avg + 0.01f*delta;

	ImGui::Begin("Editor"); 

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::CollapsingHeader("Frame times")) {
		if (ImPlot::BeginPlot("Frame times (ms)",ImVec2(200,200))) {
			ImPlot::SetupAxesLimits(
				(double)times[scroll],
				(double)times[scroll ? scroll - 1 : samples - 1],
				0.0,
				2.0*(double)avg,
				ImPlotCond_Always
			);

			ImPlotSpec spec;
			spec.Offset = (int)scroll;
			spec.Flags = ImPlotCond_Always;

			ImPlot::PlotLine("time", times.data(),
				deltas.data(), (int)samples, spec);
			ImPlot::EndPlot();
		}
	}
	ImGui::End();
}
void EditorState::imgui()
{
	setup_root_dockspace();

	if (frame_counter) {
		plot_frame_times((float)input.dt);
	}

	ev2::imgui::editor_panel_imgui(ctx);
	ev2::imgui::inspector_panel_imgui(ctx);
}

//------------------------------------------------------------------------------
// static callbacks

void image_viewer_open_callback(void *usr, ev2::ImageID image)
{
	auto it = g.inspector_image_viewers.find(image);
	if (it != g.inspector_image_viewers.end())
		return;

	std::shared_ptr<ImageViewer2> viewer = open_image_viewer(image, nullptr, nullptr);

	g.inspector_image_viewers[image] = viewer;
}
void image_viewer_close_callback(void *usr, ev2::ImageID image)
{
	EditorState *app = static_cast<EditorState*>(usr);

	auto it = g.inspector_image_viewers.find(image);

	if (it == g.inspector_image_viewers.end())
		return;

	if (std::shared_ptr<ImageViewer2> viewer = it->second.lock()) {
		g.image_viewers.erase(viewer);
	}

	g.inspector_image_viewers.erase(it);
}


//------------------------------------------------------------------------------
// GLFW input callbacks

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
#ifdef EV2_ENABLE_IMGUI
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
	ImGuiIO& io = ImGui::GetIO();
#endif

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) 
	{
		if (g.input.mouse_mode == GLFW_CURSOR_DISABLED) {
			Editor::release_capture();
		} else if (g.hot_viewport && (g.hot_viewport_flags & HOT_VIEWPORT_WANT_CAPTURE)) {
			g.input.mouse_mode = GLFW_CURSOR_DISABLED;
			g.capture_owner = g.hot_viewport;

#ifdef EV2_ENABLE_IMGUI
			io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
#endif
			glfwSetInputMode(window, GLFW_CURSOR, g.input.mouse_mode);
		}
	}
}
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
#ifdef EV2_ENABLE_IMGUI
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
#endif
	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
		g.input.left_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_RELEASE) {
		g.input.left_mouse_pressed = false;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS) {
		g.input.right_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_RELEASE) {
		g.input.right_mouse_pressed = false;
	}
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
#ifdef EV2_ENABLE_IMGUI
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
#endif
	g.input.scroll.x += xoffset;
	g.input.scroll.y += yoffset;

	g.input.scroll_delta.x = xoffset;
	g.input.scroll_delta.y = yoffset;
}

void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
#ifdef EV2_ENABLE_IMGUI
	ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
#endif
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	if (width != g.win.width || height != g.win.height) {
		g.win.width = width;
		g.win.height = height;

		g.input.needs_resize = true;
	}
}

#if defined (__linux__) || defined(__APPLE__)
void handle_sigint(int sig)
{
	(void)sig;
	g.should_close = true;
	log_info("received SIGINT");
}
#endif

//------------------------------------------------------------------------------
// Misc

void print_glfw_platform()
{
	int p = glfwGetPlatform(); 

	const char *s;
	switch (p) {
		case GLFW_PLATFORM_X11: s = "X11"; break;
		case GLFW_PLATFORM_WAYLAND: s = "Wayland"; break;
		case GLFW_PLATFORM_WIN32: s = "Win32"; break;
		case GLFW_PLATFORM_COCOA: s = "COCOA"; break;
		default: s = "[Unknown]"; break;
	}
	printf("\x1b[32mGLFW is running on %s\x1b[0m\n", s); 
}

} //namespace

int init(int argc, char *argv[], const char *title, int w, int h)
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
		return ERROR;
	}

	glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);

	print_glfw_platform();

	g.win = WindowData{
		.ptr = glfwCreateWindow(w, h, title, nullptr, nullptr),
		.width = w,
		.height = h,
		.title = title,
	};

	if (!g.win.ptr) {
		log_error("Failed to create GLFW window");
		return ERROR;
	}

	if (!glfwVulkanSupported()) {
		log_error("Vulkan not supported by GLFW");
		return ERROR;
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
		return ERROR;

	VkInstance vk_instance = ev2::get_vulkan_instance(); 
	VkSurfaceKHR surface = VK_NULL_HANDLE;

	VkResult vk_res = glfwCreateWindowSurface(vk_instance, g.win.ptr, nullptr, &surface); 

	if (vk_res < VK_SUCCESS) {
        log_error("failed to create window surface!");
		return ERROR;
    }

	ev2::GfxContextVulkanInfo vulkan_params = {
		.surface = surface		
	};

	g.ctx = ev2::create_context_for_vulkan(vulkan_params);

#ifdef EV2_ENABLE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= 
		ImGuiConfigFlags_NavEnableKeyboard |
		ImGuiConfigFlags_DockingEnable;
	io.FontGlobalScale = 1.0f; 

	// Set ImGui style
	ImGui::StyleColorsDark();
	if (!ImGui_ImplGlfw_InitForVulkan(g.win.ptr, true)) {
		log_error("Failed to initialize imgui for glfw");
		return ERROR;
	}

	ImGui_ImplVulkan_InitInfo init_info;
	ev2::populate_imgui_vulkan_init_info(g.ctx, &init_info);

	if (!ImGui_ImplVulkan_Init(&init_info)) {
		log_error("Failed to initialize imgui for vulkan");
		return ERROR;
	}

	ImPlot::CreateContext();
#endif

	ev2::imgui::set_image_viewer_close_callback(&g, image_viewer_close_callback);
	ev2::imgui::set_image_viewer_open_callback(&g, image_viewer_open_callback);

	glfwSetKeyCallback(g.win.ptr,&key_callback);
	glfwSetMouseButtonCallback(g.win.ptr,&mouse_button_callback);
	glfwSetScrollCallback(g.win.ptr,&scroll_callback);
	glfwSetCursorPosCallback(g.win.ptr,&cursor_pos_callback);
	glfwSetFramebufferSizeCallback(g.win.ptr, &framebuffer_size_callback);

	std::signal(SIGINT, handle_sigint);

	g.has_initialized = true;

	return OK;
}

int begin_frame()
{
	if (glfwWindowShouldClose(g.win.ptr))
		return SHOULD_CLOSE;
	if (g.should_close) {
		glfwSetWindowShouldClose(g.win.ptr, true);
		return SHOULD_CLOSE;
	}

	int result = OK;

	// I find it nicer to have ImGui captured wherever I need it to between frames
	if (g.frame_counter++) {
	} else {
		g.input.t0 = glfwGetTime();
	}

#ifdef EV2_ENABLE_IMGUI
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
#endif

	g.input.t1 = g.input.t0;
	g.input.t0 = glfwGetTime();
	g.input.dt = g.input.t0 - g.input.t1;
	g.input.scroll_delta = glm::vec2(0);

	glfwPollEvents();

	g.update_input();

	if (g.input.needs_resize) {
		if ((result = g.resize(g.win.width, g.win.height)) != OK)
			return result;

		g.input.needs_resize = false;
	}

#ifdef EV2_ENABLE_IMGUI
	g.imgui();
#endif

	ev2::Result ev2_result = ev2::begin_frame(g.ctx);
	if (ev2_result != ev2::SUCCESS)
		return ERROR;

	ev2::GfxPassInfo pass_info = {
		.viewport = ev2::Rect{0,0,(uint32_t)g.win.width, (uint32_t)g.win.height},
		.clear_color = true,
		.clear_depth = true,
		.name = "UI Pass",
	};

	g.gui_pass = ev2::begin_gfx_pass(g.ctx, &pass_info);

	for (auto it = g.image_viewers.begin(); it != g.image_viewers.end();) {
		ImageViewer2 &viewer = *(*it);
		int viewer_flags = 0;
		viewer.update(g.ctx, &viewer_flags);

		if (viewer_flags & Viewport::SHOULD_CLOSE_BIT) {
			it = g.image_viewers.erase(it);
		} else {
			++it;
		}
	}

	std::vector<ev2::ImageID> delete_list;
	for (auto it = g.inspector_image_viewers.begin(); it != g.inspector_image_viewers.end();) {
		if (it->second.expired()) {
			it = g.inspector_image_viewers.erase(it);
		} else {
			++it;
		}
	}

	return result;
}

int end_frame()
{
	for (const std::shared_ptr<ImageViewer2> & viewer : g.image_viewers) {
		if (viewer->is_auto_rendered())
			viewer->render(g.ctx);
	}

#ifdef EV2_ENABLE_IMGUI
	ImGui::Render();
	ImDrawData *draw_data = ImGui::GetDrawData();
	ev2::cmd_custom(g.gui_pass, [draw_data](VkCommandBuffer cmds) {
		ImGui_ImplVulkan_RenderDrawData(draw_data, cmds);
	});
#endif 

	// ending the gui pass after all other passes ensures that the ui is rendered last
	ev2::end_pass(g.ctx, g.gui_pass);

	ev2::end_frame(g.ctx);
	return OK;
}

void shutdown()
{
	g.inspector_image_viewers.clear();
	g.image_viewers.clear();

#ifdef EV2_ENABLE_IMGUI
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	ImPlot::DestroyContext();
#endif

	ev2::destroy_context(g.ctx);

	glfwDestroyWindow(g.win.ptr);
	glfwTerminate();

	g.has_shutdown = true;
}

ev2::GfxContext *ctx()
{
	assert(!g.has_shutdown && g.has_initialized);
	return g.ctx;
}

const InputData &input()
{
	assert(!g.has_shutdown && g.has_initialized);
	return g.input;
}

WindowData &window()
{
	assert(!g.has_shutdown && g.has_initialized);
	return g.win;
}

bool should_close()
{
	assert(!g.has_shutdown && g.has_initialized);
	return g.should_close;
}

ev2::PassID gui_pass()
{
	assert(!g.has_shutdown && g.has_initialized);
	return g.gui_pass;
}

uint32_t get_capture_owner()
{
	return g.capture_owner;
}

uint32_t get_hot_viewport()
{
	return g.hot_viewport;
}

void set_hot_viewport(uint32_t id, HotViewportFlags flags)
{
	g.hot_viewport = id;
	g.hot_viewport_flags = flags;
}

void release_capture()
{
	g.discard_mouse_delta = true;
	g.capture_owner = 0;
	g.hot_viewport = 0;
	g.hot_viewport_flags = 0;
	g.input.mouse_mode = GLFW_CURSOR_NORMAL;
#ifdef EV2_ENABLE_IMGUI
	ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
#endif
	glfwSetInputMode(g.win.ptr, GLFW_CURSOR, g.input.mouse_mode);
}

std::shared_ptr<ImageViewer2> open_image_viewer(
	ev2::ImageID image,
	const char *name,
	const char *pipeline,
	bool auto_render,
	uint32_t w,
	uint32_t h
)
{
	assert(!g.has_shutdown && g.has_initialized);

	glm::ivec2 pos = glm::ivec2(0.5f*(
		glm::vec2(g.win.width, g.win.height) -
		glm::vec2(w, h)
	));

	if (!pipeline) {
		pipeline = "core://pipeline/screen_quad.yaml";
	}

	std::string panel_name = "Viewer: "; 

	if (name)
		panel_name = name;
	else if (const char *img_name = ev2::get_image_name(g.ctx, image))
		panel_name += img_name;
	else 
		panel_name += "Image" + std::to_string(image.id);

	uint32_t flags = 
		ImageViewer2::EDITOR_OWNED_BIT |
		(auto_render * ImageViewer2::AUTO_RENDERED_BIT);

	std::shared_ptr<ImageViewer2> viewer( 
		new ImageViewer2(pos.x, pos.y, w, h, flags, 
			pipeline, panel_name.c_str())
	);

	if (viewer->set_image(g.ctx, image, 0, 0) != OK) {
		return nullptr;
	}

	g.image_viewers.insert(viewer);

	return viewer;
}

} // namespace Editor
