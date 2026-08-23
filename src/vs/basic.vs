#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 objectColor;

out vec3 Normal;
out vec3 FragPos;
//색을 프래그먼트 셰이더로 직접 넘기지 않고 여기서 한 번 거쳐 보내는 이유:
//인스턴싱 버전은 색이 uniform이 아니라 버텍스 attribute로 들어온다.
//프래그먼트 셰이더가 "vColor"라는 같은 입구만 보게 해두면 basic.fs 하나를 둘 다 쓸 수 있다.
out vec3 vColor;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    //비균등 스케일에서도 노멀이 안 틀어지게 역전치 행렬 사용
    Normal = mat3(transpose(inverse(model))) * aNormal;
    vColor = objectColor;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
