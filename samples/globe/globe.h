#ifndef GLOBE_H
#define GLOBE_H

#include "geometry.h"
#include "gl_renderer.h"
#include "resource_table.h"
#include "tiling.h"
#include "tile_cache.h"
#include "dataset.h"

#include <vector>

namespace globe
{
	struct GlobeVertex
	{
		glm::vec3 pos;
		glm::vec2 uv;
		glm::vec3 normal;
		union {
			struct {
				uint32_t left; 
				uint32_t right;
			};
			TileCode code;
		};
	};

	struct RenderData
	{
		BufferID vbo;
		BufferID ibo;

		// texture array indices
		BufferID ssbo;

		MaterialID material;
		GLuint vao;

		std::vector<DrawCommand> cmds;
	};

	struct Globe
	{
		std::vector<TileCode> tiles;

		RenderData render_data;

		std::unique_ptr<TileGPUCache> tex_cache;
		std::unique_ptr<TileCPUCache> data_cache;
		std::unique_ptr<TileDataSource> source;
	};


	struct GlobeUpdateInfo
	{
		Camera const *camera;
	};

	struct select_tiles_params
	{
		const TileDataSource * source;

		frustum_t frust;
		aabb3_t frust_box;
		glm::dvec3 origin;
		double res;
	};


//------------------------------------------------------------------------------
// Loaders

	LoadResult globe_create(Globe *globe, ResourceTable *table);
	LoadResult globe_update(Globe *globe, ResourceTable *table, GlobeUpdateInfo *info);
	void globe_record_draw_cmds(const RenderContext& ctx, const Globe *globe);

//------------------------------------------------------------------------------
// Interface
	extern void init_debug(ResourceTable *table);
	extern void draw(RenderContext const &ctx);
	extern void update_boxes();

	void select_tiles(
		select_tiles_params& params,
		std::vector<TileCode>& tiles
	);

};

#endif
