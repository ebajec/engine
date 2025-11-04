#include "minmax_tree.h"
#include "engine/globe/tiling.h"

#include "utils/log.h"

#include <cfloat>

void modify_update(mmt_tree *mmt, uint64_t key, mmt_value_t new_val)
{
	auto end = mmt->map.end();

	while (tile_code_zoom(key) != 0) {
		uint64_t parent = tile_code_coarsen(key);

		uint64_t children[4] = {
			tile_code_refine(parent, TILE_LOWER_LEFT),
			tile_code_refine(parent, TILE_LOWER_RIGHT),
			tile_code_refine(parent, TILE_UPPER_LEFT),
			tile_code_refine(parent, TILE_UPPER_RIGHT),
		};

		mmt_value_t p_val = {
			.min = FLT_MAX,
			.max = -FLT_MAX
		};

		bool found_child = false;

		for (uint64_t i = 0; i < 4; ++i) {
			uint64_t c = children[i];

			mmt_value_t c_val;

			if (c == key) {
				c_val = new_val;
				found_child = true;
			} else {
				auto it = mmt->map.find(c);

				if (it == end) {
					continue;
				}

			 	c_val = it->second;
			}

			p_val.min = std::min(c_val.min,p_val.min);
			p_val.max = std::min(c_val.max,p_val.max);
		}

		assert(found_child);

		auto p_it = mmt->map.find(parent);

		if (p_it == end) {
			log_error("Parent entry with id %lld not present in tree", parent);
			return;
		}

		p_it->second = p_val;

		key = parent;
	}
}

void insert_update(mmt_tree *mmt, uint64_t key, mmt_value_t new_val)
{
	while (tile_code_zoom(key) != 0) {
		uint64_t parent = tile_code_coarsen(key);

		auto it = mmt->map.find(parent); 

		if (it == mmt->map.end()) {
			mmt->map[parent] = new_val;
		} else {
			mmt_value_t p_val = it->second;

			if (new_val.max <= p_val.max && new_val.min >= p_val.min) {
				return;
			}

			it->second.min = std::min(new_val.min, p_val.min);
			it->second.max = std::max(new_val.max, p_val.max);
		}

		key = parent;
	}
}

void mmt_insert(mmt_tree *mmt, uint64_t key, float min, float max)
{
	auto end = mmt->map.end();
	auto it = mmt->map.find(key);

	mmt_value_t new_val = mmt_value_t{
		.min = min,
		.max = max
	};

	if (it == end) {
		mmt->map[key] = new_val;
		insert_update(mmt, key, new_val);
	} else {
		it->second = new_val;
		modify_update(mmt, key, new_val);
	}
}

int mmt_insert_monotonic(mmt_tree *mmt, uint64_t key, float min, float max)
{
	auto end = mmt->map.end();
	auto it = mmt->map.find(key);

	mmt_value_t new_val = mmt_value_t{
		.min = min,
		.max = max
	};

	if (it == end) {
		mmt->map[key] = new_val;
		insert_update(mmt, key, new_val);
		return 1;
	}
	return 0;
}

void mmt_remove(mmt_tree *mmt, uint64_t key)
{

}

int mmt_create(mmt_tree **p_mmt, mmt_value_t defval)
{
	mmt_tree *mmt = new mmt_tree{};
	mmt->defval = defval;
	*p_mmt = mmt;

	return 0;
}

void mmt_destroy(mmt_tree *mmt)
{
	if (!mmt)
		return;
	delete mmt;
}

mmt_result_t mmt_minmax(const mmt_tree *mmt, uint64_t key)
{
	auto end = mmt->map.end();
	auto it = end;

	mmt_result_t res{};

	do {
		it = mmt->map.find(key);
		key = tile_code_coarsen(key);
		++res.dist;
	} while (it == end && tile_code_zoom(key) != 0);

	--res.dist;

	if (it == end) {
		log_info("Failed to find parent key for entry %lld",key);
		res.min = mmt->defval.min;
		res.max = mmt->defval.max;
	} else {
		mmt_value_t val = it->second; 
		res.min = val.min;
		res.max = val.max;
	}

	return res;
}
