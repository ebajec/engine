#ifndef SLAB_ALLOCATOR_H
#define SLAB_ALLOCATOR_H

#include "diagnostic.h"

#include <new>

// libc
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdio>
#include <cstdlib>

static constexpr inline uintptr_t align_down(uintptr_t x, size_t align) {
    return x & ~( (uintptr_t)align - 1 );
}

template<size_t slab_size, size_t block_size>
class slab_allocator
{
public:
	static constexpr uint8_t blocks_per_slab = slab_size/block_size - 1;

	struct slab_t;

	struct slab_hdr_t 
	{
		alignas(block_size)
		uint8_t free_count;
		uint8_t free_list[blocks_per_slab];

		slab_t *prev, *next;
	};

	struct slab_t {
		slab_hdr_t hdr;

		uint8_t blocks[blocks_per_slab][block_size];
	};

private:
	slab_t *empty = nullptr;
	slab_t *partial = nullptr;
	slab_t *full = nullptr;

private:
	static bool slab_full(const slab_t* slab) 
	{
		return slab->hdr.free_count == 0;
	}

	static bool slab_empty(const slab_t* slab) 
	{
		return !slab || slab->hdr.free_count >= blocks_per_slab;
	}

	static bool slab_partial(const slab_t* slab) 
	{
		return !slab_empty(slab) && !slab_full(slab);
	}

	static void slab_push_free(slab_t* slab, uint8_t idx)
	{
		slab->hdr.free_list[slab->hdr.free_count++] = idx;
	}

	static uint8_t slab_pop_free(slab_t* slab)
	{
		assert(slab->hdr.free_count);
		return slab->hdr.free_list[--slab->hdr.free_count];
	}

	static slab_t *slab_alloc()
	{
#ifdef _MSC_VER
		struct slab_t *slab = static_cast<slab_t *>(_aligned_malloc(slab_size, slab_size));
#else 
		struct slab_t *slab = new(std::align_val_t{slab_size}) slab_t;
#endif
		
		slab->hdr.free_count = blocks_per_slab;
		for (uint8_t i = 0; i < blocks_per_slab; ++i) {
			slab->hdr.free_list[i] = static_cast<uint8_t>(blocks_per_slab - i - 1);
		}

		slab->hdr.next = nullptr;
		slab->hdr.prev = nullptr;

		return slab;
	}

	static void slab_free(slab_t *slab) {
		if (!slab) return;
#ifdef _MSC_VER
		_aligned_free(slab);
#else 
		delete slab;
#endif
	}
	
	/// @brief Moves src to dst, replacing src_head with it's next element
	/// and dst_head with src_head
	static void slab_list_move(slab_t *&dst_head, slab_t *&src_head) 
	{
		if (dst_head == src_head)
			return; 

		slab_t *slab = src_head;
		slab_t *prev = slab->hdr.prev;
		slab_t *next = slab->hdr.next;

		if (prev) {
			prev->hdr.next = next;
		}
		if (next) {
			next->hdr.prev = prev;
		}

		src_head = next;

		slab->hdr.prev = nullptr;
		slab->hdr.next = dst_head;

		if (dst_head) 
			dst_head->hdr.prev = slab;

		dst_head = slab;
	}
	
public:
	slab_allocator() {}

	~slab_allocator() 
	{
		// This means we've leaked some memory.
		if (partial) {
			for (slab_t *slab = partial; slab; slab = slab->hdr.next) {
				uint32_t count = static_cast<uint32_t>(blocks_per_slab - slab->hdr.free_count);
				VLOG_ERROR("Leaked %d blocks of size %d (%ld bytes)",
						count, block_size, (size_t)count * block_size);
			}
		}

		if (full) {
			for (slab_t *slab = full; slab; slab = slab->hdr.next) {
				uint32_t count = static_cast<uint32_t>(blocks_per_slab - slab->hdr.free_count);
				VLOG_ERROR("	%d blocks of size %d (%ld bytes)",
						count, block_size, (size_t)count * block_size);
			}
		}

		slab_t *slab = empty;
		while (slab) {
			slab_t *next = slab->hdr.next;

			slab_free(slab);

			slab = next;

			// These bottom two should theoretically never happen
			if (!slab && full) {
				slab = full;
				full = nullptr;
			}

			if (!slab && partial) {
				slab = partial;
				partial = nullptr;
			}
		}
	}

	void *alloc() 
	{
		slab_t *slab = partial;

		if (!slab && empty) {
			slab = empty;
			slab_list_move(partial,empty);
		} else if (!slab && !empty) {
			slab = slab_alloc();
			partial = slab;
		}

		// "Slab is full"
		assert(slab->hdr.free_count);

		uint8_t idx = slab_pop_free(slab);

		void* block = &slab->blocks[idx];

		assert(reinterpret_cast<uintptr_t>(block) + block_size <= 
		 reinterpret_cast<uintptr_t>(slab + sizeof(struct slab_t)));

		// Move to full list if necessary
		if (slab_full(slab)) {
			slab_list_move(full,partial);
		}

		assert(slab_empty(empty));
		return block;
	}

	void free(void * addr) 
	{
		uint8_t* hdr = reinterpret_cast<uint8_t*>(
			align_down(reinterpret_cast<uintptr_t>(addr), slab_size));

		struct slab_t* slab = reinterpret_cast<struct slab_t*>(hdr);

		uintptr_t block_offset = 
			static_cast<uintptr_t>((static_cast<uint8_t*>(addr) - hdr)) - offsetof(struct slab_t, blocks);

		uint8_t idx = static_cast<uint8_t>(block_offset/block_size);

		// "Attempting to free block in an empty slab"
		assert(!slab_empty(slab));

		if (slab == full) 
			slab_list_move(partial,full);

		slab_push_free(slab, idx);

		if (slab_empty(slab)) {
			if (slab == partial)
				slab_list_move(empty,partial);
			else 
				slab_list_move(empty,slab);

			assert(slab_empty(empty));
			return;
		}

		slab_list_move(partial,slab);

		assert(slab_empty(empty));
	}

//-------------------------------------------------------------------------------------------------
// Asserts

	static_assert(blocks_per_slab <= UINT8_MAX, 
		"blocks_per_slab must fit into a uint8_t");

	static_assert(((blocks_per_slab) & (slab_size/block_size)) == 0, 
		"slab_size must equal block_size times a power of two");

	static_assert(((block_size - 1)&block_size) == 0, 
		"block_size must be a power of two");

	static_assert(((slab_size - 1)&slab_size) == 0, 
		"slab_size must be a power of two");

	static_assert(sizeof(slab_hdr_t) <= block_size); 

	static_assert(sizeof(slab_t) == slab_size);
};


#endif
