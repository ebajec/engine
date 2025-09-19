#include "renderer/opengl.h"

#include "renderer/gl_debug.h"

#include "resource/buffer.h"

static LoadResult gl_buffer_create(ResourceTable *table, void **res, void *usr);
static void gl_buffer_destroy(ResourceTable *table, void *res);

ResourceAllocFns g_buffer_alloc_fns = {
	.create = gl_buffer_create,
	.destroy = gl_buffer_destroy,
	.load_file = nullptr
};

LoadResult gl_buffer_create(ResourceTable *table, void **res, void *usr)
{
	const BufferCreateInfo * ci = static_cast<const BufferCreateInfo*>(usr);

	GLBuffer *buf = new GLBuffer{};
	glCreateBuffers(1,&buf->id);

	if (gl_check_err()) {
		goto failure;
	}

	glNamedBufferStorage(buf->id,(GLsizei)ci->size, nullptr,ci->flags);

	if (gl_check_err()) {
		goto failure;
	}

	*res = buf;

	return RESULT_SUCCESS;
failure:
	if (buf && buf->id) glDeleteBuffers(1,&buf->id);
	if (buf) delete buf;
	return RESULT_ERROR;
}

void gl_buffer_destroy(ResourceTable *table, void *res)
{
	GLBuffer *buf = static_cast<GLBuffer*>(res);
	glDeleteBuffers(1,&buf->id);
	delete buf;
}

BufferID create_buffer(ResourceTable *table, size_t size, uint32_t flags)
{
	ResourceHandle h = table->create_handle(RESOURCE_TYPE_BUFFER);
	BufferCreateInfo buf_info = {
		.size = size,
		.flags = flags,
	};

	LoadResult result = table->allocate(h, &buf_info);
	if (result) {
		table->destroy_handle(h);
		return RESOURCE_HANDLE_NULL;
	}

	return h;
}

LoadResult upload_buffer(ResourceTable *table, BufferID id, void* data, size_t size)
{
	const GLBuffer *buf = table->get<GLBuffer>(id);
	if (!buf) {
		log_error("No buffer resource with id %d",id);
	}
	glNamedBufferSubData(buf->id,0,(GLsizei)size, data);
	return RESULT_SUCCESS;
}
