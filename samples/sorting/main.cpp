#include "app.h"
#include "panel.h"

#include <ev2/utils/log.h>

#include <ev2/context.h>
#include <ev2/resource.h>

#include <ev2/utils/camera.h>
#include <ev2/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdlib>

struct TestApp : public App
{
	TestApp() : App(1200, 500, "ev2_test") {
	}

	int initialize(int argc, char **argv);
	int update();
	void render();
	void destroy();
};

int TestApp::initialize(int argc, char **argv)
{
	int result = App::initialize(argc, argv);
	if (result)
		return result;

	return result;
}
int TestApp::update()
{
	int result = EXIT_SUCCESS;

	return result;
}
void TestApp::render()
{
}
void TestApp::destroy()
{
	App::terminate();
}

int main(int argc, char *argv[])
{
	std::unique_ptr<TestApp> app (new TestApp{});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

	int status;

	for (;;)
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

	return status;
}
