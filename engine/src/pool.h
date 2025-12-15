#ifndef EV2_POOL_H
#define EV2_POOL_H

#include <vector>
#include <mutex>
#include <array>
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

template<typename T>
struct ResourcePool {
	static constexpr uint32_t PAGE_SIZE_BITS = 6;
	static constexpr uint32_t PAGE_MASK = (1 << PAGE_SIZE_BITS) - 1;
	static constexpr uint32_t PAGE_SIZE = 1 << PAGE_SIZE_BITS;

	struct entry_t {
		T val;
		std::atomic_uint32_t gen;
	};

	static ResourcePool<T> *create();

	std::vector<std::unique_ptr<entry_t[]>> pages;
	std::vector<uint32_t> free_list;

	// TODO: Eventually want to not use a mutex and have pages of 
	// atomic counters
	mutable std::mutex sync;

	ResourceID allocate(T* ptr = nullptr);
	void deallocate(ResourceID id);
	T* get(ResourceID id) const;
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
		entry_t *page = new entry_t[PAGE_SIZE];

		uint32_t start = static_cast<uint32_t>(pages.size()) * PAGE_SIZE;

		// leave room for the one we're allocating
		for (uint32_t i = 1; i < PAGE_SIZE; ++i) {
			free_list.push_back(start + i);
		}
		pages.emplace_back(page);

		slot = start;
	}
	lock.unlock();

	entry_t *ent = &pages[slot >> PAGE_SIZE_BITS][slot & PAGE_MASK];

	if (ptr)
		ent->val = *ptr; 

	return ResourceID::create(slot, gen);
}

template<typename T>
void ResourcePool<T>::deallocate(ResourceID id)
{
	uint32_t slot = id.slot();
	uint32_t gen = id.gen();

	entry_t *ent = &pages[slot >> PAGE_SIZE_BITS][slot & PAGE_MASK];

	if (ent->gen++ != gen)
		return;

	memset(&ent->val, 0x0, sizeof(T));

	std::unique_lock<std::mutex> lock(sync);
	free_list.push_back(slot);
}

template<typename T>
T *ResourcePool<T>::get(ResourceID id) const
{
	uint32_t slot = id.slot();
	uint32_t gen = id.gen();

	entry_t *ent = &pages[slot >> PAGE_SIZE_BITS][slot & PAGE_MASK];

	return (ent->gen == gen) ? &ent->val : nullptr;
}


#endif // EV2_POOL_H
