#define MAX_MIPS 8
#define GROUPS 16
#define OMEGA 0.67
#define DELTA_X 0.5f

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
layout (set = 1, r8_snorm, binding = 3) readonly uniform image2D bd_mask[MAX_MIPS];

shared float block[GROUPS][GROUPS];
shared uint8_t boundary[GROUPS][GROUPS];

const uint AIR = 0xFF;

layout (push_constant, std430) uniform Inputs {
	uint N;
	uint u_level;
	uint u_iterations;
};

float get_bd_mask(ivec2 idx, uint level)
{
	ivec2 size = imageSize(bd_mask[level]);

	if (any(lessThan(idx, ivec2(0))) || any(greaterThanEqual(idx, size)))
		return 0.f;

	return imageLoad(bd_mask[level], idx).r; 
}

bool inbounds(ivec2 idx)
{
	return !( 
		any(lessThan(idx, ivec2(0))) || 
		any(greaterThanEqual(idx, ivec2(GROUPS))));
}

float get_bd(ivec2 p, out float fill)
{
	uint cell = boundary[p.x][p.y]; 
	if (cell == AIR) {
		fill = 0.f;
		return 1.f;
	}
	fill = 1.f;
	return float(cell) * (1.f/254.f); 
}

void set_bd(ivec2 p, float bd)
{
	if (bd < 0.f)
		boundary[p.x][p.y] = uint8_t(AIR);
	boundary[p.x][p.y] = uint8_t(254.f * bd);
}

float jacobi_it(ivec2 idx, float rhs, float h)
{
	ivec2 stencil[4] = {
		idx + ivec2(-1,0),
		idx + ivec2(1,0), 
		idx + ivec2(0,1), 
		idx + ivec2(0,-1)
	};

	// assuming neumann boundary conditions with
	// zero normal derivative toward out of bounds cells.  

	float fill_c = 0;
	float wt_c = get_bd(idx, fill_c);
	float c = block[idx.x][idx.y];

	float sum = 0.f;
	float den = 0.f;

	for (int i = 0; i < 4; ++i) {
		ivec2 p = stencil[i];
		bool inb = inbounds(idx); 

		if (!inb)
			continue;

		float fill;
		float wt = get_bd(p, fill);
		wt = min(wt_c, wt); 

		den += float(wt);
		sum += fill * wt * block[p.x][p.y];
	}

	float u_next = den > 1e-3 ? (sum - h*h*rhs)/den : 0;
	return fill_c * mix(c, u_next, OMEGA);
}


