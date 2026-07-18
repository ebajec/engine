#ifndef EV2_COMMON_H
#define EV2_COMMON_H

#include <cstddef>
#include <cstdint>
#include <cassert>

static constexpr uint64_t hash_combine(uint64_t a, uint64_t b) {
    return a ^ (b * 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
}


static constexpr size_t align_up(size_t x, size_t alignment)
{
    return ((x + alignment - 1) / alignment) * alignment;
}

static constexpr size_t align_up_pow2(size_t x, size_t align)
{
    return (x + (align - 1)) & ~(align - 1);;
}

static constexpr bool is_pow2(size_t x)
{
	return x && (((x - 1) & x) == 0);
}

static constexpr size_t mod_pow2(ptrdiff_t a, size_t b) {
	assert(is_pow2(b));
	return ((size_t)a) & (b - 1);
}

static_assert(mod_pow2(-3,4) == 1);
static_assert(mod_pow2(-6,8) == 2);
static_assert(1 + mod_pow2(-1,8) == 8);
static_assert(1 + mod_pow2(-1,16) == 16);

#endif
