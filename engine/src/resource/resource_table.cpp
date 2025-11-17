#include "resource/resource_table.h"
#include "resource/material_loader.h"
#include "resource/compute_pipeline.h"
#include "resource/shader_loader.h"
#include "resource/texture_loader.h"
#include "resource/model_loader.h"
#include "resource/buffer.h"
#include "resource/render_target.h"

#include <utils/log.h>

#include <vector>
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

	ResourceReloader *watcher = static_cast<ResourceReloader*>(usr);

	fs::path root = watcher->table->resource_path;

	fs::path event_path;
	try {
		event_path = fs::relative(event.path,root);
	} catch (std::exception e) {
		log_error("%s",e.what());
		return;
	}

	std::string event_path_str = event_path.string();
	ResourceType resource_type = resource_type_from_path(event_path_str.c_str());

	if (resource_type == RESOURCE_TYPE_NONE)
		return;

	ResourceUpdateInfo update_info = {
		.type = resource_type,
		.path = std::move(event_path_str)
	};

	std::unique_lock<std::mutex> lock(watcher->mut);
	watcher->updates.push_back(std::move(update_info));
}

std::unique_ptr<ResourceReloader> ResourceReloader::create(ResourceTable* table)
{
	std::unique_ptr<ResourceReloader> watcher (new ResourceReloader);
	watcher->table = table;

	if (!fs::is_directory(watcher->table->resource_path))
		return nullptr;

	watcher->monitor = std::unique_ptr<utils::monitor>(new utils::monitor(
		&monitor_callback, watcher.get(), watcher->table->resource_path.c_str()));

	return watcher;
}

static LoadResult reload_file_resource(ResourceTable *loader, ResourceHandle h) 
{
	if (h == RESOURCE_HANDLE_NULL)
		return RT_EUNKNOWN;

	ResourceEntry *ent = loader->get_internal(h);

	if (!ent || !ent->reload_info)
		return RT_EUNKNOWN;

	LoadResult result = RT_OK;

	loader->load_file(h,ent->reload_info->path.c_str());

	for (ResourceHandle sub : ent->reload_info->subscribers) {
		reload_file_resource(loader,sub);
	}

	return result;
}

LoadResult ResourceReloader::process_updates()
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

		ResourceHandle id = table->find(info.path);

		if (!id) {
			continue;
		}

		LoadResult res = reload_file_resource(table,id);

		if (res) {
			log_error("Failed to reload resource : %s",info.path.c_str());
			continue;
		}
	}

	return RT_OK;
}

std::string ResourceTable::make_path_abs(std::string_view str)
{
	fs::path res_path (resource_path);
	fs::path child_path (str);
	fs::path path = res_path / child_path; 
	return path.string();
}

//------------------------------------------------------------------------------
// V2

std::unique_ptr<ResourceTable> ResourceTable::create(const ResourceTableCreateInfo *info)
{
	std::unique_ptr<ResourceTable> loader = 
		std::unique_ptr<ResourceTable>(new ResourceTable());

	loader->resource_path = info->resource_path;

	loader->register_type(stringify(RESOURCE_TYPE_NONE));
	loader->register_type(stringify(RESOURCE_TYPE_MATERIAL));
	loader->register_type(stringify(RESOURCE_TYPE_COMPUTE_PIPELINE));
	loader->register_type(stringify(RESOURCE_TYPE_MODEL));
	loader->register_type(stringify(RESOURCE_TYPE_IMAGE));
	loader->register_type(stringify(RESOURCE_TYPE_BUFFER));
	loader->register_type(stringify(RESOURCE_TYPE_SHADER));
	loader->register_type(stringify(RESOURCE_TYPE_RENDER_TARGET));
	
	return loader;
}

void ResourceTable::register_loader(std::string_view key, ResourceLoaderFns fns)
{
	loader_fns[key.data()] = fns;
}

ResourceTable::~ResourceTable()
{
	log_info("Clearing resource table");
	for (size_t i = 0; i < entries.size(); ++i) {
		ResourceHandle h = (uint32_t)i + 1;
		destroy_handle(h);
	}
}

ResourceHandle ResourceTable::create(
	const ResourceAllocFns *vtbl, 
	uint32_t type, 
	const char *key
)
{
	ResourceHandle h = RESOURCE_HANDLE_NULL;
	ResourceEntry *ent = nullptr;

	if (!vtbl) {
		const char * s = key ? key : "Unknown";
		log_error(
			"Attempting to create resource \"%s\" with empty virtual table", 
			s);
		return RESOURCE_HANDLE_NULL;
	}

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

	ent->vtbl = vtbl;
	ent->type = type;

	if (key)
		map[key] = h;

	assert(ent);

	return h;
}

void ResourceTable::destroy_handle(ResourceHandle h)
{
	// TODO : When the time comes, push deletions to queue to be processed
	// at the end of every frame

	ResourceEntry *ent = get_internal(h);

	if (!ent) 
		return;

	// TODO : lol...
	assert(!ent->refs.load());

	if (ent->data)
		ent->vtbl->destroy(this, ent->data);

	ent->data = nullptr;
	ent->status = RESOURCE_STATUS_EMPTY;
	ent->type = RESOURCE_TYPE_NONE;

	if (ent->reload_info) {
		log_info("Deleted resource at location %s",ent->reload_info->path.c_str());
		ent->reload_info.reset(nullptr);
	} else {
		log_info("Deleted resource with id %d (%s)",h,
		   registered_types[ent->type].c_str());
	}

	std::unique_lock<std::shared_mutex> lock(mut);
	free_slots.push(h);
}

ResourceHandle ResourceTable::find(std::string_view key) 
{
	std::shared_lock<std::shared_mutex> lock(mut);
	auto it = map.find(key.data());

	if (it == map.end()) {
		return 0;
	}

	return it->second;
}

const ResourceEntry *ResourceTable::get(ResourceHandle h) 
{
	if (h == RESOURCE_HANDLE_NULL || h > entries.size()) 
		return nullptr;

	ResourceEntry *ent = entries[h - 1].get(); 

	if (ent->type == RESOURCE_TYPE_NONE)
		return nullptr;

	return ent;
}

ResourceEntry *ResourceTable::get_internal(ResourceHandle h) 
{
	return const_cast<ResourceEntry*>(get(h));
}

void ResourceTable::set_handle_key(ResourceHandle h, std::string_view key)
{
	std::unique_lock<std::shared_mutex> lock(mut);
	map[key.data()] = h;
}

LoadResult ResourceTable::load_file(ResourceHandle h, const char *path) 
{
	ResourceEntry *ent = entries[h - 1].get();
	assert(ent);

	std::string realpath = make_path_abs(path);

	OnResourceCreateFromDisk load_file_fn = ent->vtbl->load_file;
	LoadResult result = load_file_fn(this, h, realpath.c_str());

	if (!ent->reload_info) {
		ent->reload_info.reset(new ResourceReloadInfo{});
		ent->reload_info->path = path;
	}

	return result;
}

LoadResult ResourceTable::allocate(ResourceHandle h, void* alloc_info)
{
	ResourceEntry *ent = entries[h - 1].get();

	assert(ent);

	OnResourceCreate alloc_fn = ent->vtbl->create; 
	assert(alloc_fn);

	void* data = nullptr;
	LoadResult result = alloc_fn(this, &data, alloc_info);

	if (result != RT_OK) {
		goto error_cleanup;
	}

	// TODO : erase old data in a free list.
	if (ent->data) {
		OnResourceDestroy destroy_fn = ent->vtbl->destroy;
		assert(destroy_fn);

		destroy_fn(this,ent->data);
	}

	ent->status = RESOURCE_STATUS_READY;
	ent->data = data;

	return result;

error_cleanup:
	destroy_handle(h);
	return result;
}

LoadResult ResourceTable::upload(ResourceHandle h, std::string_view key, void* upload_info) 
{
	ResourceEntry *ent = get_internal(h);

	if (!ent->data || ent->status == RESOURCE_STATUS_EMPTY) {
		return RT_EUNKNOWN;
	}

	LoadResult result = RT_OK;

	auto it_loader = loader_fns.find(key.data());
	if (it_loader == loader_fns.end()) {
		log_error("No loader registered with name %s", key.data());
		return RT_EUNKNOWN;
	}

	OnResourceLoad load_fn = it_loader->second.loader_fn; 
	assert(load_fn);

	result = load_fn(this, ent->data, upload_info);

	if (result != RT_OK) {
		ent->status = RESOURCE_STATUS_INVALID;
		return result;
	}

	ent->status = RESOURCE_STATUS_READY;

	return result;
}
