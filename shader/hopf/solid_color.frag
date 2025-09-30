#version 430 core
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec4 fcolor;
layout (location = 1) in vec3 fpos;
layout (location = 2) in vec3 fnormal;
layout (location = 0) out vec4 FragColor;

void main()
{   
    vec4 color = fcolor;
    FragColor = color;
} 
