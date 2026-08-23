#version 460 core
in vec3 Normal;
in vec3 FragPos;
//색은 버텍스 셰이더에서 넘어온다. 비인스턴싱 경로는 uniform을, 인스턴싱 경로는
//인스턴스 attribute를 여기에 실어 보낸다 — 프래그먼트 셰이더는 둘을 구분할 필요가 없다.
in vec3 vColor;

uniform vec3 lightDir;     //광원 "방향" (정규화된 상태로 넘겨준다)

out vec4 FragColor;

void main()
{
    vec3 n = normalize(Normal);
    float diff = max(dot(n, -lightDir), 0.0);

    //ambient를 좀 넉넉히 줘서 뒷면도 완전히 새까맣게는 안 보이게
    vec3 result = (0.25 + 0.75 * diff) * vColor;
    FragColor = vec4(result, 1.0);
}
