#include "backends/opengl/def_opengl.h"
#include "ev2/device.h"

#include "device_impl.h"

#include "utils/asset_table.h"
#include "utils/pool.h"

#include "stb_image.h"
	
static void init_gl(ev2::Device *ctx)
{
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);       // makes callback synchronous

	glDebugMessageCallback([]( GLenum source,
							  GLenum type,
							  GLuint id,
							  GLenum severity,
							  GLsizei length,
							  const GLchar* message,
							  const void* userParam )
	{
		const char *fmt = 
			"GL DEBUG: [%u]\n"
			"    Source:   0x%x\n"
			"    Type:     0x%x\n"
			"    Severity: 0x%x\n"
			"    Message:  %s";

		if (type == GL_DEBUG_TYPE_ERROR) {
			log_error(fmt, id, source, type, severity, message);
		} else {
			log_info(fmt, id, source, type, severity, message);
		}
	}, nullptr);

	glDebugMessageControl(
		GL_DONT_CARE,          // source
		GL_DONT_CARE,          // type
		GL_DONT_CARE,          // severity
		0, nullptr,            // count + list of IDs
		GL_TRUE);              // enable

    glEnable(GL_MULTISAMPLE);

	const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);

	log_info(
		"Successfully initiailzed OpenGL\n"
		"\tRenderer: %s\n"
		"\tOpenGL version: %s\n"
		"\tVendor: %s\n"
		"\tGLSL version: %s",
		renderer, version, vendor, glslVersion
	);
}

namespace ev2 {

Device *create_device(const char *path)
{
	stbi_set_flip_vertically_on_load(true);

	Device *ctx = new Device{};

	init_gl(ctx);

	ctx->buffer_pool.reset(ResourcePool<Buffer>::create());
	ctx->image_pool.reset(ResourcePool<Image>::create());
	ctx->texture_pool.reset(ResourcePool<Texture>::create());

	size_t upload_capacity = (1 << 9) * (1 << 20);
	size_t upload_alignment = 512;

	ctx->pool.reset(UploadPool::create(ctx, 
		upload_capacity, 
		upload_alignment, 
		(1 << 14)
	));

	GLint64 ubo_offset_alignment;
	glGetInteger64v(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_offset_alignment);

	ctx->transforms = GPUTTable<glm::mat4>((size_t)ubo_offset_alignment);
	ctx->view_data = GPUTTable<ViewData>((size_t)ubo_offset_alignment);

	ctx->assets.reset(AssetTable::create(ctx, path));

	ctx->frame.ubo = ev2::create_buffer(ctx, sizeof(GPUFramedata), ev2::MAP_WRITE);

	ctx->start_time_ns = 
		std::chrono::high_resolution_clock::now().time_since_epoch().count();

	glm::mat4 proj_def = glm::mat4(1.f);
	glm::mat4 view_def = glm::mat4(1.f);

	ViewData viewdata = view_data_from_matrices(
		glm::value_ptr(proj_def), glm::value_ptr(view_def));

	ctx->default_view = EV2_HANDLE_CAST(View,ctx->view_data.add(viewdata));

	return ctx;
}

void destroy_device(Device *ctx)
{
	ctx->assets.reset();
	ctx->pool.reset();

	ctx->transforms.destroy(ctx);
	ctx->view_data.destroy(ctx);

	delete ctx;
}

};
