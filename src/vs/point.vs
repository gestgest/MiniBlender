#version 460 core
layout (location = 0) in vec3 aPos;

uniform mat4 mvp;
uniform float pointSize;

void main()
{
    gl_Position = mvp * vec4(aPos, 1.0);
    //gl_PointSize를 셰이더에서 쓰려면 CPU 쪽에서 GL_PROGRAM_POINT_SIZE를 켜야 한다
    gl_PointSize = pointSize;
}
