#include "texture_viewer.h"
#include "ev2/utils/camera.h"

#include "ev2/utils/log.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

glm::vec2 ImageViewerPanel::get_world_cursor_pos()
{
	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();
	glm::mat4 screen_to_world = glm::inverse(rd.proj*rd.view);

	glm::vec2 uv = (glm::vec2(app->input.mouse_pos[0]) -
		glm::vec2(panel_pos.x, panel_pos.y)) / 
		glm::vec2(panel_size.x, panel_size.y); 

	uv = glm::vec2(uv.x, 1.f - uv.y);

	uv = screen_to_world * glm::vec4(2.f*uv - glm::vec2(1.f),0,1);
	uv = 0.5f * (uv + glm::vec2(1.f));

	return glm::vec2(uv); 
}

ImageViewerPanel::ImageViewerPanel(App *app, 
	uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *pipeline, const char *name) : 
	app(app)
{
	static uint32_t ctr = 0;

	panel_idx = ++ctr;

	if (!name) {
		std::string name_str = "Texture" + std::to_string(panel_idx);
		name = name_str.c_str();
	}

	panel.reset(new Panel(app, app->ctx, name, x, y, w, h));
	panel->set_settings([this, ctx = app->ctx]{
		ImGui::BeginChild("FixedWidthWrapper", ImVec2(250, 0), ImGuiChildFlags_AutoResizeY);

		uint32_t max_levels = 0, max_layers = 0;
		uint32_t sel_level = level, sel_layer = layer;

		ev2::get_image_dims(ctx, this->image, nullptr, nullptr, &max_layers, &max_levels);

		if (ImGui::CollapsingHeader("Mip level selector")) {
			for (int i = 0; i <= max_levels - 1; ++i) {
				char namebuf[100];
				sprintf(namebuf, "level_%d", i);

				if (ImGui::Selectable(namebuf, this->level == i)) {
					sel_level = i;
				}
			}
		}

		if (ImGui::CollapsingHeader("Layer selector")) {
			for (int i = 0; i <= max_layers - 1; ++i) {
				char namebuf[100];
				sprintf(namebuf, "layer_%d", i);

				if (ImGui::Selectable(namebuf, this->layer == i)) {
					sel_layer = i;
				}
			}
		}

		if (sel_level != level || sel_layer == layer) {
			this->level = sel_level;
			this->layer = sel_layer;

			this->set_image(ctx, this->image, sel_level, sel_layer);
		}
		if (ImGui::CollapsingHeader("Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {

			ImGui::PushID(this->panel_idx);

			char path[PATH_MAX] {};
			std::string text = "" + pipeline_path;

			ImGui::Text("%s",text.c_str());

			ImGuiInputTextFlags flags =
				ImGuiInputTextFlags_EnterReturnsTrue |
				ImGuiInputTextFlags_ElideLeft; 

			if (ImGui::InputTextWithHint("","Enter pipeline", path, sizeof(path), flags)) {
				if (this->pipeline_path.compare(path)) {
					if (set_pipeline(path) != ev2::SUCCESS) {
						log_warn("%s: Failed to set pipeline to %s", 
							this->panel->get_name(),
							path
						);
					}
				}
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	});

	pipeline_path = pipeline;
}

int ImageViewerPanel::set_pipeline(const char *path)
{
	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(app->ctx, path);

	if (rd.pipeline.is_valid() && rd.pipeline == pipeline)
		return 0;

	if (!EV2_VALID(pipeline))
		return App::ERROR;

	if (rd.bindings.is_valid()) {
		ev2::destroy_bindings(app->ctx, rd.bindings);
	}

	ev2::BindingsID bindings = ev2::create_bindings(
		app->ctx, pipeline, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_DYNAMIC);
	rd.pipeline = pipeline;
	rd.bindings = bindings;
	pipeline_path = path;

	return App::OK;
}

int ImageViewerPanel::init(ev2::GfxContext *ctx, ev2::ImageID image) 
{
	rd.camera = ev2::create_view(ctx, nullptr, nullptr);

	int result = App::OK;
	
	result = set_image(ctx, image, 0, 0);
	if (result)
		return result;

	if (!rd.camera.is_valid() || !rd.tex.is_valid()) {
		result = App::ERROR;
		goto error;
	}

	result = set_pipeline(pipeline_path.c_str());
	if (result)
		goto error;;

	return result;
error:
	destroy(ctx);
	return result;
}

int ImageViewerPanel::update(ev2::GfxContext *ctx)
{
	bool was_resized = false;

	if (panel->update(&was_resized) != App::OK) {
		return App::ERROR;
	}

	if (!panel->imgui()) {
		return App::SHOULD_CLOSE;
	}

	ev2::Result res = ev2::reset_bindings(ctx, rd.bindings);
	if (res != ev2::SUCCESS)
		return App::ERROR;

	res = ev2::bind_texture(app->ctx, rd.bindings, "u_tex", rd.tex);
	if (res != ev2::SUCCESS)
		return App::ERROR;

	ev2::flush_bindings(app->ctx, rd.bindings);

	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();

	float aspect = (float)panel_size.y/(float)panel_size.x;

	if (was_resized || panel->is_content_selected()) {
		rd.zoom *= pow(2, app->input.scroll_delta.y);
		rd.proj = camera_proj_2d(aspect, rd.zoom);

		if (!was_resized && app->input.left_mouse_pressed) {
			glm::dvec2 delta = app->input.get_mouse_delta()/(double)panel->get_size().x; 
			rd.center += 2.f*glm::vec2(glm::vec4(delta.x, -delta.y,0,0)/(aspect*rd.zoom));
			rd.view[3] = glm::vec4(glm::inverse(glm::mat2(rd.view))*rd.center,0,1);
		}

		ev2::update_view(ctx, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));
	}
	return EXIT_SUCCESS;
}

int ImageViewerPanel::set_image(ev2::GfxContext *ctx, 
	ev2::ImageID image, uint32_t level, uint32_t layer)
{
	this->image = image;
	if (rd.tex.is_valid())
		ev2::destroy_texture(ctx, rd.tex);

	rd.tex = ev2::create_texture(ctx, image, ev2::FILTER_NEAREST, level, layer);

	return rd.tex.is_valid() ? App::OK : App::ERROR;
}

ev2::PassID ImageViewerPanel::begin_pass(ev2::GfxContext *ctx)
{
	glm::ivec2 win_size = panel->get_size(); 

	ev2::RenderTargetID window_target = panel->get_target();
	ev2::Rect view_rect = {0,0, 
		(uint32_t)std::max(win_size.x, 1), 
		(uint32_t)std::max(win_size.y, 1)};

	return ev2::begin_gfx_pass(ctx, window_target, rd.camera, view_rect);
}

void ImageViewerPanel::render(ev2::GfxContext *ctx)
{
	if (!image.is_valid()) {
		log_error("Image not initialized.");
		return;
	}

	if (!rd.bindings.is_valid()) {
		log_error("Bindings not initialized.");
		return;
	}

	ev2::PassID pass = this->begin_pass(ctx);
	ev2::cmd_use_image(pass, image, ev2::USAGE_SAMPLED_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, rd.pipeline);
	ev2::cmd_bind_resources(pass, rd.bindings);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 6, 1, 0, 0);
	});
	ev2::end_pass(ctx, pass);
}

void ImageViewerPanel::destroy(ev2::GfxContext *ctx)
{
	if (rd.bindings.is_valid())
		ev2::destroy_bindings(ctx, rd.bindings);
	if (rd.tex.is_valid())
		ev2::destroy_texture(ctx, rd.tex);
	if (rd.camera.is_valid())
		ev2::destroy_view(ctx, rd.camera);
	panel.reset();
}

