#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "asset_table.h"
#include "pool.h"
#include "resource_impl.h"

namespace ev2 {

struct Device
{
	std::unique_ptr<AssetTable> assets;

	std::unique_ptr<ResourcePool<Buffer>> buffers;
	std::unique_ptr<ResourcePool<Image>> images;
	std::unique_ptr<ResourcePool<Texture>> textures;
};

};

#endif //DEVICE_INTERNAL_H
