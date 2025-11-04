#ifndef MINMAX_TREE_H
#define MINMAX_TREE_H

#include <cstdint>

#include <unordered_map>

typedef struct mmt_value_s
{
	float min, max;
} mmt_value_t;

typedef struct {
	float min, max;
	int dist;
} mmt_result_t;

typedef struct mmt_tree_s
{
	// TODO: Use a better data structure
	std::unordered_map<uint64_t, mmt_value_t> map;
	mmt_value_t defval;		
} mmt_tree;

extern int mmt_create(mmt_tree **mmt, mmt_value_t defval);
extern void mmt_destroy(mmt_tree *mmt);

extern void mmt_insert(mmt_tree *mmt, uint64_t key, float min, float max);

/// @brief Inserts an element into the tree, but does not overwrite if
/// it already exists
/// @return 1 if insert was successful, 0 otherwise 
extern int mmt_insert_monotonic(mmt_tree *mmt, uint64_t key, float min, float max);
extern void mmt_remove(mmt_tree *mmt, uint64_t key);
extern mmt_result_t mmt_minmax(mmt_tree const *mmt, uint64_t key);

#endif // MINMAX_TREE_H
