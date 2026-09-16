#define MAX_MIPS 8
#define GROUPS 16
#define OMEGA 0.66
#define DELTA_X 2.0f

#define OOB_CELL_THRES 1e-2

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

layout (local_size_x = GROUPS, local_size_y = GROUPS, local_size_z = 1) in;

layout (set = 0, r32f, binding = 1) uniform image2D tmp_lhs;
// Layout [R1_0, R1_1, ... R1_N]
layout (set = 0, r32f, binding = 3) uniform image2D R1[MAX_MIPS];

// Layout [R2_1, ... R2_N+1]
layout (set = 0, r32f, binding = 4) uniform image2D R2[MAX_MIPS];

// Initial guess for phi (lhs)
layout (set = 1, r32f, binding = 0) readonly uniform image2D in_lhs;
layout (set = 1, r32f, binding = 1) readonly uniform image2D in_rhs;
layout (set = 1, r32f, binding = 2) writeonly uniform image2D out_lhs;
layout (set = 1, r8, binding = 3) readonly uniform image2D solid_mask[MAX_MIPS];
layout (set = 1, r8_snorm, binding = 4) readonly uniform image2D fill_mask[MAX_MIPS];

shared float block[GROUPS][GROUPS];
shared uint8_t boundary[GROUPS][GROUPS];
shared int8_t air[GROUPS][GROUPS];

layout (push_constant, std430) uniform Inputs {
	uint N;
	uint u_level;
	uint u_iterations;
};

vec2 get_bd_mask(ivec2 idx, uint level)
{
	ivec2 size = imageSize(solid_mask[level]);

	if (any(lessThan(idx, ivec2(0))) || any(greaterThanEqual(idx, size)))
		return vec2(0.f);

	return vec2(
		imageLoad(solid_mask[level], idx).r,  
		imageLoad(fill_mask[level], idx).r  
	);
}

bool inbounds(ivec2 idx)
{
	return !( 
		any(lessThan(idx, ivec2(0))) || 
		any(greaterThanEqual(idx, ivec2(GROUPS))));
}

vec2 get_bd(ivec2 p)
{
	uint solid = boundary[p.x][p.y]; 
	int air = air[p.x][p.y]; 

	return vec2(
		float(solid) / 255.f, 
		float(air) / 128.f
	);
}

void set_bd(ivec2 p, vec2 bd)
{
	boundary[p.x][p.y] = uint8_t(255.f * bd.r);
	air[p.x][p.y] = int8_t(128.f * bd.g);
}

const float THETA = 0.632;
const float SIGMA = 0.066;

float face_frac(float phi_c, float phi_n)
{
	float tht = phi_c / (phi_c - phi_n); 
	return 1.f/max(tht, 0.01);
}

float LHS(bool is_fetch, ivec2 idx, out float wt, out float phi)
{
	if (is_fetch) {
		vec2 mask = get_bd_mask(idx,0);
		wt = mask.r;
		phi = mask.g;

		return (wt > 0.f) ? imageLoad(in_lhs, idx).r : 0;
	} else {
		bool inb = inbounds(idx); 
		if (!inb) {
			wt = 0;
			phi = 0;
			return 0;
		}

		vec2 mask = get_bd(idx);
		wt = mask.r;
		phi = mask.g;

		return block[idx.x][idx.y];
	}
}

float jacobi_it(bool is_fetch, ivec2 idx, float rhs, float u_prev, float h)
{
	ivec2 stencil[4] = {
		ivec2(-1,0),
		ivec2(1,0), 
		ivec2(0,1), 
		ivec2(0,-1)
	};

	float wt_c;
	float phi_c;
	float u_c = LHS(is_fetch, idx, wt_c, phi_c);

	if (phi_c > 0) {
		return 0;
	}

	float sum = 0.f;
	float den = 0.f;

	for (int i = 0; i < 4; ++i) {
		ivec2 p = idx + stencil[i];

		float wt;
		float phi;
		float u = LHS(is_fetch, p, wt, phi);

		const bool is_air = phi > 0.f;
		wt = is_air ? face_frac(phi_c, phi) / min(wt_c, wt) : wt; 

		den += wt;
		sum += is_air ? 0.f : wt * u;
	}

	if (is_fetch) {
		float u_next = den > 1e-3 ? (sum - h*h*rhs)/den : 0;
		return mix(u_c, u_next, OMEGA); 
	} else {
		float u_next = den > 1e-3 ? (sum - h*h*rhs)/den : 0;
		float u_cheb = u_c + THETA * (u_next - u_c) + SIGMA * (u_c - u_prev);
		return u_cheb;
	}
}


