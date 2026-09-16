// Specializes shader/sort/sort.slang for FluidParticle, keyed by the Morton
// code of the particle's cell. Compiles on its own; slangc picks up all four
// entry points without being told their names:
//
//   slangc -I../sort sort_particles.slang -target spirv -o sort_particles.comp.spv
module sort_particles;

import fluid_defs_2d;
import sort;

public struct SortParticlesPushConstants
{
	public TSortPushConstantHeader<FluidParticle> sort;
	public float2 scale;
}

[vk::push_constant]
ConstantBuffer<SortParticlesPushConstants> pc;

uint spread2(uint x)
{
	x &= 0xFFFF;
	x = (x | (x << 8)) & 0x00FF00FF;
	x = (x | (x << 4)) & 0x0F0F0F0F;
	x = (x | (x << 2)) & 0x33333333;
	x = (x | (x << 1)) & 0x55555555;
	return x;
}

uint morton2(uint x, uint y)
{
	return spread2(x) | (spread2(y) << 1);
}

uint hilbert2(uint x, uint y)
{
	x &= 0xFFFF;
	y &= 0xFFFF;

	uint d = 0;

	[ForceUnroll]
	for (uint s = 0x8000; s > 0; s >>= 1)
	{
		uint rx = bool(x & s) ? 1 : 0;
		uint ry = bool(y & s) ? 1 : 0;

		d += s * s * ((3 * rx) ^ ry);

		if (ry == 0) {
			if (rx == 1) {
				x = 0xFFFF - x;
				y = 0xFFFF - y;
			}
		}
		uint t = x;
		x = y;
		y = t;
	}

	return d;
}

public struct ParticleSortSource : ISortSource
{
	public typealias Element = FluidParticle;

	public static uint getKey(FluidParticle part)
	{
		uint ix = uint(floor(part.pos.x));
		uint iy = uint(floor(part.pos.y));

		return hilbert2(ix, iy);
	}

	public static TSortPushConstantHeader<Element> header()
	{
		return pc.sort;
	}
}

typealias S = ParticleSortSource;

[shader("compute")][numthreads(CLEAR_GROUP_SIZE, 1, 1)]
void clear_offsets(uint3 d: SV_DispatchThreadID) { sort_clear_offsets<S>(d); }

[shader("compute")][numthreads(CLEAR_GROUP_SIZE, 1, 1)]
void clear_tile_info(uint3 d: SV_DispatchThreadID) { sort_clear_tile_info<S>(d); }

[shader("compute")][numthreads(TILE_SIZE, 1, 1)]
void count(uint3 d: SV_DispatchThreadID, uint g: SV_GroupIndex) { sort_count<S>(d, g); }

[shader("compute")][numthreads(TILE_SIZE, 1, 1)]
void scatter(uint3 d: SV_DispatchThreadID, uint g: SV_GroupIndex) { sort_scatter<S>(d, g); }
