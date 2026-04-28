#include <ev2/utils/log.h>

#include <ev2/context.h>
#include <ev2/render.h>
#include <ev2/resource.h>

#include <ev2/utils/camera.h>
#include <ev2/globe/globe.h>

#include "app.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <implot.h>

// std
#include <memory>
#include <cstdlib>

struct WaveSim
{
	App *app;
	Globe *globe;

	ev2::GfxContext *ctx;

	struct RenderData {
		glm::mat4 proj;
		glm::mat4 view;
		ev2::ViewID camera;
	} rd;

	float near = 0.01f, far = 10.f, fov = 0.5*PIf;

	SphericalMotionCamera control;
	glm::vec3 keydir = glm::vec3(0);

	int init();
	int update();

	void render();
	void destroy();
};

int WaveSim::init()
{
	ctx = app->ctx;

	rd.camera = ev2::create_view(ctx, nullptr, nullptr);

	ImPlot::CreateContext();

	app->insert_key_callback([this](int key, int scancode, int action, int mods){
		glfw_wasd_to_motion(this->keydir, key, action);
	});

	globe = globe_create(ctx);

	if (!globe)
		return App::ERROR;

	return App::OK;
}

int WaveSim::update()
{

	Camera camera = {
		.proj = rd.proj,
		.view = rd.view
	};

	GlobeUpdateInfo globe_info = { 
		.camera = &camera 
	};

	static float speedmult = 1.f;

	ImGui::Begin("Editor");
	ImGui::SliderFloat("speed mult", &speedmult, 1.f, 3.f, "%.5f");
	ImGui::End();

	globe_update(globe, &globe_info);
	globe_imgui(globe);
	plot_frame_times(app->input.dt);

	//------------------------------------------------------------------------------
	// Update camera

	double elev = globe_sample_elevation(globe, control.get_pos());
	control.set_min_height(elev + 1e-4);

	glm::dvec2 delta = app->input.get_mouse_delta()/(double)app->win.width;

	float aspect = (float)app->win.height/(float)app->win.width;

	float h = (control.height - 1.) - elev; 

	near = glm::clamp(0.25f*h, 0.0001f, 0.02f);
	far = glm::clamp(1000.f*near, 1.f, 2.f);
	float speed = speedmult*std::min(0.001f + h,1.f);

	rd.proj = camera_proj_3d(fov, aspect, far, near);
	rd.view = control.get_view();

	ev2::update_view(ctx, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	if (app->input.mouse_mode == GLFW_CURSOR_DISABLED) 
		control.rotate(-delta.y,delta.x);

	control.move(app->input.dt*glm::dvec3(speed*keydir));


	return App::OK;
}

void WaveSim::render()
{
	ev2::Rect view_rect = { .x0 = 0, .y0 = 0,
		.w = (uint32_t)app->win.width,
		.h = (uint32_t)app->win.height
	};

	ev2::PassCtx pass = ev2::begin_pass(ctx, {}, rd.camera, view_rect);
	globe_draw(globe,pass);
	ev2::SyncID pass_sync = ev2::end_pass(ctx, pass);

	ev2::submit(pass_sync);
}

void WaveSim::destroy()
{
	globe_destroy(globe);
	ImPlot::DestroyContext();
}

int main(int argc, char *argv[])
{
	std::unique_ptr<App> app (new App{
			1000,
			1000,
			"ev2"
	});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

	ev2::GfxContext *ctx = app->ctx;

	WaveSim sim = {
		.app = app.get()
	};

	if (sim.init() != App::OK)
		return EXIT_FAILURE;

	while (
		app->update() == App::OK &&
		sim.update() == App::OK
	) {
		app->begin_frame();
		sim.render();
		app->end_frame();
	}

	sim.destroy();
	app->terminate();

	return EXIT_SUCCESS;
}
