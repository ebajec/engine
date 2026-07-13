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

struct alignas(alignof(uintptr_t)) PoolID
{
	uint32_t slot;
	uint16_t gen;

	static PoolID create(uint32_t id, uint16_t gen) {
		return PoolID{
			.slot = id,
			.gen = gen
		};
	}

	constexpr bool operator == (const PoolID other) const {
		return other.slot == slot && other.gen == gen;
	}

	explicit operator uint64_t () const {
		return (uint64_t)slot | ((uint64_t)gen << 32);
	}

	struct Hash {
		inline size_t operator ()(const PoolID id) const {
			return std::hash<uint64_t>{}((uint64_t)id);
		};
	};
};

template<typename T, size_t PageSize = 64, size_t PageAlign = alignof(T)>
struct Pool {
	static_assert(PageSize && is_pow2(PageSize));
	static_assert(PageAlign && is_pow2(PageAlign));
	static_assert(PageAlign <= PageSize);
	static_assert(PageAlign >= alignof(T));

	static constexpr size_t PageSizeValue = PageSize;
	static constexpr size_t PageSizeBytes = PageSize * sizeof(T);
	static constexpr unsigned LogPageSize = std::bit_width(PageSize) - 1;
	static constexpr uint16_t MaxGeneration = 0x7FFF;

	struct EntryRef {
		T *val;
		uint16_t *generation;

		bool is_valid() {return val && generation;}
	};

	struct Page {
		uint16_t generation[PageSize] = {};
		alignas(PageAlign) T values[PageSize];

		constexpr bool in_use(uint32_t idx) const {
			return !(generation[idx] & (~MaxGeneration));
		}
	};

	static void delete_page(Page *page) {
		page->~Page();
		operator delete(page, std::align_val_t(PageAlign));
	}

	typedef std::unique_ptr<Page, decltype(&delete_page)> PagePtr;

	static Pool<T, PageSize, PageAlign> *create();

	std::vector<PagePtr> pages;
	std::vector<uint32_t> free_list;

	size_t cap = 0;

	mutable std::mutex sync;

	PoolID allocate(T&& val);
	void deallocate(PoolID id);
	T* get_checked(PoolID id);
	T* get_unchecked(PoolID id);

	constexpr bool is_valid(PoolID id) {
		return get_entry_checked(id).is_valid();
	}

	constexpr EntryRef entry_at(uint32_t slot) {
		Page *page = pages[(slot - 1) >> LogPageSize].get();

		size_t idx = (slot - 1) & (PageSize - 1);

		return EntryRef{
			.val = &page->values[idx], 
			.generation = &page->generation[idx]
		};
	}

	EntryRef get_entry_checked(PoolID id) {
		uint32_t slot = id.slot;

		if (!slot || slot > cap) {
			return {};
		}

		EntryRef ent = entry_at(slot);

		if (*ent.generation != id.gen) {
			return {};
		}

		return ent;
	}
};


//------------------------------------------------------------------------------
// Template implementation

template<typename T, size_t PageSize, size_t PageAlign>
Pool<T, PageSize, PageAlign> *Pool<T, PageSize, PageAlign>::create()
{
	return new Pool<T>();
}

template<typename T, size_t PageSize, size_t PageAlign>
PoolID Pool<T, PageSize, PageAlign>::allocate(T&& val)
{
	std::unique_lock<std::mutex> lock(sync);
	uint32_t slot = 0;

	if (!free_list.empty()) {
		slot = free_list.back();
		free_list.pop_back();
	} else {
		slot = static_cast<uint32_t>(++cap);

		if (slot > static_cast<uint32_t>(pages.size()) * PageSize) {
			PagePtr ptr = {
				new (std::align_val_t(PageAlign)) Page, delete_page
			};
			pages.emplace_back(std::move(ptr));
		}
	}
	lock.unlock();

	EntryRef ent = entry_at(slot);
	*ent.val = std::move(val); 
	
	// zero the top bit
	*ent.generation &= MaxGeneration;

	assert(slot);

	return PoolID::create(slot, *ent.generation);
}

template<typename T, size_t PageSize, size_t PageAlign>
void Pool<T, PageSize, PageAlign>::deallocate(PoolID id)
{
	EntryRef ent = get_entry_checked(id);

	if (!ent.is_valid()) {
		log_error("Double free attempted in pool %x at position %d", this, id.slot); 
		return;
	}

	*ent.val = T{};
	uint16_t gen = *ent.generation;

	// set top bit to one
	*ent.generation = ((1 + gen) & MaxGeneration) | (~MaxGeneration);

	std::unique_lock<std::mutex> lock(sync);
	free_list.push_back(id.slot);
}

template<typename T, size_t PageSize, size_t PageAlign>
T *Pool<T, PageSize, PageAlign>::get_checked(PoolID id)
{
	EntryRef ent = get_entry_checked(id);
	return ent.val;
}

template<typename T, size_t PageSize, size_t PageAlign>
T* Pool<T, PageSize, PageAlign>::get_unchecked(PoolID id)
{
	EntryRef ent = entry_at(id.slot);
	return ent.val;
}

#endif // EV2_POOL_H
