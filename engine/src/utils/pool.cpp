#include "utils/pool.h"

static PagedArray<uint32_t, 128> paged_array_test;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
static void paged_array_test_compile() {
	paged_array_test.push_back(0);
	paged_array_test.push_back(1);
	paged_array_test.push_back(2);

	paged_array_test.reserve(10);
	paged_array_test.resize(5);

	uint32_t tmp = paged_array_test[0];

	paged_array_test.clear();
	paged_array_test.push_back(tmp);
}
#pragma clang diagnostic pop
