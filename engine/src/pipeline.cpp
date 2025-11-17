#include "ev2/pipeline.h"

#include "device_impl.h"
#include "pipeline_impl.h"
#include "resource_impl.h"

#include "utils/log.h"

#include "engine/resource/material_loader.h"
#include "engine/resource/shader_loader.h"
#include "engine/resource/compute_pipeline.h"
#include "engine/resource/buffer.h"
#include "engine/resource/texture_loader.h"

#include <vector>
#include <algorithm>

namespace ev2 {

//------------------------------------------------------------------------------

ShaderID load_shader(Device *dev, const char *path)
{
	return ::shader_load_file(dev->rt.get(), path);
}

//------------------------------------------------------------------------------

GraphicsPipelineID load_graphics_pipeline(Device *dev, const char *path)
{
}

//------------------------------------------------------------------------------

ComputePipelineID load_compute_pipeline(Device *dev, const char *path)
{
}

//------------------------------------------------------------------------------

DescriptorLayoutID get_graphics_pipeline_layout(Device *dev, GraphicsPipelineID pipe)
{
}

DescriptorLayoutID get_compute_pipeline_layout(Device *dev, ComputePipelineID pipe)
{
	const GLComputePipeline * res = dev->rt->get<GLComputePipeline>(pipe);
	const GLShaderModule * shader = dev->rt->get<GLShaderModule>(res->shader);

	return reinterpret_cast<DescriptorLayoutID>(shader->bindings.get());
}

DescriptorSlot find_descriptor_slot(DescriptorLayoutID id, const char *name)
{
	DescriptorLayout *layout = EV2_TYPE_PTR_CAST(DescriptorLayout, id);

	auto it = layout->bindings.find(name);

	if (it == layout->bindings.end()) {
		return {.set = it->second.set, .id = it->second.id};
	}
	return {UINT16_MAX,UINT16_MAX};
}

DescriptorSetID create_descriptor_set(
	Device *dev, 
	DescriptorLayoutID layout_id, 
	uint32_t index
)
{
	DescriptorLayout * layout = EV2_TYPE_PTR_CAST(DescriptorLayout, layout_id);
	DescriptorSet * set = new DescriptorSet{};
	set->index = index;

	for (const auto& [name, bind] : layout->bindings) {
	  	ResourceBinding binding = {
	  		.type = bind.type
	  	};
		set->bindings[bind.id] = binding;	
	}

	if (set->bindings.empty()) {
		log_warn("No bindings initialized for pipeline");
	}

	return EV2_HANDLE_CAST(DescriptorSet, set);
}

void destroy_descriptor_set(Device *dev, DescriptorSetID id)
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, id);

	delete set;
}

void descriptor_set_bind_buffer(
	Device *dev, 
	DescriptorSetID set_id, 
	DescriptorSlot slot, 
	BufferID buf_id, 
	size_t offset, 
	size_t size
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	GLBuffer *buf = dev->rt->get<GLBuffer>(buf_id);

	if (set->index != slot.set) {
		log_error(
			"Mismatched binding for buffer %d. (set=%d, index=%d) to set %d",
			buf_id, slot.set, slot.id, set->index
		);
		return;
	}

	auto it = set->bindings.find(slot.id);

	if (it == set->bindings.end()) {
		log_error(
			"Attempting to bind buffer %d to nonexistent index %d in set %d", 
			buf_id, slot.id, slot.set);
	}

	ResourceBinding *binding = &it->second;
	if (
		binding->type != BINDING_TYPE_STORAGE_BUFFER && 
		binding->type != BINDING_TYPE_UNIFORM_BUFFER) 
		return;

	binding->buf = buf_id;
}

void descriptor_set_bind_texture(
	Device *dev, 
	DescriptorSetID set_id, 
	DescriptorSlot slot, 
	TextureID tex_id  
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	Texture *tex = EV2_TYPE_PTR_CAST(Texture, tex_id);

	GLImage *img = dev->rt->get<GLImage>(tex->img);

	if (set->index != slot.set) {
		log_error(
			"Mismatched binding for image %d. (set=%d, index=%d) to set %d",
			tex->img, slot.set, slot.id, set->index
		);
		return;
	}

	auto it = set->bindings.find(slot.id);

	if (it == set->bindings.end()) {
		log_error(
			"Attempting to bind image %d to nonexistent index %d in set %d", 
			tex->img, slot.id, slot.set);
		return;
	}

	ResourceBinding *binding = &it->second;

	if (
		binding->type != BINDING_TYPE_SAMPLER && 
		binding->type != BINDING_TYPE_STORAGE_IMAGE) 
		return;

	binding->buf = tex_id;
}

};
