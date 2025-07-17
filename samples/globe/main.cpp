// local

#include "resource_loader.h"
#include "material_loader.h"
#include "shader_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

#include "geometry.h"
#include "globe.h"

#include "view_utils.h"

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

static float urandf()
{
	return (float)rand()/(float)RAND_MAX;
}

frustum_t camera_frustum(const glm::dmat4& view, const glm::dmat4& proj)
{
	glm::mat4 m = glm::transpose(proj * view);
	
	frustum_t frust;
	for (int i = 0; i < 6; ++i) {
		glm::vec4 p;
		switch (i) {
			case 0: p = m[3] + m[2]; break; // near
			case 1: p = m[3] - m[2]; break; // far
			case 2: p = m[3] + m[0]; break; // left
			case 3: p = m[3] - m[0]; break; // right
			case 4: p = m[3] + m[1]; break; // bottom
			case 5: p = m[3] - m[1]; break; // top
		}

		float r_inv = 1.0f / glm::length(glm::vec3(p));
		frust.planes[i].n = glm::vec3(p) * r_inv;
		frust.planes[i].d = p.w         * r_inv;
	}
	
	return frust;
}

struct globe_t
{
	std::vector<globe::tile_code_t> tiles;

	std::vector<vertex3d> verts;
	std::vector<uint32_t> indices;

	ModelID meshID;

	static int create(globe_t * g, ResourceLoader* loader)
	{
		ModelID mesh = model_create(loader);

		if (!mesh)
			return -1;

		g->meshID = mesh;
		return 0;
	}

	int update(ResourceLoader *loader, Camera camera) 
	{
		tiles.clear();
		verts.clear();
		indices.clear();

		frustum_t frust = camera_frustum(camera.view,camera.proj);

		static int zoom = 3;

		ImGui::Begin("Demo Window");
		ImGui::SliderInt("res", &zoom, 0, 5, "%d");
		ImGui::End();

		double res = globe::tile_area(zoom);

		glm::dvec3 pos = camera_get_pos(camera.view);
		globe::select_tiles(tiles, frust, pos, res);
		globe::create_mesh(1,pos, tiles, verts, indices);

		Mesh3DCreateInfo ci = {
			.data = verts.data(),
			.vcount = verts.size(),
			.indices = indices.data(),
			.icount = indices.size()
		};

		LoadResult result = loader->upload(meshID, RESOURCE_LOADER_MODEL_3D, &ci); 

		if (result != RESULT_SUCCESS) {
			log_error("Failed to upload globe mesh");
			return -1;
		}

		return 0;
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

	auto view_component = std::shared_ptr<BaseViewComponent>( 
		new BaseViewComponent(renderer.get(), params.win.width, params.win.height)
	);
	app->addComponent(view_component);

	auto sphere_camera = std::shared_ptr<MotionCameraComponent>(new MotionCameraComponent(view_component));
	app->addComponent(sphere_camera);

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
	// Globe
	
	globe_t globe;
	if (globe_t::create(&globe, loader.get()) < 0) {
		return EXIT_FAILURE;
	}

	//globe::init_boxes(loader.get());

	//-----------------------------------------------------------------------------
	// main loop

	while (!glfwWindowShouldClose(window)) {
		hot_reloader->process_updates();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		glfwPollEvents();

		app->onFrameUpdateCallback(window);

		glm::mat4 view = sphere_camera->control.get_view();
		RenderContext ctx = view_component->get_render_context(view);

		renderer->begin_frame(app->width,app->height);

		globe.update(loader.get(), ctx.camera);
		//globe::g_disp->update();

		renderer->begin_pass(&ctx);

		//renderer->bind_material(default_meshID);
		//renderer->draw_cmd_basic_mesh3d(sphereID,glm::mat4(1.0f));
		renderer->bind_material(globe_tileID);
		renderer->draw_cmd_basic_mesh3d(globe.meshID,glm::mat4(1.0f));

		renderer->bind_material(box_material);
		//renderer->draw_cmd_mesh_outline(globe::g_disp->model);

		renderer->end_pass(&ctx);
		renderer->draw_target(ctx.target, glm::mat4(1.0f));

		renderer->end_frame();

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
