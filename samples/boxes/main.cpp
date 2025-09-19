#include <opengl.h>
#include <utils/log.h>

#include "app.h"
#include "geometry.h"

#include "camera_controller.h"
#include "view_utils.h"

#include "resource/resource_table.h"
#include "resource/material_loader.h"
#include "resource/shader_loader.h"
#include "resource/texture_loader.h"
#include "resource/model_loader.h"

// stb
#include "stb_image.h"

// imgui
#include <imgui.h>
#include <implot.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

// libc
#include <stdlib.h>
#include <string.h>

GLFWwindow *g_window;

int main(int argc, char* argv[])
{
	stbi_set_flip_vertically_on_load(true);
	log_set_flags(LOG_ERROR_BIT | LOG_INFO_BIT);

	if (!glfwInit())
		return EXIT_FAILURE;
	
	struct {
		struct{
			const char *title = "test";
			int width = 800;
			int height = 800;
			int x = 200;
			int y = 200;
		} win;
	} params;

#if defined(RESOURCE_PATH)
	const char* resource_path = RESOURCE_PATH;
#else 
	const char* resource_path = NULL;
#endif

	const char *preferred_monitor = NULL;

	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i],"--resources")) {
			resource_path = argv[++i];	
		}
		if (!strcmp(argv[i], "--monitor")) {
			preferred_monitor = argv[++i];
		}
	}

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

	GLFWwindow *window = glfwCreateWindow(
		params.win.width, 
		params.win.height, 
		params.win.title, NULL, NULL);

	if (!window) {
	    fprintf(stderr, "ERROR: could not open window with GLFW3\n");
	    glfwTerminate();
		return EXIT_FAILURE;
	}

	g_window = window;

	glfwGetFramebufferSize(window, &params.win.width, &params.win.height);

	glfwSetWindowPos(window,
		params.win.x,
		params.win.y
	);

	if (int code = glfw_init_gl_basic(window); code == EXIT_FAILURE) {
		fprintf(stderr, "ERROR: Failed to load OpenGL\n");
		return code; 
	}

	//-----------------------------------------------------------------------------
	// App
	std::unique_ptr<MyApp> app = MyApp::create(window);
	if (!app) {
		log_error("Failed to initialize applicaition window");
		return EXIT_FAILURE;
	}

	ImPlot::CreateContext();
	
	//-----------------------------------------------------------------------------
	// Resource tables

	ResourceTableCreateInfo resource_table_info = {
		.resource_path = resource_path
	};
	std::unique_ptr<ResourceTable> rt = ResourceTable::create(&resource_table_info); 
	ImageLoader::registration(rt.get());
	ModelLoader::registration(rt.get());

	std::unique_ptr<ResourceHotReloader> reloader = ResourceHotReloader::create(rt.get());

	//----------------------------------------------------------------------------
	// Renderer

	GLRendererCreateInfo renderer_info = {
		.resource_table = rt.get()
	};

	std::unique_ptr<GLRenderer> renderer = GLRenderer::create(&renderer_info);

	if (!renderer) {
		log_error("Failed to create renderer!");
		return EXIT_FAILURE;
	}

	auto view_component = std::shared_ptr<BaseViewComponent>( 
		new BaseViewComponent(rt.get(), params.win.width, params.win.height)
	);

	auto sphere_camera = std::shared_ptr<MotionCameraComponent>(
		new MotionCameraComponent(view_component)
	);

	app->addComponent(view_component);
	app->addComponent(sphere_camera);

	//-----------------------------------------------------------------------------
	// test shader 
	
	MaterialID default_meshID = load_material_file(rt.get(), "material/default_mesh3d.yaml");
	if (!default_meshID)
		return EXIT_FAILURE;

	std::vector<vertex3d> verts;
	std::vector<uint32_t> indices;

	ModelID torus = geometry::mesh_torus(2.f,1.f, 100, 100, verts, indices);

	//-----------------------------------------------------------------------------
	// main loop
	
	double t0 = 0, t1 = 0;

	while (!glfwWindowShouldClose(window)) {
		reloader->process_updates();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		t0 = t1;
		t1 = glfwGetTime();

		glfwPollEvents();

		app->onFrameUpdateCallback(window);

		Camera camera = {
			.proj = view_component->get_proj(),
			.view = sphere_camera->control.get_view()
		};

		FrameBeginInfo frameInfo = {.camera = &camera};
		FrameContext frame = renderer->begin_frame(&frameInfo);

		BeginPassInfo passInfo = {.target = view_component->target};
		RenderContext ctx = frame.begin_pass(&passInfo);
		
		frame.end_pass(&ctx);
		renderer->end_frame(&frame);

		renderer->present(ctx.target, app->width,app->height);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());	

        glfwSwapBuffers(window);   
	}

	renderer.reset(nullptr);
	reloader.reset(nullptr);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	rt.reset(nullptr);

	glfwDestroyWindow(window);
	glfwTerminate();

	return EXIT_SUCCESS;
}
