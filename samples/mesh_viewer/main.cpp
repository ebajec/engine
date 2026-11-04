#include "shared/project_mounts.h"

#include <ev2/core.h>

#include <ev2/editor.h>
#include <ev2/viewport.h>
#include <ev2/motion_camera.h>

#include <ev2/utils/log.h>
#include <ev2/utils/camera_math.h>
#include <ev2/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdlib>
#include <cstring>

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

#define quantize16u(x) (uint16_t)(roundf(((float)UINT16_MAX * (x))))
#define quantize16s(x) (int16_t)(roundf(((float)(UINT16_MAX >> 1) * (x))))

ev2::VertexPNUV16 cvt_vtx_pnuv32f_pnuv16(
	const float pos_scale[3],
	const float pos_offset[3],
	const float uv_scale[2],
	const float uv_offset[2],
	const ev2::VertexPNUV32f &vtx)
{
	return ev2::VertexPNUV16{
		.position = {
			quantize16u((vtx.position[0] - pos_offset[0]) / pos_scale[0]),
			quantize16u((vtx.position[1] - pos_offset[1]) / pos_scale[1]),
			quantize16u((vtx.position[2] - pos_offset[2]) / pos_scale[2])
		},
		.normal = {
			quantize16s(vtx.normal[0]),
			quantize16s(vtx.normal[1]),
			quantize16s(vtx.normal[2]),
		},
		.uv = {
			quantize16u((vtx.uv[0] - uv_offset[0])/uv_scale[0]),
			quantize16u((vtx.uv[1] - uv_offset[1])/uv_scale[1]),
		}
	};
}

static constexpr glm::vec3 ico_pos_scale(2,2,2);
static constexpr glm::vec3 ico_pos_offset(-1,-1,-1);
static constexpr glm::vec2 ico_uv_scale(1,1);
static constexpr glm::vec2 ico_uv_offset(0,0);

static void ico_sphere_mesh(
	uint32_t *indices,
	ev2::VertexPNUV16 *vertices,
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
			V = glm::normalize(V);

			 ev2::VertexPNUV32f vtx = {
				.position = {V.x, V.y, V.z},
				.normal = {V.x, V.y, V.z},
				.uv = {(PIf + atan2f(V.y, V.x))/TWOPIf, (0.5f * PIf + atan2f(V.z, fabsf(V.x)))/PIf}
			};
			
	  		vertices[vidx++] = cvt_vtx_pnuv32f_pnuv16(
				glm::value_ptr(ico_pos_scale),
				glm::value_ptr(ico_pos_offset),
				glm::value_ptr(ico_uv_scale),
				glm::value_ptr(ico_uv_offset), 
				vtx);
		}
	}
}

struct MeshViewer
{
	ev2::GfxContext *ctx = nullptr;

	std::unique_ptr<ev2::Viewport> main_viewport;
	std::shared_ptr<ev2::MotionCamera> main_camera;

	ev2::ImageFileID test_image;

	struct RenderData {
		ev2::MeshID mesh;
		ev2::TextureID tex;
		ev2::BufferID objects;
		struct PushConstants{
			VkDeviceAddress objects;
		} pc; 
	} rd;

	std::vector<ev2::TransformID> transforms;

	int instance_count = 30000;
	int ico_subdivs = 1;
	float sigma_scale = 0.1f;
	float mu_scale = 0.2f;

	~MeshViewer();

	int initialize(int argc, char **argv);
	int update();

	void render();
	void generate_objects();
	void regen_mesh();
};

void MeshViewer::regen_mesh()
{
	if (rd.mesh.is_valid())
		ev2::destroy_mesh(ctx, rd.mesh);

	uint32_t nfaces, nverts;

	tri_subdiv_sizes(&nfaces, &nverts, ico_subdivs);
	nfaces *= 20;
	nverts *= 20;

	std::unique_ptr<uint32_t[]> indices (new uint32_t[6 * nfaces]);
	std::unique_ptr<ev2::VertexPNUV16[]> vertices (new ev2::VertexPNUV16[nverts]); 

	ico_sphere_mesh(indices.get(), vertices.get(), ico_subdivs);

	ev2::MeshGeometry geom = {
		.vtx_format = ev2::VERTEX_FORMAT_PNUV16,
		.vtx_count = nverts,
		.vtx_data = vertices.get(),
		.idx_type = ev2::INDEX_TYPE_UINT32,
		.idx_count = 3 * nfaces,
		.idx_data = indices.get(),
		.pos_extent = {ico_pos_scale.x, ico_pos_scale.y, ico_pos_scale.z},
		.pos_offset = {ico_pos_offset.x, ico_pos_offset.y, ico_pos_offset.z},
		.uv_scale = {ico_uv_scale.x, ico_uv_scale.y},
		.uv_offset = {ico_uv_offset.x, ico_uv_offset.y},
	};

	rd.mesh = ev2::create_mesh(ctx, &geom);
}

int MeshViewer::initialize(int argc, char **argv)
{
	ctx = ev2::editor::ctx();

	main_camera = std::make_shared<ev2::MotionCamera>(
		glm::dvec3(0,0,0), glm::dvec3(1,1,1), glm::dvec3(0,0,1));
	
	main_viewport.reset(
		new ev2::Viewport("Viewport", 0, 0, 800, 800,
		ev2::RENDER_TARGET_CREATE_DEPTH_BIT | ev2::RENDER_TARGET_CREATE_COLOR_BIT
	));
	main_viewport->set_closable(false);
	main_viewport->set_camera(main_camera);

	test_image = ev2::load_image_asset(ctx, "core://image/catof.jpg");

	rd.tex = ev2::create_texture(ctx, ev2::get_image_resource(ctx, test_image), ev2::FILTER_BILINEAR);

	generate_objects();
	regen_mesh();

	return ev2::SUCCESS;
}


float urandf()
{
	return (float)(rand() + 1.f)/(float)(RAND_MAX + 1.f);
}

glm::vec3 randvec()
{
	return glm::vec3(
		1.f - 2.f * urandf(),
		1.f - 2.f * urandf(),
		1.f - 2.f * urandf()
	);
}
float abs_gaussianf(float mu, float sigma)
{

    float u = urandf();
    float r  = sqrtf(-2.0 * logf(u));
    return mu + sigma * r;
}

glm::mat3 random_frame()
{
	glm::vec3 e1 = glm::normalize(randvec());
	glm::vec3 e2 = randvec();
	e2 = glm::normalize(e2 - glm::dot(e1, e2) * e1);
	glm::vec3 e3 = glm::cross(e1, e2);
	return glm::mat3(e1, e2, e3);
}

void MeshViewer::generate_objects()
{
	int count = instance_count;

	for (ev2::TransformID transform : transforms)
		ev2::destroy_transform(ctx, transform);

	srand(10);

	transforms.resize(count);
	for (int i = 0; i < count; ++i) {
		glm::vec3 c = randvec(); 
		float scale = abs_gaussianf(mu_scale, sigma_scale);
		glm::mat3 T = scale*random_frame();

		glm::mat4 M = glm::mat4(T);
		M[3] = glm::vec4(10.f * randvec(),1);

		transforms[i] = ev2::create_transform(ctx, glm::value_ptr(M));
	}

	if (rd.objects.is_valid()) {
		ev2::destroy_buffer(ctx, rd.objects);
	}

	struct Object {
		uint texture;
		uint transform;
	};

	size_t bytes = sizeof(Object) * count;

	rd.objects = ev2::create_buffer(ctx, bytes,
		ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT); 

	uint32_t tex_handle = ev2::get_bindless_handle(ctx, rd.tex);

	ev2::UploadContext uc = ev2::begin_upload(ctx, bytes, alignof(Object));
	Object *objects = (Object*)uc.ptr;

	for (int i = 0; i < count; ++i) {
		objects[i] = Object{
			.texture = tex_handle,
			.transform = transforms[i].id,
		};
	}

	ev2::commit_buffer_uploads(ctx, uc, rd.objects, {.size = bytes});
}

void MeshViewer::render()
{
	rd.pc = {
		.objects = ev2::get_buffer_device_address(ctx, rd.objects)
	};

	ev2::GfxPassInfo pass_info = {
		.target = main_viewport->get_target(),
		.view = main_viewport->get_view(),
		.name = "Viewer",
	};

	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, "core://pipeline/mesh3d.yaml");
	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
	ev2::cmd_use_buffer(pass, rd.objects, ev2::USAGE_STORAGE_READ_GRAPHICS);
ev2:;ev2::cmd_push_constant(pass, pipeline, 0, sizeof(rd.pc), &rd.pc);
	ev2::cmd_bind_gfx_pipeline(pass, pipeline);
	ev2::cmd_draw_mesh(pass, rd.mesh, instance_count);
	ev2::end_pass(ctx, pass); 
}

int MeshViewer::update()
{
	int result = ev2::SUCCESS;
	if (result = main_viewport->update(nullptr); result != ev2::SUCCESS)
		return result;

	ImGui::Begin("Editor");
	if (ImGui::SliderInt("Instances", &instance_count, 1, 1024)) {
		generate_objects();
	}
	if (ImGui::SliderInt("Subdivisions", &ico_subdivs, 1, 8)) {
		ico_subdivs = glm::clamp(ico_subdivs, 1, 8);
		regen_mesh();
	}
	if (ImGui::SliderFloat("sigma_scale", &sigma_scale, 0, 0.1)) {
		generate_objects();
	}
	if (ImGui::SliderFloat("mu_scale", &mu_scale, 0, 0.2)) {
		generate_objects();
	}
	ImGui::End();

	ev2::flush_uploads(ctx);
	render();

	return ev2::SUCCESS;
}
MeshViewer::~MeshViewer()
{
}

int main(int argc, char *argv[])
{
	std::unique_ptr<MeshViewer> app (new MeshViewer{});

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
