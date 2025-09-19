#ifndef BSORT_H
#define BSORT_H

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cmath>

#define BUCKET_COUNT 256
#define BITS_PER_PASS 8

/// @brief Computes the hilbert curve index for a point (x,y) in [0,1] x [0,1]
/// @param n - Order of the index.  The number of bits computed for the code is 
/// 2*n, so it is necessary that n <= 16.
static inline uint32_t hilbert_index_u32_f32(float xf, float yf, uint8_t n)
{
	uint32_t N = (1 << n);
	uint32_t mask = N - 1;
	float Nf = (float)N;
	uint32_t x = ((uint32_t)(xf*Nf)) & mask;
	uint32_t y = ((uint32_t)(yf*Nf)) & mask;

	uint32_t code = 0x0;
	for (int i = n - 1; i >= 0; --i) {
		uint32_t xi = ((x >> i) & 0x1);
		uint32_t yi = ((y >> i) & 0x1);

		uint32_t b = (0x3 * xi) ^ (yi);

		code = (code << 2) | b;

		uint32_t nx = yi ? x : (xi ? y ^ mask : y); 
		uint32_t ny = yi ? y : (xi ? x ^ mask : x); 
		
		x = nx;
		y = ny;
	}

	return code;
}


template<typename uint_t>
struct bsort_kv_t {
	uint_t key;
	uint_t val;
};

template<typename uint_t>
static inline void bsort(bsort_kv_t<uint_t>* data, size_t count, uint32_t passes)
{
	static constexpr uint32_t n_bits = 8*sizeof(uint_t);
	using kv_t = bsort_kv_t<uint_t>;

	// ensure passes is even and <= 8
	passes += passes % 2;
	passes = passes > n_bits/BITS_PER_PASS ? n_bits/BITS_PER_PASS : passes;

	bsort_kv_t<uint_t>* tmp = (kv_t*)malloc(count*sizeof(kv_t));

	kv_t* src = data;
	kv_t* dst = tmp;
	
	uint64_t mask = 0x000000FF;
	uint32_t shift = 0;

	uint32_t hist[BUCKET_COUNT];
	
	for (uint32_t i = 0; i < passes; i++)
	{
		memset(hist, 0x0, sizeof(hist));

		for (size_t j = 0; j < count; j++)
		{
			uint64_t key = src[j].key;
			uint32_t b = static_cast<uint32_t>((key & mask) >> shift);
			
			hist[b]++;
		}
		
		uint32_t offset = 0;

		for (uint32_t j = 0; j < BUCKET_COUNT; j++)
		{
			uint32_t swp = hist[j];
			hist[j] = offset;
			offset += swp;
		}
		
		for (size_t j = 0; j < count; j++)
		{
			bsort_kv_t keyval = src[j];
			uint32_t b = static_cast<uint32_t>((keyval.key & mask) >> shift);
			
			dst[hist[b]++] = keyval;
		}
		
		mask <<= BITS_PER_PASS;
		shift += BITS_PER_PASS;
		
		// swap arrays
		kv_t *swp = src;
		src = dst;
		dst = swp;
	}
	
	free(tmp);
}

#endif // BSORT_H
