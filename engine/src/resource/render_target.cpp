#include "renderer/opengl.h"
#include "renderer/types.h"
#include "renderer/gl_debug.h"

#include "resource/render_target.h"

static LoadResult gl_render_target_create(ResourceTable *table, void** res, void *usr);
static void gl_render_target_destroy(ResourceTable *table, void *res);

ResourceAllocFns gl_render_target_alloc_fns = {
	.create = gl_render_target_create,
	.destroy = gl_render_target_destroy,
	.load_file = nullptr
};

LoadResult gl_render_target_create(ResourceTable *table, void** res, void *usr)
{
	const RenderTargetCreateInfo *info = static_cast<RenderTargetCreateInfo*>(usr);

	uint32_t w = info->w;
	uint32_t h = info->h;

	GLuint ubo;
	glGenBuffers(1,&ubo);
	glBindBuffer(GL_UNIFORM_BUFFER,ubo);
	glBufferData(GL_UNIFORM_BUFFER,sizeof(Framedata),NULL,GL_DYNAMIC_READ);
	glBindBuffer(GL_UNIFORM_BUFFER,0);

	GLuint fbo;
	glGenFramebuffers(1,&fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	// color attachment is optional
	GLuint color {};
	if (info->flags & RENDER_TARGET_CREATE_COLOR_BIT)
	{
		glGenTextures(1,&color);
		glBindTexture(GL_TEXTURE_2D,color);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(int)w,(int)h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER,
							GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	}

	// depth attachment is optional
	GLuint depth {};

	if (info->flags & RENDER_TARGET_CREATE_DEPTH_BIT)
	{
		glGenRenderbuffers(1,&depth);
		glBindRenderbuffer(GL_RENDERBUFFER, depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (int)w, (int)h);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER,
							   GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	}

	GLenum drawBuffers[1] = {};

	if (info->flags & RENDER_TARGET_CREATE_COLOR_BIT)
		drawBuffers[0] = GL_COLOR_ATTACHMENT0;

	glDrawBuffers(1, drawBuffers);

	// zero initialize
	GLRenderTarget *target = new GLRenderTarget{};

	target->w = w;
	target->h = h;
	target->fbo = fbo;
	target->color = color;
	target->depth = depth;
	target->ubo = ubo;

	if (!color && !depth) {
		log_error("gl_render_target_create : No depth or color attachment set!");
		goto GL_RENDER_TARGET_CREATE_CLEANUP;
	}

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		log_error("gl_render_target_create : failed to create framebuffer!");
		goto GL_RENDER_TARGET_CREATE_CLEANUP;
	}

	*res = target;
	return RT_OK;

GL_RENDER_TARGET_CREATE_CLEANUP:
	glBindBuffer(GL_UNIFORM_BUFFER,0);
	glBindTexture(GL_TEXTURE_2D,0);
	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glBindRenderbuffer(GL_RENDERBUFFER,0);
	delete target;
	return RT_EUNKNOWN;
}

void gl_render_target_destroy(ResourceTable *table, void *res)
{
	GLRenderTarget *target = static_cast<GLRenderTarget *>(res);

	if (target->fbo) glDeleteFramebuffers(1,&target->fbo);
	if (target->color) glDeleteTextures(1,&target->color);
	if (target->depth) glDeleteRenderbuffers(1,&target->depth);
	if (target->ubo) glDeleteBuffers(1,&target->ubo);

	delete target;
}

ResourceHandle render_target_create(ResourceTable *table, const RenderTargetCreateInfo *info)
{
	ResourceHandle h = table->create_handle(RESOURCE_TYPE_RENDER_TARGET);

	LoadResult result = table->allocate(h, (void*)info);

	if (result != RT_OK) {
		table->destroy_handle(h);
		return RESOURCE_HANDLE_NULL;
	}

	return h;
}

LoadResult render_target_resize(ResourceTable *table, ResourceHandle h, const RenderTargetCreateInfo *info)
{
	LoadResult result = table->allocate(h, (void*)info);
	return result;
}

