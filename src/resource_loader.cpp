#include "resource_loader.h"
#include "material_loader.h"
#include "shader_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

#include <utils/log.h>

#include <mutex>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

static ResourceType resource_type_from_path(const char *path)
{
	std::string s (path);

	if (s.ends_with(".spv")) {
		return RESOURCE_TYPE_SHADER;
	}
	if (s.ends_with(".yaml")) {
		return RESOURCE_TYPE_MATERIAL;
	}
	if (s.ends_with(".png") || s.ends_with("jpg")) {
		return RESOURCE_TYPE_IMAGE;
	}

	return RESOURCE_TYPE_NONE;
}

static void monitor_callback(void *usr, utils::monitor_event_t event) 
{
	if (event.flags & MONITOR_FLAGS_ISDIR || !(event.flags & MONITOR_FLAGS_MODIFY))
		return;

	ResourceHotReloader *watcher = static_cast<ResourceHotReloader*>(usr);

	fs::path root = watcher->loader->resource_path;

	fs::path event_path;
	try {
		event_path = fs::relative(event.path,root);
	} catch (std::exception e) {
		log_error("%s",e.what());
		return;
	}

	ResourceType resource_type = resource_type_from_path(event_path.c_str());

	if (resource_type == RESOURCE_TYPE_NONE)
		return;

	ResourceUpdateInfo update_info = {
		.type = resource_type,
		.path = std::move(event_path)
	};

	std::unique_lock<std::mutex> lock(watcher->mut);
	watcher->updates.push_back(std::move(update_info));
}

std::unique_ptr<ResourceHotReloader> ResourceHotReloader::create(std::shared_ptr<ResourceLoader> loader)
{
	std::unique_ptr<ResourceHotReloader> watcher (new ResourceHotReloader);
	watcher->loader = loader;

	if (!fs::is_directory(watcher->loader->resource_path))
		return nullptr;

	watcher->monitor = std::unique_ptr<utils::monitor>(new utils::monitor(
		&monitor_callback, watcher.get(), watcher->loader->resource_path.c_str()));

	return watcher;
}

LoadResult ResourceHotReloader::process_updates()
{
	std::unique_lock<std::mutex> lock (mut);
	std::vector<ResourceUpdateInfo> queue = std::move(updates);
	lock.unlock();

	for (ResourceUpdateInfo& info : queue) {
		log_info("Reloading resource at : %s",info.path.c_str());

		if (info.type == RESOURCE_TYPE_SHADER &&
			info.path.size() >= sizeof(".spv")) {
			info.path.resize(info.path.size() - sizeof(".spv") + 1);
		}

		ResourceHandle id = loader->find(info.path);

		if (!id) {
			continue;
		}

		LoadResult res = resource_reload(loader.get(), id);

		if (res) {
			log_error("Failed to reload resource : %s",info.path.c_str());
			continue;
		}
	}

	return RESULT_SUCCESS;
}

std::string ResourceLoader::make_path_abs(std::string_view str) {
	return fs::path(resource_path) / fs::path(str);
}

//------------------------------------------------------------------------------
// V2

std::unique_ptr<ResourceLoader> ResourceLoader::create(const ResourceLoaderCreateInfo *info)
{
	std::unique_ptr<ResourceLoader> loader = 
		std::unique_ptr<ResourceLoader>(new ResourceLoader());

	loader->resource_path = info->resource_path;
	
	// TODO : Think of a nice way to not initialize these here.
	
	loader->pfns[RESOURCE_TYPE_NONE] =	{};
	loader->pfns[RESOURCE_TYPE_MATERIAL] = g_material_loader_fns;
	loader->pfns[RESOURCE_TYPE_SHADER] = g_shader_loader_fns; 	
	loader->pfns[RESOURCE_TYPE_IMAGE] = g_image_loader_fns; 
	loader->pfns[RESOURCE_TYPE_MODEL] = g_model_loader_fns;	

	return loader;
}

static void cleanup(ResourceLoader *loader)
{
	for (size_t i = 0; i < loader->entries.size(); ++i) {
		ResourceHandle h = (uint32_t)i + 1;
		loader->destroy_handle(h);
	}
}

ResourceLoader::~ResourceLoader()
{
	cleanup(this);
}

ResourceHandle ResourceLoader::create_handle(ResourceType type)
{
	ResourceHandle h = RESOURCE_HANDLE_NULL;
	ResourceEntry *ent = nullptr;

	std::unique_lock<std::shared_mutex> lock(mut);
	if (!free_slots.empty()) {
		h = free_slots.top();
		free_slots.pop();

		ent = entries[h - 1].get();
	} else {
	 	h = static_cast<ResourceHandle>(entries.size() + 1);
	 	ent = new ResourceEntry{};
		entries.push_back(std::unique_ptr<ResourceEntry>(ent));
	}
	lock.unlock();

	assert(ent);

	ent->type = type;

	return h;
}

void ResourceLoader::destroy_handle(ResourceHandle h)
{
	// TODO : When the time comes, push deletions to queue to be processed
	// at the end of every frame

	ResourceEntry *ent = get_internal(h);

	if (!ent) 
		return;

	// TODO : lol...
	assert(!ent->refs.load());

	ResourceType type = ent->type;

	if (ent->data)
		pfns[type].destroy_fn(this, ent->data);

	ent->data = nullptr;

#if RESOURCE_ENABLE_HOT_RELOAD
	ent->reload_info.reset(nullptr);
#endif
	ent->status = RESOURCE_STATUS_EMPTY;
	ent->type = RESOURCE_TYPE_NONE;

	std::unique_lock<std::shared_mutex> lock(mut);
	free_slots.push(h);
}

ResourceHandle ResourceLoader::find(std::string_view key) 
{
	std::shared_lock<std::shared_mutex> lock(mut);
	auto it = map.find(key.data());

	if (it == map.end()) {
		return 0;
	}

	return it->second;
}

const ResourceEntry *ResourceLoader::get(ResourceHandle h) 
{
	if (h == RESOURCE_HANDLE_NULL || h > entries.size()) 
		return nullptr;

	ResourceEntry *ent = entries[h - 1].get(); 

	if (ent->type == RESOURCE_TYPE_NONE)
		return nullptr;

	return ent;
}

ResourceEntry *ResourceLoader::get_internal(ResourceHandle h) 
{
	return const_cast<ResourceEntry*>(get(h));
}

void ResourceLoader::set_handle_key(ResourceHandle h, std::string_view key)
{
	std::unique_lock<std::shared_mutex> lock(mut);
	map[key.data()] = h;
}

LoadResult resource_load(ResourceLoader *loader, ResourceHandle h, void *info)
{
	ResourceEntry *ent = loader->get_internal(h);

	LoadResult result = RESULT_SUCCESS;

	if (ent->status != RESOURCE_STATUS_EMPTY)
		return result;

	ResourceType type = ent->type;

	void* data = nullptr;

	OnResourceCreate create_fn = loader->pfns[type].create_fn; 
	assert(create_fn);

	result = create_fn(loader, &data, info);

	if (result != RESULT_SUCCESS) {
		ent->status = RESOURCE_STATUS_INVALID;
		return result;
	}

#if RESOURCE_ENABLE_HOT_RELOAD
	if (auto create_reload_info_fn = loader->pfns[type].create_reload_info_fn) {
		ent->reload_info.reset(create_reload_info_fn(info));
	}
#endif

	ent->status = RESOURCE_STATUS_READY;
	ent->data = data;

	return result;
}

LoadResult resource_reload(ResourceLoader *loader, ResourceHandle h)
{
	if (h == RESOURCE_HANDLE_NULL)
		return RESULT_ERROR;

	ResourceEntry *ent = loader->get_internal(h);

	if (!ent || !ent->reload_info)
		return RESULT_ERROR;

	LoadResult result = RESULT_SUCCESS;

	void *new_data = nullptr;
	ResourceType type = ent->type;

	OnResourceCreate create_fn = loader->pfns[type].create_fn; 
	assert(create_fn);

	result = create_fn(loader,&new_data, ent->reload_info->p_create_info);

	if (result != RESULT_SUCCESS) {
		return result;
	}

	OnResourceDestroy destroy_fn = loader->pfns[type].destroy_fn;
	assert(destroy_fn);

	destroy_fn(loader, ent->data);

	ent->data = new_data;
	ent->status = RESOURCE_STATUS_READY;

	for (ResourceHandle sub : ent->reload_info->subscribers) {
		resource_reload(loader,sub);
	}

	return result;
}
