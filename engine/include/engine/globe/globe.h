#ifndef GLOBE_H
#define GLOBE_H

#include "engine/renderer/renderer.h"
#include "engine/resource/resource_table.h"
#include "engine/globe/data_source.h"

typedef struct Globe Globe;

struct GlobeStats
{
	size_t new_loads;
	size_t loaded;
};

struct GlobeUpdateInfo
{
	Camera const *camera;
};

Globe *globe_create(ResourceTable *table);
void globe_destroy(Globe *globe);

void globe_imgui(Globe *globe);

int globe_add_source(Globe *globe, TileDataSource *source);
LoadResult globe_update(Globe *globe, GlobeUpdateInfo *info);
void globe_draw(const Globe *globe, const RenderContext& ctx);

#endif
