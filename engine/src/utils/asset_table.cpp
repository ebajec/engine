#include "ev2/defines.h"
#include "ev2/context.h"

#include "utils/asset_table.h"
#include "backends/vulkan/context.h"

#include "ev2/utils/ansi_colors.h"

#include <algorithm>
#include <numeric>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <format>

#include <cstring>
#include <cassert>
#include <fstream>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Vfs

std::string VfsMount::resolve_vfs_path(std::string_view rel_path) const
{
	return std::format("{}://{}", name, rel_path);
}

std::string VfsMount::resolve_sys_path(std::string_view path) const
{
	if (type != MOUNT_TYPE_FILESYSTEM) {
		std::string tmp(path);
		log_error("Attempting to resolve system path for %s under non-filesystem mount %s",
			tmp.c_str(), name.c_str());
		return {};
	}

	std::string_view rel_path = get_relative(path); 

	if (rel_path.empty())
		return {};

	fs::path root_path (system_path);
	fs::path child_path = fs::path(rel_path).relative_path();
	fs::path res = root_path / child_path;
	return res.string();
}

std::string_view VfsMount::get_relative(std::string_view path) const
{
	if (!matches_mount(name, path)) {
		std::string tmp(path);
		log_error("Vfs path %s is not a valid path under mount %s", tmp.c_str(), name.c_str());
		return {};
	}

	return path.substr(name.length() + sizeof("://") - 1);
}

static void monitor_callback(void *usr, utils::monitor_event_t event)
{
	VfsMonitor *monitor = static_cast<VfsMonitor*>(usr);

	const char* fullpath = event.path;

	fs::path relpath;
	try {
		relpath = fs::relative(fullpath, monitor->mount->system_path);
	} catch (std::exception e) {
		log_error("%s", e.what());
		return;
	}

	std::string vfs_path = monitor->mount->resolve_vfs_path(relpath.generic_string());

	std::unique_lock<std::mutex> lock(monitor->mut);
	if (event.flags & (MONITOR_FLAGS_MODIFY | MONITOR_FLAGS_CREATE))
		monitor->queue.push_back(vfs_path);
}

int VfsMonitor::flush(std::vector<std::string> &out)
{
	std::vector<std::string> updates; 
	
	std::unique_lock<std::mutex> lock(mut);
	updates = std::move(queue);
	lock.unlock();

	int count = (int)updates.size();
	for (std::string & path : updates)
	{
		out.push_back(std::move(path));
	}

	return count;
}

VfsMonitor *VfsMonitor::create(const VfsMount *mount)
{
	if (mount->type != MOUNT_TYPE_FILESYSTEM) {
		log_error("A monitor can only be created for filesystem mounts");
		return nullptr;
	}

	VfsMonitor *monitor = new VfsMonitor{
		.mount = mount,
	};

	monitor->monitor.reset(new utils::FileMonitor(
		monitor_callback, 
		monitor, 
		mount->system_path.c_str()
	));

	return monitor;
}

int Vfs::flush_updates(std::vector<std::string> &out)
{
	int count = 0;
	for (std::unique_ptr<VfsMonitor> &monitor : monitors)
	{
		count += monitor->flush(out);
	}
	return count;
}

Vfs *Vfs::create()
{
	return new Vfs{};
}

VfsMount *Vfs::add_mount(std::string_view name, std::string_view path, MountType type, bool monitor)
{
	std::string path_str = path.empty() ? "[undefined]" : std::string(path);
	std::string name_str = name.empty() ? "[undefined]" : std::string(name);

	if (mounts.contains(name)) {
		log_warn("Failed to mount: " ANSI_YELLOW(%s) ":%s. A mount named '%s' already exists",
			name_str.c_str(), path_str.c_str(), name_str.c_str());
		return nullptr;
	}

	if (type == MOUNT_TYPE_FILESYSTEM) {
		std::error_code code;
		if (!fs::exists(path, code)) {
			log_warn("Failed to mount: " ANSI_YELLOW(%s) ":%s. (filesystem::exists exited with code %d, %s)",
				name_str.c_str(), path_str.c_str(), code.value(), code.message().c_str());
			return nullptr;
		}
	}

	auto [it, inserted] = mounts.emplace(name, VfsMount{
		.name = name_str,
		.system_path = path_str,
		.type = type
	});

	VfsMount *mount = &it->second;

	if (monitor) {
		VfsMonitor *monitor = VfsMonitor::create(&it->second);

		if (monitor) {
			monitors.push_back(
				std::unique_ptr<VfsMonitor>(monitor)
			);
		} else {
			log_error("Failed to create monitor for mount '%s' at '%s'", 
				name_str.c_str(), path_str.c_str());
			return nullptr;
		}
	}

	log_info(
		"Mounted directory:\n\t" ANSI_YELLOW(%s) ":%s",
		mount->name.c_str(),
		mount->system_path.c_str()
	);

	return mount;
}

const VfsMount *Vfs::find_mount(std::string_view path)
{
	std::string_view mnt_name = get_mount(path);
	if (mnt_name.empty()) {
		std::string path_str (path);
		log_error("%s is ill-formed: must be prefixed by a mount point (e.g., foo://%s)", 
			path_str.c_str(), path_str.c_str());
		return nullptr;
	}

	auto it = mounts.find(mnt_name);

	if (it != mounts.end()) {
		return &it->second;
	}

	std::string mount_list = list_mounts();
	std::string name_str (mnt_name);
	std::string path_str (path);
	log_error("Invalid mount '%s' for %s.\nAvailable mounts:\n%s", 
	name_str.c_str(), path_str.c_str(), mount_list.c_str());
	return nullptr;
}

std::string Vfs::list_mounts()
{
	std::string out;

	int i = 0;
	for (const auto &[name, mount] : mounts) {
		out += std::format("\t" ANSI_YELLOW({}) ":{}", name, mount.system_path);

		if (++i < mounts.size())
			out += "\n";
	}

	return out;
}

ev2::Result Vfs::read_all(std::string_view path, VfsAllocFunc &&allocator)
{
	const VfsMount *mnt = find_mount(path);

	if (!mnt) {
		return ev2::EBAD_PATH;
	}

	switch(mnt->type) {
		case MOUNT_TYPE_FILESYSTEM: {
			std::string sys_path = mnt->resolve_sys_path(path);

			std::error_code ec;
			if (!fs::is_regular_file(sys_path, ec)) {
				return set_error(ev2::ELOAD_FAILED, "Not a regular file: %s", sys_path.c_str());
			}

			std::ifstream file(sys_path, std::ios::binary | std::ios::ate);
			if (!file) {
				return set_error(ev2::ELOAD_FAILED, "Failed to open %s", sys_path.c_str());
			}

			std::streamoff end = file.tellg();
			if (end < 0) {
				return set_error(ev2::ELOAD_FAILED, "Failed to get size of %s", sys_path.c_str());
			}

			size_t size = static_cast<size_t>(end);
			file.seekg(0, std::ios::beg);

			unsigned char *bytes = allocator(size);

			if (!bytes && size > 0) {
				return set_error(ev2::ELOAD_FAILED, "Allocation rejected for %s (%zu bytes)", sys_path.c_str(), size);
			}

			file.read(reinterpret_cast<char*>(bytes), static_cast<std::streamsize>(size));

			if (file.gcount() != static_cast<std::streamsize>(size)) {
				return set_error(ev2::ELOAD_FAILED, "File changed while reading %s", sys_path.c_str());
			}

			return ev2::SUCCESS;
		}
		default: {
			log_error("Unimplemented");
			return ev2::ELOAD_FAILED;
		}
	}
}

AssetTable *AssetTable::create(ev2::GfxContext *ctx)
{
	std::unique_ptr<AssetTable> tbl (new AssetTable{});
	tbl->ctx = ctx;

	log_info(ANSI_GREEN(Initialized asset table));
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
		ent->vtbl->destroy(ctx, ent->usr); 

	char * path = ent->path;

	ent->usr = nullptr;
	ent->status = ASSET_STATUS_EMPTY;
	ent->path = nullptr;

	++ent->gen;

	auto it = fwd_graph.find(id);
	if (it != fwd_graph.end()) {
		fwd_graph.erase(it);
	}

	it = bkwd_graph.find(id); 
	if (it != bkwd_graph.end()) {
		for (AssetID parent : it->second)  {
			auto parent_it = bkwd_graph.find(parent);

			if (parent_it != bkwd_graph.end())		
				parent_it->second.erase(id);
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

AssetID AssetTable::find(const char *path) const
{
	std::shared_lock<std::shared_mutex> lock(mut);
	auto it = map.find(path);

	if (it == map.end()) {
		return ASSET_ID_NULL;
	}

	return it->second;
}

ev2::Result AssetTable::load(const char *path, AssetID *out)
{
	const VfsMount *mnt = ctx->vfs->find_mount(path);

	if (!mnt) {
		return ev2::EBAD_PATH;
	}

	std::shared_lock<std::shared_mutex> lock(mut);
	auto it = map.find(path);

	if (it == map.end()) {
		if (out)
			*out = ASSET_ID_NULL;

		return ev2::SUCCESS;
	}
	AssetID id = it->second;

	if (out)
		*out = id;

	return ev2::SUCCESS;
}

ev2::Result AssetTable::reload(AssetID id)
{
	AssetEntry *ent = entries[id - 1].get();

	ev2::Result res = ent->vtbl->reload(ctx, &ent->usr, ent->path);

	if (res == ev2::SUCCESS) {
		log_info("Reloaded asset %s", ent->path);
	} else {
		log_error("Failed to reload asset %s", ent->path);
	}

	return res;
}

//------------------------------------------------------------------------------
// Reloading

void AssetTable::add_dependency(AssetID parent, AssetID child)
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

ev2::Result AssetTable::process_reloads()
{
	std::vector<std::string> updates;
	ctx->vfs->flush_updates(updates);

	ev2::Result res = ev2::SUCCESS;

	if (!updates.empty()) {
		ctx->wait_for_frame_completion(ctx->frame_counter - 1);
	}

	for (const std::string &key : updates) {
		AssetID id = find(key.c_str());

		if (id == ASSET_ID_NULL)
			continue;

		ev2::Result tmp = reload(id);

		if (tmp == ev2::SUCCESS) {
			if (auto it = fwd_graph.find(id); it != fwd_graph.end()) {
				for (AssetID dep : it->second) {
					tmp = reload(dep);
				}
			}
		} else {
			res = tmp;
		} 
	}

	return res;
}
