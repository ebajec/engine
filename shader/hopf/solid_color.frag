#version 430 core

in vec4 fcolor;
in vec3 fpos;
in vec3 fnormal;
out vec4 FragColor;

void main()
{   
    vec4 color = fcolor;
    FragColor = color;
} 