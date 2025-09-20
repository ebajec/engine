#include <stdexcept>
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS

#include "shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#define MAX_LINE_LENGTH 100

namespace fs = std::filesystem;

inline std::string read_text_file(const char* src){
	std::ifstream file(src);

	if (!file.is_open()) {
		fprintf(stderr, "ERROR: could not open file: %s \n",src);
		return "";
	}

	std::string line;
	std::string text;

	while (std::getline(file,line)) {
		text.append(line).append("\n");
	}

	return text;
}

void ShaderProgram::dispatchCompute(const uint countX, const uint countY, const uint countZ)
{
    GLint localSizes[3];
    glGetProgramiv(id, GL_COMPUTE_WORK_GROUP_SIZE, localSizes);
    
    uint nGroupsX = (countX - 1)/localSizes[0] + 1;
    uint nGroupsY = (countY - 1)/localSizes[1] + 1;
    uint nGroupsZ = (countZ - 1)/localSizes[2] + 1;
    glDispatchCompute(nGroupsX,nGroupsY,nGroupsZ);
}

void ShaderProgram::setUniform(const char* name, int value)
{
	glUniform1i(this->getUniform(name), value);
}

void ShaderProgram::setUniform(const char* name, unsigned int value) {
	glUniform1ui(this->getUniform(name), value);
}

void ShaderProgram::setUniform(const char* name, float value)
{
	glUniform1f(this->getUniform(name), value);
}

void ShaderProgram::setUniform(const char* name, vec3 value)
{
	glUniform3fv(this->getUniform(name), 1, glm::value_ptr(value));
}

void ShaderProgram::setUniform(const char* name, mat3 value, GLboolean transpose)
{
	glUniformMatrix3fv(this->getUniform(name), 1, transpose, glm::value_ptr(value));
}

void ShaderProgram::setUniform(const char* name, mat4 value, GLboolean transpose)
{
	glUniformMatrix4fv(this->getUniform(name), 1, transpose, glm::value_ptr(value));
}

ShaderManager::ShaderManager(const std::string& shaderPath)
{
	fs::path path(shaderPath);

	for (const auto& entry : fs::directory_iterator(path))
	{
		this->addShader(entry.path().string());
	}
}

bool ShaderManager::addShader(const std::string& path)
{
	std::string name = fs::path(path).filename().string();
	
	if(addShader(name,path))
		return true;
	return false;
}

bool ShaderManager::addShader(const std::string& name, const std::string& path)
{
	std::string source = read_text_file(path.c_str());

	if (source == "") {
		fprintf(stderr, "ERROR: could not open shader file: %s \n", path.c_str());
		return false;
	}

	std::string ext = fs::path(path).extension().string();

	GLuint type = 0;

	if (ext == ".comp")
		type = GL_COMPUTE_SHADER;
	if (ext == ".vert")
		type = GL_VERTEX_SHADER;
	if (ext == ".frag")
		type = GL_FRAGMENT_SHADER;
	if (ext == ".geom")
		type = GL_GEOMETRY_SHADER;
	
	// TODO: parse files when given glsl or tess.

	if (!type) return false;
	
	GLuint shader = compileShader(type, source.c_str());

	//compile shaders
	if (m_shaders[name])
	{
		fprintf(stderr, "ERROR: shader already exists: %s \n", name.c_str());
		glDeleteShader(shader);
		return false;
	}

	if(shader)
	{
		m_shaders[name] = shader;
		return true;
	}
	return false;
}

bool ShaderManager::addProgram(const std::string& name, const std::vector<std::string>& shaders)
{
	GLuint program = glCreateProgram();

	for (auto& shaderName : shaders)
	{
		if (!m_shaders.count(shaderName))
			return false;

		GLuint shader = m_shaders[shaderName];

		glAttachShader(program,shader);
	}

	glLinkProgram(program);
	GLint success = 0;
	glGetShaderiv(program, GL_LINK_STATUS, &success);

	GLint length = 0;
	glGetShaderiv(program, GL_INFO_LOG_LENGTH, &length);

	if (length && success == GL_FALSE) 
	{	
		std::vector<GLchar> error_message(length);
		glGetShaderInfoLog(program, length, NULL, &error_message[0]);

		if (length)
			printf("%s", &error_message[0]);
		return false;
	}
	else 
	{
		m_programs[name] = {program};
		return true;
	}
}

ShaderProgram& ShaderManager::program(const std::string& name)
{
	if (m_programs.count(name))
		return m_programs[name];
	else 
		throw std::runtime_error("Program does not exist:" + name + "\n");
}

GLuint ShaderManager::compileShader(GLenum type, const char* source)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &source, NULL);
	glCompileShader(s);

	GLint success = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &success);

	if (success == GL_FALSE) {
		GLint length = 0;
		glGetShaderiv(s, GL_INFO_LOG_LENGTH, &length);

		std::vector<GLchar> error_message(length);
		glGetShaderInfoLog(s, length, NULL, &error_message[0]);

		if (length)
			printf("%s", &error_message[0]);

		glDeleteShader(s);
		return 0;
	}
	else 
	{	
		return s;
	}
}
