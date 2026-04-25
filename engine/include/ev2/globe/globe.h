#ifndef GLOBE_H
#define GLOBE_H

#include <ev2/globe/data_source.h>
#include <ev2/context.h>
#include <ev2/render.h>

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

Globe *globe_create(ev2::Context *dev);
void globe_destroy(Globe *globe);

void globe_imgui(Globe *globe);

float globe_sample_elevation(const Globe *globe, const glm::dvec3& p);

ev2::Result globe_update(Globe *globe, GlobeUpdateInfo *info);
void globe_draw(const Globe *globe, const ev2::PassCtx& pass);

#endif
