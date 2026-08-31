#include "texture_viewer.h"

struct BoundaryEditor : public ImageViewerPanel 
{
	ev2::ComputePipelineID cursor;
	ev2::BindingsID cursor_bindings;

	BoundaryEditor(
		App *app, 
		uint32_t x, uint32_t y, 
		uint32_t w, uint32_t h, 
		const char *name = nullptr
	) :  ImageViewerPanel(app, x, y, w, h, "pipelines/core/screen_quad.yaml", name) 
	{
		cursor = ev2::load_compute_pipeline(app->ctx, "shader/fluid/bd_cursor");
		cursor_bindings = ev2::create_bindings(app->ctx, cursor, 0, 
											ev2::BINDING_MODE_DYNAMIC);
	}

	int update(ev2::GfxContext *ctx)
	{
		int res = ImageViewerPanel::update(ctx);

		if (app->input.right_mouse_pressed && panel->is_hovered()) {
			ev2::reset_bindings(ctx, cursor_bindings);
			ev2::bind_image(ctx, cursor_bindings, "img_out", image);
			ev2::flush_bindings(ctx, cursor_bindings);

			struct {
				glm::vec2 pos;
				uint32_t status;
			} pc = {
				.pos = get_world_cursor_pos(),
				.status = 0,
			};
			ev2::PassID pass = ev2::begin_compute_pass(ctx);
			ev2::cmd_use_image(pass, image, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
			ev2::cmd_bind_compute_pipeline(pass, cursor);
			ev2::cmd_bind_resources(pass, cursor_bindings);
			ev2::cmd_push_constant(pass, cursor, 0, sizeof(pc), &pc);
			ev2::cmd_dispatch(pass, 1 ,1, 1);
			ev2::end_pass(ctx, pass);
		}

		return res;
	}
};

