#include "heightmap_viewer.h"

static std::vector<uint32_t> create_quad_indices(uint32_t n)
{
	std::vector<uint32_t> indices;
	for (uint32_t i = 0; i < n; ++i) {
		for (uint32_t j = 0; j < n; ++j) {
			uint32_t in = std::min(i + 1,n - 1);
			uint32_t jn = std::min(j + 1,n - 1);

			indices.push_back((n) * i  + j); 
			indices.push_back((n) * in + j); 
			indices.push_back((n) * in + jn); 

			indices.push_back((n) * i  + j);
			indices.push_back((n) * in + jn);
			indices.push_back((n) * i  + jn);
		}
	}

	return indices;
}

int HeightmapViewerPanel::init(App *app_, ev2::GfxContext *ctx, ev2::TextureID tex)
{
	app = app_;

	panel = std::make_unique<Panel>(app_, ctx, "3D view", 700, 0, 500, 500);

	//-----------------------------------------------------------------------------
	// Input

	app->insert_key_callback([this](int key, int scancode, int action, int mods){
		glfw_wasd_to_motion(this->keydir, key, action);
	});

	rd.camera = ev2::create_view(ctx, nullptr, nullptr);

	control = MotionCamera::look_at(glm::vec3(0,0,0), glm::dvec3(1,1,1), glm::dvec3(0,0,1));

	//-----------------------------------------------------------------------------
	// Setup pipeline
	
	rd.pipeline = ev2::load_graphics_pipeline(ctx, "pipelines/heightmap.yaml");

	if (!EV2_VALID(rd.pipeline))
		return EXIT_FAILURE;

	rd.bindings = ev2::create_bindings(
		ctx, rd.pipeline, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_STATIC);

	int result = this->set_texture(ctx, tex);

	if (result)
		return EXIT_FAILURE;

	return 0;
}

int HeightmapViewerPanel::set_texture(ev2::GfxContext *ctx, ev2::TextureID tex)
{
	uint32_t h, w;
	ev2::get_texture_dims(ctx, tex, &w, &h, nullptr);

	//-----------------------------------------------------------------------------
	// Prepare index buffer
	if (rd.w != w || rd.h != h) {
		std::vector<uint32_t> indices = create_quad_indices(w);

		size_t indices_size = indices.size()*sizeof(uint32_t);

		rd.ibo = ev2::create_buffer(ctx, indices_size, ev2::BUFFER_USAGE_INDEX_BUFFER_BIT);

		ev2::UploadContext uc = ev2::begin_upload(ctx, indices_size, alignof(uint32_t)); 
		memcpy(uc.ptr, indices.data(), indices_size); 
		ev2::BufferUpload up = {.size = indices_size};
		uint64_t sync = ev2::commit_buffer_uploads(ctx, uc, rd.ibo, &up, 1);
	}

	if (ev2::bind_texture(ctx, rd.bindings, "u_tex", tex) != ev2::SUCCESS) {
		return -1;
	}
	ev2::flush_bindings(ctx, rd.bindings);

	rd.tex = tex;
	rd.w = w;
	rd.h = h;

	return 0;
}

int HeightmapViewerPanel::update(ev2::GfxContext *ctx)
{
	panel->imgui();

	glm::ivec2 panel_size = panel->get_size();

	float aspect = (float)panel_size.y/(float)panel_size.x;

	rd.proj = camera_proj_3d(PIf/4.f, aspect, 10.f, 0.01f);
	rd.view = control.get_view();
	
	ev2::update_view(ctx, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	static float speed = 1.f;

	if (app->input.mouse_mode == GLFW_CURSOR_DISABLED && panel->is_focused()) {
		glm::dvec2 delta = app->input.get_mouse_delta()/(double)panel_size.x;
		control.rotate(-delta.x, delta.y);
		control.move(app->input.dt * glm::dvec3(speed*keydir));

	}
	return 0;
}
void HeightmapViewerPanel::render(ev2::GfxContext *ctx)
{
	glm::ivec2 panel_size = panel->get_size();

	ev2::Rect rect = {
		.x0 = 0, .y0 = 0,
		.w = (uint32_t)panel_size.x, .h = (uint32_t)panel_size.y
	};
	ev2::PassID pass = ev2::begin_gfx_pass(ctx, panel->get_target(), rd.camera, rect);
	ev2::cmd_use_buffer(pass, rd.ibo, ev2::USAGE_INDEX_INPUT);

	ev2::ImageID image = ev2::get_backing_image(ctx, rd.tex);
	ev2::cmd_use_image(pass, image, ev2::USAGE_SAMPLED_GRAPHICS);

	ev2::cmd_bind_gfx_pipeline(pass, rd.pipeline);
	ev2::cmd_bind_resources(pass, rd.bindings);
	ev2::cmd_bind_index_buffer(pass, rd.ibo, 0);

	uint32_t idx_count = 6 * rd.w * rd.h;

	ev2::cmd_custom(pass, [idx_count](VkCommandBuffer cmds){
		vkCmdDrawIndexed(cmds, idx_count, 1, 0, 0, 0); 
	});

	//glDrawElements(GL_TRIANGLES, idx_count, GL_UNSIGNED_INT, nullptr);

	ev2::end_pass(ctx, pass);
}

void HeightmapViewerPanel::destroy(ev2::GfxContext *ctx)
{
	ev2::destroy_bindings(ctx, rd.bindings);
	ev2::destroy_buffer(ctx, rd.ibo);
	ev2::destroy_view(ctx, rd.camera);
	panel.reset();
}

