#ifndef GLOBE_H
#define GLOBE_H

#include "engine/renderer/renderer.h"
#include "engine/resource/resource_table.h"
#include "engine/globe/tiling.h"
#include "engine/globe/gpu_cache.h"
#include "engine/globe/cpu_cache.h"

#include <vector>

namespace globe
{
	struct GlobeVertex
	{
		glm::vec3 pos;
		glm::vec2 uv;
		glm::vec3 normal;
		glm::vec2 big_uv;
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
		BufferID indirect;
		BufferID ibo;

		// texture array indices
		BufferID ssbo;

		MaterialID material;
		GLuint vao;

		std::vector<DrawCommand> cmds;
	};

	struct Globe
	{
		size_t loaded_count;
		std::vector<TileCode> tiles;

		RenderData render_data;

		std::unique_ptr<TileGPUCache> gpu_cache;
		std::unique_ptr<TileCPUCache> cpu_cache;
		std::unique_ptr<TileDataSource> source;
	};

	// Interface

	struct GlobeUpdateInfo
	{
		Camera const *camera;
	};

	LoadResult globe_create(Globe *globe, ResourceTable *table);
	LoadResult globe_update(Globe *globe, ResourceTable *table, GlobeUpdateInfo *info);
	void globe_draw(const RenderContext& ctx, const Globe *globe);

};

#endif
