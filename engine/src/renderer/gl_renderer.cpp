#include "renderer/opengl.h"

#include "renderer/types.h"
#include "renderer/gl_debug.h"
#include "renderer/renderer.h"
#include "renderer/defaults.h"

#include <resource/material_loader.h>
#include <resource/shader_loader.h>
#include <resource/texture_loader.h>
#include <resource/model_loader.h>

#include <utils/log.h>

#include "imgui.h"

#include <glm/gtc/type_ptr.hpp>

enum GLRendererBindings
{
	GL_RENDERER_COLOR_ATTACHMENT_BINDING = 0,
	GL_RENDERER_FRAMEDATA_BINDING = 16
};

struct renderer_impl
{
	ResourceTable *table;

	// TODO: Pool per-frame ubos by the number of frames in flight
	BufferID frame_ubo;

	RendererDefaults defaults;
};

static double dtime()
{
	static bool init = false; 
	static double s0 = 0;
	if (!init) {
		init = true;
		s0 = dtime();
	}

	auto tpt = std::chrono::high_resolution_clock::now();
	double s = (double)tpt.time_since_epoch().count()/(1e9);
	return s - s0;
}

//--------------------------------------------------------------------------------------------------
// Frame ubo

static Framedata framedata_create(const Camera* camera)
{
	glm::mat4 inv = glm::inverse(camera->view);

	Framedata u;
	u.p = camera->proj;
	u.v = camera->view;
	u.pv = u.p*u.v;
	u.center = inv[3];
	u.t = (float)dtime();
	return u;
}

//--------------------------------------------------------------------------------------------------
// INTERFACE

std::unique_ptr<Renderer> Renderer::create(const RendererCreateInfo* info)
{
	Renderer* renderer = new Renderer();
	renderer_impl* impl = renderer->impl = new renderer_impl{};

	impl->table = info->resource_table;

	LoadResult result = renderer_defaults_init(impl->table, &impl->defaults);

	if (result != RESULT_SUCCESS) {
		return nullptr;
	}

	impl->frame_ubo = impl->table->create_handle(RESOURCE_TYPE_BUFFER);
	BufferCreateInfo buffer_info = {
		.size = sizeof(Framedata),
		.flags = GL_DYNAMIC_STORAGE_BIT
	};
	result = impl->table->allocate(impl->frame_ubo,&buffer_info);

	if (result != RESULT_SUCCESS) {
		impl->table->destroy_handle(impl->frame_ubo);
		return nullptr;
	}

	return std::unique_ptr<Renderer>(renderer);
}

Renderer::~Renderer()
{
	delete impl;
}

RendererDefaults const *Renderer::get_defaults() const
{
	return &impl->defaults;
}

FrameContext Renderer::begin_frame(FrameBeginInfo const *info)
{
	const GLBuffer *ubo = impl->table->get<GLBuffer>(impl->frame_ubo);

	FrameContext ctx {};
	ctx.rt = impl->table;
	ctx.renderer = this;
	ctx.data = framedata_create(info->camera);
	ctx.frame_ubo = impl->frame_ubo;

	glNamedBufferSubData(ubo->id,0,sizeof(Framedata),&ctx.data);

	glBindBufferBase(GL_UNIFORM_BUFFER,GL_RENDERER_FRAMEDATA_BINDING,ubo->id);

	return ctx;
}

void Renderer::end_frame(FrameContext *ctx)
{
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(fence);
}

RenderContext FrameContext::begin_pass(const BeginPassInfo *info) 
{
	// don't even bother with this one
	assert(info);

	const GLRenderTarget* target = rt->get<GLRenderTarget>(info->target); 

	if (!target) {
		log_error("Failed to find render target with id %d",info->target);
		throw std::runtime_error("Failed to create render context, aborting");
		return RenderContext{};
	}

	RenderContext ctx = {
		.renderer = renderer,
		.rt = rt,
		.target = info->target,
		.frame_ubo = frame_ubo
	};

	// bindings

	glBindFramebuffer(GL_FRAMEBUFFER,target->fbo);

	// initialze fbo
	
	static float rgb[3] = {0.15f,0.15f,0.2f};
	
	ImGui::Begin("Begin pass");
	ImGui::ColorPicker3("Background color",rgb);
	ImGui::End();

	glEnable(GL_DEPTH_TEST);
	glViewport(0,0,(GLsizei)target->w,(GLsizei)target->h);
    glClearColor(rgb[0],rgb[1],rgb[2],1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	return ctx;
};

void FrameContext::end_pass(const RenderContext* ctx) 
{
	glBindFramebuffer(GL_FRAMEBUFFER,0);
};

void Renderer::present(RenderTargetID id, uint32_t w, uint32_t h) const
{
	const GLRenderTarget* target = impl->table->get<GLRenderTarget>(id);

	if (!target) return;

	const GLMaterial * material = impl->table->get<GLMaterial>(impl->defaults.materials.screen_quad);

	glClearColor(0,0,0,0);
	glViewport(0,0,(int)w,(int)h);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(material->program);
	glBindTextureUnit(GL_RENDERER_COLOR_ATTACHMENT_BINDING,target->color);

	const GLModel *model = impl->table->get<GLModel>(impl->defaults.models.screen_quad);
	glBindVertexArray(model->vao);
	glDrawElements(GL_TRIANGLES,(int)model->icount,GL_UNSIGNED_INT,NULL);
}

//--------------------------------------------------------------------------------------------------
// Draw commands

void RenderContext::bind_material(MaterialID id) const
{
	const GLMaterial *material = rt->get<GLMaterial>(id); 

	if (!material) {
		log_error("Material does not exist with id %d", id);
		return;
	}

	const RendererDefaults *defaults = &renderer->impl->defaults;

	for (const auto& pair : material->tex_bindings) {
		uint32_t binding = pair.first;
		GLTextureBinding tex_bind = pair.second;

		ImageID texID = tex_bind.id;
		const GLImage *tex = rt->get<GLImage>(texID);

		GLint filter = GL_LINEAR;

		if (!tex) {
			tex = rt->get<GLImage>(defaults->textures.missing);
			filter = GL_NEAREST;

			assert(tex);
		}

		glBindTexture(GL_TEXTURE_2D, tex->id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		glBindTexture(GL_TEXTURE_2D,0);

		glBindTextureUnit(binding,tex->id);
	}

	glUseProgram(material->program);
}

void RenderContext::draw_cmd_mesh_outline(ModelID meshID) const
{
	const GLModel *model = rt->get<GLModel>(meshID);
	glBindVertexArray(model->vao);
	glDrawElements(GL_LINES,(int)model->icount,GL_UNSIGNED_INT,NULL);
	glBindVertexArray(0);
}

void RenderContext::draw_cmd_basic_mesh3d(ModelID meshID, glm::mat4 T) const
{
	const GLModel *model = rt->get<GLModel>(meshID);
	//glEnable(GL_CULL_FACE);
	//glCullFace(GL_FRONT);
	//glFrontFace(GL_CCW);
	
	glBindVertexArray(model->vao);
	glDrawElements(GL_TRIANGLES,(int)model->icount,GL_UNSIGNED_INT,NULL);
	glBindVertexArray(0);

	glDisable(GL_CULL_FACE);
};
