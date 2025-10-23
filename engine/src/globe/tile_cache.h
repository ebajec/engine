#ifndef GLOBE_CACHE_H
#define GLOBE_CACHE_H

#include "engine/globe/tiling.h"
#include "engine/globe/data_source.h"
#include "globe/async_lru_cache.h"

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

extern tc_error tc_initialize();
extern void tc_terminate();

extern tc_error tc_load(TileDataSource const *source, size_t count,
						 TileCode const * tiles, TileCode *out);  
extern tc_error tc_acquire(TileDataSource const *source, TileCode id,
						   tc_ref *ref);
extern void tc_release(tc_ref ref);

#endif
