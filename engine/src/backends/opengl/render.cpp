#include <ev2/defines.h>
#include <ev2/render.h>

#include "backends/opengl/device_impl.h"
#include "backends/opengl/render_impl.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

static ev2::Result create_render_target_gl(
	ev2::Device *ctx, 
	ev2::RenderTarget *p_target, 
	uint32_t w,
	uint32_t h,
	ev2::RenderTargetFlags flags)
{
	GLuint fbo;
	glGenFramebuffers(1,&fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	// color attachment is optional
	ev2::ImageID color {};
	if (flags & ev2::RENDER_TARGET_COLOR_BIT)
	{
		color = ev2::create_image(ctx, w, h, 0, ev2::IMAGE_FORMAT_RGBA8);

		ev2::Image *img = ctx->get_image(color);

		//glGenTextures(1,&color);
		//glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(int)w,(int)h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);

		glBindTexture(GL_TEXTURE_2D, img->id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER,
							GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, img->id, 0);
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

	if (!color.id && !depth) {
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

static void destroy_render_target_gl(ev2::Device *ctx, ev2::RenderTarget *p_target)
{
	if (p_target->fbo) glDeleteFramebuffers(1,&p_target->fbo);
	if (p_target->depth) glDeleteRenderbuffers(1,&p_target->depth);

	if (EV2_VALID(p_target->color)) ev2::destroy_image(ctx, p_target->color);
}

namespace ev2 {

RenderTargetID create_render_target(
	Device *ctx, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
)
{
	RenderTarget *target = new RenderTarget{};
	create_render_target_gl(ctx, target, w, h, flags);

	target->attachments = {
		.color = ev2::create_texture(ctx, target->color, ev2::FILTER_BILINEAR),
		.depth = EV2_NULL_HANDLE(Texture),
	};

	return EV2_HANDLE_CAST(RenderTarget, target);
}

void destroy_render_target(
	Device *ctx, 
	RenderTargetID h
)
{
	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, h);
	destroy_render_target_gl(ctx, target);

	ev2::destroy_texture(ctx, target->attachments.color);
	delete target;
}

RenderTargetAttachments get_render_target_attachments(
	Device *ctx,
	RenderTargetID id
)
{
	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, id);
	return target->attachments;
}

ViewID create_view(Device *ctx, float view[], float proj[])
{
	ViewData data = view_data_from_matrices(view, proj);
	uint32_t id = ctx->view_data.add(data);

	return EV2_HANDLE_CAST(View, id);
}

void update_view(Device *ctx, ViewID handle, float view[], float proj[])
{
	ViewData data = view_data_from_matrices(view, proj);

	uint32_t id = static_cast<uint32_t>(handle.id);

	if (!ctx->view_data.set(id, data))
		log_error("ViewID %d is invalid",id);
}

void destroy_view(Device *ctx, ViewID handle)
{
	uint32_t id = static_cast<uint32_t>(handle.id);
	ctx->view_data.remove(id);
}

void begin_frame(Device *ctx)
{
	if (gl_check_err()) {
	}

	uint64_t time_ns = 
		std::chrono::high_resolution_clock::now().time_since_epoch().count();

	double time_seconds = (double)(time_ns - ctx->start_time_ns)/1e9;

	ctx->frame.dt = time_seconds - ctx->frame.t;
	ctx->frame.t = time_seconds;

	if (ctx->assets->reloader) {
		ctx->assets->reloader->update();
	}
	ctx->transforms.update(ctx);
	ctx->view_data.update(ctx);

	GPUFramedata gpu_data = {
		.t_seconds = (uint32_t)ctx->frame.t,
		.t_fract = (float)fmod(ctx->frame.t, 1.),
		.dt = (float)ctx->frame.dt
	};

	UploadContext uc = begin_upload(ctx, sizeof(GPUFramedata), alignof(GPUFramedata));
	memcpy(uc.ptr, &gpu_data, sizeof(GPUFramedata));
	BufferUpload up = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(GPUFramedata),
	};
	uint64_t sync = commit_buffer_uploads(ctx, uc, ctx->frame.ubo, &up, 1);

	ev2::flush_uploads(ctx);
	ev2::wait_complete(ctx, sync);

	Buffer *ubo = ctx->get_buffer(ctx->frame.ubo);
	glBindBufferRange(
		GL_UNIFORM_BUFFER, 
		FRAMEDATA_BINDING,
		ubo->id,
		0,
		ubo->size
	);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0,0,0,0);
	glClearColor(0,0,0,0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void end_frame(Device *ctx)
{
}

struct RenderPass
{
	RenderTargetID target;
	ViewID view;
};

PassCtx begin_pass(Device *ctx, RenderTargetID h_target, ViewID h_view,
					Rect viewport, Rect scissor)
{
	if (EV2_IS_NULL(ctx->view_data.buffer))
		log_error("No views created.  Did you remember to call begin_frame()?"); 

	if (h_view.id == 0) {
		h_view = ctx->default_view;
	}

	Buffer * buf = ctx->get_buffer(ctx->view_data.buffer);
	uint32_t view_id = static_cast<uint32_t>(h_view.id);

	glBindBufferRange(
		GL_UNIFORM_BUFFER, 
		VIEWDATA_BINDING, 
		buf->id,
		view_id * ctx->view_data.stride,
		ctx->view_data.stride
	);

	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, h_target);

	if (!target) {
		glBindFramebuffer(GL_FRAMEBUFFER,0);
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER,target->fbo);
	}

	//static float rgb[3] = {0.15f,0.15f,0.15f};
	static float rgb[3] = {0.0f,0.0f,0.0f};

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
		.rec = EV2_HANDLE_CAST(Recorder, ctx),
		.pass = EV2_HANDLE_CAST(Pass, pass),
	};
}

SyncID end_pass(Device *ctx, PassCtx ctx)
{
	RenderPass *pass = EV2_TYPE_PTR_CAST(RenderPass, ctx.pass);
	delete pass;

	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glDisable(GL_SCISSOR_TEST);
	glViewport(0,0,0,0);

	return EV2_NULL_HANDLE(Sync);
}

void cmd_bind_gfx_pipeline(RecorderID rec, GfxPipelineID h)
{
	Device *ctx = EV2_TYPE_PTR_CAST(Device, rec);
	GraphicsPipeline *pipeline = ctx->get_gfx_pipeline(h);

	if (!pipeline) {
		log_error("Invalid pipeline handle %lld", (unsigned long long)h.id);
		return;
	}

	glUseProgram(pipeline->program);
	glBindVertexArray(pipeline->vao);
}

void cmd_bind_index_buffer(RecorderID rec, BufferID h_buf)
{
	Device *ctx = EV2_TYPE_PTR_CAST(Device, rec);

	Buffer *buf = ctx->get_buffer(h_buf);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->id); 
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

