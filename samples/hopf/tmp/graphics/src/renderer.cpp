#include "renderer.h"

int* MultiIndex::firsts()
{
    return indices.data();
}

int* MultiIndex::counts()
{
    return indices.data() + indices.size()/2;;
}

int MultiIndex::totalCount()
{
    return indices.size()/2;
}


Buffer::Buffer() : m_size(0) , m_id(0)
{
   glGenBuffers(1,&m_id);
}

Buffer::Buffer(size_t size,GLenum usage) : m_size(size) , m_id(0)
{
    glBindBuffer(GL_ARRAY_BUFFER,m_id);
    glBufferData(GL_ARRAY_BUFFER,size,nullptr,usage);
    glBindBuffer(GL_ARRAY_BUFFER,0);
}

Buffer::~Buffer()
{
    glDeleteBuffers(1,&m_id);
}

void Buffer::uploadData(void* data, size_t size, GLenum usage)
{
    glBindBuffer(GL_ARRAY_BUFFER,m_id);
    if (size < m_size)
        glBufferSubData(GL_ARRAY_BUFFER,0,size,data);
    else 
        glBufferData(GL_ARRAY_BUFFER,size,data,usage);
    m_size = size;
    glBindBuffer(GL_ARRAY_BUFFER,0);
}

void Buffer::reserve(size_t size,GLenum usage)
{
    glBindBuffer(GL_ARRAY_BUFFER,m_id);
    glBufferData(GL_ARRAY_BUFFER,size,nullptr,usage);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    m_size = size;
}

Renderer::Renderer() 
{
}

void Renderer::setShaderManager(ShaderManager *shaderManager)
{
    shaders = shaderManager;
}
