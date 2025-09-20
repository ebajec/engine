#include "misc.h"
#include <GL/glew.h>
#include <iostream>
#include "defines.h"

/**********************************************************************************
 * 
 * Helper functions
 * 
 ***********************************************************************************/
vec3 S2(GLfloat phi, GLfloat theta) 
{
    return vec3{sin(theta)*cos(phi),sin(theta)*sin(phi),cos(theta)};
}

mat3 rotation(vec3 axis, float angle) 
{   
    axis = normalize(axis);
    mat3 differential = mat3(
         0,         -axis.z,   axis.y,
         axis.z,   0,         -axis.x,
        -axis.y,   axis.x,   0
    );

    mat3 rotation = exp(mat3(angle*differential));
    return rotation;
} 

int modulo(int dividend, int divisor) {
	return (dividend % divisor + divisor) % divisor;
}

bool isFloat(GLenum type)
{
    return 
        type == GL_FLOAT      ||
        type == GL_HALF_FLOAT ||
        type == GL_FIXED;     
}
bool isIntegral(GLenum type)
{
    return 
        type == GL_INT            ||
        type == GL_BYTE           ||
        type == GL_SHORT          ||
        type == GL_UNSIGNED_INT   ||
        type == GL_UNSIGNED_BYTE  ||
        type == GL_UNSIGNED_SHORT;
}

void printData(GLfloat* data, int x, int y) 
{
    for (int i = 0; i < x; i++) {
        for (int j = 0; j < y; j++) {
            std::cout << data[i*y + j] << " ";
        }
        std::cout << "\n";
    }
}

float uRand() 
{
    return static_cast<float>(rand())/static_cast<float>(RAND_MAX);

}