#ifndef RESOURCE_TABLE_H
#define RESOURCE_TABLE_H

#include "engine/renderer/types.h"
#include <engine/utils/log.h>
#include <engine/utils/monitor.h>

#include <cstdint>
#include <cassert>
#include <unordered_set>
#include <vector>
#include <array>
#include <memory>
#include <stack>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <string_view>
#include <mutex>

#define RESOURCE_ENABLE_HOT_RELOAD 1

#define RESOURCE_HANDLE_NULL 0

#define stringify(arg) #arg

enum ResourceType :uint8_t
{
	RESOURCE_TYPE_NONE,
	RESOURCE_TYPE_MATERIAL,
	RESOURCE_TYPE_COMPUTE_PIPELINE,
	RESOURCE_TYPE_MODEL,
	RESOURCE_TYPE_IMAGE,
	RESOURCE_TYPE_BUFFER,
	RESOURCE_TYPE_SHADER,
	RESOURCE_TYPE_RENDER_TARGET,
	RESOURCE_TYPE_MAX_ENUM
};

static const char* resource_type_strings[RESOURCE_TYPE_MAX_ENUM] =
{
	stringify(RESOURCE_TYPE_NONE),
	stringify(RESOURCE_TYPE_MATERIAL),
	stringify(RESOURCE_TYPE_COMPUTE_PIPELINE),
	stringify(RESOURCE_TYPE_MODEL),
	stringify(RESOURCE_TYPE_IMAGE),
	stringify(RESOURCE_TYPE_BUFFER),
	stringify(RESOURCE_TYPE_SHADER),
	stringify(RESOURCE_TYPE_RENDER_TARGET)
};

enum ResourceLoaderType
{
	RESOURCE_LOADER_IMAGE_MEMORY,
	RESOURCE_LOADER_MODEL_2D,
	RESOURCE_LOADER_MODEL_3D,
	RESOURCE_LOADER_MAX_ENUM
};

struct ResourceTable;

// TODO : callback in debug mode when a function hits an error? 
// 'setResult'??
enum LoadResult : int32_t
{
	RT_EINVALID_HANDLE = -2,
	RT_EUNKNOWN = -1,
	RT_OK = 0
};

enum ResourceStatus : int8_t
{
	RESOURCE_STATUS_INVALID = -1,
	RESOURCE_STATUS_EMPTY = 0,
	RESOURCE_STATUS_READY = 1,
	RESOURCE_STATUS_LOADING = 2,
};
typedef uint32_t ResourceHandle;

typedef LoadResult(*OnResourceCreate)(ResourceTable* table, void** res, void* info);
typedef void(*OnResourceDestroy)(ResourceTable* table, void* res);

typedef LoadResult(*OnResourceCreateFromDisk)(ResourceTable *table, ResourceHandle h, const char *path); 
typedef LoadResult(*OnResourceLoad)(ResourceTable* table, void* res, void* info);
typedef LoadResult(*OnResourcePostLoad)(ResourceTable* table, void* res, void* info);

//------------------------------------------------------------------------------
// Virtual Tables

struct ResourceAllocFns
{
	OnResourceCreate create;
	OnResourceDestroy destroy;
	OnResourceCreateFromDisk load_file;
};

struct ResourceLoaderFns
{
	OnResourceLoad loader_fn;
	OnResourcePostLoad post_load_fn;
};

//------------------------------------------------------------------------------
// Resource table

struct ResourceReloadInfo
{
	std::string path;
	std::mutex mut;
	std::unordered_set<ResourceHandle> subscribers;

	void add_subscriber(ResourceHandle h) {
		std::lock_guard<std::mutex> lock(mut);
		subscribers.insert(h);
	}
	void remove_subscriber(ResourceHandle h) {
		std::lock_guard<std::mutex> lock(mut);
		subscribers.erase(h);
	}
};

struct ResourceEntry 
{
	void *data;

	std::atomic_int refs;
	std::atomic<ResourceStatus> status;
	ResourceType type;

	std::unique_ptr<ResourceReloadInfo> reload_info;
};

struct ResourceTableCreateInfo
{
	const char *resource_path;
};

struct ResourceTable
{
	std::shared_mutex mut;
	std::string resource_path;

	// TODO: Allocate entries in larger blocks instead of like this
	std::vector<std::unique_ptr<ResourceEntry>> entries;

	// TODO: synchronization on this
	std::stack<ResourceHandle> free_slots;

	std::unordered_map<std::string, ResourceHandle> map;

	std::unordered_map<std::string, ResourceLoaderFns> loader_fns;
	std::array<ResourceAllocFns, RESOURCE_TYPE_MAX_ENUM> alloc_fns; 

	//-----------------------------------------------------------------------------

	static std::unique_ptr<ResourceTable> create(const ResourceTableCreateInfo *info); 
	~ResourceTable();

	void register_loader(std::string_view key, ResourceLoaderFns fns);

	ResourceHandle create_handle(ResourceType type);
	void destroy_handle(ResourceHandle h);
	ResourceHandle find(std::string_view key);
	void set_handle_key(ResourceHandle h, std::string_view key);

	LoadResult load_file(ResourceHandle h, const char *path);

	LoadResult allocate(ResourceHandle h, void* alloc_info);
	LoadResult upload(ResourceHandle h, std::string_view key, void* upload_info);

	template<typename T> 
	const T *get(ResourceHandle h)
	{
		if (!h) {
			return nullptr;
		}

		if (h > entries.size()) 
			return nullptr;

		ResourceEntry *ent = entries[h - 1].get(); 

		if (!ent) {
			log_error("Resource does not exist with id %d!",h);
			return nullptr;
		}

		if (ent->type == RESOURCE_TYPE_NONE)
			return nullptr;

		return static_cast<const T*>(ent->data);
	}
	const ResourceEntry *get(ResourceHandle h);
	ResourceEntry *get_internal(ResourceHandle h);

	std::string make_path_abs(std::string_view str);
};

struct ResourceUpdateInfo
{
	ResourceType type;
	std::string path;
};

struct ResourceHotReloader
{
	ResourceTable *table;
	std::unique_ptr<utils::monitor> monitor;

	std::mutex mut;
	std::vector<ResourceUpdateInfo> updates;

	static std::unique_ptr<ResourceHotReloader> create(ResourceTable *table);
	LoadResult process_updates();
};

#endif //RESOURCE_TABLE_H
