#version 460 core

//버텍스 버퍼가 없다! gl_VertexID만 가지고 화면을 덮는 삼각형 하나를 만들어낸다.
//VAO에 아무것도 안 붙여도 glDrawArrays(GL_TRIANGLES, 0, 3)만 하면 그려진다.
//
//왜 사각형(삼각형 2개)이 아니라 삼각형 1개인가:
//사각형은 대각선 이음매에서 GPU가 픽셀 쿼드를 두 번 처리해서 살짝 낭비가 생긴다.
//화면보다 큰 삼각형 하나로 덮으면 그 이음매가 없다.

uniform mat4 invViewProj;   //클립 공간 -> 월드 공간 역변환
uniform vec3 camPos;

out vec3 nearPoint;
out vec3 farPoint;

//클립 공간의 한 점을 월드 공간으로 되돌린다
vec3 UnprojectPoint(float x, float y, float z)
{
    vec4 unprojected = invViewProj * vec4(x, y, z, 1.0);
    return unprojected.xyz / unprojected.w;   //원근 나누기를 되돌리는 과정
}

void main()
{
    //(-1,-1), (3,-1), (-1,3) — 화면(NDC -1~1)을 완전히 덮는 큰 삼각형
    vec2 pos = vec2(
        (gl_VertexID == 1) ? 3.0 : -1.0,
        (gl_VertexID == 2) ? 3.0 : -1.0
    );

    //이 픽셀이 바라보는 광선의 시작점(near 평면)과 끝점(far 평면)을 월드 공간으로 구한다.
    //프래그먼트 셰이더에서 이 광선과 y=0 평면의 교점을 찾아 격자를 그린다.
    nearPoint = UnprojectPoint(pos.x, pos.y, -1.0);
    farPoint  = UnprojectPoint(pos.x, pos.y,  1.0);

    gl_Position = vec4(pos, 0.0, 1.0);
}
