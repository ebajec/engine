#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include <utils/log.h>
#include "resource_loader.h"
#include "render_target.h"
#include "renderer_defaults.h"

#include <glm/matrix.hpp>
#include <glm/integer.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <memory>

class GLRenderer;

struct Camera
{
	glm::mat4 proj;
	glm::mat4 view;
};

struct BeginPassInfo
{
	RenderTargetID target;
	const Camera *camera;
};

struct FrameContext
{

};

struct RenderContext
{
	const GLRenderer *renderer;
	ResourceLoader *loader;
	RenderTargetID target;
	Camera camera;

	void bind_material(MaterialID material) const;
	void draw_cmd_basic_mesh3d(ModelID meshID, glm::mat4 T) const;
	void draw_cmd_mesh_outline(ModelID meshID) const;
};

//--------------------------------------------------------------------------------------------------
// OpenGL renderer

struct GLRendererCreateInfo
{
	std::shared_ptr<ResourceLoader> resource_loader;
};

typedef struct gl_renderer_impl gl_renderer_impl;
class GLRenderer 
{
	gl_renderer_impl* impl;
public:
    GLRenderer(){}
	~GLRenderer();

	static std::unique_ptr<GLRenderer> create(const GLRendererCreateInfo* info);

	void begin_frame(uint32_t w, uint32_t h);
	void end_frame();

	RenderContext begin_pass(const BeginPassInfo *info);
    void end_pass(const RenderContext* ctx);

	void draw_screen_texture(RenderTargetID id, glm::mat4 T) const; 
	const RendererDefaults *get_defaults() const;
};

#endif
