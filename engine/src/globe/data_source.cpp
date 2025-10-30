#include "globe/data_source.h"

void ds_context_destroy(ds_context *ctx)
{
	if (!ctx || !ctx->vtbl.destroy)
		return;

	ctx->vtbl.destroy(ctx);
}

