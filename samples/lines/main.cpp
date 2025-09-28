// local
#include "app.h"
#include "view_utils.h"

#include "engine/utils/camera.h"
#include "engine/utils/geometry.h"
#include "engine/utils/log.h"

#include "engine/renderer/renderer.h"

#include "engine/resource/resource_table.h"
#include "engine/resource/material_loader.h"
#include "engine/resource/shader_loader.h"
#include "engine/resource/texture_loader.h"
#include "engine/resource/model_loader.h"

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

// std
#include <string.h>
#include <complex>

#include "stb_image.h"

struct BaseViewComponent2 : AppComponent
{
	ResourceTable *table;

	RenderTargetID target;

	uint32_t w;
	uint32_t h;

	BaseViewComponent2(ResourceTable *table, int w, int h) 
	{
		this->table = table;
		this->w = (uint32_t)w;
		this->h = (uint32_t)h;

		RenderTargetCreateInfo target_info = {
			.w = (uint32_t)w,
			.h = (uint32_t)w,
			.flags = RENDER_TARGET_CREATE_COLOR_BIT | RENDER_TARGET_CREATE_DEPTH_BIT
		};
		target = render_target_create(table,&target_info);;
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
		render_target_resize(table,target, &target_info);
		return;
	}
};

struct MotionCameraComponent2 : BaseViewComponent2 
{
	MotionCamera control;
	float fov = PIf/2.0f;
	float far = 1000;
	float near = 0.01f;
	glm::dvec3 keydir;

	MotionCameraComponent2(ResourceTable *table, uint32_t w, uint32_t h) : BaseViewComponent2(table, w, h) 
	{
		glm::vec3 eye = glm::vec3(0,0,10);
		control = MotionCamera::from_normal(glm::vec3(1,0,-1),eye);
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
		static float speed = 100;
		
		ImGui::Begin("Demo Window");
		ImGui::SliderFloat("FOV", &fov, 0.0f, PI, "%.3f");
		ImGui::SliderFloat("near", &near, 0.0, 1.0, "%.5f");
		ImGui::SliderFloat("far", &far, near, 2000, "%.5f");
		ImGui::SliderFloat("speed", &speed, 0.0, 100, "%.5f");
		ImGui::End();

		control.update(speed*g_.dt*normalize(keydir));
	}


	virtual void keyCallback(int key, int scancode, int action, int mods) override
	{
		glfw_wasd_to_motion(keydir, key, action);
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

struct ViewComponent2D : BaseViewComponent2 
{
	glm::dvec2 p = glm::dvec2(0);
	double zoom = 0.1;

	ViewComponent2D(ResourceTable *table, uint32_t w, uint32_t h) : BaseViewComponent2(table, w, h) 
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
	float join_thres;
};

struct LinePoint
{
	glm::vec2 pos;
	float length;
	uint32_t padding;
};

struct LineMetadata
{
};

glm::vec2 vec2_from_complex(std::complex<float> c)
{
	return glm::vec2(c.real(),c.imag());
}

struct RandomLine : AppComponent 
{
	Renderer *renderer;
	ResourceTable *table;

	std::shared_ptr<BaseViewComponent> view;

	std::vector<glm::uvec2> indices;
	std::vector<LinePoint> points;

	std::vector<DrawCommand> cmds;
	std::vector<uint32_t> draw_counts;

	GLuint vbo;
	GLuint ibo;
	GLuint vao;

	GLuint ubo;
	GLuint cmd_buf;

	uint32_t ssbo;

	MaterialID material;

	struct LineVert
	{
		glm::vec2 uv;
		uint32_t id;
	};

	static constexpr uint32_t idx[] = {
		0,1,2, 2,1,3
	};

	RandomLine(Renderer *renderer, ResourceTable *table, uint32_t count) : 
		AppComponent(), renderer(renderer), table(table) 
	{
		//----------------------------------------------------------------------------
		// Lines

		std::complex<float> c = 1;
		std::complex<float> v = 0;


		uint32_t segs = 10;

		size_t offset = 0;

		uint32_t edge = 0;
		for (uint32_t k = 0; k < segs; ++k) { 

			LinePoint pt = {};

			uint32_t N = count;// + 2*k;

			for (uint32_t i = 0; i < N; ++i) {
				glm::vec2 pos = glm::vec2(v.real(),v.imag());

				float t = TWOPI*(float)i/(float)(N - 1);

				pos = 10.f*vec2_from_complex(std::polar<float>(1,t) + v);

				pt.length += (i > 0) ? glm::length(pos - pt.pos) : 0; 
				pt.pos = pos;
				pt.padding = k;

				points.push_back(pt);

				if (i + 1 < N) {
					indices.push_back(glm::uvec2(edge, edge + 1));
					++edge;
				}

				if (pt.length > 1e5)
					pt.length = 0;

				v += (0.5f + 0.5f*urandf())*c;

			float tht = PI*(1.0 - 2.0*urandf());
			c *= std::polar<float>(1, tht);
			}

			++edge;

			v += 5.f*(0.5f + 0.5f*urandf())*c;

			DrawCommand cmd = {
				.count = sizeof(idx)/sizeof(idx[0]),
				.instanceCount = count - 1,
				.firstIndex = 0,
				.baseVertex = 0,
				.baseInstance = static_cast<uint32_t>(offset),
			};

			cmds.push_back(cmd);
			draw_counts.push_back(cmd.instanceCount);

			offset += points.size();
		}

		material = material_load_file(table, "material/line.yaml");

		//----------------------------------------------------------------------------
		// Buffers

		glGenBuffers(1,&vbo);
		glGenBuffers(1,&ibo);
		glGenVertexArrays(1,&vao);
		glGenBuffers(1,&cmd_buf);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER,cmd_buf);
		glBufferData(GL_SHADER_STORAGE_BUFFER, cmds.size()*sizeof(DrawCommand), cmds.data(), GL_STATIC_READ);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER,ibo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, indices.size()*sizeof(glm::uvec2), indices.data(), GL_STATIC_READ);

		glBindVertexArray(vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
		glBindVertexArray(0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);

		//----------------------------------------------------------------------------
		// ssbo

		glGenBuffers(1,&ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 
			   sizeof(LinePoint)*points.size(), points.data(), GL_STATIC_READ);
		uniforms.count = points.size();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		
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

		uniforms.count = indices.size();

		glBindBuffer(GL_UNIFORM_BUFFER,ubo);
		glBufferData(GL_UNIFORM_BUFFER,sizeof(uniforms),&uniforms,GL_DYNAMIC_READ);
		glBindBuffer(GL_UNIFORM_BUFFER,0);

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

		ctx->bind_material(material);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cmd_buf);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ibo);

		glDepthMask(GL_FALSE);        // but don’t write any passing depth back into the buffer
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glBindVertexArray(vao);

		//glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cmd_buf);
		//glMultiDrawElementsIndirect(
		//	GL_TRIANGLES, 
		//	GL_UNSIGNED_INT, 
		//	nullptr,
		//	static_cast<uint32_t>(cmds.size()), 
		//	0
		//);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
		glDrawElementsInstanced(
			GL_TRIANGLES, 
			6, 
			GL_UNSIGNED_INT, 
			idx, 
			indices.size()
		);

		glBindVertexArray(0);

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

	uint32_t count = 10;

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
	// Resource tables

	ResourceTableCreateInfo resource_table_info = {
		.resource_path = resource_path
	};
	std::shared_ptr<ResourceTable> table = ResourceTable::create(&resource_table_info); 
	ModelLoader::registration(table.get());
	ImageLoader::registration(table.get());

	std::shared_ptr<ResourceHotReloader> reloader = ResourceHotReloader::create(table.get());

	//-------------------------------------------------------------------------------------------------
	// Renderer

	RendererCreateInfo renderer_info = {
		.resource_table = table.get()
	};

	std::shared_ptr<Renderer> renderer = Renderer::create(&renderer_info);

	if (!renderer) {
		log_error("Failed to create renderer!");
		return EXIT_FAILURE;
	}

	auto view_component = std::shared_ptr<MotionCameraComponent2>( 
		new MotionCameraComponent2(table.get(), params.win.width, params.win.height)
	);
	app->addComponent(view_component);

	auto random_lines = std::make_shared<RandomLine>(renderer.get(),table.get(),count);
	app->addComponent(random_lines);

	//-------------------------------------------------------------------------------------------------
	// test shader 
	
	MaterialID default_meshID = material_load_file(table.get(), "material/default_mesh3d.yaml");
	if (!default_meshID)
		return EXIT_FAILURE;

	MaterialID globe_tileID = material_load_file(table.get(), "material/globe_tile.yaml");
	if (!globe_tileID)
		return EXIT_FAILURE;

	MaterialID box_material = material_load_file(table.get(), "material/box_debug.yaml");
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
		cubeID = ModelLoader::model_load_3d(table.get(),&load_info);;

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
	ModelID sphereID = ModelLoader::model_load_3d(table.get(),&sphereLoadInfo);;

	if (!sphereID) {
		return EXIT_FAILURE;
	}

	//-----------------------------------------------------------------------------
	// main loop

	while (!glfwWindowShouldClose(window)) {
		reloader->process_updates();

		glfwPollEvents();


		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		Camera camera = view_component->get_camera(); 

		FrameBeginInfo frameInfo = {.camera = &camera};
		FrameContext frame = renderer->begin_frame(&frameInfo);

		BeginPassInfo passInfo = {.target = view_component->target};

		RenderContext ctx = frame.begin_pass(&passInfo);

		//ctx.bind_material(default_meshID);
		//ctx.draw_cmd_basic_mesh3d(sphereID,glm::mat4(1.0f));

		app->renderComponents(&ctx);
		frame.end_pass(&ctx);
		renderer->end_frame(&frame);

		renderer->present(ctx.target, app->width, app->height);

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
