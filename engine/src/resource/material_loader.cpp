#include "engine/renderer/opengl.h"

#include "engine/resource/resource_table.h"
#include "engine/resource/material_loader.h"
#include "engine/resource/shader_loader.h"
#include "engine/resource/texture_loader.h"

#include "resource/gl_utils.h"

#include <vector>
#include <string_view>
#include <utils/log.h>
#include <yaml-cpp/yaml.h>

struct Binding 
{
	std::string name;
	std::string type;
	std::string path;
};

struct ShaderPipeline
{
	std::string frag;
	std::string vert;
};

struct PreMaterialInfo
{
	std::string name;
	ShaderPipeline pipeline;
	std::vector<Binding> bindings;
};

static LoadResult gl_material_create(ResourceTable *loader, void **res, void *info);
static void gl_material_destroy(ResourceTable *loader, void *res);

static LoadResult gl_material_load_file(ResourceTable *loader, ResourceHandle h, const char *path);

ResourceAllocFns gl_material_alloc_fns = {
	.create = &gl_material_create,
	.destroy = &gl_material_destroy,
	.load_file = &gl_material_load_file 
};



static LoadResult parse_material_file(PreMaterialInfo *info, std::string_view path)
{
	YAML::Node root = YAML::LoadFile(path.data());

	const YAML::Node &shaders_node = root["shaders"];

	if (!shaders_node) {
		log_error("Material %s does not specify any shaders!",path);
		return RT_EUNKNOWN;
	}

	if (!shaders_node.IsMap()) {
		log_error("'shaders' field is not a map!");
		return RT_EUNKNOWN;
	}

	ShaderPipeline pipeline;	

	const YAML::Node &vert = shaders_node["vert"];
	const YAML::Node &frag = shaders_node["frag"];

	if (!frag) {
		log_error("Material %s does not contain a fragment shader",path);
		return RT_EUNKNOWN;
	}

	if (!vert) {
		log_error("Material %s does not contain a vertex shader", path);
		return RT_EUNKNOWN;
	}

	pipeline.frag = frag.as<std::string>();
	pipeline.vert = vert.as<std::string>();

	YAML::Node bindings_node = root["bindings"];

	if (bindings_node && !bindings_node.IsSequence()) {
		log_error("'bindings' node is not a sequence!");
		return RT_EUNKNOWN;
	}

	std::vector<Binding> bindings;

	if (bindings_node) {
		for (const auto& node : bindings_node) {
			Binding b;
			b.path = node["path"].as<std::string>();
			b.type = node["type"].as<std::string>();
			b.name = node["name"].as<std::string>();
			bindings.push_back(std::move(b));
		}
	}

	*info = {
		.name = std::string(path),
		.pipeline = std::move(pipeline),
		.bindings = std::move(bindings)
	};

	return RT_OK;
}

static LoadResult gl_material_load(ResourceTable *loader, GLMaterial *mat, const PreMaterialInfo *info) 
{
	LoadResult res = RT_OK;

	ShaderID vertID = shader_load_file(loader, info->pipeline.vert);
	if (!vertID) {
		res = RT_EUNKNOWN;
		return res;
	}

	ShaderID fragID = shader_load_file(loader, info->pipeline.frag);
	if (!fragID) {
		res = RT_EUNKNOWN;
		return res;
	}

	const GLShaderModule *vert = get_shader(loader,vertID);
	const GLShaderModule *frag = get_shader(loader,fragID);

	uint32_t program = glCreateProgram();
	glAttachShader(program, vert->id);
	glAttachShader(program, frag->id);
	glLinkProgram(program);

	if (!gl_check_program(program,info->name.c_str())) {
		glDeleteProgram(program);
		return RT_EUNKNOWN;
	}

	mat->program = program;
	mat->vert = vertID;
	mat->frag = fragID;

	typedef std::unordered_map<std::string,uint32_t> bind_map_t; 

	bind_map_t merged_ids;

	if (vert->bindings) {
		bind_map_t map = vert->bindings->ids;
		merged_ids.merge(map);
	}
	if (frag->bindings) {
		bind_map_t map = frag->bindings->ids;
		merged_ids.merge(map);
	}

	size_t bind_count = info->bindings.size();
	if (bind_count) {

		std::vector<ImageID> textures (bind_count,RESOURCE_HANDLE_NULL);

		const char* material_name = "material";

		for (size_t i = 0; i < info->bindings.size(); ++i) {
			const Binding *bind = &info->bindings[i];

			ImageID texID = image_load_file(loader,bind->path);

			if (texID == RESOURCE_HANDLE_NULL) {
				log_error(
					"While loading material %s : failed to load texture %s for binding=%s\n",
					info->name.c_str(),bind->path.c_str(),bind->name.c_str());
				continue;
			}

			textures[i] = texID;
		}

		std::unordered_map<uint32_t, GLTextureBinding> tex_bindings;

		for (size_t i = 0; i < bind_count; ++i) {
			ImageID texID = textures[i];
			
			const Binding *bind = &info->bindings[i];

			auto it = merged_ids.find(bind->name);
			if (it == merged_ids.end()) {
				log_error("While loading material %s : failed to find binding '%s'\n",
							material_name, bind->name.c_str());
				return RT_EUNKNOWN;
			}

			uint32_t id = it->second;

			GLTextureBinding texBinding = {
				.id = texID
			};

			tex_bindings[id] = std::move(texBinding);
		}

		mat->tex_bindings = std::move(tex_bindings);
	}

	log_info("Loaded material : %s",info->name.c_str());
	return res;
}

LoadResult gl_material_create(ResourceTable *loader, void **res, void *info)
{
	MaterialCreateInfo *ci = static_cast<MaterialCreateInfo*>(info);

	const char *path = ci->path.c_str();

	LoadResult result = RT_OK;

	PreMaterialInfo pre_info;
	try {
		result = parse_material_file(&pre_info,path);
	} catch (YAML::Exception e) {
		log_error("Error while parsing YAML: %s",e.what());
	}

	if (result) 
		return result;

	std::unique_ptr<GLMaterial> material ( new GLMaterial{});

	result = gl_material_load(loader, material.get(), &pre_info);

	if (result)
		return result;

	*res = material.release();
	 
	return result;
}

void gl_material_destroy(ResourceTable *loader, void *res) 
{
	GLMaterial *material = static_cast<GLMaterial*>(res);

	if (material->program) glDeleteProgram(material->program);

	delete material;
}

static void update_material_dependencies(ResourceTable *loader, ResourceHandle h)
{
	const GLMaterial *material = get_material(loader,h);

	const ResourceEntry *vert_ent = loader->get(material->vert);
	vert_ent->reload_info->add_subscriber(h);

	const ResourceEntry *frag_ent = loader->get(material->frag);
	frag_ent->reload_info->add_subscriber(h);

	for (auto &pair : material->tex_bindings) {
		ImageID img = pair.second.id;

		const ResourceEntry *img_ent = loader->get(img);

		if (!img_ent) 
			continue;

		img_ent->reload_info->add_subscriber(h);
	}
}

static LoadResult gl_material_load_file(ResourceTable *loader, ResourceHandle h, const char *path)
{
	MaterialCreateInfo ci = {
		.path = path
	};
	LoadResult result = loader->allocate(h,&ci);

	if (result != RT_OK) {
		return result;
	}

	update_material_dependencies(loader, h);

	return result;
}

ResourceHandle material_load_file(ResourceTable *loader, std::string_view path)
{
	if (ResourceHandle h = loader->find(path)) 
		return h;

	ResourceHandle h = loader->create_handle(RESOURCE_TYPE_MATERIAL);

	LoadResult result = loader->load_file(h,path.data());

	if (result != RT_OK)
		goto error_cleanup;

	loader->set_handle_key(h,path);
	return h;

error_cleanup:
	log_error("Failed to load material file at %s",path.data());
	loader->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}

const GLMaterial *get_material(ResourceTable *loader, ResourceHandle h)
{
	const ResourceEntry *ent = loader->get(h);
	if (!ent || ent->type != RESOURCE_TYPE_MATERIAL)
		return nullptr;

	return static_cast<const GLMaterial*>(ent->data);
}
