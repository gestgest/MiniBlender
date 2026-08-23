#version 460 core
layout (location = 0) in vec3 aPos;

//mvp를 통째로 받지 않고 둘로 쪼갠 이유: 정점을 "뷰 공간에서" 카메라 쪽으로 살짝 당기기 위해서다.
//정점 점은 면 위에 정확히 얹혀 있어서, 깊이 테스트를 켜면 자기가 붙어 있는 면과 z-파이팅으로 깜빡인다.
uniform mat4 modelView;
uniform mat4 projection;
uniform float pointSize;

//뷰 공간 단위(=월드 스케일)로 밀어내는 양. 0이면 밀지 않는다.
//NDC나 윈도우 깊이에서 밀지 않는 이유: 깊이 버퍼가 비선형이라 같은 값이
//가까이서는 0.001, 멀리서는 몇 미터가 된다. 뷰 공간에서 밀면 거리와 무관하게 일정하다.
uniform float depthNudge;

void main()
{
    vec4 viewPos = modelView * vec4(aPos, 1.0);
    viewPos.z += depthNudge;   //뷰 공간은 -z가 앞쪽이므로 +z가 카메라 쪽
    gl_Position = projection * viewPos;

    //gl_PointSize를 셰이더에서 쓰려면 CPU 쪽에서 GL_PROGRAM_POINT_SIZE를 켜야 한다
    gl_PointSize = pointSize;
}
