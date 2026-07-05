#ifndef EV2_POOL_H
#define EV2_POOL_H

#include <ev2/utils/log.h>
#include <ev2/defines.h>
#include "utils/common.h"

#include <vector>
#include <mutex>
#include <cassert>
#include <memory>

#include <cstring>
#include <cstdint>

#define MAKE_POOL_ID_CONVERSION(Type)\
static constexpr PoolID to_pool_id(Type##ID id) {\
	return PoolID{.slot = id.id, .gen = id.gen};\
}

struct PoolID
{
	uint32_t slot;
	uint16_t gen;

	static PoolID create(uint32_t id, uint16_t gen) {
		return PoolID{
			.slot = id,
			.gen = gen
		};
	}
};

template<typename T>
struct Pool {
	static constexpr uint32_t PageSizeBits = 6;
	static constexpr uint32_t PageMask = (1 << PageSizeBits) - 1;
	static constexpr uint32_t PAGE_SIZE = 1 << PageSizeBits;

	struct entry_t {
		T val;
		uint16_t generation;
	};

	static Pool<T> *create();

	std::vector<std::unique_ptr<entry_t[]>> pages;
	std::vector<uint32_t> free_list;

	size_t cap = 0;

	// TODO: Eventually want to not use a mutex and have pages of 
	// atomic counters
	mutable std::mutex sync;

	PoolID allocate(T&& val);
	void deallocate(PoolID id);
	T* get_checked(PoolID id) const;
	T* get_unchecked(PoolID id) const;

	entry_t *get_entry_checked(PoolID id) const {
		uint32_t slot = id.slot;

		if (!slot || slot > cap) {
			return nullptr;
		}

		entry_t *ent = &pages[(slot - 1) >> PageSizeBits][(slot - 1) & PageMask]; 

		if (ent->generation != id.gen) {
			log_error("generation counter mismatch: %d != %d", id.gen, ent->generation);
			return nullptr;
		}

		return ent;
	}
};


//------------------------------------------------------------------------------
// Template implementation

template<typename T>
Pool<T> *Pool<T>::create()
{
	return new Pool<T>();
}

template<typename T>
PoolID Pool<T>::allocate(T&& val)
{
	std::unique_lock<std::mutex> lock(sync);
	uint32_t slot = 0;

	if (!free_list.empty()) {
		slot = free_list.back();
		free_list.pop_back();
	} else {
		slot = static_cast<uint32_t>(++cap);

		if (slot > static_cast<uint32_t>(pages.size()) * PAGE_SIZE)
			pages.emplace_back(new entry_t[PAGE_SIZE]);
	}
	lock.unlock();

	entry_t *ent = &pages[(slot - 1) >> PageSizeBits][(slot - 1) & PageMask]; 
	ent->val = std::move(val); 
	uint16_t generation = ent->generation;

	assert(slot);

	return PoolID::create(slot, generation);
}

template<typename T>
void Pool<T>::deallocate(PoolID id)
{
	entry_t *ent = get_entry_checked(id);

	ent->val = T{};
	++ent->generation;

	std::unique_lock<std::mutex> lock(sync);
	free_list.push_back(id.slot);
}

template<typename T>
T *Pool<T>::get_checked(PoolID id) const
{
	entry_t *ent = get_entry_checked(id);
	return ent ? &ent->val : nullptr;
}

template<typename T>
T* Pool<T>::get_unchecked(PoolID id) const
{
	entry_t *ent = &pages[(id.slot - 1) >> PageSizeBits][(id.slot - 1) & PageMask]; 
	return &ent->val;
}

#endif // EV2_POOL_H
