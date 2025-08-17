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
#include <implot.h>
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

void plot_frame_times(float delta)
{
	delta *= 1000.f;
	static std::vector<float> times;
	static std::vector<float> deltas;
	static int scroll = 0;
	static size_t samples = 500;
	static float avg = 0;

	if (deltas.size() < samples) 
		deltas.resize(samples,0);
	if (times.size() < samples) 
		times.resize(samples,0);

	deltas[scroll] = (float)delta;
	times[scroll] = glfwGetTime();

	scroll = (scroll + 1)%samples;

	avg = 0.99*avg + 0.01*delta;

	if (ImPlot::BeginPlot("Frame times (ms)",ImVec2(200,200))) {
		ImPlot::SetupAxesLimits(times[scroll], 
		   times[scroll  ? scroll - 1 : samples - 1], 
		   0, 2*avg,ImPlotCond_Always);
		ImPlot::PlotLine("time", times.data(), 
			deltas.data(), samples, ImPlotCond_Always, scroll);
		ImPlot::EndPlot();
	}
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

	ImPlot::CreateContext();
	
	//-------------------------------------------------------------------------------------------------
	// Resource tables

	ResourceTableCreateInfo resource_table_info = {
		.resource_path = resource_path
	};
	std::unique_ptr<ResourceTable> rt = ResourceTable::create(&resource_table_info); 
	ImageLoader::registration(rt.get());
	ModelLoader::registration(rt.get());

	std::unique_ptr<ResourceHotReloader> reloader = ResourceHotReloader::create(rt.get());

	//-------------------------------------------------------------------------------------------------
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
	app->addComponent(view_component);

	auto sphere_camera = std::shared_ptr<SphereCameraComponent>(
		new SphereCameraComponent(view_component)
	);
	app->addComponent(sphere_camera);

	//-------------------------------------------------------------------------------------------------
	// test shader 
	MaterialID default_meshID = load_material_file(rt.get(), "material/default_mesh3d.yaml");
	if (!default_meshID)
		return EXIT_FAILURE;

	//-----------------------------------------------------------------------------
	// Globe
	
	std::unique_ptr<globe::Globe> globe (new globe::Globe);
	if(globe::globe_create(globe.get(), rt.get()) != RESULT_SUCCESS) {
		return EXIT_FAILURE;
	}

	//-----------------------------------------------------------------------------
	// main loop
	
	double t0 = 0, t1 = 0;

	while (!glfwWindowShouldClose(window)) {
		reloader->process_updates();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		plot_frame_times(t1 - t0);

		t0 = t1;
		t1 = glfwGetTime();

		glfwPollEvents();

		app->onFrameUpdateCallback(window);

		glm::dvec3 p = sphere_camera->control.get_pos();
		double elev = globe->source->sample_elevation_at(p);

		sphere_camera->control.set_min_height(
			2*view_component->near + elev);

		Camera camera = {
			.proj = view_component->get_proj(),
			.view = sphere_camera->control.get_view()
		};

		globe::GlobeUpdateInfo globeInfo = { 
			.camera = &camera 
		};
		globe::globe_update(globe.get(),rt.get(), &globeInfo);

		FrameBeginInfo frameInfo = {.camera = &camera};
		FrameContext frame = renderer->begin_frame(&frameInfo);

		BeginPassInfo passInfo = {.target = view_component->target};
		RenderContext ctx = frame.begin_pass(&passInfo);
		
		globe::globe_record_draw_cmds(ctx,globe.get());

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
