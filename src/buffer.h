#ifndef BUFFER_RESOURCE_H
#define BUFFER_RESOURCE_H

#include "resource_table.h"

struct GLBuffer
{
	size_t size;
	uint32_t id;
};

typedef ResourceHandle BufferID;

struct BufferCreateInfo
{
	size_t size;

	// https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferStorage.xhtml
	uint32_t flags;
};

extern ResourceAllocFns g_buffer_alloc_fns;

BufferID create_buffer(ResourceTable *table, size_t size, uint32_t flags = GL_DYNAMIC_STORAGE_BIT);
LoadResult upload_buffer(ResourceTable *table, BufferID buf, void* data, size_t size);

#endif // BUFFER_RESOURCE_H
