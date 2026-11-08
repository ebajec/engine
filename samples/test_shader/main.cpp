#include "shared/project_mounts.h"

#include <ev2/core.h>
#include "ev2/editor.h"
#include "ev2/viewport.h"
#include "ev2/panning_camera.h"
#include "ev2/motion_camera.h"

#include <ev2/utils/log.h>
#include <ev2/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdlib>

enum CameraMode
{
	CAMERA_PANNING,
	CAMERA_3D,
};

struct Sample
{
	ev2::GfxContext *ctx;

	std::unique_ptr<ev2::Viewport> viewport;
	std::shared_ptr<ev2::ICameraController> cameras[2];

	struct {
		int cell_scale = 1;
		float F = 8;
		float noise_scale = 1;
		int octaves = 8;
		float speed = 2;
		uint32_t height_tex = 0; // bindless handle
	} pc_noise;

	static constexpr uint32_t TERRAIN_RES = 2048;

	ev2::ImageID terrain_img;
	ev2::TextureID terrain_tex;

	ev2::ComputePipelineID gen_terrain;
	ev2::BindingsID gen_terrain_bindings;

	int camera_mode;

	bool generate_each_frame = false;
	bool generate_once = true; // the image starts uninitialized

	int initialize(int argc, char **argv);
	int update();

	~Sample();
};

Sample::~Sample()
{
	if (gen_terrain_bindings.is_valid())
		ev2::destroy_bindings(ctx, gen_terrain_bindings);
	if (terrain_tex.is_valid())
		ev2::destroy_texture(ctx, terrain_tex);
	if (terrain_img.is_valid())
		ev2::destroy_image(ctx, terrain_img);
}

int Sample::initialize(int argc, char **argv)
{
	ctx = ev2::editor::ctx();

	cameras[0] = std::make_shared<ev2::PanningCamera>(glm::dvec2(0.5, 0.5), 1.0);
	cameras[1] = std::make_shared<ev2::MotionCamera>(); 

	viewport.reset(new ev2::Viewport("Test", 100, 100, 250, 250));
	viewport->set_camera(cameras[0]);

	terrain_img = ev2::create_image(ctx, TERRAIN_RES, TERRAIN_RES, 1, 
		ev2::IMAGE_FORMAT_32F,
		ev2::IMAGE_USAGE_SAMPLED_BIT | ev2::IMAGE_USAGE_STORAGE_BIT);
	if (!terrain_img.is_valid())
		return ev2::EUNKNOWN;
	ev2::set_image_name(ctx, terrain_img, "terrain");

	terrain_tex = ev2::create_texture(ctx, terrain_img, ev2::FILTER_BILINEAR);
	if (!terrain_tex.is_valid())
		return ev2::EUNKNOWN;

	gen_terrain = ev2::load_compute_pipeline(ctx, "test://shader/generate_terrain.comp");
	gen_terrain_bindings = ev2::create_bindings(ctx, gen_terrain, 0, 
		ev2::BINDING_MODE_STATIC);
	if (ev2::bind_image(ctx, gen_terrain_bindings, "img_out", terrain_img) != ev2::SUCCESS)
		return ev2::EINVALID_BINDING;
	ev2::flush_bindings(ctx, gen_terrain_bindings);

	pc_noise.height_tex = ev2::get_bindless_handle(ctx, terrain_tex);

	return ev2::SUCCESS;
}

int Sample::update()
{
	int result = ev2::SUCCESS;

	ImGui::Begin("Editor");

	ImGui::SeparatorText("Noise Params");
	ImGui::SliderFloat("F", &pc_noise.F, 1, 64);
	ImGui::SliderInt("Cell Scale", &pc_noise.cell_scale, 1, 8);
	ImGui::SliderFloat("Noise Scale", &pc_noise.noise_scale, 1, 64);
	ImGui::SliderInt("Octaves", &pc_noise.octaves, 1, 16);
	ImGui::SliderFloat("Speed", &pc_noise.speed, 0.0, 16);

	ImGui::SeparatorText("Camera Settings");
	if (ImGui::Combo("Camera Mode", &camera_mode, "Panning\0""3D\0")) {
		viewport->set_camera(cameras[camera_mode]);
	}

	ImGui::SeparatorText("Terrain");
	if (ImGui::Button("Generate"))
		generate_once = true;
	ImGui::SameLine();
	ImGui::Checkbox("Every frame", &generate_each_frame);

	ImGui::End();

	// never close the viewport
	if (result = viewport->update(nullptr); result < ev2::SUCCESS)
		return result;

	if (generate_once || generate_each_frame) {
		generate_once = false;

		constexpr uint32_t group_size = 16;
		constexpr uint32_t groups = 1 + (TERRAIN_RES - 1)/group_size;

		ev2::PassID pass = ev2::begin_compute_pass(ctx, "generate_terrain");
		ev2::cmd_use_image(pass, terrain_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);
		ev2::cmd_bind_compute_pipeline(pass, gen_terrain);
		ev2::cmd_bind_resources(pass, gen_terrain_bindings);
		ev2::cmd_push_constant(pass, gen_terrain, 0, sizeof(pc_noise), &pc_noise);
		ev2::cmd_dispatch(pass, groups, groups, 1);
		ev2::end_pass(ctx, pass);
	}

	ev2::GfxPassInfo pass_info = {
		.target = viewport->get_target(),
		.view = viewport->get_view(),
	};

	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, "test://pipeline/main.yaml");

	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
	ev2::cmd_use_image(pass, terrain_img, ev2::USAGE_SAMPLED_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, pipeline);
	ev2::cmd_push_constant(pass, pipeline, 0, sizeof(pc_noise), &pc_noise);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds){ vkCmdDraw(cmds, 4, 1, 0, 0); });
	ev2::end_pass(ctx, pass);

	return ev2::SUCCESS;
}

bool should_exit(int status)
{
	return status < ev2::SUCCESS || status == ev2::SHOULD_CLOSE;
}

int main(int argc, char *argv[])
{
	int result = ev2::SUCCESS;

	if (result = ev2::editor::init(argc, argv, "Sorting", 1200, 800); result < ev2::SUCCESS)
		return result;
	add_project_mounts(ev2::editor::ctx());

	std::unique_ptr<Sample> app (new Sample{});
	if (app->initialize(argc, argv) != ev2::SUCCESS)
		return EXIT_FAILURE;

	int status;

	for (;;)
	{
		status = ev2::editor::begin_frame();
		if (should_exit(status))
			break;

		status = app->update();
		if (should_exit(status))
			break;

		status = ev2::editor::end_frame();
		if (should_exit(status))
			break;
	}

	app.reset(nullptr);
	ev2::editor::shutdown();

	return status < 0 ? status : 0;
}
