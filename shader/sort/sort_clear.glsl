#version 460 core

void main()
{
	const uint idx = gl_GlobalInvocationID.x;
	if (idx >= pc.count)
		return;

	switch (pc.sort.clear_mode)
	{
	}

}
