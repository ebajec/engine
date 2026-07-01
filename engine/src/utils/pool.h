#ifndef EV2_POOL_H
#define EV2_POOL_H

#include <ev2/utils/log.h>
#include "utils/common.h"

#include <vector>
#include <mutex>
#include <cassert>
#include <memory>
#include <atomic>
#include <bit>

#include <cstring>
#include <cstdint>

struct PoolID
{
	uint64_t u64;

	static PoolID create(uint32_t id, uint16_t gen) {
		return PoolID{
			.u64 = (((uint64_t)id) | (((uint64_t)gen) << 32))
		};
	}
	uint32_t slot() const {return static_cast<uint32_t>(u64);} 
	uint16_t gen() const {return static_cast<uint16_t>(u64 >> 32);} 
};

struct AtomicResourceState {
	typedef uint64_t value_t;

	std::atomic_uint64_t value;

	value_t ref() {
		value_t expected = value.load(std::memory_order_relaxed);
		value_t desired;
		do {
			uint32_t refs = (uint32_t)expected;
			desired = (expected & (0xFFFFFFFFLLU)) | (uint64_t)(refs + 1);
		} while (!value.compare_exchange_weak(expected, desired, 
			std::memory_order_acq_rel, std::memory_order_relaxed));

		return expected;
	}

	value_t deref() {
		value_t expected = value.load(std::memory_order_relaxed);
		value_t desired;
		do {
			uint32_t refs = (uint32_t)expected;
			desired = (expected & (0xFFFFFFFFLLU)) | (uint64_t)(refs - 1);
		} while (!value.compare_exchange_weak(expected, desired, 
			std::memory_order_acq_rel, std::memory_order_relaxed));

		return expected;
	}

	value_t inc_gen() {
		value_t expected = value.load(std::memory_order_relaxed);
		value_t desired;
		do {
			uint32_t gen = (uint32_t)(expected >> 32) + 1;
			desired = (((uint64_t)gen) << 32) | (expected & (0xFFFFFFFFLLU));
		} while (!value.compare_exchange_weak(expected, desired, 
			std::memory_order_acq_rel, std::memory_order_relaxed));

		return expected;
	}
};

#define aligned_alloc_alias(align, size) aligned_alloc(align, size)
#define aligned_free_alias(ptr) free(ptr)

template<typename T, size_t PageSize, size_t PageAlign = alignof(T)>
struct PagedArray
{
	static_assert(PageSize && is_pow2(PageSize));
	static_assert(PageAlign && is_pow2(PageAlign));
	static_assert(PageAlign <= PageSize);
	static_assert(PageAlign >= alignof(T));

	static constexpr size_t PageSizeBytes = PageSize * sizeof(T);
	static constexpr unsigned LogPageSize = std::bit_width(PageSize) - 1;

	T **pages_ = nullptr;
	size_t size_ = 0;
	uint32_t page_cap_ = 0;
	uint32_t page_alloc_num_ = 0;

	~PagedArray()
	{
		for (size_t i = 0; i < page_alloc_num_; ++i) {
			aligned_free_alias(pages_[i]);
		}
		free(pages_);
	}

	size_t size() const {return size_;}

	T &operator [] (size_t idx)
	{
		return pages_[idx >> LogPageSize][idx & (PageSize - 1)];
	}

	const T &operator [] (size_t idx) const
	{
		return this->operator[](idx);
	}

	void reserve(size_t capacity)
	{
		uint32_t req_pages = static_cast<uint32_t>((capacity + PageSize - 1) >> LogPageSize);

		if (page_alloc_num_ >= req_pages)
			return;

		if (req_pages > page_cap_) {
			pages_ = pages_ ? 
				(T**)realloc(pages_, sizeof(T*) * req_pages) : (T**)malloc(sizeof(T*) * req_pages);
			page_cap_ = req_pages;
		}

		for (size_t i = page_alloc_num_; i < req_pages; ++i)
			pages_[i] = (T*)aligned_alloc_alias(PageAlign, PageSizeBytes);

		page_alloc_num_ = req_pages;
	}

	template<typename DefVal = void>
	void resize(size_t size, DefVal defval = void{})
	{
		if (size < size_) {
			shrink_to(size, true);
		} else if (size > size_) {
			size_t start_page = static_cast<uint32_t>(size_ >> LogPageSize);
			size_t start_idx = size_ & (PageSize - 1);

			reserve(size);

			size_t rem = (size & (PageSize - 1));
			size_t end = page_alloc_num_;

			for (size_t i = start_page; i < end; ++i) {
				T *data = pages_[i];
				size_t count = (i == end - 1 && rem) ? rem : PageSize;

				for (size_t j = (i == start_page) * start_idx; j < count; ++j) {
					if constexpr(std::is_same_v<DefVal, void>)
						new (data + j) T;
					else
						new (data + j) T {defval};
				}
			}
		}
		size_ = size;
	}

	T &push_back(const T &value)
	{
		T copy = value;
		return push_back(std::move(copy));
	}

	T &push_back(T &&value)
	{
		size_t page = size_ >> LogPageSize;
		size_t idx = size_ & (PageSize - 1);

		++size_;

		if (size_ > (page_alloc_num_ << LogPageSize)) {
			size_t new_page_idx = page_alloc_num_++; 

			if (new_page_idx >= page_cap_) {
				page_cap_ = page_cap_ ? (3 * page_cap_) / 2 : 4;

				if (!pages_) {
					pages_ = (T**)malloc(page_cap_ * sizeof(T*));
				} else {
					pages_ = (T**)realloc(pages_, page_cap_*sizeof(T*));
				}
			}

			pages_[new_page_idx] = (T*)aligned_alloc_alias(PageAlign, PageSizeBytes);
		}

		return *new (&pages_[page][idx]) T{std::move(value)};
	}

	// @brief destructs all elements past position pos and sets size to pos, freeing
	// pages if free_pages is set.
	void shrink_to(size_t pos, bool deallocate) 
	{
		uint32_t filled_page_num = static_cast<uint32_t>((size_ + PageSize - 1) >> LogPageSize);
		uint32_t start_page = static_cast<uint32_t>(pos >> LogPageSize);

		size_t rem_size = size_ & (PageSize - 1);
		size_t rem_pos = pos & (PageSize - 1);

		for (size_t i = start_page; i < filled_page_num; ++i) {
			T *data = pages_[i];

			size_t count = (i == filled_page_num - 1) && rem_size ? rem_size : PageSize; 

			bool is_first = (i == start_page);

			for (size_t j = is_first * rem_pos; j < count; ++j) {
				data[j].~T();
			}

			if (deallocate && (!is_first || rem_pos == 0))
				aligned_free_alias(data);
		}

		if (deallocate) {
			uint32_t req_pages = static_cast<uint32_t>((pos + PageSize - 1) >> LogPageSize);

			if (!pos) {
				free(pages_);
				pages_ = nullptr;
			}
			else {
				pages_ = (T**)realloc(pages_, sizeof(T*) * req_pages);
			}
			page_alloc_num_ = req_pages;
			page_cap_ = start_page;
		}

		size_ = pos;
	}

	void clear() 
	{
		shrink_to(0, false);
	}
};

static PagedArray<uint32_t, 128> paged_array_test;

static void paged_array_test_compile() {
	paged_array_test.push_back(0);
	paged_array_test.push_back(1);
	paged_array_test.push_back(2);

	paged_array_test.reserve(10);
	paged_array_test.resize(5);

	uint32_t tmp = paged_array_test[0];

	paged_array_test.clear();
	paged_array_test.push_back(tmp);
}

template<typename T>
struct ResourcePool {
	static constexpr uint32_t PAGE_SIZE_BITS = 6;
	static constexpr uint32_t PAGE_MASK = (1 << PAGE_SIZE_BITS) - 1;
	static constexpr uint32_t PAGE_SIZE = 1 << PAGE_SIZE_BITS;

	struct entry_t {
		T val;
		AtomicResourceState state;
	};

	static uint16_t get_gen(uint64_t state) {
		return (uint16_t)(state >> 32);
	}
	static uint32_t get_refs(uint64_t state) {
		return (uint32_t)(state);
	}

	static ResourcePool<T> *create();

	std::vector<std::unique_ptr<entry_t[]>> pages;
	std::vector<uint32_t> free_list;

	size_t cap = 0;

	// TODO: Eventually want to not use a mutex and have pages of 
	// atomic counters
	mutable std::mutex sync;

	PoolID allocate(T&& val);
	void deallocate(PoolID id);
	T* get(PoolID id) const;

	bool check_handle(PoolID id) const {
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
PoolID ResourcePool<T>::allocate(T&& val)
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

	entry_t *ent = get_entry(slot);
	uint64_t state = ent->state.inc_gen();
	ent->val = std::move(val); 

	assert(slot);

	uint16_t gen = get_gen(state);

	return PoolID::create(slot, gen);
}

template<typename T>
void ResourcePool<T>::deallocate(PoolID id)
{
	if (!check_handle(id)) {
		return;
	}

	uint32_t slot = id.slot();
	entry_t *ent = get_entry(slot);

	uint64_t state = ent->state.value.load(std::memory_order_relaxed);
	if (id.gen() == get_gen(state)) {
		log_error("generation counter mismatch: %d != %d", id.gen(), get_gen(state));
	}

	memset(&ent->val, 0x0, sizeof(T));

	std::unique_lock<std::mutex> lock(sync);
	free_list.push_back(slot);
}

template<typename T>
T *ResourcePool<T>::get(PoolID id) const
{
	if (!check_handle(id)) {
		return nullptr;
	}

	uint32_t slot = id.slot();
	uint32_t gen = id.gen();

	entry_t *ent = get_entry(slot);

	uint64_t state = ent->state.value.load(std::memory_order_relaxed);

	return (get_gen(state) == gen) ? &ent->val : nullptr;
}


#endif // EV2_POOL_H
