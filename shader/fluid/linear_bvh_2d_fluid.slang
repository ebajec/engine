module linear_bvh_2d_fluid;
import linear_bvh;
import fluid_defs_2d;

typedef LinearBVHHeader<FluidParticle, 2> ParticleBVHHeader ;

[vk::push_constant]
ConstantBuffer<ParticleBVHHeader> pc;

public struct ParticleBVH : ILinearBVH
{
	public static const uint N = 2;
	public static const uint GroupSize = 128;
	public static const uint InternalSize = 16;

	typealias Element = FluidParticle;

	public static ParticleBVHHeader header() {return pc;}
	public static AABB<2> bounds(FluidParticle part)
	{
		AABB<2> box;
		[ForceUnroll]
		for (int i = 0 ; i < 2; ++i) {
			box.min[i] = part.pos[i];
			box.max[i] = part.pos[i];
		}
		return box;
	}
}

[shader("compute")][numthreads(ParticleBVH.GroupSize, 1, 1)]
void compute_level(uint3 d: SV_DispatchThreadID, uint g: SV_GroupIndex) { linear_bvh_compute_level<ParticleBVH>(d,g); }

