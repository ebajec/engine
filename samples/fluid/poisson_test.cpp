#include "poisson_solver.h"
#include "boundary_editor.h"
#include "utils.h"
#include "heightmap_viewer.h"
#include "project_mounts.h"

#include "ev2/editor.h"
#include "ev2/image_viewer.h"

#include <ev2/imgui/inspector.h>

#include <memory>

struct PoissonSolverApp
{
	ev2::GfxContext *ctx = nullptr;

	ev2::ImageID rhs;
	ev2::ImageID lhs;
	ev2::ImageID bd;

	ev2::TextureID heightmap_tex;

	ev2::ComputePipelineID cursor;
	ev2::BindingsID bindings;

	std::unique_ptr<PoissonSolver> solver;
	std::unique_ptr<MeanSubtractor> mean_subtractor;

	std::unique_ptr<HeightmapViewer> heightmap_panel;
	std::shared_ptr<ImageViewer2> lhs_panel;
	std::shared_ptr<ImageViewer2> rhs_panel;
	std::unique_ptr<BoundaryEditor> bd_panel;

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

	int initialize(int argc, char **argv)
	{
		(void)argc; (void)argv;

		ctx = Editor::ctx();

		bd_panel.reset(new BoundaryEditor(0, 0, 500, 500, "BdMask"));
		heightmap_panel.reset(new HeightmapViewer());

		int result = on_sim_size_changed();
		if (result < Editor::OK)
			return result;

		cursor = ev2::load_compute_pipeline(ctx, "fluid://shader/cursor_r32f.comp");
		bindings = ev2::create_bindings(ctx, cursor, 0, ev2::BINDING_MODE_DYNAMIC);

		return Editor::OK;
	}

	void record_update()
	{
		ev2::PassID pass = ev2::begin_compute_pass(ctx);

		constexpr uint group_size = 16;

		if (true) {
			Uniforms pc = uniforms;

			if (!Editor::input().right_mouse_pressed) {
				pc.power = 0.f;
			}
			pc.power *= (float)Editor::input().dt;

			ev2::cmd_use_image(pass, rhs, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
			ev2::cmd_bind_compute_pipeline(pass, cursor);
			ev2::cmd_bind_resources(pass, bindings);
			ev2::cmd_push_constant(pass, cursor, 0, sizeof(pc), &pc);
			ev2::cmd_dispatch(pass,
				1 + (grid.x - 1)/group_size, 1 + (grid.y - 1)/group_size, 1
			);
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

		int res = Editor::OK;

		if (needs_resize) {
			res = on_sim_size_changed();

			if (res < Editor::OK)
				return res;
		}

		// lhs_panel and rhs_panel are editor owned, so the editor updates and
		// renders them. bd_panel paints into its image, so we drive it here.
		res = bd_panel->update(ctx);
		if (res < Editor::OK)
			return res;

		res = heightmap_panel->update(ctx);
		if (res < Editor::OK)
			return res;

		uniforms.p1 = rhs_panel->get_grid_cursor_pos();

		ev2::reset_bindings(ctx, bindings);
		ev2::bind_image(ctx, bindings, "img_out", rhs);
		ev2::flush_bindings(ctx, bindings);

		solver->set_inputs(ctx, lhs, rhs, bd, bd);
		mean_subtractor->setup_bindings(ctx, rhs);
		mean_subtractor->setup_bindings(ctx, lhs);

		record_update();

		do_v_cycle = false;

		return Editor::OK;
	}

	void render()
	{
		bd_panel->render(ctx);
		heightmap_panel->render(ctx);
	}

	~PoissonSolverApp()
	{
		if (heightmap_panel)
			heightmap_panel.reset(nullptr);
		if (lhs.is_valid())
			ev2::destroy_image(ctx, lhs);
		if (rhs.is_valid())
			ev2::destroy_image(ctx, rhs);
		if (bd.is_valid())
			ev2::destroy_image(ctx, bd);
	}

	void reset_bd() {
		initialize_image<uint8_t>(ctx, bd, UINT8_MAX); 
		ev2::flush_uploads(ctx);
	}

	void reset_rhs() {
		;
		initialize_image(ctx, rhs, glm::vec4(0)); 
		ev2::flush_uploads(ctx);
	}

	void reset_solver() {
		initialize_image(ctx, lhs, glm::vec4(0)); 
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

		if (res < Editor::OK)
			return res;

		lhs = ev2::create_image(ctx, grid.x, grid.y, 1, ev2::IMAGE_FORMAT_32F, 
			ev2::IMAGE_USAGE_TRANSFER_DST_BIT |
			ev2::IMAGE_USAGE_SAMPLED_BIT | 
			ev2::IMAGE_USAGE_STORAGE_BIT);

		if (!lhs.is_valid())
			return Editor::ERROR;

		rhs = ev2::create_image(ctx, grid.x, grid.y, 1, ev2::IMAGE_FORMAT_32F, 
			ev2::IMAGE_USAGE_TRANSFER_DST_BIT |
			ev2::IMAGE_USAGE_SAMPLED_BIT | 
			ev2::IMAGE_USAGE_STORAGE_BIT);

		if (!rhs.is_valid())
			return Editor::ERROR;

		bd = ev2::create_image(ctx, grid.x, grid.y, 1, ev2::IMAGE_FORMAT_R8_UNORM,
			ev2::IMAGE_USAGE_TRANSFER_DST_BIT |
			ev2::IMAGE_USAGE_SAMPLED_BIT |
			ev2::IMAGE_USAGE_STORAGE_BIT);

		if (!bd.is_valid())
			return Editor::ERROR;

		res = mean_subtractor->init(ctx, grid.x, grid.y);

		if (res)
			return Editor::ERROR;

		if (heightmap_tex.is_valid())
			ev2::destroy_texture(ctx, heightmap_tex);
		heightmap_tex = ev2::create_texture(ctx, lhs, ev2::FILTER_BILINEAR);

		// The viewers outlive a resize: build them once, then just repoint them
		// at the new images.
		if (!lhs_panel) {
			lhs_panel = Editor::open_image_viewer(lhs, "lhs",
				"fluid://pipeline/pressure_viz.yaml");
			rhs_panel = Editor::open_image_viewer(rhs, "rhs",
				"fluid://pipeline/pressure_viz.yaml");

			if (!lhs_panel || !rhs_panel)
				return Editor::ERROR;

			lhs_panel->set_closable(false);
			rhs_panel->set_closable(false);

			res = heightmap_panel->set_texture(ctx, heightmap_tex);
			if (res < Editor::OK)
				return res;

			heightmap_panel->viewport()->set_closable(false);
		} else {
			res = heightmap_panel->set_texture(ctx, heightmap_tex);
			if (res < Editor::OK)
				return res;
		}

		lhs_panel->set_image(ctx, lhs, 0, 0);
		rhs_panel->set_image(ctx, rhs, 0, 0);
		bd_panel->set_image(ctx, bd, 0, 0);

		reset_solver();
		reset_rhs();
		reset_bd();

		return Editor::OK;
	}
};

int main(int argc, char *argv[])
{
	std::unique_ptr<PoissonSolverApp> app (new PoissonSolverApp{});

	int result = Editor::init(argc, argv, "poisson solver", 1200, 1200);
	if (result < Editor::OK)
		return result;

	add_project_mounts(Editor::ctx());

	if (app->initialize(argc, argv) != Editor::OK)
		return EXIT_FAILURE;

	int status = Editor::OK;

	for(;;)
	{
		status = Editor::begin_frame();
		if (status != Editor::OK)
			break;

		status = app->update();
		if (status != Editor::OK)
			break;

		app->render();

		status = Editor::end_frame();
		if (status != Editor::OK)
			break;
	}

	app.reset(nullptr);
	Editor::shutdown();

	return status < 0 ? status : 0;
}
