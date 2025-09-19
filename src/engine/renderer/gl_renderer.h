#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include "renderer/opengl.h"

#include <utils/log.h>

#include <resource/resource_table.h>
#include <resource/render_target.h>
#include <resource/buffer.h>

#include "renderer/defaults.h"

#include <glm/matrix.hpp>
#include <glm/integer.hpp>

#include <memory>

class GLRenderer;
struct FrameContext;

struct DrawCommand 
{
    uint  count;
    uint  instanceCount;
    uint  firstIndex;
    int  baseVertex;
    uint  baseInstance;
};

struct Camera
{
	glm::mat4 proj;
	glm::mat4 view;
};

struct BeginPassInfo
{
	RenderTargetID target;
};

struct RenderContext
{
	const GLRenderer *renderer;
	ResourceTable *rt;
	RenderTargetID target;
	BufferID frame_ubo;

	void bind_material(MaterialID material) const;
	void draw_cmd_basic_mesh3d(ModelID meshID, glm::mat4 T) const;
	void draw_cmd_mesh_outline(ModelID meshID) const;
};

struct FrameBeginInfo
{
	Camera const *camera;
};

struct FrameContext
{
	const GLRenderer *renderer;
	ResourceTable *table;
	Framedata data;
	BufferID ubo;

	RenderContext begin_pass(const BeginPassInfo *info);
    void end_pass(const RenderContext* ctx);
};


//--------------------------------------------------------------------------------------------------
// OpenGL renderer

struct GLRendererCreateInfo
{
	ResourceTable *resource_table;
};

typedef struct gl_renderer_impl gl_renderer_impl;
class GLRenderer 
{
	gl_renderer_impl* impl;
public:
    GLRenderer(){}
	~GLRenderer();

	static std::unique_ptr<GLRenderer> create(const GLRendererCreateInfo* info);

	FrameContext begin_frame(FrameBeginInfo const *info);
	void end_frame(FrameContext* ctx);

	void present(RenderTargetID id, uint32_t w, uint32_t h) const;

	const RendererDefaults *get_defaults() const;
};

#endif
