// local
#include "engine/resource/resource_table.h"
#include "engine/resource/material_loader.h"
#include "engine/resource/texture_loader.h"
#include "engine/resource/model_loader.h"
#include "engine/resource/compute_pipeline.h"

#include "engine/utils/functions.h"

#include "engine/utils/log.h"

#include "view_utils.h"
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

// posix
#include <unistd.h>
#include <signal.h>

static GLFWwindow *g_window = NULL;
static ResourceTable *g_rt = NULL;

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

#ifdef __linux__
void handle_sigint(int sig)
{
	(void)sig;

	if (g_window)
		glfwSetWindowShouldClose(g_window,GLFW_TRUE);
}
#endif

uint32_t float4_to_rgba(float r, float g, float b, float a)
{
	uint32_t R = ((uint32_t)(std::clamp(r,0.f,1.f)*255.f) & 0xFF) << 0;
	uint32_t G = ((uint32_t)(std::clamp(g,0.f,1.f)*255.f) & 0xFF) << 8;
	uint32_t B = ((uint32_t)(std::clamp(b,0.f,1.f)*255.f) & 0xFF) << 16;
	uint32_t A = ((uint32_t)(std::clamp(a,0.f,1.f)*255.f) & 0xFF) << 24;

	return R | G | B | A;
}

float gaussian(float x, float y)
{
	return exp(-(x*x + y*y));
}

ImageID create_test_image()
{
	uint32_t w = 1024, h = 1024;
	ImageID test_image = image_create_2d(g_rt, w, h, IMG_FORMAT_RGBA8);

	uint32_t * data = new uint32_t[w*h];

	size_t idx = 0;
	for (size_t j = 0; j < h; ++j) {
		float y = (float)j/(float)(h - 1);

		y = 2.0*y - 1;

		for (size_t i = 0; i < w; ++i) {
			float x = (float)i/(float)(w - 1);

			x = 2.0*x - 1;

			//float value = gaussian(3.f*x,3.f*y);
			float value = weierstrass(x, y);

			data[idx++] = float4_to_rgba(0, 0, value, 1);
		}
	}

	uint32_t tid = g_rt->get<GLImage>(test_image)->id; 

	glTextureSubImage2D(tid, 0, 0, 0, (int)w, (int)h, GL_RGBA, GL_UNSIGNED_BYTE, data);

	//UploadContext *uc = get_upload_context(g_rt);

	//ImageUploadRegion region = {
	//	.w = w, .h = h
	//};

	//UploadParams up = {.mode = UPLOAD_MODE_STAGING};
	//UploadSession us = begin_image_upload(uc, test_image, &up, &region, 1);
	//upload_write_span(&us, 0, 0, data, w*h*sizeof(float));
	//end_upload(&us);

	delete[] data;

	return test_image;
}

int main(int argc, char* argv[])
{
	stbi_set_flip_vertically_on_load(true);
	log_set_flags(LOG_ERROR_BIT | LOG_INFO_BIT);

#ifdef __linux__
	struct sigaction sa{};
	sa.sa_handler = handle_sigint;
	sa.sa_flags = SA_RESTART;

	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction");
		return EXIT_FAILURE;
	}
#endif

	if (!glfwInit()) {
		log_error("Failed to initialize GLFW!");
		return EXIT_FAILURE;
	}
	
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

	if (int code = init_gl_basic(window); code == EXIT_FAILURE) {
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
	// Resource table

	ResourceTableCreateInfo resource_table_info = {
		.resource_path = resource_path
	};
	g_rt = ResourceTable::create(&resource_table_info).release(); 

	ImageLoader::registration(g_rt);
	ModelLoader::registration(g_rt);

	std::unique_ptr<ResourceReloader> reloader = ResourceReloader::create(g_rt);

	//-------------------------------------------------------------------------------------------------
	// Renderer

	RendererCreateInfo renderer_info = {
		.resource_table = g_rt
	};
	std::unique_ptr<Renderer> renderer = Renderer::create(&renderer_info);

	if (!renderer) {
		log_error("Failed to create renderer!");
		return EXIT_FAILURE;
	}

	auto view_component = std::shared_ptr<BaseViewComponent>( 
		new BaseViewComponent(g_rt, params.win.width, params.win.height)
	);
	app->addComponent(view_component);

	auto panning_camera = std::shared_ptr<PanningCameraComponent>(
		new PanningCameraComponent(view_component)
	);
	app->addComponent(panning_camera);

	//-----------------------------------------------------------------------------
	// test shader 
	
	MaterialID default_meshID = material_load_file(g_rt, "material/default_mesh3d.yaml");
	if (!default_meshID)
		return EXIT_FAILURE;

	const RendererDefaults *defaults = renderer->get_defaults();

	MaterialID diffusionID = material_load_file(g_rt, "material/diffusion.yaml");
	ModelID screenQuad = defaults->models.screen_quad;

	struct UBO {
		float t = 0;

		void imgui() {
			ImGui::Begin("UBO");
			ImGui::SliderFloat("t", &t, 0, 1);
			ImGui::End();
		}
	} my_ubo;

	BufferID uboID = buffer_create(g_rt, sizeof(UBO));
	ImageID test_image = create_test_image();

	ComputePipelineID diffusionPipeline = load_compute_pipeline(g_rt, "shader/diffusion.comp");

	//-----------------------------------------------------------------------------
	// main loop
	
	double t0 = 0, t1 = 0;

	while (!glfwWindowShouldClose(window)) {
		reloader->process_updates();

		app->onFrameBeginCallback(window);

		my_ubo.imgui();
		buffer_upload(g_rt, uboID, &my_ubo, sizeof(my_ubo));

		plot_frame_times(t1 - t0);

		t0 = t1;
		t1 = glfwGetTime();

		glfwPollEvents();

		Camera camera = {
			.proj = view_component->get_proj_2d(),
			.view = panning_camera->get_view()
		};

		const GLBuffer *my_ubo_buf = g_rt->get<GLBuffer>(uboID);

		// Begin frame
		FrameBeginInfo frameInfo = {.camera = &camera};
		FrameContext frame = renderer->begin_frame(&frameInfo);

		// begin pass 
		BeginPassInfo passInfo = {.target = view_component->target};
		RenderContext ctx = frame.begin_pass(&passInfo);

		uint32_t tid = g_rt->get<GLImage>(test_image)->id;

		ctx.bind_material(diffusionID);
		glBindTextureUnit(0, tid);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, my_ubo_buf->id);

		ctx.draw_cmd_basic_mesh3d(screenQuad, glm::mat4(1));

		// end pass
		frame.end_pass(&ctx);

		// end frame
		renderer->end_frame(&frame);

		renderer->present(ctx.target, app->width,app->height);

		app->onFrameEndCallback(window);

        glfwSwapBuffers(window);   
	}

	renderer.reset(nullptr);
	reloader.reset(nullptr);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	delete g_rt;

	return EXIT_SUCCESS;
}
