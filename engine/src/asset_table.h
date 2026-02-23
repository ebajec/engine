#ifndef EV2_ASSET_TABLE_H
#define EV2_ASSET_TABLE_H

#include "ev2/device.h"

#include "utils/monitor.h"
#include "utils/log.h"

#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stack>
#include <atomic>
#include <cstdint>

typedef uint32_t AssetID;
static constexpr AssetID ASSET_ID_NULL = 0;

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
	ev2::Result (*reload)(ev2::Device *dev, void** usr, const char *path);
	void (*destroy)(ev2::Device *dev, void* usr);
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

struct AssetReloader;

struct AssetTable
{
	ev2::Device *dev;

	std::unique_ptr<AssetReloader> reloader;

	std::string root;

	// TODO: Allocate entries in larger blocks instead of like this
	std::vector<std::unique_ptr<AssetEntry>> entries;
	std::stack<AssetID> free_slots;
	std::unordered_map<std::string, AssetID> map;

	mutable std::shared_mutex mut;

	static AssetTable *create(ev2::Device *dev, const char *root, bool reload = true);
	static void destroy(AssetTable *tbl);

	/// @brief allocate an entry for an initialized asset.  The resulting
	/// entry is inserted with ASSET_STATUS_READY.
	AssetID allocate(const AssetVTable *vtbl, void *usr, 
				  const char *path, const char *msg = nullptr);
	void deallocate(AssetID id);

	// @brief Get unique handle for a resource
	AssetID load(const char *path);

	ev2::Result reload(AssetID id);

	std::string get_system_path(const char *path);

	AssetID find(const char *path) const;

	AssetEntry *get_entry(AssetID id) {
		if (!id) {
			log_error("Invalid asset id %d (this is very bad)",id);
			return nullptr;
		}
		return entries[id - 1].get();
	}

	template<typename T> T *get(AssetID id) const;
};

//------------------------------------------------------------------------------
// Reloading

struct AssetReloader
{
	AssetTable *tbl;
	std::unique_ptr<utils::monitor> monitor;

	// parent -> children
	std::unordered_map<AssetID, std::unordered_set<AssetID>> fwd_graph;
	// child -> parents
	std::unordered_map<AssetID, std::unordered_set<AssetID>> bkwd_graph;

	// update queue holds paths relative to the root of the table
	std::vector<std::string> queue;
	mutable std::mutex mut;

	static AssetReloader *create(AssetTable *tbl);
	void add_dependency(AssetID parent, AssetID child);
	ev2::Result update();
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
