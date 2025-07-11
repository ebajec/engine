// local

#include "resource_loader.h"
#include "material_loader.h"
#include "shader_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

#include "gl_renderer.h"

#include "camera_controller.h"
#include "app.h"
#include "geometry.h"

#include "utils/log.h"

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

// glad
#include <glad/glad.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

// std
#include <string.h>

#include "stb_image.h"

struct ViewComponent : AppComponent
{
	GLRenderer *renderer;

	MotionCamera control;

	RenderTargetID target;

	uint32_t w;
	uint32_t h;

	static ViewComponent *create(GLRenderer *renderer, int w, int h) 
	{
		ViewComponent *component = new ViewComponent();

		glm::vec3 eye = glm::vec3(10,0,0);

		component->renderer = renderer;
		component->control = MotionCamera::from_normal(glm::vec3(1,0,0),eye);
		component->w = (uint32_t)w;
		component->h = (uint32_t)h;

		RenderTargetCreateInfo target_info = {
			.w = (uint32_t)w,
			.h = (uint32_t)w,
			.flags = RENDER_TARGET_CREATE_COLOR_BIT | RENDER_TARGET_CREATE_DEPTH_BIT
		};
		component->target = renderer->create_target(&target_info);;
		return component;
	}

	Camera get_camera()
	{
		float fov = PIf/2.0f;
		float far = 100;
		float near = 0.1f;
		float aspect = (float)h/(float)w;

		return {
			.proj = camera_proj_3d(fov,aspect,far,near),
			.view = control.get_view()
		};
	}

	virtual void cursorPosCallback(double xpos, double ypos) override 
	{
		static int init = 0;
		static double xold = 0, yold = 0;

		double xmid = 0.5*(double)w;
		double ymid = 0.5*(double)h;

		double x = (xpos - xmid)/xmid; 
		double y = (ypos - ymid)/ymid; 

		double dx = x - xold;
		double dy = y - yold;

		xold = x;
		yold = y;

		if (!init++) {
			return;
		}

		if (g_.mouse_mode == GLFW_CURSOR_DISABLED) {
			control.rotate(-dx, dy);
		}

	}

	virtual void keyCallback(int key, int scancode, int action, int mods) override
	{
		control.handle_key_input_wasd(key, action);
	}

	virtual void onFrameUpdateCallback() override
	{
		control.update();
	}

	virtual void framebufferSizeCallback(int width, int height) override 
	{
		w = (uint32_t)width;
		h = (uint32_t)height;
		RenderTargetCreateInfo target_info = {
			.w = static_cast<uint32_t>(width),
			.h = static_cast<uint32_t>(height),
			.flags = RENDER_TARGET_CREATE_COLOR_BIT | RENDER_TARGET_CREATE_DEPTH_BIT
		};
		renderer->reset_target(target, &target_info);
		return;
	}
};

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

	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i],"--resources")) {
			resource_path = argv[++i];	
		}
	}

	GLFWwindow *window = glfwCreateWindow(
		params.win.width, 
		params.win.height, 
		params.win.title, NULL, NULL);

	if (!window) {
	    fprintf(stderr, "ERROR: could not open window with GLFW3\n");
	    glfwTerminate();
		return EXIT_FAILURE;
	}

	glfwSetWindowPos(window,
		params.win.x,
		params.win.y
	);

	if (int code = glfw_init_gl_basic(window); code == EXIT_FAILURE) {
		fprintf(stderr, "ERROR: Failed to load OpenGL\n");
		return code; 
	}

	//-------------------------------------------------------------------------------------------------
	// App
	std::unique_ptr<MyApp> app = MyApp::create(window);
	if (!app) {
		log_error("Failed to initialize applicaition window");
		return EXIT_FAILURE;
	}
	
	//-------------------------------------------------------------------------------------------------
	// Resource loaders

	ResourceLoaderCreateInfo resource_loader_info = {
		.resource_path = resource_path
	};
	std::shared_ptr<ResourceLoader> loader = ResourceLoader::create(&resource_loader_info); 

	std::shared_ptr<ResourceHotReloader> hot_reloader = ResourceHotReloader::create(loader);

	//-------------------------------------------------------------------------------------------------
	// Renderer

	GLRendererCreateInfo renderer_info = {
		.resource_loader = loader
	};

	std::shared_ptr<GLRenderer> renderer = GLRenderer::create(&renderer_info);

	if (!renderer) {
		log_error("Failed to create renderer!");
		return EXIT_FAILURE;
	}

	std::shared_ptr<ViewComponent> view_component (
		ViewComponent::create(renderer.get(), params.win.width, params.win.height));
	app->addComponent(view_component);

	//-------------------------------------------------------------------------------------------------
	// test shader 
	
	MaterialID materialID = load_material_file(loader.get(), "material/default_mesh3d.yaml");
	if (!materialID)
		return EXIT_FAILURE;

	//-----------------------------------------------------------------------------
	// sphere

	std::vector<vertex3d> sphereVerts;
	std::vector<uint32_t> sphereIndices;

	geometry::mesh_s2(100,50,sphereVerts,sphereIndices);

	Mesh3DCreateInfo sphereLoadInfo = {
		.data = sphereVerts.data(), .vcount = sphereVerts.size(),
		.indices = sphereIndices.data(), .icount = sphereIndices.size(),
	};
	ModelID sphereID = load_model_3d(loader.get(),&sphereLoadInfo);;

	if (!sphereID) {
		return EXIT_FAILURE;
	}

	//-----------------------------------------------------------------------------
	// torus

	std::vector<vertex3d> vtorus;
	std::vector<uint32_t> itorus;

	geometry::mesh_torus(5.0,1.0,100,100,vtorus,itorus);

	Mesh3DCreateInfo torusLoadInfo = {
		.data = vtorus.data(), .vcount = vtorus.size(),
		.indices = itorus.data(), .icount = itorus.size(),
	};
	ModelID torusID = load_model_3d(loader.get(),&torusLoadInfo);

	//-----------------------------------------------------------------------------
	// main loop

	while (!glfwWindowShouldClose(window)) {
		hot_reloader->process_updates();

		glfwPollEvents();

		renderer->begin_frame(app->width, app->height);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		RenderContext ctx = {
			.target = view_component->target,
			.camera = view_component->get_camera() 
		};

		renderer->begin_pass(&ctx);

		renderer->bind_material(materialID);

		renderer->draw_cmd_basic_mesh3d(torusID,glm::mat4(1.0f));
		renderer->draw_cmd_basic_mesh3d(sphereID,glm::mat4(1.0f));

		renderer->end_pass(&ctx);

		renderer->draw_target(ctx.target, glm::mat4(1.0f));

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());	

		renderer->end_frame();
        glfwSwapBuffers(window);   

		app->onFrameUpdateCallback(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	return EXIT_SUCCESS;
}
