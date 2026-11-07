#include "shared/project_mounts.h"

#include <ev2/core.h>
#include "ev2/editor.h"
#include "ev2/viewport.h"
#include "ev2/panning_camera.h"

#include <ev2/utils/log.h>
#include <ev2/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdlib>

struct Sample
{
	ev2::GfxContext *ctx;

	std::unique_ptr<ev2::Viewport> viewport;
	std::shared_ptr<ev2::PanningCamera> camera;

	struct {
		int cell_scale = 1;
		float F = 8;
		float noise_scale = 1;
		int octaves = 4;
		float speed = 2;
	} pc_noise;

	int initialize(int argc, char **argv);
	int update();

	~Sample();
};

Sample::~Sample()
{
}

int Sample::initialize(int argc, char **argv)
{
	ctx = ev2::editor::ctx();

	camera = std::make_shared<ev2::PanningCamera>(glm::dvec2(0.5, 0.5), 1.0);
	viewport.reset(new ev2::Viewport("Test", 100, 100, 250, 250));
	viewport->set_camera(camera);

	return ev2::SUCCESS;
}

int Sample::update()
{
	int result = ev2::SUCCESS;

	ImGui::Begin("Editor");

	ImGui::SliderFloat("F", &pc_noise.F, 1, 64);
	ImGui::SliderInt("Cell Scale", &pc_noise.cell_scale, 1, 8);
	ImGui::SliderFloat("Noise Scale", &pc_noise.noise_scale, 1, 64);
	ImGui::SliderInt("Octaves", &pc_noise.octaves, 1, 16);
	ImGui::SliderFloat("Speed", &pc_noise.speed, 0.1, 16);

	ImGui::End();

	// never close the viewport
	if (result = viewport->update(nullptr); result < ev2::SUCCESS)
		return result;

	ev2::GfxPassInfo pass_info = {
		.target = viewport->get_target(),
		.view = viewport->get_view(),
	};

	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, "test://pipeline/main.yaml");

	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
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
