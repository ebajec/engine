#ifndef RENDERER_H
#define RENDERER_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include "shader.h"
#include "camera.h"


/*************************************************************************
 * 
 * typedefs/structs
 * 
 *************************************************************************/

typedef unsigned int uint;
typedef struct 
{
    uint  count;
    uint  instanceCount;
    uint  first;
    uint  baseInstance;
} DrawArraysIndirectCommand;

typedef struct
{
    std::vector<int> indices;
    int totalCount();
    int* firsts();
    int* counts();
} MultiIndex;

/*************************************************************************
 * 
 * Buffer: Class Defintion
 * 
 *************************************************************************/

class Buffer
{
public:
    Buffer();
    Buffer(size_t size, GLenum usage);

    ~Buffer();

    template<typename T>
    void uploadData(std::vector<T> data, GLenum usage = GL_STREAM_DRAW);
    void uploadData(void * data, size_t size, GLenum usage = GL_STREAM_DRAW);

    void reserve(size_t size,GLenum usage = GL_STREAM_DRAW);

    const size_t size() const {return m_size;}
    const GLuint id() const {return m_id;}

private:
    GLuint m_id;
    size_t m_size;
};

template<typename T>
struct BufferMap 
{
public:
    BufferMap(Buffer& buffer, size_t offset, size_t length, GLbitfield access);
    ~BufferMap();
    void unmap();
    T operator [] (size_t index);

    T* data;
private:
    GLuint m_id;
};

/*************************************************************************
 * 
 * PrimitiveData: Class Defintion
 * 
 *************************************************************************/

template<typename vType>
class PrimitiveData
{
public:
    PrimitiveData();
    ~PrimitiveData();

    void uploadData(const std::vector<vType>& data, GLenum usage);
    void uploadData(const std::vector<vType>& data, const std::vector<uint>& indices, GLenum usage);

    void reserveAttribs(size_t count, GLenum usage = GL_STREAM_DRAW);
    void reserveIndices(size_t count, GLenum usage = GL_STREAM_DRAW);

    void attribPointer(GLuint location, GLint size, GLenum type, GLboolean normalized, 
    const void* pointer, GLuint buffer = -1);
    void attribIPointer(GLuint location, GLint size, GLenum type, const void* pointer);
    void attribLPointer(GLuint location, GLint size, GLenum type, const void* pointer);

    void bindArray() const; 
    void unbindArray() const;

    size_t attribCount() {return m_vbo.size()/sizeof(vType);}
    size_t indexCount() {return m_ebo.size()/sizeof(uint);}

    MultiIndex* getMultiDrawIndices();
    void setMultiDrawIndices(const MultiIndex& indices);
    void setMultiDrawIndices(MultiIndex&& indices);

    Buffer * vbo(){return &m_vbo;}
    Buffer * ebo(){return &m_ebo;}

private:
    GLuint m_vao;
    Buffer m_vbo;
    Buffer m_ebo;

    MultiIndex m_multiIndex;
};

/*************************************************************************
 * 
 * Renderer: Class Defintion
 * 
 *************************************************************************/

class Renderer
{
public:
    Renderer();
    ~Renderer() {}
    void setShaderManager(ShaderManager* shaderManager);
protected:
    ShaderManager * shaders;
};

/*************************************************************************
 * 
 * Buffer: Template defintions
 * 
 *************************************************************************/
template <typename T>
inline void Buffer::uploadData(std::vector<T> data, GLenum usage)
{
    if (!m_id) glGenBuffers(1,&m_id);
    
    size_t size = data.size()*sizeof(T);
    glBindBuffer(GL_ARRAY_BUFFER,m_id);

    if (size < m_size)
    {
        glBufferSubData(GL_ARRAY_BUFFER,0,size,data.data());   
    } 
    else 
    {
        glBufferData(GL_ARRAY_BUFFER,size,data.data(),usage);  
    }
    m_size = size;
    glBindBuffer(GL_ARRAY_BUFFER,0);
}

template<typename T>
BufferMap<T>::BufferMap(Buffer& buffer, size_t offset, size_t length, GLbitfield access)
{
    m_id = buffer.id();
    data = (T*)glMapNamedBufferRange(m_id,offset*sizeof(T),length*sizeof(T),access);
};

template<typename T>
BufferMap<T>::~BufferMap()
{
    glUnmapNamedBuffer(m_id);
};

template<typename T>
T BufferMap<T>::operator[](size_t index)
{   
    return data[index];
};

template<typename T>
void BufferMap<T>::unmap()
{
    glUnmapNamedBuffer(m_id);
    data = nullptr;
}

/*************************************************************************
 * 
 * PrimitiveData: Implementation of templates
 * 
 *************************************************************************/

template<typename vType>
PrimitiveData<vType>::PrimitiveData()
{
    glGenVertexArrays(1,&m_vao);
}

template<typename vType>
PrimitiveData<vType>::~PrimitiveData()
{
    glDeleteVertexArrays(1,&m_vao);
}

template<typename vType>
void PrimitiveData<vType>::uploadData(const std::vector<vType>& data, const std::vector<uint>& indices, GLenum usage)
{
    m_vbo.uploadData((void*)data.data(),data.size()*sizeof(vType),usage);
    m_ebo.uploadData((void*)indices.data(),indices.size()*sizeof(uint),usage);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m_ebo.id());
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
}

template<typename vType>
void PrimitiveData<vType>::reserveAttribs(size_t count, GLenum usage)
{
    glBindVertexArray(m_vao);
    m_vbo.reserve(count*sizeof(vType),usage);
    glBindVertexArray(0);
}

template<typename vType>
void PrimitiveData<vType>::reserveIndices(size_t count, GLenum usage)
{
    glBindVertexArray(m_vao);
    m_ebo.reserve(count*sizeof(uint),usage);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m_ebo.id());
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
}

template <typename vType>
void PrimitiveData<vType>::bindArray() const
{
    glBindVertexArray(m_vao);
}

template <typename vType>
void PrimitiveData<vType>::unbindArray() const
{
    glBindVertexArray(0);
}

template <typename vType>
void PrimitiveData<vType>::attribPointer(
        GLuint         location,   // Location of attribute in vertex array  
        GLint          size,       // Number of components in attribute (i.e., for vec4 this would be 4).
        GLenum         type,       // Data type.
        GLboolean      normalized, // Whether to normalize float data.
        const void*    pointer,     // Byte offset from beginning of VBO to first occurence of this attribute.
        GLuint         buffer
    )
{
    glBindVertexArray(m_vao);
    // if buffer was not specified, use the primitive data
    glBindBuffer(GL_ARRAY_BUFFER,buffer == -1 ? m_vbo.id() : buffer);
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location,size,type,normalized,sizeof(vType),pointer);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
}

template <typename vType>
void PrimitiveData<vType>::attribIPointer(
        GLuint         location,   // Location of attribute in vertex array  
        GLint          size,       // Number of components in attribute (i.e., for vec4 this would be 4).
        GLenum         type,       // Data type.
        const void*    pointer     // Byte offset from beginning of VBO to first occurence of this attribute.
    )
{
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER,m_vbo.id());
    glEnableVertexAttribArray(location);
    glVertexAttribIPointer(location,size,type,sizeof(vType),pointer);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
}

template <typename vType>
void PrimitiveData<vType>::attribLPointer(
        GLuint         location,   // Location of attribute in vertex array  
        GLint          size,       // Number of components in attribute (i.e., for vec4 this would be 4).
        GLenum         type,       // Data type.
        const void*    pointer     // Byte offset from beginning of VBO to first occurence of this attribute.
    )
{
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER,m_vbo.id());
    glEnableVertexAttribArray(location);
    glVertexAttribLPointer(location,size,type,sizeof(vType),pointer);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
}

template <typename vType>
MultiIndex* PrimitiveData<vType>::getMultiDrawIndices()
{
    return &m_multiIndex;
}
template <typename vType>
void PrimitiveData<vType>::setMultiDrawIndices(const MultiIndex& indices)
{
    this->m_multiIndex = indices;
}

template <typename vType>
void PrimitiveData<vType>::setMultiDrawIndices(MultiIndex&& indices)
{
    this->m_multiIndex = std::move(indices);
}

#endif


