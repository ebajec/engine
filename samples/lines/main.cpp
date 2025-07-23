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

struct BaseViewComponent : AppComponent
{
	GLRenderer *renderer;

	RenderTargetID target;

	uint32_t w;
	uint32_t h;

	BaseViewComponent(GLRenderer *renderer, int w, int h) 
	{
		this->renderer = renderer;
		this->w = (uint32_t)w;
		this->h = (uint32_t)h;

		RenderTargetCreateInfo target_info = {
			.w = (uint32_t)w,
			.h = (uint32_t)w,
			.flags = RENDER_TARGET_CREATE_COLOR_BIT | RENDER_TARGET_CREATE_DEPTH_BIT
		};
		target = renderer->create_target(&target_info);;
	}

	virtual Camera get_camera() = 0;

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

struct MotionCameraComponent : BaseViewComponent 
{
	MotionCamera control;
	float fov = PIf/2.0f;
	float far = 1000;
	float near = 0.01f;

	MotionCameraComponent(GLRenderer *renderer, uint32_t w, uint32_t h) : BaseViewComponent(renderer, w, h) 
	{
		glm::vec3 eye = glm::vec3(2,0,0);
		control = MotionCamera::from_normal(glm::vec3(1,0,0),eye);
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

	virtual void onFrameUpdateCallback() override
	{
		static float speed = 10;
		
		ImGui::Begin("Demo Window");
		ImGui::SliderFloat("FOV", &fov, 0.0f, PI, "%.3f");
		ImGui::SliderFloat("near", &near, 0.0, 1.0, "%.5f");
		ImGui::SliderFloat("far", &far, near, 2000, "%.5f");
		ImGui::SliderFloat("speed", &speed, 0.0, 1, "%.5f");
		ImGui::End();

		control.update(speed*g_.dt);
	}


	virtual void keyCallback(int key, int scancode, int action, int mods) override
	{
		control.handle_key_input(key, action);
	}

	virtual Camera get_camera() override
	{
		float aspect = (float)h/(float)w;
		return {
			.proj = camera_proj_3d(fov, aspect, far, near),
			.view = control.get_view()
		};
	}
};

glm::vec2 screen_to_world_2d(Camera camera, glm::vec2 screen) 
{
	return glm::inverse(camera.proj*camera.view)*glm::vec4(screen,0,0);
}

struct ViewComponent2D : BaseViewComponent 
{
	glm::dvec2 p = glm::dvec2(0);
	double zoom = 0.1;

	ViewComponent2D(GLRenderer *renderer, uint32_t w, uint32_t h) : BaseViewComponent(renderer, w, h) 
	{
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

		mouse_pos = glm::dvec2(x,y);

		if (!init++) {
			return;
		};

		// at the top of your frame, after you call ImGui::NewFrame():
		ImGuiIO& io = ImGui::GetIO();

		if (!io.WantCaptureMouse && isLeftClick) {
			Camera cam = get_camera();
			glm::vec2 w = screen_to_world_2d(cam, glm::vec2(dx,-dy));

			p -= w;
		}
	}

	glm::dvec2 click_pos = glm::dvec2(0);
	glm::dvec2 mouse_pos = glm::dvec2(0);

	bool isLeftClick = false;

	virtual void mouseButtonCallback(int button, int action, int mods) override 
	{
		if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
			isLeftClick = true;
			click_pos = mouse_pos;
		}
		if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT) {
			isLeftClick = false;
		}
	}

	virtual void scrollCallback(double xoffset, double yoffset) override 
	{
		zoom = std::min(10., zoom * exp(yoffset*0.2));
	}

	virtual void keyCallback(int key, int scancode, int action, int mods) override
	{
	}

	virtual Camera get_camera() override
	{
		glm::mat4 view = glm::mat4(1.0f);
		view[3] = glm::vec4(-p,0,1);

		float aspect = (float)h/(float)w;
		return {
			.proj = camera_proj_2d(aspect, zoom),
			.view = view
		};
	}

};
static float urandf()
{
	return (float)rand()/(float)RAND_MAX;
}

struct line_uniforms_t
{
	uint count;
	float thickness;
};

struct LinePoint
{
	glm::vec2 pos;
	float length;
	float padding;
};

struct RandomLine : AppComponent 
{
	GLRenderer *renderer;
	ResourceLoader *loader;

	std::shared_ptr<BaseViewComponent> view;

	std::vector<LinePoint> points;
	std::vector<uint32_t> indices;

	gl_vbo vbo;
	gl_vbo ibo;
	gl_vao vao;

	gl_ubo ubo;

	uint32_t ssbo;

	MaterialID material;

	struct LineVert
	{
		glm::vec2 uv;
		uint32_t id;
	};

	
	static constexpr uint32_t verts[] = {
		0x0,0x1,0x2,0x3//,0x4,0x6	
	};

	static constexpr uint32_t idx[] = {
		//0,4,2, 5,4,2, 5,4,1, 3,5,1
		0,1,2, 2,1,3
	};

	RandomLine(GLRenderer *renderer, ResourceLoader *loader, uint32_t count) : 
		AppComponent(), renderer(renderer), loader(loader) 
	{
		//----------------------------------------------------------------------------
		// Lines

		std::complex<float> c = 1;
		std::complex<float> v = 0;

		LinePoint pt = {};

		for (uint32_t i = 0; i < count; ++i) {

			v += 5*(0.5f + 0.5f*urandf())*c;

			float tht = PI*(1.0 - 2.0*urandf());
			c *= std::polar<float>(1, tht);

			glm::vec2 next = glm::vec2(v.real(),v.imag());

			float t = TWOPI*(float)i/(float)(count - 1);
			//next = glm::vec2(1+sin(t) - cos(7*t), cos(t) + sin(7*t));

			if (i > 0)
				pt.length += glm::length(next - pt.pos); 

			pt.pos = next;

			points.push_back(pt);

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
			   sizeof(LinePoint)*points.size(), points.data(), GL_STATIC_READ);
		uniforms.count = points.size();
		
		//----------------------------------------------------------------------------
		// ubo

		glGenBuffers(1, &ubo);
	}

	void upload_points()
	{

	}
	
	line_uniforms_t uniforms = {
		.count = 0,
		.thickness = 0.1
	};

	virtual void onRender(const RenderContext *ctx) override
	{
		if (ImGui::Begin("Demo Window")) {
			ImGui::SliderFloat("thickness", &uniforms.thickness, 0.0f, 1, "%.3f");
		}
		ImGui::End();

		uniforms.count = points.size();

		glBindBuffer(GL_UNIFORM_BUFFER,ubo);
		glBufferData(GL_UNIFORM_BUFFER,sizeof(uniforms),&uniforms,GL_DYNAMIC_READ);
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

		renderer->bind_material(material);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);

		glDepthMask(GL_FALSE);        // but don’t write any passing depth back into the buffer
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glBindVertexArray(vao);
		glDrawElementsInstanced(
			GL_TRIANGLES, 
			sizeof(idx)/sizeof(uint32_t), 
			GL_UNSIGNED_INT, 
			nullptr, 
			points.size() - 1
		);

		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);

		glFinish();
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

	uint32_t count = 1000;

	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i],"--resources")) {
			resource_path = argv[++i];	
		}
		if (!strcmp(argv[i],"--count")) {
			if (i + 1 < argc) {
				count = strtoul(argv[i + 1],NULL,10);
				++i;
			}
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

	auto view_component = std::shared_ptr<MotionCameraComponent>( 
		new MotionCameraComponent(renderer.get(), params.win.width, params.win.height)
	);
	app->addComponent(view_component);

	auto random_lines = std::make_shared<RandomLine>(renderer.get(),loader.get(),count);
	app->addComponent(random_lines);

	//-------------------------------------------------------------------------------------------------
	// test shader 
	
	MaterialID default_meshID = load_material_file(loader.get(), "material/default_mesh3d.yaml");
	if (!default_meshID)
		return EXIT_FAILURE;

	MaterialID globe_tileID = load_material_file(loader.get(), "material/globe_tile.yaml");
	if (!globe_tileID)
		return EXIT_FAILURE;

	MaterialID box_material = load_material_file(loader.get(), "material/box_debug.yaml");
	if (!box_material)
		return EXIT_FAILURE;
	
	//-----------------------------------------------------------------------------
	// cube
	ModelID cubeID; {
		std::vector<vertex3d> verts;
		std::vector<uint32_t> indices;

		//globe::mesh_cube_map(100,100,100, verts, indices);

		Mesh3DCreateInfo load_info = {
			.data = verts.data(), .vcount = verts.size(),
			.indices = indices.data(), .icount = indices.size(),
		};
		cubeID = load_model_3d(loader.get(),&load_info);;

		if (!cubeID) {
			return EXIT_FAILURE;
		}
	}
	
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

		//renderer->bind_material(default_meshID);
		//renderer->draw_cmd_basic_mesh3d(sphereID,glm::mat4(1.0f));
		renderer->bind_material(globe_tileID);
		app->renderComponents(&ctx);

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
