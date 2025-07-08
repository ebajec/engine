#ifndef RESOURCE_LOADER_H
#define RESOURCE_LOADER_H

#include "def_gl.h"

#include <utils/monitor.h>

#include <glad/glad.h>

#include <stdint.h>

#include <unordered_set>
#include <memory>
#include <stack>
#include <atomic>
#include <shared_mutex>
#include <cstring>
#include <unordered_map>
#include <string_view>

#define RESOURCE_ENABLE_HOT_RELOAD 1

#define RESOURCE_HANDLE_NULL 0

struct ResourceLoader;

// TODO : callback in debug mode when a function hits an error? 
// 'setResult'??
enum LoadResult : int32_t
{
	ERROR_INVALID_HANDLE = -2,
	RESULT_ERROR = -1,
	RESULT_SUCCESS = 0
};

enum ResourceType :uint8_t
{
	RESOURCE_TYPE_NONE,
	RESOURCE_TYPE_MATERIAL,
	RESOURCE_TYPE_MODEL,
	RESOURCE_TYPE_IMAGE,
	RESOURCE_TYPE_SHADER,
	RESOURCE_TYPE_MAX_ENUM
};

enum ResourceLoaderType
{
	RESOURCE_LOADER_IMAGE_MEMORY,
	RESOURCE_LOADER_MODEL_2D,
	RESOURCE_LOADER_MODEL_3D,
	RESOURCE_LOADER_MAX_ENUM
};

enum ResourceStatus : int8_t
{
	RESOURCE_STATUS_INVALID = -1,
	RESOURCE_STATUS_EMPTY = 0,
	RESOURCE_STATUS_READY = 1,
	RESOURCE_STATUS_LOADING = 2,
};
typedef uint32_t ResourceHandle;

typedef LoadResult(*OnResourceCreate)(ResourceLoader* loader, void** res, void* info);
typedef void(*OnResourceDestroy)(ResourceLoader* loader, void* res);

typedef LoadResult(*OnResourceCreateFromDisk)(ResourceLoader *loader, ResourceHandle h, const char *path); 
typedef LoadResult(*OnResourceLoad)(ResourceLoader* loader, void* res, void* info);
typedef LoadResult(*OnResourcePostLoad)(ResourceLoader* loader, void* res, void* info);

//------------------------------------------------------------------------------
// Virtual Tables

struct ResourceFns
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
// Resource loader

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

struct ResourceLoaderCreateInfo
{
	const char *resource_path;
};

struct ResourceLoader
{
	std::shared_mutex mut;
	std::string resource_path;

	// TODO: Allocate entries in larger blocks instead of like this
	std::vector<std::unique_ptr<ResourceEntry>> entries;

	// TODO: synchronization on this
	std::stack<ResourceHandle> free_slots;

	std::unordered_map<std::string,uint32_t> map;

	struct {
		std::array<ResourceFns, RESOURCE_TYPE_MAX_ENUM> alloc_fns; 
		std::array<ResourceLoaderFns, RESOURCE_LOADER_MAX_ENUM> loader_fns;
	} pfns;

	//-----------------------------------------------------------------------------

	static std::unique_ptr<ResourceLoader> create(const ResourceLoaderCreateInfo *info); 
	~ResourceLoader();

	ResourceHandle create_handle(ResourceType type);
	void destroy_handle(ResourceHandle h);

	LoadResult load_file(ResourceHandle h, const char *path);
	LoadResult allocate(ResourceHandle h, void* alloc_info);

	LoadResult upload(ResourceHandle h, ResourceLoaderType loader, void* upload_info);

	LoadResult reload(ResourceHandle h);

	ResourceHandle find(std::string_view key);
	const ResourceEntry *get(ResourceHandle h);

	ResourceEntry *get_internal(ResourceHandle h);

	void set_handle_key(ResourceHandle h, std::string_view key);
	std::string make_path_abs(std::string_view str);
};

struct ResourceUpdateInfo
{
	ResourceType type;
	std::string path;
};

struct ResourceHotReloader
{
	std::shared_ptr<ResourceLoader> loader;
	std::unique_ptr<utils::monitor> monitor;

	std::mutex mut;
	std::vector<ResourceUpdateInfo> updates;

	static std::unique_ptr<ResourceHotReloader> create(std::shared_ptr<ResourceLoader> loader);
	LoadResult process_updates();
};

#endif
