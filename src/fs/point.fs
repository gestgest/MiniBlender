#version 460 core

uniform vec3 pointColor;

out vec4 FragColor;

void main()
{
    //gl_PointCoord는 점 내부 좌표(0~1). 중심에서의 거리로 사각형을 동그랗게 깎는다.
    vec2 d = gl_PointCoord - vec2(0.5);
    if (dot(d, d) > 0.25)
        discard;

    FragColor = vec4(pointColor, 1.0);
}
