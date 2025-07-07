#include "material_loader.h"
#include "shader_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

#include "gl_renderer.h"
#include "def_gl.h"

#include <glm/gtc/type_ptr.hpp>
#include <atomic>

static constexpr vertex2d default_tex_quad_verts[] = {
	vertex2d{glm::vec2(-1,-1), glm::vec2(0, 0)},
	vertex2d{glm::vec2(1, -1), glm::vec2(1, 0)},
	vertex2d{glm::vec2(1,  1), glm::vec2(1, 1)},
	vertex2d{glm::vec2(-1, 1), glm::vec2(0, 1)}
};
static constexpr uint32_t default_tex_quad_indices[] = {
	0,1,2,0,2,3
};

enum gl_renderer_bindings
{
	GL_RENDERER_COLOR_ATTACHMENT_BINDING = 0,
	GL_RENDERER_FRAMEDATA_BINDING = 5
};

struct gl_target_uniforms
{
	glm::mat4 p;
	glm::mat4 v;
	glm::mat4 pv;

	float t;
};

struct gl_tex_quad
{
	gl_vao vao;
	gl_vbo vbo;
	gl_vbo ibo;
};

struct gl_render_target
{
	uint32_t w;
	uint32_t h;

	gl_framebuffer fbo;
	gl_tex color;
	gl_renderbuffer depth;
	gl_ubo ubo;
	
	// these are for drawing to a window
	~gl_render_target();
};

struct base_resources_t
{
	MaterialID screen_quad;
};

struct gl_renderer_impl
{
	std::shared_ptr<ResourceLoader> loader;

	std::atomic_uint32_t render_target_counter;
	std::unordered_map<RenderTargetID, 
		std::unique_ptr<gl_render_target>> render_targets;

	gl_tex_quad screen_quad;

	base_resources_t base_resources;

	gl_render_target* get_target(RenderTargetID id);
};

//--------------------------------------------------------------------------------------------------
// Tex quad

void gl_tex_quad_draw(const gl_tex_quad* quad)
{
	glBindVertexArray(quad->vao);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

int gl_tex_quad_create(gl_tex_quad *quad)
{
	gl_vao vao;
	glGenVertexArrays(1,&vao);
	glBindVertexArray(vao);

	gl_vbo ibo;
	glGenBuffers(1,&ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			  sizeof(default_tex_quad_indices),default_tex_quad_indices,GL_STATIC_DRAW);

	gl_vbo vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, 
			  sizeof(default_tex_quad_verts),default_tex_quad_verts,GL_STATIC_DRAW);

	glEnableVertexArrayAttrib(vao,0);
	glEnableVertexArrayAttrib(vao,1);
	glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(vertex2d),(void*)offsetof(vertex2d,position));
	glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(vertex2d),(void*)offsetof(vertex2d,uv));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER,0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);

	quad->vao = vao;
	quad->vbo = vbo;
	quad->ibo = ibo;

	return 0;
}

void gl_tex_quad_destroy(gl_tex_quad* quad)
{
	if (quad->vao) glDeleteVertexArrays(1,&quad->vao);
	if (quad->vbo) glDeleteBuffers(1, &quad->vbo);
	if (quad->ibo) glDeleteBuffers(1, &quad->ibo);
}


//--------------------------------------------------------------------------------------------------
// Frame ubo

gl_target_uniforms gl_target_uniforms_create(const RenderContext* ctx)
{
	gl_target_uniforms u;
	u.p = ctx->camera.proj;
	u.v = ctx->camera.view;
	u.pv = u.p*u.v;
	u.t = glfwGetTime();
	return u;
}

//--------------------------------------------------------------------------------------------------
// Render Target

std::unique_ptr<gl_render_target> gl_render_target_create(const RenderTargetCreateInfo* info)
{
	uint32_t w = info->w;
	uint32_t h = info->h;

	gl_ubo ubo;
	glGenBuffers(1,&ubo);
	glBindBuffer(GL_UNIFORM_BUFFER,ubo);
	glBufferData(GL_UNIFORM_BUFFER,sizeof(gl_target_uniforms),NULL,GL_DYNAMIC_READ);
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
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);

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
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

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

	impl->base_resources.screen_quad = load_material_file(impl->loader.get(),"material/screen_quad.yaml");

	gl_tex_quad_create(&impl->screen_quad);

	return std::unique_ptr<GLRenderer>(renderer);
}

GLRenderer::~GLRenderer()
{
	gl_tex_quad_destroy(&impl->screen_quad);
}

RenderTargetID GLRenderer::create_target(const RenderTargetCreateInfo* info)
{
	uint32_t id = impl->render_target_counter++;
	impl->render_targets[id] = std::move(gl_render_target_create(info));
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
	impl->render_targets[id] = std::move(gl_render_target_create(info));
}

void GLRenderer::begin_pass(const RenderContext* ctx) 
{
	// don't even bother with this one
	assert(ctx);

	gl_render_target* target = impl->get_target(ctx->target); 

	if (!target) return;

	// upload uniforms

	gl_target_uniforms uniforms = gl_target_uniforms_create(ctx);

	glBindBuffer(GL_UNIFORM_BUFFER,target->ubo);
	glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(uniforms),&uniforms);
	glBindBuffer(GL_UNIFORM_BUFFER,0);

	// bindings

	glBindBufferBase(GL_UNIFORM_BUFFER,GL_RENDERER_FRAMEDATA_BINDING,target->ubo);
	glBindFramebuffer(GL_FRAMEBUFFER,target->fbo);

	// initialze fbo

	glEnable(GL_DEPTH_TEST);
    glClearColor(.8,0.8,0.8,1);
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

	bind_material(impl->base_resources.screen_quad);

	glBindTextureUnit(GL_RENDERER_COLOR_ATTACHMENT_BINDING,target->color);

	gl_tex_quad_draw(&impl->screen_quad);
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
			// TODO: Bind default texture

		} else {
			glBindTexture(GL_TEXTURE_2D, tex->id);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);   
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);   
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D,0);

			glBindTextureUnit(binding,tex->id);
		}
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
	glDrawElements(GL_TRIANGLES,model->icount,GL_UNSIGNED_INT,NULL);

	glDisable(GL_CULL_FACE);
};
