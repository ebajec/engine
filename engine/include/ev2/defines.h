#ifndef EV2_DEFINES_H
#define EV2_DEFINES_H

#include "stddef.h"
#include "stdint.h"

#define MAKE_HANDLE(name)\
struct name##ID {uint64_t id;}

//typedef uint64_t name##ID;

#define EV2_HANDLE_CAST(ty, val)\
ev2::ty##ID{.id = (uint64_t)val}

#define EV2_TYPE_PTR_CAST(ty, val)\
(ty*)val.id

#define EV2_NULL_HANDLE(ty) ty##ID{.id = 0}

#endif // EV2_DEFINES_H
