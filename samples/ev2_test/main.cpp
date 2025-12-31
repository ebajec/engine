#include <engine/utils/log.h>

#include <ev2/device.h>
#include <ev2/render.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

extern int init_gl_basic(GLFWwindow *window);

int main(int argc, char *argv[])
{
	if (!glfwInit()) {
		log_error("Failed to initialize GLFW!");
	}

	GLFWwindow *p_window = glfwCreateWindow(800,800, "ev2", nullptr, nullptr);

	if (!p_window) {
		log_error("Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	if (init_gl_basic(p_window) < 0) {
		log_error("Failed to initialize OpenGL");
		return EXIT_FAILURE;
	}

	ev2::Device *dev = ev2::create_device(RESOURCE_PATH);

	ev2::GraphicsPipelineID screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	ev2::ImageAssetID saro_tex = ev2::load_image_asset(dev, "image/saro.jpg");

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

	ev2::DescriptorSetID screen_quad_set = ev2::create_descriptor_set(dev, screen_quad_layout);

	ev2::DescriptorSlot tex_slot = ev2::find_descriptor(screen_quad_layout, "u_tex");
	ev2::DescriptorSlot ubo_slot = ev2::find_descriptor(screen_quad_layout, "ubo");

	// Compute pipelines
	ev2::ComputePipelineID diffusion = ev2::load_compute_pipeline(dev, "shader/diffusion.comp.spv");

	if (!diffusion.id) {
		ev2::destroy_device(dev);
		return EXIT_FAILURE;
	}

	ev2::DescriptorLayoutID diffusion_layout = 
		ev2::get_compute_pipeline_layout(dev, diffusion);

	ev2::DescriptorSlot img_in_slot = ev2::find_descriptor(diffusion_layout, "img_in");
	ev2::DescriptorSlot img_out_slot = ev2::find_descriptor(diffusion_layout, "img_out");

	ev2::DescriptorSetID diffusion_set = ev2::create_descriptor_set(dev, diffusion_layout);

	float view[4][4] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	float proj[4][4] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

	ev2::ViewID camera = ev2::create_view(dev, view[0], proj[0]);

	int ctr = 0;

	uint32_t groups_x = w / 32;
	uint32_t groups_y = w / 32;

	printf("Started!\n");

	while (!glfwWindowShouldClose(p_window)) {
		glfwPollEvents();
		glfwSwapBuffers(p_window);

		int curr = ctr;
		ctr = (ctr + 1) & 0x1;
		int next = ctr;

		ev2::begin_frame(dev);

		ev2::bind_set_texture(dev, diffusion_set, img_in_slot, swap_tex[curr]);
		ev2::bind_set_texture(dev, diffusion_set, img_out_slot, swap_tex[next]);

		ev2::bind_set_texture(dev, screen_quad_set, tex_slot, swap_tex[curr]);

		ev2::RecorderID rec = ev2::begin_commands(dev);
		ev2::cmd_bind_descriptor_set(rec, diffusion_set);
		ev2::cmd_dispatch(rec, diffusion, groups_x, groups_y, 1);
		ev2::cmd_use_texture(rec, swap_tex[curr], ev2::USAGE_SAMPLED_GRAPHICS);
		ev2::SyncID cmd_sync = ev2::end_commands(rec);

		ev2::PassCtx pass = ev2::begin_pass(dev, ev2::RenderTargetID{0}, camera);
		ev2::cmd_bind_descriptor_set(pass.rec, screen_quad_set);
		ev2::cmd_draw(pass.rec, ev2::MODE_TRIANGLES, 6);
		ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

		ev2::submit(pass_sync);

		ev2::end_frame(dev);
	}

	ev2::destroy_descriptor_set(dev, diffusion_set);
	ev2::destroy_descriptor_set(dev, screen_quad_set);

	ev2::destroy_texture(dev, swap_tex[0]);
	ev2::destroy_texture(dev, swap_tex[1]);

	ev2::destroy_image(dev, swap_img[0]);
	ev2::destroy_image(dev, swap_img[1]);

	ev2::destroy_device(dev);

	glfwDestroyWindow(p_window);
	glfwTerminate();

	return 0;
}
