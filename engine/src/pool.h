#ifndef EV2_POOL_H
#define EV2_POOL_H

#include <utils/log.h>

#include <vector>
#include <mutex>
#include <array>
#include <cassert>
#include <memory>
#include <atomic>

#include <cstring>
#include <cstdint>

struct ResourceID
{
	uint64_t u64;

	static ResourceID create(uint32_t id, uint32_t gen) {
		return ResourceID{
			.u64 = (((uint64_t)id) | (((uint64_t)gen) << 32))
		};
	}
	uint32_t slot() const {return static_cast<uint32_t>(u64);} 
	uint32_t gen() const {return static_cast<uint32_t>(u64 >> 32);} 
};

struct AtomicResourceState {
	typedef uint64_t value_t;

	std::atomic_uint64_t value;

	value_t ref() {
		value_t expected = value.load(std::memory_order_relaxed);
		value_t desired;
		do {
			uint32_t refs = (uint32_t)expected;
			desired = (expected & (0xFFFFFFFFLLU << 32)) | (uint64_t)(refs + 1);
		} while (!value.compare_exchange_weak(expected, desired, 
			std::memory_order_acq_rel, std::memory_order_relaxed));

		return expected;
	}

	value_t deref() {
		value_t expected = value.load(std::memory_order_relaxed);
		value_t desired;
		do {
			uint32_t refs = (uint32_t)expected;
			desired = (expected & (0xFFFFFFFFLLU << 32)) | (uint64_t)(refs - 1);
		} while (!value.compare_exchange_weak(expected, desired, 
			std::memory_order_acq_rel, std::memory_order_relaxed));

		return expected;
	}

	value_t inc_gen() {
		value_t expected = value.load(std::memory_order_relaxed);
		value_t desired;
		do {
			uint32_t gen = (uint32_t)(expected >> 32);
			desired = (((uint64_t)gen) << 32) | (expected & (0xFFFFFFFFLLU));
		} while (!value.compare_exchange_weak(expected, desired, 
			std::memory_order_acq_rel, std::memory_order_relaxed));

		return expected;
	}
};

template<typename T>
struct ResourcePool {
	static constexpr uint32_t PAGE_SIZE_BITS = 6;
	static constexpr uint32_t PAGE_MASK = (1 << PAGE_SIZE_BITS) - 1;
	static constexpr uint32_t PAGE_SIZE = 1 << PAGE_SIZE_BITS;

	struct entry_t {
		T val;
		AtomicResourceState state;
	};

	static uint32_t get_gen(uint64_t state) {
		return (uint32_t)(state >> 32);
	}
	static uint32_t get_refs(uint64_t state) {
		return (uint32_t)(state >> 32);
	}

	static ResourcePool<T> *create();

	std::vector<std::unique_ptr<entry_t[]>> pages;
	std::vector<uint32_t> free_list;

	size_t cap = 0;

	// TODO: Eventually want to not use a mutex and have pages of 
	// atomic counters
	mutable std::mutex sync;

	ResourceID allocate(T* ptr = nullptr);
	void deallocate(ResourceID id);
	T* get(ResourceID id) const;

	bool check_handle(ResourceID id) const {
		uint32_t slot = id.slot();
		if (!slot || slot > cap) {
			log_error("Invalid resource handle passed : %d", slot);
			throw std::runtime_error("Invalid resource handle passed");
			return false;
		}
		return true;
	}

	entry_t *get_entry(uint32_t slot) const {
		--slot;
		return &pages[slot >> PAGE_SIZE_BITS][slot & PAGE_MASK];
	}
};

//------------------------------------------------------------------------------
// Template implementation

template<typename T>
ResourcePool<T> *ResourcePool<T>::create()
{
	return new ResourcePool<T>();
}

template<typename T>
ResourceID ResourcePool<T>::allocate(T* ptr)
{
	std::unique_lock<std::mutex> lock(sync);
	uint32_t slot = 0;
	uint32_t gen = 0;

	if (!free_list.empty()) {
		slot = free_list.back();
		free_list.pop_back();
	} else {
		slot = static_cast<uint32_t>(++cap);

		if (slot > static_cast<uint32_t>(pages.size()) * PAGE_SIZE)
			pages.emplace_back(new entry_t[PAGE_SIZE]);
	}
	lock.unlock();

	entry_t *ent = get_entry(slot);

	if (ptr)
		ent->val = *ptr; 

	assert(slot);

	return ResourceID::create(slot, gen);
}

template<typename T>
void ResourcePool<T>::deallocate(ResourceID id)
{
	if (!check_handle(id)) {
		return;
	}

	uint32_t slot = id.slot();
	uint32_t gen = id.gen();

	entry_t *ent = get_entry(slot);
	uint64_t state = ent->state.inc_gen();

	if (get_gen(state) != gen)
		return;

	memset(&ent->val, 0x0, sizeof(T));

	std::unique_lock<std::mutex> lock(sync);
	free_list.push_back(slot);
}

template<typename T>
T *ResourcePool<T>::get(ResourceID id) const
{
	if (!check_handle(id)) {
		return nullptr;
	}

	uint32_t slot = id.slot();
	uint32_t gen = id.gen();

	entry_t *ent = get_entry(slot);

	uint64_t state = ent->state.value.load(std::memory_order_acquire);

	return (get_gen(state) == gen) ? &ent->val : nullptr;
}


#endif // EV2_POOL_H
