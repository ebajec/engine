#include "editor.h"

#include "image_viewer.h"
#include "ev2/utils/camera.h"

#include "ev2/utils/log.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

glm::vec2 ImageViewer2::get_grid_cursor_pos()
{
	glm::ivec2 viewport_size = Viewport2::get_size();
	glm::ivec2 viewport_pos = Viewport2::get_pos();
	glm::mat4 screen_to_world = glm::inverse(rd.proj*rd.view);

	glm::vec2 uv = (glm::vec2(Editor::input().mouse_pos[0]) -
		glm::vec2(viewport_pos.x, viewport_pos.y)) / 
		glm::vec2(viewport_size.x, viewport_size.y); 

	glm::uvec2 image_size;

	ev2::get_image_dims(Editor::ctx(), image, &image_size.x, &image_size.y, nullptr);

	uv = glm::vec2(uv.x, 1.f - uv.y);

	float aspect = float(image_size.x)/float(image_size.y);

	uv = screen_to_world * glm::vec4(2.f*uv - glm::vec2(1.f),0,1);
	uv = 0.5f * (uv + glm::vec2(aspect, 1.f));

	return glm::vec2(uv); 
}

ImageViewer2::ImageViewer2(
	uint32_t x, 
	uint32_t y, 
	uint32_t w, 
	uint32_t h, 
	const char *pipeline,
	const char *name) :
	Viewport2(name, x, y, w, h)
{
	extend_settings([this, ctx = Editor::ctx()]{
		ImGui::BeginChild("FixedWidthWrapper", ImVec2(250, 0), ImGuiChildFlags_AutoResizeY);

		uint32_t max_levels = 0, max_layers = 0;
		uint32_t sel_level = level, sel_layer = layer;

		ev2::get_image_dims(ctx, this->image, nullptr, nullptr, &max_layers, &max_levels);

		if (ImGui::CollapsingHeader("Mip level selector")) {
			for (uint32_t i = 0; i < max_levels; ++i) {
				char namebuf[100];
				snprintf(namebuf, sizeof(namebuf), "level_%u", i);

				if (ImGui::Selectable(namebuf, this->level == i)) {
					sel_level = i;
				}
			}
		}

		if (ImGui::CollapsingHeader("Layer selector")) {
			for (uint32_t i = 0; i < max_layers; ++i) {
				char namebuf[100];
				snprintf(namebuf, sizeof(namebuf), "layer_%u", i);

				if (ImGui::Selectable(namebuf, this->layer == i)) {
					sel_layer = i;
				}
			}
		}

		if (ImGui::CollapsingHeader("Filter")) {
			if (ImGui::Selectable("Nearest", this->rd.filter == ev2::FILTER_NEAREST)) {
				set_texture_filter(ev2::FILTER_NEAREST);
			}
			if (ImGui::Selectable("Bilinear", this->rd.filter == ev2::FILTER_BILINEAR)) {
				set_texture_filter(ev2::FILTER_BILINEAR);
			}
		}

		if (sel_level != level || sel_layer != layer) {
			this->level = sel_level;
			this->layer = sel_layer;

			this->set_image(ctx, this->image, sel_level, sel_layer);
		}
		if (ImGui::CollapsingHeader("Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {

			ImGui::PushID(Viewport2::get_id());

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
							this->get_name(),
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

ImageViewer2::~ImageViewer2()
{
	destroy(Editor::ctx());
}

void ImageViewer2::set_texture_filter(ev2::TextureFilter filter)
{
	ev2::GfxContext *ctx = Editor::ctx();

	if (filter != rd.filter) {
		ev2::destroy_texture(ctx, rd.tex);
		rd.tex = ev2::create_texture(ctx, image, filter, level, layer);
	}
	rd.filter = filter;
}

int ImageViewer2::set_pipeline(const char *path)
{
	ev2::GfxContext *ctx = Editor::ctx();

	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, path);

	if (rd.pipeline.is_valid() && rd.pipeline == pipeline)
		return 0;

	if (!EV2_VALID(pipeline))
		return Editor::ERROR;

	if (rd.bindings.is_valid()) {
		ev2::destroy_bindings(ctx, rd.bindings);
	}

	ev2::BindingsID bindings = ev2::create_bindings(
		ctx, pipeline, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_DYNAMIC);
	rd.pipeline = pipeline;
	rd.bindings = bindings;
	pipeline_path = path;

	return Editor::OK;
}

int ImageViewer2::init(ev2::GfxContext *ctx, ev2::ImageID img)
{
	rd.camera = ev2::create_view(ctx, nullptr, nullptr);

	int result = Editor::OK;

	result = set_image(ctx, img, 0, 0);
	if (result)
		return result;

	if (!rd.camera.is_valid() || !rd.tex.is_valid()) {
		result = Editor::ERROR;
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

int ImageViewer2::update(ev2::GfxContext *ctx)
{
	int status = Editor::OK;
	int flags = 0;

	if (status = Viewport2::imgui(&flags); status != Editor::OK) {
		return status;
	}

	ev2::Result res = ev2::reset_bindings(ctx, rd.bindings);
	if (res != ev2::SUCCESS)
		return Editor::ERROR;

	res = ev2::bind_texture(ctx, rd.bindings, "u_tex", rd.tex);
	if (res != ev2::SUCCESS)
		return Editor::ERROR;

	ev2::flush_bindings(ctx, rd.bindings);

	glm::ivec2 viewport_size = get_size();

	float aspect = (float)viewport_size.y/(float)viewport_size.x;

	const Editor::InputData &input = Editor::input();

	const bool was_resized = flags & Viewport2::RESIZED_BIT;

	if (was_resized || is_content_selected()) {
		rd.zoom *= powf(2.f, (float)input.scroll_delta.y);
		rd.proj = camera_proj_2d(aspect, rd.zoom);

		if (!was_resized && input.left_mouse_pressed) {
			glm::dvec2 delta = input.get_mouse_delta()/(double)get_size().x; 
			rd.center += 2.f*glm::vec2(glm::vec4(delta.x, -delta.y,0,0)/(aspect*rd.zoom));
			rd.view[3] = glm::vec4(glm::inverse(glm::mat2(rd.view))*rd.center,0,1);
		}

		ev2::update_view(ctx, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));
	}
	return Editor::OK;
}

int ImageViewer2::set_image(ev2::GfxContext *ctx,
	ev2::ImageID img, uint32_t lvl, uint32_t lyr)
{
	this->image = img;
	if (rd.tex.is_valid())
		ev2::destroy_texture(ctx, rd.tex);

	rd.tex = ev2::create_texture(ctx, img, rd.filter, lvl, lyr);

	return rd.tex.is_valid() ? Editor::OK : Editor::ERROR;
}

void ImageViewer2::record_draw(ev2::PassID pass)
{
	ev2::cmd_use_image(pass, image, ev2::USAGE_SAMPLED_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, rd.pipeline);
	ev2::cmd_bind_resources(pass, rd.bindings);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 6, 1, 0, 0);
	});
}

void ImageViewer2::render(ev2::GfxContext *ctx)
{
	if (!image.is_valid()) {
		log_error("Image not initialized.");
		return;
	}

	if (!rd.bindings.is_valid()) {
		log_error("Bindings not initialized.");
		return;
	}

	ev2::GfxPassInfo pass_info = {
		.target = get_target(),
		.view = rd.camera,
		.clear_color = true,
		.clear_depth = true,
		.name = get_name()
	};
	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
	record_draw(pass);
	ev2::end_pass(ctx, pass);
}

void ImageViewer2::destroy(ev2::GfxContext *ctx)
{
	if (rd.bindings.is_valid())
		ev2::destroy_bindings(ctx, rd.bindings);
	if (rd.tex.is_valid())
		ev2::destroy_texture(ctx, rd.tex);
	if (rd.camera.is_valid())
		ev2::destroy_view(ctx, rd.camera);
}

