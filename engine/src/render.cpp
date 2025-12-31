#include "ev2/defines.h"
#include "ev2/render.h"
#include "device_impl.h"
#include "render_impl.h"

#include <glm/gtc/type_ptr.hpp>

ev2::Result create_render_target_gl(
	ev2::Device *dev, 
	ev2::RenderTarget *p_target, 
	uint32_t w,
	uint32_t h,
	ev2::RenderTargetFlags flags)
{
	GLuint fbo;
	glGenFramebuffers(1,&fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	// color attachment is optional
	GLuint color {};
	if (flags & ev2::RENDER_TARGET_COLOR_BIT)
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

	if (flags & ev2::RENDER_TARGET_DEPTH_BIT)
	{
		glGenRenderbuffers(1,&depth);
		glBindRenderbuffer(GL_RENDERBUFFER, depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (int)w, (int)h);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER,
							   GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	}

	GLenum drawBuffers[1] = {};

	if (flags & ev2::RENDER_TARGET_COLOR_BIT)
		drawBuffers[0] = GL_COLOR_ATTACHMENT0;

	glDrawBuffers(1, drawBuffers);

	p_target->w = w;
	p_target->h = h;
	p_target->fbo = fbo;
	p_target->color = color;
	p_target->depth = depth;

	if (!color && !depth) {
		log_error("gl_render_target_create : No depth or color attachment set!");
		goto GL_RENDER_TARGET_CREATE_CLEANUP;
	}

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		log_error("gl_render_target_create : failed to create framebuffer!");
		goto GL_RENDER_TARGET_CREATE_CLEANUP;
	}

	return ev2::SUCCESS;

GL_RENDER_TARGET_CREATE_CLEANUP:
	glBindBuffer(GL_UNIFORM_BUFFER,0);
	glBindTexture(GL_TEXTURE_2D,0);
	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glBindRenderbuffer(GL_RENDERBUFFER,0);
	return ev2::EUNKNOWN;
}

void destroy_render_target_gl(ev2::Device *dev, ev2::RenderTarget *p_target)
{
	if (p_target->fbo) glDeleteFramebuffers(1,&p_target->fbo);
	if (p_target->color) glDeleteTextures(1,&p_target->color);
	if (p_target->depth) glDeleteRenderbuffers(1,&p_target->depth);
}

namespace ev2 {

RenderTargetID create_render_target(
	Device *dev, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
)
{
	RenderTarget *target = new RenderTarget{};
	create_render_target_gl(dev, target, w, h, flags);
	return EV2_HANDLE_CAST(RenderTarget, target);
}

void destroy_render_target(
	Device *dev, 
	RenderTargetID h
)
{
	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, h);
	destroy_render_target_gl(dev, target);
	delete target;
}

ViewID create_view(Device *dev, float view[], float proj[])
{
	glm::mat4 view_mat = glm::make_mat4(view);
	glm::mat4 proj_mat = glm::make_mat4(proj);

	uint32_t view_id = dev->view_transforms.add_matrix(view_mat);
	uint32_t proj_id = dev->view_transforms.add_matrix(proj_mat);

	uint64_t handle = (((uint64_t)view_id) << 32) | (((uint64_t)proj_id));

	return EV2_HANDLE_CAST(View, handle);
}

void update_view(Device *dev, ViewID handle, float view[], float proj[])
{
	uint32_t view_id = (handle.id >> 32);
	uint32_t proj_id = (handle.id & 0xFFFFFFFF);
}

void destroy_view(Device *dev, ViewID handle)
{
	uint32_t view_id = (handle.id >> 32);
	uint32_t proj_id = (handle.id & 0xFFFFFFFF);

	dev->view_transforms.remove_matrix(view_id);
	dev->view_transforms.remove_matrix(proj_id);
}

void begin_frame(Device *dev)
{
	dev->view_transforms.update(dev);

}

void end_frame(Device *dev)
{

	if (dev->assets->reloader) {
		dev->assets->reloader->update();
	}
}

PassCtx begin_pass(Device *dev, RenderTargetID target, ViewID view)
{
	PassCtx ctx {};

	return ctx;
}

SyncID end_pass(Device *dev, PassCtx pass)
{
	return EV2_NULL_HANDLE(Sync);
}

void cmd_draw(RecorderID rec, DrawMode mode, uint32_t vert_count)
{

}

void present(Device *dev, RenderTargetID target, uint32_t w, uint32_t h)
{

}

};

