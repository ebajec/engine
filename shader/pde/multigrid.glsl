#define MAX_MIPS 6
#define GROUPS 32
#define OMEGA 0.67

#define DELTA_X 0.5f

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
	uint u_iterations;
};

bool test_boundary(ivec2 idx, uint level)
{
	ivec2 size = imageSize(in_lhs) / (1 << level);

	bool outside = 
		idx.x < 0 || idx.y < 0 ||
		idx.x > size.x - 1 || idx.y > size.y - 1;

	return outside; 
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
	// zero normal derivative.  

	float center = block[idx.x][idx.y];

	float sum = 0;
	float den = 0;

	for (int i = 0; i < 4; ++i) {
		ivec2 p = stencil[i];
		bool valid = 
			p.x >= 0 && p.x < GROUPS &&
			p.y >= 0 && p.y < GROUPS;

		if (valid)
			valid = !boundary[p.x][p.y];

		float mask = float(valid);

		den += float(mask);
		sum += valid ? block[p.x][p.y] : 0; 
	}

	float u_next = den > 0 ? (sum - h*h*rhs)/den : 0;

	return mix(center, u_next, OMEGA);
}


