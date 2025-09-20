#ifndef MISC_H
#define MISC_H

#include <GL/glew.h>
#include <vector>
#include "defines.h"

/**********************************************************************************
 * 
 * Helper functions
 * 
 ***********************************************************************************/
extern vec3 S2(GLfloat phi, GLfloat theta) ;
extern mat3 rotation(vec3 axis, float angle);
extern int modulo(int dividend, int divisor);
extern bool isFloat(GLenum type);
extern bool isIntegral(GLenum type);
extern void printData(GLfloat* data, int x, int y);
extern float uRand();

/**********************************************************************************
 * 
 * Templates
 * 
 ***********************************************************************************/
template<typename T>
static inline constexpr unsigned int factorial(T x) {
    return (x > 0) ? x*factorial(x-(T)1) : (T)1;
}
template<int n,typename T>
static inline constexpr glm::mat<n,n,T> exp(glm::mat<n,n,T> mat) {
    auto result = glm::mat<n,n,T>(1.0f);// 0
    result = result + mat;//1
    mat = mat*mat;//2
    result = result + T(pow(factorial(2.0f),-1))*mat; 
    mat = mat*mat;//3
    result = result + T(pow(factorial(3.0f),-1))*mat;
    mat = mat*mat;//4
    result = result + T(pow(factorial(4.0f),-1))*mat;
    return result;
}
template<typename T> 
static inline std::vector<T> lerp(T start, T end, int steps) 
{
    std::vector<T>  out(steps+1);   
    T diff = end - start;
    float step = pow(steps,-1);

    for (int i = 0; i < steps + 1; i++) {
        out[i] = start + (i*step)*diff;
    }
    return out;
}
#endif