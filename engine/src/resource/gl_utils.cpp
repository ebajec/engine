#include <resource/gl_utils.h>
#include <utils/log.h>

#include <vector>
#include <cstdio>

bool gl_check_program(GLuint handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &status);
    glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    if ((GLboolean)status == GL_FALSE)
        log_error("failed to link %s!\n", desc);
    if (log_length > 1)
    {
		std::vector<char> buf;
        buf.resize((size_t)(log_length + 1));
        glGetProgramInfoLog(handle, log_length, nullptr, (GLchar*)buf.data());
        fprintf(stderr, "%s\n", buf.data());
    }
    return (GLboolean)status == GL_TRUE;
}


