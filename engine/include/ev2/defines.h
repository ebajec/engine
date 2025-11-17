#ifndef EV2_DEFINES_H
#define EV2_DEFINES_H

#include "stddef.h"
#include "stdint.h"

#define MAKE_HANDLE(name)\
struct name; \
typedef uint64_t name##ID;

#define EV2_HANDLE_CAST(ty, val)\
(ty##ID)val
#define EV2_TYPE_PTR_CAST(ty, val)\
(ty*)val

#define EV2_NULL_HANDLE 0;

#endif // EV2_DEFINES_H
