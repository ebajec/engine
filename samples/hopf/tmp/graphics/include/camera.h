#pragma once
#ifndef CAMERA_H
#define CAMERA_H

#include "defines.h"
#include <GL/glew.h> 
#include <GLFW/glfw3.h> 

struct CameraUBOLayout
{
    mat4 view;
    mat4 proj;
	mat4 pv;
	vec4 cam_pos;
	vec4 cam_dir;
	float near;
	float far;
};

struct Camera {
public:
	Camera() {}
	Camera(
		vec3 normal,
		vec3 position,
		int w_screen = 1920,
		int h_screen = 1080,
		GLfloat fov = PI / 3,
		GLfloat near = 1,
		GLfloat far = 500
	);
	~Camera();

	Camera& operator=(const Camera& other);

	void bindUbo(GLuint binding) const;
	void rotate(float pitch, float yaw);
	void translate(vec3 delta,float speed);
	mat4 getViewMatrix() const;
	mat4 getProjMatrix() const;
	vec3 coord(int i) const {return vec3(coords[i]);}

	void resize(int width, int height);
	void updateUbo();

	float fov, aspect, near, far;
	vec3 position;
	mat4 coords;
	GLuint ubo;
};



#endif
