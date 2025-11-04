#ifndef CPU_CACHE_H
#define CPU_CACHE_H

#include "engine/globe/tiling.h"
#include "engine/globe/data_source.h"

#include "globe/async_lru_cache.h"

static constexpr size_t TILE_CPU_PAGE_SIZE = 32;

#ifndef KILOBYTE
#define KILOBYTE ((size_t)1024)
#endif

#ifndef MEGABYTE
#define MEGABYTE (KILOBYTE*KILOBYTE)
#endif

#ifndef GIGABYTE
#define GIGABYTE (MEGABYTE*KILOBYTE)
#endif

struct tc_ref
{
	void *data;
	size_t size;
	alc_atomic_state *p_state;
};

enum tc_error : int
{
	TC_OK = 0,
	TC_EREGISTER = -1,
	TC_ENULL = -2,
};

typedef void (*tc_post_load_fn)(void* usr, uint64_t code, const ds_buf *buf);

struct tc_cache;

tc_error tc_create(tc_cache **seg, size_t tile_size, size_t capacity);
void tc_destroy(tc_cache *seg);

tc_error tc_load(
	tc_cache *seg, 
	ds_context const *ds, 

	void *usr, 
	tc_post_load_fn post_load,

	size_t count, 
	TileCode const *tiles, 
	TileCode *out
);

tc_error tc_acquire(const tc_cache *tc, TileCode code, tc_ref *p_ref);
void tc_release(tc_ref ref);

#endif // CPU_CACHE_H
