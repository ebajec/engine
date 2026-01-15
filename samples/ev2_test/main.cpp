#include <engine/utils/log.h>

#include <ev2/device.h>
#include <ev2/render.h>

#include <engine/utils/camera.h>

#include <GLFW/glfw3.h>

#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

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

int main(int argc, char *argv[])
{
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	if (!glfwInit()) {
		log_error("Failed to initialize GLFW!");
	}
	print_glfw_platform();

	GLFWwindow *p_window = glfwCreateWindow(800,800, "ev2", nullptr, nullptr);

	if (!p_window) {
		log_error("Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	if (init_gl_context(p_window) < 0) {
		log_error("Failed to initialize OpenGL");
		return EXIT_FAILURE;
	}

	ev2::Device *dev = ev2::create_device(RESOURCE_PATH);

	ev2::GraphicsPipelineID screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	ev2::ImageAssetID saro_img = ev2::load_image_asset(dev, "image/saro.jpg");
	ev2::TextureID saro_tex = ev2::create_texture(
		dev,
		ev2::get_image_resource(dev, saro_img),
		ev2::FILTER_BILINEAR
	);

	uint32_t w = 1024, h = 1024;

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

	// Compute pipelines
	ev2::ComputePipelineID diffusion = ev2::load_compute_pipeline(dev, 
		"shader/diffusion.comp.spv");

	if (!diffusion.id) {
		ev2::destroy_device(dev);
		return EXIT_FAILURE;
	}

	ev2::DescriptorLayoutID diffusion_layout = 
		ev2::get_compute_pipeline_layout(dev, diffusion);

	// View and proj matrices are set later
	ev2::ViewID camera = ev2::create_view(dev, nullptr, nullptr);

	int ctr = 0;

	uint32_t groups_x = w / 32;
	uint32_t groups_y = w / 32;

	//------------------------------------------------------------------------------
	// Get shader resource locations and create descriptor sets

	ev2::BindingSlot tex_slot = ev2::find_binding(screen_quad_layout, "u_tex");

	ev2::DescriptorSetID saro_img_set = ev2::create_descriptor_set(dev, screen_quad_layout);

	ev2::BindingSlot img_in_slot = ev2::find_binding(diffusion_layout, "img_in");
	ev2::BindingSlot img_out_slot = ev2::find_binding(diffusion_layout, "img_out");

	ev2::DescriptorSetID diffusion_set = ev2::create_descriptor_set(dev, diffusion_layout);

	printf("Started!\n");

	while (!glfwWindowShouldClose(p_window)) {
		glfwPollEvents();
		glfwSwapBuffers(p_window);

		int curr = ctr;
		ctr = (ctr + 1) & 0x1;
		int next = ctr;

		int win_w, win_h;

		glfwGetFramebufferSize(p_window, &win_w, &win_h);

		glm::mat4 view = glm::mat4(1.f);
		glm::mat4 proj = camera_proj_2d((float)w/(float)h, 1.f);

		ev2::update_view(dev, camera, glm::value_ptr(view), glm::value_ptr(proj));

		ev2::Rect view_rect = {
			.x0 = 0,
			.y0 = 0,
			.w = (uint32_t)win_w,
			.h = (uint32_t)win_h
		};

		ev2::begin_frame(dev);

		ev2::bind_texture(dev, diffusion_set, img_in_slot, swap_tex[curr]);
		ev2::bind_texture(dev, diffusion_set, img_out_slot, swap_tex[next]);

		//ev2::bind_texture(dev, screen_quad_set, tex_slot, swap_tex[curr]);
		ev2::bind_texture(dev, saro_img_set, tex_slot, saro_tex);

		ev2::RecorderID rec = ev2::begin_commands(dev);
		ev2::cmd_bind_descriptor_set(rec, diffusion_set);
		ev2::cmd_dispatch(rec, diffusion, groups_x, groups_y, 1);
		ev2::cmd_use_texture(rec, swap_tex[curr], ev2::USAGE_SAMPLED_GRAPHICS);
		ev2::SyncID cmd_sync = ev2::end_commands(rec);

		ev2::PassCtx pass = ev2::begin_pass(dev, {}, camera, view_rect);
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

	ev2::destroy_device(dev);

	glfwDestroyWindow(p_window);
	glfwTerminate();

	return 0;
}
