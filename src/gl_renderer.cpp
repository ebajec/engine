#include "material_loader.h"
#include "shader_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

#include "def_gl.h"
#include "gl_renderer.h"
#include "renderer_defaults.h"

#include <glm/gtc/type_ptr.hpp>
#include <atomic>

struct gl_renderer_impl
{
	std::shared_ptr<ResourceLoader> loader;

	std::atomic_uint32_t render_target_counter;
	std::unordered_map<RenderTargetID, 
		std::unique_ptr<gl_render_target>> render_targets;

	RendererDefaults defaults;

	gl_render_target* get_target(RenderTargetID id);
};

//--------------------------------------------------------------------------------------------------
// Frame ubo

static gl_framedata_t gl_target_uniforms_create(const RenderContext* ctx)
{
	gl_framedata_t u;
	u.p = ctx->camera.proj;
	u.v = ctx->camera.view;
	u.pv = u.p*u.v;
	u.t = (float)glfwGetTime();
	return u;
}

//--------------------------------------------------------------------------------------------------
// Render Target

static std::unique_ptr<gl_render_target> gl_render_target_create(const RenderTargetCreateInfo* info)
{
	uint32_t w = info->w;
	uint32_t h = info->h;

	gl_ubo ubo;
	glGenBuffers(1,&ubo);
	glBindBuffer(GL_UNIFORM_BUFFER,ubo);
	glBufferData(GL_UNIFORM_BUFFER,sizeof(gl_framedata_t),NULL,GL_DYNAMIC_READ);
	glBindBuffer(GL_UNIFORM_BUFFER,0);

	gl_framebuffer fbo;
	glGenFramebuffers(1,&fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	// color attachment is optional
	gl_tex color {};
	if (info->flags & RENDER_TARGET_CREATE_COLOR_BIT)
	{
		glGenTextures(1,&color);
		glBindTexture(GL_TEXTURE_2D,color);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(int)w,(int)h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER,
							GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	}

	// depth attachment is optional
	gl_renderbuffer depth {};

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

	if (!color && !depth) {
		log_error("gl_render_target_create : No depth or color attachment set!");
	}

	// zero initialize
	std::unique_ptr<gl_render_target> target(new gl_render_target{});

	target->w = w;
	target->h = h;
	target->fbo = fbo;
	target->color = color;
	target->depth = depth;
	target->ubo = ubo;

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		log_error("gl_render_target_create : failed to create framebuffer!");
		goto GL_RENDER_TARGET_CREATE_CLEANUP;
	}

GL_RENDER_TARGET_CREATE_CLEANUP:
	glBindBuffer(GL_UNIFORM_BUFFER,0);
	glBindTexture(GL_TEXTURE_2D,0);
	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glBindRenderbuffer(GL_RENDERBUFFER,0);
	return target;
}

gl_render_target::~gl_render_target()
{
	if (fbo) glDeleteFramebuffers(1,&fbo);
	if (color) glDeleteTextures(1,&color);
	if (depth) glDeleteRenderbuffers(1,&depth);
	if (ubo) glDeleteBuffers(1,&ubo);
}


gl_render_target* gl_renderer_impl::get_target(RenderTargetID id)
{
	gl_render_target* target = nullptr;

	if (auto iter = render_targets.find(id); 
		iter != render_targets.end()){
		target = iter->second.get();
	} else {
		log_error("gl_renderer::draw_frame : failed to get render_target with id %d!", id);
	}

	return target;
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

RenderTargetID GLRenderer::create_target(const RenderTargetCreateInfo* info)
{
	uint32_t id = impl->render_target_counter++;
	impl->render_targets[id] = gl_render_target_create(info);
	return id;
}

void GLRenderer::reset_target(RenderTargetID id, const RenderTargetCreateInfo* info)
{
	auto iter = impl->render_targets.find(id); 

	if (iter == impl->render_targets.end()){
		log_error("gl_renderer::resize_target : failed to get render_target with id %d!", id);
		return;
	}

	impl->render_targets.erase(id);
	impl->render_targets[id] = gl_render_target_create(info);
}

void GLRenderer::begin_pass(const RenderContext* ctx) 
{
	// don't even bother with this one
	assert(ctx);

	gl_render_target* target = impl->get_target(ctx->target); 

	if (!target) return;

	// upload uniforms

	gl_framedata_t uniforms = gl_target_uniforms_create(ctx);

	glBindBuffer(GL_UNIFORM_BUFFER,target->ubo);
	glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(uniforms),&uniforms);
	glBindBuffer(GL_UNIFORM_BUFFER,0);

	// bindings

	glBindBufferBase(GL_UNIFORM_BUFFER,GL_RENDERER_FRAMEDATA_BINDING,target->ubo);
	glBindFramebuffer(GL_FRAMEBUFFER,target->fbo);

	// initialze fbo

	glEnable(GL_DEPTH_TEST);
    glClearColor(.8f,0.8f,0.8f,1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
};

void GLRenderer::end_pass(const RenderContext* ctx) 
{
	gl_render_target* target = impl->get_target(ctx->target);;

	if (!target) return;

    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(fence);

	glBindFramebuffer(GL_FRAMEBUFFER,0);
};

void GLRenderer::draw_target(RenderTargetID id, glm::mat4 T)
{
	gl_render_target* target = impl->get_target(id);

	if (!target) return;

	bind_material(impl->defaults.materials.screen_quad);

	glBindTextureUnit(GL_RENDERER_COLOR_ATTACHMENT_BINDING,target->color);

	const GLModel *model = get_model(impl->loader.get(),impl->defaults.models.screen_quad);
	glBindVertexArray(model->vao);
	glDrawElements(GL_TRIANGLES,(int)model->icount,GL_UNSIGNED_INT,NULL);
}

void GLRenderer::bind_material(MaterialID id) 
{
	const GLMaterial *material = get_material(impl->loader.get(),id); 

	if (!material) {
		log_error("Material does not exist with id %d", id);
		return;
	}

	for (const auto& pair : material->tex_bindings) {
		uint32_t binding = pair.first;
		GLTextureBinding tex_bind = pair.second;

		TextureID texID = tex_bind.id;
		const GLImage *tex = get_image(impl->loader.get(),texID);

		if (!tex) {
			tex = get_image(impl->loader.get(), impl->defaults.textures.missing);
		}

		glBindTexture(GL_TEXTURE_2D, tex->id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);   
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D,0);

		glBindTextureUnit(binding,tex->id);
	}

	glUseProgram(material->program);
}

//--------------------------------------------------------------------------------------------------
// Draw commands

void GLRenderer::draw_cmd_basic_mesh3d(ModelID meshID, glm::mat4 T)
{
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	const GLModel *model = get_model(impl->loader.get(),meshID);
	glBindVertexArray(model->vao);
	glDrawElements(GL_TRIANGLES,(int)model->icount,GL_UNSIGNED_INT,NULL);

	glDisable(GL_CULL_FACE);
};
