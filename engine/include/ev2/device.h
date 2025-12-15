#ifndef EV2_DEVICE_H
#define EV2_DEVICE_H

#include "ev2/defines.h"

namespace ev2 { 

enum Result
{
	SUCCESS,
	ELOAD_FAILED,
	EUNKNOWN
};

struct Device;

Device *create_device(const char *path);
void destroy_device(Device *dev);

};

#endif // EV2_DEVICE_H
