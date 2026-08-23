#pragma once

class Scene;
class OrbitCamera;

//뷰포트 클릭으로 오브젝트 고르기 (블렌더의 좌클릭 선택).
//
//두 단계로 나눈 이유:
//  1) 경계 상자(AABB)로 후보만 추린다 — 오브젝트당 나눗셈 몇 번이라 1000개를 훑어도 공짜다.
//  2) 가까운 후보부터 삼각형을 실제로 맞혀 본다.
//상자만 믿으면 구 옆의 빈 모서리를 눌러도 잡히고, ㄱ자 모델은 오목한 안쪽이 통째로 잡힌다.
//반대로 처음부터 모든 삼각형을 훑으면 큐브 1000개짜리 씬에서 클릭 한 번이 눈에 띄게 걸린다.
//
//맞은 오브젝트의 id를 돌려준다. 빈 공간이면 0 (= 선택 해제).
unsigned int PickObject(Scene& scene, const OrbitCamera& camera,
    float mouseX, float mouseY, int screenW, int screenH);
