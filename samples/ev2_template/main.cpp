#include <ev2/utils/log.h>

#include <ev2/context.h>
#include <ev2/render.h>
#include <ev2/resource.h>

#include <ev2/utils/camera.h>
#include "app.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdlib>

struct MyStuff
{
	App *app;

	struct RenderData {
		glm::mat4 proj;
		glm::mat4 view;
		ev2::ViewID camera;
	} rd;

	int init(ev2::Device *dev);
	int update(ev2::Device *dev);

	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

int MyStuff::init(ev2::Device *dev)
{
	rd.camera = ev2::create_view(dev, nullptr, nullptr);

	return EXIT_SUCCESS;
}

int MyStuff::update(ev2::Device *dev)
{
	rd.proj = camera_proj_2d((float)app->win.height/(float)app->win.width, 1.f);
	rd.view = glm::mat4(1.f);

	ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	return App::OK;
}

void MyStuff::render(ev2::Device *dev)
{
	ev2::Rect view_rect = { .x0 = 0, .y0 = 0,
		.w = (uint32_t)app->win.width,
		.h = (uint32_t)app->win.height
	};

	ev2::PassCtx pass = ev2::begin_pass(dev, {}, rd.camera, view_rect);
	ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

	ev2::submit(pass_sync);
}

void MyStuff::destroy(ev2::Device *dev)
{
}

int main(int argc, char *argv[])
{
	std::unique_ptr<App> app (new App{
		.win = {
			.width = 1000,
			.height = 1000,
			.title = "ev2"
		}
	});

	if (app->initialize(argc,argv) != App::OK)
		return EXIT_FAILURE;

	ev2::Device *dev = app->dev;

	MyStuff data = {
		.app = app.get()
	};

	if (data.init(dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	while (
		app->update() == App::OK &&
		data.update(dev) == App::OK
	) {
		app->begin_frame();
		data.render(dev);
		app->end_frame();
	}

	data.destroy(dev);
	app->terminate();

	return EXIT_SUCCESS;
}
