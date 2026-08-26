#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

//기즈모 정점은 CPU에서 이미 월드 공간 좌표로 만들어 올린다(오브젝트별 모델 행렬이 필요 없다) —
//그래서 여기선 view/projection만 곱한다. 조명도 받지 않는 단색이라 노멀도 없다.
uniform mat4 viewProj;

out vec3 vColor;

void main()
{
    gl_Position = viewProj * vec4(aPos, 1.0);
    vColor = aColor;
}
