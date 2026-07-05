#ifndef EV2_DEFINES_H
#define EV2_DEFINES_H

#include "stddef.h"
#include "stdint.h"

#define MAKE_HANDLE(name)\
struct name##ID {\
	uint64_t id = 0;\
	inline bool is_valid() const{return id;}\
}

#define MAKE_HANDLE_VERSIONED(name)\
struct alignas(uintptr_t) name##ID {\
	uint32_t id = 0;\
	uint16_t gen = 0;\
	inline bool is_valid() const{return id;}\
}

#define EV2_HANDLE_CAST(ty, val)\
ev2::ty##ID{.id = (uint64_t)val}

#define EV2_TYPE_PTR_CAST(ty, val)\
(ty*)val.id

#define EV2_NULL_HANDLE(ty) ev2::ty##ID{.id = 0}
#define EV2_IS_NULL(h) (h.id == 0)
#define EV2_VALID(h) (h.id != 0)

#endif // EV2_DEFINES_H
