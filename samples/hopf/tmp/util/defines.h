#ifndef DEFINES_H
#define DEFINES_H

#include <glm/glm.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GL/glew.h>
#include <glm/detail/type_vec2.hpp>

#define FIBER_COUNT 1000   // Number of fibers to compute
#define FIBER_SIZE 400       // Number of samples in each fiber

#ifndef PI
#define PI 3.141592654f
#endif

#ifndef ROOT2
#define ROOT2 1.4142135623f
#endif 

// For debugging - copies the buffer at [id] to an array [data] of [size] bytes 
#define SHOW_BUFFER(data, size, id)\
glBindBuffer(GL_ARRAY_BUFFER,id);\
float data[size];\
{\
    void * out = glMapBufferRange(GL_ARRAY_BUFFER,0,size*sizeof(float), GL_MAP_READ_BIT);\
    memcpy(data,out,size*sizeof(float));\
    glUnmapBuffer(GL_ARRAY_BUFFER);\
    glBindBuffer(GL_ARRAY_BUFFER,0);\
}

#define LENGTH(size,type) (size/sizeof(type))

typedef glm::ivec2 ivec2;
typedef glm::vec4 vec4;
typedef glm::vec3 vec3;
typedef glm::vec2 vec2;
typedef glm::mat4 mat4;
typedef glm::mat3 mat3;
typedef glm::mat2 mat2;

#endif
