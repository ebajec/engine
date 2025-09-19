#include "slab_allocator.hpp"

namespace slab_allocator_test_defs
{

typedef slab_allocator<64*512, 512> slab_allocator_test_64_512;

static_assert(63 == slab_allocator_test_64_512::blocks_per_slab, "size check");
static_assert(512 == alignof(slab_allocator_test_64_512::slab_hdr_t), "alignment check");
static_assert(64*512 == sizeof(slab_allocator_test_64_512::slab_t), "size check");

}
