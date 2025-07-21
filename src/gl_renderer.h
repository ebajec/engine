#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include <utils/log.h>
#include "resource_loader.h"

#include <glm/matrix.hpp>
#include <glm/integer.hpp>

#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <memory>

typedef uint32_t RenderTargetID;

struct Camera
{
	glm::mat4 proj;
	glm::mat4 view;
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

struct RenderContext
{
	RenderTargetID target;
	Camera camera;
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

	RenderTargetID create_target(const RenderTargetCreateInfo* info);
	void reset_target(RenderTargetID id, const RenderTargetCreateInfo* info);

	void begin_frame(uint32_t w, uint32_t h);
	void end_frame();

    void begin_pass(const RenderContext* ctx);
    void end_pass(const RenderContext* ctx);

	void bind_material(MaterialID material) const;

	/// @brief Render a target to the screen to a region specified by the transformation
	/// matrix.
	void draw_target(RenderTargetID id, glm::mat4 T) const; 
	void draw_cmd_basic_mesh3d(ModelID meshID, glm::mat4 T) const;
	void draw_cmd_mesh_outline(ModelID meshID) const;
};

#endif
