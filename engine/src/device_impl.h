#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "engine/resource/resource_table.h"

namespace ev2 {

struct Device
{
	std::unique_ptr<ResourceTable> rt;
	std::unique_ptr<ResourceReloader> reloader;
};

};

#endif //DEVICE_INTERNAL_H
