#include "gpu_sort.h"

#include "editor.h"
#include "viewport.h"
#include "project_mounts.h"

#include <ev2/utils/log.h>
#include <ev2/utils/camera.h>

#include <ev2/context.h>
#include <ev2/resource.h>

#include <ev2/utils/camera.h>
#include <ev2/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <algorithm>
#include <cstdlib>

struct TestApp
{
	ev2::GfxContext *ctx;

	std::unique_ptr<Viewport2> viewport;
	std::unique_ptr<GPUSort> sorter;
	ev2::BufferID buffers[2] = {};
	ev2::BufferID disp_buffer = {};
	ev2::BufferID real_buffer = {};
	ev2::ViewID camera = {};

	int bits = 4;
	int count = 16;
	int step = 0;
	uint32_t maxval = 0;

	std::vector<uint32_t> values;

	float zoom = 1.f;

	bool perpetual_sort = false;

	void on_count_changed();
	void reset();
	void randomize();
	void exec_sort();

	int initialize(int argc, char **argv);
	int update();
	void render();
	void destroy();
};

int TestApp::initialize(int argc, char **argv)
{
	ctx = Editor::ctx();

	viewport.reset(new Viewport2("Visualization", 100, 100, 250, 250));

	on_count_changed();
	randomize();
	reset();

	camera = ev2::create_view(ctx, nullptr, nullptr);

	return Editor::OK;
}

void TestApp::on_count_changed()
{
	sorter.reset(GPUSort::create(ctx, count, 32, "core://shader/sort/sort_uint.slang"));

	size_t sz = count * sizeof(uint32_t);

	for (int i = 0; i < 2; ++i) {
		if (buffers[i].is_valid())
			ev2::destroy_buffer(ctx, buffers[i]);
		buffers[i] = ev2::create_buffer(ctx, sz,
			ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT); 
	}
	real_buffer = ev2::create_buffer(ctx, sz,
		ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT); 
}

void TestApp::randomize()
{
	values.clear();
	values.reserve(count);

	for (int i = 0; i < count; ++i) {
		uint32_t r = rand() & ((1 << bits) - 1);
		values.push_back(r);
	}

	reset();
}

void TestApp::reset()
{
	if (values.size() != count)
		randomize();

	if (buffers[0].is_valid()) {
		size_t sz = count * sizeof(uint32_t);

		ev2::UploadContext uc = ev2::begin_upload(ctx, sz, alignof(uint32_t));
		memcpy(uc.ptr, values.data(), sz);
		ev2::BufferUpload up = {
			.src_offset = 0,
			.dst_offset = 0,
			.size = sz,
		};
		ev2::commit_buffer_uploads(ctx, uc, buffers[0], &up, 1);

		ev2::flush_uploads(ctx);

		disp_buffer = buffers[0];
	}
	if (buffers[1].is_valid()) {
		size_t sz = count * sizeof(uint32_t);

		ev2::UploadContext uc = ev2::begin_upload(ctx, sz, alignof(uint32_t));
		memcpy(uc.ptr, values.data(), sz);
		ev2::BufferUpload up = {
			.src_offset = 0,
			.dst_offset = 0,
			.size = sz,
		};
		ev2::commit_buffer_uploads(ctx, uc, buffers[1], &up, 1);

		ev2::flush_uploads(ctx);
	}

	if (real_buffer.is_valid()) {
		std::vector<uint32_t> tmp = values;
		std::sort(tmp.begin(), tmp.end());

		size_t sz = count * sizeof(uint32_t);

		ev2::UploadContext uc = ev2::begin_upload(ctx, sz, alignof(uint32_t));
		memcpy(uc.ptr, tmp.data(), sz);
		ev2::BufferUpload up = {
			.src_offset = 0,
			.dst_offset = 0,
			.size = sz,
		};
		ev2::commit_buffer_uploads(ctx, uc, real_buffer, &up, 1);

		ev2::flush_uploads(ctx);
	}
}

int TestApp::update()
{
	int result = Editor::OK;

	ImGui::Begin("Editor");

	if (ImGui::SliderInt("Count", &count, 1, 4*8192)) {
		on_count_changed();
		reset();
	}

	if (ImGui::SliderInt("Bits", &bits, 1, 32)) {
		randomize();
	}

	if (ImGui::Button("Reset")) {
		reset();
	}

	if (ImGui::Button("Randomize")) {
		randomize();
	}

	if (ImGui::Button("Run Sort")) {
		exec_sort();
	}

	if (ImGui::RadioButton("Sort each frame", perpetual_sort)) {
		perpetual_sort = !perpetual_sort;
	}

	ImGui::End();

	if (result = viewport->imgui(nullptr); result != Editor::OK)
		return result;

	zoom *= (float)pow(1.2, Editor::input().scroll_delta.y);

	glm::vec2 size = viewport->get_size();
	glm::mat4 view = glm::mat4(1.f);
	view[0][0] = 2.f; 
	view[1][1] = 2.f; 
	view[3][0] = -1.f; 
	view[3][1] = -1.f; 

	glm::mat4 proj = camera_proj_2d(size.y/size.x, zoom);

	ev2::update_view(ctx, camera, glm::value_ptr(view), glm::value_ptr(proj));

	render();

	return result;
}

void TestApp::exec_sort()
{
	ev2::PassID pass = ev2::begin_compute_pass(ctx);
	disp_buffer = sorter->record(pass, bits, buffers, count, sizeof(uint32_t), nullptr, 0);
	ev2::end_pass(ctx, pass);
}

void TestApp::render()
{
	ev2::GfxPipelineID p_points = ev2::load_graphics_pipeline(ctx, "test_sort://pipeline/points.yaml");
	ev2::GfxPipelineID p_background = ev2::load_graphics_pipeline(ctx, "test_sort://pipeline/bg.yaml");

	if (perpetual_sort)
		exec_sort();

	if (!disp_buffer.is_valid())
		return;

	ev2::GfxPassInfo pass_info = {
		.target = viewport->get_target(),
		.view = camera
	};
	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);

	ev2::cmd_use_buffer(pass, disp_buffer, ev2::USAGE_STORAGE_READ_GRAPHICS);

	struct {
		uint32_t count;
		uint32_t bits;
		uint32_t color;
		VkDeviceAddress values;
	} pc {
		.count = (uint32_t)count,
		.bits = (uint32_t)bits,
		.color = 0xFFFFFFFF,
		.values = ev2::get_buffer_device_address(ctx, disp_buffer)
	};
	ev2::cmd_push_constant(pass, p_points, 0, sizeof(pc), &pc);

	ev2::cmd_bind_gfx_pipeline(pass, p_background);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds) {
		vkCmdDraw(cmds, 6, 1, 0, 0);
	});
	ev2::cmd_bind_gfx_pipeline(pass, p_points);
	ev2::cmd_custom(pass, [count = count](VkCommandBuffer cmds) {
		vkCmdDraw(cmds, 6, count, 0, 0);
	});

	if (real_buffer.is_valid()) {
		pc.values = ev2::get_buffer_device_address(ctx, real_buffer);
		pc.color = 0xFF0000FF;

		ev2::cmd_push_constant(pass, p_points, 0, sizeof(pc), &pc);
		ev2::cmd_custom(pass, [count = count](VkCommandBuffer cmds) {
			vkCmdDraw(cmds, 6, count, 0, 0);
		});
	}

	ev2::end_pass(ctx, pass);
}
void TestApp::destroy()
{
}

bool should_exit(int status)
{
	return status != Editor::OK;
}

int main(int argc, char *argv[])
{
	std::unique_ptr<TestApp> app (new TestApp{});

	int result = Editor::OK;

	if (result = Editor::init(argc, argv); result < Editor::OK)
		return result;

	add_project_mounts(Editor::ctx());

	if (app->initialize(argc, argv) != Editor::OK)
		return EXIT_FAILURE;

	int status;

	for (;;)
	{
		status = Editor::begin_frame();
		if (should_exit(status))
			break;

		status = app->update();
		if (should_exit(status))
			break;

		status = Editor::end_frame();
		if (should_exit(status))
			break;
	}

	app->destroy();

	return status;
}
