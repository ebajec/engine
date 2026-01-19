# Steps towards functional API

- Add support for generating vertex attribute layouts from either
vertex shader reflections or specified in a YAML

- Add support for all the different pipeline state in the pipeline YAML
files.  I.e., cull state, depth test, alpha blend, topology, index type.

- Add an API to execute a multidraw indirect call while a pipeline is bound,
which should realistically allow me to submit any kind of draws I want

- Add an API for executing compute shaders.  This would just be a simple dispatch.

# Next steps after functional API is complete

- First, make sure that I can run my examples without making ANY gl calls.
The Vulkan implementation can then be tested without the need to modify other 
stuff.

- First focus should be on synchronization.  Experiment with using timeline
semaphores and storing counter values on resources.  1 timeline semaphore per
queue could work well, especially for uploads.
