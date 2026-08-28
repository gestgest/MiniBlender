#version 460 core

out vec4 FragColor;

void main()
{
    //선택된 정점(주황, RenderEditPoints)과 같은 색 계열의 반투명 칠 — "여기가 선택된 면이다"를
    //가리는 것 없이 보여준다. 고정값이라 uniform도 필요 없다.
    FragColor = vec4(1.0, 0.65, 0.1, 0.35);
}
