#include "ev2/core.h"
#include "context.h"

#include <cassert>

namespace ev2 {

//------------------------------------------------------------------------------
// Loading interface

static ev2::Result model_reload_callback(ev2::GfxContext *ctx, void** usr, const char *path)
{
	// TODO...
	assert(false);
	return ev2::SUCCESS;
}

static void model_destroy_callback(ev2::GfxContext *ctx, void* usr)
{
	// TODO...
	assert(false);
}

static Mesh *model_create_callback(const char * path)
{
	// TODO...
	assert(false);
	return nullptr;
}

ModelID load_model(GfxContext *ctx, const char *path)
{
	AssetID id;
	ev2::Result result = ctx->assets->load(path, &id);

	if (result < ev2::SUCCESS)
		return EV2_NULL_HANDLE(Model);

	if (id)
		return ModelID{id};

	static AssetVTable vtbl = {
		.reload = model_reload_callback,
		.destroy = model_destroy_callback,
	};

	ev2::Mesh *mesh = model_create_callback(path); 

	if (result == SUCCESS) {
		id = ctx->assets->allocate(&vtbl, mesh, path); 
		return ModelID{id};
	}

	return EV2_NULL_HANDLE(Model);

}

}
