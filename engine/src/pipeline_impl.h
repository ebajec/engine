#ifndef EV2_PIPELINE_IMPL_H
#define EV2_PIPELINE_IMPL_H

#include "ev2/resource.h"
#include "ev2/pipeline.h"

#include <vector>
#include <unordered_map>
#include <string>

#include <cstdint>

namespace ev2 {

enum DescriptorBindingType
{
	BINDING_TYPE_STORAGE_BUFFER,
	BINDING_TYPE_UNIFORM_BUFFER,
	BINDING_TYPE_STORAGE_IMAGE,
	BINDING_TYPE_SAMPLER
};

struct DescriptorBinding
{
	uint16_t set;
	uint16_t id;
	DescriptorBindingType type;
};

struct DescriptorLayout
{
	std::unordered_map<std::string, DescriptorBinding> bindings;
};

struct GraphicsPipeline
{
	ev2::ShaderID vert;
	ev2::ShaderID frag;
	uint32_t program;

	DescriptorLayout layout;
};

struct ComputePipeline
{
	ev2::ShaderID comp;
	uint32_t program;

	DescriptorLayout layout;
};

struct ResourceBinding
{
	DescriptorBindingType type;
	union {
		ev2::BufferID buf = EV2_NULL_HANDLE
		ev2::TextureID tex;
	};
};

struct DescriptorSet
{
	uint16_t index;
	std::unordered_map<uint16_t, ResourceBinding> bindings;
};

};

#endif // EV2_PIPELINE_IMPL_H
