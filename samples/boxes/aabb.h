#ifndef AABB_H
#define AABB_H

#include <TerraLens/Spatial/api.h>

#include <algorithm>

#include <cfloat>
#include <math.h>

typedef double scalar_t;
struct vec2
{
	alignas (2*sizeof(scalar_t))

	scalar_t x;
	scalar_t y;

	vec2() = default;
	constexpr vec2(scalar_t _x, scalar_t _y) : x(_x), y(_y) {}
	constexpr vec2(TL_dvec2 v) : x(v.x), y(v.y) {}

	constexpr bool operator == (const vec2& other) const {
		return x == other.x && y == other.y;
	}

	constexpr vec2 operator - () const {
		return vec2(-x, -y);
	}

	constexpr vec2 operator * (const scalar_t s) const noexcept{
		return vec2(s*x, s*y);
	}

	constexpr vec2 operator + (const vec2& a) const noexcept{
		return vec2(x + a.x, y + a.y);
	}

	constexpr vec2 operator - (const vec2& a) const noexcept{
		return vec2(x - a.x, y - a.y);
	}
};

static inline scalar_t dot(vec2 a, vec2 b) {
	return a.x*b.x + a.y*b.y;
}

static inline vec2 normalize(vec2 v) {
	scalar_t norm = sqrt(v.x*v.x + v.y*v.y);	
	return norm < FLT_EPSILON ? vec2(0,0) : v;
}

static constexpr vec2 operator *(scalar_t s,const vec2& v) noexcept{
	return v*s;
}

struct aabb_t
{
	scalar_t x0, x1;
	scalar_t y0, y1;
};

static inline int aabb_empty(aabb_t a)
{
	return a.x1 < a.x0 || a.y1 < a.y0;
}

static inline int aabb_contains(aabb_t a, vec2 p)
{
	return (a.x0 <= p.x && p.x <= a.x1 
		&& a.y0 <= p.y && p.y <= a.y1);
}

static inline int aabb_contains(aabb_t a, aabb_t b)
{
	return (a.x0 <= b.x0 && b.x1 <= a.x1 
		&& a.y0 <= b.y0 && b.y1 <= a.y1);
}

static inline scalar_t aabb_dist_sq(aabb_t a, vec2 p) 
{
	scalar_t dx = fmax(a.x0 - p.x,fmax(p.x - a.x1, 0));
	scalar_t dy = fmax(a.y0 - p.y,fmax(p.y - a.y1, 0));

	return dx*dx + dy*dy;
}

static inline vec2 aabb_centroid(aabb_t a)
{
	return vec2 {0.5f*(a.x1 + a.x0), 0.5f*(a.y1 + a.y0)};
}

static inline scalar_t aabb_centroid_dist_sq(aabb_t a, vec2 p)
{
	vec2 c = aabb_centroid(a);
	vec2 d = {c.x - p.x, c.y - p.y};

	return d.x*d.x + d.y*d.y;
}


static inline aabb_t aabb_from_corners(vec2 p0, vec2 p1)
{
	aabb_t r;
	r.x0 = std::min(p0.x,p1.x);
	r.x1 = std::max(p0.x,p1.x);
	r.y0 = std::min(p0.y,p1.y);
	r.y1 = std::max(p0.y,p1.y);

	return r;
}

static inline aabb_t aabb_add(aabb_t a, aabb_t b)
{
	aabb_t r;
	r.x0 = std::min(a.x0, b.x0);
	r.x1 = std::max(a.x1, b.x1);
	r.y0 = std::min(a.y0, b.y0);
	r.y1 = std::max(a.y1, b.y1);
	return r;
}

static inline aabb_t aabb_add(aabb_t a, vec2 p)
{
	aabb_t r;
	r.x0 = std::min(a.x0, p.x);
	r.x1 = std::max(a.x1, p.x);
	r.y0 = std::min(a.y0, p.y);
	r.y1 = std::max(a.y1, p.y);
	return r;
}

static inline aabb_t aabb_intersect(aabb_t a, aabb_t b)
{
	aabb_t r;
	r.x0 = std::max(a.x0, b.x0);
	r.x1 = std::min(a.x1, b.x1);
	r.y0 = std::max(a.y0, b.y0);
	r.y1 = std::min(a.y1, b.y1);
	return r;
}

static inline scalar_t aabb_area(aabb_t a)
{
	return (a.x1 - a.x0)*(a.y1 - a.y0); 
}

static inline aabb_t aabb_bounding(const vec2 *points, size_t count) 
{
	aabb_t box = {points[0].x,points[0].x,points[0].y,points[0].y};

	for (size_t i = 0; i < count; ++i) {
		box = aabb_add(box, points[i]);
	}

	return box;
}

static inline aabb_t aabb_bounding(const aabb_t *boxes, size_t count)
{
	aabb_t bounding = boxes[0];
	for (size_t i = 1; i < count; ++i)
	{
		bounding = aabb_add(bounding,boxes[i]);
	}
	return bounding;
}

#endif // AABB_H
