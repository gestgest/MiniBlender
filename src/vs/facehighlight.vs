#version 460 core
layout (location = 0) in vec3 aPos;

//point.vs와 같은 뷰공간 밀어내기 기법. 선택된 삼각형은 자기가 얹힌 면과 정확히 같은 위치라
//깊이 테스트를 그대로 켜면 z-파이팅으로 깜빡인다. 카메라 쪽으로 아주 조금 당겨서 확실히 이긴다.
uniform mat4 modelView;
uniform mat4 projection;
uniform float depthNudge;

void main()
{
    vec4 viewPos = modelView * vec4(aPos, 1.0);
    viewPos.z += depthNudge;   //뷰 공간은 -z가 앞쪽이므로 +z가 카메라 쪽
    gl_Position = projection * viewPos;
}
