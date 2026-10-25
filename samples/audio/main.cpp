#include "shared/heightmap_viewer.h"
#include "shared/project_mounts.h"

#include "capture.h"

#include "ev2/editor.h"
#include "ev2/image_viewer.h"

#include <ev2/utils/log.h>

#include <ev2/context.h>
#include <ev2/resource.h>

#include <ev2/utils/camera_math.h>
#include <ev2/utils/geometry.h>
#include <ev2/spherical_motion_camera.h>

// fftw
#include "fftw3.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <complex>
#include <cstdlib>
#include <cstring>

static void sphere_mesh(
	uint32_t *indices,
	float *vertices,
	int n_tht, int n_phi
)
{
	std::complex<float> d_tht = std::polar(1.f, PIf / float(n_tht - 1));
	std::complex<float> d_phi = std::polar(1.f, TWOPIf / float(n_phi));
	std::complex<float> phi = 1.f;

	int vidx = 0;
	int iidx = 0;

	for (int i = 0; i < n_phi; ++i) {

		std::complex<float> tht = 1;
		for (int j = 0; j < n_tht; ++j) {
			vertices[vidx++] = tht.imag() * phi.real();
			vertices[vidx++] = tht.imag() * phi.imag();
			vertices[vidx++] = tht.real();

			int in = i + 1 < n_phi ?  i + 1 : 0; 
			int jn = j + 1;
			
			if (jn < n_tht) {
				indices[iidx++] = i*n_tht + j;
				indices[iidx++] = in*n_tht + j;
				indices[iidx++] = in*n_tht + jn;

				indices[iidx++] = i*n_tht + j;
				indices[iidx++] = in*n_tht + jn;
				indices[iidx++] = i*n_tht + jn;
			}
			tht *= d_tht;
		}

		phi *= d_phi;
	}
}

uint32_t vindex(glm::ivec2 p, int N)
{
        return 2 * p.y <= N ? p.y * (N + 1) + p.x
                            : (N + 1 - p.y) * (N + 1) + p.x + p.y;
}

glm::ivec2 vcoord(uint32_t idx, int N)
{
	uint32_t r = idx / (N + 1);
	uint32_t c = idx - r * (N + 1);

	return c <= N - r ?
		glm::ivec2(c, r) : 
		glm::ivec2(c - (N + 1 - r), N + 1 - r);
}

void face_vertices(uint32_t f, int n, uint32_t out[3])
{
	int N = 1 << n;

	glm::ivec2 o{0,0}, u{N, 0}, v{0, N};

	for (int k = n - 1; k >= 0; --k) {
		glm::ivec2 hu = u/2, hv = v/2;
		switch ((f >> 2 * k) & 0x3) {
			case 0:
				o += hu, u = hv, v = hv - hu;
				break;
			case 1:
				u = hu, v = hv;
				break;
			case 2:
				o += hu, u = hu, v = hv;
				break;
			case 3:
				o += hv, u = hu, v = hv;
				break;
		}
	}

	out[0] = vindex(o, N);
	out[1] = vindex(o + u, N);
	out[2] = vindex(o + v, N);
}

const float ico_phi = 1.618033988749895f; // golden ratio

const glm::vec3 ico_verts[12] = {
    {-1,  ico_phi,  0}, { 1,  ico_phi,  0}, {-1, -ico_phi,  0}, { 1, -ico_phi,  0},
    { 0, -1,  ico_phi}, { 0,  1,  ico_phi}, { 0, -1, -ico_phi}, { 0,  1, -ico_phi},
    { ico_phi,  0, -1}, { ico_phi,  0,  1}, {-ico_phi,  0, -1}, {-ico_phi,  0,  1},
};

const uint32_t ico_faces[20][3] = {
    // 5 faces around vertex 0
    {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
    // 5 adjacent faces
    {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
    // 5 faces around vertex 3
    {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
    // 5 adjacent faces
    {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
};

static void tri_subdiv_sizes(uint32_t *p_nfaces, uint32_t *p_nverts, int n)
{
	uint32_t nfaces = 1;
	uint32_t nverts = 3;

	// from Euler's formula
	for (int i = 0; i < n; ++i) {
		nverts = 2 * nverts + nfaces - 1;
		nfaces *= 4;
	}

	*p_nfaces = nfaces;
	*p_nverts = nverts;
}

static void ico_sphere_mesh(
	uint32_t *indices,
	float *vertices,
	int n
)
{
	uint32_t nfaces, nverts;
	tri_subdiv_sizes(&nfaces, &nverts, n);

	size_t vidx = 0, iidx = 0;
	
	const uint32_t N = 1 << n;
	const float inv_N = 1.f/(float)N;

	// NOTE: Vertices are duped on icosahedron face boundaries.

	// icosahedron faces
	for (uint32_t F = 0; F < 20; ++F) {
		// subdivisions
		for (uint32_t f = 0; f < nfaces; ++f) {
			uint32_t verts[3];
			face_vertices(f, n, verts);

			indices[iidx++] = verts[0] + F * nverts;
			indices[iidx++] = verts[1] + F * nverts;
			indices[iidx++] = verts[2] + F * nverts;
		}

		glm::vec3
			A = ico_verts[ico_faces[F][0]],
			B = ico_verts[ico_faces[F][1]],
			C = ico_verts[ico_faces[F][2]];

		for (uint32_t v = 0; v < nverts; ++v) {
			glm::vec2 b = glm::vec2(vcoord(v, N)) * inv_N;
			glm::vec3 V = A + b.x * (B - A) + b.y * (C - A);

			vertices[vidx++] = V.x;
			vertices[vidx++] = V.y;
			vertices[vidx++] = V.z;
		}
	}
}

enum VizMode
{
	VIZ_MODE_RASTER,
	VIZ_MODE_MARCHED,
};

struct AudioViz
{
	ev2::GfxContext *ctx = nullptr;

	std::unique_ptr<ev2::Viewport> spectrum_viewport;

	std::unique_ptr<ev2::Viewport> main_viewport;
	std::shared_ptr<ev2::SphericalMotionCamera2> main_camera;

	struct RenderData {
		ev2::BufferID vbo;
		ev2::BufferID ibo;
		ev2::BufferID spectrum;
		VizMode mode = VIZ_MODE_RASTER;

		struct PushConstants{
			VkDeviceAddress spec;
			int bins;
			int Fmax;
		} pc; 
	} rd;

	Capture capture;

	std::vector<float> latest;

	int fft_window_size = 2048;

	// the window is downsampled into this
	int fft_nbins = 74;
	int Fmax = 20000;

	uint32_t index_count;

	~AudioViz();

	int initialize(int argc, char **argv);
	int update();

	void process_audio();

	void draw_sdf(ev2::PassID pass);
	void draw_ball(ev2::PassID pass);
	void render();

	void set_fft_nbins(int fft_nbins);
};

int AudioViz::initialize(int argc, char **argv)
{
	CaptureMode capture_mode = CAPTURE_MODE_OUTPUT;

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--mic"))
			capture_mode = CAPTURE_MODE_MIC;
	}

	ctx = ev2::editor::ctx();

	//camera = std::make_shared<ev2::MotionCamera>(
	//	glm::dvec3(0,0,0), glm::dvec3(1,1,1), glm::dvec3(0,0,1));
	
	main_camera = std::make_shared<ev2::SphericalMotionCamera2>();
	main_camera->auto_planes = false;
	main_camera->far_plane = 100;
	main_camera->min_height = 0;

	main_viewport.reset(
		new ev2::Viewport("Viewport", 0, 0, 800, 800,
		ev2::RENDER_TARGET_CREATE_DEPTH_BIT | ev2::RENDER_TARGET_CREATE_COLOR_BIT
	));
	main_viewport->set_closable(false);
	main_viewport->set_camera(main_camera);

	spectrum_viewport.reset(
		new ev2::Viewport("Spectrum", 0, 0, 200, 100,
		ev2::RENDER_TARGET_CREATE_COLOR_BIT
	));
	spectrum_viewport->set_closable(false);

	if (int result = capture.init(capture_mode); result != ev2::SUCCESS)
		return result;

	latest.resize(capture.capacity() * capture.format().channels);
	set_fft_nbins(fft_nbins);

	uint32_t nfaces, nverts;

#define ICOSAHEDRON
#ifdef ICOSAHEDRON
	int n_ico = 7;

	tri_subdiv_sizes(&nfaces, &nverts, n_ico);
	nfaces *= 20;
	nverts *= 20;
#else
	nverts = nfaces = n_tht * n_phi;
#endif

	index_count = 6 * nfaces;

	size_t vbo_size = 3 * sizeof(float) * nverts;
	size_t ibo_size = 6 * sizeof(uint32_t) * nfaces;

	rd.vbo = ev2::create_buffer(ctx, vbo_size, 
		ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT);
	rd.ibo = ev2::create_buffer(ctx, ibo_size, 
		ev2::BUFFER_USAGE_INDEX_BUFFER_BIT);

	ev2::UploadContext vbo_uc = ev2::begin_upload(ctx, vbo_size, alignof(float));
	ev2::UploadContext ibo_uc = ev2::begin_upload(ctx, ibo_size, alignof(uint32_t));

#ifdef ICOSAHEDRON
	ico_sphere_mesh((uint32_t*)ibo_uc.ptr, (float*)vbo_uc.ptr, n_ico);
#else
	sphere_mesh((uint32_t*)ibo_uc.ptr, (float*)vbo_uc.ptr, n_tht, n_phi);
#endif

	ev2::commit_buffer_uploads(ctx, vbo_uc, rd.vbo, {.size = vbo_size});
	ev2::commit_buffer_uploads(ctx, ibo_uc, rd.ibo, {.size = ibo_size});

	ev2::flush_uploads(ctx);

	return ev2::SUCCESS;
}

void AudioViz::draw_sdf(ev2::PassID pass)
{
	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, "audio://pipeline/march.yaml");
	ev2::cmd_use_buffer(pass, rd.spectrum, ev2::USAGE_STORAGE_READ_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, pipeline);
	ev2::cmd_push_constant(pass, pipeline, 0, sizeof(rd.pc), &rd.pc);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 4, 1, 0, 0);  
	});
}

void AudioViz::draw_ball(ev2::PassID pass)
{
	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, "audio://pipeline/ball.yaml");

	ev2::cmd_use_buffer(pass, rd.vbo, ev2::USAGE_VERTEX_INPUT);
	ev2::cmd_use_buffer(pass, rd.ibo, ev2::USAGE_INDEX_INPUT);
	ev2::cmd_use_buffer(pass, rd.spectrum, ev2::USAGE_STORAGE_READ_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, pipeline);
	ev2::cmd_push_constant(pass, pipeline, 0, sizeof(rd.pc), &rd.pc);

	ev2::cmd_bind_vertex_buffer(pass, rd.vbo, 0);
	ev2::cmd_bind_index_buffer(pass, rd.ibo, 0);
	ev2::cmd_custom(pass, [count = index_count](VkCommandBuffer cmds){
		vkCmdDrawIndexed(cmds, count, 1, 0, 0, 0);  
	});
}

void AudioViz::render()
{
	rd.pc = {
		.spec = ev2::get_buffer_device_address(ctx, rd.spectrum),
		.bins = fft_nbins,
		.Fmax = Fmax
	};

	{
		ev2::GfxPassInfo pass_info = {
			.target = main_viewport->get_target(),
			.view = main_viewport->get_view(),
			.name = "Visualization",
		};

		ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);

		switch (rd.mode) {
			case VIZ_MODE_RASTER: draw_ball(pass); break;
			case VIZ_MODE_MARCHED: draw_sdf(pass); break;
		}
		ev2::end_pass(ctx, pass); 
	}

	ev2::GfxPassInfo pass_info = {
		.target = spectrum_viewport->get_target(),
		.view = spectrum_viewport->get_view(),
		.name = "Spectrum"
	};

	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, "audio://pipeline/spectrum.yaml");

	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
	ev2::cmd_bind_gfx_pipeline(pass, pipeline);
	ev2::cmd_push_constant(pass, pipeline, 0, sizeof(rd.pc), &rd.pc);
	ev2::cmd_use_buffer(pass, rd.spectrum, ev2::USAGE_STORAGE_READ_GRAPHICS);
	ev2::cmd_custom(pass, [bins = fft_nbins](VkCommandBuffer cmds) {
		vkCmdDraw(cmds, 6, 2*bins, 0, 0);
	});
	ev2::end_pass(ctx, pass);
}

float hannf(float n, float N)
{
	float x = sinf(PIf * n / N); 
	return x * x;
}

void AudioViz::process_audio()
{
	capture.read_latest(latest);

	const CaptureFormat &format = capture.format();

	uint32_t num_latest = (uint32_t)latest.size() / format.channels;

	float *mono = (float*)fftwf_malloc(sizeof(float) * fft_window_size);
	fftwf_complex *fft = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_window_size);

	for (uint32_t n = format.channels * (num_latest - fft_window_size), idx = 0;
		n < format.channels * num_latest; n += format.channels) {
		mono[idx] = 0;

		for (uint32_t c = 0; c < format.channels; ++c) {
			mono[idx] += latest[n + c];
		}

		mono[idx] *= hannf(n, fft_window_size) / (float)format.channels;
		++idx;
	}

	fftwf_plan plan = fftwf_plan_dft_r2c_1d(fft_window_size, mono, fft, 0);
	fftwf_execute(plan);
	fftwf_free(mono);

	ev2::UploadContext uc = ev2::begin_upload(ctx, sizeof(float) * fft_nbins, alignof(float));

	float *out = (float*)uc.ptr;

	const long Nmax = Fmax * fft_window_size / format.sample_rate;
	const int samp_per_bin = Nmax / fft_nbins;

	const float factor = fft_nbins / float(fft_window_size);

	for (int i = 0, idx = 0; i + samp_per_bin <= Nmax; i += samp_per_bin) {
		float avg_mag = 0;
		for (int j = 0; j < samp_per_bin; ++j) {
			avg_mag += hypotf(fft[i + j][0], fft[i + j][1]);
	  	}

		avg_mag *= factor;

		out[idx++] = avg_mag;
	}

	fftwf_free(fft);

	ev2::commit_buffer_uploads(ctx, uc, rd.spectrum, {.size = sizeof(float) * fft_nbins});
}

int32_t next_pow2(int32_t n)
{
    if (n <= 1) return 1;
    uint32_t v = (uint32_t)n - 1;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return (int32_t)(v + 1);   // n > 2^30 has no int32 answer
}

void AudioViz::set_fft_nbins(int fft_nbins)
{
	fft_nbins = next_pow2(fft_nbins);
	if (rd.spectrum.is_valid()) {
		ev2::destroy_buffer(ctx, rd.spectrum);
	}
	rd.spectrum = ev2::create_buffer(ctx, fft_nbins * sizeof(float),
		ev2::USAGE_STORAGE_READ_GRAPHICS);
}

int AudioViz::update()
{
	int result = ev2::SUCCESS;
	if (result = main_viewport->update(nullptr); result != ev2::SUCCESS)
		return result;

	if (result = spectrum_viewport->update(nullptr); result != ev2::SUCCESS)
		return result;

	ImGui::Begin("Editor");
	if (ImGui::SliderInt("FFT Window", &fft_window_size, 512, 2*8192)) {
		fft_window_size = next_pow2(fft_window_size);
	}
	if (ImGui::SliderInt("FFT Bin Count", &fft_nbins, 4, 256)) {
		set_fft_nbins(fft_nbins);
	}
	if (ImGui::SliderInt("Fmax", &Fmax, 100, 20000)) {
	}

	// order matches VizMode
	int mode = rd.mode;
	if (ImGui::Combo("Viz Mode", &mode, "Raster\0Marched\0")) {
		rd.mode = (VizMode)mode;
	}

	ImGui::End();

	process_audio();

	ev2::flush_uploads(ctx);
	render();

	return ev2::SUCCESS;
}
AudioViz::~AudioViz()
{
}

int main(int argc, char *argv[])
{
	std::unique_ptr<AudioViz> app (new AudioViz{});

	int result = ev2::editor::init(argc, argv, "Audio Visualization", 1200, 800);
	if (result < ev2::SUCCESS)
		return result;

	add_project_mounts(ev2::editor::ctx());

	if (app->initialize(argc, argv) != ev2::SUCCESS)
		return EXIT_FAILURE;

	int status;

	for (;;)
	{
		status = ev2::editor::begin_frame();
		if (status != ev2::SUCCESS)
			break;

		status = app->update();
		if (status != ev2::SUCCESS)
			break;

		status = ev2::editor::end_frame();
		if (status != ev2::SUCCESS)
			break;
	}

	app.reset(nullptr);
	ev2::editor::shutdown();

	return status < 0 ? status : 0;
}
