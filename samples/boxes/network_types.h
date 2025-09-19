#ifndef PLAN_NETWORK_TYPES_H
#define PLAN_NETWORK_TYPES_H

#include "aabb.h"

#include <cstdint>

struct edge_t 
{
	alignas(8) 

	uint32_t u;
	uint32_t v;

	edge_t() = default;
	edge_t (uint32_t _u, uint32_t _v) : u(_u), v(_v) {}

	constexpr bool operator == (const edge_t &other) const {
		return (u == other.u && v == other.v) || (u == other.v && v == other.u);
	}
};

struct segment_t 
{
	vec2 p0;
	vec2 p1;
};

enum node_highway_type_t
{
	NODE_HIGHWAY_TYPE_NONE,
	NODE_HIGHWAY_TYPE_MOTORWAY_JUNCTION,
	NODE_HIGHWAY_TYPE_GIVE_WAY,
	NODE_HIGHWAY_TYPE_STOP,
	NODE_HIGHWAY_TYPE_CROSSING,
	NODE_HIGHWAY_TYPE_TRAFFIC_SIGNALS,
	NODE_HIGHWAY_TYPE_TURNING_CIRCLE,
};

enum node_barrier_type_t
{
	NODE_BARRIER_TYPE_NONE,
	NODE_BARRIER_TYPE_BOLLARD,
	NODE_BARRIER_TYPE_GATE,
	NODE_BARRIER_TYPE_BLOCK,
	NODE_BARRIER_TYPE_CHAIN,
	NODE_BARRIER_TYPE_CURB,
	NODE_BARRIER_TYPE_TOLL_BOOTH,
};

struct node_static_attrs_t
{
	alignas(8)

    uint16_t maxheight_cm;  // height in cm 
    uint8_t maxwidth_dm;    // width in decimeters
	
	unsigned int access				 : 1;
	unsigned int has_yield_sign      : 1;  
	unsigned int is_roundabout       : 1;
	unsigned int motor_vehicle		 : 1;
	node_highway_type_t highway 	 : 3; // 7 types
	node_barrier_type_t barrier_type : 3; // 7 types
	
    unsigned int crossing_type		 : 2;
    unsigned int reserved			 : 6;
};

enum edge_highway_type_t 
{
	EDGE_HIGHWAY_TYPE_UNCLASSIFIED,
	EDGE_HIGHWAY_TYPE_PRIMARY,
	EDGE_HIGHWAY_TYPE_SECONDARY,
	EDGE_HIGHWAY_TYPE_TERTIARY,
	EDGE_HIGHWAY_TYPE_MOTORWAY,
	EDGE_HIGHWAY_TYPE_RESIDENTIAL,
	EDGE_HIGHWAY_TYPE_FOOTWAY,
	EDGE_HIGHWAY_TYPE_CYCLEWAY,
	EDGE_HIGHWAY_TYPE_SERVICE,
	EDGE_HIGHWAY_TYPE_CONSTRUCTION,
};

enum surface_type_t
{
	SURFACE_TYPE_ASPHALT,
};

enum crossing_type_t 
{
	CROSSING_TYPE_UNMARKED,
	CROSSING_TYPE_UNCONTROLLED,
	CROSSING_TYPE_TRAFFIC_SIGNALS
};

enum oneway_type_t
{
	ONEWAY_NONE,
	ONEWAY_FORWARD,
	ONEWAY_BACKWARD
};

struct edge_static_attrs_t
{
	alignas(8)
	uint16_t max_speed; // 10 * km/h
	
	unsigned int lanes		  		: 5;  // max 32 lanes
	
	unsigned int is_bridge			: 1;
	unsigned int is_tunnel			: 1;
	unsigned int is_link			: 1;
	unsigned int is_driveable       : 1;
	unsigned int is_walkable 		: 1;
	unsigned int is_cycleable 		: 1;
	oneway_type_t oneway			: 2;  // 0=no, 1=yes, 2=-1
	surface_type_t surface			: 4;  // up to 16 types
	edge_highway_type_t highway		: 4;
};

struct edge_idx_t
{
	alignas (8)

	uint32_t edge_id;
	uint32_t geom_id;

	edge_idx_t() = default;
	edge_idx_t(uint32_t eid, size_t gid) {
		edge_id = eid;
		geom_id = static_cast<uint32_t>(gid);
	}
};

//------------------------------------------------------------------------------
// in-header definitions

static inline edge_idx_t edge_index_from_u64(uint64_t code) 
{
	edge_idx_t idx;
	idx.edge_id = static_cast<uint32_t>((code & 0xFFFFFFFF00000000) >> 32);
	idx.geom_id = static_cast<uint32_t>((code & 0x00000000FFFFFFFF));
	return idx;
}

static inline uint64_t u64_from_edge_index(edge_idx_t idx) 
{
	uint64_t code = 0;
	code |= static_cast<uint64_t>(idx.edge_id) << 32;
	code |= static_cast<uint64_t>(idx.geom_id);
	return code;
}

static inline void vec2_minmax(const vec2* points, size_t count, vec2* min, vec2* max) 
{
	scalar_t x_min = FLT_MAX, x_max = -FLT_MAX, y_min = FLT_MAX, y_max = -FLT_MAX;

	for (uint32_t i = 0; i < count; ++i) {
		x_min = fmin(points[i].x,x_min);
		x_max = fmax(points[i].x,x_max);
		y_min = fmin(points[i].y,y_min);
		y_max = fmax(points[i].y,y_max);
	}

	*min = vec2(x_min,y_min);
	*max = vec2(x_max,y_max);
}

static inline bool aabb_intersects_segment(aabb_t a, segment_t s)
{
	if (aabb_contains(a, s.p0) || aabb_contains(a, s.p1))
		return true;

	if (fmin(s.p0.x,s.p1.x) > a.x1 || fmax(s.p0.x,s.p1.x) < a.x0 || 
		fmin(s.p0.y,s.p1.y) > a.y1 || fmax(s.p0.y,s.p1.y) < a.y0)
		return false;

	vec2 d = s.p1 - s.p0;

	scalar_t sx = fabs(d.x) < FLT_EPSILON ? copysign(FLT_MAX,d.x) : 1.0f/d.x; 
	scalar_t sy = fabs(d.y) < FLT_EPSILON ? copysign(FLT_MAX,d.y) : 1.0f/d.y; 

	scalar_t t[4] = {
		sx*(a.x0 - s.p0.x),
		sy*(a.y0 - s.p0.y),
		sx*(a.x1 - s.p0.x),
		sy*(a.y1 - s.p0.y)
	};

	scalar_t t0 = FLT_MAX;
	scalar_t t1 = FLT_MAX;

	for (int i = 0; i < 4; ++i)
		t0 = fmin(t[i],t0);
	for (int i = 0; i < 4; ++i)
		t1 = (t[i] == t0) ? t1 : fmin(t[i],t1);

	return t0 <= 1 && t1 <= 1;
}

static inline scalar_t segment_point_dist_sq(segment_t s, vec2 p) 
{
	vec2 u = s.p1 - s.p0;

	vec2 d0 = p - s.p0; 
	if (dot(d0,u) < 0) {
		return dot(d0,d0);
	}

	vec2 d1 = p - s.p1; 
	if (dot(d1,u) > 0) { 
		return dot(d1,d1);
	}

	vec2 v = vec2(u.y,-u.x);

	scalar_t norm_v = dot(v,v);
	scalar_t prod_v = dot(d0,v);

	return norm_v > FLT_EPSILON ? prod_v*(prod_v/norm_v) : dot(d0,d0);
}

static inline vec2 segment_nearest(segment_t s, vec2 p) 
{
	vec2 u = s.p1 - s.p0;

	vec2 d = p - s.p0; 

	scalar_t norm_u = dot(u,u);
	scalar_t prod_u = std::clamp(dot(d,u),0.,norm_u);

	return norm_u > FLT_EPSILON ? s.p0 + u*(prod_u/(norm_u)) : s.p0;
}

static inline bool segment_intersects(segment_t s1, segment_t s2, vec2 *out) {
	vec2 p0 = s1.p0; 
	vec2 p1 = s2.p0;
	
	vec2 v0 = s1.p1 - p0;
	vec2 v1 = s2.p1 - p1;

	vec2 dp = p1 - p0;

	scalar_t det = v0.y*v1.x - v0.x*v1.y;

	if(fabs(det) < FLT_EPSILON) {
		if (fabs(dot(dp,vec2(v0.y,-v0.x))) > FLT_EPSILON)
			return false;

		if (dot(dp,dp) < dot(v0,v0)) {
			*out = p1;
			return true;
		}
	
		return false;
	}

	scalar_t denom = 1.0f/det;
	
	scalar_t t0 = (-v1.y*dp.x + v1.x*dp.y)*denom;
	scalar_t t1 = (-v0.y*dp.x + v0.x*dp.y)*denom;

	if (t0 < 0.f || t0 > 1.f || t1 < 0.f || t1 > 1.f)
		return false;

	*out = p0 + t0*v0;

	return true;
}


#endif
