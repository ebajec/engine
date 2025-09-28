#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include <engine/renderer/types.h>

#include <engine/resource/resource_table.h>
#include <engine/resource/model_loader.h>
#include <engine/resource/material_loader.h>
#include <engine/resource/render_target.h>
#include <engine/resource/buffer.h>

#include <glm/matrix.hpp>
#include <glm/integer.hpp>

#include <memory>

class Renderer;
struct FrameContext;

struct BeginPassInfo
{
	RenderTargetID target;
};

struct RenderContext
{
	const Renderer *renderer;
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
	const Renderer *renderer;
	ResourceTable *rt;
	Framedata data;
	BufferID ubo;

	RenderContext begin_pass(const BeginPassInfo *info);
    void end_pass(const RenderContext* ctx);
};

struct RendererCreateInfo
{
	ResourceTable *resource_table;
};

struct renderer_impl;

class Renderer 
{
public:
	struct renderer_impl* impl;

    Renderer(){}
	~Renderer();

	static std::unique_ptr<Renderer> create(const RendererCreateInfo* info);

	FrameContext begin_frame(FrameBeginInfo const *info);
	void end_frame(FrameContext* ctx);

	void present(RenderTargetID id, uint32_t w, uint32_t h) const;
};

#endif
