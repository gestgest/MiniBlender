#version 460 core

//무한 그리드. 격자를 지오메트리로 만들지 않고 프래그먼트에서 계산해서 그린다.
//
//지오메트리로 만들면: 범위가 유한하고(끝이 보임), 라인 수만큼 드로우콜/버텍스가 들고,
//멀어질수록 라인이 겹쳐서 지글거린다(에일리어싱).
//프래그먼트 방식이면: 드로우콜 1개, 버텍스 3개, 무한히 뻗고, fwidth로 안티에일리어싱까지 공짜.

in vec3 nearPoint;
in vec3 farPoint;

uniform mat4 viewProj;
uniform vec3 camPos;
uniform float farPlane;

out vec4 FragColor;

//격자 선 하나의 세기를 구한다. scale = 칸 크기(월드 단위)
float GridLine(vec3 worldPos, float scale)
{
    vec2 coord = worldPos.xz / scale;

    //fwidth = 이 픽셀과 옆 픽셀 사이에서 coord가 얼마나 변하는가 = 화면상 밀도.
    //이걸로 나눠주면 멀리 있어 촘촘한 곳에서도 선 두께가 픽셀 단위로 일정해진다. 이게 안티에일리어싱의 핵심.
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;

    float line = min(grid.x, grid.y);
    return 1.0 - min(line, 1.0);
}

void main()
{
    //광선과 y=0 평면의 교점을 구한다. t는 near에서 far까지의 비율.
    float t = -nearPoint.y / (farPoint.y - nearPoint.y);

    //t < 0 이면 교점이 카메라 뒤에 있다는 뜻 = 이 픽셀엔 바닥이 안 보임
    if (t < 0.0)
        discard;

    vec3 worldPos = nearPoint + t * (farPoint - nearPoint);

    //깊이를 직접 써줘야 오브젝트에 가려진 부분의 그리드가 제대로 숨는다.
    //(안 쓰면 풀스크린 삼각형의 깊이인 0이 그대로 들어가서 그리드가 항상 앞에 그려진다)
    vec4 clipPos = viewProj * vec4(worldPos, 1.0);
    gl_FragDepth = (clipPos.z / clipPos.w) * 0.5 + 0.5;

    //두 단계 격자: 1유닛 칸(얇게) + 10유닛 칸(굵게). 블렌더도 이렇게 한다.
    float small = GridLine(worldPos, 1.0);
    float big   = GridLine(worldPos, 10.0);

    vec3 color = vec3(0.32);
    float alpha = small * 0.35;

    if (big > 0.0)
    {
        color = vec3(0.42);
        alpha = max(alpha, big * 0.6);
    }

    //축선 강조: X축은 빨강, Z축은 파랑 (블렌더 컨벤션)
    float axisWidth = fwidth(worldPos.x) * 1.5;
    if (abs(worldPos.z) < axisWidth)
    {
        color = vec3(0.85, 0.25, 0.30);
        alpha = max(alpha, 0.9);
    }
    if (abs(worldPos.x) < fwidth(worldPos.z) * 1.5)
    {
        color = vec3(0.25, 0.45, 0.9);
        alpha = max(alpha, 0.9);
    }

    //멀리 갈수록 서서히 사라지게 (경계가 칼같이 잘리면 무한처럼 안 보인다)
    float distanceFade = 1.0 - smoothstep(farPlane * 0.15, farPlane * 0.45, length(worldPos - camPos));
    alpha *= distanceFade;

    if (alpha < 0.002)
        discard;

    FragColor = vec4(color, alpha);
}
