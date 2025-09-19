#ifndef RTREE_H
#define RTREE_H

#include "aabb.h"
#include "bump_allocator.h"
#include "slab_allocator.hpp"

#include <queue>
#include <vector>
#include <type_traits>

#include <cfloat>
#include <cstdint>
#include <cstring>

struct rtree_node_t;

typedef uint64_t rtree_data_t;

// @brief Entry in a node's child-array. Stores a bounding box + pointer
// for read efficiency.
//
// @note The 'h' (height) field of the parent node determines which union
// member to use.  Entries in a leaf node contain data pointers.
struct rtree_entry_t {
	aabb_t rect;
	union {
		rtree_node_t *node;
		rtree_data_t data;
	};
};

// Bytes allocated for each node.  (Header + array of entries for children).
static constexpr size_t RTREE_NODE_ALLOC_SIZE = 512;
static constexpr size_t RTREE_NODE_CAPACITY = RTREE_NODE_ALLOC_SIZE/sizeof(rtree_entry_t);
static constexpr size_t RTREE_NODE_MINFILL = RTREE_NODE_CAPACITY / 2;

/// @brief R-Tree node.  
///
/// @note Nodes do not store their own bounding boxes.  These are stored
/// in the parent's array of entries.
///
/// @note If h = 0, a node is considered a 'leaf', and it's entries 
/// contain a data handle instead of a pointer to another node.
struct rtree_node_t
{
	alignas(RTREE_NODE_ALLOC_SIZE) 

	uint8_t h; // If a tree node had a height larger than 256 there would be more
			   // nodes than what can fit in a 64-bit address space.
	
	uint8_t N; // Does not need to be large.
	
	uint8_t tag[6]; // extra bytes

	rtree_entry_t entries[RTREE_NODE_CAPACITY];
};

struct rtree_t
{
	// root is never a leaf node
	rtree_entry_t root;

	// scratch memory for deletion logic
	bump_allocator_t scratch;

	slab_allocator<128*sizeof(rtree_node_t),sizeof(rtree_node_t)> node_allocator;


	~rtree_t();
};

extern rtree_t *rtree_create();
extern int rtree_bulk_reload(rtree_t *tree, const rtree_entry_t* entries, size_t count, 
							 bool presorted = false); 

extern void rtree_free(rtree_t *tree);

/// @brief Insert a data entry into the tree.
/// @note Not thread safe.  
extern void rtree_insert(rtree_t *tree, rtree_entry_t entry);

/// @brief Remove a data entry from the tree.
/// @note Not thread safe.
extern bool rtree_remove(rtree_t *tree, rtree_entry_t entry);

//--------------------------------------------------------------------------------------------------
// Template definitions

template <typename T, typename intersects_aabb_t> 
static inline void rtree_get_intersections_rec(const rtree_node_t *node, T test, intersects_aabb_t intersect, 
											   std::vector<rtree_entry_t> &intersections)
{
	uint8_t hit_count = 0;
	rtree_node_t *hits[RTREE_NODE_CAPACITY];

	const bool is_leaf = node->h == 0;
	for (uint8_t i = 0; i < node->N; ++i) {
		rtree_entry_t ent = node->entries[i];
		bool hit = intersect(ent.rect, test);

		if (hit && is_leaf) {
			intersections.push_back(ent);
			continue;
		}

		if (hit)
			hits[hit_count++] = ent.node;
	}

	for (uint8_t i = 0; i < hit_count; ++i) {
		rtree_get_intersections_rec(hits[i],test, intersect, intersections);
	}
}

template <typename T, typename intersects_aabb_t> 
static inline std::vector<rtree_entry_t> rtree_get_intersections(const rtree_t *tree, T test, intersects_aabb_t intersect)
{
	static_assert(std::is_invocable_r_v<bool,intersects_aabb_t,aabb_t, T>, 
		"intersect must be of the form bool(*)(aabb_t, T)");

	std::vector<rtree_entry_t> intersections;
	rtree_get_intersections_rec(tree->root.node, test, intersect, intersections);

	return intersections;
}

constexpr auto rtree_knn_default_constraint = [](rtree_data_t){return true;};
constexpr auto rtree_knn_default_metric = &aabb_dist_sq;

struct rtree_knn_entry {
	rtree_data_t data;
	scalar_t d;

	constexpr bool operator < (const rtree_knn_entry& other) const {
		return d < other.d;
	}
};

typedef std::priority_queue<rtree_knn_entry,std::vector<rtree_knn_entry>>
	rtree_knn_result; 

/// @brief Find the k nearest data entries to the point p, satifying a given constraint.
///
/// @param metric scalar_t(*)(rtree_data_t, key_t) OR scalar_t(*)(aabb_t, key_t) - A custom 
/// metric to use on the data. Must return the SQUARED distance. By default, the squared 
/// distance from the entries' bounding box is used.
///
/// @param constraint bool(*)(rtree_data_t) - A constraint to apply on leaf data to omit 
/// entries. Note that a very strict constaint can easily push this function to visit 
/// every entry.
template<
	typename key_t = vec2,
	typename constraint_t = decltype (rtree_knn_default_constraint),
	typename metric_t = decltype (rtree_knn_default_metric)
>
static rtree_knn_result rtree_k_nearest(
	const rtree_t *tree, 
	key_t key, 
	size_t k, 
	scalar_t dmax = FLT_MAX, 
	constraint_t constraint = rtree_knn_default_constraint,
	metric_t metric = rtree_knn_default_metric
)
{	
	static_assert(std::is_invocable_r_v<bool,constraint_t,rtree_data_t>,
			   "constaint must have signature bool(*)(rtree_data_t)");

	rtree_knn_result result;

	if (!tree->root.node || !k) 
		return result;
	
	struct pq_entry 
	{
		rtree_entry_t ent;
		scalar_t d;
		bool is_data : 1;
	};

	constexpr auto comp = [](const pq_entry& a, const pq_entry& b) {
		return a.d > b.d;
	};

	// TODO: use an allocator which only switches to the heap once it 
	// exceeds a fixed amount of stack memory.  That way frequent single
	// element queries can be as fast as possible.
	std::vector<pq_entry> vec;
	vec.reserve(32*sizeof(pq_entry));

	std::priority_queue<pq_entry, std::vector<pq_entry>, decltype(comp)> 
		pq (comp, std::move(vec));

	pq_entry first;
	first.ent = tree->root;

	if constexpr (std::is_invocable_r_v<scalar_t,metric_t,aabb_t,key_t>)
		first.d = metric(tree->root.rect,key); 
	else 
		first.d = aabb_dist_sq(tree->root.rect,key);

	first.is_data = false;

	pq.push(first);


	scalar_t worst_dist = dmax;

	while (!pq.empty()) {
		pq_entry e = pq.top();
		pq.pop();

		if (e.d >= worst_dist)
			break;

		// -> e.d < worst_dist here
		if (e.is_data) {
 			if (!constraint(e.ent.data)) 
				continue;

			if (result.size() < k) {
				result.push(rtree_knn_entry{e.ent.data,e.d});
			} else {
				result.pop();
				result.push(rtree_knn_entry{e.ent.data,e.d});
			}
			
			if (result.size() >= k)
				worst_dist = e.d;

			continue;
		}

		const rtree_node_t *node = e.ent.node;

		bool leaf = node->h == 0;

		for (uint8_t i = 0; i < node->N; ++i) {
			rtree_entry_t child = node->entries[i];

			scalar_t d;

			if constexpr (std::is_invocable_r_v<scalar_t,metric_t,aabb_t,key_t>) {
				d = metric(child.rect,key); 
			} else {
				static_assert(std::is_invocable_r_v<scalar_t,metric_t,rtree_data_t,key_t>,
				  "Data metric must have signature scalar_t(*)(rtree_data_t, key_t)");

				d = leaf ? metric(child.data,key) : aabb_dist_sq(child.rect,key);
			}

			if (d > dmax || d > worst_dist) 
				continue;

			pq_entry entry;
			entry.ent = child;
			entry.d = d;
			entry.is_data = leaf;

			pq.push(entry);
		}
	}

	return result;
}
#endif
