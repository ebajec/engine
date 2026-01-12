#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "asset_table.h"
#include "pool.h"
#include "resource_impl.h"

#include <glm/mat4x4.hpp>

namespace ev2 {

struct MatrixCache
{
	std::vector<uint32_t> free;
	std::vector<glm::mat4> matrices;

	size_t capacity;

	BufferID buffer;

	uint32_t u_start = UINT32_MAX;
	uint32_t u_end = 0;

	void set_matrix(uint32_t idx, const glm::mat4& mat);

	uint32_t add_matrix(const glm::mat4& mat);
	void remove_matrix(uint32_t idx);

	/// @brief Upload new data to the GPU if anything has changed.
	/// @return True if buffer was resized, false otherwise
	bool update(Device *dev);
};

struct Device
{
	// Assets
	std::unique_ptr<AssetTable> assets;

	// Resource pools
	std::unique_ptr<ResourcePool<Buffer>> buffers;
	std::unique_ptr<ResourcePool<Image>> images;
	std::unique_ptr<ResourcePool<Texture>> textures;

	MatrixCache view_transforms;
};

};

#endif //DEVICE_INTERNAL_H
