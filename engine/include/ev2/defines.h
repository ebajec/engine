#ifndef EV2_DEFINES_H
#define EV2_DEFINES_H

#include "stddef.h"
#include "stdint.h"

#include <functional>

#define MAKE_HANDLE(name)\
struct name##ID {\
	uint64_t id = 0;\
	inline bool is_valid() const{return (bool)id;}\
}

#define MAKE_ASSET_HANDLE(name)\
struct name##ID {\
	uint64_t id = 0;\
	inline bool is_valid() const{return id;}\
}

#define MAKE_HANDLE_VERSIONED(name)\
namespace ev2 {\
	struct alignas(uintptr_t) name##ID {\
		uint32_t id = 0;\
		uint16_t gen = 0;\
		constexpr bool is_valid() const{return id;}\
		constexpr uint64_t uint64() const{return (uint64_t)id|((uint64_t)gen << 32);}\
		constexpr bool operator == (const name##ID &other) const {\
			return other.id == id && other.gen == gen;\
		}\
	};\
}\
namespace std {\
    template<>\
    struct hash<ev2::name##ID> {\
        size_t operator()(const ev2::name##ID& id) const noexcept {\
            return std::hash<uint64_t>{}(\
                (static_cast<uint64_t>(id.id) << 32) | id.gen\
            );\
        }\
    };\
};\

#define EV2_HANDLE_CAST(ty, val)\
ev2::ty##ID{.id = (uint64_t)val}

#define EV2_TYPE_PTR_CAST(ty, val)\
(ty*)val.id

#define EV2_NULL_HANDLE(ty) ev2::ty##ID{.id = 0}
#define EV2_IS_NULL(h) (h.id == 0)
#define EV2_VALID(h) (h.id != 0)

#endif // EV2_DEFINES_H
