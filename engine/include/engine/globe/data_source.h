#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include "engine/globe/tiling.h"

//------------------------------------------------------------------------------
// Loader interface

struct ds_token;
struct ds_context;

typedef int (*ds_load_fn)(
	void *usr, 
	uint64_t id,
	struct ds_buf *buf,
	struct ds_token *token
);

typedef uint64_t (*ds_find_fn)(
	void *usr, 
	uint64_t val
);

typedef void (*ds_destroy_fn)(
	struct ds_context *ctx
);

struct ds_vtbl
{
	ds_destroy_fn 	destroy;

	ds_load_fn 		loader;
	ds_find_fn		find;

	float (*sample)(void *usr, float u, float v, uint8_t f);
	float (*max)(void *usr);
	float (*min)(void *usr);
};

struct ds_token_vtbl
{
	int (*is_cancelled)(struct ds_token *token);
};

struct ds_token
{
	void *usr;
	const struct ds_token_vtbl *vtbl;
};

struct ds_buf
{
	void *dst;
	size_t size; // size in bytes of dst
};

struct ds_context
{
	void 		*usr;
	ds_vtbl 	vtbl;
};

void ds_context_destroy(ds_context *ctx);

#endif
