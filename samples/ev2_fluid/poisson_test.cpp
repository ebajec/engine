#include "poisson_solver.h"
#include "app.h"
#include "texture_viewer.h"
#include "heightmap_viewer.h"

#include <ev2/imgui/inspector.h>

#include <memory>

static uint64_t initialize_image(ev2::GfxContext *ctx, ev2::ImageID img, 
					 uint32_t w, uint32_t h, const void *pix_init, 
					size_t pix_size, size_t pix_align)
{
	size_t size = w * h * pix_size;
	ev2::UploadContext uc = ev2::begin_upload(ctx, size, pix_align);

	for (size_t i = 0; i < size; i += pix_size) {
		memcpy((char*)uc.ptr + i, pix_init, pix_size); 
	}
	ev2::ImageUpload upload = {
		.src_offset = 0,
		.x = 0, 
		.y = 0,
		.w = w,
		.h = h,
	};
	return ev2::commit_image_uploads(ctx, uc, img, &upload, 1);
}

struct PoissonSolverApp : public App
{
	ev2::ImageID rhs;
	ev2::ImageID lhs;
	ev2::ImageID bd;

	ev2::TextureID heightmap_tex;

	ev2::ComputePipelineID cursor;
	ev2::BindingsID bindings;

	ev2::ComputePipelineID bd_cursor;
	ev2::BindingsID bd_cursor_bindings;

	std::unique_ptr<PoissonSolver> solver;
	std::unique_ptr<MeanSubtractor> mean_subtractor;

	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;
	std::unique_ptr<ImageViewerPanel> lhs_panel;
	std::unique_ptr<ImageViewerPanel> rhs_panel;
	std::unique_ptr<ImageViewerPanel> bd_panel;

	glm::uvec2 grid;

	int m_res = 8;

	struct alignas(8) Uniforms {
		glm::vec2 p1;
		glm::vec2 p2;
		float power = 0.1;
		float sigma = 0.01;
	} uniforms;

	bool do_v_cycle = false;
	bool always_run = false;

	PoissonSolverApp() : App(1200, 1200, "poisson solver") {
	}

	int initialize(int argc, char **argv)
	{
		App::initialize(argc, argv);
		lhs_panel.reset(new ImageViewerPanel(this, 0, 0, 500, 500, 
			"pipelines/pressure_viz.yaml", "lhs"));
		rhs_panel.reset(new ImageViewerPanel(this, 0, 0, 500, 500, 
			"pipelines/pressure_viz.yaml", "rhs"));

		bd_panel.reset(new ImageViewerPanel(this, 0, 0, 500, 500, 
			"pipelines/screen_quad.yaml", "BdMask"));

		heightmap_panel.reset(new HeightmapViewerPanel());

		int result = on_sim_size_changed();

		result = lhs_panel->init(ctx, lhs);
		if (result < App::OK)
			return result;

		result = rhs_panel->init(ctx, rhs);
		if (result < App::OK)
			return result;

		result = bd_panel->init(ctx, bd);
		if (result < App::OK)
			return result;

		cursor = ev2::load_compute_pipeline(ctx, "shader/cursor_r32f");
		bindings = ev2::create_bindings(ctx, cursor, 0, ev2::BINDING_MODE_DYNAMIC);

		bd_cursor = ev2::load_compute_pipeline(ctx, "shader/bd_cursor");
		bd_cursor_bindings = ev2::create_bindings(ctx, bd_cursor, 0, ev2::BINDING_MODE_DYNAMIC);

		return App::OK;
	}

	void record_update()
	{
		ev2::PassID pass = ev2::begin_compute_pass(ctx);

		constexpr uint group_size = 16;

		if (true) {
			Uniforms pc = uniforms;

			if (!input.right_mouse_pressed) {
				pc.power = 0.f;
			}
			pc.power *= input.dt;

			ev2::cmd_use_image(pass, rhs, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
			ev2::cmd_bind_compute_pipeline(pass, cursor);
			ev2::cmd_bind_resources(pass, bindings);
			ev2::cmd_push_constant(pass, cursor, 0, sizeof(pc), &pc);
			ev2::cmd_dispatch(pass,
				1 + (grid.x - 1)/group_size, 1 + (grid.y - 1)/group_size, 1
			);
		}

		if (input.right_mouse_pressed && bd_panel->panel->is_hovered()) {
			struct {
				glm::vec2 pos;
				uint32_t status;
			} pc = {
				.pos = bd_panel->get_world_cursor_pos(),
				.status = 0,
			};
			ev2::cmd_use_image(pass, bd, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
			ev2::cmd_bind_compute_pipeline(pass, bd_cursor);
			ev2::cmd_bind_resources(pass, bd_cursor_bindings);
			ev2::cmd_push_constant(pass, bd_cursor, 0, sizeof(pc), &pc);
			ev2::cmd_dispatch(pass, 1 ,1, 1);
		}

		if (do_v_cycle || always_run) {
			mean_subtractor->record(pass, rhs);
			solver->record_setup(pass);
			solver->record_v_cycle(pass);
			mean_subtractor->record(pass, lhs);
		}
			
		ev2::end_pass(ctx, pass);
	}

	int update()
	{
		bool needs_resize = false;

		if (ImGui::Begin(ev2::imgui::EDITOR_PANEL_NAME)) {
			if (ImGui::Button("Run v-cycle")) {
				do_v_cycle = true;
			}
			if (ImGui::RadioButton("Run continuously", always_run)) {
				always_run = !always_run;
			}

			ImGui::SliderInt("N", (int*)&solver->N, 1, solver->N_max);

			int old_res = m_res;
			ImGui::SliderInt("Grid resolution", &m_res, 1, 12); 
			ImGui::SliderFloat("Cursor power", &uniforms.power, 0.f, 1.f);
			ImGui::SliderFloat("Cursor spread", &uniforms.sigma, 0.0001f, 1.f, "%.4f");

			if (ImGui::Button("Reset Solver")) {
				reset_solver();
			}

			if (ImGui::Button("Reset RHS")) {
				reset_rhs();
			}

			if (ImGui::Button("Reset Boundary")) {
				reset_bd();
			}

			needs_resize = old_res != m_res;
		}
		ImGui::End();

		int res = App::OK;

		if (needs_resize) {
			res = on_sim_size_changed();

			if (res)
				return res;
		}

		res = lhs_panel->update(ctx);
		if (res < App::OK)
			return res;

		res = rhs_panel->update(ctx);
		if (res < App::OK)
			return res;

		res = bd_panel->update(ctx);
		if (res < App::OK)
			return res;

		res = heightmap_panel->update(ctx);
		if (res < App::OK)
			return res;

		uniforms.p1 = rhs_panel->get_world_cursor_pos();

		ev2::reset_bindings(ctx, bindings);
		ev2::bind_image(ctx, bindings, "img_out", rhs);
		ev2::flush_bindings(ctx, bindings);

		ev2::reset_bindings(ctx, bd_cursor_bindings);
		ev2::bind_image(ctx, bd_cursor_bindings, "img_out", bd);
		ev2::flush_bindings(ctx, bd_cursor_bindings);

		solver->set_inputs(ctx, lhs, rhs, bd);
		mean_subtractor->setup_bindings(ctx, rhs);
		mean_subtractor->setup_bindings(ctx, lhs);

		record_update();

		do_v_cycle = false; 

		return App::OK;
	}

	void render()
	{
		rhs_panel->render(ctx);
		lhs_panel->render(ctx);
		bd_panel->render(ctx);
		heightmap_panel->render(ctx);
	}

	void destroy()
	{
		if (lhs.is_valid())
			ev2::destroy_image(ctx, lhs);
		if (rhs.is_valid())
			ev2::destroy_image(ctx, rhs);
	}

	void reset_bd() {
		uint8_t init = UINT8_MAX;
		initialize_image(ctx, bd, grid.x, grid.y, &init, sizeof(init), alignof(uint8_t)); 
		ev2::flush_uploads(ctx);
	}

	void reset_rhs() {
		glm::vec4 init(0);
		initialize_image(ctx, rhs, grid.x, grid.y, &init, sizeof(init), alignof(glm::vec4)); 
		ev2::flush_uploads(ctx);
	}

	void reset_solver() {
		glm::vec4 init(0);
		initialize_image(ctx, lhs, grid.x, grid.y, &init, sizeof(init), alignof(glm::vec4)); 
		ev2::flush_uploads(ctx);
	}

	int on_sim_size_changed() 
	{
		grid = glm::uvec2(1 << m_res);

		if (lhs.is_valid()) {
			ev2::destroy_image(ctx, lhs);
		}
		if (rhs.is_valid()) {
			ev2::destroy_image(ctx, rhs);
		}

		if (solver)
			solver->destroy(ctx);
		else
			solver.reset(new PoissonSolver);

		if (mean_subtractor)
			mean_subtractor->destroy(ctx);
		else
			mean_subtractor.reset(new MeanSubtractor);

		int res = solver->init(ctx, grid.x, grid.y);

		if (res < App::OK)
			return res;

		lhs = ev2::create_image(ctx, grid.x, grid.y, 1, ev2::IMAGE_FORMAT_32F, 
			ev2::IMAGE_USAGE_TRANSFER_DST_BIT |
			ev2::IMAGE_USAGE_SAMPLED_BIT | 
			ev2::IMAGE_USAGE_STORAGE_BIT);

		if (!lhs.is_valid())
			return App::ERROR;

		rhs = ev2::create_image(ctx, grid.x, grid.y, 1, ev2::IMAGE_FORMAT_32F, 
			ev2::IMAGE_USAGE_TRANSFER_DST_BIT |
			ev2::IMAGE_USAGE_SAMPLED_BIT | 
			ev2::IMAGE_USAGE_STORAGE_BIT);

		if (!rhs.is_valid())
			return App::ERROR;

		bd = ev2::create_image(ctx, grid.x, grid.y, 1, ev2::IMAGE_FORMAT_R8_UNORM, 
			ev2::IMAGE_USAGE_TRANSFER_DST_BIT |
			ev2::IMAGE_USAGE_SAMPLED_BIT | 
			ev2::IMAGE_USAGE_STORAGE_BIT);

		if (!rhs.is_valid())
			return App::ERROR;

		res = mean_subtractor->init(ctx, grid.x, grid.y);

		if (res)
			return App::ERROR;

		if (heightmap_tex.is_valid())
			ev2::destroy_texture(ctx, heightmap_tex);
		heightmap_tex = ev2::create_texture(ctx, lhs, ev2::FILTER_BILINEAR);

		res = heightmap_panel->init(this, ctx, heightmap_tex);

		lhs_panel->set_image(ctx, lhs, 0, 0);
		rhs_panel->set_image(ctx, rhs, 0, 0);
		bd_panel->set_image(ctx, bd, 0, 0);

		reset_solver();
		reset_rhs();
		reset_bd();

		return App::OK;
	}
};

int main(int argc, char *argv[])
{
	std::unique_ptr<PoissonSolverApp> app (new PoissonSolverApp{});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

	int status = App::OK;

	for(;;)
	{
		status = app->begin_frame();
		if (should_exit(status))
			break;

		status = app->update();
		if (should_exit(status))
			break;
		
		app->render();

		status = app->end_frame();
		if (should_exit(status))
			break;
	}

	app->destroy();

	return EXIT_SUCCESS;
}
