#pragma once
#ifndef SHADER_H
#define SHADER_H

#include <GL/glew.h> 
#include <GLFW/glfw3.h> 
#include <string>
#include <unordered_map>
#include <vector>
#include "defines.h"

struct ShaderProgram 
{
	GLuint id;

	void use() {glUseProgram(id);}

	void setUniform(const char* name, int value);
	void setUniform(const char* name, unsigned int value);
	void setUniform(const char* name, float value);
	void setUniform(const char* name, vec3 value);
	void setUniform(const char* name, mat3 value, GLboolean transpose);
	void setUniform(const char* name, mat4 value, GLboolean transpose);

	GLint getUniform(const char* name) const { return glGetUniformLocation(id, name); }

	void dispatchCompute(const uint countX, const uint countY, const uint countZ);
};

/**
 * Stores a record of compiled shaders and linked programs. 
 */
class ShaderManager
{

public:
	ShaderManager() {}

	/**
	 * Create a ShaderManager which compiles all files in a given directory.
	 */	
	ShaderManager(const std::string& shaderPath);
	~ShaderManager() {}

	bool addShader(const std::string& path);
	bool addShader(const std::string& name, const std::string& path);
	bool addProgram(const std::string& name, const std::vector<std::string>& shaders);

	ShaderProgram& program(const std::string& name);
private:
	GLuint compileShader(GLenum type, const char* source);

	std::unordered_map<std::string, GLuint> m_shaders;
	std::unordered_map<std::string, ShaderProgram> m_programs;
};




#endif

