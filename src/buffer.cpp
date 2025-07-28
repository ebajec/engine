#include "buffer.h"
#include "gl_debug.h"

static LoadResult gl_buffer_create(ResourceLoader *loader, void **res, void *usr);
static void gl_buffer_destroy(ResourceLoader *loader, void *res);

ResourceAllocFns g_buffer_alloc_fns = {
	.create = gl_buffer_create,
	.destroy = gl_buffer_destroy,
	.load_file = nullptr
};

LoadResult gl_buffer_create(ResourceLoader *loader, void **res, void *usr)
{
	const BufferCreateInfo * ci = static_cast<const BufferCreateInfo*>(usr);

	GLBuffer *buf = new GLBuffer{};
	glGenBuffers(1,&buf->id);
	glNamedBufferStorage(buf->id,(GLsizei)ci->size, nullptr,ci->flags);

	if (gl_check_err()) {
		delete buf;
		return RESULT_ERROR;
	}

	return RESULT_SUCCESS;
}

void gl_buffer_destroy(ResourceLoader *loader, void *res)
{
	GLBuffer *buf = static_cast<GLBuffer*>(res);
	glDeleteBuffers(1,&buf->id);
	delete buf;
}
