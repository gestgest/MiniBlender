#version 460 core
layout (location = 0) in vec3 aPos;      //정점마다 바뀐다 (divisor 0)
layout (location = 1) in vec3 aNormal;   //정점마다 바뀐다 (divisor 0)

//여기부터는 "인스턴스마다" 바뀐다 (divisor 1).
//mat4는 attribute 한 칸(vec4)에 안 들어가서 location 2,3,4,5 네 칸을 먹는다.
//그래서 색은 그다음인 6번부터 시작한다.
layout (location = 2) in mat4 aInstanceModel;
layout (location = 6) in vec3 aInstanceColor;

uniform mat4 view;
uniform mat4 projection;

out vec3 Normal;
out vec3 FragPos;
out vec3 vColor;

void main()
{
    //비인스턴싱 버전과 계산을 한 글자도 다르게 하지 않는다 —
    //model이 uniform에서 attribute로 바뀐 것뿐이다.
    //(여기서 셰이딩 방식까지 손대면 before/after 비교에서 무엇이 이득인지 알 수 없게 된다.
    // 특히 inverse()는 정점마다 도는 비싼 연산이라 한쪽만 빼면 GPU 시간 비교가 통째로 망가진다)
    FragPos = vec3(aInstanceModel * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(aInstanceModel))) * aNormal;
    vColor = aInstanceColor;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
