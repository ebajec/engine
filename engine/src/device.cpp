#include "ev2/device.h"

#include "device.h"

#include "engine/resource/texture_loader.h"
#include "engine/resource/model_loader.h"

namespace ev2 {

Device *create_device(const char *path)
{
	Device *dev = new Device{};

	ResourceTableCreateInfo resource_table_info = {
		.resource_path = path
	};
	ResourceTable *rt = ResourceTable::create(&resource_table_info).release(); 

	ImageLoader::registration(rt);
	ModelLoader::registration(rt);

	std::unique_ptr<ResourceReloader> reloader = ResourceReloader::create(rt);

	return dev;
}

void device_heartbeat(Device *dev)
{
	dev->reloader->process_updates();
}

void destroy_device(Device *dev)
{
	delete dev;
}
};
