#ifndef EV2_ASSET_TABLE_H
#define EV2_ASSET_TABLE_H

#include <ev2/context.h>
#include <ev2/utils/monitor.h>
#include <ev2/utils/log.h>

#include <robin_hood.h>

#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stack>
#include <atomic>
#include <cstdint>

typedef uint32_t AssetID;
static constexpr AssetID ASSET_ID_NULL = 0;

enum MountType
{
	MOUNT_TYPE_FILESYSTEM,
};

static inline bool matches_mount(std::string_view mount, std::string_view path)
{
	return 
		path.starts_with(mount) && 
		path.substr(mount.length(), std::string::npos).starts_with("://");
}

static inline std::string_view get_mount(std::string_view path)
{
	if (path.empty())
		return {};

	size_t pos = path.find_first_of(":");

	if (pos == std::string::npos) {
		return {};
	}

	return path.substr(0, pos);
}

struct VfsStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const char* s) const { return std::hash<std::string_view>{}(s); }
};

struct VfsMount
{
	std::string name;
	std::string system_path;
	MountType type;

	// @brief Resolve vfs path of a path relative to this mount.
	std::string resolve_vfs_path(std::string_view rel_path) const;
	
	// @brief Resolve system path for a vfs path under this mount,
	// otherwise return an empty string.
	std::string resolve_sys_path(std::string_view path) const;

	std::string_view get_relative(std::string_view path) const;
};

struct VfsMonitor
{
	const VfsMount *mount;
	std::unique_ptr<utils::FileMonitor> monitor;

	// Contains vfs paths owned by mount
	std::vector<std::string> queue;
	mutable std::mutex mut;

	int flush(std::vector<std::string> &out);
	static VfsMonitor *create(const VfsMount *mount);
};

typedef std::function<unsigned char*(size_t size)> VfsAllocFunc;

struct Vfs
{
	std::unordered_map<std::string, VfsMount, VfsStringHash, std::equal_to<>> mounts;

	std::vector<std::unique_ptr<VfsMonitor>> monitors;

	static Vfs *create();

	VfsMount *add_mount(std::string_view name, std::string_view path, MountType type, bool monitor = false);
	const VfsMount *find_mount(std::string_view path);

	// @brief Read entire contents of the data at path and write the output into
	// the pointer returned by allocator
	//
	// @note allocator must return nullptr on failure
	//
	// @note This may fail if the file changes during the read, in which case the allocated
	// memory will not be freed in this call.  The caller is responsible for the lifetime 
	// of the allocation made by this call.
	ev2::Result read_all(std::string_view path, VfsAllocFunc &&allocator);

	int flush_updates(std::vector<std::string> &out);

	std::string list_mounts();
};

//------------------------------------------------------------------------------
// Table

enum AssetStatus : uint8_t
{
	ASSET_STATUS_EMPTY,
	ASSET_STATUS_LOADING,
	ASSET_STATUS_READY,
};

struct AssetVTable
{
	ev2::Result (*reload)(ev2::GfxContext *ctx, void** usr, const char *path);
	void (*destroy)(ev2::GfxContext *ctx, void* usr);
};

struct AssetEntry
{
	void *usr;
	const AssetVTable *vtbl;
	char *path;

	// Internal refcount
	std::atomic_uint32_t refs;
	std::atomic_uint16_t gen;
	std::atomic_uint8_t status;
};

struct AssetTable
{
	ev2::GfxContext *ctx;

	// TODO: Allocate entries in larger blocks instead of like this
	std::vector<std::unique_ptr<AssetEntry>> entries;
	std::stack<AssetID> free_slots;
	std::unordered_map<std::string, AssetID> map;

	// parent -> children
	std::unordered_map<AssetID, std::unordered_set<AssetID>> fwd_graph;
	// child -> parents
	std::unordered_map<AssetID, std::unordered_set<AssetID>> bkwd_graph;

	bool b_reloading_enabled = false;

	mutable std::shared_mutex mut;

	static AssetTable *create(ev2::GfxContext *ctx);
	static void destroy(AssetTable *tbl);

	/// @brief allocate an entry for an initialized asset.  The resulting
	/// entry is inserted with ASSET_STATUS_READY.
	AssetID allocate(const AssetVTable *vtbl, void *usr, 
				  const char *path, const char *msg = nullptr);
	void deallocate(AssetID id);

	// @brief Get unique handle for a resource, or ASSET_ID_NULL if the asset has not
	// been loaded yet. 
	// @return ev2::EBAD_PATH if the path is ill-formed or the mount does not exist
	ev2::Result load(const char *path, AssetID *out);

	ev2::Result reload(AssetID id);

	void add_dependency(AssetID parent, AssetID child);

	// @brief only does anything if reloading is active (i.e., the vfs has
	// any active monitors
	ev2::Result process_reloads();

	AssetID find(const char *path) const;

	AssetEntry *get_entry(AssetID id) {
		if (!id || id > entries.size()) {
			log_error("Invalid asset id: %d", id);
			return nullptr;
		}
		return entries[id - 1].get();
	}

	template<typename T> T *get(AssetID id) const;
};

//------------------------------------------------------------------------------
// Templates

template<typename T> T *AssetTable::get(AssetID id) const
{
	if (!id) {
		return nullptr;
	}

	if (id > entries.size()) 
		return nullptr;

	AssetEntry *ent = entries[id - 1].get(); 

	if (!ent) {
		log_error("Asset does not exist with id %d!",id);
		return nullptr;
	}

	return static_cast<T*>(ent->usr);
}

#endif //EV2_ASSET_TABLE_H
