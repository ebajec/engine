#include "ev2/defines.h"
#include "ev2/device.h"

#include "asset_table.h"

#include <algorithm>
#include <numeric>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <filesystem>

#include <cstring>
#include <cassert>

namespace fs = std::filesystem;

AssetTable *AssetTable::create(ev2::Device *dev, const char *root, bool reload)
{
	std::unique_ptr<AssetTable> tbl (new AssetTable{});
	tbl->dev = dev;
	tbl->root = root;

	if (reload)
		tbl->reloader.reset(AssetReloader::create(tbl.get()));


	log_info("Initialized asset table at %s", root);

	return tbl.release();
}

void AssetTable::destroy(AssetTable *tbl)
{
	for (uint32_t i = 0; i < static_cast<uint32_t>(tbl->entries.size()); ++i) {
		AssetEntry *ent = tbl->entries[i].get();
		if (ent->status != ASSET_STATUS_EMPTY) {
			tbl->deallocate(i + 1);
		}
	}
}


AssetID AssetTable::allocate(
	const AssetVTable *vtbl, 
	void *usr, 
	const char *path, 
	const char *msg
)
{
	std::unique_lock<std::shared_mutex> lock(mut);

	AssetEntry *ent = nullptr;
	AssetID id = ASSET_ID_NULL;

	if (!free_slots.empty()) {
		id = free_slots.top();
		free_slots.pop();

		ent = entries[id - 1].get();
	} else {
	 	id = static_cast<AssetID>(entries.size() + 1);
	 	ent = new AssetEntry{};
		entries.push_back(std::unique_ptr<AssetEntry>(ent));
	}
	lock.unlock();

	ent->vtbl = vtbl;
	ent->usr = usr;
	ent->path = (char*)malloc(strlen(path) + 1); 
	ent->status = ASSET_STATUS_READY;
	++ent->refs;

	strcpy(ent->path, path);

	map[path] = id;

	if (msg) {
		log_info("Loaded asset : %s\n%s", path, msg);
	} else {
		log_info("Loaded asset : %s", path);
	}

	return id;
}

void AssetTable::deallocate(AssetID id)
{
	if (!id || id > entries.size()) {
		return;
	}

	AssetEntry *ent = entries[id - 1].get();

	if (ent->status == ASSET_STATUS_EMPTY) {
		log_error("Double destroy on asset with id %d", id);
		return;
	}

	if (ent->usr)
		ent->vtbl->destroy(dev, ent->usr); 

	char * path = ent->path;

	ent->usr = nullptr;
	ent->status = ASSET_STATUS_EMPTY;
	ent->path = nullptr;

	++ent->gen;

	if (reloader) {
		// TODO: review this
		std::unique_lock<std::mutex> lock(reloader->mut);

		auto it = reloader->fwd_graph.find(id);
		if (it != reloader->fwd_graph.end()) {
			reloader->fwd_graph.erase(it);
		}

		it = reloader->bkwd_graph.find(id); 
		if (it != reloader->bkwd_graph.end()) {
			for (AssetID parent : it->second)  {
				auto parent_it = reloader->bkwd_graph.find(parent);

				if (parent_it != reloader->bkwd_graph.end())		
					parent_it->second.erase(id);
			}
		}
	}

	log_info("Deleted asset %s",path);

	{
		std::unique_lock<std::shared_mutex> lock(mut);
		map.erase(path);
		free_slots.push(id);
	}

	free(path);
}

std::string AssetTable::get_system_path(const char *path)
{
	fs::path root_path (root);
	fs::path child_path (path);
	fs::path res = root_path / child_path; 
	return res.string();
}

AssetID AssetTable::find(const char *path) const
{
	std::shared_lock<std::shared_mutex> lock(mut);
	auto it = map.find(path);

	if (it == map.end()) {
		return ASSET_ID_NULL;
	}

	return it->second;
}

AssetID AssetTable::load(const char *path)
{
	std::shared_lock<std::shared_mutex> lock(mut);
	auto it = map.find(path);

	if (it == map.end()) {
		return ASSET_ID_NULL;
	}
	AssetID id = it->second;

	return id;
}

ev2::Result AssetTable::reload(AssetID id)
{
	AssetEntry *ent = entries[id - 1].get();

	ev2::Result res = ent->vtbl->reload(dev, &ent->usr, ent->path);

	if (res == ev2::SUCCESS) {
		log_info("Reloaded asset %s", ent->path);
	} else {
		log_error("Failed to reload asset %s", ent->path);
	}

	return res;
}

//------------------------------------------------------------------------------
// Reloading

static void monitor_callback(void *usr, utils::monitor_event_t event)
{
	AssetReloader *reloader = static_cast<AssetReloader*>(usr);

	const char* fullpath = event.path;

	fs::path relpath;
	try {
		relpath = fs::relative(fullpath, reloader->tbl->root);
	} catch (std::exception e) {
		log_error("%s", e.what());
		return;
	}

	std::string relpath_s = relpath.string();

	std::unique_lock<std::mutex> lock(reloader->mut);
	if (event.flags & MONITOR_FLAGS_MODIFY)
		reloader->queue.push_back(std::move(relpath_s));
}

AssetReloader *AssetReloader::create(AssetTable *tbl)
{
	std::unique_ptr<AssetReloader> reloader (new AssetReloader{}); 
	reloader->tbl = tbl;
	reloader->monitor.reset(new utils::monitor(
		monitor_callback, 
		reloader.get(), 
		tbl->root.c_str()
	));

	return reloader.release();
}

void AssetReloader::add_dependency(AssetID parent, AssetID child)
{
	if (auto it = fwd_graph.find(child); 
		it != fwd_graph.end() && it->second.contains(parent)) {
		log_error("Cyclic asset dependency detected : %d -> %d");
		return;
	}
	
	if (auto [it, inserted] = fwd_graph.emplace(parent, std::unordered_set<AssetID>{child});
		!inserted
	) {
		it->second.insert(child);
	}

	if (auto [it, inserted] = bkwd_graph.emplace(child, std::unordered_set<AssetID>{parent});
	 	!inserted
	) {
		it->second.insert(parent);
	}
}

ev2::Result AssetReloader::update()
{
	std::vector<std::string> updates; 
	
	std::unique_lock<std::mutex> lock(mut);
	updates = std::move(queue);
	lock.unlock();

	ev2::Result res = ev2::SUCCESS;

	for (const std::string &key : updates) {
		AssetID id = tbl->find(key.c_str());

		if (id == ASSET_ID_NULL)
			continue;

		ev2::Result tmp = tbl->reload(id);

		if (tmp == ev2::SUCCESS) {
			if (auto it = fwd_graph.find(id); it != fwd_graph.end()) {
				for (AssetID dep : it->second) {
					tmp = tbl->reload(dep);
				}
			}
		} else {
			res = tmp;
		} 
	}

	return res;
}
