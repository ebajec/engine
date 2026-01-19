#ifndef EV2_PIPELINE_IMPL_H
#define EV2_PIPELINE_IMPL_H

#include <engine/renderer/opengl.h>

#include "ev2/resource.h"
#include "ev2/pipeline.h"

#include <unordered_map>
#include <string>
#include <memory>

#include <cstdint>

namespace ev2 {

enum DescriptorType : uint32_t
{
  	DESCRIPTOR_TYPE_SAMPLER                    =  0,        
  	DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER     =  1,       
  	DESCRIPTOR_TYPE_SAMPLED_IMAGE              =  2,      
  	DESCRIPTOR_TYPE_STORAGE_IMAGE              =  3,     
  	DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER       =  4,    
  	DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER       =  5,   
  	DESCRIPTOR_TYPE_UNIFORM_BUFFER             =  6,  
  	DESCRIPTOR_TYPE_STORAGE_BUFFER             =  7, 
  	DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC     =  8,
  	DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC     =  9, 
  	DESCRIPTOR_TYPE_INPUT_ATTACHMENT           = 10,
  	DESCRIPTOR_TYPE_MAX_ENUM           = UINT32_MAX,
};

struct DescriptorBinding
{
	DescriptorType type;
	uint32_t set;
	uint32_t id;
};

struct DescriptorLayout
{
	std::unordered_map<std::string, DescriptorBinding> bindings;
};

struct Shader
{
	GLuint id;
	ShaderStage stage;
	std::unique_ptr<DescriptorLayout> layout;
};

struct GraphicsPipeline
{
	ev2::ShaderID vert;
	ev2::ShaderID frag;
	GLuint program;
	GLuint vao;
	DescriptorLayout layout;
};

struct ComputePipeline
{
	Shader shader;
	GLuint program;
};

struct BufferBinding
{
	ev2::BufferID handle;
	size_t offset;
	size_t size;
};

struct TextureBinding
{
	ev2::TextureID handle;
};

struct ResourceBinding
{
	DescriptorType type;
	union {
		BufferBinding buf {};
		TextureBinding tex;
	};
};

struct DescriptorSet
{
	uint16_t index;
	std::unordered_map<uint16_t, ResourceBinding> bindings;
};

};

#endif // EV2_PIPELINE_IMPL_H
