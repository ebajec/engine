#include <engine/utils/log.h>

#include <ev2/device.h>
#include <ev2/render.h>
#include <ev2/resource.h>

#include <engine/utils/camera.h>

// glfw
#include <GLFW/glfw3.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

#include <memory>
#include <cstdio>
#include <cstdlib>

extern int init_gl_context(GLFWwindow *window);

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

struct InputData
{
	glm::dvec2 mouse_pos;
	glm::dvec2 mouse_pos_diff;

	bool left_mouse_pressed = false;
	bool right_mouse_pressed = false;
};

struct WindowData 
{
	GLFWwindow *ptr;
	int width;
	int height;
	const char *title;
};

struct RenderData
{
	glm::mat4 proj;	
	glm::mat4 view;	
	ev2::ViewID camera;
}; 

struct App
{
	enum State {
		OK = 0,
		ERROR = -1,
		SHOULD_CLOSE = 1
	};

	InputData input;
	WindowData win;
	RenderData rd;

	ev2::Device *dev;

	//-----------------------------------------------------------------------------
	
	int resize(int width, int height);
	int update();
	void render();

	int initialize();
	void terminate();

	//-----------------------------------------------------------------------------
	// GLFW callbacks
	
	static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
	static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
	static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
	static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
};

void App::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));
}
void App::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
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
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));
}
void App::cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
	ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	glm::dvec2 prev_pos = app->input.mouse_pos;
	glm::dvec2 pos = glm::dvec2(xpos, ypos);

	app->input.mouse_pos_diff = pos - prev_pos;
	app->input.mouse_pos = pos;
}
void App::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (width != app->win.width || height != app->win.height) {
		if (app->resize(width, height) == App::OK) {
			app->win.width = width;
			app->win.height = height;
		}
	}
}

int App::resize(int width, int height)
{
	glfwSetWindowSize(win.ptr, width, height);

	win.width = width;
	win.height = height;

	int result = OK;

	return result;
}

int App::update()
{
	if (glfwWindowShouldClose(win.ptr))
		return SHOULD_CLOSE;

	int result = OK;

	glfwPollEvents();

	rd.proj = camera_proj_2d((float)win.width/(float)win.height, 1.f);
	rd.view = glm::mat4(1.f);

	ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	glfwSwapBuffers(win.ptr);

	return result;
}

void App::render()
{
	ev2::begin_frame(dev);

	ev2::end_frame(dev);
}

int App::initialize()
{
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	if (!glfwInit()) {
		log_error("Failed to initialize GLFW!");
		return EXIT_FAILURE;
	}
	print_glfw_platform();

	win.ptr = glfwCreateWindow(win.width, win.height, win.title, nullptr, nullptr);

	if (!win.ptr) {
		log_error("Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	if (init_gl_context(win.ptr) < 0) {
		log_error("Failed to initialize OpenGL");
		return EXIT_FAILURE;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls
	io.FontGlobalScale = 1.0f; 

	// Set ImGui style
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(win.ptr, true);
	ImGui_ImplOpenGL3_Init("#version 450");

	glfwSetKeyCallback(win.ptr,&App::key_callback);
	glfwSetMouseButtonCallback(win.ptr,&App::mouse_button_callback);
	glfwSetScrollCallback(win.ptr,&App::scroll_callback);
	glfwSetCursorPosCallback(win.ptr,&App::cursor_pos_callback);
	glfwSetFramebufferSizeCallback(win.ptr, &App::framebuffer_size_callback);
	glfwSetWindowUserPointer(win.ptr, this);

	dev = ev2::create_device(RESOURCE_PATH);

	if (!dev)
		return EXIT_FAILURE;

	rd.camera = ev2::create_view(dev, nullptr, nullptr);
	

	return EXIT_SUCCESS;
}

void App::terminate()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	ev2::destroy_device(dev);

	glfwDestroyWindow(win.ptr);
	glfwTerminate();
}

void upload_img_data(ev2::Device *dev, ev2::ImageID img, 
					 uint32_t w, uint32_t h)
{
	size_t size = w * h * sizeof(uint32_t);

	ev2::UploadContext uc = ev2::begin_upload(dev, size, alignof(uint32_t));

	uint32_t *pix = (uint32_t*)uc.ptr;

	for (uint32_t i = 0; i < w*h; ++i) {
		pix[i] = (rand() % 0xFFFFFF) << 8 | 0xFF; 
	}

	ev2::ImageUpload upload = {
		.src_offset = 0,
		.x = 0, 
		.y = 0,
		.w = w,
		.h = h,
	};

	ev2::commit_image_uploads(dev, uc, img, &upload, 1);
}

int main(int argc, char *argv[])
{
	std::unique_ptr<App> app (new App{
		.win = {
			.width = 1000,
			.height = 1000,
			.title = "ev2"
		}
	});

	if (app->initialize() != App::OK)
		return EXIT_FAILURE;

	ev2::Device *dev = app->dev;

	//------------------------------------------------------------------------------
	// Stuff for the sample
	
	ev2::GraphicsPipelineID screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	// Compute pipelines
	ev2::ComputePipelineID diffusion = ev2::load_compute_pipeline(dev, 
		"shader/diffusion.comp.spv");

	if (!diffusion.id) {
		ev2::destroy_device(dev);
		return EXIT_FAILURE;
	}

	ev2::ImageAssetID saro_img = ev2::load_image_asset(dev, "image/saro.jpg");
	ev2::TextureID saro_tex = ev2::create_texture(
		dev,
		ev2::get_image_resource(dev, saro_img),
		ev2::FILTER_BILINEAR
	);

	uint32_t w = 128, h = 128;
	uint32_t groups_x = w / 32;
	uint32_t groups_y = w / 32;

	ev2::ImageID swap_img[2] {
		ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8),
		ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8),
	};
	ev2::TextureID swap_tex[2] {
		ev2::create_texture(dev, swap_img[0],ev2::FILTER_BILINEAR),
		ev2::create_texture(dev, swap_img[1],ev2::FILTER_BILINEAR),
	};

	ev2::DescriptorLayoutID screen_quad_layout = 
		ev2::get_graphics_pipeline_layout(dev, screen_quad);

	ev2::DescriptorLayoutID diffusion_layout = 
		ev2::get_compute_pipeline_layout(dev, diffusion);

	//------------------------------------------------------------------------------
	// Get shader resource locations and create descriptor sets

	ev2::BindingSlot tex_slot = ev2::find_binding(screen_quad_layout, "u_tex");

	ev2::DescriptorSetID saro_img_set = ev2::create_descriptor_set(dev, screen_quad_layout);

	ev2::BindingSlot img_in_slot = ev2::find_binding(diffusion_layout, "img_in");
	ev2::BindingSlot img_out_slot = ev2::find_binding(diffusion_layout, "img_out");

	ev2::DescriptorSetID diffusion_set = ev2::create_descriptor_set(dev, diffusion_layout);

	printf("Started!\n");

	for (int i = 0; i < 20000; ++i) {
		upload_img_data(dev,swap_img[0], w, h);
		if (i % 128 == 0)
			ev2::flush_uploads(dev);
	}

	int ctr = 0;
	while (app->update() == App::OK) {
		int curr = ctr;
		ctr = (ctr + 1) & 0x1;
		int next = ctr;

		ev2::Rect view_rect = { .x0 = 0, .y0 = 0,
			.w = (uint32_t)app->win.width,
			.h = (uint32_t)app->win.height
		};

		ev2::begin_frame(dev);

		ev2::bind_texture(dev, diffusion_set, img_in_slot, swap_tex[curr]);
		ev2::bind_texture(dev, diffusion_set, img_out_slot, swap_tex[next]);

		//ev2::bind_texture(dev, screen_quad_set, tex_slot, swap_tex[curr]);
		ev2::bind_texture(dev, saro_img_set, tex_slot, swap_tex[0]);

		ev2::RecorderID rec = ev2::begin_commands(dev);
		ev2::cmd_bind_descriptor_set(rec, diffusion_set);
		ev2::cmd_dispatch(rec, diffusion, groups_x, groups_y, 1);
		ev2::cmd_use_texture(rec, swap_tex[curr], ev2::USAGE_SAMPLED_GRAPHICS);
		ev2::SyncID cmd_sync = ev2::end_commands(rec);

		ev2::PassCtx pass = ev2::begin_pass(dev, {}, app->rd.camera, view_rect);
		ev2::cmd_bind_pipeline(pass.rec, screen_quad);
		ev2::cmd_bind_descriptor_set(pass.rec, saro_img_set);
		ev2::cmd_draw_screen_quad(pass.rec);
		ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

		ev2::submit(pass_sync);

		ev2::end_frame(dev);
	}

	ev2::destroy_descriptor_set(dev, diffusion_set);
	ev2::destroy_descriptor_set(dev, saro_img_set);

	ev2::destroy_texture(dev, swap_tex[0]);
	ev2::destroy_texture(dev, swap_tex[1]);

	ev2::destroy_image(dev, swap_img[0]);
	ev2::destroy_image(dev, swap_img[1]);

	app->terminate();

	return EXIT_SUCCESS;
}
