#include "material_loader.h"
#include "shader_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

#include "imgui.h"

#include "def_gl.h"
#include "gl_debug.h"
#include "gl_renderer.h"
#include "renderer_defaults.h"

#include <glm/gtc/type_ptr.hpp>

struct gl_renderer_impl
{
	std::shared_ptr<ResourceLoader> loader;

	gl_ubo framedata_ubo;

	RendererDefaults defaults;
};

//--------------------------------------------------------------------------------------------------
// Frame ubo

static gl_framedata_t gl_target_uniforms_create(const RenderContext* ctx)
{
	glm::mat4 inv = glm::inverse(ctx->camera.view);

	gl_framedata_t u;
	u.p = ctx->camera.proj;
	u.v = ctx->camera.view;
	u.pv = u.p*u.v;
	u.center = inv[3];
	u.t = (float)glfwGetTime();
	return u;
}

//--------------------------------------------------------------------------------------------------
// INTERFACE

std::unique_ptr<GLRenderer> GLRenderer::create(const GLRendererCreateInfo* info)
{
	GLRenderer* renderer = new GLRenderer();
	gl_renderer_impl* impl = renderer->impl = new gl_renderer_impl{};

	impl->loader = info->resource_loader;

	LoadResult result = renderer_defaults_init(impl->loader.get(), &impl->defaults);

	if (result != RESULT_SUCCESS) {
		return nullptr;
	}
	return std::unique_ptr<GLRenderer>(renderer);
}

GLRenderer::~GLRenderer()
{
}

const RendererDefaults *GLRenderer::get_defaults() const 
{
	return &impl->defaults;
}

void GLRenderer::begin_frame(uint32_t w, uint32_t h)
{
	glClearColor(0,0,0,0);
	glViewport(0,0,(int)w,(int)h);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLRenderer::end_frame()
{
}

RenderContext GLRenderer::begin_pass(const BeginPassInfo *info) 
{
	// don't even bother with this one
	assert(info);

	const GLRenderTarget* target = impl->loader->get<GLRenderTarget>(info->target); 

	if (!target) {
		log_error("Failed to find render target with id %d",info->target);
		throw std::runtime_error("Failed to create render context, aborting");
		return RenderContext{};
	}

	RenderContext ctx = {
		.renderer = this,
		.loader = impl->loader.get(),
		.target = info->target,
		.camera = *info->camera,
	};

	// upload uniforms
	gl_framedata_t uniforms = gl_target_uniforms_create(&ctx);

	glBindBuffer(GL_UNIFORM_BUFFER,target->ubo);
	glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(uniforms),&uniforms);
	glBindBuffer(GL_UNIFORM_BUFFER,0);

	// bindings

	glBindBufferBase(GL_UNIFORM_BUFFER,GL_RENDERER_FRAMEDATA_BINDING,target->ubo);
	glBindFramebuffer(GL_FRAMEBUFFER,target->fbo);

	// initialze fbo
	
	static float rgb[3] = {0.5,0.5,0.5};
	
	ImGui::Begin("Begin pass");
	ImGui::ColorPicker3("Background color",rgb);
	ImGui::End();

	glEnable(GL_DEPTH_TEST);
    glClearColor(rgb[0],rgb[1],rgb[2],1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	return ctx;
};

void GLRenderer::end_pass(const RenderContext* ctx) 
{
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(fence);

	glBindFramebuffer(GL_FRAMEBUFFER,0);
};

void GLRenderer::draw_screen_texture(RenderTargetID id, glm::mat4 T) const
{
	const GLRenderTarget* target = impl->loader->get<GLRenderTarget>(id);

	if (!target) return;

	const GLMaterial * material = impl->loader->get<GLMaterial>(impl->defaults.materials.screen_quad);

	glUseProgram(material->program);
	glBindTextureUnit(GL_RENDERER_COLOR_ATTACHMENT_BINDING,target->color);

	const GLModel *model = impl->loader->get<GLModel>(impl->defaults.models.screen_quad);
	glBindVertexArray(model->vao);
	glDrawElements(GL_TRIANGLES,(int)model->icount,GL_UNSIGNED_INT,NULL);
}

//--------------------------------------------------------------------------------------------------
// Draw commands

void RenderContext::bind_material(MaterialID id) const
{
	const GLMaterial *material = loader->get<GLMaterial>(id); 

	if (!material) {
		log_error("Material does not exist with id %d", id);
		return;
	}

	for (const auto& pair : material->tex_bindings) {
		uint32_t binding = pair.first;
		GLTextureBinding tex_bind = pair.second;

		TextureID texID = tex_bind.id;
		const GLImage *tex = loader->get<GLImage>(texID);

		GLint filter = GL_LINEAR;

		if (!tex) {
			const RendererDefaults * defaults = renderer->get_defaults();
			tex = loader->get<GLImage>(defaults->textures.missing);
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
	const GLModel *model = loader->get<GLModel>(meshID);
	glBindVertexArray(model->vao);
	glDrawElements(GL_LINES,(int)model->icount,GL_UNSIGNED_INT,NULL);
}

void RenderContext::draw_cmd_basic_mesh3d(ModelID meshID, glm::mat4 T) const
{
	//glEnable(GL_CULL_FACE);
	//glCullFace(GL_BACK);
	//glFrontFace(GL_CCW);
	
	const GLModel *model = loader->get<GLModel>(meshID);
	glBindVertexArray(model->vao);
	glDrawElements(GL_TRIANGLES,(int)model->icount,GL_UNSIGNED_INT,NULL);

	glDisable(GL_CULL_FACE);
};
