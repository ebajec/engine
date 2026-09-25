#ifndef BOUNDARY_EDITOR_H
#define BOUNDARY_EDITOR_H

#include "ev2/editor.h"
#include "ev2/image_viewer.h"

// An image viewer that paints into the image it is displaying. Sample owned:
// Editor::open_image_viewer always builds a plain ImageViewer2, and the
// editor's update loop is not virtual, so the owner drives this one.
struct BoundaryEditor : public ImageViewer2
{
	ev2::ComputePipelineID cursor;
	ev2::BindingsID cursor_bindings;

	BoundaryEditor(
		uint32_t x, uint32_t y,
		uint32_t w, uint32_t h,
		const char *name = nullptr
	) : ImageViewer2(x, y, w, h, 0, "core://pipeline/screen_quad.yaml", name)
	{
		ev2::GfxContext *ctx = Editor::ctx();

		cursor = ev2::load_compute_pipeline(ctx, "fluid://shader/bd_cursor.comp");
		cursor_bindings = ev2::create_bindings(ctx, cursor, 0,
											ev2::BINDING_MODE_DYNAMIC);
	}

	int update(ev2::GfxContext *ctx, int *p_flags = nullptr)
	{
		int res = ImageViewer2::update(ctx, p_flags);

		if (res < Editor::OK)
			return res;

		ev2::ImageID image = get_image();

		if (Editor::input().right_mouse_pressed && is_hovered() && image.is_valid()) {
			ev2::reset_bindings(ctx, cursor_bindings);
			ev2::bind_image(ctx, cursor_bindings, "img_out", image);
			ev2::flush_bindings(ctx, cursor_bindings);

			struct {
				glm::vec2 pos;
				uint32_t status;
			} pc = {
				.pos = get_grid_cursor_pos(),
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

#endif // BOUNDARY_EDITOR_H
