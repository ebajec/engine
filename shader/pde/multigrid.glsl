
#define MAX_MIPS 6
#define JACOBI_ITERATIONS 4
#define GROUPS 32
#define OUTPUT_W (GROUPS - 2*JACOBI_ITERATIONS)

layout (local_size_x = GROUPS, local_size_y = GROUPS, local_size_z = 1) in;

// Initial guess for phi (lhs)
layout (r32f, binding = 1) readonly uniform image2D in_lhs;
layout (r32f, binding = 2) uniform image2D tmp_lhs;
layout (r32f, binding = 3) writeonly uniform image2D out_lhs;
layout (r32f, binding = 4) readonly uniform image2D in_rhs;

layout (binding = 5) uniform sampler2D bd_mask;


shared float block[GROUPS][GROUPS];
shared bool boundary[GROUPS][GROUPS];

layout (binding = 0) uniform ubo {
	uint N;
	uint u_level;
};

bool test_boundary(ivec2 idx, uint level)
{
	ivec2 size = imageSize(in_lhs) / (1 << level);

	bool outside = 
		idx.x < 0 || idx.y < 0 ||
		idx.x > size.x - 1 || idx.y > size.y - 1;

	return outside; 
}


float jacobi_lhs(vec4 nbr, bvec4 bd, float rhs, float h)
{
	float denom = dot(vec4(not(bd)), vec4(1));

	if (denom == 0)
		return 0;

	return (dot(nbr, vec4(not(bd))) - h*h*rhs)/denom;
}

float jacobi_it(ivec2 idx, float rhs, float h)
{
	ivec2 l_idx = clamp(idx + ivec2(-1,0), ivec2(0), ivec2(GROUPS - 1));
	ivec2 r_idx = clamp(idx + ivec2(1,0), ivec2(0), ivec2(GROUPS - 1));
	ivec2 t_idx = clamp(idx + ivec2(0,1), ivec2(0), ivec2(GROUPS - 1));
	ivec2 b_idx = clamp(idx + ivec2(0,-1), ivec2(0), ivec2(GROUPS - 1));

	float l = block[l_idx.x][l_idx.y];
	float r = block[r_idx.x][r_idx.y];
	float t = block[t_idx.x][t_idx.y];
	float b = block[b_idx.x][b_idx.y];

	bool bl = boundary[l_idx.x][l_idx.y];
	bool br = boundary[r_idx.x][r_idx.y];
	bool bt = boundary[t_idx.x][t_idx.y];
	bool bb = boundary[b_idx.x][b_idx.y];

	float denom = (
		float(!bl) + float(!br) + 
		float(!bb) + float(!bt)
	);

	if (denom == 0)
		return 0;

	return (
		mix(l,0,float(bl)) + 
		mix(r,0,float(br)) + 
		mix(b,0,float(bb)) + 
		mix(t,0,float(bt)) - 
		h*h*rhs)/denom;
}


