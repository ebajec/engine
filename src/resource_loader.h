#ifndef RESOURCE_LOADER_H
#define RESOURCE_LOADER_H

#include "def_gl.h"

#include <utils/monitor.h>

#include <glad/glad.h>

#include <stdint.h>

#include <memory>
#include <atomic>
#include <shared_mutex>
#include <cstring>
#include <unordered_map>
#include <string_view>

#define RESOURCE_ENABLE_HOT_RELOAD 1

#define RESOURCE_HANDLE_NULL 0

struct ResourceLoader;

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

enum ResourceStatus : int8_t
{
	RESOURCE_STATUS_INVALID = -1,
	RESOURCE_STATUS_EMPTY = 0,
	RESOURCE_STATUS_READY = 1,
	RESOURCE_STATUS_LOADING = 2,
};
typedef uint32_t Handle;

typedef LoadResult(*OnResourceCreate)(ResourceLoader* loader, void** res, void* info);
typedef void(*OnResourceDestroy)(ResourceLoader* loader, void* res);

typedef void*(*OnResourceLoadDesc)(void* desc);
typedef void(*OnResourceDestroyDesc)(void* desc);

typedef LoadResult(*OnResourcePostLoad)(ResourceLoader* loader, void* res, void* info);

struct FileDesc
{
	std::string path;

	bool operator == (const FileDesc& other) const {
		return other.path == path;
	}
};

struct ResourceReloadInfo
{
	std::mutex mut;
	std::vector<Handle> subscribers;
	void *p_create_info;

	void add_subscriber(Handle h) {
		std::lock_guard<std::mutex> lock(mut);
		subscribers.push_back(h);
	}

	virtual ~ResourceReloadInfo() {}
};

struct ResourceFns
{
	OnResourceCreate create_fn;
	OnResourceDestroy destroy_fn;

#if RESOURCE_ENABLE_HOT_RELOAD
	ResourceReloadInfo *(*create_reload_info_fn)(void *info);
#endif
};

struct ResourceEntry 
{
	void *data;

	std::atomic_int refs;
	std::atomic<ResourceStatus> status;

	ResourceType type;

#if RESOURCE_ENABLE_HOT_RELOAD
	std::unique_ptr<ResourceReloadInfo> reload_info;
#endif
};

struct ResourceLoaderCreateInfo
{
	const char *resource_path;
};

struct ResourceWatcher;

struct ResourceLoader
{
	std::shared_mutex mut;
	std::string resource_path;

	std::vector<std::unique_ptr<ResourceEntry>> entries;
	std::unordered_map<std::string,uint32_t> map;
	std::array<ResourceFns, RESOURCE_TYPE_MAX_ENUM> pfns;

	static std::unique_ptr<ResourceLoader> create(const ResourceLoaderCreateInfo *info); 

	Handle create_handle(ResourceType type);
	void destroy_handle(Handle h) {}

	Handle find(std::string_view key);
	const ResourceEntry *get(Handle h);
	ResourceEntry *get_internal(Handle h);

	void set_handle_key(Handle h, std::string_view key);
	std::string make_path_abs(std::string_view str);
};

extern LoadResult resource_load(ResourceLoader *loader, Handle h, void *info);
extern LoadResult resource_reload(ResourceLoader *loader, Handle h);

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
