#version 460 core
in vec3 Normal;
in vec3 FragPos;

uniform vec3 lightDir;     //광원 "방향" (정규화된 상태로 넘겨준다)
uniform vec3 objectColor;

out vec4 FragColor;

void main()
{
    vec3 n = normalize(Normal);
    float diff = max(dot(n, -lightDir), 0.0);

    //ambient를 좀 넉넉히 줘서 뒷면도 완전히 새까맣게는 안 보이게
    vec3 result = (0.25 + 0.75 * diff) * objectColor;
    FragColor = vec4(result, 1.0);
}
