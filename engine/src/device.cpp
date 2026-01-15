#include <engine/renderer/opengl.h>

#include "ev2/device.h"

#include "device_impl.h"

#include "asset_table.h"
#include "pool.h"

#include "stb_image.h"

static void init_gl(ev2::Device *dev)
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

	Device *dev = new Device{};

	init_gl(dev);

	dev->buffer_pool.reset(ResourcePool<Buffer>::create());
	dev->image_pool.reset(ResourcePool<Image>::create());
	dev->texture_pool.reset(ResourcePool<Texture>::create());

	GLint64 ubo_offset_alignment;
	glGetInteger64v(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_offset_alignment);

	dev->transforms = GPUTTable<glm::mat4>((size_t)ubo_offset_alignment);
	dev->view_data = GPUTTable<ViewData>((size_t)ubo_offset_alignment);

	dev->assets.reset(AssetTable::create(dev, path));

	return dev;
}

void destroy_device(Device *dev)
{
	AssetTable::destroy(dev->assets.get());

	dev->transforms.destroy(dev);
	dev->view_data.destroy(dev);

	delete dev;
}

};
