// local

#include "resource_table.h"
#include "material_loader.h"
#include "shader_loader.h"
#include "texture_loader.h"
#include "model_loader.h"
#include "camera_controller.h"

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

// libc
#include <stdlib.h>
#include <string.h>

// posix
#include <unistd.h>
#include <signal.h>


#include "stb_image.h"

class CameraDebugView : public AppComponent
{
	ResourceTable *m_rt;

	MaterialID frust_material;
	BufferID m_ubo;
	uint32_t m_vao;

	std::optional<Camera> m_camera;
public:
	CameraDebugView(ResourceTable * rt) : AppComponent(), m_rt(rt)
	{
		//------------------------------------------------------------------------------
		// Test Camera
		
		m_ubo = create_buffer(m_rt, sizeof(glm::mat4));

		static uint32_t frust_indices[] = {
			0,1, 0,2, 2,3, 3,1, 
			0,4, 1,5, 2,6, 3,7, 
			4,5, 4,6, 6,7, 7,5
		};

		BufferID test_ibo = create_buffer(m_rt, sizeof(frust_indices));
		LoadResult result = upload_buffer(m_rt, test_ibo, frust_indices, sizeof(frust_indices));
		if (result)
			return;

		frust_material = load_material_file(m_rt, "material/frustum.yaml");

		if (!frust_material)
			return;

		glGenVertexArrays(1,&m_vao);
		glBindVertexArray(m_vao);

		const GLBuffer *test_ibo_data = m_rt->get<GLBuffer>(test_ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, test_ibo_data->id);

		glBindVertexArray(0);
	}

	void set_camera(const Camera* camera) {
		if (camera)
			m_camera = *camera;
		else 
			m_camera.reset();
	}

	const Camera *get_camera() {
		return m_camera ? &m_camera.value() : nullptr;
	}

	void render(const RenderContext& ctx)
	{ 
		glm::mat4 pv = glm::mat4(0);
		if (!m_camera) {
			return;
		} else {
			pv = m_camera->proj*m_camera->view;
		}

		LoadResult result = upload_buffer(m_rt, m_ubo, &pv, sizeof(pv));

		ctx.bind_material(frust_material);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_rt->get<GLBuffer>(m_ubo)->id);
		glBindVertexArray(m_vao);
		glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT,nullptr);
		glBindVertexArray(0);

	}
};

size_t levenshtein(const char *s, const char *t) {
    size_t n = strlen(s), m = strlen(t);
    size_t *col = (size_t*)malloc((m+1)*sizeof(size_t)),
           *prev = (size_t*)malloc((m+1)*sizeof(size_t));
    for (size_t j = 0; j <= m; j++) prev[j] = j;

    for (size_t i = 1; i <= n; i++) {
        col[0] = i;
        for (size_t j = 1; j <= m; j++) {
            size_t cost = (s[i-1] == t[j-1]) ? 0 : 1;
            size_t del = prev[j] + 1;
            size_t ins = col[j-1] + 1;
            size_t sub = prev[j-1] + cost;
            col[j] = del < ins ? (del < sub ? del : sub)
                              : (ins < sub ? ins : sub);
        }
        memcpy(prev, col, (m+1)*sizeof(size_t));
    }
    size_t result = prev[m];
    free(prev);
    free(col);
    return result;
}


GLFWmonitor *glfw_select_monitor(const char* match)
{
	if (!match)
		return NULL;

	int count;
	GLFWmonitor **monitors = glfwGetMonitors(&count);

	size_t best = 0;
	size_t score = UINT64_MAX;

	for (int i = 0; i < count; ++i) {
		const char *name = glfwGetMonitorName(monitors[i]);

		size_t test = levenshtein(name, match);

		if (test < score) {
			best = i;
			score = test;
		}
	}

	return monitors[best];
}

static GLFWwindow *g_window = NULL;

void handle_sigint(int sig)
{
	(void)sig;

	if (g_window)
		glfwSetWindowShouldClose(g_window,GLFW_TRUE);
}

int main(int argc, char* argv[])
{
	stbi_set_flip_vertically_on_load(true);
	log_set_flags(LOG_ERROR_BIT | LOG_INFO_BIT);

	struct sigaction sa{};
	sa.sa_handler = handle_sigint;
	sa.sa_flags = SA_RESTART;

	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction");
		return EXIT_FAILURE;
	}

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

	GLFWmonitor *monitor = glfw_select_monitor(preferred_monitor);
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
	std::shared_ptr<ResourceTable> rt = ResourceTable::create(&resource_table_info); 
	rt->register_loader("globe", globe::loader_fns);
	ImageLoader::registration(rt.get());
	ModelLoader::registration(rt.get());

	std::shared_ptr<ResourceHotReloader> hot_retable = ResourceHotReloader::create(rt);

	//-------------------------------------------------------------------------------------------------
	// Renderer

	GLRendererCreateInfo renderer_info = {
		.resource_table = rt
	};

	std::shared_ptr<GLRenderer> renderer = GLRenderer::create(&renderer_info);

	if (!renderer) {
		log_error("Failed to create renderer!");
		return EXIT_FAILURE;
	}

	auto view_component = std::shared_ptr<BaseViewComponent>( 
		new BaseViewComponent(rt.get(), params.win.width, params.win.height)
	);
	app->addComponent(view_component);

	auto sphere_camera = std::shared_ptr<SphereCameraComponent>(
		new SphereCameraComponent(view_component)
	);
	app->addComponent(sphere_camera);

	auto camera_debug = std::shared_ptr<CameraDebugView>(
		new CameraDebugView(rt.get())
	);
	app->addComponent(camera_debug);

	//-------------------------------------------------------------------------------------------------
	// test shader 
	MaterialID default_meshID = load_material_file(rt.get(), "material/default_mesh3d.yaml");
	if (!default_meshID)
		return EXIT_FAILURE;

	MaterialID globe_tileID = load_material_file(rt.get(), "material/globe_tile.yaml");
	if (!globe_tileID)
		return EXIT_FAILURE;

	//-----------------------------------------------------------------------------
	// Globe
	
	globe::init_boxes(rt.get());

	std::unique_ptr<globe::Globe> globe (new globe::Globe);
	ResourceHandle globeID = globe::globe_create(globe.get(), rt.get());

	//-----------------------------------------------------------------------------
	// main loop

	while (!glfwWindowShouldClose(window)) {
		hot_retable->process_updates();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		glfwPollEvents();

		app->onFrameUpdateCallback(window);

		Camera camera = {
			.proj = view_component->get_proj(),
			.view = sphere_camera->control.get_view()
		};

		static bool fixed_camera = false;

		if (ImGui::Button(fixed_camera ? 
				"Unfix camera##globe tile boxes" : 
				"Fix camera##globe tile boxes")
		) {
			camera_debug->set_camera(&camera);
		}

		globe::GlobeUpdateInfo globeInfo = { 
			.camera = &camera 
		};
		globe::globe_update(globe.get(),rt.get(), &globeInfo);

		FrameBeginInfo frameInfo = {.camera = &camera};
		FrameContext frame = renderer->begin_frame(&frameInfo);

		BeginPassInfo passInfo = {.target = view_component->target};
		RenderContext ctx = frame.begin_pass(&passInfo);

		camera_debug->render(ctx);

		ctx.bind_material(globe_tileID);
		ctx.draw_cmd_basic_mesh3d(globe->modelID,glm::mat4(1.0f));
		globe::render_boxes(ctx);
		frame.end_pass(&ctx);

		renderer->end_frame(&frame);

		renderer->present(ctx.target, app->width,app->height);

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
