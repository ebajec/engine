#ifndef RENDER_TARGET_H
#define RENDER_TARGET_H

#include "resource_loader.h"

#include <cstdint>

typedef ResourceHandle RenderTargetID;

struct GLRenderTarget
{
	uint32_t w;
	uint32_t h;

	gl_framebuffer fbo;
	gl_tex color;
	gl_renderbuffer depth;
	gl_ubo ubo;
};

enum RenderTargetCreateFlagBits
{
	RENDER_TARGET_CREATE_COLOR_BIT = 0x1,
	RENDER_TARGET_CREATE_DEPTH_BIT = 0x2,
};
typedef uint32_t RenderTargetCreateFlags;

struct RenderTargetCreateInfo
{
	uint32_t w; 
	uint32_t h;
	RenderTargetCreateFlags flags;
};

extern ResourceAllocFns g_target_alloc_fns;

extern ResourceHandle render_target_create(ResourceLoader *loader, const RenderTargetCreateInfo *info);
extern LoadResult render_target_resize(ResourceLoader *loader, ResourceHandle h, const RenderTargetCreateInfo *info);


#endif
