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
			.max = FLT_MIN
		};

		for (uint64_t i = 0; i < 4; ++i) {
			uint64_t c = children[i];

			mmt_value_t c_val;

			if (c == key) {
				c_val = new_val;
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
void mmt_remove(mmt_tree *mmt, uint64_t key)
{

}

mmt_tree *mmt_create(mmt_value_t defval)
{
	mmt_tree *mmt = new mmt_tree{};
	mmt->defval = defval;
	return mmt;
}

void mmt_destroy(mmt_tree *mmt)
{
	delete mmt;
}

mmt_value_t mmt_minmax(mmt_tree *mmt, uint64_t key)
{
	auto end = mmt->map.end();
	auto it = end;

	do {
		it = mmt->map.find(key);
		key = tile_code_coarsen(key);
	} while (it == end && (tile_code_zoom(key) != 0));

	if (it == end)
		return mmt->defval;
	return it->second;
}
