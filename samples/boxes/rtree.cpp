#include "aabb.h"
#include "rtree.h"
#include "bsort.h"

// stl
#include <vector>

// libc
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cassert>

namespace {

struct split_result_t
{
	aabb_t old_node_box;
	aabb_t new_node_box;
	rtree_node_t *new_node;
};

struct reinsert_list_t 
{
	reinsert_list_t *next;
	uint8_t height;
	uint8_t count;
	rtree_entry_t arr[RTREE_NODE_MINFILL];
};

struct remove_result_t 
{
	reinsert_list_t *list;
	bool reinsert:1;
	bool removed:1;
};

bool is_leaf(const rtree_node_t *node) 
{
	return node->h == 0;
}

rtree_node_t *node_create(rtree_t *tree)
{
	void *mem = tree->node_allocator.alloc();
	rtree_node_t * node = static_cast<rtree_node_t*>(mem);

	//rtree_node_t *node = new rtree_node_t;
	
	node->h = 0;
	node->N = 0;
	return node;
}

void node_free(rtree_t *tree, rtree_node_t *node)
{
	tree->node_allocator.free(node);

	//delete node;
}

void node_free_rec(rtree_t *tree, rtree_node_t *node)
{
	if (!node) return;

	if (!is_leaf(node))
		for (uint8_t i = 0; i < node->N; ++i)
			node_free_rec(tree,node->entries[i].node);
	node_free(tree,node);
}

/// @brief compute the minimum bounding rectangle (mbr) for a node
aabb_t compute_mbr(rtree_entry_t *entries, uint8_t N)
{
	assert(N);
	aabb_t bounding = entries[0].rect;
	for (uint8_t i = 1; i < N; ++i)
	{
		bounding = aabb_add(bounding,entries[i].rect);
	}
	return bounding;
}

uint8_t smallest_overlap_index(rtree_node_t *node, aabb_t box)
{
	uint8_t argmin = 0;
	scalar_t min_expansion = FLT_MAX;
	for (uint8_t i = 0; i < node->N; ++i)
	{
	  	aabb_t entry_mbr = node->entries[i].rect;
		aabb_t expansion = aabb_add(entry_mbr,box);

		scalar_t expansion_area = aabb_area(expansion) - aabb_area(entry_mbr);

		if (expansion_area < min_expansion) {
			argmin = i;
			min_expansion = expansion_area;
		}
	}

	return argmin;
}

void node_insert_internal(rtree_node_t* node, rtree_entry_t entry)
{
	assert(!is_leaf(node));
	assert(node->N < RTREE_NODE_CAPACITY);
	node->entries[node->N++] = entry;
}

void node_insert_entry(rtree_node_t* node, rtree_entry_t entry)
{
	assert(node->N < RTREE_NODE_CAPACITY);
	node->entries[node->N++] = entry;
}

/// @brief Split a node's entries into two nodes. 
split_result_t node_split_contents_linear(rtree_node_t *src, rtree_node_t *dst)
{
	assert(src->N == RTREE_NODE_CAPACITY);

	constexpr uint8_t N = RTREE_NODE_CAPACITY;

	rtree_entry_t entries[RTREE_NODE_CAPACITY];
	memcpy(entries, src->entries, N*sizeof(rtree_entry_t));

	scalar_t min_x, max_x, min_y, max_y;
	min_x = min_y = FLT_MAX;
	max_x = max_y = -FLT_MAX;

	for (uint8_t i = 0; i < N; ++i) {
		aabb_t box = entries[i].rect;

		min_x = fmin(box.x0,min_x);
		max_x = fmax(box.x1,max_x);
		min_y = fmin(box.y0,min_y);
		max_y = fmax(box.y1,max_y);
	}

	if (max_y - min_y > max_x - min_x) {
		constexpr auto comp_y = [](const rtree_entry_t &a, const rtree_entry_t &b) {
			return (a.rect.y1 + a.rect.y0) < (b.rect.y1 + b.rect.y0);
		};
		std::sort(entries, entries + N, comp_y);
	} else {
		constexpr auto comp_x = [](const rtree_entry_t &a, const rtree_entry_t &b) {
			return (a.rect.x1 + a.rect.x0) < (b.rect.x1 + b.rect.x0);
		};
		std::sort(entries, entries + N, comp_x);
	}

	rtree_entry_t src_e = entries[0];
	rtree_entry_t dst_e = entries[N - 1];

	uint8_t src_idx = 0;
	uint8_t dst_idx = 0;

	src->entries[src_idx++] = src_e;
	dst->entries[dst_idx++] = dst_e;

	aabb_t src_box = src_e.rect;
	aabb_t dst_box = dst_e.rect;

	scalar_t src_area = aabb_area(src_box);
	scalar_t dst_area = aabb_area(dst_box);

	uint8_t min_fill = N/2 + N%2;

	for (uint8_t i = 1; i < N - 1; ++i) {
		rtree_entry_t e = entries[i];

		aabb_t src_test_box = aabb_add(src_box,e.rect);
		aabb_t dst_test_box = aabb_add(dst_box,e.rect);

		scalar_t src_test_area = aabb_area(src_test_box);
		scalar_t dst_test_area = aabb_area(dst_test_box);

		uint8_t rem = static_cast<uint8_t>(N - (src_idx + dst_idx));

		bool src_expands_less = src_test_area - src_area < dst_test_area - dst_area; 
		bool fill_src = src_idx + rem <= min_fill;
		bool fill_dst = dst_idx + rem <= min_fill; 

		// add to whichever side requires the least amount of expansion, 
		// or whichever one must be filled to meet the minimum size
		// requirements
		if (fill_src || (!fill_dst && src_expands_less))
		{
			src->entries[src_idx++] = e;
			src_box = src_test_box;
			src_area = src_test_area;
		} else {
			dst->entries[dst_idx++] = e;
			dst_box = dst_test_box;
			dst_area = dst_test_area;
		}
	}
	
	src->N = src_idx;
	dst->N = dst_idx;
	dst->h = src->h;

	split_result_t result{};
	result.old_node_box = src_box;
	result.new_node_box = dst_box;
	result.new_node = dst;

	return result;
}

/// @brief Inserts a data entry into the subtree below a node. 
///
/// @note the assumption is made that every internal node has at least one leaf node, 
/// and every leaf node contains at least one entry.  I.e., there may be no empty nodes.
split_result_t node_insert_recursive(rtree_t *tree, rtree_node_t *node, uint8_t lvl, rtree_entry_t entry)
{ 
	// Terminate recursion and insert the entry when the correct level is reached
	if (node->h == lvl)
	{
		node_insert_entry(node,entry);
	}
	else 
	{
		uint8_t i = smallest_overlap_index(node,entry.rect);
		
		// not a leaf, so entry points to another node
		rtree_entry_t cur_entry = node->entries[i];
		split_result_t result = node_insert_recursive(tree,cur_entry.node,lvl,entry);

		// Insertion required a split of the entries node. Recompute the
		// mbr for the split entry
		if (result.new_node)
		{
			node->entries[i].rect = result.old_node_box;
			
			rtree_entry_t new_entry{};
			new_entry.rect = result.new_node_box;
			new_entry.node = result.new_node;

			node_insert_internal(node,new_entry); 
		}
		// Otherwise, just expand the bounding box
		else  
		{
			node->entries[i].rect = aabb_add(node->entries[i].rect,entry.rect);
		}
	}

	split_result_t result{};

	// On overflow, create a new node of the same type and distribute the contents.
	if (node->N == RTREE_NODE_CAPACITY)
	{ 
		rtree_node_t *new_node = node_create(tree);
		result = node_split_contents_linear(node,new_node);
	}

	return result;
}

void sort_by_hilbert_code(const rtree_entry_t *in, rtree_entry_t *out, size_t count) 
{
	scalar_t x_min, x_max, y_min, y_max;

	x_min = y_min = FLT_MAX;
	x_max = y_max = -FLT_MAX;

	for (size_t i = 0; i < count; ++i) {
		aabb_t rect = in[i].rect;
		x_min = fmin(rect.x0,x_min);
		x_max = fmax(rect.x1,x_max);
		y_min = fmin(rect.y0,y_min);
		y_max = fmax(rect.y1,y_max);
	}

	scalar_t dx = x_max - x_min;
	scalar_t dy = y_max - y_min;

	dx = fmax(dx,dy);
	dy = dx;

	scalar_t ox = x_min;;
	scalar_t oy = y_min;;

	scalar_t sx = 1.f/(fabs(dx) < 1e-6 ? 1 : dx);
	scalar_t sy = 1.f/(fabs(dy) < 1e-6 ? 1 : dy);

	std::vector<bsort_kv_t<uint64_t>> keyval (count);

	// Order of the space filling curve used to sort the values
	constexpr int curve_order = 16;

	for (size_t i = 0; i < count; ++i) {
		rtree_entry_t ent = in[i];

		scalar_t x_med = sx*(0.5f*(ent.rect.x1 + ent.rect.x0) - ox);
		scalar_t y_med = sy*(0.5f*(ent.rect.y1 + ent.rect.y0) - oy);

		uint32_t code = hilbert_index_u32_f32((float)x_med, (float)y_med, curve_order);

		keyval[i].key = code;
		keyval[i].val = i;
	}

	bsort<uint64_t>(keyval.data(), keyval.size(), 4);
	
	for (size_t i = 0; i < count; ++i) {
		out[i] = in[keyval[i].val];
	}
}

}; // namespace

void rtree_insert_at(rtree_t *tree, rtree_entry_t entry, uint8_t lvl)
{
	rtree_node_t *root_node = tree->root.node;

	// Tree is empty
	if (!root_node) {
		tree->root.rect = entry.rect;

		rtree_node_t *leaf = node_create(tree);
		leaf->entries[0] = entry;

		root_node = tree->root.node = node_create(tree);
		root_node->entries[0].rect = entry.rect;
		root_node->entries[0].node = leaf;

		root_node->N = leaf->N  = 1;
		root_node->h = 1;

		return;
	}

	split_result_t result = node_insert_recursive(tree,root_node,lvl,entry);

	// Root has split; create a new root.
	if (result.new_node)
	{
		rtree_node_t *new_root = node_create(tree);

		new_root->h = root_node->h + 1;

		rtree_entry_t n1 {};
		n1.rect = result.old_node_box;
		n1.node = root_node;

		rtree_entry_t n2 {};
		n2.rect = result.new_node_box;
		n2.node = result.new_node;

		node_insert_internal(new_root,n1);
		node_insert_internal(new_root,n2);

		tree->root.node = new_root;

	}

	tree->root.rect = aabb_add(tree->root.rect,entry.rect);	
}

bool node_remove_entry(rtree_node_t *node, rtree_entry_t ent) {
	for (uint8_t i = 0; i < node->N; ++i) {
		if (!memcmp(node->entries + i, &ent, sizeof(rtree_entry_t))) {
			node->entries[i] = node->entries[--node->N];
			return true;
		}
	}
	return false;
}

remove_result_t node_remove_recursive(rtree_t *tree, rtree_node_t *node, rtree_entry_t entry)
{
	remove_result_t prev_result{};

	if (is_leaf(node) && node_remove_entry(node, entry)) {
		prev_result.removed = true;
	} else {
		for (uint8_t i = 0; i < node->N; ++i) {
			rtree_entry_t child = node->entries[i];
			
			if (aabb_contains(child.rect, entry.rect)) {
				prev_result = node_remove_recursive(tree,child.node,entry);
			}

			// If previous node fell below the minimum fill and added it's 
			// entries into the reinsertion list, remove it.
			//
			// Note that child gets freed before this point.
			if (prev_result.reinsert) { 
				node->entries[i] = node->entries[--node->N];
				break;
			} 
			// Otherwise, just update the mbr
			else if (prev_result.removed) {
				node->entries[i].rect = compute_mbr(child.node->entries,child.node->N);
				break;
			}
		}
	}

	// propagate the reinsertion list and removal status upwards.
	remove_result_t result {};
	result.list = prev_result.list;
	result.removed = prev_result.removed;

	uint8_t N = node->N;

	// If child count is less than the minimum, add this node's entries
	// to a list of nodes to be reinserted. 
	//
	// This will also trigger a removal of this node from the parent's array, 
	// and delete the old node.
	if (node != tree->root.node && N < RTREE_NODE_MINFILL) {
		reinsert_list_t *new_result = (reinsert_list_t*)
			bump_allocator_alloc(&tree->scratch,sizeof(reinsert_list_t)); 

		assert(new_result);

		memcpy(new_result->arr, node->entries, N * sizeof(rtree_entry_t));

		new_result->count = N;
		new_result->height = node->h;
		new_result->next = result.list;
		result.list = new_result;

		result.reinsert = true;

		node_free(tree, node);
	} 

	return result;
}

//--------------------------------------------------------------------------------------------------
// INTERFACE

rtree_t *rtree_create()
{
	rtree_t *tree = new rtree_t{};
	return tree;
}

int rtree_bulk_reload(rtree_t *tree, const rtree_entry_t* entries, size_t count, bool presorted)
{
	if (!count) return -1;

	// clear the tree if it has data (but not the allocators).
	if (tree->root.node) {
		node_free_rec(tree,tree->root.node);
		tree->root.node = nullptr;
	}

	std::vector<rtree_entry_t> tmp_vector (count);
	std::vector<rtree_entry_t> sorted (count);

	if (presorted) 
		memcpy(sorted.data(), entries, count*sizeof(rtree_entry_t));
	else 
   		sort_by_hilbert_code(entries, sorted.data(), count);

	rtree_entry_t *tmp = tmp_vector.data();
	rtree_entry_t *data = sorted.data();

	uint8_t max_fill = static_cast<uint8_t>(7*RTREE_NODE_CAPACITY/10);

	// Number of nodes at the current tree level.
	size_t curr_count = count;
	//size_t ctr = 0;

	uint8_t h = 0;
	do {
		size_t next_count = (curr_count + max_fill - 1)/max_fill;

		// Create the set of nodes for the current level.
		size_t idx = 0;
		for (size_t i = 0; i < next_count; ++i) {

			uint8_t N = (idx + max_fill <= curr_count) ? 
				max_fill : static_cast<uint8_t>(curr_count % max_fill);

			rtree_node_t* node = node_create(tree);
			memcpy(node->entries, data + idx, N*sizeof(rtree_entry_t));
			node->N = N;

			node->h = h;

			tmp[i].rect = compute_mbr(node->entries,N);
			tmp[i].node = node;

			idx += N;
			//ctr += N;
		}

		assert(next_count);
		sort_by_hilbert_code(tmp, data, next_count);

		curr_count = next_count;
		h++;
	} while (curr_count > max_fill);

	// Create the root. Since cur_count == 0 here, prev_count < NODE_CAPACITY
	uint8_t N = static_cast<uint8_t>(curr_count);

	rtree_node_t* node = node_create(tree);
	memcpy(node->entries, data, N*sizeof(rtree_entry_t));
	node->N = N;
	node->h = h;

	tree->root.rect = compute_mbr(node->entries,N);
	tree->root.node = node;

	return 0;
}

void rtree_free(rtree_t *tree)
{
	node_free_rec(tree,tree->root.node);
	bump_allocator_destroy(&tree->scratch);

	//delete tree;
}

rtree_t::~rtree_t() {
	rtree_free(this);
}

void rtree_insert(rtree_t *tree, rtree_entry_t entry)
{
	rtree_insert_at(tree,entry,0);
}

bool rtree_remove(rtree_t *tree, rtree_entry_t entry)
{
	rtree_node_t *root = tree->root.node;

	bump_allocator_reserve(&tree->scratch, (root->h + 1) * sizeof(reinsert_list_t));

	remove_result_t result = node_remove_recursive(tree, root, entry); 

	if (!result.removed)
		return false;

	if (!root->N) {
		node_free(tree,root);
		tree->root.node = nullptr;

	} else if (root->N == 1 && root->h > 1) {
		// mbr would already be computed in this case
		rtree_entry_t new_root = root->entries[0];
		tree->root = new_root;
		node_free(tree,root);

	} else {
		tree->root.rect = compute_mbr(root->entries,root->N);
	}

	reinsert_list_t *list = result.list;

	while (list) {
		for (uint8_t i = 0; i < list->count; ++i) {
			rtree_insert_at(tree, list->arr[i], list->height);
		}
		list = list->next;
	}

	bump_allocator_clear(&tree->scratch);

	return true;
}

