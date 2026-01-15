#include "ev2/defines.h"
#include "ev2/render.h"
#include "device_impl.h"
#include "render_impl.h"

#include <glm/gtc/type_ptr.hpp>

static ev2::Result create_render_target_gl(
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

static void destroy_render_target_gl(ev2::Device *dev, ev2::RenderTarget *p_target)
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

static inline ViewData view_data_from_matrices(float view[], float proj[])
{
	glm::mat4 view_mat = view ? glm::make_mat4(view) : glm::mat4(1.f);
	glm::mat4 proj_mat = proj ? glm::make_mat4(proj) : glm::mat4(1.f);

	return ViewData{
		.p = proj_mat,
		.v = view_mat,
		.pv = proj_mat * view_mat,
		.center = glm::vec3(0),
	};

}

ViewID create_view(Device *dev, float view[], float proj[])
{
	ViewData data = view_data_from_matrices(view, proj);
	uint32_t id = dev->view_data.add(data);

	return EV2_HANDLE_CAST(View, id);
}

void update_view(Device *dev, ViewID handle, float view[], float proj[])
{
	ViewData data = view_data_from_matrices(view, proj);

	uint32_t id = static_cast<uint32_t>(handle.id);

	if (!dev->view_data.set(id, data))
		log_error("ViewID %d is invalid",id);
}

void destroy_view(Device *dev, ViewID handle)
{
	uint32_t id = static_cast<uint32_t>(handle.id);
	dev->view_data.remove(id);
}

void begin_frame(Device *dev)
{
	if (gl_check_err()) {
	}

	if (dev->assets->reloader) {
		dev->assets->reloader->update();
	}
	dev->transforms.update(dev);
	dev->view_data.update(dev);
}

void end_frame(Device *dev)
{

}

struct RenderPass
{
	RenderTargetID target;
	ViewID view;
};

PassCtx begin_pass(Device *dev, RenderTargetID h_target, ViewID h_view,
					Rect viewport, Rect scissor)
{
	Buffer * buf = dev->get_buffer(dev->view_data.buffer);
	uint32_t view_id = static_cast<uint32_t>(h_view.id);

	glBindBufferRange(
		GL_UNIFORM_BUFFER, 
		VIEWDATA_BINDING, 
		buf->id,
		view_id * dev->view_data.stride,
		dev->view_data.stride
	);

	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, h_target);

	if (!target) {
		glBindFramebuffer(GL_FRAMEBUFFER,0);
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER,target->fbo);
	}

	static float rgb[3] = {0.15f,0.15f,0.2f};

	glEnable(GL_DEPTH_TEST);
	glViewport(viewport.x0, viewport.y0, viewport.w, viewport.h);

	if (scissor.w && scissor.h) {
		glEnable(GL_SCISSOR_TEST);
		glScissor(scissor.x0, scissor.y0, scissor.w, scissor.h);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
    glClearColor(rgb[0],rgb[1],rgb[2],1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


	RenderPass * pass = new RenderPass{
		.target = h_target,
		.view = h_view,
	};

	return PassCtx{
		.rec = EV2_HANDLE_CAST(Recorder, dev),
		.pass = EV2_HANDLE_CAST(Pass, pass),
	};
}

SyncID end_pass(Device *dev, PassCtx ctx)
{
	RenderPass *pass = EV2_TYPE_PTR_CAST(RenderPass, ctx.pass);
	delete pass;

	return EV2_NULL_HANDLE(Sync);
}

void cmd_bind_pipeline(RecorderID rec, GraphicsPipelineID h)
{
	Device *dev = EV2_TYPE_PTR_CAST(Device, rec);
	GraphicsPipeline *pipeline = dev->get_gfx_pipeline(h);

	if (!pipeline) {
		log_error("Invalid pipeline handle %lld", (unsigned long long)h.id);
		return;
	}

	glUseProgram(pipeline->program);
}

void cmd_draw_screen_quad(RecorderID rec)
{
	// A temporary thing while I use OpenGL
	static GLuint vao = 0;
	static GLuint ibo = 0;
	if (!vao) { 
		const uint32_t indices[] = {
			0,1,2,0,2,3
		};
		glCreateBuffers(1, &ibo);
		glNamedBufferStorage(ibo, sizeof(indices), indices, 0);

		glGenVertexArrays(1, &vao);

		glBindVertexArray(vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glBindVertexArray(0);
	}
	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);
}

};

