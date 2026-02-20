#ifndef EV2_DEVICE_H
#define EV2_DEVICE_H

#include "ev2/defines.h"

namespace ev2 { 

enum Result
{
	TIMEOUT = 1,
	SUCCESS = 0,
	ELOAD_FAILED = -1,
	EINVALID_BINDING = -2,
	EUNKNOWN = -1024
};

struct Device;

Device *create_device(const char *path);
void destroy_device(Device *dev);

};

#endif // EV2_DEVICE_H
