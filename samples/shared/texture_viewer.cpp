#include "texture_viewer.h"
#include "engine/utils/camera.h"

glm::vec2 TextureViewerPanel::get_world_cursor_pos()
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

int TextureViewerPanel::init(App *app_, ev2::Device *dev, ev2::TextureID tex) 
{
	app = app_;

	panel = std::make_unique<Panel>(dev,"Simulation",200,0,500,500);

	rd.screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	if (!EV2_VALID(rd.screen_quad))
		return EXIT_FAILURE;

	rd.camera = ev2::create_view(dev, nullptr, nullptr);

	ev2::DescriptorLayoutID screen_quad_layout = 
		ev2::get_graphics_pipeline_layout(dev, rd.screen_quad);

	rd.screen_quad_set = ev2::create_descriptor_set(dev, screen_quad_layout);
	rd.tex_slot = ev2::find_binding(screen_quad_layout, "u_tex");

	int result = this->set_texture(dev, tex);

	if (result)
		return result;

	return 0;
}

int TextureViewerPanel::set_texture(ev2::Device *dev, ev2::TextureID tex)
{
	ev2::bind_texture(dev, rd.screen_quad_set, rd.tex_slot, tex);
	rd.map = tex;

	return 0;
}

int TextureViewerPanel::update(ev2::Device *dev)
{
	panel->imgui(); 

	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();

	float aspect = (float)panel_size.y/(float)panel_size.x;
	float zoom = pow(2, app->input.scroll.y);

	rd.proj = camera_proj_2d(aspect, zoom);

	glm::mat4 p_inv = glm::inverse(rd.proj);

	if (panel->is_content_selected()) {
		if (app->input.left_mouse_pressed && panel->is_content_selected()) {
			glm::dvec2 delta = app->input.get_mouse_delta()/(double)panel->get_size().x; 
			rd.center += 2.f*glm::vec2(glm::vec4(delta.x, -delta.y,0,0)/(aspect*zoom));
		}

		rd.view[3] = glm::vec4(glm::inverse(glm::mat2(rd.view))*rd.center,0,1);
	}
		
	ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));
	return EXIT_SUCCESS;
}

void TextureViewerPanel::render(ev2::Device *dev)
{
	glm::ivec2 win_size = panel->get_size(); 

	ev2::RenderTargetID window_target = panel->get_target();
	ev2::Rect view_rect = {0,0, (uint32_t)win_size.x, (uint32_t)win_size.y};

	ev2::PassCtx pass = ev2::begin_pass(dev, window_target, rd.camera, view_rect);
	ev2::cmd_bind_gfx_pipeline(pass.rec, rd.screen_quad);
	ev2::cmd_bind_descriptor_set(pass.rec, rd.screen_quad_set);
	ev2::cmd_draw_screen_quad(pass.rec);
	ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

	ev2::submit(pass_sync);
}

void TextureViewerPanel::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, rd.screen_quad_set);
	panel.reset();
}

