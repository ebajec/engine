#include <glad/glad.h>

#include <utils/log.h>

static inline uint32_t gl_check_err() 
{
    GLenum error = glGetError();

    if (!error) return error;

    switch (error) {
        case GL_INVALID_ENUM:      
            log_error( "Invalid enum\n");
            return error;
        case GL_INVALID_VALUE:     
            log_error( "Invalid value\n");
            return error;
        case GL_INVALID_OPERATION: 
            log_error( "Invalid operation\n");
            return error;
        case GL_STACK_OVERFLOW:    
            log_error( "Stack overflow\n");
            return error;
        case GL_STACK_UNDERFLOW:   
            log_error("Stack underflow\n");
            return error;
        case GL_OUT_OF_MEMORY:     
            log_error("Out of memory\n");
            return error;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
           log_error("Invalid framebuffer operation\n");
           return error;
    }

	return error;
}
