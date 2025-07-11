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
#include <complex>

#include "stb_image.h"

struct ViewComponent : AppComponent
{
	GLRenderer *renderer;

	MotionCamera control;

	RenderTargetID target;

	uint32_t w;
	uint32_t h;

	float fov = PIf/2.0f;
	float far = 100;
	float near = 0.01f;

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
		float aspect = (float)h/(float)w;

		return {
			.proj = camera_proj_3d(fov, aspect, far, near),//camera_proj_2d(aspect),
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
		static float speed = 0.5;
		
		ImGui::Begin("Demo Window");
		ImGui::SliderFloat("FOV", &fov, 0.0f, PI, "%.3f");
		ImGui::SliderFloat("near", &near, 0.0, 1.0, "%.3f");
		ImGui::SliderFloat("speed", &speed, 0.0, 100, "%.3f");
		ImGui::End();

		control.update(speed);
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

static float urandf()
{
	return (float)rand()/(float)RAND_MAX;
}

struct RandomLine : AppComponent 
{
	GLRenderer *renderer;
	ResourceLoader *loader;

	std::vector<glm::vec2> points;
	std::vector<uint32_t> indices;

	gl_vbo vbo;
	gl_vbo ibo;
	gl_vao vao;

	uint32_t ssbo;

	MaterialID material;

	static constexpr uint32_t verts[] = {
		0,1,2,3	
	};

	static constexpr uint32_t idx[] = {
		0,1,2,1,2,3
	};

	RandomLine(GLRenderer *renderer, ResourceLoader *loader) : 
		AppComponent(), renderer(renderer), loader(loader) 
	{
		//----------------------------------------------------------------------------
		// Lines

		static uint32_t count = 1000000;

		std::complex<float> c = 1;
		std::complex<float> v = 0;

		for (uint32_t i = 0; i < count; ++i) {

			float r = urandf();
			v += (1.0f + 50*urandf())*r*c;

			float tht = 0.3*HALFPI*urandf();
			c *= std::polar<float>(1, tht);

			if (i % rand() == 0)
				c *= std::polar<float>(1,TWOPI);

			points.push_back(glm::vec2(v.real(),v.imag()));

			indices.push_back(i);
			if (i + 1 < count)
				indices.push_back(i + 1);
		}

		material = load_material_file(loader, "material/line.yaml");

		//----------------------------------------------------------------------------
		// Buffers

		glGenBuffers(1,&vbo);
		glGenBuffers(1,&ibo);
		glGenVertexArrays(1,&vao);

		glBindVertexArray(vao);

		glBindBuffer(GL_ARRAY_BUFFER,vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_READ);

		glEnableVertexArrayAttrib(vao,0);
		glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, 0, (void*)0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_READ);

		glBindVertexArray(0);

		//----------------------------------------------------------------------------
		// ssbo

		glGenBuffers(1,&ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 
			   sizeof(glm::vec2)*points.size(), points.data(), GL_STATIC_READ);
	}

	void upload_points()
	{

	}

	virtual void onRender() override
	{
		renderer->bind_material(material);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 
			   sizeof(glm::vec2)*points.size(), points.data(), GL_STATIC_READ);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glDisable(GL_DEPTH_TEST);

		glBindVertexArray(vao);
		glDrawElementsInstanced(
			GL_TRIANGLES, 
			sizeof(indices)/sizeof(uint32_t), 
			GL_UNSIGNED_INT, 
			nullptr, 
			points.size() - 1
		);

		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);

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

	auto random_lines = std::make_shared<RandomLine>(renderer.get(),loader.get());
	app->addComponent(random_lines);

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
		//renderer->draw_cmd_basic_mesh3d(sphereID,glm::mat4(1.0f));

		app->renderComponents();

		renderer->end_pass(&ctx);
		renderer->draw_target(ctx.target, glm::mat4(1.0f));

		renderer->end_frame();

		app->onFrameUpdateCallback(window);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());	

        glfwSwapBuffers(window);   

	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	return EXIT_SUCCESS;
}
